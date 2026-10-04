# Tasks

Paths are relative to `tools/sh-baker`, except where a task says "this repo". Run sh-baker commands from `tools/sh-baker`. Tests read `data/` relative to the current directory.

**Delivery.** The sh-baker work goes to `DaviesX/sh-baker` as seven stacked PRs (design, "Development and delivery"). Each PR's branch is made from the previous PR's branch, and PR 1's from `$BASE`. `$BASE` is the stack's base commit: `86d7239`, sh-baker's `main` when the stack was planned. It changes only under "If `main` moves", which records the new value under 1.1.

| PR | Branch | Group |
|----|--------|-------|
| 1/7 | `q3map2-input/1-fixtures` | 3 |
| 2/7 | `q3map2-input/2-tangents` | 4 |
| 3/7 | `q3map2-input/3-embree-handles` | 5 |
| 4/7 | `q3map2-input/4-move-types` | 6 |
| 5/7 | `q3map2-input/5-owned-layers` | 7 |
| 6/7 | `q3map2-input/6-q3map2-loader` | 8 |
| 7/7 | `q3map2-input/7-point-bake` | 9 |

This change's proposal is already on this repo's `master`: PR #1 was squash-merged as `3ba13443`. This repo gets two more PRs to `DaviesX/netradiant-custom`:
- the plan PR (group 2), which also carries this revision of the change's artifacts;
- the landing PR (group 10).

On 2026-10-03 the user authorized pushing each branch and opening its PR as soon as its group verifies, without asking each time. The user reviews each PR while the next one is implemented, and the user merges. Never merge a PR, and never push to `main` or `master`. Never push to `origin` in this repo, which is upstream Garux; this repo's remote is `fork`.

**Gate.** Every sh-baker PR passes these checks on its committed branch before it is pushed, on top of its group's own checks. Run every check that pipes `g++` into `grep` under `set -o pipefail`, so a failed compile fails the check instead of printing 0.
- Every target builds in `build/` (default options, so the visualizer and `sh_cvt` build too).
- `./build/sh_baker_test > build/tests.txt 2>&1` gives every test listed in `build/baseline/tests.txt` the same result, and every new test passes.
- For each scene `box`, `MultiLights`, `layers` and `notangent`, the 3.2 command run with `./build/sh_baker_main` into `build/out-<scene>` gives empty `diff -r` against `build/baseline/out-<scene>-1`.
- `git diff -U0 $BASE -- 'src/*_test.cpp' ':!src/saver_test.cpp' | grep '^-[^-]'` is empty, so no pre-existing test line was changed or deleted.
- From PR 5/7 on, `git diff $BASE -- src/saver_test.cpp` removes only the `tinygltf::Value` extension construction, the texture-index maps and the asserts on `extension`/`texture_paths` (7.1).
- `git status` shows nothing uncommitted apart from the ignored `build/`.

**Opening a PR.**
1. Commit on the group's branch with a message that cites B2 and the PR's place in the stack (for example "q3map2 input (2/7)"), ending with the attribution line.
2. Run the gate on the committed branch. Fix any failure with another commit, or amend, since the branch is not pushed yet.
3. `git push -u origin <branch>`, then `gh pr create --repo DaviesX/sh-baker --base <previous branch, or main for PR 1> --head <branch>`. Title the PR "q3map2 input (n/7): <summary>". The description says:
   - what the PR changes and why;
   - which spec requirements and scenarios it covers;
   - the gate's evidence (test counts, the four empty diffs);
   - "Stacked on #<previous>" (except PR 1);
   - that the stack implements this repo's openspec change `sh-baker-q3map2-input`.

   It ends with the generated-by line.
4. Add the branch and its pushed tip to `build/stack-tips.txt` (one `<branch> <sha>` line per branch, replacing any older line for it). Record the PR's number and tip under the task as a *Done* line.
5. Go on with the next group without waiting for review.

