# PBR overlay for Quake III Arena

This pack overlays the downloaded Quake III Arena gamepack so that `Q3.game` authors PBR levels: physical light
entities, the lighting preview's documentation, and three build entries that compile the town. Materials are ordinary
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
Q3.game/default_build_menu.xml      the downloaded build menu plus the three "Town" builds
README.md                           this file (not installed)
```

`games/Q3.game` selects the `pbr` entity module: the `quake3` module plus the editor's physical light entities.
`_pbr_lights.ent` sorts before the stock `entities.ent`, and the first definition of a class name wins, so its
`light` replaces the stock q3map2 light; every other stock entity stays available.

Stock ioquake3 prints `light_spot doesn't have a spawn function` (and the same for `light_sun`) once per entity when
it loads such a map; that is harmless, since the lights are only kept in the BSP for `renderer_sh`. The stock check
allowlists exactly those two lines.

## Build menu

The overlay's `default_build_menu.xml` is the downloaded Quake III menu, unchanged, plus three builds at the end.
Each runs `-meta -keeplights`, then `-vis -saveprt`, then a light stage:

| Build | Light stage |
|---|---|
| Town (draft) | `-light -shbake -shsamples 128` |
| Town (pre-release) | `-light -shbake -shsamples 512 -super 2` |
| Town (final) | `-light -shbake -shsamples 2048 -super 2` |

These are the stock check's compile stages (`tools/stockcheck/`); the check runs the draft's light stage
(`-light -shbake`, whose defaults are 128 samples and `-super 1`), so a draft compile from the editor is the compile
the check gates. `-keeplights` keeps every light entity in the BSP for `renderer_sh`. `-shbake` runs both bakes (see
"The SH bake" below): the lightmap for stock ioquake3 and `maps/<name>/<name>.shsf` for `renderer_sh`. It needs the
bundled q3map2 built with `SH_BAKER=1`; a plain build stops at the light stage and says how to rebuild
(`tools/stockcheck/README.md` gives the steps).

The editor reads `<settings>/Q3.game/build_menu.xml` in preference to the default menu once that file exists (it is
written when the menu is customised). If the Town builds are missing, delete that file or add them by hand.

### Quality presets

The presets differ only in `-shsamples`, the exact number of rays per point, and `-super`, which bakes 2 × 2
super-luxels per luxel and averages them. On the benchmark (12 cores, 5 bounces), with the noise measured against a
4096-sample `-super 2` bake as the root mean square of the lightmap byte error (steps of the stored 0..64 range):

| Build | Points per bake | Compile | Lightmap noise | Side-file L0 noise | Use it for |
|---|---|---|---|---|---|
| Town (draft) | 41939 | 1.6 s (0.7 + 0.8) | 5.0 | 119% | iterating on geometry and lights |
| Town (pre-release) | 157346 | 22 s (10.3 + 11.9) | 0.75 (dark 0.82, bright 0.34) | 22% | screenshots, playtests, comparing with the preview |
| Town (final) | 157346 | 90 s (41.7 + 48.3) | 0.28 (dark 0.31, bright 0.17) | 7% | the shipped pk3 |

Compile is the whole build, with the indirect and full bakes' times in brackets. Dark is a reference byte below 8,
bright 24 or more. The side-file figure is the relative error of each luxel's L0 luminance. The draft's lightmap
figure also holds the difference between `-super 1` and `-super 2` wherever light changes within a luxel, so it
overstates the draft's noise. The final preset's 0.28 is at the floor that rounding to 8 bits sets (1/√12 ≈ 0.29).
Time grows linearly with samples × points: a 4096-sample `-super 2` light stage on the benchmark takes 187 s.

