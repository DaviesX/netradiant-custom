# Proposal

## Why

The stock lightmap is still q3map2's placeholder `-light`, which reads the editor's lights with q3map2's own model and doesn't match the preview. Nothing yet writes the SH side file that `renderer_sh` needs. B2–B4 delivered the parts: sh-baker bakes caller-supplied points from q3map2 draw surfaces, and q3map2 owns the side-file format. B5 connects them as `q3map2 -light -shbake`, which bakes the editor's physical lights with sh-baker on q3map2's own luxels, grid points and vertices. A full bake writes the vanilla lightmap, the light grid and the vertex colours. An indirect bake over the same points writes the side file, because `renderer_sh` computes direct light at runtime (plan §4.3).

## What Changes

- **sh-baker PR #65, merged as `ad8f39f`: the editor's light model.** It added:
  - a per-light range cutoff, which the light tree also honours;
  - smoothstep cone falloff, with a hard edge for equal cones.

  Its review dropped the per-light shadow flag and the separate punctual-direct result. Every light casts in a bake, point lights and `_shadows 0` lights included. PR #66 (`046ff2e`) made `indirect_only` drop all direct light at the baked point, emissive included. The submodule pointer is at `8f181ba` (#68, which contains #65, #66 and #67).
- **sh-baker PR: full-sphere points.** A new `SurfacePoint::full_sphere` flag, off by default, samples the whole sphere in world space and counts light from every direction at the baked point, for light-grid probes. One PR to `github.com/DaviesX/sh-baker`, reviewed and merged by the user before the grid work that needs it. Defaults keep every existing bake bit-identical.
- **sh-baker PR #68, merged as `8f181ba`: exact sample count.** `BakeConfig::confidence_threshold <= 0` turns off the adaptive stop, which ended points lit only by unshadowed point lights at 64 samples whatever `samples` said. q3map2 always passes 0, so `-shsamples N` is exact.
- **Side-file format version 2.** After the hull mesh it adds an SH grid: origin, spacing and dimensions, then nine RGB `float32` coefficients per probe and one valid byte per probe. Probe *i* is BSP light-grid point *i*. The side file always holds the indirect bake, so it has no contents field. The writer writes version 2, and the reader accepts only version 2. **BREAKING** for the format only. Version 1 (B4) has no writer or reader outside this repository.
- **`-shbake`, an option of `-light`** (`light_sh.cpp`, built only under `SH_BAKER=1`):
  - **Scene and lights.** It replaces the lighting of `IlluminateRawLightmap`, `TraceGrid` and `IlluminateVertexes`. It builds an sh-baker scene from:
    - the draw surfaces that cast under the editor's caster rule;
    - one shrunk solid hull per casting brush;
    - emissive surfaces as area lights;
    - the `light`, `light_spot` and `light_sun` entities, read with the editor's physical model.
  - **One point list.** It holds:
    - every mapped super-luxel;
    - one full-sphere point per light-grid point;
    - every draw vertex.
  - **Two bakes over it.** Each is an ordinary `BakeSHLightMap` call, so results line up index for index.
    - The **full bake** (direct plus indirect) feeds the stock target. The BSP lightmap pages and vertex colours get its irradiance at the point's normal, a cosine-weighted estimate with no SH ringing. The grid bytes come from its full-sphere probe SH, fit as ambient plus one directional light. The encoding reproduces the editor preview's tonemap (Uncharted2, white point 11.2) for white albedo, calibrated to opengl1 at default cvars.
    - The **indirect bake** (`indirect_only`) feeds `renderer_sh`. The side file `maps/<name>/<name>.shsf` gets its SH pages, its SH grid and the un-shrunk hull mesh. Page *i* of the side file is BSP lightmap page *i*. `renderer_sh` adds direct light at runtime.
  - **Toggles.** Both bakes run by default. `-shnofull` skips the full bake and writes the neutral byte 64 to the lightmap pages and vertex colours; the grid gets neutral ambient (63) with no directed light. `-shnoindirect` skips the indirect bake and writes no side file. The page layout doesn't depend on lit bytes, so it is the same in every mode.
  - **Refusals.** Anything that would change lightmap storage is refused, including a shader's `q3map_lightmapSize`. Options for the replaced lighting are ignored with one warning.
  - **Recorded gaps.** The bake shades with Lambert, so normal maps and metallic-roughness don't change baked light. It shadows every light, while the editor preview and `renderer_sh` keep the caster rule (point lights and `_shadows 0` lights don't cast). The indirect bake has no direct light from emissive surfaces, so `renderer_sh` lacks it until LTC area lights (C7).
- **q3map2's shader parser** learns:
  - the `qer_pbr_*` keywords;
  - the stage facts the editor uses: base-colour stage, base `alphaFunc`, additive emissive stage and its `rgbGen const`;
  - the literal surfaceparms of the caster rule.

  Without `-shbake`, q3map2's output stays byte-identical.
- **A pure helper module** `light_sh_model.{h,cpp}` holds:
  - the light conversion;
  - the caster classification;
  - the lightmap encoding;
  - the grid fit from one full-sphere SH, against the engine's model lighting.

  It has no sh-baker or q3map2-global dependency, so `make test-q3map2-shbake-model` tests it like the B4 module.
