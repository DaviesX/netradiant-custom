# Tasks

The gate for each group (design decision 15):

- q3map2 builds (`make binaries-q3map2`) with and without `SH_BAKER=1`, with no new warnings;
- `make test-q3map2-extension` passes, and from group 4 on so does `make test-q3map2-shbake-model`;
- the stock check passes on a clean `git archive` copy of the benchmark;
- until group 7, the benchmark compiled single-threaded with the placeholder stages gives a `bench.bsp` byte-identical to the baseline's, apart from the q3map2 version stamp. Any difference is reported to the user before continuing.

Every task's exit criterion includes the stock check (`tools/stockcheck/stockcheck.py`). Ask before any push or PR. Scratch maps live under the scratchpad, never in `content/`.

## 1. Baseline

- [ ] 1.1 Create branch `b5-shbake` from `master`. Build q3map2 and a Release `libsh_baker.so` in `tools/sh-baker/build` at the current submodule commit. Run the stock check on a clean `git archive` copy of the benchmark. Compile the benchmark single-threaded twice with the placeholder stages, and keep that `bench.bsp` as the byte-identity baseline. Verify that the stock check passes and that both compiles give the same checksum, then record it.

## 2. sh-baker PR: the editor's light model

- [ ] 2.1 In the `tools/sh-baker` submodule, on a new branch, add `Light::range`, `Light::casts_shadows` and `Light::cone_falloff`, with the hard edge for equal cones in both modes, `LightBounds::max_range` with its `Union` and `Importance` rules, and the per-point choice between the BVH and the directional set in `Sample` and `Pdf` (design decision 1). Add gtest cases for:
  - range cutoff and default range;
  - smoothstep at t = 0.25 and linear by default;
  - equal cones giving a hard edge and no NaN, and inverted cones in linear mode unchanged;
  - a non-casting light behind an opaque blocker and behind a 128/255 card;
  - out-of-range lights never sampled with one ranged light and one sun, and `Sample`/`Pdf` agreeing at in-range and out-of-range points.

  Verify that the full sh-baker suite passes.
- [ ] 2.2 Add `BakeConfig::split_punctual_direct`, with `BakeResult::punctual_direct_sh_texture` and `punctual_direct_irradiance`: area-light-only estimation at the point, and the deterministic per-light pass. Add gtests:
  - split plus punctual equals a full bake within 5% L0 at 1024 samples;
  - exact I/r² irradiance under a point light;
  - punctual SH and irradiance bit-identical at 16 and 1024 samples;
  - skipped points keep the markers;
  - a defaults-unchanged test comparing a fixed scene's results bit for bit with the behaviour before the change.

  Verify that the suite passes and the CLI's output on its test scene is unchanged.
- [ ] 2.3 Show the user the diff and ask before pushing the branch and opening the PR against `github.com/DaviesX/sh-baker`. Once the user merges it, move the submodule pointer to the merged commit on `b5-shbake`, rebuild the Release library, and verify that q3map2 still builds and the stock check passes.

## 3. Side-file version 2

- [ ] 3.1 Add `SHBakeContents` to `q3map2_extension.h`, and the contents field to the writer, the reader and the header comment. In the header comment, give the full and indirect-only meanings and the SH-basis note about main plus punctual (design decision 12, spec `q3map2-sh-extension`). Update the B4 tests:
  - golden bytes, now 148;
  - empty file, now 28 bytes;
  - version tests, now 1 and 3;
  - huge-counts file, now 28 bytes.

  Add tests for contents round-trip, an unknown contents value refused on write, and bad contents rejected on read. Verify that `make test-q3map2-extension` passes and that q3map2 builds.

## 4. Tested core: `light_sh_model`

- [ ] 4.1 Add `light_sh_model.{h,cpp}`, with a header comment stating each rule and its source in the editor or gamepack README. Add `light_sh_model_test.cpp` and the `make test-q3map2-shbake-model` target, built from the module and test alone. Verify that the target builds and runs with a placeholder check.
- [ ] 4.2 Implement the light conversions of design decision 9. Add a test for each:
  - each default, and each fallback for unparsable or out-of-range `radius`, `cone` and `cone_inner`;
  - the 89.9° clamp and inner ≤ outer;
  - `angles` with positive pitch looking down, and the default +X;
  - `angle`, including −1 and −2;
  - target direction;
  - `_color` normalised (`0.5 0.5 0.5` equals white; all zero is white);
  - `_shadows`, and point lights never casting;
  - the 50000-flux light giving 50000/(4π·64²) at 64 units.

  Verify that the tests pass.