Hard lighting (a sun through a narrow gap, sharp spot edges) needs about 4096 samples to converge smoothly; the
day-time Sponza baked at that into two 2048² pages took about 1.5 hours. Use `-shsamples 4096` by hand when the
final preset still shows blotches. A denoiser or path guiding could close the gap later, at low priority.

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
- `_color` is linear RGB and multiplies the irradiance. The editor divides it by its largest component
  (`plugins/entity/colour.h` `read_colour_normalized`), so only the hue counts: `0.5 0.5 0.5` lights exactly like
  `1 1 1`, and brightness is set with `intensity`. A missing, unparsable or all-zero `_color` is white. The bake does
  the same.
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
2. Engine: compile with a Town build (or the stock check), load the map in `renderer_sh` with
   `+set fs_game benchmark +devmap bench` and the direct-light-only path (no shadow maps, no SH indirect, no SSAO, no
   sky emission), same resolution, and take a screenshot from the same view.
3. Compare in linear light after decoding sRGB. Pass criterion: mean absolute error over all pixels ≤ 2/255 and no
   8×8 block with a mean absolute error above 6/255, ignoring the outermost 8 pixel border and any pixel where the
   editor draws overlays (entity boxes, light shapes).

Differences that are expected and must stay inside the tolerance: mip selection and anisotropy, MSAA of the window
target (the HDR target is single-sampled), and the editor's per-face light culling window versus the engine's.

## The SH bake

`q3map2 -light -shbake` replaces q3map2's lighting with sh-baker, lit by the entities above and by emissive
surfaces. Every other step of `-light` (luxel mapping, `-super`, page layout) is q3map2's own. It runs two bakes over
the same points (super-luxels, one point per light-grid point, draw vertices):

| Bake | Light at the baked point | Writes | Read by |
|---|---|---|---|
| full | direct plus indirect | the BSP lightmap pages, vertex colours and light grid | stock ioquake3 |
| indirect | indirect only, no direct light (punctual or emissive) | the side file `maps/<name>/<name>.shsf`: SH pages, solid hull mesh, SH grid | `renderer_sh` |

`renderer_sh` computes direct light at runtime from the light entities, so the side file holds only what they
bounce. Each bake costs about the same; the default runs both. On the benchmark each takes under a second at the
defaults (41939 points); "Quality presets" above gives the times of the three Town builds.

### Options

| Option | Effect |
|---|---|
| `-shbake` | run both bakes; needs q3map2 built with `SH_BAKER=1` |
| `-shnofull` | skip the full bake: lightmap luxels and vertex colours get the neutral byte 64, the grid neutral ambient 63 63 63 with no directed light |
| `-shnoindirect` | skip the indirect bake: no side file |
| `-shsamples N` | rays per point, exactly (default 128): q3map2 turns off sh-baker's adaptive stop |
| `-shbounces N` | maximum path depth (default 5) |
| `-shdumpl0` | also write each side-file page's L0 as `maps/<name>/<name>_l0_<page>.hdr`, for inspection; never packed |

`-shnofull` and `-shnoindirect` may both be given; then nothing is baked. Every `-shbake` run first deletes an earlier
run's side file and dumps, so none is left next to a BSP whose pages it doesn't match. `-super N`, `-samplesize`,
`-nogrid`, `-novertex`, `-threads` and the other layout options keep their meaning, but `-threads` limits only
q3map2's own stages: sh-baker's bakes use every core. The output doesn't depend on the thread count. `-deluxe`, `-external`, a page
size other than 128 (`-lightmapsize`, `q3map_lightmapSize`), `-trisoup`, `-bounce`, `-approx`, `-fill`, `-nolm` and the
debug colour modes are refused. Options that tune only q3map2's own lighting (`-fast`, `-samples`, `-filter`,
`-patchshadows`, `-gamma`, `-dirty`, ...) are ignored with one warning. `q3map2 -help light` lists them all.

### What differs from the preview

- **Every light casts in the bake**, point lights and `_shadows 0` spots and suns included: sh-baker has no per-light
  shadow switch. The preview and `renderer_sh` keep the caster rule (point lights never cast). The difference shows
  in the stock lightmap's direct light and, slightly, in bounced light. No warning is printed; A9 judges it.
- **The bake is Lambert.** It reads only the base colour (times `qer_pbr_baseColorFactor`), the alpha test and
  emission. Normal, metallic-roughness and occlusion maps don't change baked light, and metal or glossy surfaces bounce
  light as diffuse.
