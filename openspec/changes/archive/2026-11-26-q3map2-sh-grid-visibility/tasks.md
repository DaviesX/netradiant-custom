# Tasks

## 1. Side file version 3 (format)

- [x] 1.1 `q3map2_extension.h`:
  - Add `std::vector<float> visibility` to `SHGrid`, one per probe.
  - Rewrite the layout comment for version 3: the new section 8, the grid as 36 + 113 × N bytes, and the version rule stating that version 3 takes no exception.
  - Describe probe sky visibility under "SH grid", including invalid probes holding 0.
  - List the new writer refusals.

  Verify: the header compiles in `make test-q3map2-extension`.
- [x] 1.2 `q3map2_extension.cpp`:
  - Set `c_version` to 3 and `c_bytesPerProbe` to 113.
  - Make `GridProblem` and the shared per-probe check refuse:
    - a visibility count that differs from the probe count;
    - a non-finite visibility or one outside [0, 1];
    - a non-zero visibility on an invalid probe.
  - Write the visibility section after the coefficients.
  - Make the reader read it, apply the same checks, and keep its file-size test overflow-safe with the new stride. Messages name the probe and the value; the unknown-version message says that only version 3 is supported.

  Verify: 1.3 passes.
- [x] 1.3 Update `q3map2_extension_test.cpp` to the delta spec's scenarios:
  - the golden array rebuilt by hand, 406 bytes, with probe 0's visibility 0.375 (`0x3EC00000`) and probe 1's 0;
  - versions 1, 2 and 4 rejected;
  - probe-value exactness, including the visibility 1 − 2^−20;
  - the wrong-count refusals for coefficients and visibilities;
  - an invalid probe with visibility 0.5 refused;
  - probe visibilities 1.5, −0.25 and NaN refused;
  - the reader's NaN visibility, out-of-range visibility and invalid-probe-visibility cases;
  - probe order with visibility i / 16;
  - the empty file still 60 bytes.

  `MakeGrid` fills varied visibilities. Verify: `make test-q3map2-extension` passes.

## 2. `-shbake` fills the probe visibility

- [x] 2.1 `light_sh.cpp` `ShStoreIndirect`: size `grid.visibility` to the probe count, filled with 0. For each probe made valid, store the indirect bake's `environment_visibility_texture` value for its point. Invalid probes keep 0, whether they are in solid or have a non-finite SH or visibility. Extend the side-file summary line with the valid probes' mean sky visibility. Verify: q3map2 builds with `SH_BAKER=1`, and a benchmark bake writes a side file the reader accepts (spec "Side file written").
- [x] 2.2 Write a scratchpad bake check that reads version 3 and covers:
  - the shbake delta's scenarios: the town's open-crossroads probe above 0.25 and its sealed-room probes at exactly 0; the benchmark's in-solid probe invalid with visibility 0;
  - the sky-strength-independence pair, from B12's `open` scenario with and without the strength;
  - that the BSP's lightmap, light-grid and draw-vertex lumps are byte-identical to a bake before this change.

  The open-floor map isn't checked in: regenerate it with B12's scratchpad `sc/gen.py` (its `open` variant, plus a copy without `qer_pbr_emissiveStrength`), as B12 did; if the scratchpad has been cleared, rewrite the generator from B12's `pr.md` description in this change's scratchpad. Verify: every check passes; record the script paths for the PR.
- [x] 2.3 Run the stock check on the town and the benchmark with the new q3map2. Verify: both pass.

## 3. Docs

- [x] 3.1 Gamepack README (`setup/data/gamepacks/pbr/README.md`):
  - The bake table's indirect row (line 842): "SH grid" becomes "SH grid with per-probe sky visibility (side file version 3)"; the table names no version today, so this adds it.
  - "Sky light" (the q3map2 paragraph near line 462): the probe sky visibility, a uniform fraction since a probe has no normal, used by `renderer_sh` as the sky's irradiance × the blended probe visibility.
  - The grid note (line 954): probes outside the playable volume may have a visibility near 1.

  Verify: the text matches the spec deltas.
- [x] 3.2 `docs/pbr-plan/pbr-plan.tex`:
  - Rewrite the "Side file" item's grid sentence (lines 1207-1210, "nine RGB coefficients … and a valid flag; a probe left in solid … is invalid and zero") for version 3: each probe also holds its sky visibility, the uniform escape fraction over the sphere, and a probe in solid or with a non-finite result is invalid with zero coefficients and visibility.
  - Mark B13 done where it is cited (line 110, §4.3's sky item near line 1578, and the task table near line 1767).

  `CLAUDE.md`: tick B13 and cite the change name. Verify: `latexmk -pdf` builds `docs/pbr-plan/` without new errors.
