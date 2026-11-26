# Design

## Context

Paths are in sh-baker (`tools/sh-baker`, pinned at `a38f699`, which is `origin/main`) unless stated.

- **Escapes.** `Trace` (`src/tracer.cpp:10-130`) returns `Zero()` when a ray hits nothing. At depth 0 it first calls `TraceConfig::on_direct_hit_sky_fn`, which `BakeSHLightMap` uses to count escapes (`src/baker.cpp:194-202`). Visibility is that count over `sample_count` (`:228-232`).
- **Sample directions.** Sensor rays are uniform over the hemisphere around `sp.normal` (`src/sensor.cpp:108-134`), or over the sphere for full-sphere points, weighted by `SampleRayInvPdf` (2π or 4π). The first ray has `tfar` 1e4 (`sensor.cpp:105`); bounce rays have 1e10.
- **Ray order per sample.** `SampleRay`, then (unless `indirect_only`) NEE at the point, which draws random numbers, then `Trace`. So the full bake and the indirect bake don't share random streams. Nothing here relies on them doing so.
- **The environment.** `Scene::environment` (`src/scene.h:89-114`) is a `Texture` (equirect `Texture32F`, Y-up, with the `intensity_multiplier` already applied to its texels at load, `src/loader.cpp:969-973`) or a `Preetham` sky. Only `ProjectEnvironmentToSH` reads it:
  - `ProjectLatLongMap` (`src/scene.cpp:136-177`) maps texel (x, y) to θ = (y + ½)π/H from +Y and φ = (x + ½)2π/W, with dir = (sin θ sin φ, cos θ, sin θ cos φ).
  - `ProjectPreethamToSH` (`:199-259`) evaluates `EvaluatePerez`, caps it at 30× the zenith value, colours it (0.2, 0.5, 0.9) × lum/zenith, and treats everything below the horizon as black. It ignores `sun_intensity`.
- **glTF scenes get an environment automatically.** The glTF loader derives a Preetham environment from the brightest directional light whenever there is no skybox (`src/loader.cpp:1019-1043`). Many CLI fixtures therefore already carry an environment that nothing reads today.
- **q3map2 never sets `Scene::environment`** (B12 will). It compiles against `baker.h` and `scene.h`.

## Goals / Non-Goals

**Goals:**
- Return sky radiance on escape in the full bake, with no new sampling strategy, and stay bit-identical when the feature is off.
- Make visibility the right weight for C6's E_sky(n) × V.
- Keep the two PRs independent, so either can merge first.

**Non-Goals:**
- Importance-sampling the environment, MIS, or putting it into the light tree. Escape sampling is unbiased; noise is B7's and B9's concern.
- A sky term in NEE. `light_sun` stays a separate directional light, and an environment with a sun disc would double-count it. That is an authoring rule for B12, not an sh-baker check.
- Counting escapes through cut-outs toward visibility, sky faces as geometry, and per-sky environments.
- Changing the CLI.

## Decisions

### 1. The opt-in is a `BakeConfig` field, not just "the environment is set"
`BakeConfig` gains `bool environment_light = false`.

The plan's B11 wording, "when `Scene::environment` is set", would change every CLI bake of a glTF scene with a directional light, because the loader already derives a Preetham environment there. The Preetham radiance has an arbitrary scale (`sun_intensity` is ignored) and no sun disc, so those fixtures would gain an uncalibrated sky. An explicit opt-in keeps "unset = bit-identical" true in practice.

Alternatives considered:
- Stop the loader from setting the environment. That changes `ProjectEnvironmentToSH` users and the saver's environment copy.
- Evaluate only `Texture` environments. That leaves a silent asymmetry.

### 2. The radiance lookup lives next to the SH projection
`Eigen::Vector3f EvaluateEnvironment(const Environment&, const Eigen::Vector3f& world_dir)` goes in `scene.{h,cpp}`.

- **Frame.** It first applies `env.to_environment` (an `Eigen::Matrix3f`, identity by default) to the direction.
- **Texture.** It inverts the projection's mapping: θ = acos(clamp(y)), φ = atan2(x, z) taken into [0, 2π), u = φ/2π·W − ½, v = θ/π·H − ½. It filters bilinearly, wrapping in u and clamping in v, and reads channels 0-2 whatever `channels` is (3 or 4).
- **Preetham.** It shares a helper with `ProjectPreethamToSH` (the capped, coloured Perez value, zero for y < 0), so the two can't drift apart.

