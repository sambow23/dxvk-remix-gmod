#pragma once

// rtx_fork_ui.h — shared types and render-thread entry points for the
// screen-space UI rendering subsystem (rtx_fork_ui.cpp).
//
// The subsystem is split across two translation units so that the render-side
// code (dispatchUi, referenced by rtx_context.cpp) never instantiates
// D3D9DeviceEx::EmitCs — that template pulls in private device symbols that the
// unit-test executables do not link. The API-thread staging + present flush
// (which DO use EmitCs) live in rtx_fork_api_entry.cpp and hand work to the
// render-thread mutators declared here.

#include <cstdint>
#include <vector>

#include <remix/remix_c.h>                  // remixapi_UIVertex, remixapi_Format
#include "../../util/rc/util_rc_ptr.h"      // Rc<>

namespace dxvk {

  class DxvkContext;
  class DxvkBuffer;

  namespace fork_ui {

    struct UiDrawCmd {
      uint64_t textureId = 0;
      uint32_t indexCount = 0;
      uint32_t indexOffset = 0;
      int32_t  vertexOffset = 0;
      uint32_t flags = 0;        // REMIXAPI_UI_DRAW_FLAG_* bitmask
    };

    struct UiDrawList {
      uint32_t                       displayWidth = 0;
      uint32_t                       displayHeight = 0;
      std::vector<remixapi_UIVertex> vertices;
      std::vector<uint32_t>          indices;
      std::vector<UiDrawCmd>         commands;
    };

    struct UiTextureUpload {
      uint64_t        id = 0;
      uint32_t        width = 0;
      uint32_t        height = 0;
      remixapi_Format format = REMIXAPI_FORMAT_R8G8B8A8_UNORM;
      Rc<DxvkBuffer>  staging;
    };

    // True if the format is one the UI texture path accepts (8-bit RGBA/BGRA).
    bool isSupportedFormat(remixapi_Format format);

    // Render-thread state mutators (defined in rtx_fork_ui.cpp). Called from
    // the presentUiFlush EmitCs lambda in rtx_fork_api_entry.cpp.
    void applyTextureUpload(DxvkContext* ctx, const UiTextureUpload& upload);
    void freeTexture(uint64_t id);
    void setCurrentDrawList(UiDrawList&& list);

  } // namespace fork_ui
} // namespace dxvk