**Moving the stack.** Every rebase follows these steps, so commits the user added on GitHub are kept and nothing is overwritten blind:
1. `git fetch origin`. Fast-forward each open PR's local branch to `origin/<branch>` with `git merge --ff-only`. If one cannot fast-forward, stop and ask the user.
2. Rewrite `build/stack-tips.txt` with every stack branch's current tip. These are the old tips the later steps use.
3. Rebase from the top branch with `git rebase --update-refs`, which moves every branch in between with it. Re-run the gate on each moved branch.
4. Push each moved branch with `git push --force-with-lease=<branch>:<its old tip> origin <branch>`. Force-push only this stack's own branches. Then update `build/stack-tips.txt`.

**Review changes.** When the user asks for a change on PR k:
1. Do steps 1 and 2 of "Moving the stack".
2. Make the change on PR k's branch as a new commit, and run its gate.
3. Rebase the later branches with `git rebase --update-refs <PR k branch> <top branch>`, then do steps 3 and 4.
4. If the change alters a requirement or decision, update `design.md` or the spec in this repo first, and say so in the PR thread.

**After a merge.** The user squash-merges, and has stacked before: #53 went into #51's branch, then #51 into `main`. Whenever a stack PR lands on `main`:
1. Do steps 1 and 2 of "Moving the stack".
2. Take the lowest PR that is still open. Its *old base* is its base branch's tip from `build/stack-tips.txt`. That branch's PR is merged, so the tip no longer moves.
3. Check `git diff --quiet <old base> origin/main`. If it fails, `main` holds something the old base does not: either another commit landed ("If `main` moves"), or a PR was merged into a branch that never reached `main`. Stop and ask the user.
4. `git rebase --update-refs --onto origin/main <old base> <top branch>`. This drops the commits `main` already has in squashed form, including PRs merged into other PRs' branches, as #53 was.
5. Retarget the PR with `gh pr edit <number> --repo DaviesX/sh-baker --base main`.
6. Do steps 3 and 4 of "Moving the stack".

**If `main` moves.** If sh-baker's `main` gains a commit that is not one of this stack's PRs, stop and ask the user. If the user agrees to build on it:
1. Do steps 1 and 2 of "Moving the stack".
2. Rebase with `git rebase --update-refs --onto origin/main <old base> <top branch>`. The old base is that of the lowest open PR ("After a merge", step 2), or `$BASE` if PR 1 is still open.
3. Set `$BASE` to `origin/main`'s commit and record it under 1.1.
4. Rebuild `build/baseline/` from it (1.1), and re-record its outputs (3.2).
5. Re-run every open PR's gate, then do step 4 of "Moving the stack".

**This repo's PRs.** The plan PR and the landing PR follow "Moving the stack" and "After a merge" too, with `fork` for `origin`, `master` for `main` and `DaviesX/netradiant-custom` for the repo. Their tips are recorded in their *Done* lines rather than in `build/stack-tips.txt`. Their gate is their group's own checks.

**Byte-identity rule.** Any difference between the gate's CLI outputs and the baseline stops the work. Find which PR introduced it, measure it numerically, and check whether codegen alone (LTO inlining or FMA contraction under `-march=native`) explains it. Then report the evidence to the user before continuing. Never accept a difference silently, loosen a comparison, or open the PR with the difference in it.

## 1. Baseline (local, no PR)