Putting it beside the projection lets one test check them against each other: integrate the lookup into SH and compare with `ProjectEnvironmentToSH`. The lookup draws no random numbers.

### 3. The tracer gets a nullable environment pointer
`TraceConfig` gains `const Environment* environment`.

- `BakeSHLightMap` sets it to `&*scene.environment` only when the scene has an environment, `environment_light` is set, and `indirect_only` is not. Otherwise it passes `nullptr`.
- In `Trace`'s miss branch, the depth-0 callback runs as before, then the branch returns `EvaluateEnvironment(*environment, ray.direction)` or `Zero()`.
- This one branch covers the bake point's own rays (depth 0, weighted by `inv_pdf_uniform` in the baker), bounce rays and transmission rays through cut-outs. The bounce path's firefly clamp then applies to bounced sky light like any other radiance.
- Russian roulette uses throughput only, so random-number use is unchanged.
- The stale "Sky is handled by NEE" comment and the `Trace` doc's sky paragraph are corrected.
- The constructor's new parameter is defaulted, so the existing `TraceConfig` call sites in tests compile unchanged.

### 4. Visibility uses a cosine-weighted ratio estimator (PR 2)
For surface points the baker accumulates `vis_num += escaped · cosθ` and `vis_den += cosθ`, with cosθ = max(0, ray.direction · sp.normal), and stores `vis_den > 0 ? vis_num / vis_den : 0`. The callback stays `void()`: it sets a per-sample `escaped` flag that the baker reads after `Trace` returns.

- **Why a ratio estimator.** The alternative, the mean of 2cosθ·eᵢ (unbiased under uniform hemisphere sampling), can exceed 1 at finite sample counts. That breaks the side file's [0, 1] rule unless clamped, and clamping biases open points. The ratio is exactly 1 when fully open and 0 when closed, always lies in [0, 1], has lower variance, and is consistent.
- **Why not cosine-sample the rays.** That would change the SH estimator, the random streams and every result.
- **Full-sphere points** keep `escapes / sample_count`.
- **SH is unaffected.** The ray directions and random-number use don't change.

### 5. Two PRs from the same base, each with its own gate
- **PR 1** (branch `sky-radiance`) and **PR 2** (branch `cosine-visibility`) both branch from `a38f699`. Both edit the same `TraceConfig(...)` call in `baker.cpp` (PR 1 adds the environment argument, PR 2 changes the callback lambda and the accumulation around it), and both append tests at the end of `baker_test.cpp`, so whichever merges second rebases with small, mechanical conflicts in those two files.
- **PR 1's gate:** every pre-existing test unchanged, and the four CLI fixtures byte-identical to `a38f699`.
- **PR 2's gate:** SH channels byte-identical. Visibility differences are measured and reported: the mean and maximum change, plus the skylight-hole test.
- **The pinned default-bake references.** `kDefaultsFull` and `kDefaultsIndirectOnly` (`src/baker_test.cpp`) pin the default full and indirect-only bakes' bits from `046ff2e` and say never to regenerate them. Every point in both is a surface point, so PR 2 re-records only their `visibility` words, from PR 2's own commit, and states that in the comment above the tables. The SH and irradiance words stay as recorded, so the test keeps pinning everything PR 2 must not change. `sh-baker-light-model`'s "Defaults keep existing output" gets the matching exception (its delta in this change).
  - Alternative considered: exempt visibility from the test. Rejected, because it would leave the new definition unpinned.
- **Test conditions.** The adaptive stop depends on luminance, so sky radiance changes a point's sample count at the default `confidence_threshold` 0.01. Every bit-identity and analytic test in both PRs therefore sets `confidence_threshold` 0 and `firefly_clamp` 0. The analytic scenes use finite geometry well inside the first ray's `tfar` of 1e4: "infinite" floors and ceilings are squares of side 2000 centred on the point, with the point at most 20 units from their plane. The band they miss then lies within about 1.2° of the horizon: under 0.011 of a full-sphere probe's uniform fraction and about 4e-4 of a surface point's cosine-weighted visibility, both inside the 0.02 tolerances.