- **Emissive direct light is not in the side file.** The indirect bake drops it with the rest of direct light, and
  `renderer_sh` adds it only once it evaluates `light_area` (task C7). The stock lightmap has it.
- **Surfaces cast by the caster rule above.** Brushes whose every side casts opaque also go in as solid hulls.

### Warnings

The light stage warns about inputs the bake doesn't read: once per kind for the keys, keywords and classnames, once
per image, brush, light or shader for the rest. None fails the compile or the stock check:

- q3map2-only light keys: a light's `spawnflags` or `fade`; worldspawn `_minlight`, `_minvertexlight`,
  `_mingridlight`, `_maxlight` and `_ambient`, and `_ambient` on any other entity;
- the shader keywords `q3map_sun`, `q3map_sunExt`, `q3map_skyLight` and `q3map_surfaceLight`;
- a classname starting with `light` that is none of `light`, `light_spot`, `light_sun` and `light_area`;
- a second `light_sun` (only the first in map order lights the bake);
- a light with zero or negative `intensity` (left out);
- an image that can't be loaded (white in its place);
- a brush that makes no closed hull (skipped; its draw surfaces still occlude);
- per emissive shader, the number of its surfaces with no attached `light_area` or `_proxy` light. The bake lights
  from those surfaces anyway; the preview and `renderer_sh` don't. `light_area` and `_proxy` lights themselves are
  left out of the bake without a warning, since the emitting surface already bakes. The benchmark's neon sign gives
  this warning for its 5 drawn faces;
- the number of points whose full-bake irradiance, or indirect-bake SH or visibility, isn't finite (stored as 0, the
  grid probe invalid), once per bake.

### Encoding

Per channel, a luxel's or vertex's irradiance E from the full bake becomes a byte as a Lambert approximation of the
preview's tonemap (`ShLightmapBytes`, `tools/quake3/q3map2/light_sh_model.h`):

```
d    = E / π
t    = clamp( Uncharted2( d ) / Uncharted2( 11.2 ), 0, 1 )   // the preview's Hable constants and white point
byte = round( 63.75 × sRGB_encode( t ) )
```

