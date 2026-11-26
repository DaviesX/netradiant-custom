# Proposal

## Why

`renderer_sh` adds direct sky light at runtime as the sky's irradiance times a baked sky visibility (C6). Surfaces have that visibility already: each side-file luxel's visibility plane. The SH grid has none, but it lights dynamic models and vertex-lit surfaces, so those would get either no direct sky light or unshadowed sky light indoors. sh-baker already computes the value for grid points: a full-sphere point's visibility is the uniform fraction of escapes (B11, sh-baker #70). q3map2 drops it when it fills the grid. B13 stores it, and C4b's reader is written against the format that holds it.

## What Changes

- **BREAKING** Side file version 3: the SH grid gains one `float32` sky visibility per probe, after the probe coefficients and before the valid flags. A probe then takes 113 bytes instead of 109. Writer and reader move to version 3 only. Version 2 files are rejected; no renderer reads them, and a rebake rewrites them.
- The extension's `SHGrid` gains a `visibility` array, one value per probe. The writer refuses a non-finite value, a value outside [0, 1], or a non-zero value on an invalid probe. The reader rejects the same.
- `-shbake`'s indirect bake fills each valid probe's visibility from the indirect bake's result for that grid point: the uniform fraction of the point's first rays that escaped, over the whole sphere. Invalid probes (in solid, or with a non-finite result) hold 0. Visibility depends only on the map's geometry, so a map whose sky emits nothing still gets it.
- `make test-q3map2-extension` covers the new layout: the golden bytes, refusals, the reader's checks and the probe-order round trip.
- Docs: the format comment in `q3map2_extension.h`, the gamepack README's SH bake and sky-light sections, `CLAUDE.md` (B13 done) and the plan's B13 notes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `q3map2-sh-extension`: the extension contents, byte layout, version rule, value encoding, writer refusals, reader checks and SH grid meaning gain the per-probe sky visibility (version 3).
- `q3map2-shbake`: the side file's SH grid holds each probe's sky visibility from the indirect bake.

## Impact

- Code: `tools/quake3/q3map2/q3map2_extension.{h,cpp}`, `q3map2_extension_test.cpp` and `light_sh.cpp` (`ShStoreIndirect`). No sh-baker change and no new dependency.
- Output: every `-shbake` side file becomes version 3 and grows by 4 bytes per grid probe. The BSP doesn't change: lightmap, grid and vertex lumps are byte-identical to before.
- Downstream: C4b (`renderer_sh`'s side-file loader) reads version 3 only. No other reader exists. The stock check only checks that the file exists and packs it.