- [x] 1.1 In `tools/sh-baker`, run `git fetch origin` and check that `origin/main` is still `86d7239`. If `main` has moved, stop and ask the user whether to base the stack on the new tip, because the baseline and the design's line references assume `86d7239`. Check out `86d7239` detached and configure and build the baseline:
  - `cmake -DCMAKE_BUILD_TYPE=Release -DSH_BAKER_BUILD_VISUALIZER=OFF -B build/baseline -S .`
  - `cmake --build build/baseline --parallel 12`

  If FetchContent cannot reach the network, add `-DFETCHCONTENT_SOURCE_DIR_XATLAS=<abs>/ioq3-custom/sh-baker/deps/xatlas-src -DFETCHCONTENT_SOURCE_DIR_MIKKTSPACE=<abs>/ioq3-custom/sh-baker/deps/mikktspace-src`, and record that the flags were used. Run `./build/baseline/sh_baker_test > build/baseline/tests.txt 2>&1`. Verify:
  - `git status` in the submodule is clean, because `build/` is ignored;
  - `tests.txt` lists every test with its result. Note the pass and fail counts and any failing test names under this task as a *Done* line.

  Configure the working build with `cmake -DCMAKE_BUILD_TYPE=Release -B build -S .`, adding the same `FETCHCONTENT_SOURCE_DIR_*` flags if the baseline used them. Every gate builds it with `cmake --build build --parallel 12`. Record `$BASE = 86d7239` as a *Done* line here, with any later value from "If `main` moves" under it.

  *Done (2026-10-03):* `origin/main` is `86d7239`, so `$BASE = 86d7239`. The `FETCHCONTENT_SOURCE_DIR_*` flags were used for both `build/baseline` and `build`, even though the network was reachable. `CMakeLists.txt` fetches xatlas and MikkTSpace at `master`, which could drift between builds. The local sources are pinned at xatlas `f700c77` and MikkTSpace `3e895b4`. Baseline tests: 101 run, 101 passed, 0 failed. Toolchain: g++ 15.2.0, cmake 4.2.3.

## 2. Plan correction (this repo, PR on `DaviesX/netradiant-custom`)

- [ ] 2.1 In this repo, start the branch and commit this revision of the change:
  1. `git fetch fork`. Fast-forward local `master` with `git fetch fork master:master`, so it holds the squash-merged proposal `3ba13443`.
  2. `git switch -c b2-plan master`. The uncommitted revision of `proposal.md`, `design.md` and `tasks.md` carries over, because `master`'s copies of the change match the local `sh-baker-q3map2-input` branch.
  3. Commit those three files as "Plan B2's delivery as seven stacked PRs", with the attribution line.
  4. Once `git diff --quiet sh-baker-q3map2-input master` confirms the squash holds all of it, delete the local `sh-baker-q3map2-input` branch.

  Verify: `git log --oneline master..b2-plan` shows one commit, touching only those three files.
- [ ] 2.2 On `b2-plan`, bring the plan in line with the decision that q3map2 supplies the luxel points (design, first decision):
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
    - reword B2 (q3map2 input contract, delivered as a stack of seven sh-baker PRs), B3 (progress and cancel callbacks) and B5 (inside `-light`, q3map2 supplies luxel points), and add B2b to B5's `needs:`;
    - note in B4 that the per-page SH layout is assembled by q3map2 from per-luxel results, and that the side file also carries the solid hull mesh;
    - reword C5 to load the hull mesh from the side file instead of reconstructing hulls, with `needs: B5`;
    - add B2b.

  Rebuild with `latexmk -pdf` in `docs/pbr-plan/`. Verify:
  - the PDF builds without errors;
  - `grep -n 'atlasCount\|multi-page\|Multi-page\|pages it has already allocated' docs/pbr-plan/pbr-plan.tex CLAUDE.md` finds hits only inside the "What changed in revision N" sections, plus one new history note saying the multi-page layout was dropped;
  - the B2 line in `CLAUDE.md` names the owned material layers, the q3map2 loader and the point bake.

  This PR changes only documents, so the stock check cannot change. It runs in group 10.
- [ ] 2.3 Commit `docs/pbr-plan/pbr-plan.tex`, `docs/pbr-plan/pbr-plan.pdf` and `CLAUDE.md` on `b2-plan`, with the attribution line. Push with `git push -u fork b2-plan`, and open the PR with `gh pr create --repo DaviesX/netradiant-custom --base master --head b2-plan`. The description has two parts: the delivery revision of the change (stacked PRs, the gate, the stack rules), and the plan sections and tasks corrected. Record its number and pushed tip as a *Done* line. Verify: the PR's file list is exactly the six files of 2.1 and this task.

## 3. PR 1/7: fixtures