- **Build:**
  - `SH_BAKER=1` in the `Makefile` adds the sh-baker include path and links the submodule's Release `libsh_baker.so` with an rpath. Only `light_sh.cpp` is compiled with `-march=native`.
  - Without the flag, `-shbake` stops with a message saying to rebuild with `SH_BAKER=1`.
- **Workflow:**
  - The stock check and the overlay's Town build switch their light stage to `-light -shbake`. The Town build then becomes three quality presets: draft (`-shsamples 128`), pre-release (`-shsamples 512 -super 2`) and final (`-shsamples 2048 -super 2`). The stock check keeps the defaults.
  - The stock check packs the side file.
  - The gamepack README, `CLAUDE.md` and plan §4.3 record the encoding, the two bakes and the gaps. The plan adds C7, LTC area lights in `renderer_sh`, and C6 reads the SH grid for dynamic models.

Assumption: B5 is developed and verified on the benchmark map. A8's town block isn't needed to land it. The plan's "town block" exit wording is satisfied when A8 compiles with this stage, and A9 then judges the encoding.

## Capabilities

### New Capabilities
- `q3map2-shbake`: the `-shbake` stage of q3map2 `-light`. It covers:
  - scene assembly (caster rule, hulls, materials, emitters);
  - light entities read with the physical model, every light shadowed;
  - the point list of luxels, full-sphere grid points and vertices;
  - the full and indirect bakes and their toggles;
  - how SH follows q3map2's lightmap storage, with a page layout independent of lit bytes;
  - the irradiance encoding into lightmap, grid and vertex colours;
  - side-file output, its SH grid included;
  - refused and ignored options, warnings and the build flag.
- `sh-baker-light-model`: sh-baker's support for the editor's light model and for grid probes. It covers:
  - the range cutoff and range-aware light selection (merged);
  - smoothstep cone falloff (merged);
  - full-sphere points (#67);
  - the exact sample count (#68).

### Modified Capabilities
- `q3map2-sh-extension`: version 2 adds the SH grid after the hulls. It adds the requirement SH grid meaning and changes Extension contents, SH basis (full sphere for probes), Byte layout, Version rule, Value encoding, Writer refuses malformed input and Reader rejects malformed files. The golden bytes grow from 144 to 398, and the empty file from 24 to 60 bytes.
- `stock-check`: the light stage becomes `-light -shbake`. Stale side files are deleted first, and the pk3 includes the new one (Isolated compile).
- `pbr-gamepack`: the Town build becomes three builds whose light stages are `-light -shbake` at three quality presets.
- `benchmark-map`: the compile flags become `-light -shbake`, and the lightmap is no longer a placeholder (Benchmark compile flags). The pk3 holds the side file (Single pk3 packaging), and lighting fidelity is left to A9 (Stock screenshots reviewed by the author).
- `pbr-light-entities`: the point light's default intensity is corrected from 100 to 50000, which the editor, the README and the `.ent` file already use (Point light keys).
- `sh-baker-shadow-rays`: transmittance applies to every light, since a bake has no per-light shadow switch, and at full-sphere points as at surface points (Applies at bake points and at bounces).

## Impact

- **sh-baker** (submodule):
  - #65, #66, #67 and #68 are merged; the pointer is at `8f181ba`.
  - #67, the one new PR: `rasterizer.h` (`SurfacePoint::full_sphere`), `baker.cpp` (per-point sample domain and weight), `light.h` (no cosine rejection at a full-sphere baked point), and tests.
  - The pointer moves to the merged commit. q3map2 must be rebuilt against the new headers, since the shared structs have no ABI versioning.
- **q3map2:**
  - New files: `light_sh.cpp`, `light_sh_stub.cpp`, `light_sh_model.{h,cpp}`, `light_sh_model_test.cpp`.
  - Changed files:
    - `light.cpp`: option parsing (`-shnofull`, `-shnoindirect`) and the `LightWorld` branch.
    - `lightmaps_ydnar.cpp`: SH storage carried through supersampling, the neighbour fill, wrap and page placement.
    - `q3map2.h`: `rawLightmap_t` and `outLightmap_t` SH buffers, and `shaderInfo_t` fields.
    - `shaders.cpp`: stage and `qer_pbr_*` parsing.
    - `help.cpp`: the new options.
    - `q3map2_extension.{h,cpp}` and its test: version 2 with the SH grid.
- **Build:** the `Makefile` gets `SH_BAKER=1` and the new test target. A q3map2 that can run the stock check needs `SH_BAKER=1` and a Release sh-baker build.
- **Tools and docs:**
  - `tools/stockcheck/stockcheck.py` and its README.
  - `setup/data/gamepacks/pbr/Q3.game/default_build_menu.xml` and `README.md`.
  - `CLAUDE.md` (B5, C6, new C7) and `docs/pbr-plan/pbr-plan.tex` §4.3, §4.4 and §Units.
- **Licence:** a q3map2 built with `SH_BAKER=1` is distributed under GPLv3 (Embree and TBB are Apache-2.0), as plan §4.3 records.
- **Downstream:**
  - C4 computes direct light at runtime for every editor light and never reads baked direct light.
  - C5 and C6 read side-file version 2. C6 takes indirect light from the luxel SH, and lights dynamic models from the SH grid by trilinear blending of valid probes.
  - C7 (new): LTC area lights for static emissive quads, since the indirect bake has no emissive direct light.
  - A9 judges the full bake's lightmap, which shadows point lights the editor preview doesn't.