opengl1 at default cvars in a window shifts the byte left by 2, so a white Lambert surface shows what the preview
would show at exposure 1, up to 8-bit steps. Only bytes 0 to 64 are used, about 65 levels, so dark gradients band.
The preview's specular and its tonemap of albedo × light (Q3 multiplies the albedo by tonemapped light) are not
reproduced; saturated colours drift in the highlights. The neutral bytes of `-shnofull` are 64 for lightmaps and
vertex colours (a multiplier of 1.0 on opengl1) and ambient 63 63 63, directed 0 for the grid (64 would trigger the
engine's rescale, below). The grid bytes are fitted to the full bake's grid SH as ambient plus one directional light;
the engine rules and constants they are solved against follow.

## How the stock renderers read the baked bytes

`-shbake` writes the lightmap pages, vertex colours and light grid for **opengl1 at default cvars in a window**, the
calibration target (B5 design decisions 5 and 6). These are the engine's rules, read from the fork's source
(`../ioq3-custom/ioq3` at `0135354f`, task 6.3). `tools/quake3/q3map2/light_sh_model.h` holds the same constants and
its tests check them.

### opengl1 (`renderergl1`)

| Rule | Value at defaults | Source |
|---|---|---|
| Overbright bits | `tr.overbrightBits` 0: `r_overBrightBits 1` is forced to 0 without hardware gamma and in any window | `tr_image.cpp:1194-1216` |
| `identityLight` | 1 / (1 << 0) = 1, so `identityLightByte` is 255 | `tr_image.cpp:1218-1219` |
| Load-time shift | `r_mapOverBrightBits 2` − 0 = 2: lightmap, vertex and grid bytes are shifted left by 2, so byte 64 is a multiplier of 1.0 | `tr_bsp.cpp:103-127` |
| Rescale | if any shifted channel exceeds 255, the whole triple is scaled by 255 / max, which shifts the hue; bytes are therefore kept at 63 or below | `tr_bsp.cpp:115-124` |
| Grid load | ambient and directed triples each go through that shift; the grid origin is `gridSize` · ceil(world mins / `gridSize`) | `tr_bsp.cpp:1762-1803` |
| Zero-ambient skip | a cell whose shifted ambient sums to 0 is skipped as one in a wall, and the trilinear weights of the rest are renormalised when they sum below 0.99 | `tr_light.cpp:197-199, 245-249` |
| `r_ambientScale` | 0.6, applied after the trilinear blend | `tr_init.cpp:1005`, `tr_light.cpp:250` |
| `r_directedScale` | 1 | `tr_init.cpp:1006`, `tr_light.cpp:251` |
| Floor | `identityLight` · 32 = 32 is added to every model's ambient after the scale | `tr_light.cpp:332-337` |
| Ambient clamp | each channel at `identityLightByte` (255) | `tr_light.cpp:360-365` |
| Diffuse | a vertex facing away from the light direction gets the ambient; otherwise ambient + N·L · directed, each channel clamped at 255 | `tr_shade_calc.cpp:1062-1106` |

So a model's face realises min(255, 4a · 0.6 + 32) facing away from the grid direction and adds 4d · N·L facing it,
for grid bytes a and d, against 4 × byte for a lightmapped wall. The fit (`ShFitGridPoint`) solves the bytes from
that. What it cannot reach: a model is never darker than 32/255 (the floor), and its ambient never exceeds
4 · 63 · 0.6 + 32 = 183.2/255; directed light makes up the rest only on faces towards the light. Because of the skip,
`-shbake` gives every baked grid point at least ambient 1 1 1, even a dark one with no directed light, so a dark point
still pulls a model towards the floor instead of being replaced by its brighter neighbours; only points left in solid
are 0. On the benchmark that leaves 3 of 1275 points at zero (358 under q3map2's own rule, which raises ambient only
when directed isn't zero).

The grid is coarse (q3map2's default `gridsize` is 64 64 128), and a model takes the trilinear blend of the eight
cells around its origin, so near a light it can be noticeably brighter or darker than the floor under it: the 128-unit
vertical spacing blends floor-level light with light up to 128 units higher. A worldspawn `gridsize` with a smaller
z (for example `64 64 64`) narrows that at the cost of grid size; the side file's SH grid follows the same spacing.

### opengl2 (`renderergl2`) at defaults

opengl2 is not calibrated; A9 measures it. Its differences:

- **Shift by 1.** `tr.overbrightBits` is `r_overBrightBits` (1), clamped only to `r_mapOverBrightBits`, with no window
  or hardware-gamma test (`tr_image.cpp:2912-2926`). The load-time shift is therefore 2 − 1 = 1, `identityLight` is
  0.5 and the model floor is 16.
- **×2 except `rgbGen vertex`.** Stages with `rgbGen identity`, `lightingDiffuse` and `exactVertex` (the implicit
  shader of a vertex-lit surface) are multiplied by 1 << 1 = 2 (`tr_shade.cpp:604-686`), so lightmaps, the grid and
  implicit vertex lighting come out at the same pre-tonemap value as opengl1: 4 × byte for a lightmap, and
  2 · 2a · 0.6 + 2 · 16 = 4a · 0.6 + 32 for a model's ambient. An explicit `rgbGen vertex` stage is not multiplied, so
  it shows at half of opengl1's brightness.
- **Clamps.** The model's ambient and directed triples are each scaled down by their largest channel when it exceeds
  255 in the halved units (`tr_light.cpp:387-418`), not clamped per channel, and N·L is evaluated per pixel. At grid
  bytes of 63 or below the ambient stays under that limit, so there is no 183/255 ceiling before the tonemap.
- **Auto-exposure.** `r_hdr 1`, `r_toneMap 1` and `r_autoExposure 1` (`tr_init.cpp:1201-1212`) tonemap the frame with
  an exposure taken from its own average luminance (`tr_backend.cpp:1575-1577`), so absolute brightness follows the
  view: dark views are lifted and bright ones lowered. Values above 1.0 survive until the tonemap.
