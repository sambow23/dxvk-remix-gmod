// src/dxvk/rtx_render/rtx_fork_light.cpp
//
// Fork-owned file. Contains the implementations of fork_hooks:: functions
// for the LightManager external-light and persistent-light paths.

#include "rtx_fork_hooks.h"

#include "rtx_light_manager.h"
#include "rtx_lights.h"
#include "rtx_options.h"
#include "rtx_types.h"

namespace dxvk {
namespace fork_hooks {

  void flushPendingLightMutations(LightManager& mgr) {
    for (auto h : mgr.m_pendingExternalLightErases) {
      auto it = mgr.m_externalLights.find(h);
      if (it != mgr.m_externalLights.end()) {
        it->second.getPrimInstanceOwner().setReplacementInstance(
          nullptr, ReplacementInstance::kInvalidReplacementIndex,
          &it->second, PrimInstance::Type::Light);
        mgr.m_externalLights.erase(it);
      }

      mgr.m_externalDomeLights.erase(h);
      mgr.m_externalActiveLightList.erase(h);
      if (mgr.m_externalActiveDomeLight == h) {
        mgr.m_externalActiveDomeLight = nullptr;
      }
    }
    mgr.m_pendingExternalLightErases.clear();

    for (auto& upd : mgr.m_pendingExternalLightUpdates) {
      auto it = mgr.m_externalLights.find(upd.first);
      if (it != mgr.m_externalLights.end()) {
        it->second.getPrimInstanceOwner().setReplacementInstance(
          nullptr, ReplacementInstance::kInvalidReplacementIndex,
          &it->second, PrimInstance::Type::Light);
        mgr.m_externalLights.erase(it);
      }

      auto [itNew, inserted] = mgr.m_externalLights.emplace(upd.first, upd.second);
      itNew->second.setFrameLastTouched(mgr.device()->getCurrentFrameId());
      (void)inserted;
    }
    mgr.m_pendingExternalLightUpdates.clear();

    for (auto h : mgr.m_pendingExternalActiveLights) {
      if (mgr.m_externalLights.find(h) != mgr.m_externalLights.end()) {
        mgr.m_externalActiveLightList.insert(h);
      } else if (mgr.m_externalDomeLights.find(h) != mgr.m_externalDomeLights.end()
              && mgr.m_externalActiveDomeLight == nullptr) {
        mgr.m_externalActiveDomeLight = h;
      }
    }
    mgr.m_pendingExternalActiveLights.clear();

    for (auto h : mgr.m_persistentExternalLights) {
      if (mgr.m_externalLights.find(h) != mgr.m_externalLights.end()) {
        mgr.m_externalActiveLightList.insert(h);
      } else if (mgr.m_externalDomeLights.find(h) != mgr.m_externalDomeLights.end()
              && mgr.m_externalActiveDomeLight == nullptr) {
        mgr.m_externalActiveDomeLight = h;
      }
    }
  }

  void updateLightStaticSleep(
      RtLight* light,
      const RtLight& newLight,
      DxvkDevice* device,
      uint64_t externalId) {
    const uint16_t bufferIdx = light->getBufferIdx();

    if (!newLight.isDynamic && !LightManager::suppressLightKeeping()) {
      const uint32_t isStaticCount = light->isStaticCount;

      if (isStaticCount < RtxOptions::getNumFramesToPutLightsToSleep()) {
        *light = newLight;
        light->setBufferIdx(bufferIdx);
        if (externalId != kInvalidExternallyTrackedLightId) {
          light->setExternallyTrackedLightId(externalId);
        }
        light->isStaticCount = isStaticCount + 1;
      } else {
        light->isStaticCount = isStaticCount + 1;
      }
    } else {
      *light = newLight;
      light->setBufferIdx(bufferIdx);
      if (externalId != kInvalidExternallyTrackedLightId) {
        light->setExternallyTrackedLightId(externalId);
      }
    }

    light->setFrameLastTouched(device->getCurrentFrameId());
  }

  void setExternalLightEmplace(
      LightManager& mgr,
      remixapi_LightHandle handle,
      const RtLight& rtlight) {
    auto [it, inserted] = mgr.m_externalLights.emplace(handle, rtlight);
    if (inserted) {
      it->second.setFrameLastTouched(mgr.device()->getCurrentFrameId());
    }
  }

  void disableExternalLightQueue(LightManager& mgr, remixapi_LightHandle handle) {
    mgr.m_pendingExternalLightErases.push_back(handle);
  }

  void registerPersistentLight(LightManager& mgr, remixapi_LightHandle handle) {
    if (handle) {
      mgr.m_persistentExternalLights.insert(handle);
    }
  }

  void unregisterPersistentLight(LightManager& mgr, remixapi_LightHandle handle) {
    if (handle) {
      mgr.m_persistentExternalLights.erase(handle);
    }
  }

  void queueAutoInstancePersistent(LightManager& mgr) {
    for (auto h : mgr.m_persistentExternalLights) {
      mgr.m_pendingExternalActiveLights.insert(h);
    }
  }

} // namespace fork_hooks
} // namespace dxvk