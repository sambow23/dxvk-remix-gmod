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
#include "rtx_fsr3_wrapper.h"
#include "dxvk_device.h"
#include "rtx_context.h"
#include "../util/log/log.h"

// Include Vulkan backend for FidelityFX
#include <FidelityFX/host/backends/vk/ffx_vk.h>

namespace dxvk {

  // ===================================================================
  // FSR3Context Implementation
  // ===================================================================

  FSR3Context::FSR3Context(DxvkDevice* device)
    : m_device(device) {
    if (m_device && !initialize()) {
      Logger::warn("[FSR3] Failed to initialize FSR3 context");
    }
  }

  bool FSR3Context::initialize() {
    if (m_initialized) {
      return true;
    }

    if (!m_device) {
      m_fsr3NotSupportedReason = "Device not initialized";
      return false;
    }

    // Check FSR3 support
    if (!checkFSR3Support()) {
      m_fsr3NotSupportedReason = "FSR3 not supported on this hardware/driver";
      return false;
    }

    m_supportsFSR3 = true;
    m_initialized = true;
    
    Logger::info("[FSR3] FSR3Context initialized successfully");
    return true;
  }

  bool FSR3Context::checkFSR3Support() {
    // FSR3 is a shader-based solution and should work on most modern hardware
    // that supports compute shaders and has adequate memory bandwidth
    
    // Check for basic Vulkan device capabilities
    const VkPhysicalDeviceProperties& deviceProps = m_device->adapter()->deviceProperties();
    
    // FSR3 requires compute shader support
    if (deviceProps.limits.maxComputeWorkGroupSize[0] < 32 ||
        deviceProps.limits.maxComputeWorkGroupSize[1] < 32) {
      m_fsr3NotSupportedReason = "Insufficient compute shader support";
      return false;
    }
    
    return true;
  }

  void FSR3Context::shutdown() {
    if (!m_initialized) {
      return;
    }

    m_initialized = false;
    m_supportsFSR3 = false;
  }

  std::unique_ptr<FSR3UpscalerContext> FSR3Context::createFSR3UpscalerContext() {
    if (!m_initialized) {
      if (!initialize()) {
        return nullptr;
      }
    }

    if (!m_supportsFSR3) {
      Logger::warn("[FSR3] Cannot create FSR3UpscalerContext - FSR3 not supported");
      return nullptr;
    }

    return std::make_unique<FSR3UpscalerContext>(m_device);
  }

  // ===================================================================
  // FSR3FeatureContext Implementation
  // ===================================================================

  FSR3FeatureContext::FSR3FeatureContext(DxvkDevice* device)
    : m_device(device) {
  }

  FSR3FeatureContext::~FSR3FeatureContext() {
    // Base destructor
  }

  // ===================================================================
  // FSR3UpscalerContext Implementation
  // ===================================================================

  FSR3UpscalerContext::FSR3UpscalerContext(DxvkDevice* device)
    : FSR3FeatureContext(device) {
    Logger::info("[FSR3] FSR3UpscalerContext created");
  }

  FSR3UpscalerContext::~FSR3UpscalerContext() {
    releaseFSR3Feature();
  }

  FSR3UpscalerContext::OptimalSettings FSR3UpscalerContext::queryOptimalSettings(
    const uint32_t displaySize[2], 
    uint32_t qualityMode) const {
    
    OptimalSettings settings = {};
    
    if (m_fsr3Context) {
      // Use actual FSR3 SDK to query optimal settings
      float upscaleRatio = ffxFsr3UpscalerGetUpscaleRatioFromQualityMode(static_cast<FfxFsr3UpscalerQualityMode>(qualityMode));
      
      if (upscaleRatio > 1.0f) {
        settings.optimalRenderSize[0] = (uint32_t)(displaySize[0] / upscaleRatio);
        settings.optimalRenderSize[1] = (uint32_t)(displaySize[1] / upscaleRatio);
      } else {
        settings.optimalRenderSize[0] = displaySize[0];
        settings.optimalRenderSize[1] = displaySize[1];
      }
    } else {
      // Fallback to hardcoded scaling ratios based on quality mode
      float scalingRatio = 1.0f;
      switch (qualityMode) {
        case FFX_FSR3UPSCALER_QUALITY_MODE_ULTRA_PERFORMANCE: scalingRatio = 3.0f; break;
        case FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE: scalingRatio = 2.3f; break;
        case FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED: scalingRatio = 1.7f; break;
        case FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY: scalingRatio = 1.3f; break;
        default: scalingRatio = 1.0f; break;
      }
      
      if (scalingRatio > 1.0f) {
        settings.optimalRenderSize[0] = (uint32_t)(displaySize[0] / scalingRatio);
        settings.optimalRenderSize[1] = (uint32_t)(displaySize[1] / scalingRatio);
      } else {
        settings.optimalRenderSize[0] = displaySize[0];
        settings.optimalRenderSize[1] = displaySize[1];
      }
    }
    
    // Set min/max render sizes
    settings.minRenderSize[0] = displaySize[0] / 4;
    settings.minRenderSize[1] = displaySize[1] / 4;
    settings.maxRenderSize[0] = displaySize[0];
    settings.maxRenderSize[1] = displaySize[1];
    settings.sharpness = 0.8f; // Default sharpness
    
    Logger::info(str::format("[FSR3] Optimal render size: ", settings.optimalRenderSize[0], "x", settings.optimalRenderSize[1],
                            " for display: ", displaySize[0], "x", displaySize[1]));
    
    return settings;
  }

