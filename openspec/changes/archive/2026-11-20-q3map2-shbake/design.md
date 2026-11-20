# Design: `-shbake` in q3map2 (B5)

## Context

The motivation is in proposal.md. The relevant state:

- **sh-baker at `8f181ba`** (origin/main, the submodule pointer on `b5-shbake`; #68 on top of `fdad058` (#67), which is on top of `046ff2e`, where this was first written):
  - **The bake call.** `BakeSHLightMap(scene, points, raster, config)` (`src/baker.h`) bakes N points laid out N×1, in one call with one BVH. Per point it returns an `SHCoeffs` and an environment visibility. `SHCoeffs` holds nine RGB coefficients, the hemisphere projection ∫ L_i Y dω, and `irradiance`, an RGB Monte Carlo estimate of ∫ L_i max(0, n · ω) dω at the point's normal (`sh_coeffs.h:26`, accumulated at `sh_coeffs.cpp:83` for both the next-event samples and the traced rays). Points with `material_id < 0` keep the marker −1. Any id ≥ 0 bakes; the id is used for nothing else.
  - **Scene input.** `AddQ3Map2Surface` (`src/loader_q3map2.h`) appends one checked draw surface. A surface with `material_id < 0` is a pure occluder. Additive materials (`Material::additive`, meaning every stage blends `GL_ONE` on the destination) are left out of the BVH and act as emitters only.
  - **Indirect-only.** Since #66, `BakeConfig::indirect_only` drops all direct light at the baked point, punctual and area (emissive) alike: it skips the point's next-event estimation (`baker.cpp:212`). Every bounce still sees every light.
  - **Bounces.** `bounces = 0` still adds next-event estimation at the first hit, so it is one bounce of indirect light, not none.
  - **Light tree.** It drops any bounded light whose `flux` is 0. The glTF loader sets `flux = Flux(light)` (`loader.cpp:797`), and q3map2 must do the same.
  - **Light model.** #65 (merged as `ad8f39f`) added `Light::range`, `Light::ConeFalloff::kSmoothstep` with a hard edge for equal cones, and range-aware light selection (decision 1). Its review dropped `Light::casts_shadows` and the split punctual-direct result, so every light is still shadowed and there is no per-light switch.
  - **Sample domain.** Every point samples the hemisphere around its normal with the bake-wide weight 2π (`baker.cpp:163`, applied at `:220`). Next-event estimation passes the point's normal to the light tree, and the incoming-radiance functions reject light below the surface (`light.h:71,115,160,216`). `SurfacePoint` (`rasterizer.h:18`) had no full-sphere mode; #67 (`fdad058`) added `SurfacePoint::full_sphere` (decision 6).
  - **Sample count.** The sensor's adaptive stop ends a point after its 64-sample minimum once the 3σ error of its L0 luminance falls below `max(confidence_threshold · mean, 1e-4)`. The absolute floor ended every point whose per-sample L0 is constant (one lit only by unshadowed point lights) at 64 samples, whatever `samples` said. #68 (`8f181ba`) made `confidence_threshold <= 0` turn the stop off, so every valid point takes exactly `samples` rays; the default 0.01 is unchanged. q3map2 always passes 0 (decision 1).
  - **Ray offsets.** Shadow rays and the sensor's first ray use `tnear` 0.005 (`light.h`, `sensor.cpp:85`). `Trace()` resets every traced primary and bounce ray to 0.001 (`tracer.cpp:16`). Transmission continuations use 0.005 (`occlusion.cpp:138`).
  - **Shading.** The bake's BRDF is Lambert: it uses only albedo, alpha and emission. `normal_texture` and the metallic-roughness texture are never read on the bake path. Alpha is fractional: transmittance is 1 − alpha.
  - **Absolute thresholds.** The emission CDF cut-offs (`scene.cpp:267`, `:424`), `joint_pdf > 1e-8`, and the 1e-12 area and Jacobian guards in `light.cpp` are absolute. The sensor's `tfar` is 1e4.
- **q3map2's `-light`** (`light.cpp`, `LightWorld`). Line numbers are the `b5-shbake` working tree's, as tasks.md uses:
  - **Order.** `SetupGrid`, then `CreateEntityLights` and `CreateSurfaceLights`, `TraceGrid`, `MapRawLightmap`, dirt and floodlight, `IlluminateRawLightmap`, `StitchSurfaceLightmaps` (disabled: it returns at once, `lightmaps_ydnar.cpp:1181`), `IlluminateVertexes`, the bounce loop, and finally `StoreSurfaceLightmaps`.
  - **Neighbour flood.** `IlluminateRawLightmap` ends with a 3×3 neighbour flood of occluded and unmapped super-luxels (`light_ydnar.cpp:2691-2787`). It skips neighbours with `count == 0` (`:2748`). This is the only place `CLUSTER_FLOODED` is set (`:2786`).
  - **Storage steps in `StoreSurfaceLightmaps`** (`lightmaps_ydnar.cpp`):
    - the super-sample average (2315-2494), which takes only luxels with `count > 0`;
    - a BSP-level neighbour fill for luxels with no sample, which averages colour but zeroes the deluxel (2534-2588);
    - wrap;
    - collapse into twins, skipped under `-nocollapse` or `-deluxe`;
    - solid stamping, skipped under `-nocollapse`;
    - vertex approximation, only with `-approx`;
    - placement, through `ColorToBytes`;
    - `FillOutLightmap` (called at 2993), only with `-fill`.
  - **Deluxemap precedent.** `-deluxe` carries a parallel per-luxel buffer through most of these steps, but not through the neighbour fill.
  - **Byte conversion.** All 8-bit lighting output goes through `ColorToBytes` (`light_ydnar.cpp:62`). Worldspawn `_minlight`, `_minvertexlight` and `_ambient` apply on q3map2's 0–255 scale.
  - **Grid.** `TraceGrid` nudges in-solid grid points with `Random()` and forces ambient to 1 when ambient is 0 but directed is not, because Q3 skips grid cells whose ambient is 0.
- **Brush lump.** Bevel sides are written with the `noshader` shader: `EmitShader` sets it (`writebsp.cpp:45`), and the bevel branch uses it (`:382`). Brushes of a model with an origin are stored in model space; `SetEntityOrigins` moves only draw vertices.
- **The editor:**
  - **Tonemap.** The lighting preview tonemaps with Uncharted2 (Hable constants, white point 11.2) and then sRGB-encodes (`setup/data/tools/gl/tonemap_fp.glsl`).
  - **Light keys.** Defaults, clamps, `angle`, and `_color` normalised by its largest component are in `plugins/entity/light.cpp` and `colour.h`.
  - **Rules.** The base-colour and emissive rules are in `plugins/shaders/shaders.cpp` (`deriveQuake3`), and the caster rule in `radiant/renderstate.cpp` (`Shadow_materialCasts`).
- **The stock renderers.**
  - **opengl1** in a window shifts lightmap, vertex and grid bytes left by 2 (`r_mapOverBrightBits 2`, no hardware gamma), so byte 64 is a multiplier of 1.0. Model lighting is grid ambient × `r_ambientScale` (0.6) plus `identityLight` · 32, and directed × `r_directedScale`.
  - **opengl2** at defaults shifts by 1, multiplies most stages by 2 but `rgbGen vertex` by 1, and tonemaps with auto-exposure.

## Goals / Non-Goals

**Goals:**
- Two ordinary bakes per compile over one point list: the full bake fills every stock output (lightmap pages, vertex colours, grid bytes), and the indirect bake fills the side file for `renderer_sh`.
- SH that maps to the BSP's own lightmap pages with no extra mapping, and an SH grid that maps to the BSP's light grid.
- A page layout that doesn't depend on lit bytes, so side-file page i is BSP page i whichever bakes run.
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
- **A per-light shadow switch in the bake.** sh-baker shadows every light (decision 1).
- **Emissive direct light in the side file.** The indirect bake leaves it out (#66). `renderer_sh` gets it later from LTC area lights (C7).

## Decisions

### 1. Two independent bakes over one point list, and the merged light model

**The light model (#65, merged as `ad8f39f`).**
- **`Light` fields:**
  - `float range = 0`: the test sits in the punctual incoming-radiance functions (`light.h:31-34`), so it covers both the baked point and the bounces.
  - `ConeFalloff cone_falloff = kLinear`: smoothstep treats cos inner ≤ cos outer as a hard edge (`SpotConeFactor`, `light.h:38-51`). Linear mode keeps the old formula unchanged, the division by zero for equal cones included. q3map2 always uses smoothstep.
- **Light tree.** `LightBounds` has `max_range`, where 0 means unbounded. `Union` gives 0 if either side is 0, else the larger range. `Importance` returns 0 when the shaded point is farther than `max_range` from the node's bounds (`light_tree.h:40-52`).
- **Choosing between the BVH and the directional set.** `Sample` and `Pdf` evaluate the BVH root's importance at the shaded point first. When it is 0, the whole choice goes to the directional set, or nothing is sampled if there is none. Otherwise directional lights keep their fixed share, n_dir/(n_dir + 1). Both apply the same per-point rule, so the estimator stays unbiased.
- **Dropped in review.** `Light::casts_shadows` and the split option (`split_punctual_direct`, `punctual_direct_sh_texture`, `punctual_direct_irradiance`) were removed by #65's final commit. Every light casts in a bake, point lights included (decision 9). `SHCoeffs::irradiance` already gives the lightmap a cosine-weighted estimate with no SH ringing (decision 5), and with hundreds of lights an exact per-light pass costs time that light-tree importance sampling spends better.

**Two bakes.** `ShBakeWorld` makes up to two ordinary `BakeSHLightMap` calls over the same point list (decision 10), so results line up index for index:
- **Full bake** (`indirect_only = false`): direct plus indirect. It feeds the stock target: the BSP lightmap pages, the vertex colours and the grid bytes (decisions 5 and 6).
- **Indirect bake** (`indirect_only = true`): no direct light at the baked point, punctual or emissive (#66). It feeds `renderer_sh`: the SH side file, its luxel pages and its SH grid (decision 12). `renderer_sh` computes direct light at runtime from the editor's lights and reads only indirect light from the side file.

**Options.** `-shbake` runs both by default, because the shippable pk3 must run on stock ioquake3 and on `renderer_sh`, and the easiest editor path should give the shippable result. The three Town builds and the stock check use the default. q3map2 passes `confidence_threshold = 0` to both bakes (`light_sh.cpp`, `ShRunBake`), so `-shsamples N` is the exact number of rays per point and a bake's time is linear in samples × points. The Town builds are quality presets that differ only in the light stage: draft `-shsamples 128`, pre-release `-shsamples 512 -super 2`, final `-shsamples 2048 -super 2`. The stock check keeps the defaults (128, `-super 1`); task 9.2 records the presets' times and noise.
- `-shnofull` skips the full bake. Every lightmap luxel a surface uses, and every baked vertex colour, is written as neutral byte 64, which opengl1 in a window turns into a multiplier of 1.0 (Context), so textures show at their own brightness. 128 would look twice overbright. The grid gets neutral ambient 63 63 63 and no directed light, not 64, which would trigger the engine's rescale of the triple (decision 6).
- `-shnoindirect` skips the indirect bake. No side file is written, and the stage first removes an earlier run's `maps/<name>/<name>.shsf` and `<name>_l0_NNNN.hdr` dumps (every `-shbake` run does), so no stale side file sits next to a BSP whose pages it no longer matches.
- Both may be given: nothing is baked, the lightmap pages and vertex colours get 64, the grid its neutral bytes, and there is no side file.

**Cost and determinism.** Both bakes cost roughly twice one bake. Each converges on its own, and sh-baker seeds each point by its index (`baker.cpp:188`), so each bake is deterministic for a given point list.

*Alternatives:*
- *One bake with a split punctual-direct result:* rejected in #65's review (above).
- *One full bake, with the side file holding direct light too:* `renderer_sh` would count direct light twice or lose its runtime shadows and specular. The editor can't read the side file, so the engine does the direct light.
- *Lightmap irradiance from the SH:* the order-2 SH of a delta light rings: +6.25% at normal incidence, wrong at grazing angles, and a 0.0625·E floor on the back. `SHCoeffs::irradiance` has none of that.

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
6. `ShBakeWorld()`, which first removes an earlier run's side file and L0 dumps (decision 1).
7. Skips `IlluminateVertexes` and the bounce loop.
8. `StoreSurfaceLightmaps`, then writes the side file if the indirect bake ran.

`ShBakeWorld` builds the scene and the point list once, then runs the bakes the options leave on (decision 1), each with its own progress. The indirect bake runs first. Its results are copied into q3map2's buffers and freed before the full bake starts, so only one bake's results are alive at a time. It writes:
- from the full bake: the super-luxel colour buffers, as linear irradiance (decision 5), with count 1; the vertex luxels; the grid bytes (decision 6);
- from the indirect bake: the super-SH and visibility buffers, and the SH grid (decision 12).

Under `-shnofull`, every baked super-luxel and vertex luxel still gets a fixed placeholder colour, 0 0 0, with count 1, exactly the luxels the full bake would fill. `ColorToBytes` then returns 64 per channel whatever its input (decision 5), and the grid gets its neutral bytes (decision 6).

It then runs the neighbour flood of `light_ydnar.cpp:2691-2787`, minus `splotchFix`, dark and ambient handling, on colour, SH and visibility together, setting `CLUSTER_FLOODED`. The flood, the super-sample average and the BSP-luxel −1 test choose luxels by cluster and count, never by colour. Every baked luxel has count 1 in every mode, so they choose the same luxels whichever bakes run.

Under `-shbake`, `minLight`, `minVertexLight`, `minGridLight` and every `ambientColor` are zero, and `_maxlight` has no effect, since it only acts inside the replaced `ColorToBytes` (spec: the inputs are warned).

Option checks run in `LightMain` right after parsing (decision 14).

Without `SH_BAKER=1`, a stub `light_sh_stub.cpp` is linked and `LightMain` stops before `ShBakeWorld`.

### 4. SH through `StoreSurfaceLightmaps`

`rawLightmap_t` gains super and BSP SH buffers: 27 floats plus 1 for visibility per luxel, style 0 only, allocated only when the indirect bake runs. `outLightmap_t` gains a page SH buffer. The SH is the indirect bake's (decision 1).

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

**The page layout must not depend on lit bytes.** Side-file page i is BSP page i in every mode, and `-shnofull` writes the same layout as the default. Collapse into twins (`CompareBSPLuxels`, `lightmaps_ydnar.cpp:1332`) and solid stamping (`:2624-2650`) are the byte comparisons, and both are off. The raw-lightmap sort compares shader names, style counts and sizes (`CompareRawLightmap`, `:2121`), and `FindOutLightmaps` (`:1806`) tests `solid`, twins, styles and custom sizes; none of these reads a lit value under `-shbake`, where only style 0 exists. A rework task audits the rest of `StoreSurfaceLightmaps` for any other comparison and checks that the page layout is the same with and without `-shnofull`.

Before writing, any luxel still at −1 is set to 0.

### 5. Irradiance and encoding

- **Irradiance.** For a super-luxel or vertex, E is the full bake's `SHCoeffs::irradiance` at that point: the cosine-weighted Monte Carlo estimate at its normal (Context), direct plus indirect, with no SH ringing. `-super N` averages E linearly, which also reduces its noise. The SH convolution (Σ Â_l L_lm Y_lm(n)) is no longer the byte source; `ShIrradiance` stays in `light_sh_model` only if a test or diagnostic still uses it, and the rework task after 4.4 drops it otherwise.
- **Encoding.** `ShLightmapBytes(E)` (in `light_sh_model`) computes, per channel:
  - d = E/π;
  - t = clamp(U(d)/U(11.2), 0, 1), where U is Hable's curve with the preview's constants;
  - byte = round(63.75 · sRGB(t)).

  This is a Lambert approximation of the preview. A white Lambert surface at exposure 1 would show sRGB(U(d)/U(11.2)), and opengl1 multiplies a texel by 4·byte/255, so for white albedo the lightmap reproduces that approximation up to 8-bit steps. The preview itself shades with (1 − F)·Disney diffuse plus GGX specular with F0 = 0.04 (`pbr_fp.glsl:239-252`), so it differs with view angle and roughness: a recorded fidelity gap. For coloured albedo the match is weaker still, since the preview tonemaps the product. Hable's curve is the roll-off, and it reaches 1 at the white point.
- **Calibration.** It is calibrated to opengl1 at default cvars in a window (the user's choice). The opengl2 differences (shift by 1, ×2 except `rgbGen vertex`, auto-exposure) go into the gamepack README for A9.
- **Plumbing.** Under `-shbake`, `ColorToBytes` returns `ShLightmapBytes(color)` and ignores its scale arguments. Under `-shnofull` it returns 64 per channel, the neutral byte (decision 1), whatever the colour buffers hold. The buffers still get a placeholder with count 1 (decision 3), so the same luxels reach `ColorToBytes`. Page area no surface uses, and luxels left at −1, stay 0 0 0 as in the default.

### 6. Grid: one full-sphere point per grid point, fit from one SH

**Grid points.** Each grid point is one baked point with `SurfacePoint::full_sphere` set, in both bakes. The full bake's SH gives the grid bytes (below). The indirect bake's SH gives the side file's SH grid (decision 12). Both are projections ∫ L_i Y dω over the whole sphere.

**sh-baker PR: `SurfacePoint::full_sphere`** (`bool`, default false; task 2.4). When set:
- sample rays are uniform over the sphere in world space, with no tangent frame, so no tangent is needed, and they are weighted 4π instead of the bake-wide 2π (`baker.cpp:163,220` becomes per point);
- next-event estimation at the point passes a zero normal to the light tree, which `Importance` already supports (`light_tree.h:51`), and the incoming-radiance functions skip their `cos_n` rejection (`light.h:71,115,160,216`) only at the baked point, not at bounces. A parameter set only for the baked point carries this;
- `AccumulateRadiance`'s projection (radiance × Y, no cosine) is already right. Its `irradiance` is meaningless for such a point, and q3map2 ignores it;
- with the default, every existing bake is bit-identical: a new defaults test compares a fixed scene's Release bake bit for bit (task 2.4).

Its tests: a probe in empty space between two opposed lights matches the analytic full-sphere SH; a sun exactly edge-on to the probe is still counted; a probe inside a closed box of inward-facing emissive quads of uniform radiance L and albedo 0 gives L0 = 2√π · L with L1 and L2 zero (`BakeSHLightMap` never reads `Scene::environment`, so a sky can't stand in); defaults are unchanged. It is clang-formatted (Google style), shown to the user before any push, and merged by the user. Then the submodule pointer moves and the Release library is rebuilt. Group 6 depends on it.

**The fit.** `ShFitGridPoint(sh)` fits the full bake's grid SH as ambient plus one directional light:
- **Direction.** d is the normalized luminance-weighted L1 vector (L11, L1m1, L10). When its length is too small to normalize (a threshold set in `light_sh_model`), d is +Z.
- **Directed.** D = max(0, L1 · d)/0.488603 per channel. This is the E⊥ of a delta light along d, so for one light it is exactly its E⊥.
- **Ambient.** The delta light's L0 projection, 0.282095 · D, is subtracted from L0 per channel and the rest clamped at 0. Then A = π · 0.282095 · L0_rest, the irradiance of the remaining light averaged over all normals (a constant radiance L gives A = πL).
- **Tests.** One sun: exact direction, D = E⊥, ambient about 0. A constant sky: ambient πL only, no directed.
- **Bytes.** In opengl1 a model's realised ambient is min(255, shift(a)·0.6 + 32·identityLight), and a face towards d adds shift(dir)·1 (`tr_light.cpp:333-358`, `tr_shade_calc.cpp:1083-1104`). target(·) is the post-shift value, 4 × the lightmap byte. The bytes are solved per channel:
  - ambient: post-shift value = clamp((target(enc(A)) − 32)/0.6, 0, 255), divided by 4 into a pre-shift byte;
  - realised = min(255, ambient_post · 0.6 + 32), from the final ambient byte (after the ambient-1 rule below);
  - directed: post-shift value = clamp(target(enc(A + D)) − realised, 0, 255), divided by 4.

  Each pre-shift byte is clamped to 63. Any byte above 63 would trigger `R_ColorShiftLightingBytes`' rescale of the whole triple (`tr_bsp.cpp:103-127`) and shift the hue. Ambient is set to 1 1 1 whenever its triple is 0, with or without directed light, before directed is solved: the engine skips a cell whose ambient sums to 0 as one in a wall (`tr_light.cpp:197-199`), so a dark baked point would otherwise be replaced by its brighter neighbours (task 6.3 widened q3map2's rule, which needs directed light). Exceptions: faces darker than the engine's floor of 32 get the floor, and ambient above about 185/255 post-shift, enc(A) ≈ 46, can't be reached.
- **Confirming the model.** Task 6.3 confirmed this model and its constants from the fork's `renderergl1` source (`tr_light.cpp`, `tr_bsp.cpp`, `tr_image.cpp`, `tr_shade_calc.cpp`); none differ. opengl2's version is in the gamepack README.
- **Grid positions.** These are computed single-threaded before the bakes, using `TraceGrid`'s nudge with a fixed, index-seeded sequence instead of `Random()`, so the output doesn't depend on threads. Points that stay in solid aren't baked (`material_id −1`): their grid bytes are black and their SH-grid probe is invalid (decision 12).
- **Neutral bytes under `-shnofull`.** Every grid point that isn't in solid gets ambient (63, 63, 63), directed (0, 0, 0) and the direction bytes q3map2 writes for +Z. The ambient is the fit's for a target of byte 64: (255 − 32)/0.6 exceeds 255 post-shift, so it takes the per-channel clamp of 63. The fit would also put the gap from the ambient ceiling (183.2) to 4 · 64 into directed light along the direction, round((256 − 183.2)/4) = 18; the neutral keeps directed 0 so that no direction is favoured. Models show at the engine's ambient ceiling, 0.6 · 252 + 32 ≈ 183/255, not at 1.0; the engine can't reach 1.0 from ambient alone. The ambient-1 rule doesn't change it, since ambient isn't 0.

### 7. Scene assembly

**Surfaces.** For every BSP draw surface of every model, except two kinds:
- `_skybox` copies (`parentSurfaceNum >= 0`): they sit in the portal sky's transformed space, and the bake has no skybox trace node to hold them;
- `MST_FOLIAGE` instances, which q3map2's tracer also skips.

For the rest:
- The geometry comes from `yDrawVerts`.
- Planar, meta and triangle-soup surfaces use the BSP indices. The benchmark's `misc_model` foliage is triangle soup.
- Patches use the tessellation q3map2's tracer uses for patch shadows.
- A surface is added if its shader casts (decision 8), or if it emits. An emitting shader that doesn't cast is marked `additive` too, so it emits without entering the BVH.
- Its material is that shader's bake material.

**Hulls.** For every BSP brush of every model:
- The brush qualifies if its content shader is solid and every non-bevel side's shader casts and isn't alpha-tested. A side is a bevel if its shader is `noshader` and its un-shrunk winding is degenerate (area < 0.1 units²).
- The planes are translated by the origin of the model's entity.
- The un-shrunk hull chops `BaseWindingForPlane` by every other plane; that winding decides the bevels. The faces the side file gets are chopped by the non-bevel planes only, since a bevel touches the hull at an edge or corner and chopping by it adds only collinear points. They are triangulated as fans, in q3map2's winding.
- The bake hull moves every drawn non-bevel plane inward by 0.25 map units, so the draw surface on it is hit first. That is above both scaled ray offsets (0.16 and 0.032 units). A `nodraw` side (caulk) has no draw surface and stays in place: moving it in too opened a 0.5-unit gap between two caulk-backed brushes at a seam, and a spot shining along the seam lit the corner luxels on the far side (task 6.5). Its polygon is chopped by the drawn planes moved in only 1/32 unit, not 0.25: chopped by the 0.25 inset it stopped short of a drawn neighbour's plane and left a strip a light within 0.25 units of that plane shone along; chopped flush, its edge sat on the drawn plane and grazed rays from luxels on that plane, such as a vertical sun at a floor's edge row (task 6.5 review). The bake hull is only an occluder, so its faces needn't meet edge to edge.
- It is added with `material_id −1`. A brush whose hull build fails is skipped with a warning naming it.

**Materials.** There is one bake material per used shader, Lambert-relevant fields only:
- **Albedo** is the base stage's image, from `ImageLoad` as RGBA8, multiplied by `qer_pbr_baseColorFactor` in linear space.
- **Alpha** is rewritten to 0 or 255 by the base `alphaFunc` (`GT0`, `LT128`, `GE128`), or forced to 255 without one.
- **Emissive** follows the spec. A shader is `additive` if every stage is additive, or if it emits but doesn't cast, so an opaque sign with a glow stage occludes.
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
- **Shadows.** sh-baker has no per-light shadow switch (decision 1), so both bakes shadow every light, point lights and `_shadows 0` lights included. The editor preview and `renderer_sh`'s runtime direct light keep the caster rule (point lights never cast; spots and suns follow `_shadows`). This changes the stock lightmap's direct light and, slightly, bounced light; A9 judges it, and no warning is printed. `light_sh_model` still parses `_shadows` (task 4.2 tests it), but the bake doesn't use it.

Only the first sun is used. A light whose intensity is 0 or negative is left out with a warning: sh-baker's light tree can't sample negative flux, and the editor's negative lights have no bake equivalent.

### 10. Points and the calls

One point list serves both bakes (decision 1). It is laid out in three runs:
1. Super-luxels, in raw lightmap order, row-major. Each is mapped and not `CLUSTER_OCCLUDED`. Other slots keep `material_id −1`.
2. Grid points, one `full_sphere` point each (decision 6), unless `-nogrid`. Points still in solid keep `material_id −1`.
3. Draw vertices, in surface order, excluding `q3map_noVertexLight` shaders and `_skybox` copies, unless `-novertex`. Under `-novertex` no vertex luxel is written, so the vertex colours are the encoding of 0 (black), or 64 under `-shnofull`.

Every baked point has `material_id 0`, and the scene always holds at least one material.
- **Vertex offset.** Vertex points are offset 0.25 map units along the normal. This is new, and not taken from q3map2: it keeps the scaled ray offsets clear of the vertex's own surface. Vertices in solid are not nudged.
- **Normals and tangents.** Super-normals come from `MapRawLightmap`, including `-shade`. Tangents are any unit vector perpendicular to the normal, with w = +1. Grid points have no normal or tangent; `full_sphere` ignores both.
- **Determinism.** sh-baker seeds per index, so the output doesn't depend on the thread count. Both bakes use the same list, so index i is the same point in each.
- **Progress.** Each bake reports to `Sys_Printf` through B3's `progress_fn`, labelled full or indirect.
- **Memory.** Per point, sh-baker holds the input point (64 B, given `Vector4f`'s 16-byte alignment; the `full_sphere` bool fits in the padding), one `SHCoeffs` with its irradiance (120 B) and visibility (4 B). q3map2 adds 112 B of SH per super-luxel, about 300 B per point at peak. A 4 M-point town peaks near 1.2 GB. The bakes run one after the other, and each bake's results are copied into q3map2's buffers and freed before the next starts.

### 11. `light_sh_model`: the tested core

`light_sh_model.{h,cpp}` depends only on the standard library and `math/vector.h`. It holds:
- the light conversion from key strings;
- the caster and material rules over a stage struct;
- `ShLightmapBytes`;
- the grid fit from one full-sphere SH, including the engine model and the neutral bytes;
- SH → irradiance, only while a test or diagnostic uses it (decision 5);
- the alpha rewrite;
- the single-tile UV test.

`light_sh_model_test.cpp` and `make test-q3map2-shbake-model` follow `test-q3map2-extension` exactly. `light_sh.cpp` holds only the glue that needs sh-baker or q3map2 globals.

### 12. Side-file version 2

Version 2 is redefined. It never landed on master: B4 on master is version 1, and the version-2 code on this branch (the contents field) is uncommitted. The side file always holds the indirect bake (decision 1), so the contents field and `SHBakeContents` are dropped. Version 2 adds an SH grid section after the hulls.

**Byte layout, version 2.** Little-endian, no padding beyond what is listed. The file ends exactly after the last valid flag.

```
header, 24 bytes:
  char[4]  magic 'S','H','S','F'
  uint32   version (2)
  uint32   page count
  uint32   hull count
  uint32   hull vertex count
  uint32   hull index count
pages:          as in version 1 (8 + 56 * width * height bytes each, binary16)
hull ranges:    per hull, uint32 first index, uint32 index count
hull vertices:  per vertex, float32 x, y, z
hull indices:   one uint32 per index
SH grid, 36 + 109 * N bytes, N = nx * ny * nz:
  float32[3]  origin (x, y, z), map units: the position of probe 0 (q3map2's gridMins)
  float32[3]  spacing (x, y, z), map units: the BSP's gridSize
  uint32[3]   dimensions nx, ny, nz
  coefficients: per probe, 27 float32, probe-major as in sh-baker's SHCoeffs:
                coefficient k, channel c of probe i at (i * 9 + k) * 3 + c
  valid flags:  one uint8 per probe, 0 or 1
```

- **Flags in a separate array.** Interleaving a byte after each probe's 108 bytes would break the 4-byte alignment of every later probe. With the flags last, every section but the flag array starts on a 4-byte boundary, and nothing follows the flags.
- **Probe order.** x fastest, then y, then z, the BSP light grid's order (`light.cpp:1471-1472`). Probe i is BSP grid point i, at origin + (x, y, z) ∘ spacing.
- **Coefficients.** Same basis, order and units as the luxel pages (radiance projected onto Y, not convolved), but integrated over the whole sphere, not a hemisphere (decision 6). They are float32, not binary16: the grid is small, and float32 avoids the 65504 clip.
- **Valid flag.** 0 when the grid point is still in solid after the nudge (decision 6's rule and index-seeded sequence). An invalid probe's 27 coefficients are 0.
- **No grid.** N = 0 is allowed (`-nogrid`): dimensions are written 0, 0, 0, origin and spacing 0, and the section is 36 bytes.

**Refusals.** The writer refuses and the reader rejects, as they do for pages and hulls:
- dimensions with some but not all components 0, or a product that overflows or exceeds the remaining file;
- a coefficient array whose length isn't 27 · N, or a flag array whose length isn't N;
- a non-finite origin, spacing or coefficient, or a spacing ≤ 0 when N > 0;
- a negative L0 component in a valid probe;
- a valid byte other than 0 or 1, or an invalid probe with a nonzero coefficient;
- truncation, or bytes after the last flag.

The version rule is unchanged: the writer writes 2, and the reader accepts only 2. `SHQ3Map2Extension` gains an `SHGrid` (origin, spacing, dimensions, coefficients, valid flags).

**Tests.** The B4 tests move to version 2:
- the header shrinks back to 24 bytes, and the contents tests go;
- the empty file is 24 + 36 = 60 bytes;
- the golden file gains a 2 × 1 × 1 grid with one valid and one invalid probe: 144 + 36 + 2 · 109 = 398 bytes;
- the version tests check 1 and 3, and the huge-counts file covers both the header counts and the grid dimensions;
- each grid refusal above has a writer test and a reader test.

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
| Honoured | `-super`/`-supersample`, `-samplesize`, `-minsamplesize`, `-extravisnudge`, `-rawlightmapsizelimit`, `-nocollapse`, `-nolightmapsearch`, `-lightmapsearchpower`, `-lightmapsearchblocksize`, `-slowallocate`, `-shade`, `-shadeangle`, `-nogrid`, `-novertex`, `-export`, `-lomem`, `-custinfoparms`, `-trianglecheck` (it moves luxel origins in `MapSingleLuxel`), `-samplescale`, `-debugsamplesize`, `-nodeluxe`/`-nodeluxemap`, `-threads`, and `-shbake`'s own options (`-shnofull`, `-shnoindirect`, `-shsamples`, `-shbounces`, `-shdumpl0`) |
| Ignored, warned once | every other `-light` option, from `-point`, `-spot`, `-area`, `-sky`, `-scale`, the `-grid*` scales and `-gamma`/`-sRGB*`/`-exposure`/`-compensate`/`-brightness`/`-contrast`/`-saturation`, through `-samples`, `-randomsamples`, `-filter`, `-dark`, `-thresh`, `-fast*`, `-cheap*`, `-patchshadows`, `-dirty*`, `-floodlight`, `-lowquality`, `-wolf`/`-q3` (they pick only the falloff in `CreateEntityLights`, which `-shbake` skips), `-nostyle`, `-style`, `-cpma`, `-lightanglehl`, `-bounce*`, `-sunonly`, `-onesky`, `-extradist`, `-normalmap`, `-notrace`, `-nosurf`, `-dump`, `-vertexscale`, `-backsplash` |

`-trisoup` is refused because it turns planar surfaces into vertex-lit triangle soups before the write (`light.cpp:1890-1897`), so no page would match them. A task cross-checks this table, aliases included, against `LightMain`, so that no option is left unclassified.

`-shnofull` and `-shnoindirect` replace `-shindirect`, which group 5 had added (`shIndirect` in `q3map2.h`, the option in `LightMain`, `help.cpp`). `-shindirect` is removed, not kept as an alias, because the side file is now always indirect. `-shdumpl0` dumps the indirect bake's L0; with `-shnoindirect` there is nothing to dump, so `ShBakeCheckOptions` warns and no `.hdr` is written (task 5.4).

### 15. Branching and gate

- **sh-baker PRs.** #65, #66, the `full_sphere` PR #67 (task 2.4) and the exact-sample-count PR #68 (task 9.1) are merged, and the pointer is at `8f181ba`.
- **netradiant branch.** The netradiant work is on `b5-shbake` from `master`. The submodule pointer moves after each PR merges.
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
- **[Bake time on a town, doubled by two bakes]** → The default runs both bakes, about twice one bake. `-shsamples` (exact) and `-shbounces` are available, and defaults are 128 and 5. `-shnofull` or `-shnoindirect` halves the time while iterating on one target. The stock check and the draft Town build use 128 samples; the pre-release (512, `-super 2`) and final (2048, `-super 2`) builds take 22 s and 90 s on the benchmark.
- **[The bake shadows every light]** → Point lights and `_shadows 0` lights cast in the stock lightmap but not in the preview or in `renderer_sh`'s direct light (decision 9). It is documented, not warned, and A9 judges it.
- **[Noise in the Monte Carlo irradiance]** → The lightmap bytes come from a sampled estimate, not an exact per-light term. The sample count is fixed (`confidence_threshold` 0), so the noise is set by `-shsamples` and `-super N`, and the encoding's 65 levels hide part of it. The final preset is at the 8-bit rounding floor on the benchmark; hard lighting can need 4096 samples. A9 judges what is left and may change the presets.
- **[Emissive direct light missing from the side file]** → The indirect bake drops it (#66), so `renderer_sh` lacks it until LTC area lights (C7). The stock lightmap has it.
- **[The page layout comes to depend on bytes]** → A future change could reintroduce a byte comparison in `StoreSurfaceLightmaps`. The audit task and the `-shnofull` layout check guard it (decision 4).
- **[ABI drift between sh-baker headers and the `.so`]** → The submodule pointer is pinned, and q3map2 is rebuilt whenever it moves.
- **[The port of the editor's rules drifts]** → The unit tests take the editor's spec cases. A divergence is fixed in both.
- **[The stock check needs an `SH_BAKER=1` q3map2]** → It fails with q3map2's clear message, and the README gives the build steps.
- **[Hard punctual shadows alias]** → `-super N` averages them. The pre-release and final presets use `-super 2`; A9 may change it.
- **[Lambert-only bake]** → Metal and glossy surfaces bounce light as diffuse. This is recorded as a fidelity gap.

## Migration Plan

- **Side-file format.** It goes from version 1 to version 2 with no migration, because no version 1 file exists outside the tests. The uncommitted contents-field version 2 on this branch is replaced, not migrated: no file of that shape was ever written outside the tests.
- **Options.** `-shindirect` is removed. Nothing outside this branch used it.
- **Workflow switch.** The stock check and the Town build switch in group 7, with the default `-shbake` (both bakes). Group 9 splits the Town build into the three presets.
- **Rollback.** Reverting the netradiant PR reverts the submodule pointer. #65, #66, the `full_sphere` PR and #68 keep sh-baker's own behaviour at their defaults, so they can stay merged. #66 changes `indirect_only` output for other users of sh-baker, but that is sh-baker's own decision.

## Open Questions

- ~~**Whether the Town build adds `-super 2`.**~~ Settled in task 9.2: the pre-release and final presets add it, the draft and the stock check don't.
- **Whether the bake should honour `_shadows` later.** It would need a per-light switch in sh-baker, which #65's review dropped. A9's verdict on the shadow difference decides whether to ask again.
- ~~**The grid direction threshold.**~~ Settled in task 6.3: `c_shGridMinL1Length2` stays 1e-30. Below any useful threshold D rounds to directed byte 0, and the engine multiplies the direction by the directed light, so d has no visible effect there.
