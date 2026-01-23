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
#include "rtx_context.h"
#include "rtx_rcas.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/post_fx/rcas.h"

#include <rtx_shaders/rcas.h>
#include "rtx_imgui.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class RcasShader : public ManagedShader {
      SHADER_SOURCE(RcasShader, VK_SHADER_STAGE_COMPUTE_BIT, rcas)

      PUSH_CONSTANTS(RcasArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(RCAS_INPUT)
        RW_TEXTURE2D(RCAS_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RcasShader);
  }

  DxvkRCAS::DxvkRCAS(DxvkDevice* device)
    : CommonDeviceObject(device)
    , m_vkd(device->vkd()) {
  }

  DxvkRCAS::~DxvkRCAS() {
  }

  void DxvkRCAS::showImguiSettings() {
    ImGui::Checkbox("RCAS Sharpening Enabled", &enableObject());
    if (enable()) {
      ImGui::Indent();
      ImGui::DragFloat("Sharpness", &sharpnessObject(), 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Unindent();
    }
  }

  void DxvkRCAS::dispatch(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& outputBuffer) {
    
    ScopedGpuProfileZone(ctx, "RCAS Sharpening");

    // Early exit if disabled
    if (!isEnabled()) {
      return;
    }

    const VkExtent3D& outputExtent = outputBuffer.image->info().extent;

    // Setup push constants
    RcasArgs args = {};
    args.imageSize = { (uint32_t)outputExtent.width, (uint32_t)outputExtent.height };
    args.sharpness = sharpness();

    // Bind resources
    ctx->bindResourceView(RCAS_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceSampler(RCAS_INPUT, linearSampler);
    ctx->bindResourceView(RCAS_OUTPUT, outputBuffer.view, nullptr);

    // Bind shader
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RcasShader::getShader());
    ctx->pushConstants(0, sizeof(RcasArgs), &args);

    // Calculate workgroups (8x8 threads per workgroup)
    const uint32_t workgroupsX = (outputExtent.width + 7) / 8;
    const uint32_t workgroupsY = (outputExtent.height + 7) / 8;

    // Dispatch compute shader
    ctx->dispatch(workgroupsX, workgroupsY, 1);
  }

}
