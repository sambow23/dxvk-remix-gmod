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

#include <iostream>
#include <stdexcept>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_fork_hooks.h"
#include "../../../src/dxvk/rtx_render/rtx_lights.h"

namespace dxvk {
  Logger Logger::s_instance("test_external_light_sleep.log");
}

namespace {

dxvk::RtLight makeSphereLight(const dxvk::Vector3& position) {
  const dxvk::RtLightShaping shaping;
  const auto sphere = dxvk::RtSphereLight::tryCreate(
    position,
    dxvk::Vector3(1.0f, 0.8f, 0.6f),
    4.0f,
    shaping);
  if (!sphere.has_value()) {
    throw std::runtime_error("failed to create sphere light");
  }

  return dxvk::RtLight(*sphere);
}

void require(bool condition, const char* label) {
  if (!condition) {
    throw std::runtime_error(label);
  }
}

void copiesSleepingStaticLightWhenDefinitionChanges() {
  dxvk::RtLight current = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  dxvk::RtLight incoming = makeSphereLight(dxvk::Vector3(-16.0f, 0.0f, 0.0f));
  current.isStaticCount = 100;
  incoming.isDynamic = false;

  const dxvk::fork_hooks::StaticLightSleepUpdateDecision decision =
    dxvk::fork_hooks::decideStaticLightSleepUpdate(current, incoming, 2, false);

  require(decision.copyDefinition, "changed sleeping static light must copy the incoming definition");
  require(!decision.preserveBufferIdx, "changed sleeping static light invalidates stale previous-frame light mapping");
  require(decision.invalidateNrcHistory, "changed sleeping static light invalidates NRC world-space history");
  require(decision.nextStaticCount == 0, "changed sleeping static light restarts the static counter");
}

void keepsSleepingStaticLightWhenDefinitionIsUnchanged() {
  dxvk::RtLight current = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  dxvk::RtLight incoming = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  current.isStaticCount = 100;
  incoming.isDynamic = false;

  const dxvk::fork_hooks::StaticLightSleepUpdateDecision decision =
    dxvk::fork_hooks::decideStaticLightSleepUpdate(current, incoming, 2, false);

  require(!decision.copyDefinition, "unchanged sleeping static light remains asleep");
  require(decision.preserveBufferIdx, "unchanged sleeping static light keeps previous-frame light mapping");
  require(!decision.invalidateNrcHistory, "unchanged sleeping static light preserves NRC world-space history");
  require(decision.nextStaticCount == 101, "unchanged sleeping static light continues counting static frames");
}

void copiesAwakeStaticLightUntilItSleeps() {
  dxvk::RtLight current = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  dxvk::RtLight incoming = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  current.isStaticCount = 1;
  incoming.isDynamic = false;

  const dxvk::fork_hooks::StaticLightSleepUpdateDecision decision =
    dxvk::fork_hooks::decideStaticLightSleepUpdate(current, incoming, 2, false);

  require(decision.copyDefinition, "awake static light still copies the incoming definition");
  require(decision.preserveBufferIdx, "unchanged awake static light keeps previous-frame light mapping");
  require(!decision.invalidateNrcHistory, "unchanged awake static light preserves NRC world-space history");
  require(decision.nextStaticCount == 2, "awake static light increments the static counter");
}

void copiesChangedAwakeStaticLightWithoutInvalidatingNrcHistory() {
  dxvk::RtLight current = makeSphereLight(dxvk::Vector3(0.0f, 0.0f, 0.0f));
  dxvk::RtLight incoming = makeSphereLight(dxvk::Vector3(0.25f, 0.0f, 0.0f));
  current.isStaticCount = 1;
  incoming.isDynamic = false;

  const dxvk::fork_hooks::StaticLightSleepUpdateDecision decision =
    dxvk::fork_hooks::decideStaticLightSleepUpdate(current, incoming, 2, false);

  require(decision.copyDefinition, "changed awake static light copies the incoming definition");
  require(decision.preserveBufferIdx, "changed awake static light keeps previous-frame light mapping");
  require(!decision.invalidateNrcHistory, "changed awake static light does not reset NRC before it has gone to sleep");
  require(decision.nextStaticCount == 0, "changed awake static light restarts the static counter");
}

void externalApiHashOverridesInitialHashOnly() {
  constexpr XXH64_hash_t kApiHash = 0x5E01559543F46808ull;
  dxvk::RtLight light = makeSphereLight(dxvk::Vector3(960.5f, 63.920002f, 621.230469f));
  const XXH64_hash_t positionHash = light.getInitialHash();

  require(positionHash != kApiHash, "test API hash must differ from the position-derived light hash");

  light.setExternalApiHash(kApiHash);

  require(light.getInitialHash() == kApiHash, "external API hash must drive the displayed initial light identity");
  require(light.getTransformedHash() == positionHash, "external API hash must not hide definition changes");
}

void keepsApiTrackedSleepingStaticLightMappedWhenDefinitionRebases() {
  dxvk::RtLight current = makeSphereLight(dxvk::Vector3(-63.5f, 63.920002f, 621.230469f));
  dxvk::RtLight incoming = makeSphereLight(dxvk::Vector3(960.5f, 63.920002f, 621.230469f));
  current.isStaticCount = 100;
  incoming.isDynamic = false;

  const dxvk::fork_hooks::StaticLightSleepUpdateDecision decision =
    dxvk::fork_hooks::decideStaticLightSleepUpdate(current, incoming, 2, false, true);

  require(decision.copyDefinition, "API-tracked static light must copy rebased position updates");
  require(decision.preserveBufferIdx, "API-tracked rebased static light must keep previous-frame light mapping");
  require(!decision.invalidateNrcHistory, "API-tracked rebased static light must preserve NRC world-space history");
  require(decision.nextStaticCount == 0, "API-tracked changed static light restarts the static counter");
}

} // anonymous namespace

int main() {
  try {
    std::cout << "Begin external light sleep tests" << std::endl;
    copiesSleepingStaticLightWhenDefinitionChanges();
    keepsSleepingStaticLightWhenDefinitionIsUnchanged();
    copiesAwakeStaticLightUntilItSleeps();
    copiesChangedAwakeStaticLightWithoutInvalidatingNrcHistory();
    externalApiHashOverridesInitialHashOnly();
    keepsApiTrackedSleepingStaticLightMappedWhenDefinitionRebases();
    std::cout << "All external light sleep tests passed" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    throw;
  }

  return 0;
}
