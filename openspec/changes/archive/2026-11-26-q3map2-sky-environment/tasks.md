# Tasks

The work is on branch `q3map2-sky-environment`, from `fork/master` (`90cb7c76`, with sh-baker at `297a88f`).

- **Builds.** q3map2 is built with `SH_BAKER=1`.
- **Unexpected output.** Report any output difference that a group doesn't expect to the user before continuing.
- **Stock check.** Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`). Run it once per group that changes q3map2's output.
- **Pushing.** Ask before pushing any branch or opening any PR.

## 1. Baseline

- [x] 1.1 Build the pre-change q3map2 (`SH_BAKER=1`) from `90cb7c76` in a worktree in the scratchpad. Bake single-threaded with `-light -shbake`, keeping the BSPs, side files and logs as the baseline:
  - the benchmark;
  - the fixtures map;
  - the town.

  Also bake the town with `-shnofull`. Verify that every output exists and that two runs of the benchmark are byte-identical.

  Done. The baseline is the q3map2 built from the unchanged tree (code identical to `90cb7c76`), kept in the scratchpad with a bake script (`bake.sh`, single-threaded `-meta -keeplights`, `-vis -saveprt`, `-light -shbake -threads 1`). Two benchmark runs are byte-identical; the town bakes in about 5 s.

## 2. Pure rules (`light_sh_model`, `make test-q3map2-shbake-model`)

- [x] 2.1 Add to `ShShaderDesc` the fields `emissiveStrengthDeclared` and `skyFarbox` (the `skyparms` first token unless it is `-`, kept as written). Add `ShSkySource( const ShShaderDesc& ) → skylight::SkySource`, which takes the sky stage as the first stage with `hasMap()` that isn't `$lightmap`, and its `rgbGen const` as C (design decision 5). Document both in the header's rule list. Verify with new test cases:
  - a farbox name;
  - `-` gives a cloud layer;
  - `full` is kept as a farbox;
  - a rend2 first stage is skipped;
  - `$lightmap` is skipped;
  - `rgbGen const` and its absence;
  - no strength, strength 0, and a declared positive strength, against `skylight::EffectiveStrength`.
- [x] 2.2 Make `ShDeriveMaterial` give a shader with `skyparms` no emissive stage, and leave `surfaceparm sky` without `skyparms` as it is (design decision 7). Update rule 2's "Emissive" text in `light_sh_model.h`. Verify with test cases:
  - a `skyparms` shader with an additive stage and a strength doesn't emit;
  - the same stages under `surfaceparm sky` alone still emit;
  - every existing emissive case passes.
- [x] 2.3 Add the pure equirect resampler (design decisions 2 and 3). It takes a `skylight::SkyRadiance` and returns 1024 × 512 × 3 floats:
  - in sh-baker's texel layout (row 0 at the zenith, `(y * W + x) * 3`, θ and φ at texel centres);
  - each texel the sin θ-weighted mean of a 4 × 4 sub-grid;
  - together with the `to_environment` rows (0 1 0; 0 0 1; 1 0 0) as a constant.

  Verify with test cases:
  - Mapping: the texels nearest world +X, +Y, −Y and the zenith hold the farbox face drawn there.
  - Cloud layer: every row above the horizon is exactly ℒ and every row below it 0.
  - Irradiance: the texture integrated with its own texel solid angles gives an irradiance within 0.5% of the largest of the six irradiances of `skylight::Irradiance(ProjectRadiance(…))` at ±X, ±Y and ±Z. Check this for a uniform farbox, an up-only farbox, an `_rt`-only farbox and a cloud layer.
- [x] 2.4 Run `make test-q3map2-shbake-model` and `make test-sky-light`. Verify that both exit 0. (The stock check is not run in this group: q3map2's output doesn't change here.)

## 3. Shader parser and bake glue

- [x] 3.1 In `shaders.cpp` and `q3map2.h` (`shaderSH_t`), record:
  - `pbrEmissiveStrengthSet` when `qer_pbr_emissiveStrength` parses;
  - `skyFarbox` from `skyparms` by the editor's rule;

  and pass both through `ShShaderDescFor`. Leave `skyParmsImageBase` and the placeholder `-light` untouched. Verify that the placeholder `-light -patchshadows` BSP of the benchmark is byte-identical to the baseline's, apart from the version stamp.
- [x] 3.2 In `light_sh.cpp` `ShBuildScene`:
  - Collect the candidates from `bspShaders` whose shader info has `skyparms`, and choose with `skylight::SortCandidates` and `ChooseSky`. With two or more candidates, print one warning naming them and the chosen one.
  - Only when `skylight::EffectiveStrength` is positive, load the farbox faces or the cloud image with `ImageLoad`. A failed image prints one warning and gives no sky light. A sky without a positive strength loads nothing and warns about nothing.
  - Build the `SkyRadiance`, resample it, and set `scene.environment` (`Type::Texture`, 3 channels, `to_environment`).
  - Leave the environment unset when there is no sky light.

  Verify that a benchmark bake runs and that its log has no sky line.
- [x] 3.3 Add the cross-check (design decision 4):
  - project the texture with `sh_baker::ProjectEnvironmentToSH` and store it in `sh_coeffs`;
  - evaluate the irradiance at R·n for ±X, ±Y and ±Z, and compare with the derivation;
  - print the sky summary line (shader, kind, strength, E(+Z) from the derivation and from the texture);
  - warn when any value is off by more than 1% of the largest of the six derivation irradiances.

  Verify that the town's summary gives (1.518, 2.499, 2.006) within 0.1% from the derivation and within 1% from the texture, with no warning.
  Done. Town: `sky textures/town/sky (cloud layer, strength 14.6): irradiance at +Z 1.5184 2.4989 2.0060; sampled 1024 x 512: 1.5183 2.4994 2.0063 (0.023% off at most)`, no warning.
- [x] 3.4 Run the regression checks against the baseline. Verify:
  - the benchmark's and the fixtures' `-shbake` BSPs and side files are byte-identical, apart from the version stamp ("Legacy sky unchanged");
  - the town's `-shnofull` side file equals the town's full-run side file ("No full bake with sky light");
  - the town's outputs differ from the baseline.

  Report the size of the town's change: the mean lightmap byte change on outdoor and indoor luxels.
  Done. Benchmark, fixtures and fixtures_lit: BSPs (version stamp masked) and side files byte-identical to the baseline; the town's `-shnofull` side file equals its full-run side file, and its `-shnofull` BSP the baseline's. Town lightmap, by side-file visibility: > 0.5, 2374 luxels, mean byte 25.9 → 35.4; (0, 0.5], 23354 luxels, 15.1 → 24.7; 0, 7241 luxels, 9.0 → 9.4. On the 66 luxels with visibility above 0.9 the side file's L0-implied irradiance is 0.4% of the full bake's (2.37 → 4.68 in green), so no direct sky reaches the side file.
- [x] 3.5 Build scratch maps with a generator script in the scratchpad (as B11's t9 maps were built), bake them, and check each scenario of the `q3map2-shbake` delta with a checker script:
  - open floor under a cloud sky (lightmap bytes within 1 of the encoding of π s M, side-file L0 exactly 0);
  - bounced sky light beside a white block (L0 above 0 with the strength, exactly 0 without);
  - the overhang underside above a black floor (bytes 0 0 0);
  - farbox orientation with an `_rt`-only farbox (the +X wall above the −X wall);
  - a legacy sky with a missing farbox and no strength (no warning about it);
  - a missing `_dn` face (one warning, output equal to the no-strength map);
  - two skies (one warning naming both, `textures/a/sky` used);
  - an additive cloud layer without a strength (no emitter warning, output equal to the map without that stage);
  - a `_dn`-only farbox (no cross-check warning).

  Verify that the checker prints PASS for each.
  Done. `sc/gen.py` and `sc/check.py` in the scratchpad: 9 of 9 PASS. The open-floor scenario is baked at `-shsamples 2048`: at the default 128 samples the bytes scatter 35–40 around an unbiased mean (37.52 against the unrounded 37.54), so the spec scenario now names the sample count and adds a check of the mean (37.59 at 2048, every luxel 37 or 38). Others: floor L0 max 0.39 with the strength, 0 without; underside 289 luxels all 0; +X wall mean byte 27.1, −X 0.0; one warning naming `env/nodn_dn` and output equal to the no-strength map; no warning for the legacy farbox; one warning naming both skies, `textures/a/sky` used; the additive cloud layer draws no emitter warning and equals the map without the stage; the `_dn`-only farbox is 0.013% off.
- [x] 3.6 Update the documentation:
  - Rewrite the gamepack README's "Sky light → q3map2" section per the modified `sky-environment-light` README requirement: the choice from the BSP and the three differences from the editor, the sampled texels, the two bakes' contents, no sky emitters, the summary line and its cross-check, and that a sky change needs a rebake. Drop the B12 caveat in it and its mentions at the other "until B12" places (around lines 343, 446 and 818).
  - Check `q3map2_extension.h`'s contents note still holds.

  Verify that `grep -n "B12" setup/data/gamepacks/pbr/README.md` shows no future-tense caveat.
- [x] 3.7 Run the stock check on clean copies of the benchmark and the town. Verify that it prints PASS for compile, opengl1 and opengl2 on both. Look at the sky-lit town in opengl1, and record a note and screenshots of the change for A9 in `content/town/README.md`, if its notes on lighting change.

  Done. `tools/stockcheck/stockcheck.py` PASS (compile, opengl1, opengl2) on the benchmark and the town. The baseline and new opengl1 views of the town compared: facades in the sun's shade read as brick under the sky in `crossroads` and `street`, `sw-room` unchanged away from its doorway; noted in `content/town/README.md` (no screenshots committed).

## 4. Plan, CLAUDE.md and landing

- [x] 4.1 Update `docs/pbr-plan/pbr-plan.tex`: B12 done in §4.2, plus §Sky light and §4.3 with the resampling, the cross-check and the measured town change. Rebuild the PDF with `latexmk -pdf`. In `CLAUDE.md`, tick B12 with its merged semantics, and update A12's and the sky light's "until B12" wording. Verify that latexmk reports no errors, and that `grep -n "B12" CLAUDE.md` reads as done.
- [x] 4.2 Run `git clang-format` on the changed lines of the existing C++ files and `clang-format -i` on new files, then `openspec validate q3map2-sky-environment`. Verify that `git diff --check` is clean and validation passes.
  Done. `git clang-format` on the staged q3map2 changes; rebuilt, tests pass, and the town and benchmark bakes are unchanged by the formatting. `git diff --check` clean, `openspec validate` passes. The build's 176 `offsetof` warnings predate the change (same count before it).
- [x] 4.3 Commit. After the user agrees, push to `fork` and open the PR to `DaviesX/netradiant-custom`, with a description giving the semantics, the regression and scenario results, and the stock check. Verify that the PR URL exists, and record it here.
  Done. Commit `f8e738cc`; PR: https://github.com/DaviesX/netradiant-custom/pull/16 (notes that the scenario and regression scripts stayed in the scratchpad).
- [x] 4.4 After the merge, archive the change. At that point, update the `sky-environment-light` main spec's purpose to name both bakes. Verify that `openspec validate --specs` passes.
  Done (2026-11-26, after PR #16 merged as `74783a24`). Deltas synced into `openspec/specs/q3map2-shbake` (one requirement added, three modified) and `openspec/specs/sky-environment-light` (README requirement modified, purpose now names both bakes); `openspec validate --specs` passes.
