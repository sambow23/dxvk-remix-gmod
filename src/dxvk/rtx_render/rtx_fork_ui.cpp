// src/dxvk/rtx_render/rtx_fork_ui.cpp
//
// Fork-owned file. Render-side of the screen-space UI rendering subsystem: a
// textured-quad draw-list rasterizer that composites the game's 2D UI directly
// over the final tone-mapped image, replacing the CPU framebuffer-readback
// overlay path (remixapi_DrawScreenOverlay).
//
// Design mirrors the ImGui DXVK backend (src/dxvk/imgui/imgui_impl_dxvk.hpp):
// stream vertex/index data into host-visible buffers, bind a vertex+fragment
// pipeline with straight-alpha blending, and issue one indexed draw per
// command. The render target is rtOutput.m_finalOutput, which is created with
// VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT (see rtx_resources.cpp).
//
// This TU deliberately does NOT use D3D9DeviceEx::EmitCs. The API-thread
// staging and the present flush (which use EmitCs) live in
// rtx_fork_api_entry.cpp; they hand work to the render-thread mutators declared
// in rtx_fork_ui.h (applyTextureUpload / freeTexture / setCurrentDrawList).
// Keeping EmitCs out of this TU matters because rtx_context.cpp references
// dispatchUi and is linked into the unit-test executables, which do not provide
// the private device symbols the EmitCs template instantiates.
//
// All state here (texture registry, current draw list, streaming buffers,
// samplers, the built-in white texture) is touched ONLY on the render thread
// (inside the present-flush EmitCs lambda's mutator calls and dispatchUi), so
// it needs no locking.
//
// dispatchUi uses only public DxvkContext raster APIs and the public
// RtxContext resource accessors, so it requires no friend declaration.
//
// See docs/fork-touchpoints.md and docs/RemixUIAPI.md.

#include "rtx_fork_hooks.h"
#include "rtx_fork_ui.h"

#include <cstring>
#include <unordered_map>
#include <vector>

#include "rtx_context.h"
#include "rtx_resources.h"
#include "rtx_shader_manager.h"
#include "dxvk_device.h"

#include <rtx_shaders/ui_vertex.h>
#include <rtx_shaders/ui_fragment.h>
#include "rtx/pass/ui/ui_bindings.h"

namespace dxvk {

  namespace {

    // CPU mirror of the shader-side UiPushConstants (two float2 = 16 bytes).
    // Kept separate from the shader header so the ManagedShader push-constant
    // size is computed from a plain C++ type.
    struct UiPushConstantsCpu {
      float scale[2];
      float translate[2];
    };

    class UiVertexShader : public ManagedShader {
      SHADER_SOURCE(UiVertexShader, VK_SHADER_STAGE_VERTEX_BIT, ui_vertex)

      PUSH_CONSTANTS(UiPushConstantsCpu)

      BEGIN_PARAMETER()
      END_PARAMETER()

      // uv and color
      INTERFACE_OUTPUT_SLOTS(2);
    };

    class UiPixelShader : public ManagedShader {
      SHADER_SOURCE(UiPixelShader, VK_SHADER_STAGE_FRAGMENT_BIT, ui_fragment)

      BEGIN_PARAMETER()
        SAMPLER2D(UI_TEXTURE0_INPUT)
      END_PARAMETER()

      INTERFACE_INPUT_SLOTS(2);
      INTERFACE_OUTPUT_SLOTS(1);
    };

    // --- Render-thread-owned state ------------------------------------------

    struct UiTexture {
      Rc<DxvkImage>     image;
      Rc<DxvkImageView> view;
      uint32_t          width = 0;
      uint32_t          height = 0;
      VkFormat          format = VK_FORMAT_UNDEFINED;
    };

    std::unordered_map<uint64_t, UiTexture> g_uiTextures;
    fork_ui::UiDrawList                     g_currentDrawList;

    Rc<DxvkBuffer> g_vb;
    Rc<DxvkBuffer> g_ib;
    VkDeviceSize   g_vbSize = 0;
    VkDeviceSize   g_ibSize = 0;

    // Built-in 1x1 opaque white texture, bound for draw commands with id 0
    // (untextured / solid-colour quads). Lazily created on first dispatch.
    UiTexture g_whiteTexture;

    // Depth buffer for 3D screen-space UI draws (inventory models, block item
    // icons). Lazily created and resized to the output extent. Flat 2D draws do
    // not touch it; 3D commands (REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST) test+write it.
    Rc<DxvkImage>     g_uiDepthImage;
    Rc<DxvkImageView> g_uiDepthView;
    uint32_t          g_uiDepthWidth = 0;
    uint32_t          g_uiDepthHeight = 0;

