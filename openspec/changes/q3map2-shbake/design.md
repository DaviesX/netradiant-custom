# Design: `-shbake` in q3map2 (B5)

## Context

The motivation is in proposal.md. The relevant state:

- **sh-baker at `ce5419a`:**
  - **The bake call.** `BakeSHLightMap(scene, points, raster, config)` (`src/baker.h`) bakes N points laid out N×1, in one call with one BVH. Per point it returns an `SHCoeffs` (nine RGB coefficients, the hemisphere projection ∫ L_i Y dω) and an environment visibility. Points with `material_id < 0` keep the marker −1. Any id ≥ 0 bakes; the id is used for nothing else.
  - **Scene input.** `AddQ3Map2Surface` (`src/loader_q3map2.h`) appends one checked draw surface. A surface with `material_id < 0` is a pure occluder. Additive materials (`Material::additive`, meaning every stage blends `GL_ONE` on the destination) are left out of the BVH and act as emitters only.
  - **Indirect-only.** `BakeConfig::indirect_only` drops the punctual lights' direct term at the baked point only, keeping area lights and every bounce's direct term (`baker.cpp:225-233`).
  - **Bounces.** `bounces = 0` still adds next-event estimation at the first hit, so it is one bounce of indirect light, not none.
  - **Light tree.** It drops any bounded light whose `flux` is 0. The glTF loader sets `flux = Flux(light)` (`loader.cpp:797`), and q3map2 must do the same.
  - **Light-model gaps.**
    - `Light` has no range.
    - The spot falloff is a linear clamp, which divides by zero when the cones are equal.
    - Every light is shadowed.
  - **Ray offsets.** Shadow rays and the sensor's first ray use `tnear` 0.005 (`light.h`, `sensor.cpp:85`). `Trace()` resets every traced primary and bounce ray to 0.001 (`tracer.cpp:16`). Transmission continuations use 0.005 (`occlusion.cpp:138`).
  - **Shading.** The bake's BRDF is Lambert: it uses only albedo, alpha and emission. `normal_texture` and the metallic-roughness texture are never read on the bake path. Alpha is fractional: transmittance is 1 − alpha.
  - **Absolute thresholds.** The emission CDF cut-offs (`scene.cpp:267`, `:424`), `joint_pdf > 1e-8`, and the 1e-12 area and Jacobian guards in `light.cpp` are absolute. The sensor's `tfar` is 1e4.
- **q3map2's `-light`** (`light.cpp`, `LightWorld`):
  - **Order.** `SetupGrid`, then `CreateEntityLights` and `CreateSurfaceLights`, `TraceGrid`, `MapRawLightmap`, dirt and floodlight, `IlluminateRawLightmap`, `StitchSurfaceLightmaps` (disabled: it returns at once, `lightmaps_ydnar.cpp:1170`), `IlluminateVertexes`, the bounce loop, and finally `StoreSurfaceLightmaps`.
  - **Neighbour flood.** `IlluminateRawLightmap` ends with a 3×3 neighbour flood of occluded and unmapped super-luxels (`light_ydnar.cpp:2693-2781`). This is the only place `CLUSTER_FLOODED` is set.
  - **Storage steps in `StoreSurfaceLightmaps`** (`lightmaps_ydnar.cpp`):
    - the super-sample average (2347-2433);
    - a BSP-level neighbour fill for luxels with no sample, which averages colour but zeroes the deluxel (2472-2510);
    - wrap;
    - collapse into twins, skipped under `-nocollapse` or `-deluxe`;
    - solid stamping, skipped under `-nocollapse`;
    - vertex approximation, only with `-approx`;
    - placement, through `ColorToBytes`;
    - `FillOutLightmap` (2906), only with `-fill`.
  - **Deluxemap precedent.** `-deluxe` carries a parallel per-luxel buffer through most of these steps, but not through the neighbour fill.
  - **Byte conversion.** All 8-bit lighting output goes through `ColorToBytes` (`light_ydnar.cpp:62`). Worldspawn `_minlight`, `_minvertexlight` and `_ambient` apply on q3map2's 0–255 scale.
  - **Grid.** `TraceGrid` nudges in-solid grid points with `Random()` and forces ambient to 1 when ambient is 0 but directed is not, because Q3 skips grid cells whose ambient is 0.
