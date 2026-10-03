# Tasks

Paths in groups 1–6 are relative to `tools/sh-baker`, except where a task says "this repo". Run sh-baker commands from `tools/sh-baker`. Tests read `data/` relative to the current directory.

**Byte-identity rule.** Several tasks compare CLI outputs against the baseline. Any difference stops the work. Find which task introduced it, measure it numerically, and check whether codegen alone (LTO inlining or FMA contraction under `-march=native`) explains it. Then report the evidence to the user before continuing. Never accept a difference silently or loosen a comparison.

## 1. Baseline and plan correction (before any code change)

- [ ] 1.1 In `tools/sh-baker`, check out a branch `q3map2-input` from `86d7239`. Configure and build the baseline:
  - `cmake -DCMAKE_BUILD_TYPE=Release -DSH_BAKER_BUILD_VISUALIZER=OFF -B build/baseline -S .`
  - `cmake --build build/baseline --parallel 12`

  If FetchContent cannot reach the network, add `-DFETCHCONTENT_SOURCE_DIR_XATLAS=<abs>/ioq3-custom/sh-baker/deps/xatlas-src -DFETCHCONTENT_SOURCE_DIR_MIKKTSPACE=<abs>/ioq3-custom/sh-baker/deps/mikktspace-src`, and record that the flags were used. Run `./build/baseline/sh_baker_test > build/baseline/tests.txt 2>&1`. Verify:
  - `git status` in the submodule is clean, because `build/` is ignored;
  - `tests.txt` lists every test with its result. Note the pass and fail counts and any failing test names under this task as a *Done* line.
- [ ] 1.2 Add two fixtures (committed in the PR). The first is `data/layers/`. It is a copy of `data/box/scene.gltf` with three small PNGs, carrying an exporter-shaped `SH_material_layers`, written by a short script kept beside it as `data/layers/make_fixture.py`. It must hold:
  - one opaque two-layer material (`baseLayer` 1, `cullMode` `NONE`, `surfaceBlend` `BLEND`);
  - one layer with every tcMod kind and a `WAVE` rgbGen;
  - one animated layer whose frame 0 is its texture;
  - one `WAVE` rgbGen without `func`;
  - one additive material (every `blendDst` `ONE`, `surfaceBlend` `ADD`) with an animated layer and `KHR_materials_emissive_strength` above 0, on a primitive the bake sees, so the emissive compositor's output reaches the lightmap.

  Every non-index number must be written as a decimal literal (`30.0`, not `30`) that is exactly representable as a float, as the exporter writes them. tinygltf keeps JSON integers as integers, and the owned model writes doubles.

  The second is `data/notangent/`, generated the same way by `data/notangent/make_fixture.py`. It is the box with its primitive's `TANGENT` removed, plus a second node, offset beside the box, whose primitive has no material, no `TANGENT` and no `TEXCOORD_0`. Verify:
  - `./build/baseline/sh_baker_main --input data/<fixture>/scene.gltf --output build/baseline/out-<fixture>-0 --width 256 --samples 8 --dilation 4` exits 0 for both fixtures;
  - the `layers` output `scene.gltf` carries `SH_material_layers` on both materials, and its lightmap differs from a bake of the same fixture with the additive material's emissive strength set to 0, which shows the emissive composite reaches the output;
  - the `notangent` output `scene.gltf` has `TANGENT` on both primitives, which shows the loader generated them through MikkTSpace and the fallback.
