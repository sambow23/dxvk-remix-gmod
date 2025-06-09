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
#pragma once

// FidelityFX SDK includes
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_interface.h>
#include <FidelityFX/host/ffx_types.h>
#include <FidelityFX/host/ffx_error.h>

#include <memory>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_resources.h"

namespace dxvk {
  class DxvkDevice;
  class DxvkContext;
  class RtxContext;

  class FSR3Context;
  class FSR3UpscalerContext;

  /**
   * Main FSR3 SDK context manager - similar to NGXContext
   */
  class FSR3Context final {
  public:
    FSR3Context() : m_device(nullptr) { } // Default constructor for Active<>
    explicit FSR3Context(DxvkDevice* device);
    ~FSR3Context() {
      shutdown();
    }

    FSR3Context(const FSR3Context&)                = delete;
    FSR3Context(FSR3Context&&) noexcept            = delete;
    FSR3Context& operator=(const FSR3Context&)     = delete;
    FSR3Context& operator=(FSR3Context&&) noexcept = delete;

    void shutdown();

    bool supportsFSR3() const {
      return m_supportsFSR3;
    }

    const std::string& getFSR3NotSupportedReason() const {
      return m_fsr3NotSupportedReason;
    }
    
    std::unique_ptr<FSR3UpscalerContext> createFSR3UpscalerContext();
    
  private:
    bool initialize();
    bool checkFSR3Support();

    DxvkDevice* m_device = nullptr;
    bool m_initialized = false;
    bool m_supportsFSR3 = false;
    std::string m_fsr3NotSupportedReason;
  };

  /**
   * Base class for FSR3 feature contexts - similar to NGXFeatureContext
   */
  class FSR3FeatureContext {
  public:
    FSR3FeatureContext(const FSR3FeatureContext&)                = delete;
    FSR3FeatureContext(FSR3FeatureContext&&) noexcept            = delete;
    FSR3FeatureContext& operator=(const FSR3FeatureContext&)     = delete;
    FSR3FeatureContext& operator=(FSR3FeatureContext&&) noexcept = delete;

    virtual ~FSR3FeatureContext();
    virtual void releaseFSR3Feature() = 0;

  protected:
    explicit FSR3FeatureContext(DxvkDevice* device);
    DxvkDevice* m_device = nullptr;
  };

  /**
   * FSR3 Upscaler context - similar to NGXDLSSContext
   */
  class FSR3UpscalerContext final : public FSR3FeatureContext {
  public:
    struct OptimalSettings {
      float sharpness;
      uint32_t optimalRenderSize[2];
      uint32_t minRenderSize[2];
      uint32_t maxRenderSize[2];
    };

    struct FSR3Buffers {
      const Resources::Resource* pColorBuffer;           // Input color
      const Resources::Resource* pDepthBuffer;           // Input depth
      const Resources::Resource* pMotionVectors;         // Input motion vectors
      const Resources::Resource* pExposureBuffer;        // Optional exposure
      const Resources::Resource* pOutputBuffer;          // Upscaled output
    };

    struct FSR3Settings {
      bool resetAccumulation;                            // Reset temporal accumulation
      float sharpness;                                   // Sharpening amount [0.0, 2.0]
      float jitterOffset[2];                             // Camera jitter offset
      float motionVectorScale[2];                        // Motion vector scaling
      float deltaTime;                                   // Frame delta time
      float preExposure;                                 // Pre-exposure value
      uint32_t renderSize[2];                            // Render resolution
      uint32_t displaySize[2];                           // Display resolution
    };

    // Query optimal FSR3 settings for a given resolution and quality profile
    OptimalSettings queryOptimalSettings(const uint32_t displaySize[2], uint32_t qualityMode) const;

    // Initialize FSR3 upscaler context
    void initialize(
      Rc<DxvkContext> renderContext,
      uint32_t maxRenderSize[2],
      uint32_t displayOutSize[2],
      bool isContentHDR,
      bool depthInverted,
      uint32_t qualityMode);

    // Release FSR3 upscaler
    void releaseFSR3Feature() override;

    // Check if FSR3 is initialized
    bool isFSR3Initialized() const { 
      return m_initialized && m_fsr3Context != nullptr; 
    }

    // Evaluate FSR3 upscaling
    bool evaluateFSR3(
      Rc<RtxContext> renderContext, 
      const FSR3Buffers& buffers, 
      const FSR3Settings& settings) const;

  public:
    // Constructor - public for make_unique, use FSR3Context::createFSR3UpscalerContext
    explicit FSR3UpscalerContext(DxvkDevice* device);
    ~FSR3UpscalerContext() override;

    FSR3UpscalerContext(const FSR3UpscalerContext&)                = delete;
    FSR3UpscalerContext(FSR3UpscalerContext&&) noexcept            = delete;
    FSR3UpscalerContext& operator=(const FSR3UpscalerContext&)     = delete;
    FSR3UpscalerContext& operator=(FSR3UpscalerContext&&) noexcept = delete;

  private:
    bool m_initialized = false;
    FfxFsr3UpscalerContext* m_fsr3Context = nullptr;
    
    // Helper methods
    FfxErrorCode createFSR3Context();
    void destroyFSR3Context();
    
    // Vulkan-specific setup
    void setupVulkanInterface();
  };

  /**
   * Utility functions for FSR3 integration
   */
  namespace FSR3Utils {
    // Convert FSR3Profile to FSR3 SDK quality mode
    uint32_t profileToQualityMode(int profile);
    
    // Convert Vulkan format to FSR3 format
    FfxSurfaceFormat vulkanFormatToFSR3Format(VkFormat format);
    
    // Get recommended render resolution for given display size and quality
    void getRecommendedRenderResolution(
      uint32_t displayWidth, uint32_t displayHeight,
      uint32_t qualityMode,
      uint32_t& renderWidth, uint32_t& renderHeight);
  }
} 