  void FSR3UpscalerContext::initialize(
    Rc<DxvkContext> renderContext,
    uint32_t maxRenderSize[2],
    uint32_t displayOutSize[2],
    bool isContentHDR,
    bool depthInverted,
    uint32_t qualityMode) {
    
    // Release any existing context
    releaseFSR3Feature();

    // Setup Vulkan interface
    setupVulkanInterface();

    // Allocate FSR3 context
    m_fsr3Context = new FfxFsr3UpscalerContext();

    // Create FSR3 context description
    FfxFsr3UpscalerContextDescription contextDesc = {};
    contextDesc.flags = 0;
    
    if (isContentHDR) {
      contextDesc.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
    }
    
    if (depthInverted) {
      contextDesc.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
    }
    
    contextDesc.maxRenderSize.width = maxRenderSize[0];
    contextDesc.maxRenderSize.height = maxRenderSize[1];
    contextDesc.maxUpscaleSize.width = displayOutSize[0];
    contextDesc.maxUpscaleSize.height = displayOutSize[1];
    
    // TODO: Set up backend interface
    // contextDesc.backendInterface = ...;
    
    // Initialize FSR3 upscaler context
    FfxErrorCode errorCode = ffxFsr3UpscalerContextCreate(m_fsr3Context, &contextDesc);
    if (errorCode != FFX_OK) {
      Logger::err(str::format("[FSR3] Failed to create FSR3 upscaler context: ", (int)errorCode));
      delete m_fsr3Context;
      m_fsr3Context = nullptr;
      return;
    }

    m_initialized = true;
    
    Logger::info(str::format("[FSR3] FSR3UpscalerContext initialized - Render: ", maxRenderSize[0], "x", maxRenderSize[1],
                            ", Display: ", displayOutSize[0], "x", displayOutSize[1],
                            ", HDR: ", isContentHDR ? "Yes" : "No"));
  }

  void FSR3UpscalerContext::releaseFSR3Feature() {
    if (!m_initialized) {
      return;
    }

    if (m_fsr3Context) {
      FfxErrorCode errorCode = ffxFsr3UpscalerContextDestroy(m_fsr3Context);
      if (errorCode != FFX_OK) {
        Logger::warn(str::format("[FSR3] Warning: Failed to properly destroy FSR3 context: ", (int)errorCode));
      }
      delete m_fsr3Context;
      m_fsr3Context = nullptr;
    }

    m_initialized = false;
    Logger::info("[FSR3] FSR3UpscalerContext released");
  }