- [ ] 1.3 Record the baseline CLI outputs. For each scene `box`, `MultiLights`, `layers` and `notangent`, run twice: `./build/baseline/sh_baker_main --input data/<scene>/scene.gltf --output build/baseline/out-<scene>-<run> --width 256 --samples 32 --dilation 4`. Verify that `diff -r` of run 1 against run 2 is empty for every scene. If any differ, stop and ask the user how outputs should be compared (design, Risks); do not go on with a weaker check.
- [ ] 1.4 In this repo, bring the plan in line with the decision that q3map2 supplies the luxel points (design, first decision):
  - In `docs/pbr-plan/pbr-plan.tex` §4.2:
    - item 1 becomes "q3map2 input contract": the owned material-layer model (no tinygltf in the headers q3map2 includes), the q3map2 loader TU (checked tracing geometry, shared MikkTSpace tangents), and baking caller-supplied points in one call. Drop the multi-page UV layout and the `atlasCount > 1` claim, and say why: xatlas never emits several atlases here, and q3map2 owns the luxel mapping;
    - item 2 becomes progress and cancel callbacks on that single call, since one call already builds the BVH once;
    - item 3 says SH comes back per luxel and q3map2 assembles the pages.
  - In §4.3:
    - **Placement:** `-shbake` runs inside `-light`, after `MapRawLightmap`, in place of `IlluminateRawLightmap`'s math (and the grid and vertex passes). `StoreSurfaceLightmaps` packs the result.
    - **Scene:** surfaces go through the q3map2 loader.
    - **Bake and write:** one call for all mapped luxels. The SH side data must follow `StoreSurfaceLightmaps`' approximation, solid stamps and deduplication, or those must be disabled when baking SH.
    - Also record the units note: the sensor's `tnear` is in metres.
    - **Solid hulls:** the occluders are one closed hull per casting brush, structural and detail, built from the BSP brush lumps with q3map2's winding code. Each plane is pushed inward by ε > `tnear`, bevel sides are skipped, and each hull is added as a material-less surface. The draw surfaces stay the receivers and own the luxels. Brushless surfaces (patches, models, foliage) stay thin. B5 writes the un-shrunk hulls into the side file.
    - **Anti-aliasing:** it comes from `-super N`. Each super-luxel is baked as a point, and `StoreSurfaceLightmaps` averages them, SH included. `-samples`' adaptive subsampling is part of the replaced lighting and is not available.
  - In §4.1, replace "bakes SH into the lightmap pages it has already allocated, in a new `-shbake` stage" with the bake inside `-light` on q3map2's own luxels.
  - In §0.2, reword "`-light` gives a placeholder lightmap and allocates the lightmap pages; `-shbake` (4.3) replaces its contents" to say `-light -shbake` (4.3) replaces the lighting.
  - In §"The prior attempt", replace "fails unless everything fits one page" with "always builds a single atlas, whose size xatlas picks near the requested width".
  - In §3.5, replace "hull reconstruction" from the brush lumps with loading the hull mesh q3map2 writes into the side file, so the bake and the shadow maps use one solid set. Add the hull mesh to §4.2's side-file outputs.
  - Update the dependency-summary rows for 3.5, 4.2 and 4.3. C5 now needs B5's side file.
  - In §4.3's build bullet, say that the headers q3map2 includes (`loader_q3map2.h`, `baker.h`) need only `src` and Eigen, because sh-baker declares the Embree handles itself.
  - Add a §4.2 item and a Track B task **B2b**: an sh-baker PR making NEE shadow rays honour alpha the way the tracer's indirect rays do, so fences and leaf cards cast through their cut-outs. It is needed by B5 and changes bake output.
  - In `CLAUDE.md`:
    - reword B2 (q3map2 input contract), B3 (progress and cancel callbacks) and B5 (inside `-light`, q3map2 supplies luxel points), and add B2b to B5's `needs:`;
    - note in B4 that the per-page SH layout is assembled by q3map2 from per-luxel results, and that the side file also carries the solid hull mesh;
    - reword C5 to load the hull mesh from the side file instead of reconstructing hulls, with `needs: B5`;
    - add B2b.

  Rebuild with `latexmk -pdf` in `docs/pbr-plan/`. Verify:
  - the PDF builds without errors;
  - `grep -n 'atlasCount\|multi-page\|Multi-page\|pages it has already allocated' docs/pbr-plan/pbr-plan.tex CLAUDE.md` finds hits only inside the "What changed in revision N" sections, plus one new history note saying the multi-page layout was dropped;
  - the B2 line in `CLAUDE.md` names the owned material layers, the q3map2 loader and the point bake.

  Commit when the user asks.

## 2. Owned material-layer model

- [ ] 2.1 Move types without changing them (design, "The layer stack is an sh-baker-owned model"):
  - create `src/texture.h` with `Texture`, `Texture32F` and `Texture32I` moved from `src/scene.h`;
  - move `Material` from `src/scene.h` into `src/material.h`, which includes `texture.h` and `material_layers.h` and no longer includes `scene.h`;
  - move the layer enums and `RgbGen`/`TcMod` unchanged from `src/layer_composite.h` into `src/material.h`, and make `layer_composite.h` include `material.h`;
  - make `src/scene.h` include `material.h`;
  - update `CMakeLists.txt`'s header list.

  Configure the working build with `cmake -DCMAKE_BUILD_TYPE=Release -B build -S .`, adding the same `FETCHCONTENT_SOURCE_DIR_*` flags as 1.1 if the baseline used them, and build with `cmake --build build --parallel 12`. Verify:
  - every target builds;
  - `./build/sh_baker_test` gives the same results as `build/baseline/tests.txt`;
  - the 1.3 command for `box` into `build/out-box` gives empty `diff -r` against `build/baseline/out-box-1`.
