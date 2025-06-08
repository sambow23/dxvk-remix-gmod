/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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
#include <locale>
#include <codecvt>
#include <cassert>

#include "rtx.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "dxvk_device.h"
#include "rtx_fsr3.h"
#include "rtx_fsr3_wrapper.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"

#include "rtx_matrix_helpers.h"

namespace dxvk {
  const char* fsr3ProfileToString(FSR3Profile fsr3Profile) {
    switch (fsr3Profile) {
    case FSR3Profile::UltraPerf: return "Ultra Performance";
    case FSR3Profile::MaxPerf: return "Max Performance";
    case FSR3Profile::Balanced: return "Balanced";
    case FSR3Profile::MaxQuality: return "Max Quality";
    case FSR3Profile::Auto: return "Auto";
    case FSR3Profile::FullResolution: return "Full Resolution";
    default:
      assert(false);
    case FSR3Profile::Invalid: return "Invalid";
    }
  }

  DxvkFSR3::DxvkFSR3(DxvkDevice* device) : CommonDeviceObject(device), RtxPass(device) {
    // Create FSR3 context
    if (!m_fsr3Context) {
      m_fsr3Context = device->getCommon()->metaFSR3Context().createFSR3UpscalerContext();
      if (!m_fsr3Context) {
        Logger::warn("[FSR3] Failed to create FSR3 upscaler context");
      }
    }
  }

  DxvkFSR3::~DxvkFSR3() { }

  void DxvkFSR3::onDestroy() {
    if (m_fsr3Context) {
      m_fsr3Context->releaseFSR3Feature();
    }
    m_fsr3Context = nullptr;
  }

  void DxvkFSR3::release() {
    mRecreate = true;

    if (m_fsr3Context) {
      m_fsr3Context->releaseFSR3Feature();
    }
  }

  // FSR3 quality mapping - following FSR conventions
  uint32_t DxvkFSR3::profileToQualityMode(FSR3Profile profile) {
    switch (profile) {
    case FSR3Profile::UltraPerf: return 0;      // Ultra Performance
    case FSR3Profile::MaxPerf: return 1;        // Performance  
    case FSR3Profile::Balanced: return 2;       // Balanced
    case FSR3Profile::MaxQuality: return 3;     // Quality
    case FSR3Profile::FullResolution: return 4; // Full Resolution
    default: return 2; // Default to Balanced
    }
  }

  bool DxvkFSR3::supportsFSR3() const {
    return m_device->getCommon()->metaFSR3Context().supportsFSR3();
  }

  bool DxvkFSR3::isEnabled() const {
    return RtxOptions::isFSR3Enabled();
  }

  FSR3Profile DxvkFSR3::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
    FSR3Profile desiredProfile = FSR3Profile::UltraPerf;

    // Standard display resolution based FSR3 config
    if (displayHeight <= 1080) {
      desiredProfile = FSR3Profile::MaxQuality;
    } else if (displayHeight < 2160) {
      desiredProfile = FSR3Profile::Balanced;
    } else if (displayHeight < 4320) {
      desiredProfile = FSR3Profile::MaxPerf;
    } else {
      // For > 4k (e.g. 8k)
      desiredProfile = FSR3Profile::UltraPerf;
    }

    // Adjust based on graphics preset
    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias FSR3 more towards performance
      desiredProfile = (FSR3Profile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = (FSR3Profile) std::max(0, (int) desiredProfile - 2);
    }

