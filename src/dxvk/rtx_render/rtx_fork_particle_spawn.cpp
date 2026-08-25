// src/dxvk/rtx_render/rtx_fork_particle_spawn.cpp
//
// Fork-owned file. Implements the fork_hooks:: functions the upstream particle
// manager calls on its spawn path.
//
// Background (2026-08-23): weather precipitation spawned exactly zero particles
// under an API-driven integration. RtxParticleSystemManager::spawnParticles
// records the emitter as a slot index into InstanceManager::m_instances, but
// RtxParticleSystemManager::simulate consumes that index from inside
// SceneManager::prepareSceneData - which begins by running garbageCollection().
// InstanceManager::garbageCollection() removes instances by swapping the vector's
// back element into the freed slot, so any emitter that was appended late in the
// frame is precisely the element that moves. The camera-glued precipitation
// emitter is appended last (RtxContext::injectRTX submits it immediately before
// prepareSceneData) and, because its identity hash includes its object-to-world
// transform, it gets a brand new ReplacementInstance - and therefore a brand new
// RtInstance at the back of the vector - on every frame the camera or the wind
// moves. The recorded index was then always stale, writeSpawnContextsToGpu took
// its "I dont see this case being hit" branch, and zeroed the system's spawn
// count every frame with no log line.
//
// See docs/fork-touchpoints.md for the full fork-hooks catalogue.

#include "rtx_fork_hooks.h"

#include "rtx_constants.h"        // kInvalidInstanceId
#include "rtx_instance_manager.h" // RtInstance::getId
#include "rtx_precipitation.h"    // PrecipitationSystem::debugLogging

#include "../../util/log/log.h"
#include "../../util/util_global_time.h"
#include "../../util/util_once.h"
#include "../../util/util_string.h"

namespace dxvk {
namespace fork_hooks {

  namespace {
    // Roughly one line per call site per second. The spawn path runs every frame for
    // every emitter, so an ungated log makes the file unreadable within seconds.
    constexpr uint64_t kDiagnosticIntervalMs = 1000;

    bool diagnosticDue(uint64_t& lastEmittedMs) {
      const uint64_t nowMs = GlobalTime::get().absoluteTimeMs();
      if (lastEmittedMs != 0 && nowMs - lastEmittedMs < kDiagnosticIntervalMs) {
        return false;
      }
      lastEmittedMs = nowMs;
      return true;
    }
  }  // namespace

  // ---------------------------------------------------------------------------
  // resolveSpawnEmitterInstance
  //
  // The recorded vector index is only a hint: it is correct for as long as nothing
  // reindexes InstanceManager::m_instances between spawn time and here, which is
  // not something the spawn path can promise. Validate it against the stable
  // RtInstance id and fall back to a scan when it has drifted.
  //
  // The scan is bounded by the instance count and runs at most once per spawn
  // context (typically one or two per frame), so it does not show up in a profile.
  // ---------------------------------------------------------------------------
  const RtInstance* resolveSpawnEmitterInstance(
      const std::vector<RtInstance*>& instanceTable,
      uint32_t recordedVectorIdx,
      uint64_t recordedInstanceUid) {
    const bool hasUid = recordedInstanceUid != kInvalidInstanceId;

    // Fast path: the index still points at the instance that was recorded.
    if (recordedVectorIdx < instanceTable.size()) {
      const RtInstance* candidate = instanceTable[recordedVectorIdx];
      if (candidate != nullptr && (!hasUid || candidate->getId() == recordedInstanceUid)) {
        return candidate;
      }
    }

    // Instances created by the renderer (view model / player model copies) can carry
    // kInvalidInstanceId, and those are not identifiable by id. Nothing to recover.
    if (!hasUid) {
      return nullptr;
    }

    for (const RtInstance* candidate : instanceTable) {
      if (candidate != nullptr && candidate->getId() == recordedInstanceUid) {
        if (PrecipitationSystem::debugLogging()) {
          static uint64_t s_lastEmittedMs = 0;
          if (diagnosticDue(s_lastEmittedMs)) {
            Logger::info(str::format(
              "[RTX Precipitation] spawn context index drifted: recorded slot ", recordedVectorIdx,
              " now holds a different instance; recovered emitter id ", recordedInstanceUid,
              " at slot ", candidate->getVectorIdx(),
              " (instance table size ", instanceTable.size(), ")"));
          }
        }
        return candidate;
      }
    }

    // Unlike the drift case above this is a real loss - the spawn is discarded - so say so
    // once per process even when the debug option is off. It used to be entirely silent.
    ONCE(Logger::warn(str::format(
      "[RTX Particles] spawn context dropped: emitter instance id ", recordedInstanceUid,
      " (recorded slot ", recordedVectorIdx, ") is no longer in the instance table (size ",
      instanceTable.size(), "); this frame's spawn is discarded")));

    return nullptr;
  }

  // ---------------------------------------------------------------------------
  // particleSpawnDiagnostic
  //
  // One throttled line per outcome of RtxParticleSystemManager::spawnParticles.
  // Reaching this at all proves the emitter survived every step between
  // PrecipitationSystem::submit and SceneManager::processDrawCallState, which is
  // the half of the pipeline that otherwise fails without logging anything.
  // ---------------------------------------------------------------------------
  void particleSpawnDiagnostic(
      const char* outcome,
      uint32_t instanceIdx,
      uint64_t instanceUid,
      uint32_t numParticles,
      uint32_t maxParticles) {
    if (!PrecipitationSystem::debugLogging()) {
      return;
    }

    static uint64_t s_lastEmittedMs = 0;
    if (!diagnosticDue(s_lastEmittedMs)) {
      return;
    }

    Logger::info(str::format(
      "[RTX Precipitation] spawnParticles: ", outcome,
      " (emitter slot ", instanceIdx, ", id ", instanceUid,
      ", particles ", numParticles, "/", maxParticles, ")"));
  }

}  // namespace fork_hooks
}  // namespace dxvk
