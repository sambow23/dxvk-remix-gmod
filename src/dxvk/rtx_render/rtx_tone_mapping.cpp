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
#include "rtx_tone_mapping.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx_fork_hooks.h"

#include <rtx_shaders/auto_exposure.h>
#include <rtx_shaders/auto_exposure_histogram.h>
#include <rtx_shaders/tonemapping_apply_tonemapping.h>
#include "rtx_imgui.h"

namespace dxvk {
  namespace {
    class ApplyTonemappingShader : public ManagedShader {
      SHADER_SOURCE(ApplyTonemappingShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_apply_tonemapping)

      PUSH_CONSTANTS(ToneMappingApplyToneMappingArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT)
        RW_TEXTURE1D_READONLY(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT)
        RW_TEXTURE2D(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT)
      END_PARAMETER()
    };
  }

  DxvkToneMapping::DxvkToneMapping(DxvkDevice* device)
  : CommonDeviceObject(device), m_vkd(device->vkd()) {
  }

  DxvkToneMapping::~DxvkToneMapping() {
  }

  void DxvkToneMapping::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    ApplyTonemappingShader::getShader();
  }

  void DxvkToneMapping::showImguiSettings() {
    RemixGui::DragFloat("Global Exposure", &exposureBiasObject(), 0.01f, -4.f, 4.f);

    RemixGui::Checkbox("Color Grading Enabled", &colorGradingEnabledObject());
    if (colorGradingEnabled()) {
      ImGui::Indent();
      RemixGui::DragFloat("Contrast", &contrastObject(), 0.01f, 0.f, 1.f);
      RemixGui::DragFloat("Saturation", &saturationObject(), 0.01f, 0.f, 1.f);
      RemixGui::DragFloat3("Color Balance", &colorBalanceObject(), 0.01f, 0.f, 1.f);
      RemixGui::Separator();
      ImGui::Unindent();
    }

    RemixGui::Checkbox("Tonemapping Enabled", &tonemappingEnabledObject());
    if (tonemappingEnabled()) {
      ImGui::Indent();
      fork_hooks::showTonemapOperatorUI();
      RemixGui::Separator();
      ImGui::Unindent();
    }
  }

  void DxvkToneMapping::dispatchApplyToneMapping(
    Rc<RtxContext> ctx,
    Rc<DxvkImageView> exposureView,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& colorBuffer,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Apply Tone Mapping");

    const VkExtent3D workgroups = util::computeBlockCount(colorBuffer.view->imageInfo().extent, VkExtent3D { 16, 16, 1 });

    ToneMappingApplyToneMappingArgs pushArgs = {};
    pushArgs.toneMappingEnabled = tonemappingEnabled();
    pushArgs.enableAutoExposure = autoExposureEnabled;
    pushArgs.colorGradingEnabled = colorGradingEnabled();
    pushArgs.exposureFactor = exp2f(exposureBias());

    fork_hooks::populateTonemapOperatorArgs(pushArgs);

    pushArgs.colorBalance = colorBalance();
    pushArgs.contrast = contrast();
    pushArgs.saturation = saturation();

    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT, exposureView, nullptr);
    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT, colorBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ApplyTonemappingShader::getShader());
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkToneMapping::dispatch(
    Rc<RtxContext> ctx,
    Rc<DxvkImageView> exposureView,
    const Resources::RaytracingOutput& rtOutput,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Tone Mapping");

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    const Resources::Resource& inputColorBuffer = rtOutput.m_finalOutput.resource(Resources::AccessType::Read);
    dispatchApplyToneMapping(
      ctx,
      exposureView,
      inputColorBuffer,
      rtOutput.m_finalOutput.resource(Resources::AccessType::Write),
      autoExposureEnabled);
  }
}
