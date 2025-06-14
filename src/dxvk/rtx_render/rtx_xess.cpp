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
#include "dxvk_device.h"
#include "rtx_xess.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "../../util/util_math.h"

// Stub implementations for XeSS API functions
static xess_result_t xessGetVersion(xess_version_t* version) {
  if (version) {
    version->major = 1;
    version->minor = 0;
    version->patch = 0;
  }
  return XESS_RESULT_SUCCESS;
}

static xess_result_t xessVKGetRequiredInstanceExtensions(uint32_t* count, const char** extensions) {
  if (count) *count = 0;
  return XESS_RESULT_SUCCESS;
}

static xess_result_t xessVKGetRequiredDeviceExtensions(uint32_t* count, const char** extensions) {
  if (count) *count = 0;
  return XESS_RESULT_SUCCESS;
}

static xess_result_t xessVKCreateContext(void* device, xess_context_handle_t* context) {
  return XESS_RESULT_ERROR_NOT_IMPLEMENTED;
}

static xess_result_t xessDestroyContext(xess_context_handle_t context) {
  return XESS_RESULT_SUCCESS;
}

static xess_result_t xessGetOptimalInputResolution(xess_context_handle_t context, const xess_2d_t* output, xess_quality_settings_t quality, xess_2d_t* input, void* reserved1, void* reserved2) {
  // Stub implementation - calculate approximate input resolution
  if (input && output) {
    float scale = 1.0f / 2.0f; // Default to balanced
    switch (quality) {
      case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scale = 1.0f / 3.0f; break;
      case XESS_QUALITY_SETTING_PERFORMANCE: scale = 1.0f / 2.3f; break;
      case XESS_QUALITY_SETTING_BALANCED: scale = 1.0f / 2.0f; break;
      case XESS_QUALITY_SETTING_QUALITY: scale = 1.0f / 1.7f; break;
      case XESS_QUALITY_SETTING_ULTRA_QUALITY: scale = 1.0f / 1.5f; break;
      case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scale = 1.0f / 1.3f; break;
      case XESS_QUALITY_SETTING_AA: scale = 1.0f; break;
      default: scale = 1.0f / 2.0f; break;
    }
    input->x = (uint32_t)(output->x * scale);
    input->y = (uint32_t)(output->y * scale);
    return XESS_RESULT_SUCCESS;
  }
  return XESS_RESULT_ERROR_INVALID_ARGUMENT;
}

namespace dxvk {
  const char* xessProfileToString(XeSSProfile xessProfile) {
    switch (xessProfile) {
    case XeSSProfile::UltraPerf: return "Ultra Performance";
    case XeSSProfile::Performance: return "Performance";
    case XeSSProfile::Balanced: return "Balanced";
    case XeSSProfile::Quality: return "Quality";
    case XeSSProfile::UltraQuality: return "Ultra Quality";
    case XeSSProfile::UltraQualityPlus: return "Ultra Quality Plus";
    case XeSSProfile::NativeAA: return "Native Anti-Aliasing";
    case XeSSProfile::Auto: return "Auto";
    default:
      assert(false);
    case XeSSProfile::Invalid: return "Invalid";
    }
  }

  // Helper function to convert XeSS result codes to strings
  static const char* xessResultToString(xess_result_t result) {
    switch (result) {
      case XESS_RESULT_SUCCESS:                    return "Success";
      case XESS_RESULT_ERROR_UNSUPPORTED:         return "Unsupported";
      case XESS_RESULT_ERROR_UNINITIALIZED:       return "Uninitialized";
      case XESS_RESULT_ERROR_INVALID_ARGUMENT:    return "Invalid argument";
      case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "Device out of memory";
      case XESS_RESULT_ERROR_DEVICE:              return "Device error";
      case XESS_RESULT_ERROR_NOT_IMPLEMENTED:     return "Not implemented";
      case XESS_RESULT_ERROR_INVALID_CONTEXT:     return "Invalid context";
      case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "Operation in progress";
      case XESS_RESULT_WARNING_NONOPTIMAL_SETTINGS: return "Non-optimal settings";
      default:                                     return "Unknown error";
    }
  }

  // Default constructor
  DxvkXeSS::DxvkXeSS() 
    : m_device(nullptr), m_context(nullptr) {
    Logger::info("[RTX-XeSS] XeSS object created without device (default constructor)");
  }

