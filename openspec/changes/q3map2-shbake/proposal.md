# Proposal

## Why

The stock lightmap is still q3map2's placeholder `-light`, which reads the editor's lights with q3map2's own model and doesn't match the preview. Nothing yet writes the SH side file that `renderer_sh` needs. B2–B4 delivered the parts: sh-baker bakes caller-supplied points from q3map2 draw surfaces, and q3map2 owns the side-file format. B5 connects them as `q3map2 -light -shbake`, which bakes the editor's physical lights with sh-baker on q3map2's own luxels. From that one bake it writes the vanilla lightmap, the light grid, the vertex colours and the side file (plan §4.3).

## What Changes

- **sh-baker PR: the editor's light model.** One PR to `github.com/DaviesX/sh-baker`, reviewed and merged by the user before the q3map2 work that needs it. It adds:
  - a per-light range cutoff, which the light tree also honours;
  - smoothstep cone falloff;
  - a per-light flag for casting shadows;
  - a bake option that returns the punctual lights' direct light as separate, exact results (SH and irradiance), beside the indirect result.

  The new fields default to today's behaviour, so the CLI and the existing tests are unchanged.
- **Side-file format version 2.** The header gains a contents field: *full* (direct plus indirect) or *indirect only* (punctual direct left out, emissive direct kept). The writer writes version 2, and the reader accepts only version 2. No version 1 file was ever written. `renderer_sh` picks its shading path from this field. **BREAKING** for the format only. Version 1 has no writer or reader outside this repository.
- **`-shbake`, an option of `-light`** (`light_sh.cpp`, built only under `SH_BAKER=1`):
  - **Scene and lights.** It replaces the lighting of `IlluminateRawLightmap`, `TraceGrid` and `IlluminateVertexes`. It builds an sh-baker scene from:
    - the draw surfaces that cast under the editor's caster rule;
    - one shrunk solid hull per casting brush;
    - emissive surfaces as area lights;
    - the `light`, `light_spot` and `light_sun` entities, read with the editor's physical model.
  - **One bake.** It bakes, in one call:
    - every mapped super-luxel;
    - two opposite hemispheres at every light-grid point;
    - every draw vertex.
  - **Outputs.**
    - The BSP lightmap pages, light grid and vertex colours get direct plus indirect irradiance. The punctual direct term is exact, not taken from SH. The encoding reproduces the editor preview's tonemap (Uncharted2, white point 11.2) for white albedo, calibrated to opengl1 at default cvars.
    - The side file `maps/<name>/<name>.shsf` gets the SH pages and the un-shrunk hull mesh. Page *i* of the side file is BSP lightmap page *i*.
  - **Contents and refusals.** `-shindirect` selects the indirect-only contents; the default is full. Anything that would change lightmap storage is refused, including a shader's `q3map_lightmapSize`. Options for the replaced lighting are ignored with one warning.
  - **Lambert only.** The bake shades with Lambert, so normal maps and metallic-roughness don't change baked light. This is a recorded fidelity gap.
- **q3map2's shader parser** learns:
  - the `qer_pbr_*` keywords;
  - the stage facts the editor uses: base-colour stage, base `alphaFunc`, additive emissive stage and its `rgbGen const`;
  - the literal surfaceparms of the caster rule.

  Without `-shbake`, q3map2's output stays byte-identical.
- **A pure helper module** `light_sh_model.{h,cpp}` holds:
  - the light conversion;
  - the caster classification;
  - SH-to-irradiance;
  - the lightmap encoding;
  - the grid fit, against the engine's model lighting.

  It has no sh-baker or q3map2-global dependency, so `make test-q3map2-shbake-model` tests it like the B4 module.
- **Build:**
  - `SH_BAKER=1` in the `Makefile` adds the sh-baker include path and links the submodule's Release `libsh_baker.so` with an rpath. Only `light_sh.cpp` is compiled with `-march=native`.
  - Without the flag, `-shbake` stops with a message saying to rebuild with `SH_BAKER=1`.
