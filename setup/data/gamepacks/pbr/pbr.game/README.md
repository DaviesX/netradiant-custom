# PBR gamepack

Quake 3 map format with glTF-style metallic-roughness materials and physically based lights.
This pack is the Stage 1 deliverable of the PBR plan: material format, light entities, editor lighting preview.

## Installation

Copy `games/pbr.game` into `<radiant>/gamepacks/games/` and `pbr.game/` into `<radiant>/gamepacks/`.
On first start choose "PBR" in the game dialog. When asked for the engine path, either

- point it at `<radiant>/gamepacks/pbr.game/` to use the bundled benchmark (its `base/` holds the materials,
  textures and map), or
- point it at your game install; Radiant then reads `<enginepath>/base/materials/*.mtr`.

Lighting preview is camera render mode "Lighting" (`Shift+]` / `Shift+[` cycle modes, `F3` toggles).

## Layout

```
games/pbr.game                     game description (shaders="pbr", entities="pbr")
pbr.game/default_build_menu.xml    q3map2 build menu: BSP, BSP + VIS, BSP + VIS + light (fast)
pbr.game/base/entities.ent         entity definitions (light, light_spot, light_sun, ...)
pbr.game/base/default_shaderlist.txt
pbr.game/base/materials/bench.mtr  benchmark materials
pbr.game/base/textures/bench/      benchmark textures (generated, TGA)
pbr.game/base/maps/bench.map       benchmark map
pbr.game/base/scripts/bench.shader q3map2 compile shim, see below
```

## Material format (`materials/*.mtr`)

One block per material, one keyword per line, case-insensitive, unknown keywords skipped, nested `{ }` blocks skipped.
Name materials `textures/...` so they appear in the texture browser.

| Keyword | Arguments | Default |
|---|---|---|
| `basecolor` | texture (sRGB) | white |
| `normal` | texture (linear, tangent space) | flat |
| `metallicroughness` | texture (linear, G = roughness, B = metallic) | white |
| `occlusion` | texture (linear, R = occlusion) | white |
| `emissive` | texture (sRGB) | black |
| `basecolorfactor` | r g b a | 1 1 1 1 |
| `metallicfactor` | float | 1 |
| `roughnessfactor` | float | 1 |
| `emissivefactor` | r g b | 0 0 0 |
| `emissivestrength` | float | 1 |
| `alphamode` | `opaque` / `mask` / `blend` | opaque |
| `alphacutoff` | float | 0.5 |
| `doublesided` | | off |
| `qer_editorimage` | texture | basecolor |
| `qer_trans` | float | |
| `qer_nocarve` | | |
| `surfaceparm` | nodraw nonsolid water lava slime fog areaportal playerclip botclip sky | |

The texture gamma preference is never applied to textures referenced by a PBR material. Base colour and emissive
are decoded from sRGB in the shader with the exact piecewise transfer function.

The `.mtr` files are the single material source for the editor and the SH baker. q3map2 does not read them yet:
for BSP/VIS the surfaceparms that matter (sky, fog, nodraw, clip, nonsolid) are mirrored in
`scripts/bench.shader`, listed by `scripts/shaderlist.txt`. Keep it in sync with `materials/bench.mtr` until
q3map2 gains a `.mtr` reader.

## Light entities and units

| Entity | Keys | Units |
|---|---|---|
| `light` | `origin`, `_color`, `intensity` (50000), `radius` (256) | intensity = radiant flux |
| `light_spot` | as `light` plus `angles` or `target`, `cone` (45), `cone_inner` (30) | intensity = radiant flux, cone half-angles in degrees |
| `light_sun` | `angles`, `_color`, `intensity` (3) | intensity = irradiance on a surface facing the sun |

- Point radiant intensity: `I = flux / (4π)`.
- Spot radiant intensity: `I = flux / (2π (1 − cos(cone)))`; narrowing the cone concentrates the same flux.
  Cone falloff: `smoothstep(cos(cone), cos(cone_inner), cos(angle))`.
- Irradiance at distance `d`: `E = I / d²` with `d` in **map units**, zero beyond `radius`.
- Sun: `E = intensity`, no falloff, every lit surface receives it. `angles` is pitch/yaw/roll of the direction the
  light *travels* (Quake convention, positive pitch looks down). At most one sun is honoured: the first in map order.
- `_color` is linear RGB and multiplies the irradiance.
- **Spatial scale is the Quake 3 map unit.** The engine (`sh-renderer`) divides by the squared distance in raw
  scene units and never converts to metres, so neither does the editor. A watt here is therefore "per map unit²",
  which is why sensible flux values are in the tens of thousands (a 50000 light gives `E ≈ 1` at 64 units).

### Mapping to the engine

`sh-renderer` loads `KHR_lights_punctual` and multiplies every glTF `intensity` by `1/200` (`kLightIntensityScale`
in `src/loader.cpp`), then shades `color × intensity / d²` for point and spot lights and `color × intensity` for
the sun. For the editor and engine to agree, the map-to-glTF exporter must write

| Map entity | glTF `intensity` | glTF `range` | glTF `spot` |
|---|---|---|---|
| `light` | `200 × flux / (4π)` | `radius` | |
| `light_spot` | `200 × flux / (2π (1 − cos(cone)))` | `radius` | `innerConeAngle = cone_inner`, `outerConeAngle = cone` (radians) |
| `light_sun` | `200 × intensity` | | |