  // Constructor with device
  DxvkXeSS::DxvkXeSS(DxvkDevice* device) 
    : m_device(device), m_context(nullptr) {
    Logger::info("XeSS: Initializing XeSS upscaler...");
    
    // First check if XeSS is enabled in options
    if (!RtxOptions::isXeSSEnabled()) {
      Logger::info("XeSS: Disabled in options (upscaler type is not XeSS)");
      m_enabled = false;
      return;
    }
    
    Logger::info("XeSS: Enabled in options, checking system support...");
    
    // Check if XeSS is supported on this system
    if (!validateXeSSSupport(device)) {
      Logger::warn("XeSS: System does not support XeSS - falling back to other upscaler");
      m_enabled = false;
      return;
    }
    
    m_enabled = true;
    Logger::info("XeSS: Successfully initialized and ready for use");
  }

  DxvkXeSS::~DxvkXeSS() {
    destroyXeSSContext();
  }

  void DxvkXeSS::onDestroy() {
    destroyXeSSContext();
  }

  void DxvkXeSS::release() {
    if (m_xessContext) {
      destroyXeSSContext();
    }
    m_initialized = false;
  }

  bool DxvkXeSS::isXeSSLibraryAvailable() {
    Logger::info("[RTX-XeSS] Checking XeSS library availability...");
    
    // Try to get XeSS version to test if library is available
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    
    if (result == XESS_RESULT_SUCCESS) {
      Logger::info(str::format("[RTX-XeSS] XeSS SDK version: ", version.major, ".", version.minor, ".", version.patch));
      return true;
    } else {
      Logger::warn(str::format("[RTX-XeSS] XeSS library not available: ", xessResultToString(result)));
      return false;
    }
  }