- **Brush lump.** Bevel sides are written with the `noshader` shader (`writebsp.cpp:382`). Brushes of a model with an origin are stored in model space; `SetEntityOrigins` moves only draw vertices.
- **The editor:**
  - **Tonemap.** The lighting preview tonemaps with Uncharted2 (Hable constants, white point 11.2) and then sRGB-encodes (`setup/data/tools/gl/tonemap_fp.glsl`).
  - **Light keys.** Defaults, clamps, `angle`, and `_color` normalised by its largest component are in `plugins/entity/light.cpp` and `colour.h`.
  - **Rules.** The base-colour and emissive rules are in `plugins/shaders/shaders.cpp` (`deriveQuake3`), and the caster rule in `radiant/renderstate.cpp` (`Shadow_materialCasts`).
- **The stock renderers.**
  - **opengl1** in a window shifts lightmap, vertex and grid bytes left by 2 (`r_mapOverBrightBits 2`, no hardware gamma), so byte 64 is a multiplier of 1.0. Model lighting is grid ambient × `r_ambientScale` (0.6) plus `identityLight` · 32, and directed × `r_directedScale`.
  - **opengl2** at defaults shifts by 1, multiplies most stages by 2 but `rgbGen vertex` by 1, and tonemaps with auto-exposure.

## Goals / Non-Goals

**Goals:**
- One bake per compile that fills every lighting output from the same data.
- SH that maps to the BSP's own lightmap pages with no extra mapping.
- A stock lightmap that looks like the editor's tonemapped preview on opengl1.
- Pure logic tested without sh-baker, in the style of `test-q3map2-extension`.
- No change to q3map2's output without `-shbake`.

**Non-Goals:**
- **Light styles.** Only style 0 is baked.
- **Non-default page sizes, external lightmaps and deluxe pages.** These are refused (spec).
- **Matching opengl2 exactly.** Its differences are recorded for A9.
- **PBR shading in the bake.** The bake is Lambert, so normal maps, metallic-roughness and occlusion don't affect baked light. This is a recorded fidelity gap.
- **Moving brush models.** Every model casts as placed, as in the editor. A door bakes its closed position.
- **An environment or sky emitter.** Rays that escape add nothing. Visibility is still recorded.
- **Cancelling from the editor's build monitor.**

## Decisions

### 1. One sh-baker PR for the light model and the split result

The PR is on a branch of `github.com/DaviesX/sh-baker` and is merged by the user before the q3map2 groups that need it.

- **`Light` fields:**
  - `float range = 0`: the test sits in the punctual incoming-radiance functions, so it covers both the baked point and the bounces.
  - `bool casts_shadows = true`: false skips `FindTransmittance`.
  - `ConeFalloff cone_falloff = kLinear`: smoothstep treats cos inner ≤ cos outer as a hard edge. Linear mode keeps today's formula except where cos inner = cos outer, today's only division by zero, which becomes the same hard edge. Inverted cones in linear mode keep today's finite output.
- **Light tree.** `LightBounds` gains `max_range`, where 0 means unbounded. `Union` gives 0 if either side is 0, else the larger range. `Importance` returns 0 when the shaded point is farther than `max_range` from the node's bounds. Emitter bounds are unchanged.
- **Choosing between the BVH and the directional set.** Today `LightTree::Sample` gives directional lights a fixed share, n_dir/(n_dir + 1), and returns a single-light root leaf without checking its importance (`light_tree.cpp:424-453`). `Sample` and `Pdf` now evaluate the BVH root's importance at the shaded point first. When it is 0, the whole choice goes to the directional set, or nothing is sampled if there is none. Otherwise the fixed share stays. `Sample` and `Pdf` apply the same per-point rule, so the estimator stays unbiased.
- **The split option.** `BakeConfig::split_punctual_direct` adds two results: `BakeResult::punctual_direct_sh_texture` and `punctual_direct_irradiance` (RGB).
  - With the option set, the baked point's next-event estimation uses the area-light tree, as `indirect_only` does.
  - After the sample loop, one pass loops over every punctual light. Each light gets range, falloff and visibility once (binary in practice, since q3map2 rewrites alpha to 0 or 1). The pass adds E⊥ · Y(ω) to the SH result and E⊥ · max(0, n · ω) to the irradiance.
  - Noise-free and exact, it costs about one shadow ray per light per point.
