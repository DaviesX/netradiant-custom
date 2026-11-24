# PBR overlay for Quake III Arena

This pack overlays the downloaded Quake III Arena gamepack so that `Q3.game` authors PBR levels: physical light
entities (point, spot, sun and area lights, plus proxies for other emitters), the lighting preview's documentation,
and three build entries that compile the town. Materials are ordinary
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
Q3.game/baseq3/_pbr_lights.ent      definitions of light, light_spot, light_sun and light_area (the only copy)
Q3.game/default_build_menu.xml      the downloaded build menu plus the three "Town" builds
README.md                           this file (not installed)
```

`games/Q3.game` selects the `pbr` entity module: the `quake3` module plus the editor's physical light entities.
`_pbr_lights.ent` sorts before the stock `entities.ent`, and the first definition of a class name wins, so its
`light` replaces the stock q3map2 light; every other stock entity stays available.

Stock ioquake3 prints `light_spot doesn't have a spawn function` (and the same for `light_sun` and `light_area`) once
per entity when it loads such a map; that is harmless, since the lights are only kept in the BSP for `renderer_sh`.
The stock check allowlists exactly those three spawn-function lines (`tools/stockcheck/allowlist.txt`).

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

Both light stages, `-light -shbake` and q3map2's placeholder `-light` without `-shbake`, leave out the attached
emitter lights: every `light_area`, and every `light`, `light_spot` or `light_sun` whose `_proxy` value starts with `1`
(q3map2's reading of a boolean key, so `1` and `1.0` count and `0`, `true` or an empty value don't). The surface they
stand in for already emits in the bake, so baking the light as well would count that light twice. Without such
lights in the map the output is byte-identical to a build that predates them. See "Area lights and emissive proxies"
below.

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
| `light_area` | `origin` (centre), `angles` (0 0 0, roll included), `width` (64), `height` (64), `_color`, `intensity` (1), `radius` (256), `_frustum` (0) | intensity = radiance, uniform over a one-sided rectangle; `_frustum` = shadow half-angle in degrees |

`light` and `light_spot` also take `_proxy` (boolean, default 0): the light stands in for an emissive surface (see
"Area lights and emissive proxies"). The preview renders a proxy exactly as the same light without the key; only
q3map2's light stages read it, to leave the light out. `light_sun` offers no `_proxy`: q3map2 honours the key on a
sun too, so that every compile stage agrees, but the editor ignores it there.

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
(task C4), which `-keeplights` keeps there. sh-baker never sees `light_area` or `_proxy` lights (see the build menu
note above); it lights from the emitting surfaces themselves.

## Area lights and emissive proxies

