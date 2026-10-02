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

The pack also gives **Quake III Arena** (`Q3.game`) the physical light entities: `games/Q3.game` is the downloaded
game file with `entities="pbr"`, and `Q3.game/baseq3/_pbr_lights.ent` defines `light`, `light_spot` and
`light_sun` (it loads before the stock `entities.ent`, so its `light` wins). `install-gamepacks.sh`, which the
default `make` target runs, installs this pack after the downloaded ones. On an existing install, or after
installing the downloaded packs by hand, run

```
sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks
```

Stock ioquake3 prints `light_spot doesn't have a spawn function` (and the same for `light_sun`) once per entity
when it loads such a map; that is harmless, since the lights are only kept in the BSP for `renderer_sh`.

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
| `light_spot` | as `light` plus `angles` or `target`, `cone` (45), `cone_inner` (30), `_shadows` (1) | intensity = radiant flux, cone half-angles in degrees |
| `light_sun` | `angles`, `_color`, `intensity` (3), `_shadows` (1) | intensity = irradiance on a surface facing the sun |

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

## Shadows

Lighting mode renders shadow maps for `light_sun` and `light_spot`. Point lights never cast: `light` ignores
`_shadows` and its contribution is never attenuated. Spot and sun lights read `_shadows` (boolean, default 1);
setting it to 0 makes that light shine through occluders and frees its region of the atlas. The camera preference
"Lighting shadows" (default on) disables generation and sampling for the whole scene and returns the Stage 1 image.

The scheme is ported from `sh-renderer` so the two renderers can be compared: the caster pass culls **back** faces
and stores the surface nearest the light (first depth), and the bias is paid in the fragment program, rather than
using second-depth (front-face culled) shadow maps. Second depth is undefined for patches and models, which have
no back face, so it would need a second bias regime for free-form geometry.

Every constant below is one of two kinds. **Engine** means it is copied from `sh-renderer` and must not be changed
without changing the engine too, or parity is lost. **Editor** means there is no engine counterpart visible in the
source that was read, and it may need revisiting when parity is attempted.

| Parameter | Value | Source | Where |
|---|---|---|---|
| Caster culling | back faces, no polygon offset | engine | `draw_shadow_map.cpp` |
| Normal offset | `normal × (1 − N·L) × 0.005` (map units) | engine | `radiance.frag` `ComputeShadow` |
| Slope-scaled bias | `0.001 × tan(acos(N·L))`, clamped to `[0, 0.003]` normalised depth | engine | `radiance.frag` `ComputeShadow` |
| PCF kernel | 9 taps on a Poisson disc, rotated per pixel by interleaved gradient noise | engine | `radiance.frag` `PCFPoissonDisk9` |
| Cascade count | 3 | engine | `kNumShadowMapCascades` |
| Cascade resolution | 1024 × 1024 | engine | `kCascadeShadowMapSize` |
| Split lambda | 0.8 | engine | `cascade.cpp` |
| Split distribution | `λ · near · (far/near)^p + (1 − λ) · (near + (far − near) · p)` | engine | `cascade.cpp` |
| Texel snapping | bounds centre floored to the cascade's world-units-per-texel grid | engine | `cascade.cpp` |
| Snap compensation | extents widened by one texel after snapping | editor | snapping shifts the centre up to a texel, which would push the slice corners outside the map |
| Near-plane padding | 20 map units | engine | `cascade.cpp` `z_padding` |
| Sun penumbra factor | `2 / (cascade + 1)` | engine | `radiance.frag` `ComputeSunShadow` |
| Spot penumbra factor | 1.0 | editor | not visible in `radiance.frag` |
| Shadow distance | 4096 map units | editor | `c_shadowDistance`, `radiant/cascade.h` |
| Atlas size | 2048 × 2048, `GL_DEPTH_COMPONENT24` | editor | `radiant/renderstate.cpp` |
| Sun atlas layout | 3 cascades of 1024² in a 2×2 grid, fourth quadrant unused | editor | GLSL 1.20 forbids a dynamic sampler index |
| Spot tile size | 512 × 512, 16 tiles in one 2048² atlas | editor | tile size not visible in the engine source |
| Spot frustum | fov = 2 × `cone`, near 4 map units, far = `radius` | editor | |
| Tile guard band | 1 texel, unwritten, taps clamped into it | editor | prevents bleed between atlas tiles |
| Filtering | `GL_LINEAR` with `GL_COMPARE_R_TO_TEXTURE` / `GL_LEQUAL` | editor | free 2×2 hardware PCF per tap |

