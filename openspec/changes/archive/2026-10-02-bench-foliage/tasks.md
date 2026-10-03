# Tasks

## 1. Early checks (stop and raise any gap before continuing)

- [x] 1.1 In a scratch copy of the benchmark, place one hand-written OBJ (an upright quad with `usemtl textures/bench/fence` plus a box with `usemtl textures/bench/brick`) as `misc_model`. Verify that the editor and a q3map2 + `opengl1` run show it upright, front-facing and at the same place.
- [x] 1.2 In the same scratch map, verify in lighting mode with shadows on that the alpha-tested quad is lit, masked and casts a cut-out shadow under the sun, and that it is lit on both sides. Record what each side looks like in the editor and in the engine. A back-face mismatch is noted for C4 and doesn't block.
- [x] 1.3 With a scratch shader that adds `q3map_forceMeta`, verify from the compile's `.srf`/BSP surface dump that the model surfaces get a lightmap index, and that without it they are vertex-lit. Delete the scratch copy afterwards.

## 2. Generator and assets

- [x] 2.1 Write `tools/benchmark/gen_foliage.py` (Pillow only, fixed seed, Pillow version in its header).
  - Textures in `content/benchmark/textures/bench/`: `bark_c`, `bark_nrm`, `leaves` (RGBA), `grass` (RGBA) and `dirt_c`.
  - Meshes with `.mtl` in `content/benchmark/models/bench/`: `tree.obj`, `grass_clump.obj` and `grass_patch.obj`.
  - The OBJs are Y-up, with counter-clockwise front faces, explicit normals and `usemtl` set to full shader names.
  - It asserts every TGA is stored bottom-up.

  Verify by running it twice and checking with `git status` that the second run changes nothing.
- [x] 2.2 Add the `bark`, `leaves`, `grass` and `dirt` shaders to `scripts/bench.shader` per design.md. Verify the editor loads the file with no rend2-keyword warning, and that the reserved-suffix scan of `textures/bench/` finds nothing.

## 3. Map and views

- [x] 3.1 Split the asphalt brush at x 304 and make the east part `bench/dirt`. Place two `tree` instances, three or four `grass_patch` instances and five or six `grass_clump` instances along the asphalt edge inside the fog. Tune the positions in the editor's lighting mode until the canopy shadows land on the dirt. Verify in the editor that nothing intersects the sky ceiling, the walls or the fence's sightline from `street`.
- [x] 3.2 Add the `garden` view to `maps/bench.cameras`. Verify it has 160 units of open floor ahead and that the stock screenshot frames both trees, grass and a canopy shadow (adjust the request until it does).

- [x] 3.3 Record the author's light retune: give the new ceiling spot at `0 512 150` `_shadows 0` as the non-casting case, update the README's light table, and widen the `benchmark-map` light requirement. Verify the inventory: one `light_sun`, one `light`, three `light_spot`, one with `_shadows 0`, and `sign_target` resolves.

## 4. Stock check and editor parity

- [x] 4.1 Run `tools/stockcheck/stockcheck.py content/benchmark bench`. Verify every row passes, and check the compile log for autoclip warnings and for any lightmap count jump. Then confirm in the engine that a trunk blocks the player and grass doesn't (walk into each with `opengl1`), that the pk3 listing has no `models/` entry, and that the compiled BSP's entity lump has no `misc_model` (`q3map2 -info`, or by reading the lump from the packed BSP).
- [x] 4.2 Capture the editor in lighting mode with shadows on at the `garden` position recorded by the opengl1 run (`NETRADIANT_CAMERA_*`). Verify it shows both trees, grass and the cut-out canopy shadow, with no grass shadow, and frames the same scene as the stock screenshot.
- [x] 4.3 Show the author the stock screenshots of every view on both renderers and the editor capture. Record the acceptance date in `content/benchmark/README.md`.

## 5. Documentation

- [x] 5.1 Update `content/benchmark/README.md`: the layout table (models, generator, 38 brushes, the new entity count), the material table (bark, leaves, grass, dirt), a vegetation section (misc_model, `q3map_forceMeta`, `q3map_clipModel` (which clips even a `nonsolid` shader, so only `bark` has it), the caster split between leaves and grass, and that canopy shadows reach the engine only after C5) and the `garden` view. Verify every path and count it states against the tree.
- [x] 5.2 Update `CLAUDE.md`:
  - A10 says the Silent Hill look includes grass and trees, built as in the benchmark;
  - Track A lists `bench-foliage` before A7, checked off;
  - C5 also casts from alpha-tested draw surfaces under the editor's caster rule.

  In `docs/pbr-plan/pbr-plan.tex`:
  - add grass and trees to §Stage 1;
  - add a world-building convention: vegetation is lightmapped `misc_model` meshes, and grass is static crossed quads;
  - add to §3.5 that alpha-tested draw surfaces (fence, leaf cards) cast through their alpha test alongside the brush hulls, replacing the "known gap" note's suggestion for them.

  Verify `latexmk -pdf` in `docs/pbr-plan/` builds without errors.