- [ ] 3.1 Create the branch `q3map2-input/1-fixtures` from `$BASE`, and add two fixtures. The first is `data/layers/`. It is a copy of `data/box/scene.gltf` with three small PNGs, carrying an exporter-shaped `SH_material_layers`, written by a short script kept beside it as `data/layers/make_fixture.py`. It must hold:
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
- [ ] 3.2 Record the baseline CLI outputs. For each scene `box`, `MultiLights`, `layers` and `notangent`, run twice: `./build/baseline/sh_baker_main --input data/<scene>/scene.gltf --output build/baseline/out-<scene>-<run> --width 256 --samples 32 --dilation 4`. Verify that `diff -r` of run 1 against run 2 is empty for every scene. If any differ, stop and ask the user how outputs should be compared (design, Risks); do not go on with a weaker check.
- [ ] 3.3 Run the gate and open PR 1/7 against `main`: "Add fixtures for material layers and missing tangents". The description says which later PRs use each fixture (2/7 and 5/7) and gives the 3.1 evidence. Verify: `git diff --stat $BASE` lists only files under `data/layers/` and `data/notangent/`.

## 4. PR 2/7: shared tangents

- [ ] 4.1 Create `q3map2-input/2-tangents` from `q3map2-input/1-fixtures`. Move the MikkTSpace callbacks, `GenerateTangents` and the fallback-basis loop verbatim from `src/loader.cpp` into new `src/tangents.h` / `src/tangents.cpp`, behind `void GenerateTangents(Geometry* geo, bool use_texture_uvs)` (design, "Tangent code moves"). `ProcessPrimitive` calls it with `!geo.texture_uvs.empty() && uv0_data != nullptr`. Add both files to `LIB_SOURCES` / `LIB_HEADERS` in `CMakeLists.txt`. Verify:
  - `grep -n 'genTangSpaceDefault\|SMikkTSpace' src/loader.cpp` is empty;
  - every pre-existing loader test passes.
- [ ] 4.2 Run the gate, in which `notangent` exercises the moved code, and open PR 2/7: "Move tangent generation into tangents.{h,cpp}".

## 5. PR 3/7: Embree handles

- [ ] 5.1 Create `q3map2-input/3-embree-handles` from `q3map2-input/2-tangents`. In `src/scene.h`, replace `#include <embree4/rtcore.h>` with the two handle typedefs at global scope (design, "Embree handles are declared in `scene.h`"). Add `#include <embree4/rtcore.h>` to every `.cpp` that calls `rtc*` and no longer compiles. Verify: under `set -o pipefail`, `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3 src/scene.h src/baker.h | grep -c 'embree4/'` prints 0, and `g++` exits 0.
- [ ] 5.2 Run the gate and open PR 3/7: "Declare Embree's handles in scene.h instead of including rtcore.h".

## 6. PR 4/7: moved types

- [ ] 6.1 Create `q3map2-input/4-move-types` from `q3map2-input/3-embree-handles`. Move types without changing them (design, "The layer stack is an sh-baker-owned model"):
  - create `src/texture.h` with `Texture`, `Texture32F` and `Texture32I` moved from `src/scene.h`;
  - move `Material` from `src/scene.h` into `src/material.h`, which includes `texture.h` and `material_layers.h` and no longer includes `scene.h`;
  - move the layer enums and `RgbGen`/`TcMod` unchanged from `src/layer_composite.h` into `src/material.h`, and make `layer_composite.h` include `material.h`;
  - make `src/scene.h` include `material.h`;
  - update `CMakeLists.txt`'s header list.
- [ ] 6.2 Run the gate and open PR 4/7: "Move Material and the layer enums into material.h, and Texture into texture.h".

## 7. PR 5/7: owned material-layer model

- [ ] 7.1 Create `q3map2-input/5-owned-layers` from `q3map2-input/4-move-types`. Replace the verbatim carrier with the owned model in one commit, so every target keeps building:
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
  - under `set -o pipefail`, `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3 src/scene.h | grep -c 'tiny_gltf\|json.hpp'` prints 0, and `g++` exits 0;
  - `git diff $BASE -- src/saver_test.cpp` removes only the `tinygltf::Value` extension construction, the texture-index maps and the asserts on `extension`/`texture_paths`.
