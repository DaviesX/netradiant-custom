# Design: Alpha in sh-baker's shadow rays (B2b)

## Context

The motivation is in proposal.md. The relevant code in sh-baker `f4e4388`:

- `FindOcclusion(RTCScene, const Ray&)` (`src/occlusion.cpp`) runs `rtcIntersect1` and returns the closest hit's position, normal, UV and `material_id`, which it reads from the `Geometry*` user data. It does not return the hit distance.
- The light helpers in `src/light.h` build each shadow ray. The direction is normalised, `tnear` is 0.005, and `tfar` is `dist - 0.005` for point, spot and area lights, or `1e10` for directional lights.
- `EvaluateLightSamples` (tracer, per bounce) and `AccumulateIncomingLightSamples` (baker, per bake point) each call `if (FindOcclusion(...)) continue;`. Neither has access to the materials.
- The tracer reads `GetAlpha(mat, uv)`: the albedo alpha, nearest texel, wrapped, and 1 when the texture has no alpha channel. It continues the ray with weight `1 - alpha` from the hit position, using `tnear` 0.005.
- `BuildBVH` keeps additive materials out of the scene and attaches every other geometry, material-less ones included, with build quality HIGH.
- The glTF loader composites a Quake 3 stage stack's coverage into the albedo's alpha (`CompositeAlbedoCoverage`). In B5, q3map2 will fill the albedo the same way. So `GetAlpha` is already where a material's coverage lives.

## Goals / Non-Goals

**Goals:**
- One visibility function, shared by both NEE paths, that matches what the tracer does with alpha.
- No change at all, down to the byte and the RNG stream, when no shadow ray crosses anything with alpha below 1.

**Non-Goals:**
- Alpha-test thresholds (`alphaMode`/`alphaCutoff`, Quake 3 `alphaFunc`). Coverage stays fractional, exactly as the tracer reads it.
- Coloured transmission or stochastic transparency in shadow rays.
- Speed-ups for shadow rays, such as `rtcOccluded1` fast paths for scenes with no alpha.

## Decisions

### 1. A transmittance walk with ordered `rtcIntersect1` calls, not an Embree filter callback

Add `float light_internal::FindTransmittance(RTCScene, const std::vector<Material>&, const Ray&)` and `kMaxTransmittanceCrossings = 64` to `light.{h,cpp}`. It loops:

1. Call `FindOcclusion(scene, materials, ray)` on the current ray. A miss returns the transmittance accumulated so far.
2. If the hit's alpha is 1 or more, return 0. The overload gives material-less hits alpha 1.
3. If 64 crossings have already been counted, return 0. Otherwise multiply the transmittance by `1 - alpha`.
4. Continue with `ContinuePastOcclusion`: the same ray, keeping origin, direction and `tfar`, with `tnear` set to the next float after the hit's `t`. Stop with the transmittance if `tnear >= tfar`; otherwise repeat.

So 64 crossings with alpha below 1 still pass, and a 65th counts as blocked.

`Occlusion` gains `float t`, the hit's ray parameter, which `FindOcclusion` fills from `rtc_ray_hit.ray.tfar`. `occlusion.{h,cpp}` also gain the helpers that the walk and the tracer share: `OcclusionWithAlpha` (the hit and its `GetAlpha`, 1 for material-less hits), a `FindOcclusion(scene, materials, ray)` overload that returns it, `ContinuePastOcclusion` for the shadow walk, and `NewPastOcclusion`, the tracer's transmission ray (origin at the hit, `tnear` 0.005, `tfar` 1e10). The tracer is rewritten onto the overload and `NewPastOcclusion`, with the same ray parameters.

