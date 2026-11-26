# Design

## Context

See proposal.md for the motivation. The current state:

- **sh-baker already computes the value.** `BakeSHLightMap` records one visibility per point in `BakeResult::environment_visibility_texture`, from each sample's first ray (`baker.cpp`, the `on_first_ray_fn` callback). The weight is 1 for a `full_sphere` point and cos θ for a surface point. So a grid point's value is already the uniform fraction of escapes. B11 (sh-baker #70) fixed this, and `baker_test.cpp` covers it, for example a half-open full-sphere point at about 0.5. The value comes from geometry alone. It is recorded whether or not `Scene::environment` is set, and in both the full and the `indirect_only` bake.
- **q3map2 drops it for grid points.** `ShStoreIndirect` (`light_sh.cpp`) reads the visibility for every point through its `shLuxel` helper, and uses it to decide finiteness. For grid probes it copies only the 27 coefficients into `SHGrid`.
- **The format.** Side file version 2 (`q3map2_extension.{h,cpp}`) stores the grid as a 36-byte header, then 108 bytes of `float32` coefficients per probe, then 1 valid-flag byte per probe, with the flags last so everything before them stays 4-byte aligned. `c_bytesPerProbe` is 109. The reader computes the grid's size from the dimensions with overflow-safe arithmetic before it allocates. No renderer reads the file yet; C4b will be the first reader, written against version 3.

## Goals / Non-Goals

**Goals:**
- One sky-visibility value per probe in the side file, as sh-baker computes it, carrying the same checks the format already applies to its other values.
- Version 3 as the first version a renderer reads, fixed from then on.

**Non-Goals:**
- No change to sh-baker, the page visibility plane, the BSP lumps or the full bake. The stock grid's sky light is already baked into its bytes.
- No runtime use. C6 multiplies the sky irradiance by this value, and C4b reads it.
- No per-probe directional visibility, such as an SH-projected visibility or a bent normal. A scalar is what C6 specifies, and it is exact for a uniform sky. The error for a non-uniform sky is accepted, as it is for luxels.

## Decisions

1. **A separate `float32` section between the probe coefficients and the valid flags.**
   - Every probe takes 113 bytes, so the grid is 36 + 113 × N bytes. The coefficient section keeps its offset and stride, the new section starts 4-byte aligned (108 × N bytes after the coefficients), and the flags stay last, as the alignment rule already requires.
   - Alternatives considered:
     - Interleaving the value as a 28th float per probe would change the coefficient stride that C4b and the tests rely on, for no gain.
     - Storing it as `binary16` like the page visibility would save 2 bytes per probe. That is under 2 KB on the town's grid. The grid is `float32` throughout, so one encoding per section wins.

2. **Version 3 only, in both the writer and the reader.**
   - Version 2 files are rejected rather than read with a default visibility. Defaulting to 0 or 1 would silently light models wrongly. No version-2 reader exists outside q3map2's own tests, and a rebake rewrites the file.
   - The version rule's "no exception" clause is stated for version 3, because C4b reads it.

3. **The value comes straight from the indirect bake's result.**
   - `ShStoreIndirect` copies `environment_visibility_texture.pixel_data[index]` for each grid point it already marks valid. The finiteness test already covers this value, since `shLuxel` checks it, so a non-finite visibility already makes the probe invalid.
   - The full bake's visibility for the same point is not used. The two bakes trace the same first rays, but the side file holds the indirect bake's results throughout, so the source stays the same as the pages'.

4. **Invalid probes hold 0, and the writer and reader enforce it.**
   - This matches the existing rule for invalid probes' coefficients, so a corrupted file can't smuggle data into a probe that has none.
   - Values outside [0, 1] and non-finite values are refused, like page visibility.

5. **Visibility doesn't depend on the sky's strength.**
   - A map whose sky has no `qer_pbr_emissiveStrength`, or no sky at all, still gets real probe visibility. sh-baker records escapes regardless, and the value is geometric. A renderer multiplies it by a zero sky irradiance, so this is harmless.
   - A sky added later at runtime then works without a rebake, as the luxels' visibility already does.

6. **Tests:**
   - The format is covered by `make test-q3map2-extension`: the hand-built golden bytes grow from 398 to 406 bytes, with refusal and reader cases for the new section.
   - The bake is covered by a scratchpad check over the town and two small sky maps, as B12's scenario checks were. The repo has no bake test harness, and these follow B12's practice of scratchpad scripts named in the PR.

## Risks / Trade-offs

- **[Breaking format change]** Every existing `.shsf` stops loading. → No reader ships yet. The town, the benchmark and q3dm7 are rebaked when they're next built. The stock check only checks that the file exists and packs it.
- **[Probes outside the playable volume]** The BSP grid spans the world bounds. Sky brushes don't cast, so a probe above the town's low sky ceiling, or outside the walls in the void, may be valid with a visibility near 1. → This is harmless: C6 only blends probes around shaded points inside the map. It is documented in the gamepack README beside the existing note on the grid.
- **[Scalar visibility for a non-uniform sky]** For a dark-horizon or farbox sky, irradiance × uniform visibility misestimates partly occluded probes. → This is accepted, as it is for luxels (§Sky light). Directional visibility is a later concern for C6, if ever.
- **[Noise]** The visibility uses the same samples as the SH: 128 at the draft preset. The noise is about √(v(1 − v)/N), at most about 0.044 at 128 samples. → This is the same order as the luxels' noise. The higher presets reduce it.

## Migration Plan

Merge, rebuild q3map2 with `SH_BAKER=1`, then rebake maps whose side file matters. A rollback reverts the commit, and maps baked in between need a rebake to return to version 2.
