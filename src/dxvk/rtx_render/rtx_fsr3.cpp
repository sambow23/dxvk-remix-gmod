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
    // TODO: Initialize FSR3 context when SDK is integrated
    // For now, just mark as needing recreation
    mRecreate = true;
  }

  DxvkFSR3::~DxvkFSR3() { }

  void DxvkFSR3::onDestroy() {
    // TODO: Cleanup FSR3 context/resources when SDK is integrated
  }

  void DxvkFSR3::release() {
    mRecreate = true;
    // TODO: Release FSR3 context when SDK is integrated
  }

  // FSR3 quality mapping - following FSR conventions
  uint32_t DxvkFSR3::profileToQualityMode(FSR3Profile profile) {
    uint32_t qualityMode = 2; // Balanced as default
    switch (profile)
    {
    case FSR3Profile::UltraPerf: qualityMode = 1; break;  // Ultra Performance
    case FSR3Profile::MaxPerf: qualityMode = 2; break;    // Performance  
    case FSR3Profile::Balanced: qualityMode = 3; break;   // Balanced
    case FSR3Profile::MaxQuality: qualityMode = 4; break; // Quality
    case FSR3Profile::FullResolution: qualityMode = 4; break; // Use Quality mode for full res
    }
    return qualityMode;
  }

  bool DxvkFSR3::supportsFSR3() const {
    // TODO: Check FSR3 SDK availability and hardware support
    // For now, assume supported on modern AMD/NVIDIA hardware
    return true;
  }

  bool DxvkFSR3::isEnabled() const {
    return RtxOptions::isFSR3Enabled(); // TODO: Add this option to RtxOptions
  }

  FSR3Profile DxvkFSR3::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
    FSR3Profile desiredProfile = FSR3Profile::UltraPerf;

    // FSR3 auto profile selection based on resolution
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

    // Adjust based on graphics preset (similar to DLSS)
    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      desiredProfile = (FSR3Profile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
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
    mRecreate = true;
    mProfile = profile;

    if (mProfile == FSR3Profile::FullResolution) {
      mInputSize[0] = outRenderSize[0] = displaySize[0];
      mInputSize[1] = outRenderSize[1] = displaySize[1];
    } else {
      // Calculate FSR3 scaling ratios based on profile
      float scalingRatio = 1.0f;
      switch (mActualProfile) {
        case FSR3Profile::UltraPerf: scalingRatio = 3.0f; break;    // 33% render scale
        case FSR3Profile::MaxPerf: scalingRatio = 2.3f; break;      // ~43% render scale  
        case FSR3Profile::Balanced: scalingRatio = 1.7f; break;     // ~59% render scale
        case FSR3Profile::MaxQuality: scalingRatio = 1.3f; break;   // ~77% render scale
        default: scalingRatio = 1.7f; break; // Default to Balanced
      }

      // Calculate input resolution
      uint32_t inputWidth = (uint32_t)(displaySize[0] / scalingRatio);
      uint32_t inputHeight = (uint32_t)(displaySize[1] / scalingRatio);

      // Align to 32-pixel boundaries for better performance (similar to DLSS-RR)
      const int step = 32;
      inputWidth = (inputWidth + step - 1) / step * step;
      inputHeight = (inputHeight + step - 1) / step * step;

      mInputSize[0] = outRenderSize[0] = inputWidth;
      mInputSize[1] = outRenderSize[1] = inputHeight;
    }

    mFSR3OutputSize[0] = displaySize[0];
    mFSR3OutputSize[1] = displaySize[1];
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
    ctx->setFramePassStage(RtxFramePassStage::FSR3); // TODO: Add this enum value

    if (mRecreate) {
      initializeFSR3(ctx);
      mRecreate = false;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    // Setup inputs similar to DLSS
    float jitterOffset[2];
    RtCamera& camera = sceneManager.getCamera();
    camera.getJittering(jitterOffset);
    mMotionVectorScale = MotionVectorScale::Absolute;

    float motionVectorScale[2] = { 1.f, 1.f };

    // FSR3 input resources
    std::vector<Rc<DxvkImageView>> pInputs = {
      rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
      rtOutput.m_primaryScreenSpaceMotionVector.view,
      rtOutput.m_primaryDepth.view,
      rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read),
      rtOutput.m_primaryAlbedo.view
    };

    // FSR3 output resources
    std::vector<Rc<DxvkImageView>> pOutputs = {
      rtOutput.m_finalOutput.view(Resources::AccessType::Write)
    };

    // Setup barriers for inputs
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

    // Setup barriers for outputs
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

    // TODO: FSR3 SDK dispatch call would go here
    // Placeholder structure for FSR3 parameters:
    /*
    FSR3DispatchParameters fsr3Params = {};
    fsr3Params.commandList = ctx->getCommandList();
    fsr3Params.colorBuffer = &rtOutput.m_compositeOutput;
    fsr3Params.depthBuffer = &rtOutput.m_primaryDepth;
    fsr3Params.motionVectors = &rtOutput.m_primaryScreenSpaceMotionVector;
    fsr3Params.outputBuffer = &rtOutput.m_finalOutput;
    fsr3Params.jitterOffset[0] = jitterOffset[0];
    fsr3Params.jitterOffset[1] = jitterOffset[1];
    fsr3Params.motionVectorScale[0] = motionVectorScale[0];
    fsr3Params.motionVectorScale[1] = motionVectorScale[1];
    fsr3Params.resetAccumulation = resetHistory;
    fsr3Params.sharpness = mSharpness;
    fsr3Params.enableReactiveMask = mReactiveMaskEnabled;
    fsr3Params.frameTimeDelta = frameTime; // TODO: Get from frame timing
    
    // Dispatch FSR3
    dispatchFSR3(fsr3Params);
    */

    // For now, just copy input to output as placeholder
    // TODO: Remove this when FSR3 SDK is integrated
    Logger::warn("FSR3: Using placeholder passthrough - FSR3 SDK not integrated yet");

    // Setup output barriers
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
    ImGui::SliderFloat("Sharpness", &mSharpness, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Reactive Mask", &mReactiveMaskEnabled);
    
    // TODO: Add more FSR3-specific settings when SDK is integrated
    ImGui::Text("FSR3 SDK Status: Not Integrated (Placeholder)");
  }

  void DxvkFSR3::initializeFSR3(Rc<DxvkContext> renderContext) {
    // Wait for GPU idle to prevent racing conditions during reinitialization
    m_device->waitForIdle();

    // TODO: Initialize FSR3 context with SDK
    /*
    FSR3InitParams initParams = {};
    initParams.renderContext = renderContext;
    initParams.inputSize[0] = mInputSize[0];
    initParams.inputSize[1] = mInputSize[1];
    initParams.outputSize[0] = mFSR3OutputSize[0];
    initParams.outputSize[1] = mFSR3OutputSize[1];
    initParams.isHDR = mIsHDR;
    initParams.qualityMode = profileToQualityMode(mActualProfile);
    
    // Initialize FSR3
    initializeFSR3Context(initParams);
    */

    Logger::info("FSR3: Initialized with placeholder implementation");
  }
} 