    constexpr VkFormat kUiDepthFormat = VK_FORMAT_D32_SFLOAT;

    constexpr VkDeviceSize kBufferAlignment = 256;

    VkFormat toVkFormat(remixapi_Format format) {
      switch (format) {
        case REMIXAPI_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case REMIXAPI_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case REMIXAPI_FORMAT_R8G8B8A8_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
        case REMIXAPI_FORMAT_B8G8R8A8_SRGB:  return VK_FORMAT_B8G8R8A8_SRGB;
        default:                             return VK_FORMAT_UNDEFINED;
      }
    }

    // Create a UI sampled texture image + view of the given dimensions/format.
    void createUiTextureImage(DxvkContext* ctx, UiTexture& out,
                              uint32_t width, uint32_t height, VkFormat format) {
      DxvkImageCreateInfo imageInfo = {};
      imageInfo.type        = VK_IMAGE_TYPE_2D;
      imageInfo.format      = format;
      imageInfo.flags       = 0;
      imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.extent      = { width, height, 1 };
      imageInfo.numLayers   = 1;
      imageInfo.mipLevels   = 1;
      imageInfo.usage       = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      imageInfo.stages      = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      imageInfo.access      = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.layout      = VK_IMAGE_LAYOUT_UNDEFINED;

      out.image = ctx->getDevice()->createImage(
        imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXRenderTarget, "Remix UI texture");

      DxvkImageViewCreateInfo viewInfo = {};
      viewInfo.type      = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format    = format;
      viewInfo.usage     = VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLevel  = 0;
      viewInfo.numLevels = 1;
      viewInfo.minLayer  = 0;
      viewInfo.numLayers = 1;

      out.view   = ctx->getDevice()->createImageView(out.image, viewInfo);
      out.width  = width;
      out.height = height;
      out.format = format;
    }

    void uploadStagingToImage(DxvkContext* ctx, const UiTexture& tex,
                              const Rc<DxvkBuffer>& staging) {
      VkImageSubresourceLayers subresource = {};
      subresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      subresource.mipLevel       = 0;
      subresource.baseArrayLayer = 0;
      subresource.layerCount     = 1;

      ctx->copyBufferToImage(tex.image, subresource, { 0, 0, 0 },
                             { tex.width, tex.height, 1 }, staging, 0, 0, 0);
    }

    void ensureWhiteTexture(DxvkContext* ctx) {
      if (g_whiteTexture.view.ptr()) {
        return;
      }

      createUiTextureImage(ctx, g_whiteTexture, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);

      DxvkBufferCreateInfo stagingInfo = {};
      stagingInfo.size   = 4;
      stagingInfo.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      stagingInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      stagingInfo.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;

      Rc<DxvkBuffer> staging = ctx->getDevice()->createBuffer(
        stagingInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        DxvkMemoryStats::Category::RTXBuffer, "Remix UI white staging");

      const uint32_t white = 0xFFFFFFFFu;
      std::memcpy(DxvkBufferSlice(staging).mapPtr(0), &white, sizeof(white));
      uploadStagingToImage(ctx, g_whiteTexture, staging);
    }

    // Ensure a depth attachment sized to the output exists, then clear it to
    // the far plane. Called only when the draw list contains 3D commands.
    void ensureUiDepth(DxvkContext* ctx, uint32_t width, uint32_t height) {
      if (!g_uiDepthView.ptr() || g_uiDepthWidth != width || g_uiDepthHeight != height) {
        DxvkImageCreateInfo imageInfo = {};
        imageInfo.type        = VK_IMAGE_TYPE_2D;
        imageInfo.format      = kUiDepthFormat;
        imageInfo.flags       = 0;
        imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.extent      = { width, height, 1 };
        imageInfo.numLayers   = 1;
        imageInfo.mipLevels   = 1;
        imageInfo.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.stages      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageInfo.access      = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.layout      = VK_IMAGE_LAYOUT_UNDEFINED;

        g_uiDepthImage = ctx->getDevice()->createImage(
          imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXRenderTarget, "Remix UI depth");

        DxvkImageViewCreateInfo viewInfo = {};
        viewInfo.type      = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format    = kUiDepthFormat;
        viewInfo.usage     = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        viewInfo.aspect    = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.minLevel  = 0;
        viewInfo.numLevels = 1;
        viewInfo.minLayer  = 0;
        viewInfo.numLayers = 1;

        g_uiDepthView   = ctx->getDevice()->createImageView(g_uiDepthImage, viewInfo);
        g_uiDepthWidth  = width;
        g_uiDepthHeight = height;
      }

      VkImageSubresourceRange range = {};
      range.aspectMask   = VK_IMAGE_ASPECT_DEPTH_BIT;
      range.baseMipLevel = 0;
      range.levelCount   = 1;
      range.baseArrayLayer = 0;
      range.layerCount   = 1;
      ctx->clearDepthStencilImage(g_uiDepthImage, { 1.0f, 0 }, range);
    }

