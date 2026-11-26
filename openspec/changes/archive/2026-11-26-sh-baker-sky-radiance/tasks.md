# Tasks

> **As merged (2026-11-26).** Tasks 1.2-1.5 shipped as the user revised them in #69 (design.md "As merged"): no `environment_light` opt-in, `EvaluateEnvironment` in `light.{h,cpp}`, `TraceConfig` carrying `indirect_only` instead of an environment pointer, and the indirect bake keeping bounced sky light, so the planned "indirect bake ignores the environment" test became `BouncedSkyLight`'s and `SkyThroughCutOut`'s indirect halves. The task text below is the plan as reviewed.

Both sh-baker groups share one gate:
- every target builds;
- every pre-existing test gives the same result as the `a38f699` build, and the new tests pass;
- the CLI runs `--width 256 --samples 32 --dilation 4` on `box`, `MultiLights`, `layers` and `notangent`, and its outputs are compared with those of the `a38f699` build;
- `loader_q3map2.h` and `baker.h` still compile under `-fno-exceptions -fno-rtti`, with no tinygltf, JSON or Embree header in their `-M` closure.

Any output difference the group doesn't expect is reported to the user before continuing. Ask before pushing any branch or opening any PR. Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`), run once per group on the netradiant side.

## 1. Sky radiance on escape (sh-baker PR 1, branch `sky-radiance`)

- [x] 1.1 Create branch `sky-radiance` from sh-baker `main` (`a38f699`), and build a baseline of `a38f699`: record its test results and its CLI outputs for the four fixtures. Verify both builds succeed and the baseline artifacts exist in the scratchpad.
- [x] 1.2 Add `Eigen::Matrix3f to_environment` (identity) to `Environment` and `EvaluateEnvironment(env, world_dir)` to `scene.{h,cpp}`, following design decision 2. Factor the capped, coloured Perez value out of `ProjectPreethamToSH` into a helper both use, keeping `ProjectPreethamToSH`'s arithmetic bit-identical. Verify with new `scene_test.cpp` tests:
  - texture top row and bottom row at ±Y;
  - longitude wrap at φ = 0 and 2π;
  - bilinear midpoint between two texels;
  - 4-channel texture;
  - Preetham zero below the horizon;
  - the lookup integrated to SH matching `ProjectEnvironmentToSH` within 1% of L0, for a texture and for a Preetham environment;
  - a +Z→+Y frame;
  - the existing projection tests unchanged.
- [x] 1.3 Add `const Environment* environment` (default `nullptr`) to `TraceConfig`, return `EvaluateEnvironment` in `Trace`'s miss branch after the depth-0 callback, and correct the stale "Sky is handled by NEE" comment and the sky paragraph of `Trace`'s doc. Verify with new `tracer_test.cpp` tests: an escaped ray with an environment returns its radiance, one without returns 0, and a ray through an alpha-0 quad returns the radiance. Existing tracer tests stay unchanged.
- [x] 1.4 Add `bool environment_light = false` to `BakeConfig` (documented in `baker.h`), and pass the environment pointer from `BakeSHLightMap` only when the scene has an environment, the opt-in is set and the bake isn't `indirect_only`. Verify with new `baker_test.cpp` tests for the spec scenarios:
  - an open floor under a uniform sky (L0 √π·L, irradiance π·L);
  - bounced sky light (√π·0.5·L, and exactly 0 with the opt-in off);
  - sky through a cut-out;
  - the indirect bake ignoring the environment (bit-identical with and without it);
  - a Z-up scene;
  - visibility bit-identical with the opt-in on and off.
  Every one of these tests sets `confidence_threshold` 0 and `firefly_clamp` 0, and builds its floors and ceilings as squares of side 2000 centred on the point, with the point at most 20 units from their plane (design decision 5).
- [x] 1.5 Update the sh-baker README:
  - escaped rays return environment radiance in the full bake when `BakeConfig::environment_light` is set, never with `indirect_only`;
  - the environment frame;
  - an environment must not contain a sun that a directional light already provides.
  Verify by reading the rendered section.
