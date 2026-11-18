# Tasks

The gate for each sh-baker group is the same as in B2. Every target builds. The tests give the same results as the `f4e4388` build, and the new tests pass. The CLI runs `--width 256 --samples 32 --dilation 4` on `box`, `MultiLights`, `layers` and `notangent`, and its outputs are compared with those of the `f4e4388` build. Any output difference is reported to the user before continuing. Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`).

## 1. Transmittance query (sh-baker, branch `nee-alpha-shadows`)

- [x] 1.1 Create branch `nee-alpha-shadows` from sh-baker `main` (`f4e4388`), and build a baseline of `f4e4388` for comparison. Verify by building both, then recording the baseline test results and its CLI outputs for the four fixtures.
- [x] 1.2 Add `float t` to `Occlusion` and fill it in `FindOcclusion`. Add `FindTransmittance(RTCScene, const std::vector<Material>&, const Ray&)` to `occlusion.{h,cpp}`, following design decision 1: the ordered walk, the `tnear` step of 0.005, a return of 0 on a material-less or `alpha >= 1` hit, and the 64-crossing limit. Document it in the header. Verify that new `occlusion_test.cpp` tests pass, covering:
  - a miss returning 1;
  - one 128/255 quad;
  - two quads multiplying;
  - an alpha-0 quad;
  - RGB and RGBA-255 quads returning 0;
  - a material-less box behind an alpha-0 quad;
  - a quad beyond `tfar` being ignored;
  - 100 alpha-0 quads returning 0;
  - `t` on an existing hit test.
  Note 2026-11-17: merged as 77f1b72 with `FindTransmittance` and `kMaxTransmittanceCrossings` moved to `light_internal` in `light.{h,cpp}`. `occlusion.{h,cpp}` instead gained `OcclusionWithAlpha`, a `FindOcclusion(scene, materials, ray)` overload, `ContinuePastOcclusion` and `NewPastOcclusion`. The walk steps `tnear` to `std::nextafter(hit.t)` rather than adding 0.005. The walk tests are `TransmittanceTest` in `light_test.cpp`, with an added case for an opaque surface closely behind a cut-out. `occlusion_test.cpp` gains only a separate hit-distance test, `ReportsHitDistance`.

## 2. NEE uses transmittance

- [x] 2.1 Add `const std::vector<Material>& materials` to `EvaluateLightSamples` and `AccumulateIncomingLightSamples` (`light.{h,cpp}`), replace both `FindOcclusion` gates with the transmittance scale, and pass the materials from `tracer.cpp` and `baker.cpp`. Update the existing `light_test.cpp` calls by changing only their argument lists. Verify that everything builds and the existing tests give the same results.
- [x] 2.2 Add `light_test.cpp` tests for both NEE functions with a point light. Cover a 128/255 quad (ratio within 1%), two quads (squared), an alpha-0 quad (unchanged), an opaque RGBA quad (zero) and a quad beyond the light (unchanged). Add a spot or directional case so more than one light type is covered. Verify that they pass, and that each attenuation test fails on the `f4e4388` behaviour, by checking with the old gate.
- [x] 2.3 Add a `baker_test.cpp` test for the cut-out fence: a floor lit by a directional light through a striped RGBA fence, baked over caller-supplied points with no bounces, where points behind the open stripes are lit and points behind the opaque stripes are dark. Add a single-point bake behind a 128/255 quad, checking that L0 is about `1 - 128/255` of the unoccluded value. Verify that both pass.
- [x] 2.4 Update the sh-baker README section on lighting or shadows: shadow rays pass through albedo alpha, and material-less occluders block. Verify by reading the rendered section.

## 3. Gate and PR

- [x] 3.1 Run the gate. Every target builds. The tests give the same results as the baseline, and the new tests pass. `box` and `MultiLights` are byte-identical to the baseline. For `layers` and `notangent`, compare with the baseline and report any difference, with its size and where it is, to the user before continuing. `loader_q3map2.h` and `baker.h` still compile under `-fno-exceptions -fno-rtti` with no tinygltf, JSON or Embree header in their `-M` closure. Verify with the recorded gate output.
- [x] 3.2 Run `clang-format -i` on every touched C++ file, commit, push the branch, and open the PR to `github.com/DaviesX/sh-baker`. The description must state the output change and the gate results. Verify that the PR URL exists and `git diff --check` is clean.
  Done 2026-11-12: sh-baker PR #62 (branch `nee-alpha-shadows`, commit 276d346). After review, added an area-light case, a point light in the bake-point test, and closed material-less boxes for the hull scenarios.
  Note 2026-11-17: the user adjusted the PR and merged it as 77f1b72 (base f4e4388). The tracer now uses the `FindOcclusion` overload and `NewPastOcclusion`, and `tracer_test.cpp` gained `TraceThroughTransparentSurface`. The gate in 3.1 ran on 276d346. Task 4.1 re-runs it on the merge commit.

## 4. Landing (netradiant, after the user merges)

- [x] 4.1 On a branch from `fork/master`, bump `tools/sh-baker` to the merge commit `77f1b72` and re-run the gate on it. Tick `[x] **B2b**` in `CLAUDE.md`, add `\emph{Done (\texttt{sh-baker} PR \#62).}` to plan §4.2 item 2, and rebuild the PDF with `latexmk -pdf`. Verify that the PDF builds without errors and the submodule points at the merge commit.
  Done 2026-11-17: landed together with B3 on netradiant branch `b3-progress-cancel` (not a separate branch from `fork/master`), so the submodule moved straight to `ce5419a`, which contains `77f1b72`. Gate on `ce5419a` against a fresh `f4e4388` build: 162/162 tests (133 baseline tests unchanged, none removed, 29 new); `box`, `MultiLights`, `layers` and `notangent` byte-identical; header checks pass. B2b ticked in `CLAUDE.md`; plan §4.2 item 2 marked done; PDF rebuilt (20 pages, no errors).
- [x] 4.2 Archive the change, which creates `openspec/specs/sh-baker-shadow-rays/spec.md`. Verify that `openspec validate --specs --strict` passes.
  Done 2026-11-17: archived as `2026-11-17-sh-baker-nee-alpha-shadows`, creating `openspec/specs/sh-baker-shadow-rays/spec.md`; `openspec validate --specs --strict` passes (12 items, info notes only).
- [x] 4.3 Run the stock check on a clean copy of the benchmark, push the branch to `fork`, and open the landing PR with `--repo DaviesX/netradiant-custom`. Verify that the stock check prints PASS for compile, opengl1 and opengl2, and that the PR URL exists.
  Done 2026-11-17: stock check on a clean `git archive` copy of the benchmark: compile, opengl1 and opengl2 PASS (`stockcheck-out/20261117-195904`). Combined landing PR DaviesX/netradiant-custom#4 from `b3-progress-cancel`, based on `b2-land` while #3 is open.
