# Proposal: Alpha in sh-baker's shadow rays (B2b)

## Why

sh-baker's direct-light term (next-event estimation, NEE) tests visibility with `FindOcclusion` and drops the light sample on any hit (`src/light.cpp:265` and `:339`). So a fence or a leaf card blocks direct light completely, as if its cut-outs were solid. The tracer's indirect rays already see through alpha (`src/tracer.cpp:38-58`), which leaves the two terms inconsistent. The town's fences and trees need shadows with holes in them, and B5 (q3map2 `-shbake`) depends on this. B2 left the fix out deliberately because it changes bake output (plan §4.2 item 2).

## What Changes

- Shadow rays honour alpha. A new visibility query walks a shadow ray from its origin to its `tfar`, hit by hit in order of distance, and returns a transmittance between 0 and 1: the product of `1 - alpha` over the hits. Alpha is `GetAlpha(material, uv)`, the same coverage the tracer reads. The walk stops at zero transmittance as soon as it hits a fully opaque surface or a material-less occluder.
- Both NEE functions, `EvaluateLightSamples` (used by the tracer at each bounce) and `AccumulateIncomingLightSamples` (used by the baker at each bake point), scale a light sample's radiance by that transmittance. A sample whose transmittance is zero is skipped, as an occluded sample is today. Both functions gain a `const std::vector<Material>& materials` parameter right after `rtc_scene`, so they can look up materials. These are internal APIs: `light.h` is not among the headers q3map2 includes.
- The transmittance is uncoloured coverage, as in the tracer: alpha attenuates light but does not tint it.
- Material-less occluders, such as q3map2's solid hulls, still block completely. Additive emitters are still left out of the BVH.
- **Bake output changes** for any scene where a shadow ray crosses a surface whose alpha is below 1. Where no shadow ray does, the output stays byte-identical. Opaque hits give a transmittance of exactly 0 or 1, the walk makes one intersection call and draws no random numbers, and the tracer's rewrite onto the new hit helpers keeps its ray parameters (`tnear` 0.001 for the first hit; 0.005 from the hit with `tfar` 1e10 for transmission) and its arithmetic.
- Delivered as one sh-baker PR to `github.com/DaviesX/sh-baker`, which the user reviews and merges. The landing then bumps the submodule and ticks B2b.

## Capabilities

### New Capabilities
- `sh-baker-shadow-rays`: how sh-baker's direct-light visibility treats what a shadow ray crosses: transmittance through alpha, opaque and material-less hits, the ray's extent, and how output changes relative to the opaque-only test.

### Modified Capabilities
None. The requirement "Occluders block without shading" in `sh-baker-q3map2-loader` already says material-less surfaces block direct light, and it still holds. The byte-identity requirement in `sh-baker-material-layers` is scoped to the commit before that change, so it is not contradicted.

## Impact

- **sh-baker code:** In `src/occlusion.{h,cpp}`, `Occlusion` gains the hit distance `t`, and three new helpers are added: `FindOcclusion(scene, materials, ray)` (returns `OcclusionWithAlpha`, the hit plus its alpha, which is 1 for material-less hits), `ContinuePastOcclusion` (the same ray, with `tnear` just past the hit) and `NewPastOcclusion` (a new ray from the hit: the tracer's transmission ray). `occlusion.h` now includes `material.h`. `src/light.{h,cpp}` get `light_internal::FindTransmittance` and `kMaxTransmittanceCrossings`, and the two NEE functions use it and take the materials. `src/tracer.cpp` uses the new `FindOcclusion` overload and `NewPastOcclusion` and passes the materials. `src/baker.cpp` passes the materials.
- **sh-baker tests:** `src/light_test.cpp` gets the walk tests (`TransmittanceTest`) and the NEE tests for point, spot, directional and area lights (`LightTransmittanceTest`). `src/baker_test.cpp` gets the cut-out fence and the half-covered bake point. `src/occlusion_test.cpp` gets a hit-distance test, and `src/tracer_test.cpp` gets a trace through an alpha-0 surface. Existing calls in `light_test.cpp` get the new argument and keep their assertions.
- **Bake output:** `data/box` and `data/MultiLights` are expected to stay byte-identical. `data/layers` and `data/notangent` are measured against `f4e4388` and the differences reported, because `layers` has RGBA textures. As merged, all four are byte-identical to `f4e4388`, including `layers`.
- **Unchanged:** q3map2's headers (`loader_q3map2.h`, `baker.h`), the CLI flags, and the output formats.
- **Out of scope:**
  - The glTF `alphaMode`/`alphaCutoff` (Sponza uses `MASK`). The loader ignores them today, for indirect rays too.
  - The Quake 3 `alphaFunc` threshold from the editor's caster rule. B5 turns it into the coverage it passes to sh-baker.
  - Shadows in `renderer_sh` (C5).
- **netradiant (landing):** the `tools/sh-baker` submodule bump, ticking `[x] **B2b**` in `CLAUDE.md`, a "Done" note on plan §4.2 item 2 with a PDF rebuild, and archiving the change.