- [x] 1.6 Run the gate. Every pre-existing test passes unchanged, and all four fixture outputs are byte-identical to the baseline. Verify with the recorded gate output.
- [x] 1.7 Run `clang-format -i` on every touched C++ file and commit. After the user agrees, push and open the PR to `github.com/DaviesX/sh-baker`, with a description stating that output is unchanged unless the opt-in is set, plus the gate results. Verify that `git diff --check` is clean and the PR URL exists. PR: https://github.com/DaviesX/sh-baker/pull/69

## 2. Cosine-weighted visibility (sh-baker PR 2, branch `cosine-visibility`)

- [x] 2.1 Create branch `cosine-visibility` from `a38f699`, independent of group 1. Verify it builds.
- [x] 2.2 In `BakeSHLightMap`, replace the escape count for surface points with the ratio estimator of design decision 4: a per-sample `escaped` flag set by the callback, with `vis_num`/`vis_den` weighted by max(0, dir·normal). Full-sphere points keep `escapes / sample_count`, and a zero denominator gives 0. Update the `baker.h` documentation of `environment_visibility_texture`. Verify with new `baker_test.cpp` tests:
  - the skylight hole: 0.75 ± 0.02 at 4096 samples, where the uniform fraction would be 0.5;
  - fully open gives exactly 1 and a closed box exactly 0;
  - a full-sphere probe above a floor gives 0.5 ± 0.02;
  - SH bit-identical to the `a38f699` build for the same scene.
  These tests set `confidence_threshold` 0 and `firefly_clamp` 0 and use the finite geometry of design decision 5.
- [x] 2.3 Re-record only the surface points' `visibility` words of `kDefaultsFull` and `kDefaultsIndirectOnly` (`src/baker_test.cpp:860-990`; every point in both is a surface point) from this branch's Release build, keeping their SH and irradiance words as recorded at `046ff2e`. Update the comment above the table to name the commit the visibility words come from and why. Update any other existing test that asserts a uniform surface-point visibility value (`IndirectProbeIsSumOfHemispheres` drops its visibility identity, probe = mean of the two hemispheres, which no longer holds). List each changed assertion in the PR description. Verify that the suite passes, that no SH or irradiance assertion or reference word changed (`git diff` of `baker_test.cpp` touches only visibility words and comments), and that full-sphere visibility assertions are unchanged.
- [x] 2.4 Update the sh-baker README's description of environment visibility: cosine-weighted for surface points, uniform for full-sphere points, alpha cut-outs block. Verify by reading the rendered section.
- [x] 2.5 Run the gate. SH channels are byte-identical to the baseline, while visibility channels differ. Report the mean and maximum visibility change per fixture to the user. Verify with the recorded gate output.
- [x] 2.6 Run `clang-format -i` on every touched C++ file and commit. After the user agrees, push and open the PR, with a description stating the visibility value change, that SH is unchanged, and the gate results. Verify that `git diff --check` is clean and the PR URL exists. PR: https://github.com/DaviesX/sh-baker/pull/70

## Follow-ups

- [x] F.1 Sky light through an alpha cut-out needs at least one bounce: a ray passing a cut-out is traced at depth + 1, so with `bounces 0` it returns nothing (pre-existing, it applies to all indirect light). The "Sky through a cut-out" scenario in `specs/sh-baker-environment-light/spec.md` reads as unconditional, and `BakerSkyTest.SkyThroughCutOut` uses 1 bounce. Either qualify the scenario ("with at least 1 bounce") before archiving, or make transmission keep the depth in sh-baker (a separate PR that changes output). Not blocking the PRs. Resolved 2026-11-26: the user chose to qualify the scenario ("with at least 1 bounce"); no sh-baker change.

## 3. Landing (netradiant, after the user merges both PRs)

