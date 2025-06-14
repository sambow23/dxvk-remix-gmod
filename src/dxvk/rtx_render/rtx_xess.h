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

#include "rtx_resources.h"
#include "rtx_options.h"

#include "../dxvk_format.h"
#include "../dxvk_include.h"
#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"

// XeSS includes - temporarily disabled for build
// TODO: Re-enable when include path issues are resolved
// #include <xess/xess.h>
// #include <xess/xess_vk.h>

// Forward declarations for XeSS types
typedef void* xess_context_handle_t;
typedef int xess_result_t;
typedef int xess_quality_settings_t;
typedef struct { uint32_t x, y; } xess_2d_t;
typedef struct { uint32_t major, minor, patch; } xess_version_t;

// XeSS result constants
#define XESS_RESULT_SUCCESS 0
#define XESS_RESULT_ERROR_UNSUPPORTED 1
#define XESS_RESULT_ERROR_UNINITIALIZED 2
#define XESS_RESULT_ERROR_INVALID_ARGUMENT 3
#define XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY 4
#define XESS_RESULT_ERROR_DEVICE 5
#define XESS_RESULT_ERROR_NOT_IMPLEMENTED 6
#define XESS_RESULT_ERROR_INVALID_CONTEXT 7
#define XESS_RESULT_ERROR_OPERATION_IN_PROGRESS 8
#define XESS_RESULT_WARNING_NONOPTIMAL_SETTINGS 9

// XeSS quality settings constants
#define XESS_QUALITY_SETTING_ULTRA_PERFORMANCE 0
#define XESS_QUALITY_SETTING_PERFORMANCE 1
#define XESS_QUALITY_SETTING_BALANCED 2
#define XESS_QUALITY_SETTING_QUALITY 3
#define XESS_QUALITY_SETTING_ULTRA_QUALITY 4
#define XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS 5
#define XESS_QUALITY_SETTING_AA 6

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief XeSS upscaler
   * 
   * Simple implementation of Intel XeSS Super Resolution for generic GPU path
   */
  class DxvkXeSS : public RcObject {

  public:

    // Default constructor
    DxvkXeSS();

    // Constructor with device
    DxvkXeSS(DxvkDevice* device);

    ~DxvkXeSS();

    bool isEnabled() const { return m_enabled; }
    bool isActive() const { return m_enabled && m_device != nullptr; }

    void initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent);
    void release();

    VkExtent3D getInputSize(const VkExtent3D& targetExtent) const;
    
    void dispatch(
      Rc<DxvkContext> renderContext,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory);

    static XeSSProfile getProfile() { return RtxOptions::xessProfile(); }

  private:

    DxvkDevice* m_device = nullptr;
    bool m_enabled = false;
    bool m_initialized = false;

    xess_context_handle_t m_xessContext = nullptr;
    VkExtent3D m_targetExtent = { 0, 0, 0 };
    VkExtent3D m_inputExtent = { 0, 0, 0 };
    XeSSProfile m_currentProfile = XeSSProfile::Auto;

    // Additional member variables needed by implementation
    xess_context_handle_t m_context = nullptr;
    XeSSProfile m_profile = XeSSProfile::Auto;
    XeSSProfile m_actualProfile = XeSSProfile::Auto;
    VkExtent2D m_inputSize = { 0, 0 };
    VkExtent2D m_xessOutputSize = { 0, 0 };
    bool m_recreate = false;

    void createXeSSContext(const VkExtent3D& targetExtent);
    void destroyXeSSContext();
    bool validateXeSSSupport(DxvkDevice* device);
    
    // Additional methods needed by implementation
    XeSSProfile getAutoProfile() const;
    XeSSProfile getAutoProfile(uint32_t displayWidth, uint32_t displayHeight);
    XeSSProfile getCurrentProfile() const;
    void setSetting(const char* name, const char* value);
    void setSetting(const uint32_t displaySize[2], const XeSSProfile profile, uint32_t outRenderSize[2]);
    void getInputSize(uint32_t& width, uint32_t& height) const;
    void getOutputSize(uint32_t& width, uint32_t& height) const;
    xess_quality_settings_t profileToQuality(XeSSProfile profile) const;
    
    // Static helper to check if XeSS library is available at all
    static bool isXeSSLibraryAvailable();
  };

} 