    return desiredProfile;
  }

  void DxvkFSR3::setSetting(const uint32_t displaySize[2], const FSR3Profile profile, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    
    // Handle the "auto" case
    FSR3Profile actualProfile = profile;
    if (actualProfile == FSR3Profile::Auto) {
      actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
    }

    if (mActualProfile == actualProfile && displaySize[0] == mFSR3OutputSize[0] && displaySize[1] == mFSR3OutputSize[1]) {
      // Nothing changed that would alter FSR3 resolution(s), so return the last cached optimal render size
      outRenderSize[0] = mInputSize[0];
      outRenderSize[1] = mInputSize[1];
      return;
    }
    
    mActualProfile = actualProfile;

    // We need to force a recreation of resources before running FSR3
    mRecreate = true;

    // Update our requested profile
    mProfile = profile;

    if (mProfile == FSR3Profile::FullResolution) {
      mInputSize[0] = outRenderSize[0] = displaySize[0];
      mInputSize[1] = outRenderSize[1] = displaySize[1];
    } else {
      uint32_t qualityMode = profileToQualityMode(mActualProfile);
      
      if (!m_fsr3Context) {
        m_fsr3Context = m_device->getCommon()->metaFSR3Context().createFSR3UpscalerContext();
      }
      
      if (m_fsr3Context) {
        auto optimalSettings = m_fsr3Context->queryOptimalSettings(displaySize, qualityMode);

        // Align resolution to avoid artifacts (similar to DLSS-RR requirement)
        const int step = 32;
        optimalSettings.optimalRenderSize[0] = (optimalSettings.optimalRenderSize[0] + step - 1) / step * step;
        optimalSettings.optimalRenderSize[1] = (optimalSettings.optimalRenderSize[1] + step - 1) / step * step;
        
        mInputSize[0] = outRenderSize[0] = optimalSettings.optimalRenderSize[0];
        mInputSize[1] = outRenderSize[1] = optimalSettings.optimalRenderSize[1];
      } else {
        // Fallback to hardcoded ratios if FSR3 context creation failed
        float scalingRatio = 1.7f; // Default to Balanced
        switch (mActualProfile) {
        case FSR3Profile::UltraPerf: scalingRatio = 3.0f; break;
        case FSR3Profile::MaxPerf: scalingRatio = 2.3f; break;
        case FSR3Profile::Balanced: scalingRatio = 1.7f; break;
        case FSR3Profile::MaxQuality: scalingRatio = 1.3f; break;
        default: break;
        }
        
        mInputSize[0] = outRenderSize[0] = (uint32_t)(displaySize[0] / scalingRatio);
        mInputSize[1] = outRenderSize[1] = (uint32_t)(displaySize[1] / scalingRatio);
      }
    }

    mFSR3OutputSize[0] = displaySize[0];
    mFSR3OutputSize[1] = displaySize[1];
    
    Logger::info(str::format("[FSR3] Resolution set - Render: ", mInputSize[0], "x", mInputSize[1],
                            ", Display: ", mFSR3OutputSize[0], "x", mFSR3OutputSize[1],
                            ", Profile: ", fsr3ProfileToString(mActualProfile)));
  }

  FSR3Profile DxvkFSR3::getCurrentProfile() const {
    return mActualProfile;
  }

  void DxvkFSR3::getInputSize(uint32_t& width, uint32_t& height) const {
    width = mInputSize[0];
    height = mInputSize[1];
  }

  void DxvkFSR3::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = mFSR3OutputSize[0];
    height = mFSR3OutputSize[1];
  }

  void DxvkFSR3::dispatch(
    Rc<RtxContext> ctx,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    bool resetHistory)
  {
    ScopedGpuProfileZone(ctx, "FSR3");
    ctx->setFramePassStage(RtxFramePassStage::FSR3);

    if (mRecreate) {
      initializeFSR3(ctx);
      mRecreate = false;
    }

    if (!m_fsr3Context || !m_fsr3Context->isFSR3Initialized()) {
      Logger::warn("[FSR3] Cannot dispatch FSR3 - context not initialized");
      return;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    // Set up input textures
    std::vector<Rc<DxvkImageView>> pInputs = {
      rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
      rtOutput.m_primaryScreenSpaceMotionVector.view,
      rtOutput.m_primaryDepth.view
    };

    // Optional exposure input
    const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
    if (autoExposure.enabled() && autoExposure.getExposureTexture().image != nullptr) {
      pInputs.push_back(autoExposure.getExposureTexture().view);
    }

    std::vector<Rc<DxvkImageView>> pOutputs = {
      rtOutput.m_finalOutput.view(Resources::AccessType::Write)
    };

    // Set up barriers for input resources
    for (auto input : pInputs) {
      if (input == nullptr) {
        continue;
      }
      
      barriers.accessImage(
        input->image(),
        input->imageSubresources(),
        input->imageInfo().layout,
        input->imageInfo().stages,
        input->imageInfo().access,
        input->imageInfo().layout,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    }

    // Set up barriers for output resources
    for (auto output : pOutputs) {
      barriers.accessImage(
        output->image(),
        output->imageSubresources(),
        output->imageInfo().layout,
        output->imageInfo().stages,
        output->imageInfo().access,
        output->imageInfo().layout,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT);
    }

    barriers.recordCommands(ctx->getCommandList());

    // Prepare FSR3 dispatch parameters
    float jitterOffset[2];
    RtCamera& camera = sceneManager.getCamera();
    camera.getJittering(jitterOffset);
    
    float motionVectorScale[2] = { 1.0f, 1.0f };

    // Set up FSR3 buffers
    FSR3UpscalerContext::FSR3Buffers buffers = {};
    buffers.pColorBuffer = &rtOutput.m_compositeOutput.resource(Resources::AccessType::Read);
    buffers.pDepthBuffer = &rtOutput.m_primaryDepth;
    buffers.pMotionVectors = &rtOutput.m_primaryScreenSpaceMotionVector;
    buffers.pOutputBuffer = &rtOutput.m_finalOutput.resource(Resources::AccessType::Write);
    
    if (autoExposure.enabled() && autoExposure.getExposureTexture().image != nullptr) {
      buffers.pExposureBuffer = &autoExposure.getExposureTexture();
    }

    // Set up FSR3 settings
    FSR3UpscalerContext::FSR3Settings settings = {};
    settings.resetAccumulation = resetHistory;
    settings.sharpness = 0.8f; // Default sharpness
    settings.jitterOffset[0] = jitterOffset[0];
    settings.jitterOffset[1] = jitterOffset[1];
    settings.motionVectorScale[0] = motionVectorScale[0];
    settings.motionVectorScale[1] = motionVectorScale[1];
    settings.deltaTime = 16.67f; // ~60 FPS default
    settings.preExposure = 1.0f;
    settings.renderSize[0] = mInputSize[0];
    settings.renderSize[1] = mInputSize[1];
    settings.displaySize[0] = mFSR3OutputSize[0];
    settings.displaySize[1] = mFSR3OutputSize[1];

    // Dispatch FSR3
    bool success = m_fsr3Context->evaluateFSR3(ctx, buffers, settings);
    if (!success) {
      Logger::warn("[FSR3] FSR3 dispatch failed");
    }

    // Clean up barriers for output resources
    for (auto output : pOutputs) {
      barriers.accessImage(
        output->image(),
        output->imageSubresources(),
        output->imageInfo().layout,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        output->imageInfo().layout,
        output->imageInfo().stages,
        output->imageInfo().access);

      ctx->getCommandList()->trackResource<DxvkAccess::None>(output);
      ctx->getCommandList()->trackResource<DxvkAccess::Write>(output->image());
    }
    barriers.recordCommands(ctx->getCommandList());
  }

  void DxvkFSR3::showImguiSettings() {
    // FSR3-specific settings can be added here
    ImGui::Text("FSR3 Upscaling Active");
    if (m_fsr3Context && m_fsr3Context->isFSR3Initialized()) {
      ImGui::Text("Status: Initialized");
    } else {
      ImGui::Text("Status: Not Initialized");
    }
  }

  void DxvkFSR3::initializeFSR3(Rc<DxvkContext> renderContext) {
    // Wait for GPU to avoid racing conditions during reinitialization
    m_device->waitForIdle();

    if (!m_fsr3Context) {
      m_fsr3Context = m_device->getCommon()->metaFSR3Context().createFSR3UpscalerContext();
      if (!m_fsr3Context) {
        Logger::err("[FSR3] Failed to create FSR3 upscaler context");
        return;
      }
    }

    // Release any existing FSR3 feature
    m_fsr3Context->releaseFSR3Feature();

    uint32_t qualityMode = profileToQualityMode(mActualProfile);

    // Initialize FSR3 upscaler context
    m_fsr3Context->initialize(
      renderContext,
      mInputSize,
      mFSR3OutputSize,
      mIsHDR,
      mInverseDepth,
      qualityMode);

    Logger::info(str::format("[FSR3] FSR3 initialized - Profile: ", fsr3ProfileToString(mActualProfile),
                            ", Quality Mode: ", qualityMode,
                            ", HDR: ", mIsHDR ? "Yes" : "No"));
  }
} 