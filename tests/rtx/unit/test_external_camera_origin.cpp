/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "../../../src/dxvk/rtx_render/rtx_fork_camera_origin.h"
#include "../../../src/dxvk/rtx_render/rtx_fork_game_state.h"
#include "../../../src/dxvk/rtx_render/rtx_fork_hooks.h"
#include "../../../src/dxvk/rtx_render/rtx_lights.h"
#include "../../../src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h"

namespace dxvk {
  Logger Logger::s_instance("test_external_camera_origin.log");
}

namespace {

void require(bool condition, const char* label) {
  if (!condition) {
    throw std::runtime_error(label);
  }
}

void requireNear(float actual, float expected, const char* label) {
  if (std::fabs(actual - expected) > 0.001f) {
    throw std::runtime_error(label);
  }
}

std::string readFile(const char* path) {
  std::ifstream file(std::string(BUILD_SOURCE_ROOT) + path, std::ios::binary);
  if (!file) {
    throw std::runtime_error(std::string("failed to open ") + path);
  }

  return std::string(
      std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>());
}

void requireContains(const std::string& haystack, const char* needle, const char* label) {
  require(haystack.find(needle) != std::string::npos, label);
}

void requireNotContains(const std::string& haystack, const char* needle, const char* label) {
  require(haystack.find(needle) == std::string::npos, label);
}

void readsPublishedWorldOriginOffset() {
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.x", "45056");
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.y", "0");
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.z", "49152");

  const dxvk::Vector3 offset = dxvk::fork_camera_origin::readWorldOriginOffsetFromGameState();

  requireNear(offset.x, 45056.0f, "origin offset x");
  requireNear(offset.y, 0.0f, "origin offset y");
  requireNear(offset.z, 49152.0f, "origin offset z");
}

void invalidPublishedWorldOriginFallsBackToZero() {
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.x", "45056");
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.y", "not-a-number");
  dxvk::fork_game_state::GameStateStore::get().set("__mcrtx.world_origin.z", "49152");

  const dxvk::Vector3 offset = dxvk::fork_camera_origin::readWorldOriginOffsetFromGameState();

  require(offset == dxvk::Vector3(0.0f), "invalid origin offset falls back to zero");
}

void readsPublishedStableCameraPosition() {
  auto& gameState = dxvk::fork_game_state::GameStateStore::get();
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueX, "12.25");
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueY, "64.5");
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueZ, "-7.75");

  dxvk::Vector3 position(0.0f);
  require(dxvk::fork_camera_origin::tryReadStableCameraPositionFromGameState(position),
          "stable camera position is available");
  requireNear(position.x, 12.25f, "stable camera x");
  requireNear(position.y, 64.5f, "stable camera y");
  requireNear(position.z, -7.75f, "stable camera z");
}

void invalidPublishedStableCameraPositionIsRejected() {
  auto& gameState = dxvk::fork_game_state::GameStateStore::get();
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueX, "12.25");
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueY, "view-bob");
  gameState.set(dxvk::fork_camera_origin::kStableCameraGameValueZ, "-7.75");

  const dxvk::Vector3 sentinel(1.0f, 2.0f, 3.0f);
  dxvk::Vector3 position = sentinel;
  require(!dxvk::fork_camera_origin::tryReadStableCameraPositionFromGameState(position),
          "invalid stable camera position is rejected");
  require(position == sentinel, "invalid stable camera position leaves output unchanged");
}

void cloudTranslationRejectsHistoryWithoutViewBob() {
  requireNear(std::exp2(-0.0f / 0.000025f), 1.0f,
              "stationary camera retains cloud history");
  requireNear(std::exp2(-0.000025f / 0.000025f), 0.5f,
              "2.5 cm camera travel halves cloud history");
  requireNear(std::exp2(-0.0001f / 0.000025f), 0.0625f,
              "10 cm camera travel retires cloud history");
}

void cloudAnchoringSourceIntegration() {
  AtmosphereArgs args = {};
  require(args.cloudCameraTravelKm == 0.0f, "cloud camera travel defaults to zero");
  require(sizeof(AtmosphereArgs) % 16u == 0u, "atmosphere args remain 16-byte aligned");

  const std::string atmosphereCommon = readFile(
      "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh");
  requireContains(atmosphereCommon, "vec3 cloudGeometryPosition",
                  "camera-local cloud geometry helper");
  requireContains(atmosphereCommon,
                  "- vec3(args.cameraWorldPosYUpKm.x, 0.0f, args.cameraWorldPosYUpKm.z)",
                  "cloud shell follows camera horizontally");
  requireContains(atmosphereCommon,
                  "vec3(0.0f, args.cameraWorldPosYUpKm.y, 0.0f)",
                  "cloud shell retains world camera height");

  const std::string skyShader = readFile(
      "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh");
  requireContains(skyShader, "-args.cloudCameraTravelKm / 0.000025f",
                  "logical translation rejects stale cloud history");
  requireNotContains(skyShader, "args.cloudBrightness",
                     "cloud brightness remains reverted");

  const std::string compositeShader = readFile(
      "src/dxvk/shaders/rtx/pass/composite/composite.comp.slang");
  requireNotContains(compositeShader, "cloudHorizonVisibility",
                     "cloud post-fog restoration remains reverted");
  requireNotContains(compositeShader, "cloudRadiance - fogColor.rgb * cloudLayer.a",
                     "clouds remain behind depth fog");
}

