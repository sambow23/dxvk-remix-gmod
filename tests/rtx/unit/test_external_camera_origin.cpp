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
#include <iostream>
#include <stdexcept>

#include "../../../src/dxvk/rtx_render/rtx_fork_camera_origin.h"
#include "../../../src/dxvk/rtx_render/rtx_fork_game_state.h"

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

} // anonymous namespace

int main() {
  try {
    std::cout << "Begin external camera origin tests" << std::endl;
    readsPublishedWorldOriginOffset();
    invalidPublishedWorldOriginFallsBackToZero();
    std::cout << "All external camera origin tests passed" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    throw;
  }

  return 0;
}
