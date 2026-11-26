# Design

## Context

- **The derivation is done and editor-free.** A12 put it in `libs/skylight.h`:
  - `SkySource`;
  - `SkyRadiance`, which gives L(ω) for a direction (nearest farbox texel, or the cloud mean above the horizon);
  - `ProjectRadiance` and `Irradiance`;
  - `SortCandidates` and `ChooseSky`.

  The editor fills `SkySource` from `IShader` (`radiant/skyenvironment.cpp`). q3map2 must fill it from `shaderInfo_t`.
- **sh-baker at `297a88f` (B11) has one way in: `Scene::environment`.** When it is set, an escaping ray returns `EvaluateEnvironment(env, dir)`:
  - the full bake gets direct and bounced sky light;
  - an `indirect_only` bake gets bounced sky light only.

  `Environment` has two kinds: an equirectangular `Texture32F` (filtered bilinearly, `light.cpp:210-256`) and Preetham. It has no cube map and no callback. Its texture frame is Y-up:
  - texel (x, y) of W × H is at θ = (y + ½)π/H from +Y and φ = (x + ½)2π/W;
  - its direction is (sin θ sin φ, cos θ, sin θ cos φ).

  `to_environment` turns world directions into that frame. The bake reads only `texture` and `to_environment`: `intensity_multiplier` and `sh_coeffs` are glTF-loader conveniences that no bake path reads.
- **q3map2's shader parser misses three things** that `SkySource` needs:
  - It stores `skyParmsImageBase` for the placeholder `-light`, and drops `-` and `full`. The editor keeps every token but `-` (`plugins/shaders/shaders.cpp:1811`).
  - `shaderSH_t::pbrEmissiveStrength` defaults to 1 and has no "declared" flag, while `skylight::EffectiveStrength` needs one.
  - The stages are already parsed into `shaderSH_t::stages`, with each stage's rend2 flag.
- **Sky shaders already cast nothing.** The caster rule never casts a `skyparms` or `surfaceparm sky` shader, and their brushes make no hull. Escaping rays therefore leave through the town's low sky ceiling, as the stock engine's sky does.
- **The emissive rule (`light_sh_model.cpp` `ShDeriveMaterial`) ignores sky shaders.** It takes the first additive map stage, with the strength defaulting to 1. A sky's additive cloud layer therefore becomes an unattached emitter. Being additive, it is also kept out of the BVH.

## Goals / Non-Goals

**Goals:**
- Both bakes see exactly A12's L(ω), within the resampling tolerance, in A12's orientation.
- No sh-baker change.
- Byte-identical output for maps without sky light (and without an additive sky stage), so the benchmark and the fixtures stay the regression baseline.
- The resampling is checked against sh-baker's own reading of the texture on every run, not only in unit tests.

**Non-Goals:**
- Importance sampling of the environment. sh-baker reaches the sky only through its cosine-sampled sensor rays and bounces; there is no next-event estimation (NEE) towards the sky. Shaded areas are noisier at low sample counts, and B7's guiding covers this.
- Side-file changes. The format and version are untouched: `q3map2_extension.h` already says that bounced sky light is kept when the scene has an environment. Per-probe sky visibility is B13.
- Editor changes. The editor's emissive data for a sky shader doesn't light anything in the preview, and the sky draws at its own bytes (A13).
- A sky in `_skybox` portal space, `q3map_skyLight`, or `q3map_sun`, which stay unsupported and warned.

## Decisions

### 1. Hand sh-baker an equirectangular texture resampled in q3map2

q3map2 evaluates `skylight::SkyRadiance` over an equirect grid and passes the result as `Environment{Type::Texture}`. sh-baker's bilinear lookup then gives L(ω) on every escape.

- *Rejected: a sh-baker PR adding a cube-map environment or a radiance callback.* A cube map would match a farbox texel for texel. But B12 is a q3map2 task, the bakes are diffuse, and an equirect at the resolution below is finer than the town's or a stock farbox's detail matters for. A `std::function` callback would cost an indirect call per escape, and would put q3map2's types into the Monte Carlo loop.
- *Rejected: handing over the SH.* That is forbidden by the spec: the truncation is −ℒ/4 straight down for a cloud layer.

### 2. Resolution 1024 × 512, each texel a solid-angle-weighted 4 × 4 mean

The texture size is fixed, whatever the farbox's face size.
- **Cost.** It is 6 MB of float RGB, and about 8.4 M `SkyRadiance` calls to fill. Those calls are table lookups, under a second.
- **Detail.** At the equator a texel spans 0.35°, the same as a 256-pixel farbox face at its centre.
- **The filter.** Each texel is the mean of L over a 4 × 4 grid of (θ, φ) sub-positions inside it, weighted by sin θ. This box-filters the farbox's nearest-texel radiance, so face seams and the equirect's own aliasing don't bias the integral.
- **The cloud horizon.** H is even, so the horizon falls on a row boundary. Every row of a cloud sky is then exact (ℒ or 0), and only sh-baker's bilinear blend smears the horizon, across ±0.18°. Directions there have cos θ ≈ 0 for an upward normal. For a horizontal normal, the gain just below the horizon cancels the loss just above it.
- *Rejected: a size derived from the face size.* It would not be deterministic across sky edits, and it gains nothing for diffuse light.

### 3. Frame: `to_environment` maps world (x, y, z) to texture (y, z, x)

`to_environment` has the rows (0 1 0; 0 0 1; 1 0 0). This is a cyclic permutation, so a proper rotation with determinant +1.
- World +Z becomes sh-baker's +Y (up), and θ is measured from world up.
- φ = atan2(y, x) is the usual map azimuth from +X: texture column 0 starts at world +X, and columns increase towards +Y.
- The resampler computes each texel's world direction as `to_environmentᵀ × dir_texture` and evaluates `SkyRadiance` there.