- **Defaults.** With every new field at its default, results are bit-identical wherever they were finite.

*Alternatives:*
- *Two bakes:* this doubles the bake time and gives the two results different noise.
- *Lightmap irradiance from the SH alone:* the order-2 SH of a delta light rings: +6.25% at normal incidence, wrong at grazing angles, and a 0.0625·E floor on the back. That is visible on the dominant direct term.

### 2. Bake in a scaled space, write in map units

q3map2 hands sh-baker positions multiplied by s = 1/32, an exact power of two.
- Point and spot radiant intensities are multiplied by s², and ranges by s.
- Sun irradiance and emissive radiance are unchanged.

For punctual lights and the sun, the resulting radiance and irradiance are the same as an unscaled bake would give. Every q3map2-side value stays in map units.

The reason is the ray offsets. Scaled, `tnear` 0.005 is 0.16 map units and 0.001 is 0.032 map units. Unscaled they would be 0.005 and 0.001 units, a few float ulps at ±4096.

The scaling is not fully invisible, because sh-baker's thresholds are absolute:
- the emission-CDF cut-offs see areas 1024× smaller, which is why textured emitters avoid the CDF (decision 7);
- the 1e-8 and 1e-12 guards see smaller values;
- the sensor's `tfar` of 1e4 becomes 320000 map units, which only lengthens rays that escape.

These are recorded in the plan.

*Alternative: a configurable epsilon in sh-baker.* This touches every offset site. The scaling gives the same benefit with no library change.

### 3. Where the stage sits in `LightWorld`

With `shBake` set, `LightWorld` runs:
1. `SmoothNormals`, if `-shade` is given.
2. `SetupGrid`.
3. Skips the light creation and `TraceGrid`.
4. `MapRawLightmap`.
5. Skips dirt and floodlight.
6. `ShBakeWorld()`.
7. Skips `IlluminateVertexes` and the bounce loop.
8. `StoreSurfaceLightmaps`, then writes the side file.

`ShBakeWorld` builds the scene, bakes once, and writes:
- the super-luxel colour buffers, as linear irradiance (decision 5), with count 1;
- the super-SH buffers;
- the vertex luxels;
- the grid bytes.

It then runs the neighbour flood of `light_ydnar.cpp:2693-2781`, minus `splotchFix`, dark and ambient handling, on colour, SH and visibility together, setting `CLUSTER_FLOODED`.

Under `-shbake`, `minLight`, `minVertexLight`, `minGridLight` and every `ambientColor` are zero, and `_maxlight` has no effect, since it only acts inside the replaced `ColorToBytes` (spec: the inputs are warned).

Option checks run in `LightMain` right after parsing (decision 14).

Without `SH_BAKER=1`, a stub `light_sh_stub.cpp` is linked and `LightMain` stops before `ShBakeWorld`.

### 4. SH through `StoreSurfaceLightmaps`

`rawLightmap_t` gains super and BSP SH buffers: 27 floats plus 1 for visibility per luxel, style 0 only, allocated only under `-shbake`. `outLightmap_t` gains a page SH buffer.

SH follows the colour, with the colour's weights, in:
- the super-sample average, using the same lit-or-occluded choice;
- the BSP-level neighbour fill, where SH averages the same neighbours as the colour. This is not the deluxemap pattern, which zeroes the deluxel there;
- wrap;
- placement on the page.