**Shadow distance is the one that matters most.** The reference fits its cascades to the camera's own near and far
planes. The editor's far clip is the world diagonal (about 227000 units), which would leave the last cascade at
hundreds of map units per texel, so the cascade range is capped at `c_shadowDistance` instead. Surfaces beyond it
project outside the last cascade and render lit, not shadowed. If the engine's camera range turns out to differ
from `[1, 4096]`, the inherited normalised-depth bias means something different there and will need re-deriving.

Surfaces excluded from the caster set, by material (the editor's caster rule, which the SH baker follows; it
applies to `.mtr` materials and Quake 3 shaders alike): `surfaceparm sky` (a low sky ceiling would otherwise
occlude the sun everywhere), `fog` (its faces bound a volume the light travels through, not a surface that stops
it), `water`, `slime` and `lava`, non-solid `nodraw`, `playerclip`, `botclip`, `areaportal`, `noshadows`,
`trigger` or `hint` (all three map to the same editor flag), and `surfaceparm trans` unless the material also has
`surfaceparm alphashadow`. Solid `nodraw` (caulk) casts. Alpha-tested materials cast through their alpha test, so
a fence casts its cut-out pattern: a `.mtr` mask discards below its cutoff, and a Quake 3 shader uses its base
colour stage's `alphaFunc` (`GT0`, `LT128` or `GE128`), not `qer_alphafunc`. Alpha-blended materials without
`trans` cast a **solid** shadow: a single depth map cannot represent partial occlusion, so `alphaMode blend` is
drawn at full opacity and the benchmark map's glass casts the shadow of an opaque pane. Put `surfaceparm noshadows`
on a `.mtr` material, or `surfaceparm trans` on a Quake 3 shader, to take it out of the caster set; for glass that
is usually what you want. (The `.mtr` parser doesn't read `trans` or `alphashadow`.)

Shadow maps persist between frames. The spot atlas is regenerated when the scene graph changes or any light is
attached, detached or edited; the sun cascades are regenerated for those and also whenever the camera moves,
because they are fitted to its frustum. A static scene viewed from a static camera costs sampling only.

If the context reports fewer than seven texture image units, or the depth-texture framebuffers cannot be created,
lighting mode renders unshadowed and warns once in the console. More than 16 shadow-casting spot lights is not an
error: the 16 nearest the camera get tiles, the rest render unshadowed, and a warning is printed once.

## Benchmark map (`maps/bench.map`)

A street block between two building shells with enterable interiors, a low sky ceiling (z = 256), a fog volume on
the street, one `light_sun`, two `light`, two `light_spot` (one targeted at the neon sign via `info_null`).
Materials exercised: base colour + normal + metallic-roughness + occlusion (`bench/brick`), rough dielectric
(`bench/asphalt`), polished metal (`bench/metal`), emissive (`bench/neon`), alpha mask + double-sided
(`bench/fence`), alpha blend (`bench/glass`), double-sided opaque (`bench/tarp`), plus sky, fog, caulk and clip.

Occlusion is exercised by a free-standing concrete pillar on the floor of the south building, under the downward
`light_spot`, which casts onto the floor; a `bench/metal` patch arch across the west end of the street, which casts
under the sun and the west point light; the two building interiors, whose ceilings seal their floors from the sun
away from the doorways; and the `light_spot` aimed at the neon sign, which sets `_shadows 0` and therefore lights
through the sign.

Compiles leak-free with "BSP + VIS" (37 brushes, 1 patch, 8 entities; verified with the bundled q3map2). q3map2
reports "Entity 7 (info_null): Entity in solid" for the sign aiming point, which is expected and harmless.

### Parity check against the engine

Documented view: camera at the `info_player_start` origin `(-96, -64, 24)` plus the player eye height used by the
engine, yaw 90° (facing building A and the neon sign), pitch 0, field of view 90, exposure 1.0, ambient 0.02,
window 1280×720.

The engine side of this comparison is the direct-light-only path, which has no shadow maps, so turn the "Lighting
shadows" preference **off** for it. Comparing the shadowed image needs the engine's shadow path enabled and its
cascade range matched to `c_shadowDistance`; see the shadow distance note above.

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