`range` must always be written: without it the engine derives a radius as `sqrt(intensity × max(color) / 0.01)`.
The engine culls point and spot lights per 16×16 screen tile by `range`, so a light contributes nothing beyond it;
the editor reproduces that with a hard cutoff at `radius`.

## Shading

Metallic-roughness BRDF ported from `sh-renderer/glsl/radiance.frag` (`ComputeDirectBRDF`): GGX normal
distribution with roughness clamped to ≥ 0.05, Smith geometry with the Schlick-GGX approximation
(`k = (roughness + 1)² / 8`), Schlick Fresnel with `F0 = mix(0.04, basecolor, metallic)`, specular denominator
`4 N·V N·L + 0.001`, Disney (Burley) diffuse weighted by `kD = (1 − F)(1 − metallic)`.
One additive pass per light plus a base pass writing `emissive × emissivefactor × emissivestrength + ambient × basecolor × occlusion`.
Sources: `gl/pbr_vp.glsl`, `gl/pbr_fp.glsl`, `gl/pbr_base_vp.glsl`, `gl/pbr_base_fp.glsl`.

The scene is rendered into an RGBA16F framebuffer and resolved to the window with

```
curr = Uncharted2( hdr × exposure )      // Hable: A=0.15 B=0.50 C=0.10 D=0.20 E=0.02 F=0.30
ldr  = curr / Uncharted2( 11.2 )         // white point W = 11.2
out  = sRGB_encode( ldr )
```

`gl/tonemap_fp.glsl`, ported from `sh-renderer/glsl/tonemap.frag` (the engine encodes sRGB through
`GL_FRAMEBUFFER_SRGB`, same transfer function; its exposure is fixed at 1.0). Exposure and ambient are camera
preferences ("Lighting exposure", default 1.0; "Lighting ambient", default 0.02). If the half-float framebuffer
cannot be created, lighting mode renders straight to the window without tonemapping and warns once in the console.

Known differences from the engine, kept on purpose:

- The engine's ambient comes from the baked SH lightmaps (Stage 3); the editor's constant ambient is a stand-in and
  should be 0 for the parity check. The engine ignores `occlusion` entirely for now.
- The engine reads occlusion from the R channel of the metallic-roughness texture (ORM packing); the editor reads a
  separate `occlusion` map. Pack both ways identically until the engine reads the separate map.
- The engine's alpha cutoff is fixed at 0.5 (`alphacutoff` is editor-only), and it has no `blend` mode.

## Benchmark map (`maps/bench.map`)

A street block between two building shells with enterable interiors, a low sky ceiling (z = 256), a fog volume on
the street, one `light_sun`, two `light`, two `light_spot` (one targeted at the neon sign via `info_null`).
Materials exercised: base colour + normal + metallic-roughness + occlusion (`bench/brick`), rough dielectric
(`bench/asphalt`), polished metal (`bench/metal`), emissive (`bench/neon`), alpha mask + double-sided
(`bench/fence`), alpha blend (`bench/glass`), double-sided opaque (`bench/tarp`), plus sky, fog, caulk and clip.

Compiles leak-free with "BSP + VIS" (36 brushes, 8 entities; verified with the bundled q3map2).

### Parity check against the engine

Documented view: camera at the `info_player_start` origin `(-96, -64, 24)` plus the player eye height used by the
engine, yaw 90° (facing building A and the neon sign), pitch 0, field of view 90, exposure 1.0, ambient 0.02,
window 1280×720.

Procedure:

1. Editor: open `bench.map`, switch to lighting mode, set the camera to the documented view and take a screenshot.
   This can be scripted: with `NETRADIANT_CAMERA_SCREENSHOT=<file.png>` in the environment the camera view is
   saved a few frames after the first draw; `NETRADIANT_CAMERA_ORIGIN="x y z"` and
   `NETRADIANT_CAMERA_ANGLES="pitch yaw roll"` set the view first, and the preferences can be forced from the
   command line, e.g.

   ```
   radiant.exe -global-gamefile pbr.game -pbr.game-EnginePath <path>/gamepacks/pbr.game/ -pbr.game-CameraRenderMode 4 <path>/gamepacks/pbr.game/base/maps/bench.map
   ```

   (`CameraRenderMode 4` is lighting mode.) Note the entity boxes are drawn in the capture; place the camera
   outside them (the spawn box spans 24 units around its origin).
2. Engine: compile with "BSP + VIS", load the map with the direct-light-only path (no shadow maps, no SH indirect,
   no SSAO, no sky emission), editor ambient set to 0, same resolution, take a screenshot from the same view.
3. Compare in linear light after decoding sRGB. Pass criterion: mean absolute error over all pixels ≤ 2/255 and no
   8×8 block with a mean absolute error above 6/255, ignoring the outermost 8 pixel border and any pixel where the
   editor draws overlays (entity boxes, light shapes). Overlays can be disabled with the entity filters before the
   screenshot.

Differences that are expected and must stay inside the tolerance: mip selection and anisotropy, MSAA of the window
target (the HDR target is single-sampled), and the editor's per-face light culling window versus the engine's.