Under `-shbake`:
- collapse is skipped, as under `-deluxe`;
- solid stamping is off, as under `-nocollapse`;
- `-approx` and `-fill` are refused;
- stitch is already a no-op.

So no step depends on colour equality. With internal game-size pages, `olm->lightmapNum` is the BSP page index, which is why every way of changing the page size is refused.

Before writing, any luxel still at −1 is set to 0.

### 5. Irradiance and encoding

- **Irradiance.** For a super-luxel or vertex with normal n: E = punctual irradiance (exact, from decision 1) + Σ Â_l L_lm Y_lm(n) over the main SH, with Â₀ = π, Â₁ = 2π/3 and Â₂ = π/4. The main SH holds indirect and emissive light, which is smooth enough for order 2. `-super` averages E linearly.
- **Encoding.** `ShLightmapBytes(E)` (in `light_sh_model`) computes, per channel:
  - d = E/π;
  - t = clamp(U(d)/U(11.2), 0, 1), where U is Hable's curve with the preview's constants;
  - byte = round(63.75 · sRGB(t)).

  This is a Lambert approximation of the preview. A white Lambert surface at exposure 1 would show sRGB(U(d)/U(11.2)), and opengl1 multiplies a texel by 4·byte/255, so for white albedo the lightmap reproduces that approximation up to 8-bit steps. The preview itself shades with (1 − F)·Disney diffuse plus GGX specular with F0 = 0.04 (`pbr_fp.glsl:239-252`), so it differs with view angle and roughness: a recorded fidelity gap. For coloured albedo the match is weaker still, since the preview tonemaps the product. Hable's curve is the roll-off, and it reaches 1 at the white point.