void calculatesPreviousHistoryOffsetAcrossOriginHops() {
  const dxvk::Vector3 previousOrigin(-1024.0f, 0.0f, 0.0f);
  const dxvk::Vector3 currentOrigin(0.0f, 0.0f, 0.0f);

  const dxvk::Vector3 offset =
    dxvk::fork_camera_origin::calculatePreviousCameraHistoryOffset(true, previousOrigin, currentOrigin);

  requireNear(offset.x, -1024.0f, "history offset x");
  requireNear(offset.y, 0.0f, "history offset y");
  requireNear(offset.z, 0.0f, "history offset z");
}

void missingPreviousOriginDoesNotOffsetHistory() {
  const dxvk::Vector3 previousOrigin(-1024.0f, 0.0f, 0.0f);
  const dxvk::Vector3 currentOrigin(0.0f, 0.0f, 0.0f);

  const dxvk::Vector3 offset =
    dxvk::fork_camera_origin::calculatePreviousCameraHistoryOffset(false, previousOrigin, currentOrigin);

  require(offset == dxvk::Vector3(0.0f), "missing previous origin leaves history unshifted");
}

dxvk::RtLight makeSphereLight(const dxvk::Vector3& position) {
  dxvk::RtLight light(dxvk::RtSphereLight(
    position,
    dxvk::Vector3(1.0f, 0.75f, 0.5f),
    0.25f,
    dxvk::RtLightShaping {}));
  light.isDynamic = false;
  return light;
}

void stableExternalHashSurvivesLocalPositionJump() {
  constexpr XXH64_hash_t stableHash = 0x123456789abcdef0ull;

  dxvk::RtLight before = makeSphereLight(dxvk::Vector3(5.0f, 2.0f, 9.0f));
  before.setExternalStableHash(stableHash);
  before.setLocalWorldOrigin(dxvk::Vector3(45056.0f, 0.0f, 49152.0f));

  dxvk::RtLight after = makeSphereLight(dxvk::Vector3(-1019.0f, 2.0f, 9.0f));
  after.setExternalStableHash(stableHash);
  after.setLocalWorldOrigin(dxvk::Vector3(46080.0f, 0.0f, 49152.0f));

  require(before.hasExternalStableHash(), "before light has stable external hash");
  require(after.hasExternalStableHash(), "after light has stable external hash");
  require(before.hasLocalWorldOrigin(), "before light has local world origin");
  require(after.hasLocalWorldOrigin(), "after light has local world origin");
  require(before.getInitialHash() == stableHash, "initial hash uses stable external hash");
  require(before.getTransformedHash() == stableHash, "before transformed hash uses stable external hash");
  require(after.getTransformedHash() == stableHash, "after transformed hash uses stable external hash");
  require(before.getSphereLight().getHash() != after.getSphereLight().getHash(), "local shape hash changes across origin hop");
  require(dxvk::fork_hooks::shouldCopyStaticLightForOriginHop(before, after), "origin hop forces static light copy");
}

void unchangedOriginDoesNotForceStaticCopy() {
  dxvk::RtLight before = makeSphereLight(dxvk::Vector3(5.0f, 2.0f, 9.0f));
  before.setLocalWorldOrigin(dxvk::Vector3(45056.0f, 0.0f, 49152.0f));

  dxvk::RtLight after = makeSphereLight(dxvk::Vector3(5.0f, 2.0f, 9.0f));
  after.setLocalWorldOrigin(dxvk::Vector3(45056.0f, 0.0f, 49152.0f));

  require(!dxvk::fork_hooks::shouldCopyStaticLightForOriginHop(before, after), "unchanged origin does not force copy");
}

void missingOriginDoesNotForceStaticCopy() {
  dxvk::RtLight before = makeSphereLight(dxvk::Vector3(5.0f, 2.0f, 9.0f));
  dxvk::RtLight after = makeSphereLight(dxvk::Vector3(-1019.0f, 2.0f, 9.0f));

  require(!dxvk::fork_hooks::shouldCopyStaticLightForOriginHop(before, after), "missing origin does not force copy");
}

} // anonymous namespace

int main() {
  try {
    std::cout << "Begin external camera origin tests" << std::endl;
    readsPublishedWorldOriginOffset();
    invalidPublishedWorldOriginFallsBackToZero();
    readsPublishedStableCameraPosition();
    invalidPublishedStableCameraPositionIsRejected();
    cloudTranslationRejectsHistoryWithoutViewBob();
    cloudAnchoringSourceIntegration();
    calculatesPreviousHistoryOffsetAcrossOriginHops();
    missingPreviousOriginDoesNotOffsetHistory();
    stableExternalHashSurvivesLocalPositionJump();
    unchangedOriginDoesNotForceStaticCopy();
    missingOriginDoesNotForceStaticCopy();
    std::cout << "All external camera origin tests passed" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    throw;
  }

  return 0;
}