- [ ] 2.2 Replace the verbatim carrier with the owned model in one step, so every target keeps building:
  - in `src/material.h`, extend `RgbGen` with an absent wave function, add `WaveType::kNone`, and give `TcMod` values for every type plus a `wave`, in field order `{type, values, wave}`. Add `SurfaceBlend`, `CullMode`, `MaterialLayer` and the owned `MaterialLayers`. Delete `src/material_layers.h`;
  - in `src/loader.cpp`, deserialize `SH_material_layers` once into `MaterialLayers` with full fidelity:
    - every tcMod type with its parameters, `ROTATE`'s scalar and `TURB`/`STRETCH`'s wave string;
    - `WAVE` rgbGen, with or without `func`;
    - `animFreq` and `animFrames`;
    - texture and frame paths through `ResolveTexturePath`.

    Apply the spec's defaults to unknown names with a warning. Build the compositor's `CompositeLayer`s from that model plus loaded pixels, treating an absent wave function as `SIN`;
  - in `src/saver.cpp`, replace `RemapLayerTextureIndices` with a serializer from `MaterialLayers` (design, "Saver"). It writes the exporter's keys, value types and conditional keys, calls `AddOrReuseTexture` in today's order, and keeps a missing source's slot as -1;
  - rewrite the setup of `PassesMaterialLayersThrough` and `LoaderRetainsMaterialLayers` to build the owned model, and `LoaderRetainsMaterialLayers`' checks on the in-memory model. Keep every assertion on the saved glTF.

  Verify:
  - every target builds;
  - `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3 src/scene.h | grep -c 'tiny_gltf\|json.hpp'` prints 0;
  - every pre-existing test has its baseline result;
  - `git diff 86d7239 -- src/saver_test.cpp` removes only the `tinygltf::Value` extension construction, the texture-index maps and the asserts on `extension`/`texture_paths`;
  - the 1.3 command for `layers` into `build/out-layers` gives empty `diff -r` against `build/baseline/out-layers-1`.
- [ ] 2.3 Add tests for every scenario in `specs/sh-baker-material-layers/spec.md` that pre-existing tests and the CLI comparisons do not already cover:
  - "Built in code": a test that includes only `material.h`, builds an animated layer with a `TURB` tcMod and a `WAVE` rgbGen, and checks the fields;
  - "Every tcMod kind", "Animated layer" and "WAVE without func", loading `data/layers/scene.gltf` in `src/loader_test.cpp`;
  - "Unknown blend factor", on a scratch copy with `blendSrc` edited;
  - "Round trip": load `data/layers`, save, and compare the extension `Value`s key by key and by `Value` type, ignoring texture indices but checking that they name the same image files;
  - "Frame 0 shares the layer texture" and "Unknown source", in `src/saver_test.cpp`.

  Verify: all of them pass.

## 3. Shared tangents and the q3map2 loader

- [ ] 3.1 Move the MikkTSpace callbacks, `GenerateTangents` and the fallback-basis loop verbatim from `src/loader.cpp` into new `src/tangents.h` / `src/tangents.cpp`, behind `void GenerateTangents(Geometry* geo, bool use_texture_uvs)` (design, "Tangent code moves"). `ProcessPrimitive` calls it with `!geo.texture_uvs.empty() && uv0_data != nullptr`. Add both files to `LIB_SOURCES` / `LIB_HEADERS` in `CMakeLists.txt`. Verify:
  - `grep -n 'genTangSpaceDefault\|SMikkTSpace' src/loader.cpp` is empty;
  - every pre-existing loader test passes;
  - running the 1.3 command for `box` and `notangent` with `./build/sh_baker_main` into `build/out-<scene>` gives output where `diff -r` against `build/baseline/out-<scene>-1` is empty for both.
- [ ] 3.2 In `src/scene.h`, replace `#include <embree4/rtcore.h>` with the two handle typedefs at global scope (design, "Embree handles are declared in `scene.h`"). Add `#include <embree4/rtcore.h>` to every `.cpp` that calls `rtc*` and no longer compiles. Verify:
  - every target builds;
  - `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3 src/scene.h src/baker.h | grep -c 'embree4/'` prints 0;
  - the 1.3 `box` comparison is still empty.
