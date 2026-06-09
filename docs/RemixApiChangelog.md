## Remix API Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.2]

### Added
- MaterialInfoOpaqueEXT.displaceOut

### Changed
- renamed MaterialInfoOpaqueEXT.heightTextureStrength to MaterialInfoOpaqueEXT.displaceIn

### Fixed

### Removed


## [0.4.3]

### Added
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceDiffusionProfile
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadius
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadiusScale
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceMaxSampleRadius
- GameStateStore keys `__atmosphere.moonN.enabled`, `__atmosphere.moonN.elevation`, `__atmosphere.moonN.rotation`, and `__atmosphere.moonN.phase` — plugin-controlled moon cycle state for the existing atmosphere moon renderer. Missing or invalid values leave current runtime state unchanged. See [`docs/RemixAtmosphereAPI.md`](RemixAtmosphereAPI.md).
- GameStateStore keys `__weather.drift_speed` and `__weather.drift_intensity` — plugin-controlled cloud-drift speed and intensity multipliers. Both default to 1.0 when unset. Smoothed inside the renderer with tau = 1.0s. See [`docs/integrators/weather-presets.md`](integrators/weather-presets.md) section 8 for the recommended per-preset values and integration pattern.
- Screen-space UI rendering API: `remixapi_Interface.RegisterUITexture`, `remixapi_Interface.FreeUITexture`, `remixapi_Interface.SubmitUIDrawList`, with the `remixapi_UITextureHandle` handle, the `remixapi_UIVertex` / `remixapi_UIDrawCommand` / `remixapi_UIDrawList` structs, and the `REMIXAPI_STRUCT_TYPE_UI_DRAW_LIST` struct type. Composites a per-frame textured-quad draw list directly over the final tone-mapped image, replacing CPU framebuffer-readback overlays. See [`docs/RemixUIAPI.md`](RemixUIAPI.md).
- `remixapi_UIVertex.z` (normalized depth [0,1]) and `remixapi_UIDrawCommand.flags` with the `REMIXAPI_UI_DRAW_FLAG_DEPTH_TEST` bit, enabling depth-tested 3D screen-space UI geometry (inventory model previews, block item icons) alongside flat 2D draws in the same draw list. See [`docs/RemixUIAPI.md`](RemixUIAPI.md).

### Changed

### Fixed
- Restored the `remixapi_Interface.SetGameValue` / `remixapi_Interface.GetGameValue` vtable slots, the `REMIXAPI remixapi_SetGameValue` export prototype, and the `Interface::SetGameValue` C++ wrapper, all dropped by an upstream merge. `remixapi_SetGameValue` is exported from d3d9.dll again and both slots are populated by `remixapi_InitializeLibrary` (`sizeof(remixapi_Interface)` 320 → 336).

### Removed
