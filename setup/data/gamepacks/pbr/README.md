# PBR overlay for Quake III Arena

This pack overlays the downloaded Quake III Arena gamepack so that `Q3.game` authors PBR levels: physical light
entities, the lighting preview's documentation, and a build entry that compiles the town. Materials are ordinary
`scripts/*.shader` files with vanilla stages plus top-level `qer_pbr_*` keywords (plan §Material authoring; the
benchmark's `content/benchmark/scripts/bench.shader` is the worked example).

History: until task A7 this pack also held `pbr.game`, a separate game with `.mtr` materials. A7 retired it; it is
in git history before change `retire-pbr-game`.

## Installation

`install-gamepacks.sh`, which the default `make` target runs, installs this pack after the downloaded ones. On an
existing install, or after installing the downloaded packs by hand, run

```
sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks
```

The pack must go after the downloaded Quake III pack, because its files replace some of that pack's.

Lighting preview is camera render mode "Lighting" (`Shift+]` / `Shift+[` cycle modes, `F3` toggles). It is on by
default for Quake 3 based games and can be turned off with the camera preference "PBR lighting preview".

## Layout

```
games/Q3.game                       the downloaded game file with entities="pbr"
Q3.game/baseq3/_pbr_lights.ent      definitions of light, light_spot and light_sun (the only copy)
Q3.game/default_build_menu.xml      the downloaded build menu plus the "Town" build
README.md                           this file (not installed)
```

`games/Q3.game` selects the `pbr` entity module: the `quake3` module plus the editor's physical light entities.
`_pbr_lights.ent` sorts before the stock `entities.ent`, and the first definition of a class name wins, so its
`light` replaces the stock q3map2 light; every other stock entity stays available.

Stock ioquake3 prints `light_spot doesn't have a spawn function` (and the same for `light_sun`) once per entity when
it loads such a map; that is harmless, since the lights are only kept in the BSP for `renderer_sh`. The stock check
allowlists exactly those two lines.

## Build menu

The overlay's `default_build_menu.xml` is the downloaded Quake III menu, unchanged, plus one build at the end:

| Build | Runs |
|---|---|
| Town: BSP -keeplights + VIS + light (placeholder) | `-meta -keeplights`, then `-vis -saveprt`, then `-light -patchshadows` |

These are the stock check's compile stages (`tools/stockcheck/`), so a compile from the editor is the compile the
check gates. `-keeplights` keeps every light entity in the BSP for `renderer_sh`. The `-light` lightmap is a
placeholder: q3map2 treats every classname starting with `light` as its own light and reads none of the physical
keys, so the stock lighting looks nothing like the preview until sh-baker writes the lightmap (`-shbake`, task B5,
which joins this build).

The editor reads `<settings>/Q3.game/build_menu.xml` in preference to the default menu once that file exists (it is
written when the menu is customised). If the Town build is missing, delete that file or add the build by hand.

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

The editor preview, sh-baker and `renderer_sh` share this model. `renderer_sh` reads these entities from the BSP
(task C4), which `-keeplights` keeps there.

## Shading

Metallic-roughness BRDF ported from `sh-renderer/glsl/radiance.frag` (`ComputeDirectBRDF`): GGX normal
distribution with roughness clamped to ≥ 0.05, Smith geometry with the Schlick-GGX approximation
(`k = (roughness + 1)² / 8`), Schlick Fresnel with `F0 = mix(0.04, basecolor, metallic)`, specular denominator
`4 N·V N·L + 0.001`, Disney (Burley) diffuse weighted by `kD = (1 − F)(1 − metallic)`.

One additive pass per light plus a base pass writing
`emissive × emissive colour × qer_pbr_emissiveStrength + ambient × basecolor × occlusion`. The emissive map is the
first additive stage's image and its colour is that stage's `rgbGen const` (white without one); the base colour is
the stage the lightmap multiplies. Sources: `gl/pbr_vp.glsl`, `gl/pbr_fp.glsl`, `gl/pbr_base_vp.glsl`,
`gl/pbr_base_fp.glsl`.

The preview lights exactly the lightmapped, non-blended shaders that are not sky, fog or `nolightmap`, whether or not
they declare `qer_pbr_*`; a shader without PBR keywords renders as a fully rough dielectric. Every other shader
draws with its textured-mode state, unlit.

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

Base colour and emissive are decoded from sRGB in the shader with the exact piecewise transfer function. The texture
gamma preference is never applied to a texture loaded for lighting mode; textured mode keeps it.

Known differences from the engine, kept on purpose:

- The engine's ambient comes from the baked SH lightmaps (task C6); the editor's constant ambient is a stand-in and
  should be 0 for the parity check. The engine ignores occlusion entirely for now.
- The engine reads occlusion from the R channel of the metallic-roughness texture (ORM packing); the editor reads the
  separate `qer_pbr_occlusion` map. Pack both ways identically until the engine reads the separate map.
- The engine's alpha cutoff is fixed at 0.5. The editor uses the base colour stage's `alphaFunc` (`GT0`, `LT128` or
  `GE128`). The engine has no blended lit surfaces, and neither has the preview.

## Shadows

Lighting mode renders shadow maps for `light_sun` and `light_spot`. Point lights never cast: `light` ignores
`_shadows` and its contribution is never attenuated. Spot and sun lights read `_shadows` (boolean, default 1);
setting it to 0 makes that light shine through occluders and frees its region of the atlas. The camera preference
"Lighting shadows" (default on) disables generation and sampling for the whole scene and returns the unshadowed image.

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
| Near-plane padding | 65536 map units (the world extent) | editor | `c_shadowCascadeNearPadding`, `radiant/cascade.h`; the engine's `z_padding` is 20 in metres, and 20 map units clipped walls and ceilings out of the caster pass |
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

### The caster rule

The editor's caster rule, which the SH baker follows:

- **Never cast:** `surfaceparm sky` (a low sky ceiling would otherwise occlude the sun everywhere), `fog` (its faces
  bound a volume the light travels through, not a surface that stops it), `water`, `slime` and `lava`, non-solid
  `nodraw`, `playerclip`, `botclip`, `areaportal`, `noshadows`, `trigger` or `hint` (all three map to the same
  editor flag), and `surfaceparm trans` unless the shader also has `surfaceparm alphashadow`.
- **Cast solid:** solid `nodraw` (caulk).
- **Cast through their alpha test:** alpha-tested shaders, so a fence casts its cut-out pattern. The test is the base
  colour stage's `alphaFunc` (`GT0`, `LT128` or `GE128`), not `qer_alphafunc`.
- **Cast:** every other drawn surface. A blended shader without `surfaceparm trans` casts a **solid** shadow: a single
  depth map cannot represent partial occlusion. Put `surfaceparm trans` on glass to take it out of the caster set,
  which is usually what you want.

Shadow maps persist between frames. The spot atlas is regenerated when the scene graph changes or any light is
attached, detached or edited; the sun cascades are regenerated for those and also whenever the camera moves,
because they are fitted to its frustum. A static scene viewed from a static camera costs sampling only.

If the context reports fewer than seven texture image units, or the depth-texture framebuffers cannot be created,
lighting mode renders unshadowed and warns once in the console. More than 16 shadow-casting spot lights is not an
error: the 16 nearest the camera get tiles, the rest render unshadowed, and a warning is printed once.

## Benchmark and engine parity

The benchmark is the `benchmark` mod in `content/benchmark/`; its README describes the map, materials, lights,
views and stock check.

### Parity check against the engine

Documented view: the benchmark's `sign` view at the eye position the engine reaches, origin `(-96, 52, 50)`, angles
`0 90 0` (pitch 0, yaw 90: facing building A and the neon sign), field of view 90, exposure 1.0, window 1280×720.
These values are also recorded in `content/fixtures/baseline.txt`.

The engine side of this comparison is `renderer_sh`'s direct-light-only path, which has no shadow maps, so turn the
"Lighting shadows" preference **off** for it, and set "Lighting ambient" to 0. Comparing the shadowed image needs the
engine's shadow path enabled and its cascade range matched to `c_shadowDistance`; see the shadow distance note above.

Procedure:

1. Editor: open `bench.map` under `Q3.game` with the `benchmark` mod, switch to lighting mode, set the camera to the
   documented view and take a screenshot. This can be scripted: with `NETRADIANT_CAMERA_SCREENSHOT=<file.png>` in
   the environment the camera view is saved a few frames after the first draw; `NETRADIANT_CAMERA_ORIGIN="x y z"` and
   `NETRADIANT_CAMERA_ANGLES="pitch yaw roll"` set the view first, and the preferences can be forced from the command
   line (the editor doesn't exit afterwards):

   ```
   NETRADIANT_CAMERA_SCREENSHOT=sign.png NETRADIANT_CAMERA_ORIGIN="-96 52 50" NETRADIANT_CAMERA_ANGLES="0 90 0" \
     install/radiant.x86_64 -global-gamefile Q3.game -Q3.game-GameName benchmark -Q3.game-CameraRenderMode 4 \
     -Q3.game-fieldOfView 90 -Q3.game-LightingShadows false -Q3.game-LightingAmbient 0 \
     <enginepath>/benchmark/maps/bench.map
   ```

   (`CameraRenderMode 4` is lighting mode.) The entity boxes are drawn in the capture; disable them with the entity
   filters, or keep the camera outside them.
2. Engine: compile with the Town build (or the stock check), load the map in `renderer_sh` with
   `+set fs_game benchmark +devmap bench` and the direct-light-only path (no shadow maps, no SH indirect, no SSAO, no
   sky emission), same resolution, and take a screenshot from the same view.
3. Compare in linear light after decoding sRGB. Pass criterion: mean absolute error over all pixels ≤ 2/255 and no
   8×8 block with a mean absolute error above 6/255, ignoring the outermost 8 pixel border and any pixel where the
   editor draws overlays (entity boxes, light shapes).

Differences that are expected and must stay inside the tolerance: mip selection and anisotropy, MSAA of the window
target (the HDR target is single-sampled), and the editor's per-face light culling window versus the engine's.