- **Calibration.** It is calibrated to opengl1 at default cvars in a window (the user's choice). The opengl2 differences (shift by 1, ×2 except `rgbGen vertex`, auto-exposure) go into the gamepack README for A9.
- **Plumbing.** Under `-shbake`, `ColorToBytes` returns `ShLightmapBytes(color)` and ignores its scale arguments.

### 6. Grid fit

Each grid point is baked twice, with normals +Z and −Z. The main SH, the punctual SH and the punctual irradiance are each summed over the pair.
- **Direction.** d is the normalized luminance-weighted L1 vector (L11, L1m1, L10) of the punctual SH. Without punctual light it comes from the main SH, and failing both it is +Z.
- **Directed.** D = (L1 · d)/0.488603 per channel. For one light this is exactly its E⊥.
- **Ambient.** A = π · 0.282095 · L0 of the main SH, which is its irradiance averaged over all normals. It has no ringing from direct light.
- **Bytes.** In opengl1 a model's realised ambient is min(255, shift(a)·0.6 + 32·identityLight), and a face towards d adds shift(dir)·1 (`tr_light.cpp:333-358`, `tr_shade_calc.cpp:1083-1104`). target(·) is the post-shift value, 4 × the lightmap byte. The bytes are solved per channel:
  - ambient: post-shift value = clamp((target(enc(A)) − 32)/0.6, 0, 255), divided by 4 into a pre-shift byte;
  - realised = min(255, ambient_post · 0.6 + 32);
  - directed: post-shift value = clamp(target(enc(A + D)) − realised, 0, 255), divided by 4.

  Each pre-shift byte is clamped to 63. Any byte above 63 would trigger `R_ColorShiftLightingBytes`' rescale of the whole triple (`tr_bsp.cpp:103-127`) and shift the hue. Ambient is set to 1 when it is 0 and directed isn't. Exceptions: faces darker than the engine's floor of 32 get the floor, and ambient above about 185/255 post-shift, enc(A) ≈ 46, can't be reached.
- **Confirming the model.** Task 6.3 confirms this model and its constants from the fork's `renderergl1` source (`tr_light.c` or its port) and records opengl2's version.
- **Grid positions.** These are computed single-threaded before the bake, using `TraceGrid`'s nudge with a fixed, index-seeded sequence instead of `Random()`, so the output doesn't depend on threads. Points that stay in solid are written black.

### 7. Scene assembly

**Surfaces.** For every BSP draw surface of every model, except two kinds:
- `_skybox` copies (`parentSurfaceNum >= 0`): they sit in the portal sky's transformed space, and the bake has no skybox trace node to hold them;
- `MST_FOLIAGE` instances, which q3map2's tracer also skips.

For the rest:
- The geometry comes from `yDrawVerts`.
- Planar, meta and triangle-soup surfaces use the BSP indices. The benchmark's `misc_model` foliage is triangle soup.
- Patches use the tessellation q3map2's tracer uses for patch shadows.
- A surface is added if its shader casts (decision 8), or if it emits.
- Its material is that shader's bake material.

**Hulls.** For every BSP brush of every model:
- The brush qualifies if its content shader is solid and every non-bevel side's shader casts and isn't alpha-tested. A side is a bevel if its shader is `noshader` and its un-shrunk winding is degenerate (area < 0.1 units²).
- The planes are translated by the origin of the model's entity.
- The un-shrunk hull chops `BaseWindingForPlane` by every other plane. It is triangulated as fans, in q3map2's winding, for the side file.
- The bake hull moves every non-bevel plane inward by 0.25 map units. That is above both scaled ray offsets (0.16 and 0.032 units).
- It is added with `material_id −1`. A brush whose hull build fails is skipped with a warning naming it.

**Materials.** There is one bake material per used shader, Lambert-relevant fields only:
- **Albedo** is the base stage's image, from `ImageLoad` as RGBA8, multiplied by `qer_pbr_baseColorFactor` in linear space.
- **Alpha** is rewritten to 0 or 255 by the base `alphaFunc` (`GT0`, `LT128`, `GE128`), or forced to 255 without one.
- **Emissive** follows the spec. A shader is `additive` only if every stage is additive, so an opaque sign with a glow stage occludes.
- A 1×1 metallic-roughness texture (roughness 1, metallic 0) only satisfies sh-baker's `CHECK` should the advanced BRDF ever be reached. The normal and occlusion images are not loaded.
- Unloadable images become 1×1 white, with one warning per image.

**Area lights.** One area light is built per emissive surface, mirroring the glTF loader's `ProcessAreaLights`: area, then flux from `Flux`.
- The textured emission CDF is built only when the surface's UVs lie in one [0, 1) tile with no two triangles overlapping in UV.
- Otherwise the CDF and UV maps are left unset, so sampling is uniform, and flux is strength · max component of (factor · mean RGB texel) · area. The mean texel ignores alpha.

This is needed because `RasterizeGeometryUVMaps` keeps one triangle per texel, so tiled emitters would lose power. Lights are created after every surface is added.

**Punctual lights.** `flux = Flux(light)` is set after scaling and cones (Context).

### 8. Shader parser

`shaderInfo_t` gains fields:
- the literal surfaceparms the editor tests (`trans`, `alphashadow`, `noshadows`, `trigger`, `hint`, `sky`, `fog`, liquids, `playerclip`, `botclip`, `areaportal`, `nodraw`, `nonsolid`);
- the parsed stage list (`map` or `clampMap`, `blendFunc`, `alphaFunc`, `rgbGen const`, `tcGen environment`, the `$lightmap` stage);
- the `qer_pbr_*` values.

Existing fields, including `lightImagePath`, are unchanged.

The derivation rules are pure functions in `light_sh_model`, ported from `deriveQuake3` and `Shadow_materialCasts` and unit-tested with the editor's spec cases. The cases include an opaque-plus-additive shader.

### 9. Light entities

The keys are read as the spec lists, mirroring `PBRLight` in `plugins/entity/light.cpp`:
- **Defaults.** Intensity is 50000 (sun 3). Radius 256, cone 45 and inner cone 30 are the fallbacks for unparsable or out-of-range values.
- **Cones.** The 89.9° clamp, and `cone_inner` ≤ `cone`.
- **Direction.** `angles` gives (cos yaw cos pitch, sin yaw cos pitch, −sin pitch). `angle` is yaw only, with −1 and −2 meaning up and down. A spot's `target` wins.
- **Colour.** `_color` is divided by its largest component.
- **Shadows.** Point lights never cast. Spots and suns follow `_shadows`.

Only the first sun is used.

### 10. Points and the single call

The point list is laid out in three runs:
1. Super-luxels, in raw lightmap order, row-major. Each is mapped and not `CLUSTER_OCCLUDED`. Other slots keep `material_id −1`.
2. Grid point pairs, unless `-nogrid`.
3. Draw vertices, in surface order, excluding `q3map_noVertexLight` shaders and `_skybox` copies, unless `-novertex`.

Every baked point has `material_id 0`, and the scene always holds at least one material.
- **Vertex offset.** Vertex points are offset 0.25 map units along the normal. This is new, and not taken from q3map2: it keeps the scaled ray offsets clear of the vertex's own surface. Vertices in solid are not nudged.
- **Normals and tangents.** Super-normals come from `MapRawLightmap`, including `-shade`. Tangents are any unit vector perpendicular to the normal, with w = +1.
- **Determinism.** sh-baker seeds per index, so the output doesn't depend on the thread count.
- **Progress.** Progress goes to `Sys_Printf` through B3's `progress_fn`.
- **Memory.** sh-baker holds the input point (48 B), two SH results (216 B), irradiance and visibility (16 B). q3map2 adds 112 B of SH per super-luxel, about 420 B per point at peak. A 4 M-point town peaks near 1.7 GB. The bake results are copied into q3map2's buffers and freed straight after the bake.

### 11. `light_sh_model`: the tested core

`light_sh_model.{h,cpp}` depends only on the standard library and `math/vector.h`. It holds:
- the light conversion from key strings;
- the caster and material rules over a stage struct;
- SH → irradiance;
- `ShLightmapBytes`;
- the grid fit including the engine model;
- the alpha rewrite;
- the single-tile UV test.

`light_sh_model_test.cpp` and `make test-q3map2-shbake-model` follow `test-q3map2-extension` exactly. `light_sh.cpp` holds only the glue that needs sh-baker or q3map2 globals.

### 12. Side-file version 2

`SHQ3Map2Extension` gains `SHBakeContents { Full = 0, IndirectOnly = 1 }`, stored after the version.

The B4 tests move to version 2:
- the golden bytes become 148;
- the empty file becomes 28 bytes;
- the version tests check 1 and 3.

There are new tests for the contents field. Full contents is main + punctual SH; `-shindirect` writes main alone.

### 13. Build

`SH_BAKER=1`:
- adds `-Itools/sh-baker/src`, Eigen and `-march=native` for `light_sh.o`;
- links `-Ltools/sh-baker/build -lsh_baker` with an `$ORIGIN` rpath;
- copies `libsh_baker.so` into `install/`.

Without the flag, `light_sh_stub.o` is linked. sh-baker is built in Release by its own CMake, as the READMEs document.

### 14. Option classification

Every `-light` option is classified in `LightMain`:

| Class | Options |
| --- | --- |
| Refused | `-deluxe`/`-deluxemap`, `-deluxemode`, `-debugdeluxe`, `-external`, `-lightmapdir`, `-lightmapsize` ≠ game, `-extlmhacksize`, `-trisoup`, `-bounce` > 0, `-approx` > 0, `-fill`, `-fillpink`, `-nolm`, `-debug`, `-debugsurfaces`/`-debugsurface`, `-debugaxis`, `-debugcluster`, `-debugorigin`, `-debugnormals`, `-border`, `-dirtdebug`/`-debugdirt`; and a used shader's `q3map_lightmapSize` ≠ game. Refusal is decided from the state the options set (`deluxemap`, `externalLightmaps`, the page size, `trisoup`, `bounce`, `approximateTolerance`, `lightmapFill`, `nolm`, the debug flags), so aliases are covered |
| Honoured | `-super`/`-supersample`, `-samplesize`, `-minsamplesize`, `-extravisnudge`, `-rawlightmapsizelimit`, `-nocollapse`, `-nolightmapsearch`, `-lightmapsearchpower`, `-lightmapsearchblocksize`, `-slowallocate`, `-shade`, `-shadeangle`, `-nogrid`, `-novertex`, `-export`, `-lomem`, `-custinfoparms`, `-wolf`, `-q3`, `-threads`, and `-shbake`'s own options |
| Ignored, warned once | every other `-light` option, from `-point`, `-spot`, `-area`, `-sky`, `-scale`, the `-grid*` scales and `-gamma`/`-sRGB*`/`-exposure`/`-compensate`/`-brightness`/`-contrast`/`-saturation`, through `-samples`, `-randomsamples`, `-filter`, `-dark`, `-thresh`, `-fast*`, `-cheap*`, `-patchshadows`, `-dirty*`, `-floodlight`, `-lowquality`, `-trianglecheck`, `-nostyle`, `-style`, `-cpma`, `-lightanglehl`, `-bounce*`, `-sunonly`, `-onesky`, `-extradist`, `-normalmap`, `-notrace`, `-nosurf`, `-dump`, `-vertexscale`, `-backsplash` |

`-trisoup` is refused because it turns planar surfaces into vertex-lit triangle soups before the write (`light.cpp:1890-1897`), so no page would match them. A task cross-checks this table, aliases included, against `LightMain`, so that no option is left unclassified.

### 15. Branching and gate

- **sh-baker PR.** The PR (group 2) is opened after asking the user.
- **netradiant branch.** The netradiant work is on `b5-shbake` from `master`. The submodule pointer moves after the PR merges.
- **Gate for every group:**
  - both builds;
  - both test targets;
  - the stock check on a clean `git archive` copy;
  - until group 7, the single-threaded placeholder BSP is byte-identical to the baseline apart from the version stamp. Any difference is reported first.

## Risks / Trade-offs

- **[The tonemapped encoding uses only bytes 0–64]** → That gives about 65 levels, with banding in dark gradients. This is inherent to the stock overbright path. A9 judges it.
- **[Coloured albedo differs from the preview]** → The preview tonemaps albedo × light, while Q3 multiplies the albedo by a tonemapped light. This is exact for white, and saturated colours drift in the highlights. It is recorded for A9.
- **[The grid engine model may differ in the fork]** → Task 6.3 confirms the constants from source and checks them with a screenshot.
- **[Bevel detection misfires]** → The test needs both the `noshader` shader and a degenerate winding. Failed hulls are skipped with a warning, and draw surfaces still occlude.
- **[Bake time on a town]** → `-shsamples` and `-shbounces` are available, and defaults are 128 and 5. The Town build and the stock check use the defaults.
- **[ABI drift between sh-baker headers and the `.so`]** → The submodule pointer is pinned, and q3map2 is rebuilt whenever it moves.
- **[The port of the editor's rules drifts]** → The unit tests take the editor's spec cases. A divergence is fixed in both.
- **[The stock check needs an `SH_BAKER=1` q3map2]** → It fails with q3map2's clear message, and the README gives the build steps.
- **[Hard punctual shadows alias]** → `-super N` averages them, and A9 picks the Town build's value.
- **[Lambert-only bake]** → Metal and glossy surfaces bounce light as diffuse. This is recorded as a fidelity gap.

## Migration Plan

- **Side-file format.** It goes from version 1 to version 2 with no migration, because no version 1 file exists outside the tests.
- **Workflow switch.** The stock check and the Town build switch in group 7.
- **Rollback.** Reverting the netradiant PR reverts the submodule pointer. The sh-baker PR's defaults keep sh-baker's own behaviour, so it can stay merged.

## Open Questions

- **Whether the Town build adds `-super 2`.** This is a tuning choice for A9 that changes one build-menu argument.