- [x] 3.1 On a branch from `fork/master`, bump `tools/sh-baker` to the merge commit containing both PRs. Rebuild q3map2 with `SH_BAKER=1` and re-run both gates on that commit. Verify the submodule points at the merge commit and q3map2 builds.
  Done 2026-11-26 on branch `sh-baker-sky-radiance` (from `master`, which is `fork/master` plus the A14 commit `4c8b68a0`). `tools/sh-baker` at `297a88f` (#70's squash on #69's `30fabe9`). Gate, Release with `SH_BAKER_BIT_EXACT_TESTS=ON`, against an `a38f699` build: every target builds; baseline 202 tests pass, merge 221 pass, no baseline test missing or failing (19 new); no-RTTI caller test passes; `loader_q3map2.h` and `baker.h` compile under `-fno-exceptions -fno-rtti` with 0 tinygltf/JSON/Embree headers in their closure. CLI fixtures: saved glTF, `.bin` and layer PNGs byte-identical; every SH and irradiance channel differs in all four, because the merged #69 has no opt-in and the glTF loader's derived Preetham sky now lights them (R:G:B about 0.2:0.5:0.9, the Preetham colour), which the user accepted ("As merged" in design.md); visibility differs as #70 intends (mean |d| 0.0000/0.0338/0.0083/0.0066, max 0.031/0.267/0.282/0.194 for box/MultiLights/layers/notangent). q3map2 builds with `SH_BAKER=1` against the rebuilt `libsh_baker.so`.
- [x] 3.2 Update `tools/quake3/q3map2/q3map2_extension.h`'s format comment to define the visibility plane as cosine-weighted, and re-run `make test-q3map2-extension`. Re-run the "Exact sample count" check from the modified `q3map2-shbake` spec: bake the scratch map with `-shsamples 128` and `-shsamples 64` single-threaded, and confirm some partly shaded luxels differ in visibility. Verify the test passes and the check's output is recorded in this task.
  Done. Format comment defines visibility as cosine-weighted, notes the version-2 exception (user's decision) and that the indirect bake keeps bounced sky light when the scene has an environment. `make test-q3map2-extension`: 623 checks, 0 failed. Exact sample count, regenerated scratch map (black floor in a sky shell, black wall, one `light` 300, single-threaded): `-shsamples 128` and `64` both log "exactly N samples per point"; of 16384 luxels, 5388 are partly shaded at 128 and 5327 of them differ in visibility at 64 (max |d| 0.178); visibility range [0, 1].
- [x] 3.3 Update the documents:
  - tick `[x] **B11**` in `CLAUDE.md`, with its semantics (as merged: no opt-in) in its text;
  - change the B13/C6 wording that says "escape fraction" to cosine-weighted visibility (probes stay uniform);
  - change plan §Sky light's "Indirect bake and runtime" bullet the same way, and mark §4.2's B11 item done;
  - change the gamepack README wherever it defines visibility.
  Rebuild the PDF with `latexmk -pdf`. Verify the PDF builds without errors and `grep -n "escape fraction\|first rays that escape"` finds no stale definition.
  Done, for the merged model (user's decision: bounced sky baked). CLAUDE.md: B11 ticked with the merged semantics, B12 passes the environment to both bakes, C6 adds only direct sky, B13/C6 visibility wording, A12's reuse note. Plan: §4.2 item 9 done, its paragraph after the list, §Sky light's heading and "Indirect bake and runtime" and "Full bake" bullets, the C6 sky bullet, the revision-8 summary and the task table. Gamepack README: B12 for both bakes (it defines visibility nowhere). `latexmk -pdf` builds with no errors; the grep finds only the new cosine-weighted definition.
- [x] 3.4 Run the stock check on clean copies of the benchmark and the town. Verify it prints PASS for compile, opengl1 and opengl2.
  Done. `tools/stockcheck/stockcheck.py content/benchmark bench` and `content/town town1` (each compiles from a fresh copy): compile, opengl1 and opengl2 PASS for both.
- [x] 3.5 Archive the change, which creates `openspec/specs/sh-baker-environment-light/spec.md` and applies the two q3map2 deltas. Verify that `openspec validate --specs --strict` passes.
  Done 2026-11-26 with `openspec archive`; `openspec validate --specs --strict` passes.
- [ ] 3.6 After the user agrees, push the branch to `fork` and open the landing PR with `--repo DaviesX/netradiant-custom`. Verify that the PR URL exists.
