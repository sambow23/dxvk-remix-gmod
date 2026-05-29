# Remix Screen-Space UI API

The UI API composites a game's 2D UI directly over the final
tone-mapped image on the GPU, replacing the older CPU
framebuffer-readback overlay path (`DrawScreenOverlay`). The plugin
registers the textures its UI samples, then submits a textured-quad
**draw list** each frame; the runtime rasterizes it as a screen-space
pass at the post-tonemap overlay slot.

This is part of the typed C API surface
([`remix_c.h`](../public/include/remix/remix_c.h)); see
[`RemixApi.md`](RemixApi.md) for the hub reference.

## Mental model

- **Textures are persistent.** Register each UI atlas / texture once
  with `RegisterUITexture` under a caller-chosen `id` (e.g. a GL texture
  name). Update it by calling `RegisterUITexture` again with the same
  `id`; free it with `FreeUITexture`. `id` 0 is reserved as a built-in
  1×1 opaque-white texture for untextured / solid-colour quads.
- **The draw list is per-frame.** Build a flat vertex buffer, a 32-bit
  index buffer, and an array of draw commands, then call
  `SubmitUIDrawList` once per frame before `Present`. A null or empty
  draw list clears the UI for that frame.
- **Painter's algorithm by default.** Commands render in array order;
  later commands draw on top. There is no per-draw clip rectangle.
  Flat 2D commands ignore depth; opt into depth testing per command with
  `REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST` for self-occluding 3D geometry (see
  [Depth-tested 3D draws](#depth-tested-3d-draws)).
- **Coordinates** are screen-space pixels in a **top-left origin**,
  mapped onto the draw list's `displayWidth` × `displayHeight` logical
  canvas (typically the game's UI resolution). The runtime scales the
  canvas to the render output extent.
- **Blending** is straight (non-premultiplied) alpha:
  `srcAlpha, 1 - srcAlpha`. The fragment colour is `vertexColor ×
  texture.Sample(uv)`.

## Types

```c
typedef uint64_t remixapi_UITextureHandle;   // caller-chosen id; 0 = built-in white

typedef struct remixapi_UIVertex {
  float    x;       // screen-space pixel position (top-left origin)
  float    y;
  float    z;       // normalized depth [0,1]; 0 for flat 2D quads
  float    u;       // texture coordinate
  float    v;
  uint32_t color;   // packed RGBA8, R in the least-significant byte
} remixapi_UIVertex;

// remixapi_UIDrawCommand::flags bits.
#define REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST 0x1u

typedef struct remixapi_UIDrawCommand {
  remixapi_UITextureHandle textureId;     // texture to sample (0 = white)
  uint32_t                 indexCount;    // indices consumed by this draw
  uint32_t                 indexOffset;   // first index into pIndices
  int32_t                  vertexOffset;  // added to each index
  uint32_t                 flags;         // REMIXAPI_UI_DRAW_FLAG_* bitmask
} remixapi_UIDrawCommand;

typedef struct remixapi_UIDrawList {
  remixapi_StructType           sType;          // REMIXAPI_STRUCT_TYPE_UI_DRAW_LIST
  void*                         pNext;
  uint32_t                      displayWidth;   // logical UI canvas size, px
  uint32_t                      displayHeight;
  const remixapi_UIVertex*      pVertices;
  uint32_t                      vertexCount;
  const uint32_t*               pIndices;
  uint32_t                      indexCount;
  const remixapi_UIDrawCommand* pCommands;
  uint32_t                      commandCount;
} remixapi_UIDrawList;
```

## Functions

### `RegisterUITexture`

```c
remixapi_ErrorCode RegisterUITexture(
    remixapi_UITextureHandle id,
    uint32_t                 width,
    uint32_t                 height,
    remixapi_Format          format,
    const void*              pPixelData,
    uint64_t                 dataSize);
```

Registers or updates a UI texture from CPU pixel data. If a texture with
the same `id`, dimensions and format already exists, its contents are
updated in place; otherwise it is (re)created. Accepts only the 8-bit
`R8G8B8A8` / `B8G8R8A8` (`UNORM` / `SRGB`) formats. `dataSize` must equal
`width * height * 4`. `id` 0 is rejected
(`REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS`). The pixel data is copied; the
caller may free its buffer immediately after the call returns. The GPU
upload is applied on the render thread at the next `Present`.

### `FreeUITexture`

```c
remixapi_ErrorCode FreeUITexture(remixapi_UITextureHandle id);
```

Releases a previously registered UI texture. Unknown ids are ignored.
`id` 0 cannot be freed.

### `SubmitUIDrawList`

```c
remixapi_ErrorCode SubmitUIDrawList(const remixapi_UIDrawList* drawList);
```

Submits the UI draw list for the next presented frame. The vertex, index
and command arrays are copied; the caller retains ownership of its
buffers. `sType` must be `REMIXAPI_STRUCT_TYPE_UI_DRAW_LIST`. A null draw
list, or one with no commands / vertices / indices, clears the UI for the
next frame.

## Minimal usage

```c
// Once: upload the HUD atlas.
iface.RegisterUITexture(/*id*/ 1, atlasW, atlasH,
                        REMIXAPI_FORMAT_R8G8B8A8_UNORM,
                        atlasPixels, (uint64_t)atlasW * atlasH * 4);

// Each frame: build and submit the draw list, then Present.
remixapi_UIDrawList list = {0};
list.sType         = REMIXAPI_STRUCT_TYPE_UI_DRAW_LIST;
list.displayWidth  = uiWidth;
list.displayHeight = uiHeight;
list.pVertices     = vertices;   list.vertexCount  = vertexCount;
list.pIndices      = indices;    list.indexCount   = indexCount;
list.pCommands     = commands;   list.commandCount = commandCount;
iface.SubmitUIDrawList(&list);

iface.Present(NULL);
```

## Depth-tested 3D draws

By default a draw composites flat in submission (painter) order. To render
3D screen-space geometry that must self-occlude — inventory model previews,
3D block item icons — set `REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST` in the
command's `flags` and provide per-vertex `z` (normalized depth in `[0,1]`,
near = 0). Depth-tested commands test and write a depth buffer that is
cleared once at the start of the pass; flat 2D commands (`flags` = 0,
`z` = 0) ignore the depth buffer entirely and keep painter ordering.

Mix freely within one draw list: a 2D panel, then a depth-tested 3D model,
then 2D icons/text on top all compose correctly because the 2D draws never
write depth and respect submission order, while the 3D draw self-occludes
against the cleared buffer. Distinct 3D elements that do not overlap in
screen space share the buffer safely. Lighting is the caller's
responsibility — bake it into the vertex `color` (the pass does no shading).

## Threading

`RegisterUITexture`, `FreeUITexture` and `SubmitUIDrawList` may be called
from the plugin's render/API thread; they copy their inputs into a
pending buffer guarded by an internal mutex. The runtime drains that
buffer onto its own render thread at `Present`, where texture uploads are
applied and the draw list is rasterized. There is no requirement to call
these on any specific thread relative to the rest of the API.

## Notes

- The pass renders after tone mapping, the screen tint, and the legacy
  screen overlay, so the UI always lands on top of the composited scene.
- Flat 2D draws have no depth test; order them correctly within the
  command array. For self-occluding 3D geometry use
  `REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST` with per-vertex `z` (see above).
