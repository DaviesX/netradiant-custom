# Proposal

## Why

The editor preview lights a map from its sky shader (A12). sh-baker can now return a sky radiance on escaping rays (B11, merged as `297a88f`). q3map2's `-shbake` doesn't connect the two yet. As a result, the stock lightmap and grid miss all sky light, and the side file misses bounced sky light. The town's sky is meant to give half its sun's light on an open floor, but today it bakes as black.

`-shbake` also has a bug. It treats a sky shader's additive cloud stage as an emitter, with the default strength of 1 even when the shader declares no `qer_pbr_emissiveStrength`. This means a stock baseq3 sky with an additive second layer counts as an unattached emitter today. This is task B12 of the plan (§Sky light, §4.3).

## What Changes

- **The sky is chosen and its radiance derived.** `-shbake` picks the map's sky shader by A12's rule: among the shaders in the BSP that declare `skyparms`, the alphabetically first, compared without case. If there is more than one, it prints one warning. It then builds the sky's radiance L(ω) with `libs/skylight.h`. q3map2's shader parser fills `skylight::SkySource`, and for that it learns three things:
  - whether `qer_pbr_emissiveStrength` was declared at all;
  - the farbox name, which follows the editor's rule (every token but `-`);
  - the sky stage.
- **sh-baker gets the radiance as texels.** `-shbake` resamples L(ω) into an equirectangular RGB float texture in sh-baker's `Environment` layout, with `to_environment` turning Z-up into sh-baker's Y-up. It sets that texture as `Scene::environment`, so both bakes see it.
  - sh-baker never gets the SH, and sh-baker doesn't change.
  - With B11's semantics, the full bake puts direct and bounced sky light into the lightmap pages, vertex colours and grid. The indirect bake puts only bounced sky light into the side file, and `renderer_sh` adds the direct sky (C6).
- **No change without sky light.** A map with no sky shader, or whose sky has no positive `qer_pbr_emissiveStrength`, gets no environment. Its output is unchanged, unless its sky has an additive stage (next item).
- **Sky shaders stop being emitters.** A shader that declares `skyparms` has no emissive stage in `-shbake`'s material rule. For maps whose sky has an additive stage, this changes the output: the spurious emitter goes away, and so does its warning.
- **New messages.** The scene summary prints:
  - the sky: its shader, its kind (farbox or cloud layer), its strength, and its irradiance at +Z from `skylight.h`;
  - the same irradiance from sh-baker's projection of the resampled texture, as a cross-check of the resampling.

  A farbox face or cloud image that can't be loaded prints one warning and the map gets no sky light, as in the editor. A sky whose irradiance cross-check is off by more than 1% of its largest irradiance over the six axis directions prints a warning. Images are loaded only for a sky with a positive strength, so a legacy sky never warns.
- **Docs.** The gamepack README's "Sky light → q3map2" section is rewritten. The plan, CLAUDE.md (B12 ticked) and the `sky-environment-light` purpose are updated.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `q3map2-shbake`:
  - New requirement: the sky environment light in both bakes (sky choice, radiance from `skylight.h`, the resampled texture, the warnings, unchanged output without sky light).
  - "Materials from shader text": sky shaders have no emissive.
  - "Bake toggles" and "Side file output": they say that the indirect bake keeps bounced sky light.
- `sky-environment-light`:
  - The README requirement drops the "until B12" caveat and documents q3map2's use instead.
  - The purpose names both bakes.

## Impact

- **Code:**
  - `tools/quake3/q3map2/`:
    - `light_sh.cpp`: building the sky and environment, logging, and the cross-check;
    - `light_sh_model.{h,cpp}`: a pure sky-source builder, the equirect resampler, and the emissive exclusion, tested in `light_sh_model_test.cpp` by `make test-q3map2-shbake-model`;
    - `shaders.cpp` and `q3map2.h` (`shaderSH_t`): the strength-declared flag, the farbox name and the sky stage.
  - `libs/skylight.h` is included as is and is not changed.
  - No sh-baker change: the submodule stays at `297a88f`.
- **Output:**
  - The town's stock lightmap, grid and vertex colours gain sky light, and its side file gains bounced sky light.
  - The benchmark and the fixtures, whose sky declares no strength and has no additive stage, are byte-identical.
  - Maps whose sky has an additive stage lose the spurious emitter.
- **Bake time:** an escaping ray now does one bilinear texture lookup. The texture is about 6 MB.
- **Docs:**
  - `setup/data/gamepacks/pbr/README.md`;
  - `docs/pbr-plan/pbr-plan.tex` and its PDF;
  - `CLAUDE.md`;
  - `content/town/README.md`, if the town's measured look changes its notes.