An emissive surface lights the stock lightmap through the bake, but the preview and `renderer_sh` compute direct
light from light entities only. A flat rectangular emitter (a fire's face, a light panel) gets a `light_area` on its
face; any other emitter gets a `_proxy` point or spot light. Both are derived from the emitter's shader by the
commands below, and the bake ignores both.

### The `light_area` model

- **Shape.** A rectangle centred on `origin`, `width` along the right vector and `height` along the up vector of
  `angles`, emitting along its forward vector. One-sided: nothing behind its plane is lit.
- **Basis.** Quake's `AngleVectors` of `angles` = pitch yaw roll in degrees, positive pitch looking down:

  ```
  forward = (  cp·cy,              cp·sy,             −sp   )
  right   = ( −sr·sp·cy + cr·sy,  −sr·sp·sy − cr·cy,  −sr·cp )
  up      = (  cr·sp·cy + sr·sy,   cr·sp·sy − sr·cy,   cr·cp )
  ```

  with s and c the sine and cosine of p (pitch), y (yaw) and r (roll). right = forward × up, and the corners
  c − hr − hu, c + hr − hu, c + hr + hu, c − hr + hu (hr = right × `width`/2, hu = up × `height`/2) wind so that
  −(hr × hu) is forward. `angles 0 270 0` gives forward −Y, right −X and up +Z (the benchmark fireplace, facing south).
  `angle` (yaw only, −1 up, −2 down) works as for spots; `target` is ignored. Roll turns the rectangle about its normal.
- **Units.** `intensity` is the radiance L, uniform over the rectangle, times `_color` (normalised to its largest
  component, as for the other lights). The flux is Φ = π × L × `width` × `height`. On the normal at a distance d large
  against the rectangle, E ≈ L × A / d²; close to it the exact form factor applies: for the defaults (64 × 64, L = 1)
  at 64 units E = π × 0.2395 = 0.752, not 1. A 16 × 16 light at 200 units gives 0.006386 against L·A/d² = 0.0064.
- **Cutoff.** `radius` is measured from the centre, for culling and shading alike. The effective radius is
  max(`radius`, half-diagonal), half-diagonal = ½√(`width`² + `height`²), so the cutoff sphere always reaches the
  corners; a smaller `radius` is clamped up silently. A value that is not positive (`width`, `height`, `intensity`,
  `radius`) falls back to its default.
- **Culling.** A box lying wholly behind the plane (its support point toward the normal is not in front) is not lit.
  A brush face is lit when any of the four corners lies strictly in front of its plane, so the emitter's own coplanar
  face and the brush's back face stay unlit by their own light, and a face cutting the rectangle is lit. Patches and
  models use the bounds test.
- **Shadows.** Only with `_frustum` > 0 (see "Shadows"); `_shadows` is not read.
- **Gizmo.** The outline and a normal arrow (32 units, or the shorter side if less) are drawn in every view, selected
  or not. When selected, the effective-radius sphere and, with `_frustum` > 0, the shadow frustum's square pyramid out
  to that sphere. Clicking selects through the centre marker only. The entity's bounds include the rectangle, so the
  outline stays drawn while the marker is off-screen.

`RendererLightParams` (`include/irender.h`) carries an area light as type `eArea`: `origin` the centre, `direction`
the normal, `intensity` L, `radius` the effective radius, `right` and `up` the half-extent axes hr and hu (not
normalised), `cosOuter` cos(`_frustum`) for the shadow path only (the half-angle clamped below 90° − 1.15°, as the
shadow path clamps it), and `castsShadows` = `_frustum` > 0.

### Area shading (linearly transformed cosines)

The light pass's area branch (`u_light_type 4` in `gl/pbr_fp.glsl`) follows upstream's `webgl/shaders/ltc/ltc_quad.fs`.
For a fragment at P with normal N, view V, base colour, metallic and perceptual roughness r (the clamped roughness of
the point pass, [0.05, 1]), the outgoing radiance is 0 when dot(P − c, n) ≤ 0 (behind the light) or |P − c| > the
effective radius. Otherwise

```
Lo = L × _color × ( (1 − metallic) × baseColor × FF + S × (F0 × t2.x + (1 − F0) × t2.y) ) × shadow
F0 = mix( 0.04, baseColor, metallic )
```

- **Frame.** T1 = normalize(V − N(N·V)), T2 = N × T1; each corner, relative to P, is taken to (T1, T2, N)
  coordinates (upstream's `LTC_Evaluate`). At exactly normal incidence, where V − N(N·V) vanishes, any tangent is
  used (the lobe is isotropic there; an editor detail).
- **FF**, the clipped normalised form factor: the four corners clipped to the horizon (upstream's
  `ClipQuadToHorizon`, with constant indices), projected onto the unit sphere, and the sum of upstream's
  `IntegrateEdge` over the clipped polygon's edges, clamped at 0. `IntegrateEdge`'s rational fit of θ/sin θ already
  includes 1/(2π), so FF is the cosine-weighted solid angle divided by π: FF ∈ [0, 1], a receiver facing an infinite
  emitter gets 1, and E = π × L × FF.
- **S**, the specular integral: the same, after transforming the corners by M⁻¹ from table 1.
- **t2**, table 2 at the same coordinates: specular is weighted by F0 × magnitude + (1 − F0) × Fresnel.
- **Diffuse has no 1/π and no (1 − F) weight**, because the form factor already divides by π and an area light has no
  single half-vector. This is upstream's convention, so the area pass uses Lambert diffuse where the point, spot and
  sun passes use Disney diffuse weighted by (1 − F).
- **Fallback.** Without the tables (below), the pass shades the diffuse term alone; FF needs no table.

`renderer_sh` (task C7) implements the same formula with the same tables.

### LTC tables

The tables are upstream's GGX fit, unchanged:

| | |
|---|---|
| Repository | `github.com/selfshadow/ltc_code` |
| File | `fit/results/ltc.js` (arrays `g_ltc_1`, `g_ltc_2`), the 2017 revision of the fit: Heitz, Dupuy, Hill and Neubelt (2016), with the magnitude and Fresnel terms of Hill and Heitz (2017) and the sphere-horizon term |
| Commit | `31e5e96b54f98f33098f8503003119ba2231a1c6` |
| Licence | upstream's `LICENSE`: two-clause BSD with a clause asking users to cite the paper |
| Vendored copy | `tools/ltc/` (`ltc.js`, `LICENSE`, the converter `make_ltc_bin.py`, and a README with the layout checked against upstream's source) |
| Binary | `setup/data/tools/gl/ltc_ggx.bin`, installed as `gl/ltc_ggx.bin` |
| SHA-256 | `c7d00f52deff4699aa75012a6383f414c9cc79ae0d3d8b6e61fba19bb42d2dd4` |

Citation the licence asks for: Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt, "Real-Time
Polygonal-Light Shading with Linearly Transformed Cosines", ACM Transactions on Graphics (Proceedings of ACM SIGGRAPH
2016) 35(4), 2016. Project page: https://eheitzresearch.wordpress.com/415-2/

The licence's full notice (copyright, conditions and disclaimer) followed by that citation is
`setup/data/tools/gl/ltc_ggx.LICENSE.txt`, which installs beside the binary as `gl/ltc_ggx.LICENSE.txt`. That file is
the notice shipped with the binary; this README is not installed.

**Format** (all little-endian, 16 + 131072 = 131088 bytes):

- a 16-byte header: magic `LTC1`, then width 64, height 64 and table count 2, each a `uint32`;
- table 1, then table 2, each 64 × 64 RGBA `float32` (each value the `float32` nearest to upstream's printed decimal),
  in upstream's entry order: entry i = a + 64·t holds roughness index a (u, the fast axis) and angle index t (v);
- table 1: M⁻¹ normalised by its middle element, (M⁻¹₀₀, M⁻¹₀₂, M⁻¹₂₀, M⁻¹₂₂) in glm's column-major indexing, so in
  GLSL `M⁻¹ = mat3( vec3( t1.x, 0, t1.y ), vec3( 0, 1, 0 ), vec3( t1.z, 0, t1.w ) )`;
- table 2: (magnitude, Fresnel, 0, sphere-horizon term). The area pass clips to the horizon and does not read the
  sphere term; it stays so the file is upstream's data unchanged.

**Indexing.** u = r, the perceptual roughness (upstream fits entry a at roughness a/63 with alpha = roughness², the
point pass's alpha = r²), and v = √(1 − N·V) with N·V clamped to [0, 1]. Both are mapped as x × 63/64 + 0.5/64, so 0
and 1 land on texel centres.

**Texture.** The editor uploads both tables as one 64 × 128 `GL_RGBA32F` texture on texture unit 7 (table 1 in rows
0-63, table 2 in rows 64-127), linear filtering, clamp to edge, and samples table 1 at (u, v/2) and table 2 at
(u, ½ + v/2). It is created with the PBR programs and bound once per frame in lighting mode, whatever the shadow
preference. It needs `GL_ARB_texture_float` (or OpenGL 3.0) and 8 texture image units; without them, or if
`gl/ltc_ggx.bin` is missing or malformed, area lights shade diffuse only and one warning naming the file is printed.

`renderer_sh` reads the same binary file unchanged. Rebuild it with `python3 tools/ltc/make_ltc_bin.py`, which
reproduces the committed file byte for byte (`--check` verifies without writing).

### Deriving a light from its emitter

An emitter is a shader with an emissive stage: the first additive map stage that isn't the lightmap,
`$whiteimage` or an environment map, whose colour (`rgbGen const`, white without one) isn't black. Its mean
radiance over a surface is

```
L̄ = qer_pbr_emissiveStrength × emissive colour × M
```

where M is the mean linear texel over the surface's UV footprint on the emissive image. The footprint is the face's
texture coordinates (for a clipped region, its UVs through the face's affine texture mapping), fan-triangulated; each
triangle is sampled on a barycentric grid of at least two samples per covered texel along each axis, with
nearest-texel lookup and wrap (`clampMap` is treated as wrap), and the triangles are weighted by area. Each sample is
the sRGB-decoded RGB; for a `blendFunc GL_SRC_ALPHA GL_ONE` stage it is multiplied by the texel's alpha, as the engine
blends it. Patches are averaged over their tessellation's triangles, weighted by surface area. The image is loaded
from disk, so the commands work outside lighting mode. The helpers are `libs/emissivelight.h`, tested by
`make test-emissive-light`.

A value V (radiance or flux) is written as `intensity` = its largest component and `_color` = V / `intensity`, each
with `%g` at six significant digits.

### The commands

Entity menu, shown only when the game's `entities` key is `pbr`. Each is one undo step. A refusal prints one console
line saying why and changes nothing.

- **Create area light from selected face.** Needs exactly one selected brush face that is a rectangle (collinear
  vertices within 0.1 unit removed, then 4 corners within 0.5° of 90°, opposite edges equal within 0.1 unit) and whose
  shader has an emissive stage. forward is the face normal; `width` runs along the edge more nearly perpendicular to
  world Z, or the longer edge on a horizontal face; up = right × forward, and if up points down both right and up are
  flipped; `origin` is the rectangle's centre on the face plane. It writes `origin`, `angles` (solved from forward and
  right, so re-evaluating them gives the basis back within 0.01°), `width`, `height`,
  `radius` = max(256, 8 × ⌈2 × half-diagonal / 8⌉), `intensity` and `_color` from L̄ over the face, and selects the
  new light; `_frustum` stays at its default. Refusals: no face or several faces selected, "face is not a rectangle",
  "shader … has no emissive stage", "emissive image … not found", zero radiance over the face.
- **Create proxy light from selection.** Gathers the selected brush faces, every face of each wholly selected brush
  and the selected patches, and keeps those whose shader has an emissive stage. The flux is

  ```
  Φ = π × Σ k_f × L̄_f × A_f       k_f = 2 when every map stage of the shader is additive, else 1
  ```

  The area-weighted mean normal n̄ = Σ A_f n_f (a patch contributes Σ A_t n_t over its triangles). The origin is the
  area-weighted centroid, moved 8 units along n̄ when |n̄| is at least 0.1 of the total area, else left at the
  centroid. It writes `origin`, `_proxy 1`, `intensity` and `_color` from Φ; `radius` stays at its default. The kind
  is below. Refusals: nothing selected, no selected surface emissive, a missing emissive image, zero flux.
- **Re-derive light from emitter.** Needs exactly one selected light. It rewrites only `intensity` and `_color`.
  - For a `light_area`, the emitters are found in the whole map with q3map2-shbake's attachment test
    (`ShAreaRectAttached`), applied to each brush face: the shader has an emissive stage, the outward normal is within
    1° of the light's forward vector, every vertex lies within 1 unit of the rectangle's plane, and some triangle of
    the face's fan triangulation, projected on the plane and clipped to the rectangle, covers more than 0.001 square
    units (per triangle, not the whole polygon's overlap). Patches are not candidates. The radiance is the
    overlap-weighted mean L = Σ O_f L̄(f ∩ R) / Σ O_f, O_f the area of face f's overlap with the rectangle R and
    L̄(f ∩ R) the face's radiance over that overlap alone. On a light Create made, it gives back what Create wrote.
  - For a `light` or `light_spot` with `_proxy 1`, select its emitting surfaces with it; the flux is recomputed as in
    Create proxy. A `light_sun` is refused whatever its `_proxy` (a flux in a sun's irradiance `intensity` would be the
    wrong unit).
  - Refusals: no light or several selected, a light that is neither an area light nor a point or spot proxy, no
    attached emitter ("no emissive face lies on the area light's rectangle"), a proxy selected without its surfaces,
    a missing emissive image, a zero result.

On the benchmark, the fireplace's fire face (96 × 48, `textures/bench/fire`, strength 64, `rgbGen const ( 1 0.9 0.8 )`,
covering `fire_e` once, whose mean linear texel is (0.287290, 0.101201, 0.026250)) gives `intensity 18.3866`,
`_color 1 0.317035 0.0730977` and `radius 256` (2 × 53.7 rounds to 112, below 256). The map then sets `radius 768` and
`_frustum 70` by hand; Re-derive keeps them, since it rewrites only `intensity` and `_color`.

### Proxy kinds and their residual

- **Hemisphere spot**, when every gathered surface is opaque (k = 1) and |n̄| is at least 0.1 of the total area: a
  `light_spot` with `cone 90`, `cone_inner 89`, `_shadows 0` and `angles` = the pitch and yaw of n̄ (roll 0). The spot
  model clamps both half-angles to 89.9°, so it emits into the hemisphere with I = Φ / (2π(1 − cos 89.9°)) ≈ Φ / 2π.
  `cone_inner 89` keeps the falloff flat to 89° with a 0.9° rim (`cone_inner` equal to `cone` would make it
  `smoothstep(e, e, x)`, which GLSL leaves undefined). `_shadows 0`, because a frustum of nearly 180° gives an
  unusable shadow map. Its gizmo is the cone drawn to the radius sphere, here a disc of the radius through the origin.
- **Point light** otherwise: two-sided billboards (all-additive shaders), and surfaces whose normals cancel.

Both are about **2× dim on the axis**, with the right total flux. A one-sided face of radiance L and area A gives
E = L × A / d² on its normal far away (as the stock lightmap does); the hemisphere spot gives Φ / (2πd²) = L × A / (2d²).
A two-sided billboard (Φ = 2πLA) gives L × A / d² on either side; the point proxy gives 2πLA / (4πd²) = L × A / (2d²).
Off the axis the spot's intensity stays flat where a Lambertian face falls as cos θ, so near grazing the proxy is
brighter than the face. An area light has neither gap, which is why flat rectangular emitters should get a
`light_area`.

### Difference from the bake

- `-shbake` and the placeholder `-light` ignore `light_area` entities and `light`, `light_spot` and `light_sun`
  entities whose `_proxy` starts with `1` (see "Build menu"); the emitting surface bakes instead. Emissive surfaces
  with no such light get one warning per shader (see "Warnings").
- sh-baker's emissive surfaces (its area lights) emit from both sides (`tools/sh-baker/src/light.h`, kept two-sided for
  billboard flames), while `light_area` is one-sided: the back of a baked emitter that isn't against a wall lights the
  stock lightmap and not the preview. A9 judges the difference.
- The side file holds no emissive direct light; `renderer_sh` adds it by evaluating `light_area` (C7) and `_proxy`
  lights (ordinary point and spot lights).

## Sky light

The map's sky shader is an environment light in the same units as the light entities (openspec change
`sky-environment-light`). The editor preview uses it now; the full bake (task B12) and `renderer_sh` (task C6) reuse
the same derivation, which lives in `libs/skylight.h` (header-only, tested by `make test-sky-light`). This section is
complete enough to reimplement it without reading that code.

### Which shader counts

The candidates are the distinct shaders that declare `skyparms` on the map's brush faces and patches, in every entity,
hidden ones included. A shader with `surfaceparm sky` but no `skyparms` is no candidate and gives no sky light: it has
no farbox or cloud layer to define a radiance from. The map's sky is the alphabetically first candidate by shader name,
compared without case. With two or more candidates the console prints one warning naming all of them and the one
used, and prints it again only when the set of candidates changes. A map with no candidate has no sky light.

### Radiance

The sky's radiance in direction ω is

```
L(ω) = s × C × T(ω)
```

- `s` is the shader's `qer_pbr_emissiveStrength`. It must be declared: a sky shader without the keyword, or with a
  strength of 0 or less, emits nothing, so every legacy Quake 3 sky is unchanged.
- `C` is the sky stage's `rgbGen const`, or white without one. The sky stage is the shader's first map stage that is
  not `$lightmap`. Further stages (a second cloud layer, additive or blended) are ignored: two scrolling layers have no
  fixed mean worth modelling.
- `T(ω)` is the sky image in linear RGB: each texel decoded from sRGB with the piecewise curve, alpha ignored.

**Farbox.** When `skyparms` names a farbox, `T` is the six images `<farbox>_rt`, `_bk`, `_lf`, `_ft`, `_up` and
`_dn`, each texel in the direction where the stock renderer draws it (`tr_sky.cpp`'s `MakeSkyVec` and
`kSkyTexOrder`). For the texel in column `i` of `W` (from the left) and row `j` of `H` (from the top), with face
coordinates `s' = 2(i + ½)/W − 1` and `t' = 1 − 2(j + ½)/H`, the direction (normalise it) is:

| Face | Axis | Direction |
|---|---|---|
| `_rt` | +X | `( 1, −s', t')` |
| `_lf` | −X | `(−1, s', t')` |
| `_bk` | +Y | `( s', 1, t')` |
| `_ft` | −Y | `(−s', −1, t')` |
| `_up` | +Z | `(−t', −s', 1)` |
| `_dn` | −Z | `( t', −s', −1)` |

Note that `_bk` is +Y and `_lf` is −X, not the other way round: the stock renderer draws axis `i` with face
`kSkyTexOrder[i] = {rt, lf, bk, ft, up, dn}[i]`. Each texel weighs by the solid angle it subtends exactly: with
`F(u, v) = atan2(u v, √(1 + u² + v²))`, the texel `[u₀, u₁] × [v₀, v₁]` in face coordinates weighs
`F(u₁, v₁) − F(u₀, v₁) − F(u₁, v₀) + F(u₀, v₀)`; the six faces sum to 4π at any resolution, and faces of different
sizes are each used at their own resolution. Cloud stages are ignored when there is a farbox. If any of the six images
fails to load, the console warns once naming the farbox and the map has no sky light.

**Cloud layer.** When `skyparms` names no farbox (`-`), `T(ω)` is the unweighted mean linear texel `M` of the sky
stage's image for every ω above the horizon (z > 0) and black below. The stock renderer never draws a cloud layer's
bottom face. The dome mapping and `tcMod`s are ignored: they don't change the mean of a sky image. If the image fails
to load, the console warns once naming it and the map has no sky light.

### Spherical harmonics and irradiance

The radiance is projected onto the nine real L0–L2 spherical harmonics, in RGB, in Z-up map coordinates, ordered and
scaled as sh-baker's `SHCoeffs`:

| Index | Function |
|---|---|
| 0 | `0.282095` |
| 1, 2, 3 | `0.488603 y`, `0.488603 z`, `0.488603 x` |
| 4, 5 | `1.092548 xy`, `1.092548 yz` |
| 6 | `0.315392 (3z² − 1)` |
| 7, 8 | `1.092548 xz`, `0.546274 (x² − y²)` |

`L_lm = ∫ L(ω) Y_lm(ω) dω`: for a farbox the sum over texels of `L × Y_lm(direction) × solid angle`; for a cloud layer
of radiance `ℒ = s × C × M` above the horizon, analytically `L₀₀ = 0.282095 × 2π ℒ`, `L₁₀ = 0.488603 × π ℒ` and every
other coefficient 0 (`L₂₀` integrates to 0 over the hemisphere).

The sky irradiance at a unit normal `n` is

```
E(n) = Σ Â_l L_lm Y_lm(n),   Â₀ = π, Â₁ = 2π/3, Â₂ = π/4,   each channel clamped below at 0
```

For a cloud layer this is exact: `E(+Z) = πℒ`, `E` at any horizontal normal `πℒ/2`, `E(−Z) = 0`. A uniform sphere of
radiance `L` gives `E = πL` for every normal.

The SH is for irradiance only. Anything that needs the radiance in a direction (the full bake's escaping rays in B12)
uses `L(ω)` itself, never the truncated SH radiance, which for a cloud layer is `ℒ/2 + ¾ℒz`: −ℒ/4 straight down.

### In the editor preview

The base pass of every lit surface adds

```
(1 − metallic) × basecolor × occlusion × E(n) / π
```

with `n` the interpolated vertex normal turned to world space (not the normal map) and metallic read from the
metallic-roughness map's blue channel × `qer_pbr_metallicFactor`, as the light pass reads it. The `1/π` is the light
passes' Lambert normalisation. The term is not shadowed. Emitters, unlit surfaces and the sky surface itself (which
shows its own bytes, "Shading" below) don't receive it and draw as
before. The constant ambient stays as it is and adds on top. The preview recomputes the sky light at the next
lighting-mode draw after a map edit, a shader reload or a preference change, never per frame.

The editor has no sky visibility, so interiors are **overlit**: a sealed room's floor receives the full open-sky light.
The camera preference "Lighting sky" (per game, default on, shown with the other lighting preferences) turns the term
off for interior work; nothing else changes. The engine fixes this with the baked visibility (task C6).

### q3map2

q3map2 does not read the sky light yet; B12 gives it the derivation, for the full bake only. Until then one caveat
holds: `-shbake`'s emissive rule takes a shader's first additive stage as its emitter and doesn't exclude sky
shaders, so a sky with an additive cloud stage and a `qer_pbr_emissiveStrength` counts as an unattached emitter and
gets the warning. A sky without an additive stage, like the town's, is unaffected. B12 excludes sky shaders from that
rule.

### Worked example: the town

`textures/town/sky` is a cloud layer (`skyparms - 512 -`) whose stage is `textures/town/sky` with no `rgbGen const`,
so `C = 1`. Its image is a uniform sRGB (51, 66, 59), linear `M = (0.03310, 0.05448, 0.04374)`. The open-floor
irradiance is `E(+Z) = π s M`. The sky should be half the town sun's `intensity 5`, compared in the largest channel as
`intensity` is the largest channel of a light's colour: `π s × 0.05448 = 2.5`, so `s = 14.6067`. The shader declares
`qer_pbr_emissiveStrength 14.6`, which gives `E(+Z) = (1.518, 2.499, 2.006)`, 0.05% under the target. A horizontal
surface (a facade) gets half that, and a downward-facing one none.

## Fog

The editor's lighting mode renders the map's fog volumes as the stock renderers do, with opengl1 as the reference for
exact colours (openspec change `fog-preview`). `renderer_sh` (task C3a) implements the same model. The pure helpers
are in `libs/fogvolume.h` (header-only, tested by `make test-fog-volume`). This section is complete enough to
reimplement the model without reading that code.

### Which brushes are fog volumes

A fog volume is a brush, in any entity, hidden ones included, whose **content shader** declares
`fogparms ( r g b ) d`. The content shader is chosen as q3map2's `SetBrushContents` (`map.cpp`) chooses it:

1. Start with the first side's shader.
2. Walk the other sides in order. A liquid side (`surfaceparm water`, `slime` or `lava`) replaces a content shader
   that is not liquid. Otherwise a fog side (`surfaceparm fog`) replaces a content shader that is neither liquid nor
   fog.

So a fog face wins over solid and non-solid faces, and a liquid face wins over a fog face: a brush with a water face
whose shader has no `fogparms` is no fog volume, while a water shader that declares `fogparms` makes one. The colour is
`(r, g, b)` with each channel clamped to [0, 1]; the distance `d` is raised to 1 when it is smaller, as `tr_bsp.cpp`
does. A `fogparms` line without three colour numbers and a distance makes no volume and prints one warning. Global
fog (`fog` or `_fog` on worldspawn) is not a fog volume and is not rendered.

**Order.** Volumes are ordered as q3map2 orders the brushes it makes fogs from (`FinishBrush` and
`MoveBrushesToWorld` in `map.cpp`, walked by `CreateMapFogs` in `fog.cpp`). A brush is *translucent* when any side's
shader declares one of `trans`, `trigger`, `playerclip`, `monsterclip`, `alphashadow`, `lightfilter`, `nodrop`,
`botclip`, `clusterportal`, `donotenter`, `areaportal`, `origin`, `water`, `slime` or `lava` (q3map2's
`C_TRANSLUCENT`), and *opaque* otherwise. The order is:

1. the opaque brushes of `func_group` entities, later groups first, each group's in map order;
2. worldspawn's opaque brushes, in reverse map order;
3. worldspawn's translucent brushes, in map order;
4. the translucent brushes of `func_group` entities, in group order, each group's in map order;
5. every other brush entity in entity order: its opaque brushes in reverse map order, then its translucent ones in map
   order.

**Membership.** A point belongs to a volume when it lies at most `ε = 1 + depth / 512` map units outside every plane
of the brush, where `depth` is the point's distance along the view axis. The tolerance absorbs the error of
reconstructing positions from a 24-bit depth buffer, so a floor lying in the plane of a fog face (the benchmark's
street) is fogged evenly. When a point lies in several volumes, the **last** in the order above wins, as q3map2's
later fogs overwrite earlier ones.

**Limits.** A brush with more than 32 planes is no volume (one warning naming it and its plane count, repeated only
after it has dropped back under the limit). At most 8 volumes are honoured, the first 8 in the order; with more, one
warning names the number found, repeated only when the number changes.

### The fog surface

A volume's fog surface is one of its sides. Its plane bounds the fogged part of the view ray when the camera is
outside the volume.

- When the content shader declares a non-zero `q3map_fogDir ( x y z )` and some side's outward normal has a negative
  dot product with it, the surface is the first side, in the order −X, +X, −Y, +Y, −Z, +Z and then the other sides,
  with the most negative dot product (q3map2 `fog.cpp`, which walks its sides in that order after its axial swap).
- Otherwise it is the first side, in the order +Z, −Z, +Y, −Y, +X, −X and then the other sides, whose shader is not
  `surfaceparm nodraw` (q3map2 `writebsp.cpp`).
- With no such side there is no fog surface.

Differences from q3map2: q3map2 also skips sides with no visible part after compiling (`visibleHull`), which needs a
compiled BSP, and orders non-axial sides as its axial swap leaves them, where the editor uses map order. Where q3map2
would skip the editor's side as hidden, or orders non-axial sides differently, the two differ, and it shows only with
the camera outside the volume. The town's volume has every side hidden inside the hull, so it has no fog surface in
stock and a +Z surface in the editor; the camera never leaves it in the street.

### Fog amount

For a point `p` in a volume of distance `d`, seen from the eye `e` looking along the unit forward vector `v`:

```
depth = (p − e) · v
f     = sqrt( clamp( depth × fraction / d, 0, 1 ) )      // f = 0 when depth ≤ 0
```

Let `t(x)` be the distance of a point `x` below the fog surface's plane, positive on the volume's side
(`t(x) = dist − n · x` for the surface side's outward plane `n · x = dist`).

- No fog surface: `fraction = 1`, wherever the eye is.
- A fog surface with the eye inside, `t(e) ≥ 0`: `fraction = 1` when `t(p) ≥ −ε` (the membership tolerance), else 0.
- A fog surface with the eye outside, `t(e) < 0`: `fraction = 0` when `t(p) < 1`, else `t(p) / (t(p) − t(e))`, the
  share of the view ray below the plane.

This is the model of `RB_CalcFogTexCoords` (`renderergl1/tr_shade_calc.cpp`: s = depth / (8 d) + 1/512, t from the
surface plane) and `R_FogFactor` (`renderergl1/tr_image.cpp`: `sqrt(min(8 (s − 1/512) × fraction, 1))` through a
256-entry table), and of rend2's `CalcFog` (`renderergl2/glsl/generic_vp.glsl`), in rend2's analytic form evaluated
per pixel.

### Composite: in display space, after the tonemap

The fog is applied to the finished image, after the tonemap curve and the sRGB encode, as opengl1 blends its fog pass
(`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` of `fogparms` × `identityLight`, which is 1 at default cvars) onto 8-bit
pixels:

```
out = (1 − f) × pixel + f × C,      C = 255 × colour       (8-bit display values)
```

Each pixel uses the nearest surface that writes depth. Exposure and the tonemap never touch the fog colour, so the
`fogparms` colour the author picks is exactly the colour on screen, and "fog colour equals sky colour" holds in the
preview. Not fogged: pixels where nothing writing depth was drawn, and pixels whose nearest depth-writing surface is a
sky (a shader declaring `skyparms`), including where glass or a glow was drawn over the sky. Fogging in linear light
before the tonemap was rejected: the curve would bend the fog colour away from what the author set. opengl2 at
default cvars fogs inside its HDR framebuffer and then applies its own auto-exposure tonemap (the town's sky reads
60 80 71 there against 51 66 59 on opengl1), so across the stock renderers it is the seam that carries over, not the
exact colours.

The composite is exact for opaque surfaces and for additive stages over an opaque base: opengl1's fog pass over the
composite gives `(base + add)(1 − f) + f C`.

**Preference.** The camera preference "Lighting fog" (per game, default on, shown with the other lighting preferences)
turns the fog off and changes nothing else: fog faces stay undrawn and the sky keeps its own colour. With it off, the
lighting view of a map whose fog brushes carry the fog shader on every face (the town, the benchmark) equals the same
view with those brushes deleted. The preview rebuilds the volumes at the next lighting-mode draw after a map edit, a
shader reload or a preference change, never per frame. Without the off-screen target (the fallback above) there is no
fog.

### Known differences from stock

- **Assignment.** The stock renderers fog per draw surface, as q3map2 assigns it: brush faces are chopped at the fog
  brush; patches are cut only at its ±Z planes; world triangle and meta surfaces (models) are fogged whole by the last
  volume they touch; brush-model surfaces are fogged whole by the volume with the largest bounding-box overlap;
  surfaces flush with a fog side and facing out of the volume are not fogged; `q3map_noFog` surfaces never are. The
  editor fogs per pixel.
- **Translucent and all-additive surfaces** take the fog of whatever lies behind them; stock fogs them by their own
  vertices.
- **The sky.** Stock fogs a cloud layer that lies in a volume at its cloud box's distance; the editor never fogs the
  sky. That is invisible when the fog colour equals the sky colour.
- **Fog faces.** Stock draws a volume's visible faces as fog-only surfaces, which fog what lies beyond them as seen
  from outside. The editor draws no fog faces in lighting mode.
- **Interpolation.** The stock renderers evaluate s and the eye-outside t per vertex and interpolate them across each
  triangle; that difference depends on the geometry and is not measured. opengl1 also reads a 256 × 32 fog image with
  linear filtering, clamped at its edge, which caps `depth / d` at 8 before the eye-outside fraction applies and
  filters the fraction over 32 rows. Measured at single points inside volumes (`make test-fog-volume`), the editor's
  f is within **0.047** of opengl1's wherever f ≥ 0.1 with no fog surface or with the eye inside (largest near
  f = 0.1, where the image's first texels linearise the square root); with the eye outside a surfaced volume the two
  differ by up to **0.32**. rend2's analytic `CalcFog` agrees within 1e-6.
- **Malformed `fogparms`.** q3map2 and stock still make a volume from it; the editor makes none and warns.

### Worked example: the town

`textures/town/fog` declares `fogparms ( 0.200392 0.258824 0.231373 ) 2048` and `surfaceparm fog`, `trans`,
`nonsolid`. Its one brush covers the street level from z = −8 to 264 (x and y ±1032); every face carries the fog
shader, so it is the content shader, and the brush is translucent through `trans`. It is the map's only volume. The
fog colour is `C = 255 × (0.200392, 0.258824, 0.231373) = (51.1, 66.0, 59.0)`, the sky image's 51 66 59.

From the `street` view (eye −783 0 50, looking along +X), the eye is inside the volume, so `fraction = 1`. The far
facade at x = 1024 lies at `depth = 1807`, so `f = sqrt(1807 / 2048) = 0.9393`: a facade pixel of tonemapped value P
becomes `0.0607 P + 0.9393 C`, which for any P in 0..255 lies within 16 of C, and within 8 for P within 130 of C. The
sealed rooms lie below z = −8, more than ε outside the bottom face, so they are never fogged.

## Shading

Metallic-roughness BRDF ported from `sh-renderer/glsl/radiance.frag` (`ComputeDirectBRDF`): GGX normal
distribution with roughness clamped to ≥ 0.05, Smith geometry with the Schlick-GGX approximation
(`k = (roughness + 1)² / 8`), Schlick Fresnel with `F0 = mix(0.04, basecolor, metallic)`, specular denominator
`4 N·V N·L + 0.001`, Disney (Burley) diffuse weighted by `kD = (1 − F)(1 − metallic)`.

One additive pass per light plus a base pass writing
`emissive × emissive colour × qer_pbr_emissiveStrength + ambient × basecolor × occlusion` plus the sky term of "Sky
light" above; ambient and sky add together. The emissive map is the
first additive stage's image and its colour is that stage's `rgbGen const` (white without one); the base colour is
the stage the lightmap multiplies. Sources: `gl/pbr_vp.glsl`, `gl/pbr_fp.glsl`, `gl/pbr_base_vp.glsl`,
`gl/pbr_base_fp.glsl`.

The preview lights exactly the lightmapped, non-blended shaders that are not sky, fog or `nolightmap`, whether or not
they declare `qer_pbr_*`; a shader without PBR keywords renders as a fully rough dielectric. Area lights are shaded as
in "Area shading" above, with Lambert diffuse instead of Disney.

Emitters the preview does not light are drawn in the HDR framebuffer with their emissive in linear light, the
sRGB-decoded texel × `rgbGen const` × `qer_pbr_emissiveStrength` as the base pass computes it, and get no light
passes. This covers shaders with an emissive stage that are not sky or fog and either have only additive map stages
or declare `surfaceparm nolightmap`:

- **All-additive** (a flame, a glow sprite): one pass with ambient 0, leaving only the emissive, added to the frame
  with the stage's source factor (`GL_ONE`, or `GL_SRC_ALPHA` for a `GL_SRC_ALPHA GL_ONE` stage) and destination
  `GL_ONE`, depth-tested without depth write, after the opaque surfaces, with the shader's cull.
- **Opaque `nolightmap`** (no `qer_trans`, no blended base stage): drawn opaque as sRGB-decoded base colour × base
  colour factor + emissive (the stock fullbright result in linear light), honouring the alpha test.
- A blended shader that is not all-additive keeps its textured-mode state.

Two rules come before all of these (openspec change `fog-preview`):

- **Sky.** A shader declaring `skyparms` is drawn unlit, without the camera headlight, in its usual place (sort
  `eSortFullbright`), so glass and glows still blend over it. Its pixels show the sky image's own bytes at any
  exposure: the 8-bit texel × the sky stage's `rgbGen const` (white for a farbox), sRGB-decoded, multiplied by the
  tonemap's `Uncharted2( 11.2 )`, put through the inverse of the Hable curve and divided by exposure, so that the
  resolve maps it back exactly. A cloud layer samples its sky stage's image (the first map stage that is not
  `$lightmap`) at the face's texture coordinates; a farbox samples its cubemap along the view direction. The sky also
  writes its own fragment depth to a second attachment, which the resolve uses to leave sky pixels unfogged.
  `surfaceparm sky` without `skyparms` is no sky and draws like any other unlit shader. Sources:
  `gl/sky_lit_vp.glsl`, `gl/sky_lit_fp.glsl`.
- **Fog faces.** A face whose shader declares `fogparms` is not drawn in lighting mode; its selection highlight still
  is. The fog itself comes from the resolve ("Fog" above).

Every other shader draws with its textured-mode state, unlit.

The scene is rendered into an off-screen target (an RGBA16F colour attachment, an R32F sky-depth attachment and a
24-bit depth texture) and resolved to the window with

```
curr = Uncharted2( hdr × exposure )      // Hable: A=0.15 B=0.50 C=0.10 D=0.20 E=0.02 F=0.30
ldr  = curr / Uncharted2( 11.2 )         // white point W = 11.2
out  = sRGB_encode( ldr )
out  = (1 − f) × out + f × fogparms colour   // "Fog" above; skipped for sky pixels and without fog volumes
```

The resolve also writes the scene's depth into the window target, so overlays drawn afterwards (selection, grid,
text) are occluded at any window multisampling.

`gl/tonemap_fp.glsl`, ported from `sh-renderer/glsl/tonemap.frag` (the engine encodes sRGB through
`GL_FRAMEBUFFER_SRGB`, same transfer function; its exposure is fixed at 1.0). Exposure and ambient are camera
preferences ("Lighting exposure", default 1.0; "Lighting ambient", default 0.02; "Lighting sky", default on;
"Lighting fog", default on). If the off-screen target cannot be created (framebuffer objects, any of its three
attachment formats or two draw buffers unavailable, or the framebuffer incomplete), lighting mode renders straight to
the window without tonemapping and fog, the sky draws at its own colour (texel × `rgbGen const`), and the console
warns once.

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

Lighting mode renders shadow maps for `light_sun`, `light_spot` and `light_area`. Point lights never cast: `light`
ignores `_shadows` and its contribution is never attenuated. Spot and sun lights read `_shadows` (boolean, default 1);
setting it to 0 makes that light shine through occluders and frees its region of the atlas. An area light ignores
`_shadows` and casts only when `_frustum` (half-angle in degrees, default 0) is greater than 0. The camera preference
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
| Area frustum | perspective from the rectangle's centre along its normal, square, aligned with its right and up axes (roll included, as the gizmo's pyramid); fov = 2 × `_frustum`, each half clamped below 90° − 1.15° (π/2 − 0.02); near 4 map units, which keeps the emitter's own face out; far = the effective radius max(`radius`, half-diagonal) | editor | |
| Area shadow sampling | as a spot: one atlas tile, penumbra factor 1, N·L taken toward the rectangle's centre, except the bias: none in normalised depth; instead the receiver moves t·(1 − N·L) along its normal and t·clamp(slope, 1, 3) toward the light, where t = 2·d·tan(half-angle)/512 is one tile texel at its distance d and slope = tan of the angle between N and L; receivers beside the frustum are shadowed (spots treat them as lit), so a casting area light lights only within `_frustum`, fading out over the last 10° before it on each axis of the square frustum (1 − smoothstep(`_frustum` − 10°, `_frustum`, off-axis angle), the two axes multiplied); choose it to cover the region the light should reach | editor | |
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
lighting mode renders unshadowed and warns once in the console. Shadow-casting area lights share the spot atlas
with the spots. More than 16 shadow-casting spot and area lights is not an error: the 16 nearest the camera get
tiles, the rest render unshadowed, and a warning ("spot and area lights") is printed once. The LTC tables take texture
unit 7, above the five material textures and the two atlases.

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
     -Q3.game-fieldOfView 90 -Q3.game-LightingShadows false -Q3.game-LightingAmbient 0 -Q3.game-LightingFog false \
     <enginepath>/benchmark/maps/bench.map
   ```

   (`CameraRenderMode 4` is lighting mode.) The entity boxes are drawn in the capture; disable them with the entity
   filters, or keep the camera outside them. "Lighting fog" is off because the documented view lies inside the
   benchmark's fog box and `renderer_sh`'s direct-light path has no fog.
2. Engine: compile with a Town build (or the stock check), load the map in `renderer_sh` with
   `+set fs_game benchmark +devmap bench` and the direct-light-only path (no shadow maps, no SH indirect, no SSAO, no
   sky emission), same resolution, and take a screenshot from the same view.
3. Compare in linear light after decoding sRGB. Pass criterion: mean absolute error over all pixels ≤ 2/255 and no
   8×8 block with a mean absolute error above 6/255, ignoring the outermost 8 pixel border, any pixel where the
   editor draws overlays (entity boxes, light shapes), and the sky's pixels: the editor's sky shows its own bytes,
   and `renderer_sh` has no matching sky until task C3a.

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
  this warning for its 5 drawn faces (the front, both ends, the top and the bottom), which have no light; the
  fireplace's fire face has its `light_area` and draws none;
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
