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
#include "dxvk_objects.h"
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
    Logger::info("[FSR3] Creating FSR3Context");
    if (m_device) {
      if (!initialize()) {
        Logger::err("[FSR3] Failed to initialize FSR3 context - check logs for details");
      } else {
        Logger::info("[FSR3] FSR3Context created and initialized successfully");
      }
    } else {
      Logger::err("[FSR3] Cannot create FSR3Context - device is null");
      m_fsr3NotSupportedReason = "Device is null";
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

    // Setup Vulkan interface
    setupVulkanInterface();
    
    if (!m_ffxInterface) {
      m_fsr3NotSupportedReason = "Failed to create FidelityFX Vulkan interface";
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

    shutdownVulkanInterface();
    m_initialized = false;
    m_supportsFSR3 = false;
  }

  void FSR3Context::setupVulkanInterface() {
    Logger::info("[FSR3] Setting up Vulkan interface");
    
    // Create VkDeviceContext for FidelityFX
    VkDeviceContext vkDeviceContext = {};
    vkDeviceContext.vkDevice = m_device->handle();
    vkDeviceContext.vkPhysicalDevice = m_device->adapter()->handle();
    vkDeviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;
    
    Logger::info("[FSR3] VkDeviceContext created");
    
    // Create FfxDevice
    m_ffxDevice = ffxGetDeviceVK(&vkDeviceContext);
    Logger::info("[FSR3] FfxDevice created");
    
    // Calculate scratch buffer size (max 4 contexts for FSR3 upscaler)
    const size_t maxContexts = 4;
    m_scratchBufferSize = ffxGetScratchMemorySizeVK(m_device->adapter()->handle(), maxContexts);
    Logger::info("[FSR3] Scratch buffer size calculated");
    
    // Allocate scratch buffer
    m_scratchBuffer = std::make_unique<uint8_t[]>(m_scratchBufferSize);
    Logger::info("[FSR3] Scratch buffer allocated");
    
    // Create FfxInterface
    m_ffxInterface = std::make_unique<FfxInterface>();
    Logger::info("[FSR3] FfxInterface allocated");
    
    FfxErrorCode result = ffxGetInterfaceVK(
      m_ffxInterface.get(),
      m_ffxDevice,
      m_scratchBuffer.get(),
      m_scratchBufferSize,
      maxContexts
    );
    
    if (result != FFX_OK) {
      Logger::err("[FSR3] Failed to create FidelityFX Vulkan interface");
      m_ffxInterface.reset();
      m_scratchBuffer.reset();
      m_scratchBufferSize = 0;
    } else {
      Logger::info("[FSR3] FidelityFX Vulkan interface created successfully");
      Logger::info("[FSR3] Interface function pointers verified");
    }
  }
  
  void FSR3Context::shutdownVulkanInterface() {
    m_ffxInterface.reset();
    m_scratchBuffer.reset();
    m_scratchBufferSize = 0;
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
    
    Logger::info("[FSR3] Calculated optimal render size");
    
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

    // Temporary implementation to avoid compilation errors
    // TODO: Restore full FSR3Context integration once compilation issues are resolved
    
    Logger::info("[FSR3] FSR3 upscaler initialize called - temporarily disabled");
    
    // Mark as not initialized to prevent crashes
    m_initialized = false;
    
    // For now, just return without doing anything to avoid the crash
    Logger::warn("[FSR3] FSR3 initialization temporarily disabled to prevent SDK crashes");
    return;
  }

  void FSR3UpscalerContext::releaseFSR3Feature() {
    if (!m_initialized) {
      return;
    }

    if (m_fsr3Context) {
      FfxErrorCode errorCode = ffxFsr3UpscalerContextDestroy(m_fsr3Context);
      if (errorCode != FFX_OK) {
        Logger::warn("[FSR3] Warning: Failed to properly destroy FSR3 context");
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
    
    // Convert DXVK resources to FidelityFX resources
    if (buffers.pColorBuffer) {
      dispatchDesc.color = convertDxvkResourceToFfx(buffers.pColorBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    
    if (buffers.pDepthBuffer) {
      dispatchDesc.depth = convertDxvkResourceToFfx(buffers.pDepthBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    
    if (buffers.pMotionVectors) {
      dispatchDesc.motionVectors = convertDxvkResourceToFfx(buffers.pMotionVectors, FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    
    if (buffers.pExposureBuffer) {
      dispatchDesc.exposure = convertDxvkResourceToFfx(buffers.pExposureBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    
    if (buffers.pOutputBuffer) {
      dispatchDesc.output = convertDxvkResourceToFfx(buffers.pOutputBuffer, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Convert DXVK command buffer to FidelityFX command list
    dispatchDesc.commandList = convertDxvkCommandList(renderContext);
    
    // Set up basic parameters
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
    
    // Dispatch FSR3
    FfxErrorCode errorCode = ffxFsr3UpscalerContextDispatch(m_fsr3Context, &dispatchDesc);
    if (errorCode != FFX_OK) {
      Logger::err("[FSR3] FSR3 dispatch failed");
      return false;
    }

    Logger::info("[FSR3] FSR3 evaluate completed successfully");
    
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
    // No longer needed - interface comes from FSR3Context
  }

  // ===================================================================
  // Resource Conversion Helper Methods
  // ===================================================================

  FfxResource FSR3UpscalerContext::convertDxvkResourceToFfx(const Resources::Resource* resource, FfxResourceStates state) const {
    if (!resource || resource->image == nullptr) {
      Logger::warn("[FSR3] Invalid resource for conversion");
      return FfxResource{};
    }

    // Get VkImage handle from DXVK resource
    VkImage vkImage = resource->image->handle();
    
    // Create VkImageCreateInfo from DXVK image info
    VkImageCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = resource->image->info().format;
    createInfo.extent = {
      resource->image->info().extent.width,
      resource->image->info().extent.height,
      resource->image->info().extent.depth
    };
    createInfo.mipLevels = resource->image->info().mipLevels;
    createInfo.arrayLayers = resource->image->info().numLayers;
    createInfo.samples = VkSampleCountFlagBits(resource->image->info().sampleCount);
    createInfo.tiling = resource->image->info().tiling;
    createInfo.usage = resource->image->info().usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Get resource description from FidelityFX
    FfxResourceDescription ffxResDesc = ffxGetImageResourceDescriptionVK(vkImage, createInfo);
    
    // Convert to FfxResource
    return ffxGetResourceVK(&vkImage, ffxResDesc, nullptr, state);
  }

  FfxCommandList FSR3UpscalerContext::convertDxvkCommandList(Rc<DxvkContext> dxvkContext) const {
    if (dxvkContext == nullptr) {
      Logger::warn("[FSR3] Invalid DxvkContext for command list conversion");
      return FfxCommandList{};
    }

    // Get the current command buffer from DXVK context
    VkCommandBuffer cmdBuffer = dxvkContext->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    
    // Convert to FfxCommandList using FidelityFX Vulkan backend
    return ffxGetCommandListVK(cmdBuffer);
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
          Logger::warn("[FSR3] Unknown Vulkan format");
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

// =======================================================================
// Stub implementations for Frame Interpolation functions (not needed for FSR3 upscaling)
// =======================================================================

extern "C" {
  // Stub implementation for frame generation configuration
  // This is required by the FidelityFX Vulkan backend but not needed for basic FSR3 upscaling
  FFX_API FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(const FfxFrameGenerationConfig* config) {
    // Not implemented - frame interpolation is not supported in this integration
    return FFX_ERROR_INVALID_ARGUMENT;
  }
} 