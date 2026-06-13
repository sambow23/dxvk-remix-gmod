#include "rtx_fork_hooks.h"

#include "rtx_context.h"
#include "rtx_resources.h"
#include "rtx_shader_manager.h"
#include "rtx/pass/screen_overlay/screen_overlay.h"
#include <rtx_shaders/screen_overlay.h>

namespace dxvk {

  namespace {
    class ScreenOverlayShader : public ManagedShader {
      SHADER_SOURCE(ScreenOverlayShader, VK_SHADER_STAGE_COMPUTE_BIT, screen_overlay)

      PUSH_CONSTANTS(ScreenOverlayArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(SCREEN_OVERLAY_INPUT_OUTPUT)
        SAMPLER2D(SCREEN_OVERLAY_TEXTURE)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ScreenOverlayShader);
  }

  namespace fork_hooks {

    void dispatchScreenOverlay(RtxContext& ctx, Resources::RaytracingOutput& rtOutput) {
      if (!ctx.m_pendingScreenOverlay.has_value()) {
        return;
      }

      ScopedGpuProfileZone(&ctx, "Screen Overlay");
      auto& overlay = *ctx.m_pendingScreenOverlay;

      if (ctx.m_screenOverlayWidth != overlay.width
       || ctx.m_screenOverlayHeight != overlay.height
       || ctx.m_screenOverlayFormat != overlay.format
       || !ctx.m_screenOverlayImage.ptr()) {
        DxvkImageCreateInfo imageInfo = {};
        imageInfo.type = VK_IMAGE_TYPE_2D;
        imageInfo.format = overlay.format;
        imageInfo.flags = 0;
        imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.extent = { overlay.width, overlay.height, 1 };
        imageInfo.numLayers = 1;
        imageInfo.mipLevels = 1;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        imageInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.layout = VK_IMAGE_LAYOUT_UNDEFINED;

        ctx.m_screenOverlayImage = ctx.m_device->createImage(
          imageInfo,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXRenderTarget,
          "Screen overlay image");

        DxvkImageViewCreateInfo viewInfo = {};
        viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = overlay.format;
        viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.minLevel = 0;
        viewInfo.numLevels = 1;
        viewInfo.minLayer = 0;
        viewInfo.numLayers = 1;

        ctx.m_screenOverlayView = ctx.m_device->createImageView(ctx.m_screenOverlayImage, viewInfo);
        ctx.m_screenOverlayWidth = overlay.width;
        ctx.m_screenOverlayHeight = overlay.height;
        ctx.m_screenOverlayFormat = overlay.format;
      }

      {
        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;

        VkOffset3D offset = { 0, 0, 0 };
        VkExtent3D extent = { overlay.width, overlay.height, 1 };

        ctx.copyBufferToImage(ctx.m_screenOverlayImage, subresource, offset, extent,
                              overlay.stagingBuffer, 0, 0, 0);
      }

      ctx.setPushConstantBank(DxvkPushConstantBank::RTX);

      auto& finalOutput = rtOutput.m_finalOutput.resource(Resources::AccessType::ReadWrite);
      const VkExtent3D outputSize = finalOutput.image->info().extent;
      const VkExtent3D workgroups = util::computeBlockCount(
        outputSize, VkExtent3D { SCREEN_OVERLAY_TILE_SIZE, SCREEN_OVERLAY_TILE_SIZE, 1 });

      ScreenOverlayArgs pushArgs = {};
      pushArgs.imageSize = { outputSize.width, outputSize.height };
      pushArgs.opacity = overlay.opacity;
      ctx.pushConstants(0, sizeof(pushArgs), &pushArgs);

      ctx.bindResourceView(SCREEN_OVERLAY_INPUT_OUTPUT, finalOutput.view, nullptr);
      ctx.bindResourceView(SCREEN_OVERLAY_TEXTURE, ctx.m_screenOverlayView, nullptr);
      ctx.bindResourceSampler(
        SCREEN_OVERLAY_TEXTURE,
        ctx.getResourceManager().getSampler(
          VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

      ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ScreenOverlayShader::getShader());
      ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
      ctx.m_pendingScreenOverlay.reset();
    }

  }
}