  bool DxvkXeSS::validateXeSSSupport(DxvkDevice* device) {
    Logger::info("[RTX-XeSS] Validating XeSS support...");
    
    if (!isXeSSLibraryAvailable()) {
      return false;
    }

    // Get XeSS version
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("[RTX-XeSS] Failed to get XeSS version: ", xessResultToString(result)));
      return false;
    }
    
    Logger::info(str::format("[RTX-XeSS] XeSS SDK version: ", version.major, ".", version.minor, ".", version.patch));

    // Check required instance extensions (simplified)
    uint32_t instanceExtCount = 0;
    result = xessVKGetRequiredInstanceExtensions(&instanceExtCount, nullptr);
    if (result == XESS_RESULT_SUCCESS && instanceExtCount > 0) {
      Logger::info(str::format("[RTX-XeSS] XeSS requires ", instanceExtCount, " instance extensions"));
    }

    // Check required device extensions (simplified)
    uint32_t deviceExtCount = 0;
    result = xessVKGetRequiredDeviceExtensions(&deviceExtCount, nullptr);
    if (result == XESS_RESULT_SUCCESS && deviceExtCount > 0) {
      Logger::info(str::format("[RTX-XeSS] XeSS requires ", deviceExtCount, " device extensions"));
    }

    // GPU compatibility check (simplified)
    auto adapter = device->adapter();
    auto deviceProps = adapter->deviceProperties();
    
    if (deviceProps.vendorID == 0x8086) { // Intel
      Logger::info("[RTX-XeSS] Intel GPU detected - using optimized XeSS path");
    } else {
      Logger::info("[RTX-XeSS] Non-Intel GPU detected - using generic XeSS path");
    }

    // Test context creation (simplified)
    xess_context_handle_t testContext = nullptr;
    result = xessVKCreateContext(device->handle(), &testContext);
    if (result == XESS_RESULT_SUCCESS) {
      Logger::info("[RTX-XeSS] XeSS context creation test successful");
      xessDestroyContext(testContext);
      return true;
    } else {
      Logger::warn(str::format("[RTX-XeSS] XeSS context creation test failed: ", xessResultToString(result)));
      return false;
    }
  }

  xess_quality_settings_t DxvkXeSS::profileToQuality(XeSSProfile profile) const {
    switch (profile) {
      case XeSSProfile::UltraPerf: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
      case XeSSProfile::Performance: return XESS_QUALITY_SETTING_PERFORMANCE;
      case XeSSProfile::Balanced: return XESS_QUALITY_SETTING_BALANCED;
      case XeSSProfile::Quality: return XESS_QUALITY_SETTING_QUALITY;
      case XeSSProfile::UltraQuality: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
      case XeSSProfile::UltraQualityPlus: return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
      case XeSSProfile::NativeAA: return XESS_QUALITY_SETTING_AA;
      default: return XESS_QUALITY_SETTING_BALANCED;
    }
  }

  VkExtent3D DxvkXeSS::getInputSize(const VkExtent3D& targetExtent) const {
    if (!m_enabled) {
      return targetExtent;
    }

    xess_quality_settings_t quality = profileToQuality(getProfile());
    
    // Calculate input resolution based on XeSS quality setting
    float scaleFactor = 1.0f;
    switch (quality) {
      case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scaleFactor = 0.33f; break;
      case XESS_QUALITY_SETTING_PERFORMANCE:       scaleFactor = 0.5f;  break;
      case XESS_QUALITY_SETTING_BALANCED:          scaleFactor = 0.67f; break;
      case XESS_QUALITY_SETTING_QUALITY:           scaleFactor = 0.75f; break;
      case XESS_QUALITY_SETTING_ULTRA_QUALITY:     scaleFactor = 0.85f; break;
      case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scaleFactor = 0.9f; break;
      case XESS_QUALITY_SETTING_AA:                scaleFactor = 1.0f;  break;
      default:                                      scaleFactor = 0.67f; break;
    }

    VkExtent3D inputExtent;
    inputExtent.width = std::max(1u, static_cast<uint32_t>(targetExtent.width * scaleFactor));
    inputExtent.height = std::max(1u, static_cast<uint32_t>(targetExtent.height * scaleFactor));
    inputExtent.depth = targetExtent.depth;

    return inputExtent;
  }

  void DxvkXeSS::initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent) {
    if (!m_enabled) {
      return;
    }

    // Check if we need to recreate the context
    XeSSProfile currentProfile = getProfile();
    if (m_initialized && 
        m_targetExtent.width == targetExtent.width &&
        m_targetExtent.height == targetExtent.height &&
        m_currentProfile == currentProfile) {
      return; // Already initialized with correct settings
    }

    // Release existing context if any
    if (m_xessContext) {
      destroyXeSSContext();
    }

    m_targetExtent = targetExtent;
    m_inputExtent = getInputSize(targetExtent);
    m_currentProfile = currentProfile;

    createXeSSContext(targetExtent);
    m_initialized = true;

    Logger::info(str::format("XeSS: Initialized with input resolution ", m_inputExtent.width, "x", m_inputExtent.height, 
                            " -> output resolution ", m_targetExtent.width, "x", m_targetExtent.height));
  }

  void DxvkXeSS::createXeSSContext(const VkExtent3D& targetExtent) {
    // For this simple implementation, we'll create a basic XeSS context
    // In a full implementation, you would:
    // 1. Create XeSS context with proper Vulkan integration
    // 2. Set up input/output textures
    // 3. Configure quality settings
    
    Logger::info("XeSS: Creating context (placeholder implementation)");
    
    // Placeholder - in real implementation you would call XeSS SDK functions
    m_xessContext = reinterpret_cast<xess_context_handle_t>(0x1); // Dummy handle
  }

  void DxvkXeSS::destroyXeSSContext() {
    if (m_xessContext) {
      Logger::info("XeSS: Destroying context");
      // In real implementation: xessDestroyContext(m_xessContext);
      m_xessContext = nullptr;
    }
  }

  void DxvkXeSS::dispatch(
    Rc<DxvkContext> renderContext,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    bool resetHistory) {
    
    if (!m_enabled || !m_initialized) {
      // Fallback: just copy input to output
      renderContext->copyImage(
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutputExtent);
      return;
    }

    // Placeholder XeSS dispatch
    // In a real implementation, you would:
    // 1. Set up XeSS input textures (color, motion vectors, depth)
    // 2. Call XeSS execute function
    // 3. Handle output texture
    
    Logger::debug("XeSS: Dispatching upscaling (placeholder)");
    
    // For now, just copy the input to output as a fallback
    renderContext->copyImage(
      rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      { 0, 0, 0 },
      rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      { 0, 0, 0 },
      rtOutput.m_compositeOutputExtent);
  }

  XeSSProfile DxvkXeSS::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
    XeSSProfile desiredProfile = XeSSProfile::UltraPerf;

    // Standard display resolution based XeSS config
    if (displayHeight <= 1080) {
      desiredProfile = XeSSProfile::Quality;
    } else if (displayHeight < 2160) {
      desiredProfile = XeSSProfile::Balanced;
    } else if (displayHeight < 4320) {
      desiredProfile = XeSSProfile::Performance;
    } else {
      // For > 4k (e.g. 8k)
      desiredProfile = XeSSProfile::UltraPerf;
    }

    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias XeSS more towards performance
      desiredProfile = (XeSSProfile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = (XeSSProfile) std::max(0, (int) desiredProfile - 2);
    }

    return desiredProfile;
  }

  void DxvkXeSS::setSetting(const uint32_t displaySize[2], const XeSSProfile profile, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    
    // Handle the "auto" case
    XeSSProfile actualProfile = profile;
    if (actualProfile == XeSSProfile::Auto) {
      actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
    }

    if (m_actualProfile == actualProfile && displaySize[0] == m_xessOutputSize.width && displaySize[1] == m_xessOutputSize.height) {
      // Nothing changed that would alter XeSS resolution(s), so return the last cached optimal render size
      outRenderSize[0] = m_inputSize.width;
      outRenderSize[1] = m_inputSize.height;
      return;
    }
    
    m_actualProfile = actualProfile;
    m_recreate = true;
    m_profile = profile;

    if (m_profile == XeSSProfile::NativeAA) {
      m_inputSize.width = outRenderSize[0] = displaySize[0];
      m_inputSize.height = outRenderSize[1] = displaySize[1];
    } else {
      // Calculate optimal input resolution based on quality setting
      xess_2d_t outputRes = { displaySize[0], displaySize[1] };
      xess_2d_t inputRes;
      
      xess_quality_settings_t quality = profileToQuality(m_actualProfile);
      
      // Use XeSS SDK to get optimal input resolution
      if (m_xessContext) {
        xess_result_t result = xessGetOptimalInputResolution(m_xessContext, &outputRes, quality, &inputRes, nullptr, nullptr);
        if (result == XESS_RESULT_SUCCESS) {
          m_inputSize.width = outRenderSize[0] = inputRes.x;
          m_inputSize.height = outRenderSize[1] = inputRes.y;
        } else {
          // Fallback to manual calculation
          float scale = 1.0f;
          switch (quality) {
          case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scale = 1.0f / 3.0f; break;
          case XESS_QUALITY_SETTING_PERFORMANCE: scale = 1.0f / 2.3f; break;
          case XESS_QUALITY_SETTING_BALANCED: scale = 1.0f / 2.0f; break;
          case XESS_QUALITY_SETTING_QUALITY: scale = 1.0f / 1.7f; break;
          case XESS_QUALITY_SETTING_ULTRA_QUALITY: scale = 1.0f / 1.5f; break;
          case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scale = 1.0f / 1.3f; break;
          case XESS_QUALITY_SETTING_AA: scale = 1.0f; break;
          default: scale = 1.0f / 2.0f; break;
          }
          m_inputSize.width = outRenderSize[0] = (uint32_t)(displaySize[0] * scale);
          m_inputSize.height = outRenderSize[1] = (uint32_t)(displaySize[1] * scale);
        }
      } else {
        // Fallback calculation when no context available yet
        float scale = 1.0f / 2.0f; // Default to balanced
        m_inputSize.width = outRenderSize[0] = (uint32_t)(displaySize[0] * scale);
        m_inputSize.height = outRenderSize[1] = (uint32_t)(displaySize[1] * scale);
      }
    }

    m_xessOutputSize.width = displaySize[0];
    m_xessOutputSize.height = displaySize[1];
  }

  XeSSProfile DxvkXeSS::getCurrentProfile() const {
    return m_actualProfile;
  }

  void DxvkXeSS::getInputSize(uint32_t& width, uint32_t& height) const {
    width = m_inputSize.width;
    height = m_inputSize.height;
  }

  void DxvkXeSS::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = m_xessOutputSize.width;
    height = m_xessOutputSize.height;
  }

  XeSSProfile DxvkXeSS::getAutoProfile() const {
    // Simple auto profile selection based on resolution
    return XeSSProfile::Balanced;
  }

  void DxvkXeSS::setSetting(const char* name, const char* value) {
    // Placeholder for XeSS settings
    Logger::debug(str::format("XeSS: Setting ", name, " = ", value));
  }
} 