- [ ] 7.2 Add tests for every scenario in `specs/sh-baker-material-layers/spec.md` that pre-existing tests and the gate's CLI comparisons do not already cover:
  - "Built in code": a test that includes only `material.h`, builds an animated layer with a `TURB` tcMod and a `WAVE` rgbGen, and checks the fields;
  - "Every tcMod kind", "Animated layer" and "WAVE without func", loading `data/layers/scene.gltf` in `src/loader_test.cpp`;
  - "Unknown blend factor", on a scratch copy with `blendSrc` edited;
  - "Round trip": load `data/layers`, save, and compare the extension `Value`s key by key and by `Value` type, ignoring texture indices but checking that they name the same image files;
  - "Frame 0 shares the layer texture" and "Unknown source", in `src/saver_test.cpp`.

  Verify: all of them pass.
- [ ] 7.3 Run the gate, in which `layers` proves the serialization exact, and open PR 5/7: "Own the material-layer model instead of carrying tinygltf::Value". The description names the library API break: `MaterialLayers::extension` and `::texture_paths` are replaced, and the glTF files do not change.

## 8. PR 6/7: q3map2 loader

- [ ] 8.1 Create `q3map2-input/6-q3map2-loader` from `q3map2-input/5-owned-layers`. Add `src/loader_q3map2.h` / `src/loader_q3map2.cpp` with `Q3Map2Surface` and `AddQ3Map2Surface` (design, "A new TU for q3map2"), and add them to `CMakeLists.txt`. It `CHECK`s each case of the spec's "Malformed input aborts", and that no light in the scene has a geometry pointer (design, "Scene assembly order is checked"), with messages that name the field and value. Then, in order:
  1. swap each triangle's second and third index (q3map2's clockwise to counter-clockwise);
  2. normalize the normals, repairing short ones from the flipped triangles' face normals (design, "A new TU for q3map2");
  3. fill zero texture UVs for an occluder without them;
  4. drop triangles that repeat an index;
  5. call `GenerateTangents(&geo, !surface.texture_uvs.empty())`;
  6. append the geometry.

  The header comment states the contract: which inputs abort, which are repaired, the assembly order (all surfaces, then lights, then the BVH), and the tangent caveat at shared seam vertices. Add `src/loader_q3map2_test.cpp` to the test target, with one test per scenario in `specs/sh-baker-q3map2-loader/spec.md`, except the two bake scenarios (9.1). The aborting cases are `EXPECT_DEATH` tests, matched on the message and run with `GTEST_FLAG_SET(death_test_style, "threadsafe")`, because TBB may have started threads:
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
- [ ] 8.2 Check the headers against q3map2's flags:
  - `printf '#include "loader_q3map2.h"\n#include "baker.h"\n' | g++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -Isrc -I/usr/include/eigen3 -x c++ -`;
  - `g++ -std=c++20 -x c++ -M -Isrc -I/usr/include/eigen3` on `src/loader_q3map2.h` and on `src/baker.h`.

  Verify: both commands exit 0, and the second's output contains none of `tiny_gltf.h`, `json.hpp`, `embree4/` or `loader.h`. `-M` is used rather than `-MM`, which hides system headers such as `/usr/include/tiny_gltf.h`.
- [ ] 8.3 Run the gate and open PR 6/7: "Add a q3map2 surface loader". The description covers the contract (what aborts, what is repaired, the winding flip, the assembly order) and the 8.2 evidence.

## 9. PR 7/7: point bake and docs