- **Workflow:**
  - The stock check and the overlay's Town build switch their light stage to `-light -shbake`.
  - The stock check packs the side file.
  - The gamepack README, `CLAUDE.md` and plan §4.3 record the encoding and the gaps.

Assumption: B5 is developed and verified on the benchmark map. A8's town block isn't needed to land it. The plan's "town block" exit wording is satisfied when A8 compiles with this stage, and A9 then judges the encoding.

## Capabilities

### New Capabilities
- `q3map2-shbake`: the `-shbake` stage of q3map2 `-light`. It covers:
  - scene assembly (caster rule, hulls, materials, emitters);
  - light entities read with the physical model;
  - the luxel, grid and vertex bake;
  - how SH follows q3map2's lightmap storage;
  - the irradiance encoding into lightmap, grid and vertex colours;
  - side-file output and contents selection;
  - refused and ignored options, warnings and the build flag.
- `sh-baker-light-model`: sh-baker's support for the editor's light model. It covers:
  - the range cutoff and range-aware light selection;
  - smoothstep cone falloff;
  - per-light shadow casting;
  - the separate, exact punctual-direct result.

### Modified Capabilities
- `q3map2-sh-extension`: version 2 adds the bake-contents field to the header. It adds the requirement Bake contents field and changes Extension contents, SH basis (main plus punctual SH for full contents), Byte layout, Version rule, Writer refuses malformed input and Reader rejects malformed files. The golden bytes grow to 148, and the empty file from 24 to 28 bytes.
- `stock-check`: the light stage becomes `-light -shbake`. Stale side files are deleted first, and the pk3 includes the new one (Isolated compile).
- `pbr-gamepack`: the Town build group's light stage becomes `-light -shbake`.
- `benchmark-map`: the compile flags become `-light -shbake`, and the lightmap is no longer a placeholder (Benchmark compile flags). The pk3 holds the side file (Single pk3 packaging), and lighting fidelity is left to A9 (Stock screenshots reviewed by the author).
- `pbr-light-entities`: the point light's default intensity is corrected from 100 to 50000, which the editor, the README and the `.ent` file already use (Point light keys).
- `sh-baker-shadow-rays`: transmittance applies only to lights that cast shadows (Applies at bake points and at bounces).

## Impact

- **sh-baker** (submodule, one PR):
  - `light.{h,cpp}`, `scene.h` (`Light` fields), `light_tree.{h,cpp}` (range-aware importance), `baker.{h,cpp}` (split option and results), and tests.
  - The submodule pointer moves to the merged commit. q3map2 must be rebuilt against the new headers, since the shared structs have no ABI versioning.
- **q3map2:**
  - New files: `light_sh.cpp`, `light_sh_stub.cpp`, `light_sh_model.{h,cpp}`, `light_sh_model_test.cpp`.
  - Changed files:
    - `light.cpp`: option parsing and the `LightWorld` branch.
    - `lightmaps_ydnar.cpp`: SH storage carried through supersampling, the neighbour fill, wrap and page placement.
    - `q3map2.h`: `rawLightmap_t` and `outLightmap_t` SH buffers, and `shaderInfo_t` fields.
    - `shaders.cpp`: stage and `qer_pbr_*` parsing.
    - `q3map2_extension.{h,cpp}` and its test: version 2.
- **Build:** the `Makefile` gets `SH_BAKER=1` and the new test target. A q3map2 that can run the stock check needs `SH_BAKER=1` and a Release sh-baker build.
- **Tools and docs:**
  - `tools/stockcheck/stockcheck.py` and its README.
  - `setup/data/gamepacks/pbr/Q3.game/default_build_menu.xml` and `README.md`.
  - `CLAUDE.md` (B5) and `docs/pbr-plan/pbr-plan.tex` §4.3 and §Units.
- **Licence:** a q3map2 built with `SH_BAKER=1` is distributed under GPLv3 (Embree and TBB are Apache-2.0), as plan §4.3 records.
- **Downstream:** C5 and C6 read side-file version 2 and branch on its contents field.
