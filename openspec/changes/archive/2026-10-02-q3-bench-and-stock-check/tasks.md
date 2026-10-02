# Tasks

## 1. Benchmark content (A4)

- [x] 1.1 Create `content/benchmark/` and symlink it as `/home/davis/q3game/q3data/benchmark`. Copy `maps/bench.map` and `textures/bench/*.tga` (no `*.import`) from `setup/data/gamepacks/pbr/pbr.game/base/`, renaming `brick_n`, `asphalt_n`, `concrete_n` and `metal_n` to `*_nrm`. Bake the glass's `baseColorFactor` alpha (0.35) into `glass.tga`. Add `.gitignore` entries for compile outputs under `content/`. Verify: a scripted scan finds no texture matching `_(n|nh|s)\.` and no `*.import`.
- [x] 1.2 Write `content/benchmark/scripts/bench.shader` per the design's conversion rules (13 materials; the unused trigger is dropped; no `q3map_sun` and no `q3map_surfacelight`) and `scripts/shaderlist.txt`. Verify, under `Q3.game` with `fs_game benchmark` and `q3-shader-pbr-materials` archived (so the bench's lights preview):
  - every bench shader appears in the texture browser with no rend2-keyword warning;
  - in lighting mode, the lights show normal-map relief on brick, asphalt, concrete and metal, the neon glows, the fence is masked and casts its cut-out, and the glass is blended and casts nothing.
- [x] 1.3 Check the copied entities: the map keeps both `light` entities, both `light_spot` entities (the sign spot targeting `sign_target` with `_shadows 0`) and the `light_sun`, with their keys and values exactly as in the `pbr.game` bench. Verify: a scripted diff of the entity blocks against `setup/data/gamepacks/pbr/pbr.game/base/maps/bench.map` shows no difference, and the sky shader has no `q3map_sun`.
- [x] 1.4 Write `content/benchmark/maps/bench.cameras` with the requests `sign -96 -64 24 90`, `south-interior` (near the pillar under the downward spot) `arch` (facing the west patch arch) and `street` (fog, sign, glass and fence in one frame), each placed so a forward drift of up to 130 units ends in open space. Verify: in the editor, each requested origin is inside the playable volume, with at least 160 units clear ahead along its yaw (checked in the 2D view).

## 2. Stock check tool (A5)

- [x] 2.1 Create `tools/stockcheck/stockcheck.py` with argument parsing (mod dir, map name, `--engine`, `--q3map2`, `--q3base`, `--out`) and defaults:
  - engine: `/home/davis/projects/ioq3-custom/ioq3/build/Release/ioquake3`;
  - q3map2: `install/q3map2`;
  - q3base: `/home/davis/q3game/q3data`;
  - out: `stockcheck-out/`.

  Fail early with a message naming any missing path. Verify: `--engine /nonexistent` exits non-zero before compiling.
- [x] 2.2 Implement the isolated compile: a temporary root with pak symlinks and a copy of the mod, then `-meta -keeplights`, `-vis -saveprt`, `-light -patchshadows`, each with `-game quake3 -fs_basepath <tmp> -fs_homepath <tmp>/home -fs_game <mod>`. Fail on a non-zero exit, `leaked` (case-insensitive) or `Unknown q3map_* directive`, and show the offending lines. Verify:
  - the bench compiles, and its BSP's entity lump contains its `light`, `light_spot` and `light_sun` entities with their keys;
  - a scratch copy with `q3map_notARealDirective` in a used shader fails at compile;
  - a scratch copy with one outer wall brush deleted fails with a leak.
- [x] 2.3 Implement the allowlist packer (BSP and lightmaps, `levelshots/<map>.jpg|tga`, `scripts/*.shader`, `shaderlist.txt`, `textures/**` images). Verify: the bench's zip listing contains only those entries, with no `.map`, `.prt` or `*.import`.
- [x] 2.4 Implement the isolated run: a fresh root with pak symlinks and only the pk3, a temporary homepath, and the generated `stockcheck.cfg`. The cfg runs devmap, then per view `setviewpos` / wait / `viewpos` / wait / `screenshot`, then a final wait, then `quit`. Use the launch flags from design.md and a 120-second timeout. Parse the `viewpos` lines into `views.txt` and convert the screenshots to PNG. Verify: an `opengl1` run on the bench produces the log, one PNG per view (the last included) and a `views.txt` with one line per view, each within 256 units of its request.
- [x] 2.5 Implement the run checks:
  - `R_FindImageFile could not find` and `Couldn't find image file for shader`;
  - `WARNING` and `doesn't have a spawn function` lines not matched by `allowlist.txt`, which starts with the `light_spot` and `light_sun` spawn-function entries;
  - abnormal exit and timeout;
  - missing screenshots, and `viewpos` lines that are missing or more than 256 units from the request.

  Verify:
  - a scratch bench copy whose used `bench/concrete` shader gains a `stage normalMap` line fails the `opengl1` run, showing the warning, while `opengl2` passes;
  - a scratch copy with one used image removed from `textures/` fails with `R_FindImageFile could not find`;
  - the readback check, on synthetic logs: a view whose `viewpos` line is missing, or more than 256 units from its request, fails. (A request inside solid rock can't serve: the game honours `setviewpos` into solid and the player stays at the request, so its readback is in range.)
  - the bench's `light_spot`/`light_sun` spawn-function lines don't fail the run, while a scratch copy with an entity of classname `func_notreal` fails it.
- [x] 2.6 Implement the matrix (`opengl1`, `opengl2`) and the PASS/FAIL table with a header holding the engine path and `git -C <engine source> rev-parse --short HEAD`. Verify: the table has a compile row and two engine rows, and the header shows the engine source's current short commit.
- [x] 2.7 Write `tools/stockcheck/README.md`: usage, defaults, "don't touch the mouse during a run", the allowlist policy (including why the two spawn-function entries are there), the output layout, the placeholder `-light` lightmap, and the fact that only used, drawn shaders are checked. Add `stockcheck-out/` to `.gitignore`. Verify: the README's example command runs as written.

## 3. Bring the benchmark to PASS

- [x] 3.1 Run `tools/stockcheck/stockcheck.py content/benchmark bench` and fix content until every row passes, adding justified allowlist entries only for warnings unrelated to content. Verify: the check exits 0.
- [x] 3.2 Show the author the screenshots of all four views on both renderers. Lighting isn't judged (the lightmap is a placeholder); the review checks that every shader renders with its image (no missing-texture or default images), the fence is masked, the glass is blended, and the sky draws. Record the acceptance date in `content/benchmark/README.md`. Verify: the author confirms in conversation.
- [x] 3.3 Write `content/benchmark/README.md`: what each material and light exercises, the conversion from the `pbr.game` bench (shader forms, `.mtr` keys to `qer_pbr_*`, lights copied unchanged), the compile flags and why the `-light` lightmap is a placeholder, the cameras and `views.txt` handling, and how to run the stock check. Add a pointer at the top of `setup/data/gamepacks/pbr/pbr.game/README.md` saying the bench now lives in `content/benchmark/`. Verify by reading.

## 4. Plan and project docs

- [x] 4.1 Check that plan §Verification and §0.4 match the implemented tool (matrix, failure strings, allowlist with the spawn-function entries, `viewpos` readback) and fix any difference. Tick A4 and A5 in `CLAUDE.md`. Verify: `latexmk -pdf` builds.

## 5. Integration

- [x] 5.1 Re-run the stock check after deleting every compile output under `content/benchmark/`. Verify: every row passes, there are eight PNGs and eight `views.txt` lines, and the header records the engine commit.
