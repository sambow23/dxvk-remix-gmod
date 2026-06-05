/*
* Copyright (c) 2023-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "dxvk_format.h"
#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"

#include "../spirv/spirv_code_buffer.h"
#include "../util/util_matrix.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkPipelineManager;

  // Global tonemapper. Operator-only pipeline; sRGB conversion and dithering
  // are handled by the final output pass.
  class DxvkToneMapping: public CommonDeviceObject {
  public:
    explicit DxvkToneMapping(DxvkDevice* device);
    ~DxvkToneMapping();

    void dispatch(
      Rc<RtxContext> ctx,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      bool autoExposureEnabled = true);

    bool isEnabled() const { return tonemappingEnabled(); }

    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void showImguiSettings();

  private:
    void dispatchApplyToneMapping(
      Rc<RtxContext> ctx,
      Rc<DxvkImageView> exposureView,
      const Resources::Resource& inputBuffer,
      const Resources::Resource& colorBuffer,
      bool autoExposureEnabled);

    Rc<vk::DeviceFn> m_vkd;

    RTX_OPTION("rtx.tonemap", float, exposureBias, 0.f, "The exposure value to use for the global tonemapper when auto exposure is disabled, or a bias multiplier on top of the auto exposure's calculated exposure value.");
    RTX_OPTION("rtx.tonemap", bool, tonemappingEnabled, true, "A flag to enable or disable the global tonemapper.");
    RTX_OPTION("rtx.tonemap", bool, colorGradingEnabled, false, "A flag to enable or disable color grading after the global tonemapper's tonemapping pass.");

    RTX_OPTION("rtx.tonemap", Vector3, colorBalance, Vector3(1.0f, 1.0f, 1.0f), "The color tint to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");
    RTX_OPTION("rtx.tonemap", float, contrast, 1.0f, "The contrast adjustment to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");
    RTX_OPTION("rtx.tonemap", float, saturation, 1.0f, "The saturation adjustment to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");
  };

}