- [ ] 3.3 Add `src/loader_q3map2.h` / `src/loader_q3map2.cpp` with `Q3Map2Surface` and `AddQ3Map2Surface` (design, "A new TU for q3map2"), and add them to `CMakeLists.txt`. It `CHECK`s each case of the spec's "Malformed input aborts", and that no light in the scene has a geometry pointer (design, "Scene assembly order is checked"), with messages that name the field and value. Then, in order:
  1. swap each triangle's second and third index (q3map2's clockwise to counter-clockwise);
  2. normalize the normals, repairing short ones from the flipped triangles' face normals (design, "A new TU for q3map2");
  3. fill zero texture UVs for an occluder without them;
  4. drop triangles that repeat an index;
  5. call `GenerateTangents(&geo, !surface.texture_uvs.empty())`;
  6. append the geometry.

  The header comment states the contract: which inputs abort, which are repaired, the assembly order (all surfaces, then lights, then the BVH), and the tangent caveat at shared seam vertices. Add `src/loader_q3map2_test.cpp` to the test target, with one test per scenario in `specs/sh-baker-q3map2-loader/spec.md`, except the point bake (4.1). The aborting cases are `EXPECT_DEATH` tests, matched on the message and run with `GTEST_FLAG_SET(death_test_style, "threadsafe")`, because TBB may have started threads:
  - valid lit surface;
  - flipped triangles;
  - occluder without UVs;
  - index out of range;
  - unknown material;
  - lit surface without texture UVs;
  - non-finite texture UV;
  - surface after an area light;
  - tiling UVs;
  - normals normalized;
  - zero normal repaired, with a quad wound clockwise as seen from +Z;
  - repeated index;
  - tangents follow U;
  - fallback tangents for an occluder.

  Verify: all of them pass.
- [ ] 3.4 Check the headers against q3map2's flags:
  - `printf '#include "loader_q3map2.h"\n#include "baker.h"\n' | g++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -Isrc -I/usr/include/eigen3 -x c++ -`;
  - `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3` on `src/loader_q3map2.h` and on `src/baker.h`.

  Verify: the first command exits 0, and the second's output contains none of `tiny_gltf.h`, `json.hpp`, `embree4/` or `loader.h`. `-M` is used rather than `-MM`, which hides system headers such as `/usr/include/tiny_gltf.h`.

## 4. Baking caller-supplied points

- [ ] 4.1 Add a comment to `BakeSHLightMap` in `src/baker.h` saying that it bakes any list of points laid out as N×1 with supplied `position`, `normal`, a unit `tangent` perpendicular to the normal with `w` of +1 or -1 (it only orients the hemisphere, and `w = 0` flattens it) and `material_id ≥ 0`. Results come back at the points' indices, and invalid points keep the not-baked marker. Add the spec scenarios "Points from different surfaces in one call" and "Hull behind a crack" to `src/baker_test.cpp`. Build the points through `AddQ3Map2Surface` geometry plus hand-made `SurfacePoint`s, with `bounces = 0`. Verify: the test passes with no change to `src/baker.cpp`. If it needs one, stop and report why.

## 5. Docs

- [ ] 5.1 Add a short "Library: q3map2 input" section to `README.md`. It covers:
  - `MaterialLayers` as the owned form of `SH_material_layers`;
  - `AddQ3Map2Surface`: what it repairs, what aborts, and the assembly order;
  - that the headers q3map2 includes need only `src` and Eigen;
  - baking caller-supplied points as N×1;
  - that the CLI and glTF output are unchanged.

  Verify: every function, type and field the section names exists (`grep` in `src/*.h`), and the CLI section of the README is unchanged.

## 6. Integration and delivery

- [ ] 6.1 Build `build/` with default options, so the visualizer and `sh_cvt` build too. Run `./build/sh_baker_test > build/tests.txt 2>&1`, and run the 1.3 commands for every scene with `./build/sh_baker_main` into `build/out-<scene>`. Verify:
  - every test listed in `build/baseline/tests.txt` has the same result;
  - every new test passes;
  - `git diff -U0 86d7239 -- 'src/*_test.cpp' ':!src/saver_test.cpp' | grep '^-[^-]'` is empty, so no other pre-existing test line was changed or deleted (`saver_test.cpp` is checked in 2.2);
  - `diff -r build/out-<scene> build/baseline/out-<scene>-1` is empty for every scene.
- [ ] 6.2 Commit on `q3map2-input` with a message that cites B2, and end it with the attribution line. Then ask the user before pushing the branch and before opening the PR to `DaviesX/sh-baker`. The PR description covers:
  - why q3map2 supplies the luxel points, and so why there is no page model or rasterizer change;
  - the owned material-layer model and the moved `Material`/`Texture` headers;
  - the q3map2 loader contract and the tangent move;
  - the Embree handle declarations in `scene.h`;
  - the two new fixtures;
  - the point bake;
  - the 6.1 evidence that test results and outputs are unchanged.

  Verify: the PR exists and links the branch, or the user has said not to open it yet.
- [ ] 6.3 After the user merges the PR, in this repo:
  - point `tools/sh-baker` at the merged commit;
  - tick B2 in `CLAUDE.md`, and mark plan §4.2 item 1 done, rebuilding the PDF;
  - run the stock check, which every task's exit criterion includes: `tools/stockcheck/stockcheck.py content/benchmark bench`. The default q3map2 build does not link sh-baker, so the result must equal the last run's.

  Commit when the user asks. Verify: `git submodule status` shows the merged commit, `CLAUDE.md` shows `[x] **B2**`, and the stock check exits 0.