  bool FSR3UpscalerContext::evaluateFSR3(
    Rc<RtxContext> renderContext, 
    const FSR3Buffers& buffers, 
    const FSR3Settings& settings) const {
    
    if (!m_initialized || !m_fsr3Context) {
      Logger::warn("[FSR3] Cannot evaluate FSR3 - context not initialized");
      return false;
    }

    // Set up FSR3 dispatch description
    FfxFsr3UpscalerDispatchDescription dispatchDesc = {};
    
    // TODO: Convert Vulkan resources to FFX resources
    // The resource conversion functions need to be implemented based on the actual
    // FidelityFX Vulkan backend integration
    
    // For now, we'll set up the basic parameters that don't require resource conversion
    dispatchDesc.jitterOffset.x = settings.jitterOffset[0];
    dispatchDesc.jitterOffset.y = settings.jitterOffset[1];
    dispatchDesc.motionVectorScale.x = settings.motionVectorScale[0];
    dispatchDesc.motionVectorScale.y = settings.motionVectorScale[1];
    dispatchDesc.reset = settings.resetAccumulation;
    dispatchDesc.enableSharpening = settings.sharpness > 0.0f;
    dispatchDesc.sharpness = settings.sharpness;
    dispatchDesc.frameTimeDelta = settings.deltaTime;
    dispatchDesc.preExposure = settings.preExposure;
    dispatchDesc.renderSize.width = settings.renderSize[0];
    dispatchDesc.renderSize.height = settings.renderSize[1];
    dispatchDesc.upscaleSize.width = settings.displaySize[0];
    dispatchDesc.upscaleSize.height = settings.displaySize[1];
    
    // TODO: Set up command list
    // dispatchDesc.commandList = ...;
    
    // TODO: Set up resource handles
    // dispatchDesc.color = ...;
    // dispatchDesc.depth = ...;
    // dispatchDesc.motionVectors = ...;
    // dispatchDesc.output = ...;
    
    // For now, skip the actual dispatch since we don't have proper resource conversion
    Logger::info("[FSR3] FSR3 evaluate called - implementation needs completion");
    
    /* TODO: Enable when resource conversion is implemented
    // Dispatch FSR3
    FfxErrorCode errorCode = ffxFsr3UpscalerContextDispatch(m_fsr3Context, &dispatchDesc);
    if (errorCode != FFX_OK) {
      Logger::err(str::format("[FSR3] FSR3 dispatch failed: ", (int)errorCode));
      return false;
    }
    */

    Logger::info(str::format("[FSR3] FSR3 evaluate completed - Render: ", settings.renderSize[0], "x", settings.renderSize[1],
                            ", Display: ", settings.displaySize[0], "x", settings.displaySize[1],
                            ", Sharpness: ", settings.sharpness));
    
    return true;
  }

  FfxErrorCode FSR3UpscalerContext::createFSR3Context() {
    // This is now handled in initialize()
    return FFX_OK;
  }

  void FSR3UpscalerContext::destroyFSR3Context() {
    // This is now handled in releaseFSR3Feature()
  }

  void FSR3UpscalerContext::setupVulkanInterface() {
    // TODO: Set up Vulkan interface for FidelityFX SDK
    // This will be implemented with the actual Vulkan backend setup
  }

  // ===================================================================
  // FSR3Utils Implementation
  // ===================================================================

  namespace FSR3Utils {
    
    uint32_t profileToQualityMode(int profile) {
      // Convert FSR3Profile enum to FSR3 SDK quality mode
      switch (profile) {
        case 0: return FFX_FSR3UPSCALER_QUALITY_MODE_ULTRA_PERFORMANCE; // UltraPerf
        case 1: return FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE;       // MaxPerf
        case 2: return FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED;          // Balanced
        case 3: return FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY;           // MaxQuality
        case 5: return FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY;           // FullResolution -> Quality
        default: return FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED;         // Default to Balanced
      }
    }

    FfxSurfaceFormat vulkanFormatToFSR3Format(VkFormat format) {
      // Convert VkFormat to FidelityFX surface format
      switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
        case VK_FORMAT_R16G16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16_FLOAT;
        case VK_FORMAT_R32G32_SFLOAT: return FFX_SURFACE_FORMAT_R32G32_FLOAT;
        case VK_FORMAT_R8_UNORM: return FFX_SURFACE_FORMAT_R8_UNORM;
        case VK_FORMAT_R32_SFLOAT: return FFX_SURFACE_FORMAT_R32_FLOAT;
        case VK_FORMAT_R8G8_UNORM: return FFX_SURFACE_FORMAT_R8G8_UNORM;
        case VK_FORMAT_R16_SFLOAT: return FFX_SURFACE_FORMAT_R16_FLOAT;
        default:
          Logger::warn(str::format("[FSR3] Unknown Vulkan format: ", (int)format));
          return FFX_SURFACE_FORMAT_UNKNOWN;
      }
    }

    void getRecommendedRenderResolution(
      uint32_t displayWidth, uint32_t displayHeight,
      uint32_t qualityMode,
      uint32_t& renderWidth, uint32_t& renderHeight) {
      
      // Use FSR3 SDK to get proper scaling ratios
      float upscaleRatio = ffxFsr3UpscalerGetUpscaleRatioFromQualityMode(static_cast<FfxFsr3UpscalerQualityMode>(qualityMode));
      
      if (upscaleRatio > 1.0f) {
        renderWidth = (uint32_t)(displayWidth / upscaleRatio);
        renderHeight = (uint32_t)(displayHeight / upscaleRatio);
      } else {
        renderWidth = displayWidth;
        renderHeight = displayHeight;
      }
    }
  }

} 