- [ ] 4.3 Implement the caster rule, base-colour and emissive derivation, and the all-stages-additive test over a stage struct, ported from `deriveQuake3` and `Shadow_materialCasts`. Add tests from the `pbr-material-format` and `pbr-shadow-maps` spec scenarios:
  - sky, fog, liquids, clips, areaportal, noshadows, trigger and hint;
  - trans with and without alphashadow;
  - caulk;
  - an alpha-tested fence;
  - the lightmap-multiplied base before and after the lightmap stage;
  - additive emissive with `rgbGen const`;
  - environment-mapped stages skipped;
  - an opaque base with an additive glow being not additive.

  Verify that the tests pass.
- [ ] 4.4 Implement SH → irradiance, `ShLightmapBytes` (Hable with the preview's constants and white point, sRGB, ×63.75), the grid fit with the engine model (design decision 6), the alpha rewrite for `GT0`, `LT128` and `GE128`, and the single-tile UV test. Add tests:
  - constant-radiance SH gives E = π · L;
  - the midtone bytes 37 and 29, and the white point 64;
  - one sun gives a grid direction exact to 1e-4, D = E⊥, and ambient only from the main SH;
  - a sub-floor ambient (the lit face uses the realised ambient, not the target), the ambient ceiling, the per-channel clamp to 63, and the ambient-1 rule;
  - each alpha function at its threshold;
  - tiled and overlapping UVs rejected.

  Verify that the tests pass.

## 5. q3map2 plumbing (no lighting change yet)

- [ ] 5.1 Extend `shaderInfo_t` and `ParseShaderFile` (design decision 8): the literal surfaceparm flags, the stage list fed to `light_sh_model`, and the `qer_pbr_*` values. Leave every existing field as it was. Verify that q3map2 builds and the byte-identity check holds.
- [ ] 5.2 Add the `SH_BAKER=1` Makefile logic and the stub (design decision 13). Add `-shbake`, `-shindirect`, `-shsamples`, `-shbounces` and `-shdumpl0` to `LightMain`, with the option classification of design decision 14 (refusals, including a used shader's `q3map_lightmapSize`, and one ignored-options warning), and the stub's `SH_BAKER=1` message. Cross-check the table against every `takeArg` in `LightMain`, and fail the task if any option is unclassified. Document the options in `help.cpp`. Verify:
  - both builds succeed;
  - each refused cause in the spec (`-deluxe`, its alias `-deluxemap`, `-trisoup`, `-external`, `-lightmapdir`, `-lightmapsize 256`, `-extlmhacksize`, `-bounce 1`, `-approx 1`, `-fill`, `-nolm`, `-debug`, a scratch shader with `q3map_lightmapSize 256 256`) exits naming the cause;
  - `-shsamples 0` is refused;
  - the stub's message names `SH_BAKER=1`;
  - the byte-identity check holds.
- [ ] 5.3 Add the SH buffers to `rawLightmap_t` and `outLightmap_t`, allocated only under `-shbake` (design decision 4). Carry them through the super-sample average, the BSP-level neighbour fill (averaging SH with the colour's neighbours, not zeroing it as the deluxel does), wrap and page placement. Under `-shbake`, skip collapse and solid stamping, zero `minLight`, `minVertexLight`, `minGridLight` and every `ambientColor`, and route `ColorToBytes` to `ShLightmapBytes`. Verify that q3map2 builds and the byte-identity check holds.

## 6. The `-shbake` stage

- [ ] 6.1 In `light_sh.cpp`, build the scene in scaled space (design decisions 2, 7 and 9):
  - casting and emitting draw surfaces, without `_skybox` copies or `MST_FOLIAGE` instances;
  - tessellated patches;
  - Lambert materials with the alpha rewrite;
  - hulls, with the model origin and `noshader`-plus-degenerate bevel detection;
  - area lights, with the CDF only for single-tile UVs;
  - the three light types, with `flux` set;
  - warnings for every unsupported input, including worldspawn `_minlight`, `_maxlight` and the rest, `_ambient` on a `func_group`, and extra suns.

  Print the counts. Verify on the benchmark that the counts are plausible and the light tree holds one light per entity. On scratch maps, verify that the warnings fire for `q3map_surfaceLight`, a second sun and `_minlight 64`, and that `_minlight` leaves the lightmap byte-identical.
- [ ] 6.2 Build the point list (super-luxels, grid pairs, vertices; design decision 10). Run one `BakeSHLightMap` call with the split option and `Sys_Printf` progress. Write:
  - irradiance (punctual exact plus main convolved) and SH into the super buffers;
  - vertex luxels;
  - grid bytes.

  Then run the neighbour flood, and wire `LightWorld`'s `-shbake` branch (design decision 3). Verify on the benchmark:
  - `-light -shbake` completes, prints point counts, and progress reaches 100%;
  - no black luxels along the courtyard seams;
  - `-threads 1` and the default thread count give byte-identical BSPs;
  - `-shsamples 16` logs 16 samples;
  - `-samples 3 -patchshadows -gridscale 2` warns once and gives a byte-identical BSP;
  - the engine loads the BSP without warnings.
- [ ] 6.3 Confirm the grid engine model and constants (`r_ambientScale`, `r_directedScale`, the `identityLight`·32 floor, the load-time shift, the zero-ambient skip) from the fork's `renderergl1` source, and record opengl2's version. Set any constant that differs in `light_sh_model` and its tests. Verify with a stock-check screenshot that a model in the benchmark is lit like the lightmapped wall beside it on opengl1. Record the constants and the opengl2 differences (shift by 1, ×2 except `rgbGen vertex`, auto-exposure) in the gamepack README.
- [ ] 6.4 Write the side file after `StoreSurfaceLightmaps`: fill the remaining −1 luxels with 0, write the full or indirect contents, the un-shrunk hull mesh in map units, and the `-shdumpl0` dumps. A writer failure goes to `Error()`. Verify on the benchmark:
  - the page count and sizes match the BSP's lightmaps;
  - a planar indirect-only luxel's byte equals the encoding of its side-file irradiance within one byte at `-super 1`;
  - `-shindirect` gives a byte-identical BSP and an indirect-only file with lower L0 in a spot's beam;
  - `-shdumpl0` writes `bench_l0_0000.hdr` onwards;
  - with `-threads 1` and the default thread count, the side files are byte-identical;
  - no `lm_*.hdr` exists.
- [ ] 6.5 Verify the scene-level spec scenarios on scratch maps:
  - point-light irradiance and radius cutoff;
  - the sun at 60° giving half its irradiance;
  - the sky ceiling passing the sun;
  - trans glass passing a spot;
  - the opaque glowing sign occluding;
  - the T-junction seam not leaking;
  - a sky brush making no hull;
  - the emissive sign brightening the wall;
  - a vertex-lit model lit;
  - the single-sun grid direction and ambient.

  Record the results for the PR description.

## 7. Workflow switch and docs

- [ ] 7.1 Switch the stock check's light stage to `-light -shbake` (spec `stock-check`):
  - delete stale side files and dumps from the copied mod before compiling;
  - fail on a missing side file;
  - pack the side file.

  Update `tools/stockcheck/README.md` with the `SH_BAKER=1` and Release sh-baker build steps. Verify that the stock check passes on the benchmark, the pk3 holds the side file and no `.hdr`, a stub-built q3map2 fails with the `SH_BAKER=1` message, and a planted stale side file doesn't mask a missing one.
- [ ] 7.2 Switch the overlay's Town build group to `-light -shbake`. Update the gamepack README:
  - the build table row;
  - the encoding (tonemap, constants, opengl1 calibration, opengl2 differences) and grid constants;
  - the `_color` normalisation as the editor does it;
  - the warned gaps;
  - the Lambert-only bake;
  - the bake options.

  Verify that the menu differs from the downloaded one only by the group and its separator, and that the group compiles the benchmark from the editor.
- [ ] 7.3 Update `CLAUDE.md`: tick B5, and change "The vanilla lightmap" rule to say sh-baker writes it. Update plan §4.3 and §Units: the split and exact-irradiance results, the scaled bake space and its threshold caveats, the tonemapped encoding, side-file version 2, and the recorded gaps. Rebuild the PDF with `latexmk -pdf`. Verify that it builds without new warnings, and don't commit the aux files.

## 8. Integration

- [ ] 8.1 Run the full gate on `b5-shbake`:
  - both builds;
  - both test targets;
  - the stock check on a clean `git archive` copy, where all three rows pass and the pk3 contents match `benchmark-map`'s packaging requirement;
  - `openspec validate q3map2-shbake --strict`;
  - `sh-baker` at the merged PR commit;
  - the placeholder-path byte-identity check with the `SH_BAKER=1` build.

  Take the stock check's opengl1 and opengl2 screenshots of the documented views for the user to review. Then ask before pushing `b5-shbake` and opening the netradiant PR on `DaviesX/netradiant-custom`.