Orientation is therefore owned in one place, by `skylight.h`'s face mapping, and the resampler only has to invert sh-baker's documented texel convention.

### 4. Cross-check through sh-baker's own projector, every run

After filling the texture, the glue projects it with `sh_baker::ProjectEnvironmentToSH`, the inverse of `EvaluateEnvironment`'s texel mapping. The result is in the texture frame. From it the glue evaluates the cosine-convolved irradiance at R·n, for n in ±X, ±Y and ±Z.
- It compares these with `skylight::Irradiance(ProjectRadiance(sky), n)`, and warns if any channel differs by more than 1% of the largest of the six irradiances (max over n and channels). Measuring against E(+Z) alone would leave no tolerance for a sky lit from the side or below, such as a `_dn`-only farbox.
- It prints both values at +Z in the summary.
- It stores the projection in `env.sh_coeffs`. No bake reads it, but it keeps the struct coherent.

This catches a wrong frame, row order or channel order, which unit tests of q3map2's own integral would miss: they would share q3map2's mistaken idea of sh-baker's layout. The pure tests (task 2) still pin the mapping for specific directions.

### 5. The sky source is a pure function of the shader description

`light_sh_model` gains `ShSkySource(const ShShaderDesc&) → skylight::SkySource`, kept pure so `make test-q3map2-shbake-model` covers it. `ShShaderDesc` gains two fields:
- `emissiveStrengthDeclared`;
- `skyFarbox`: the `skyparms` first token unless it is `-`, kept as written, so `full` is a farbox named `full`, as in the editor.

The sky stage is the first stage with `hasMap()` (which excludes rend2 stages) that isn't `$lightmap`, the editor's rule at `plugins/shaders/shaders.cpp:1940`. Its `rgbGen const` is C. q3map2's own `skyParmsImageBase` stays as it is, for the placeholder `-light`.

### 6. Candidates come from the BSP's shader list

At `-light` time q3map2 has the BSP, not the map's brushes. The candidates are the entries of `bspShaders` whose `shaderInfo_t` has `skyparms`. That list covers every shader used by a draw surface, a brush side or a fog, so brush faces and patches in every entity are included, as the editor counts them. Candidates are sorted and chosen with `skylight::SortCandidates` and `ChooseSky`.

Three differences from the editor:
- a sky shader used only on a `misc_model`'s surfaces is a candidate in q3map2 and not in the editor;
- a shader whose only faces q3map2 culled is not in q3map2's list;
- a sky shader used only by surfaces copied into a `_skybox` portal is a candidate, though the bake skips those copies (`light_sh.cpp:262`).

All three are rare, and the multi-sky warning names the set either way. They are recorded in the README.

### 7. Sky shaders have no emissive, by `skyparms` only

`ShDeriveMaterial` sets no emissive stage when `parms.skyParms` is set. A shader with `surfaceparm sky` but no `skyparms` keeps today's rule. A12 defines the sky shader by `skyparms`, and such a shader has no sky radiance either, so an additive stage with a strength is its only light: it stays an emitter.

### 8. Images and lifetime

Farbox faces are loaded as `<farbox>_rt` … `_dn`, and the cloud image by its stage map name, all through q3map2's `ImageLoad`. It searches the same extensions as the other bake images and caches for the whole process, so `SkyRadiance`'s borrowed `ImageRGBA8` views stay valid. One missing image means one warning and no environment, matching the editor's messages. As in the editor (`radiant/skyenvironment.cpp:134`), nothing is loaded unless `skylight::EffectiveStrength` is positive, so a legacy sky whose images aren't installed (the benchmark's `env/xnight2`, from pak0) warns about nothing.

### 9. Where it runs

`ShBuildScene` builds the sky after the draw surfaces and lights, and before the summary, which prints the sky lines. `scene.environment` is set once, so both bakes read the same `Scene`. Because `EvaluateEnvironment` draws no random numbers (B11), setting the environment changes no sample stream. The two bakes stay independent, so the side file of a `-shnofull` run equals that of a full run.

## Risks / Trade-offs

- **[Risk] Sky light is noisier than punctual light**, since it is reached only through cosine-sampled rays. Shaded doorways and interiors under a bright sky are noisier at the draft preset's 128 samples. → The presets already go to 2048 samples. B7 (guiding) and B10 (denoising) target this. A9 judges the stock look.
- **[Risk] The town's stock lightmap gets brighter.** Its sky was set to half the sun on an open floor (A12), so this is intended. The stock check is unaffected, and A9 judges the look.
- **[Risk] A low sky ceiling acts as an open sky.** Rays escape through it, as the stock engine draws it. This is intended (CLAUDE.md B12, "Sky surfaces stay non-casting").
- **[Trade-off] Bilinear smear at a cloud horizon, ±0.18°.** Its effect on irradiance is far below the 1% cross-check (Decision 2).
- **[Risk] A legacy map whose sky has an additive stage changes output**, because the spurious emitter is removed. → This is the bug fix. The scenario "Additive cloud layer is no emitter" pins it, and the README records it.
- **[Risk] q3map2's and the editor's candidate sets differ** in the rare cases of Decision 6. → Documented. The warning names the candidates.

## Migration Plan

- Rebuild q3map2 with `SH_BAKER=1` and rebake maps with `-shbake`. There is no format or version change.
- Maps with a positive sky strength gain sky light. Others are unchanged unless their sky has an additive stage.
- Rollback is reverting the change. Old BSPs and side files stay valid.
