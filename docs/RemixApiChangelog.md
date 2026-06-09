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

### Changed

### Fixed
- Restored the `remixapi_Interface.SetGameValue` / `remixapi_Interface.GetGameValue` vtable slots, the `REMIXAPI remixapi_SetGameValue` export prototype, and the `Interface::SetGameValue` C++ wrapper, all dropped by an upstream merge. `remixapi_SetGameValue` is exported from d3d9.dll again and both slots are populated by `remixapi_InitializeLibrary` (`sizeof(remixapi_Interface)` 320 → 336).

### Removed
- `rtx.atmosphere.sunDisc` (GameStateStore/config key) — removed. The option had no consumer (the sun disc is rendered via the sun-as-distant-light / NEE path); setting it had no effect.
