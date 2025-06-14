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

// Include XeSS SDK headers
#include "xess/xess.h"
#include "xess/xess_vk.h"

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief XeSS upscaler
   * 
   * Simple implementation of Intel XeSS Super Resolution for generic GPU path
   */
  class DxvkXeSS : public RcObject {

  public:

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
      const Resources::RaytracingOutput& rtOutput);

    static XeSSProfile getProfile() { return RtxOptions::xessProfile(); }
    static xess_quality_settings_t profileToQuality(XeSSProfile profile);

  private:

    DxvkDevice* m_device = nullptr;
    bool m_enabled = false;
    bool m_initialized = false;

    xess_context_handle_t m_xessContext = nullptr;
    VkExtent3D m_targetExtent = { 0, 0, 0 };
    VkExtent3D m_inputExtent = { 0, 0, 0 };
    XeSSProfile m_currentProfile = XeSSProfile::Auto;

    void createXeSSContext(const VkExtent3D& targetExtent);
    void destroyXeSSContext();
    bool checkXeSSSupport();
  };

} 