# Proposal

## Why

B5 bakes the town's lighting with sh-baker inside q3map2. The plan (rev. 6, §4.2 item 1) had sh-baker rasterize q3map2's lightmap pages itself, using multi-page UV layouts. That was dropped for three reasons:
- **xatlas never emits several pages here.** As sh-baker calls it, `texelsPerUnit` is 0, so xatlas grows one atlas without limit (`xatlas.cpp:8289`).
- **The rasterizer would be off by up to half a luxel.** It snaps vertices to whole texels, while q3map2 puts lightmap coordinates on luxel centres (`lightmaps_ydnar.cpp:732`). It also writes past the edge of non-square pages.
- **q3map2 already has every luxel's sample point.** `MapRawLightmap` fills each raw lightmap with per-luxel origins, normals and a mapped flag (`superOrigins`, `superNormals`, `superClusters`) before `IlluminateRawLightmap` runs (`light.cpp:2007`, `:2031`).

The bake reads only a point's position, its normal, a tangent used solely to orient the sampling hemisphere, and a validity flag (`sensor.cpp:85–99`, `baker.cpp:142`). q3map2 can supply all of these. So q3map2 supplies the points and sh-baker only bakes them. That leaves no rasterizer work, no page model and no edge guessing in sh-baker.

sh-baker still needs two things from q3map2 for tracing:
- geometry it can trust: normals, tangents, valid indices, and no degenerate triangles;
- the surfaces' materials, including Quake 3 stage stacks such as alpha-tested fences and additive flames.

Today the only producer is the glTF loader, and `Material` drags tinygltf into every header through a verbatim `tinygltf::Value`. This change, task B2 rescoped, is the sh-baker work that gives q3map2 a clean input contract.

## What Changes

All sh-baker code changes are in the `tools/sh-baker` submodule. They go to `github.com/DaviesX/sh-baker` as seven small stacked PRs, each based on the previous one, in this order: fixtures; the tangent move; the Embree declarations; the type moves; the owned layer model; the q3map2 loader; the point bake and docs. Each is opened as soon as it passes its checks, so the user reviews one while the next is implemented. The user merges them. Every PR keeps the CLI output byte-identical on its own.

- **Owned material-layer model.** The Quake 3 stage stack becomes sh-baker types in `material.h`: the blend, rgbGen, wave, tcMod, surface-blend and cull enums, plus a per-layer struct with texture source paths, animation, blend factors, rgbGen and tcMods with all their parameters. These replace the verbatim `tinygltf::Value` in `material_layers.h`. `Material` moves into `material.h` with them, and `Texture` moves to a new `texture.h` to break the include cycle. The glTF loader deserializes `SH_material_layers` into these types, and the saver serializes them back with the exporter's exact key set and value types. After this, `scene.h`'s include closure no longer reaches tinygltf.
- **q3map2 loader.** A new translation unit `loader_q3map2.{h,cpp}`, separate from the glTF loader. It takes one q3map2 draw surface (world-space positions, normals, texture UVs, indices, material) and appends a geometry for tracing, flipping q3map2's clockwise winding to sh-baker's counter-clockwise. Input only a caller bug or corrupt data can produce (count, index, material or non-finite faults) fails a `CHECK` naming the field. What real content produces is repaired: zero normals from degenerate patches get their face normals. Like the glTF loader, it drops degenerate triangles and generates tangents. Its header builds under q3map2's `-fno-exceptions -fno-rtti` and does not reach tinygltf. B5 extends this TU with materials from shaders and with lights.
- **Embree out of the public headers.** `scene.h` declares the two opaque Embree handles (`RTCDevice`, `RTCScene`) instead of including `<embree4/rtcore.h>`. With the owned material model, the headers q3map2 includes (`loader_q3map2.h`, `baker.h`) then need only `src` and Eigen, as plan §4.3 says.
- **Solid hulls as occluders.** q3map2's draw surfaces are thin, but the brushes behind them are solid. B5 adds each casting brush's hull, shrunk slightly inward, as a material-less surface. It blocks light and gets no chart, so light cannot leak through cracks between thin surfaces. This change only pins down, with a test, that material-less surfaces block light without shading. B5 also writes the hulls into the side file, so `renderer_sh`'s shadow maps use the same solid casters (C5).
- **Assembly order.** All surfaces are added before area lights and the BVH. Adding a surface to a scene that already has a light pointing at geometry fails a `CHECK`.
- **Shared tangents.** The MikkTSpace glue and the fallback tangents move unchanged from `loader.cpp` into `tangents.{h,cpp}`, so both loaders produce tangents the same way.
- **Baking caller-supplied points.** The library documents and tests that `BakeSHLightMap` bakes any list of sample points in one call, laid out as N×1, with results in input order and no rasterization. This is how B5 bakes every q3map2 luxel with one BVH.
- **CLI unchanged.** On the same scene and arguments, `sh_baker_main` writes byte-identical outputs, including for a scene that carries `SH_material_layers`. The existing tests pass, except the two pass-through tests, whose setup changes but whose assertions on the saved glTF stay.
- **Plan correction.** In this repo, the plan (§4.2 and §4.3, plus matching lines in §0.2, §4.1, §3.5 and the dependency summary) and `CLAUDE.md` (B2–B5 and C5) are updated:
  - q3map2 supplies the luxel points;
  - B5 runs inside `-light`, replacing `IlluminateRawLightmap`'s math, because the page layout is fixed only after lighting (`StoreSurfaceLightmaps` deduplicates and approximates by lit value);
  - B3's per-page session shrinks to progress and cancel callbacks;
  - the multi-page layout is gone;
  - B5 adds solid brush hulls as occluders and writes them into the side file, and C5 loads them from there instead of rebuilding hulls;
  - a new task, B2b, adds a separate sh-baker PR making NEE shadow rays honour alpha. It is kept out of this change because it changes bake output.
