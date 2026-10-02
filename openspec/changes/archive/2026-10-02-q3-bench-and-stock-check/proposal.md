# Proposal

## Why

Every later task needs two things:
- a benchmark map that is ordinary Quake 3 content, previewable under `Q3.game`;
- an automatic check that stock ioquake3 runs it cleanly, as the lvlworld fallback of plan rev. 6 (§1, §Compatibility contract).

Today the benchmark exists only in the frozen `pbr.game`. There it uses `.mtr` materials and `_n` texture names that rend2 auto-loads, neither of which the stock target accepts. Nothing checks content against the engine. This change delivers tasks A4 (§0.1) and A5 (§0.4, §Verification). From here on, every task's exit criterion includes the stock check.

The bench's lights stay as they are. After `q3-shader-pbr-materials` (which also does A6), `Q3.game` uses the `pbr` entity module, so `light`, `light_spot` and `light_sun` with their physical keys preview under `Q3.game` exactly as they did under `pbr.game`.

## What Changes

- **Benchmark as a Quake 3 mod (A4).** The bench is copied into the `benchmark` mod directory (`content/benchmark/`, symlinked as `/home/davis/q3game/q3data/benchmark`).
  - `materials/bench.mtr` becomes `scripts/bench.shader`, with vanilla stages plus `qer_pbr_*` keywords (including `qer_pbr_normal`), per `q3-shader-pbr-materials`.
  - The `*_n` images are renamed to `*_nrm`.
  - The map's `light`, `light_spot` and `light_sun` entities and their keys are copied unchanged. Their values were tuned in the editor preview and stay so.
  - The frozen `pbr.game` copy is left untouched.
- **Compile flags.** `-meta -keeplights`, then `-vis -saveprt`, then `-light -patchshadows`. `-keeplights` is a BSP-stage option that keeps every light entity in the BSP for `renderer_sh`. The `-light` lightmap is a placeholder: q3map2 treats every classname starting with `light` as one of its own lights and reads none of the physical keys, so its lightmap isn't expected to match the preview. sh-baker writes the real lightmap later (B5), and A9 judges it.
- **Documented views.** `content/benchmark/maps/bench.cameras` lists the requested views. The stock check records the eye position the engine actually reached (`setviewpos` pushes the player forward), and later editor captures reuse that recorded position.
- **One pk3.** `benchmark.pk3` holds the BSP, its lightmaps, the levelshot, shaders, every image, the PBR maps included, and nothing else. Stock renderers never reference the PBR images.
- **Stock check (A5).** `tools/stockcheck/stockcheck.py`:
  1. compiles the mod in an isolated temporary basepath with the bundled q3map2, failing on a leak or on `Unknown q3map_* directive`;
  2. packs the pk3;
  3. runs the fork's ioquake3 build windowed on the desktop GPU with `cl_renderer opengl1` and with `opengl2`, each at default cvars, in a clean temporary basepath and homepath;
  4. visits every view (`setviewpos`, settle, `viewpos`, `screenshot`);
  5. fails on unallowlisted `WARNING` lines, missing images, a missing or far-off view readback, a missing screenshot, a crash or a timeout;
  6. prints a PASS/FAIL table, keeps screenshots and recorded views, and records the engine's commit.

  The allowlist starts with exactly two entries: the game's `light_spot doesn't have a spawn function` and `light_sun doesn't have a spawn function` messages, which the stock game prints once per such entity and which are harmless.
- **Plan.** Rev. 6 already describes the check (§Verification). This change makes the tool match it.
- **BREAKING:** none. No editor code changes.

## Capabilities

### New Capabilities

- `benchmark-map`: the Quake 3 benchmark content, covering its location, materials, lights, views, compile flags and packaging.
- `stock-check`: the stock-engine verification tool, covering its inputs, isolation, run matrix, view handling, failure conditions and outputs.

### Modified Capabilities

None. `pbr-gamepack` keeps describing the frozen bench until A7 deletes the pack.

## Impact

- New `content/benchmark/` tree:
  - `maps/bench.map`, `maps/bench.cameras`;
  - `scripts/bench.shader`, `scripts/shaderlist.txt`;
  - `textures/bench/*` (renamed copies);
  - `levelshots/bench.jpg`;
  - `README.md`.
- `.gitignore` gains the compile outputs under `content/` and `stockcheck-out/`.
- New `tools/stockcheck/`: `stockcheck.py`, `allowlist.txt` and `README.md`. It needs Python 3 with Pillow (present).
- Read-only dependencies:
  - the fork's engine build at `/home/davis/projects/ioq3-custom/ioq3/build/Release`;
  - the bundled q3map2 (`install/q3map2`);
  - `/home/davis/q3game/q3data/baseq3/pak0–8.pk3`.
- `CLAUDE.md`: tick A4 and A5 on completion.
- Requires `q3-shader-pbr-materials` to be archived first: previewing the converted shaders and the bench's lights under `Q3.game` needs it. The stock check itself needs no editor code.