- *Alternative: an occlusion filter callback on `rtcOccluded1` that multiplies `1 - alpha` and rejects the hit.* That is cheaper, but Embree calls filters in no particular order, and it may report the same primitive more than once when spatial splits are on. `RTC_BUILD_QUALITY_HIGH` enables spatial splits, so a product of `1 - alpha` could be counted twice. Ordered closest-hit calls are exact and mirror the tracer's loop.
- *Alternative: move the origin to each hit, as the tracer does (`NewPastOcclusion`).* Moving `tnear` along one ray (`ContinuePastOcclusion`) keeps `tfar` meaningful, so the light's distance needs no recomputing, and it avoids drift accumulating from re-origination.
- The step past a hit is one float step in `t` (`std::nextafter`), not a fixed offset, so a surface just behind a cut-out is not skipped. The hit's own triangle is not hit again, because the origin and direction are unchanged and Embree returns the same `t`. Two coplanar duplicate triangles at the same `t` count once. That is acceptable, because duplicates are a content error that the tracer also mishandles.

### 2. The NEE functions take the scene's materials

`EvaluateLightSamples` and `AccumulateIncomingLightSamples` gain a `const std::vector<Material>& materials` parameter right after `rtc_scene`. The call `if (FindOcclusion(...)) continue;` becomes:

```cpp
float visibility =
    light_internal::FindTransmittance(rtc_scene, materials, visibility_ray);
if (visibility <= 0.0f) continue;
radiance *= visibility;
```

The tracer passes `config.scene.materials` and the baker passes `scene.materials`.

- *Why the material vector and not the `Scene`:* the function only needs material lookup, and the existing tests, which have no materials, can pass `{}`.
- **Byte identity:** when every hit is opaque, `visibility` is exactly 0 or 1. A sample with `visibility == 0` is skipped, as before. A sample with `visibility == 1` multiplies `radiance` by `1.0f`, which IEEE-754 makes exact. The walk makes one `rtcIntersect1` call per shadow ray, as before, and draws no random numbers. `ContinuePastOcclusion` runs only after a hit with alpha below 1. The tracer's rewrite onto `FindOcclusion(scene, materials, ray)` and `NewPastOcclusion` keeps its ray parameters and arithmetic. So the output can differ only where some alpha is below 1, and the multiplication needs no `visibility < 1` guard.

### 3. Material-less and additive geometry are unchanged

Material-less hits get alpha 1 from the `FindOcclusion` overload, so the walk returns 0 at step 2. That keeps "Occluders block without shading" from `sh-baker-q3map2-loader`. The tracer still checks `material_id < 0` itself and stops there. Additive materials are not in the BVH, so the walk never sees them.

### 4. Delivery: one PR, then a landing

The change goes up as one sh-baker PR on branch `nee-alpha-shadows` off `main`. Before opening it, run `clang-format -i` on the touched files. The landing in netradiant comes after the user merges.

## Risks / Trade-offs

- [Cost: shadow rays through foliage now walk up to 65 closest-hit calls instead of stopping at one any-hit] → Opaque content still takes one call. Foliage pays for each card it crosses. That is acceptable for an offline bake. If it proves slow, a later change can add an any-hit fast path when no material in the scene has alpha.
- [Fractional alpha from bilinear-free nearest sampling gives hard-edged holes] → This matches the tracer and the Quake 3 alpha test. B5 decides the coverage it supplies.
- [`data/layers` output may change, since it has RGBA textures, some possibly with partial alpha in shadow paths] → The gate measures the difference against `f4e4388` and reports it to the user before continuing, per the byte-identity rule. It is accepted only with the user's agreement.
- [The spec's 64-crossing limit is arbitrary] → It is far beyond realistic stacks of cards (a ray through a tree crosses tens at most), and anything with 64 partial covers has negligible transmittance anyway.

## Migration Plan

This is not a runtime migration. After the PR merges, the landing bumps `tools/sh-baker`, ticks B2b in `CLAUDE.md`, adds a "Done" note to plan §4.2 item 2 and rebuilds the PDF, archives the change, and runs the stock check. To roll back, revert the sh-baker commit `77f1b72` (#62) and the submodule bump.
