/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_options.h"
#include "rtx_common_object.h"

namespace dxvk {

  class DxvkDevice;

  class DxvkRCAS : public CommonDeviceObject {
  public:
    DxvkRCAS(DxvkDevice* device);
    ~DxvkRCAS();

    void dispatch(
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::Resource& inputBuffer,
      const Resources::Resource& outputBuffer);

    void showImguiSettings();

    inline bool isEnabled() const { return enable() && sharpness() > 0.0f; }

    RTX_OPTION_ENV("rtx.rcas", bool, enable, false, "RTX_RCAS_ENABLE", "Enable RCAS sharpening post-process effect.");
    RTX_OPTION("rtx.rcas", float, sharpness, 1.0f, "Sharpness strength (0.0 to 1.0). Higher values produce stronger sharpening.");

  private:
    Rc<vk::DeviceFn> m_vkd;
  };

}
