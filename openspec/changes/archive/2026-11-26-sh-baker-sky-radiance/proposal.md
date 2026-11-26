# Proposal: Sky radiance in sh-baker's point bake (B11), and cosine-weighted sky visibility

## Why

The full bake has to carry sky light into the stock lightmap, vertex colours and light grid (plan §Sky light). But sh-baker's point bake reads no environment: a ray that escapes returns zero at every depth (`src/tracer.cpp:20-28`), and the comment there ("Sky is handled by NEE") is out of date, because NEE has no environment term. B12 (q3map2 passes the sky shader's environment to the full bake) can't start until sh-baker returns radiance on escape.

The indirect bake's sky visibility has its own problem. Today it is the uniform fraction of a point's first rays that escape (`src/baker.cpp:194-232`). `renderer_sh` (C6) multiplies the sky's irradiance at the normal by this value, and the right weight for irradiance is cosine-weighted. A uniform fraction overstates the sky wherever the open sky is off to the side of the normal: a wall beside an opening, or a floor under an overhang. That makes the enhanced target brighter than the full bake (stock) in exactly those places.

## What Changes

> **As merged (2026-11-26).** The user revised PR 1 before merging it: there is no opt-in, so any scene with an environment gets sky light, and an `indirect_only` bake keeps *bounced* sky light (sky reached after at least one surface reflection) and drops only direct sky. The plan adopts this: the indirect bake carries bounced sky, and `renderer_sh` adds only the direct sky term. The CLI fixtures therefore gain the loader's derived Preetham sky. The bullets below are the proposal as reviewed; design.md's "As merged" section and the specs describe what shipped.


The change is delivered as two independent sh-baker PRs to `github.com/DaviesX/sh-baker`, each reviewed and merged by the user, plus one netradiant landing.

- **PR 1, sky radiance (B11).**
  - A ray that escapes the scene returns the environment's radiance in its direction. This applies at the baked point (depth 0), after any number of bounces, and to rays that pass through alpha cut-outs.
  - It happens only when the scene has an environment, the bake is not `indirect_only`, and a new `BakeConfig` opt-in is set. An `indirect_only` bake never returns sky radiance at any depth.
  - The radiance is the same function the environment's existing SH projection integrates: the equirect texel (bilinear) for a texture environment, and the existing Preetham formula for a Preetham environment.
  - The environment gains a world-to-environment frame, defaulting to identity, so q3map2's Z-up scenes can use the Y-up equirect convention.
  - Visibility recording is unchanged in this PR.
  - With the opt-in off, which is the default, every bake is bit-identical to the commit before. The lookup draws no random numbers.
  - The CLI sets no new field, so its output is unchanged. This includes glTF scenes whose loader derives a Preetham environment from a directional light; without the opt-in they would suddenly gain a sky.
- **PR 2, cosine-weighted visibility.**
  - For a surface point, environment visibility becomes cosine-weighted: Σ escapeᵢ·cos θᵢ / Σ cos θᵢ over the point's first rays, with θ measured from the point's normal. The value stays in [0, 1].
  - Full-sphere points (grid probes) keep the uniform fraction, because they have no normal.
  - SH results stay bit-identical. The pinned default-bake test (`kDefaultsFull`, `kDefaultsIndirectOnly`) re-records only its visibility words. **BREAKING (values):** the surface points' visibility changes in every bake, including the side file's visibility plane and the CLI's visibility channel. The format does not change.
- **Landing (netradiant), after both merges.**
  - Bump `tools/sh-baker`, tick B11, and update the visibility wording in `CLAUDE.md` and plan §Sky light (with a PDF rebuild), and in the q3map2 specs below.
  - q3map2 code is unchanged: it doesn't set the opt-in yet; B12 does. The stock check passes.

**Not in this change:**
- Visibility of rays escaping through alpha cut-outs, which still count as blocked.
- Sky faces as geometry in sh-baker, the "a ray must cross a sky face" rule, and per-sky environments.
- A `_skyshader` override.
- Environment importance sampling or MIS.
- Choosing the sky. That stays with the existing rule in `libs/skylight.h` (`ChooseSky`, the alphabetically first `skyparms` shader), which the editor uses today and q3map2 will include in B12. B11 only consumes whatever environment the caller passes.
- B12, B13 and C6.

## Capabilities

### New Capabilities
- `sh-baker-environment-light`: how sh-baker's point bake uses a scene environment. Covers radiance returned on escaped rays and when it applies (opt-in, `indirect_only`), the radiance lookup and its frame, bit-identity with the opt-in off, and how environment visibility is defined for surface and full-sphere points.

### Modified Capabilities
- `q3map2-sh-extension`: the visibility each page luxel stores is redefined from the uniform fraction of first rays that reach the sky to the cosine-weighted fraction.
- `sh-baker-light-model`: "Defaults keep existing output" gains one exception: surface points' environment visibility, which PR 2 redefines. The pinned default-bake references re-record only those words.
- `q3map2-shbake`: the "Exact sample count" scenario relied on visibility being a multiple of 1/N. It is restated with an observable that doesn't depend on how visibility is weighted.

## Impact

- **sh-baker code:**
  - PR 1: `src/scene.{h,cpp}` (environment frame and radiance lookup), `src/tracer.{h,cpp}` (escape branch, environment pointer in `TraceConfig`, corrected comments), `src/baker.{h,cpp}` (the opt-in in `BakeConfig`).
  - PR 2: `src/baker.cpp` (visibility estimator) and the `baker.h` documentation.
  - Tests in `scene_test.cpp`, `tracer_test.cpp` and `baker_test.cpp`; README sections on the environment and visibility.
- **sh-baker headers q3map2 includes:** `baker.h` gains one `BakeConfig` field, and `scene.h`'s `Environment` gains a frame. q3map2 must be rebuilt against them (as with every bump). The headers keep compiling under `-fno-exceptions -fno-rtti`.
- **Bake output:**
  - PR 1: unchanged unless the opt-in is set.
  - PR 2: surface-point visibility changes everywhere, while SH, irradiance and full-sphere visibility stay bit-identical.
- **Downstream:** C6's E_sky(n) × V becomes exact for a uniform sky seen through any opening. B13's probe visibility stays uniform, as specified here. `renderer_sh` needs no format change.
- **netradiant (landing):**
  - The submodule bump.
  - `CLAUDE.md` B11 (ticked) and the B13/C6 visibility wording.
  - Plan §Sky light and §4.2, with a PDF rebuild.
  - `q3map2_extension.h`'s format comment, if it defines visibility.
  - The gamepack README wherever it describes visibility.
  - The stock check on the benchmark and the town.