    Rc<DxvkBuffer> createHostBuffer(const Rc<DxvkDevice>& device, VkDeviceSize size,
                                    VkBufferUsageFlags usage, const char* name) {
      DxvkBufferCreateInfo info = {};
      info.size   = size;
      info.usage  = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

      return device->createBuffer(
        info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        DxvkMemoryStats::Category::RTXBuffer, name);
    }

    void ensureStreamingCapacity(const Rc<DxvkDevice>& device, size_t vtxBytes, size_t idxBytes) {
      if (vtxBytes > g_vbSize) {
        const VkDeviceSize newSize = (vtxBytes + kBufferAlignment - 1) & ~(kBufferAlignment - 1);
        g_vb = createHostBuffer(device, newSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "Remix UI VB");
        g_vbSize = newSize;
      }
      if (idxBytes > g_ibSize) {
        const VkDeviceSize newSize = (idxBytes + kBufferAlignment - 1) & ~(kBufferAlignment - 1);
        g_ib = createHostBuffer(device, newSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "Remix UI IB");
        g_ibSize = newSize;
      }
    }

  } // anonymous namespace

namespace fork_ui {

  bool isSupportedFormat(remixapi_Format format) {
    return toVkFormat(format) != VK_FORMAT_UNDEFINED;
  }

  void applyTextureUpload(DxvkContext* ctx, const UiTextureUpload& upload) {
    const VkFormat vkFormat = toVkFormat(upload.format);
    if (vkFormat == VK_FORMAT_UNDEFINED || !upload.staging.ptr()) {
      return;
    }

    UiTexture& tex = g_uiTextures[upload.id];
    if (!tex.view.ptr() || tex.width != upload.width || tex.height != upload.height || tex.format != vkFormat) {
      createUiTextureImage(ctx, tex, upload.width, upload.height, vkFormat);
    }
    uploadStagingToImage(ctx, tex, upload.staging);
  }

  void freeTexture(uint64_t id) {
    g_uiTextures.erase(id);
  }

  void setCurrentDrawList(UiDrawList&& list) {
    g_currentDrawList = std::move(list);
  }

} // namespace fork_ui

namespace fork_hooks {

