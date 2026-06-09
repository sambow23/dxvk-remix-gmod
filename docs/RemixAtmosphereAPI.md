# Remix Atmosphere API

The atmosphere system exposes game-driven celestial controls through the
standard Remix C API `SetGameValue` channel. There is no dedicated
`remixapi_SetMoonState` function; instead, games and plugins write
string-keyed values into GameStateStore under the `__atmosphere.*`
namespace.

For the generic channel itself, see
[`RemixApi.md`](RemixApi.md#game-state--setgamevalue--getgamevalue). For
the runtime-side consumer, see
[`rtx_fork_atmosphere.cpp`](../src/dxvk/rtx_render/rtx_fork_atmosphere.cpp).

## Surface 1 — Game-driven moon cycle

The runtime supports up to four moons, matching `MAX_MOONS` in
[`atmosphere_args.h`](../src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h).
Each moon slot is addressed by index: `moon0` through `moon3`.

Write these keys with `SetGameValue` to drive the per-frame moon cycle:

```c
iface.SetGameValue("__atmosphere.moon0.enabled",   "true");
iface.SetGameValue("__atmosphere.moon0.elevation", "28.0");
iface.SetGameValue("__atmosphere.moon0.rotation",  "135.0");
iface.SetGameValue("__atmosphere.moon0.phase",     "0.5");
```

| Key | Type | Default when absent | Meaning |
| :-- | :-: | :-- | :-- |
| `__atmosphere.moonN.enabled` | bool-as-string | leave current runtime state unchanged | Enables or disables moon slot `N`. Accepted values: `1/0`, `true/false`, `yes/no`, `on/off` (case-insensitive). |
| `__atmosphere.moonN.elevation` | float-as-string | leave current runtime state unchanged | Moon elevation in degrees. Typical range `[-90, 90]`. |
| `__atmosphere.moonN.rotation` | float-as-string | leave current runtime state unchanged | Moon azimuth / rotation in degrees. Typical range `[0, 360]`. |
| `__atmosphere.moonN.phase` | float-as-string | leave current runtime state unchanged | Moon phase scalar. `0 = new`, `0.25 = first quarter`, `0.5 = full`, `0.75 = third quarter`, `1.0 = new` wrap. |

### Runtime semantics

- The runtime reads these keys during the per-frame atmosphere update in
  Physical Atmosphere mode.
- Missing keys are ignored; the current runtime option value is preserved.
- Invalid values are ignored; there is no partial reset on parse failure.
- These keys drive only cycle state (`enabled`, `elevation`, `rotation`,
  `phase`). Appearance controls such as angular radius, brightness,
  surface style, crater density, and roughness remain on the persistent
  `rtx.atmosphere.moonN.*` config surface.

This split is deliberate: the game owns fast-changing orbital state,
while modders and users keep artistic moon appearance in config.

## Surface 2 — Persistent appearance tuning

Per-moon appearance stays on the existing RTX option surface:

- `rtx.atmosphere.moonN.angularRadius`
- `rtx.atmosphere.moonN.brightness`
- `rtx.atmosphere.moonN.color`
- `rtx.atmosphere.moonN.surfaceStyle`
- `rtx.atmosphere.moonN.craterDensity`
- `rtx.atmosphere.moonN.surfaceContrast`
- `rtx.atmosphere.moonN.surfaceNoiseScale`
- `rtx.atmosphere.moonN.darkSideBrightness`
- `rtx.atmosphere.moonN.roughnessAmount`

These are intentionally not mirrored into `__atmosphere.*` in this
first slice.

## Notes for integrators

- Write all four keys for a moon from the same simulation tick so disk
  placement, phase shading, and moon direct lighting stay in sync.
- If your game has only one moon, use `moon0` and leave the remaining
  slots untouched.
- If you stop writing a key, the runtime keeps the last successfully
  applied value. Explicitly write a new value when ownership changes.