- [ ] 9.1 Create `q3map2-input/7-point-bake` from `q3map2-input/6-q3map2-loader`. Add a comment to `BakeSHLightMap` in `src/baker.h` saying that it bakes any list of points laid out as N×1 with supplied `position`, `normal`, a unit `tangent` perpendicular to the normal with `w` of +1 or -1 (it only orients the hemisphere, and `w = 0` flattens it) and `material_id ≥ 0`. Results come back at the points' indices, and invalid points keep the not-baked marker. Add the spec scenarios "Points from different surfaces in one call" and "Hull behind a crack" to `src/baker_test.cpp`. Build the points through `AddQ3Map2Surface` geometry plus hand-made `SurfacePoint`s, with `bounces = 0`. Verify: both tests pass with no change to `src/baker.cpp`. If they need one, stop and report why.
- [ ] 9.2 Add a short "Library: q3map2 input" section to `README.md`. It covers:
  - `MaterialLayers` as the owned form of `SH_material_layers`;
  - `AddQ3Map2Surface`: what it repairs, what aborts, and the assembly order;
  - that the headers q3map2 includes need only `src` and Eigen;
  - baking caller-supplied points as N×1;
  - that the CLI and glTF output are unchanged.

  Verify: every function, type and field the section names exists (`grep` in `src/*.h`), and the CLI section of the README is unchanged.
- [ ] 9.3 Run the gate and open PR 7/7: "Document and test baking caller-supplied points". The description also summarizes the whole stack: why q3map2 supplies the luxel points (so no page model or rasterizer change), and the gate's evidence on the stack's top.

## 10. Landing (this repo, PR on `DaviesX/netradiant-custom`)

- [ ] 10.1 After the user has merged the whole stack, check `main`. In `tools/sh-baker`, `git fetch origin`. If `main` holds a commit that is not one of the stack's PRs, ask the user first ("If `main` moves"). Check out `origin/main` detached. Verify:
  - every file the stack added exists (`data/layers/`, `data/notangent/`, `src/texture.h`, `src/tangents.*`, `src/loader_q3map2*`), and `src/material_layers.h` is gone;
  - the gate passes on `main`, including its `saver_test.cpp` check, and every test the stack added is listed in `build/tests.txt` and passes;
  - the header checks of 5.1, 7.1 and 8.2 pass. PR 3/7 adds no file or test, so the 5.1 check is what shows it reached `main`.

  If something is missing, a PR was merged into a branch that never reached `main`. Report which one, and open a PR that carries it to `main` only after the user agrees.
- [ ] 10.2 `git fetch fork`. Create the branch `b2-land` from `fork/master` if the plan PR is merged, or from `b2-plan` if it is still open. Then:
  - point `tools/sh-baker` at `main`'s commit;
  - tick B2 in `CLAUDE.md`, and mark plan §4.2 item 1 done, rebuilding the PDF;
  - run the stock check, which every task's exit criterion includes, on a clean copy of the content, so the user's uncommitted work in `content/` is never touched or measured:
    ```
    D=$(mktemp -d)
    git archive HEAD content/benchmark | tar -x -C "$D"
    tools/stockcheck/stockcheck.py "$D/content/benchmark" bench
    ```
    The default q3map2 build does not link sh-baker, so the result must equal the last run's;
  - tick every task in group 10, since 10.3 only commits and pushes what this task prepares. Then archive the change with `openspec archive sh-baker-q3map2-input --yes`, which moves it under `openspec/changes/archive/` and adds both capabilities to `openspec/specs/`.

  Verify:
  - `git submodule status` shows `main`'s commit;
  - `CLAUDE.md` shows `[x] **B2**`;
  - the stock check exits 0;
  - `openspec/specs/sh-baker-material-layers/spec.md` and `openspec/specs/sh-baker-q3map2-loader/spec.md` exist;
  - `openspec validate --specs --strict` passes.
- [ ] 10.3 Commit the submodule pointer, `CLAUDE.md`, the plan and its PDF, the archived change and the new specs, with the attribution line. Push with `git push -u fork b2-land`, and open the PR with `gh pr create --repo DaviesX/netradiant-custom --base <master or b2-plan> --head b2-land`. The description lists the merged sh-baker PRs and the stock check result. If the plan PR is squash-merged while this one is open, follow "After a merge" with this repo's names.