  void dispatchUi(RtxContext& ctx, Resources::RaytracingOutput& rtOutput) {
    const fork_ui::UiDrawList& list = g_currentDrawList;
    if (list.commands.empty() || list.displayWidth == 0 || list.displayHeight == 0) {
      return;
    }

    ScopedGpuProfileZone(&ctx, "Screen UI");

    DxvkContext& dctx = ctx;
    const Rc<DxvkDevice> device = ctx.getDevice();

    ensureWhiteTexture(&dctx);

    const size_t vtxBytes = list.vertices.size() * sizeof(remixapi_UIVertex);
    const size_t idxBytes = list.indices.size() * sizeof(uint32_t);
    ensureStreamingCapacity(device, vtxBytes, idxBytes);

    dctx.updateBuffer(g_vb, 0, vtxBytes, list.vertices.data());
    dctx.updateBuffer(g_ib, 0, idxBytes, list.indices.data());

    // Pixel -> NDC transform (top-left origin canvas of displayWidth x displayHeight).
    UiPushConstantsCpu pc = {};
    pc.scale[0]     = 2.0f / static_cast<float>(list.displayWidth);
    pc.scale[1]     = 2.0f / static_cast<float>(list.displayHeight);
    pc.translate[0] = -1.0f;
    pc.translate[1] = -1.0f;

    // Vertex layout for remixapi_UIVertex.
    DxvkVertexBinding binding = {};
    binding.binding   = 0;
    binding.fetchRate = 0;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    DxvkVertexAttribute attrs[3] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = offsetof(remixapi_UIVertex, x);
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT;    attrs[1].offset = offsetof(remixapi_UIVertex, u);
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;   attrs[2].offset = offsetof(remixapi_UIVertex, color);
    dctx.setInputLayout(3, attrs, 1, &binding);

    DxvkInputAssemblyState ia = {};
    ia.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestart  = VK_FALSE;
    dctx.setInputAssemblyState(ia);

    DxvkRasterizerState rs = {};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    dctx.setRasterizerState(rs);

    DxvkMultisampleState ms = {};
    ms.sampleMask = 0xFFFFFFFFu;
    dctx.setMultisampleState(ms);

    // Flat 2D draws composite in painter order with no depth interaction; 3D
    // draws (inventory models, block icons) test+write the UI depth buffer.
    bool needsDepth = false;
    for (const fork_ui::UiDrawCmd& cmd : list.commands) {
      if (cmd.flags & REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST) {
        needsDepth = true;
        break;
      }
    }

    DxvkDepthStencilState ds2d = {};
    ds2d.enableDepthTest   = VK_FALSE;
    ds2d.enableDepthWrite  = VK_FALSE;
    ds2d.enableStencilTest = VK_FALSE;
    ds2d.depthCompareOp    = VK_COMPARE_OP_ALWAYS;

    DxvkDepthStencilState ds3d = {};
    ds3d.enableDepthTest   = VK_TRUE;
    ds3d.enableDepthWrite  = VK_TRUE;
    ds3d.enableStencilTest = VK_FALSE;
    ds3d.depthCompareOp    = VK_COMPARE_OP_LESS_OR_EQUAL;

    dctx.setDepthStencilState(ds2d);

    DxvkBlendMode bm = {};
    bm.enableBlending = VK_TRUE;
    bm.colorSrcFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    bm.colorDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bm.colorBlendOp   = VK_BLEND_OP_ADD;
    bm.alphaSrcFactor = VK_BLEND_FACTOR_ONE;
    bm.alphaDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bm.alphaBlendOp   = VK_BLEND_OP_ADD;
    bm.writeMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    dctx.setBlendMode(0, bm);

    dctx.bindShader(VK_SHADER_STAGE_VERTEX_BIT, UiVertexShader::getShader());
    dctx.bindShader(VK_SHADER_STAGE_FRAGMENT_BIT, UiPixelShader::getShader());
    // Preceding compute passes (tone-map, tint, overlay) leave the RTX push
    // bank active; the graphics pipeline reads push constants from the D3D9
    // bank, so restore it before writing the screen-space transform.
    dctx.setPushConstantBank(DxvkPushConstantBank::D3D9);
    dctx.pushConstants(0, sizeof(pc), &pc);

    // Bind the final tone-mapped image as the colour attachment.
    auto& finalOutput = rtOutput.m_finalOutput.resource(Resources::AccessType::ReadWrite);
    const VkExtent3D outputExtent = finalOutput.image->info().extent;

    if (needsDepth) {
      ensureUiDepth(&dctx, outputExtent.width, outputExtent.height);
    }

    DxvkRenderTargets rt;
    rt.color[0].view   = ctx.getResourceManager().getCompatibleViewForView(
      finalOutput.view, finalOutput.image->info().format);
    rt.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;
    if (needsDepth) {
      rt.depth.view   = g_uiDepthView;
      rt.depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    dctx.bindRenderTargets(rt);

    VkViewport vp = {};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width  = static_cast<float>(outputExtent.width);
    vp.height = static_cast<float>(outputExtent.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc = { { 0, 0 }, { outputExtent.width, outputExtent.height } };
    dctx.setViewports(1, &vp, &sc);

    DxvkBufferSlice vbSlice(g_vb, 0, vtxBytes);
    DxvkBufferSlice ibSlice(g_ib, 0, idxBytes);
    dctx.bindVertexBuffer(0, vbSlice, sizeof(remixapi_UIVertex));
    dctx.bindIndexBuffer(ibSlice, VK_INDEX_TYPE_UINT32);

    // REPEAT addressing matches the game's GUI texture wrap mode: tiled
    // backgrounds (the menu dirt) use UVs far beyond [0,1] to repeat a small
    // atlas; all other UI quads sample within [0,1] where REPEAT == CLAMP.
    const Rc<DxvkSampler> sampler = ctx.getResourceManager().getSampler(
      VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    bool depthStateOn = false;
    for (const fork_ui::UiDrawCmd& cmd : list.commands) {
      const UiTexture* tex = &g_whiteTexture;
      if (cmd.textureId != 0) {
        auto it = g_uiTextures.find(cmd.textureId);
        if (it == g_uiTextures.end() || !it->second.view.ptr()) {
          continue;   // texture not (yet) registered — skip this draw
        }
        tex = &it->second;
      }

      const bool wantDepth = (cmd.flags & REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST) != 0;
      if (wantDepth != depthStateOn) {
        dctx.setDepthStencilState(wantDepth ? ds3d : ds2d);
        depthStateOn = wantDepth;
      }

      dctx.bindResourceView(UI_TEXTURE0_INPUT, tex->view, nullptr);
      dctx.bindResourceSampler(UI_TEXTURE0_INPUT, sampler);
      dctx.drawIndexed(cmd.indexCount, 1, cmd.indexOffset, cmd.vertexOffset, 0);
    }
  }

} // namespace fork_hooks
} // namespace dxvk