### 6. The landing updates the documents that define visibility
- **Visibility wording:** the plan's §Sky light "Indirect bake and runtime" bullet, CLAUDE.md's B11 text, and the two q3map2 spec deltas.
- **Format comment:** `q3map2_extension.h` only names the plane, so it needs a definition line at most.
- **Gamepack README:** its visibility lines describe the editor and C6, and change only if they quote the uniform definition.
- **q3map2 code:** no changes. It compiles against the new headers and sets no environment, so its bakes are unchanged by PR 1 and change only in visibility through PR 2 (as merged: until B12 sets `Scene::environment`).

## As merged (2026-11-26)

The user revised both PRs before merging (sh-baker `30fabe9` for #69, `297a88f` for #70). The specs describe the merged behaviour, which replaces decision 1 and parts of decisions 3, 5 and 6:

- **No opt-in (replaces decision 1).** `BakeConfig::environment_light` is gone. Sky light applies whenever `Scene::environment` is set, and the caller chooses by setting or clearing it. The CLI fixtures gain the glTF loader's derived Preetham sky, so their SH and irradiance change (the landing gate measured it; saved glTF and binaries stay byte-identical). Library bakes without an environment keep their pinned SH and irradiance.
- **Bounced sky in the indirect bake (replaces part of decision 3).** `TraceConfig` carries `indirect_only` instead of an environment pointer; `Trace` starts a path with `include_sky = !indirect_only`, transmission past a cut-out keeps the flag, and a surface reflection sets it. So an `indirect_only` bake drops direct sky light (including sky seen only through cut-outs) and keeps bounced sky light. The plan adopts this (user's decision at landing): B12 passes the environment to both bakes, the side file carries bounced sky, and `renderer_sh` adds only direct sky light, sky irradiance at the normal × visibility. A sky change needs a rebake for its bounce.
- **`EvaluateEnvironment` lives in `light.{h,cpp}`**, not `scene.{h,cpp}`; its function is as decision 2 says.
- **Visibility (decision 4) through the sensor.** The tracer reports every first ray (`on_first_ray_fn(ray, escaped)`); the baker passes the weight, cos θ for surface points and 1 for full-sphere points, to `AddVisibilitySample`, and `GetVisibility` returns the ratio, or escapes over the sample count for full-sphere points. The values match decision 4.
- **Format version.** The side file's visibility plane changes meaning but keeps version 2, by the user's decision: no v2 reader exists, and B13's v3 is the first version a reader (C4b) consumes. `q3map2_extension.h` notes the exception.
- **F.1** (sky through a cut-out needs at least 1 bounce) is resolved by qualifying the scenario.

## Risks / Trade-offs

- **[Escape-only sampling is noisy for small bright sky regions]** → The town's sky is a uniform cloud mean and the benchmark's farbox is a dim night sky. Measure at the draft preset in B12. B7 already plans guiding for sky escapes.
- **[A farbox with a sun disc plus `light_sun` double-counts]** → B12 documents the authoring rule, and the README gets a note in PR 1.
- **[The visibility ratio estimator is biased at very low sample counts]** → The bias is O(1/N) and zero at fully open or closed points. The bake's minimum is far above where it matters (draft is 128).
- **[Changing visibility changes the shipped side files]** → The format and version are unchanged, and `renderer_sh` doesn't read visibility yet (C6). Existing pk3s simply carry the old values until rebaked.
- **[Rays leaking through hull gaps return sky radiance]** → Accepted. The town compiles leak-free, and the "must cross a sky face" rule was deliberately left out.

## Migration Plan

Merge PR 1 and PR 2 in either order. Then the landing: bump the submodule to the commit containing both, re-run both gates on it, run the stock check on clean copies of the benchmark and the town, and archive. To roll back, revert the submodule bump. No data migration is needed, since side files are rebuilt by every compile.

## Open Questions

None that change the specs or the tasks. Exactly how B12 builds the equirect, including its resolution, is B12's.