- **BREAKING (library API only):** `MaterialLayers::extension` and `::texture_paths` are replaced by the owned fields, and `Material`/`Texture` move headers. `scene.h` still includes both, so code that includes `scene.h` keeps compiling unless it used the removed fields. The only such code is `saver_test.cpp`. The glTF files sh-baker reads and writes do not change.

## Capabilities

### New Capabilities

- `sh-baker-material-layers`: sh-baker's own, format-independent model of a material's Quake 3 stage stack, and how the glTF loader and saver translate it to and from `SH_material_layers` without loss.
- `sh-baker-q3map2-loader`: the contract by which q3map2 hands its scene to sh-baker. In this change that covers the surface input and its winding, aborting on malformed input, repairing normals, degenerate-triangle removal, tangents, material-less occluders, the scene assembly order, a header usable under q3map2's flags, and baking caller-supplied points. B5 extends it with materials and lights.

### Modified Capabilities

None. The existing capabilities cover the editor, gamepack, content and stock check, not sh-baker.

## Impact

- Code (submodule `tools/sh-baker`, on the stack's branches):
  - `src/texture.h` (new): `Texture`, `Texture32F` and `Texture32I`, moved from `src/scene.h`;
  - `src/material.h`: `Material` (moved from `src/scene.h`) and the layer types, moved from `src/layer_composite.h` (enums) and `src/material_layers.h` (deleted);
  - `src/loader.cpp`, `src/saver.cpp`: deserialize and serialize `SH_material_layers` through the owned types;
  - `src/tangents.h`, `src/tangents.cpp` (new): code moved from `src/loader.cpp`;
  - `src/loader_q3map2.h`, `src/loader_q3map2.cpp` (new);
  - `src/baker.h`: a doc comment on baking caller-supplied points;
  - `CMakeLists.txt`: the new sources, headers and test file;
  - `data/layers/` (new): a small glTF fixture carrying `SH_material_layers` with every field kind;
  - `data/notangent/` (new): the box without `TANGENT`, plus a material-less primitive without `TANGENT` and `TEXCOORD_0`, so the byte-identity check exercises the moved tangent code;
  - `src/scene.h`: the Embree handle declarations, plus `#include <embree4/rtcore.h>` in the `.cpp` files that call Embree;
  - tests: `src/loader_q3map2_test.cpp` (new). The two pass-through tests in `src/saver_test.cpp` are rewritten. New cases go in `src/loader_test.cpp`, `src/saver_test.cpp` and `src/baker_test.cpp`;
  - `README.md`: a short library note.
- This repo, in two more PRs on `master`, which already holds the proposal (PR #1): the plan PR and the landing PR:
  - `docs/pbr-plan/pbr-plan.tex` and its PDF;
  - `CLAUDE.md` (B2–B5 and C5 wording and the new B2b now, and the B2 tick after merge);
  - the submodule pointer after merge;
  - at landing, this change archived and both capabilities added to `openspec/specs/`.
- Unaffected:
  - `main.cpp` and the CLI's output, which stays byte-identical;
  - the rasterizer, xatlas atlasing, the baker's code, the tracer, the light tree, the visualizer and `sh_cvt`;
  - sh-renderer, which reads an unchanged `SH_material_layers`;
  - q3map2 and the editor.
- Dependencies: none new.
