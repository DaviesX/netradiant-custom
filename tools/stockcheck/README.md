# Stock check

Answers one question: does the level's pk3 run cleanly on stock Quake 3? Stock here means ioquake3 with
`cl_renderer opengl1` and with `opengl2`, at default cvars, which is what lvlworld.com players run. Every task's exit
criterion includes it (plan §0.4, §Verification).

```
tools/stockcheck/stockcheck.py content/benchmark bench
```

The arguments are the mod directory (laid out as `maps/`, `scripts/`, `textures/`, optionally `levelshots/`; its
directory name is the `fs_game`) and the map name. It exits 0 only if every row of the table is PASS.

| Option | Default |
|---|---|
| `--engine` | `/home/davis/projects/ioq3-custom/ioq3/build/Release/ioquake3`, the fork's build; its renderer libraries sit beside it |
| `--q3map2` | `install/q3map2` in this repository |
| `--q3base` | `/home/davis/q3game/q3data`, which holds `baseq3/pak0.pk3` to `pak8.pk3` |
| `--out` | `stockcheck-out/` in this repository (git-ignored) |

A missing engine, q3map2, pak, `.map`, `.cameras` file or allowlist, or a duplicate view name, stops the check before
anything is compiled. Stale compile outputs in the mod (`maps/<map>.bsp`, `maps/<map>/lm_*`, ...) are deleted from the
compile copy, never packed.
It needs Python 3 with Pillow.

**Don't touch the mouse or keyboard during a run.** The engine opens two 1280×720 windows on the desktop, one after
the other, and takes screenshots from them. A run takes about 20 seconds.

## What it does

1. **Compile.** It copies the mod into a fresh temporary basepath next to links to the stock paks and runs the bundled
   q3map2 there with a fresh homepath, so it never reads `~/.q3a`:
   - `-meta -keeplights`, which keeps every light entity in the BSP for `renderer_sh`;
   - `-vis -saveprt`;
   - `-light -patchshadows`.

   A non-zero exit, `leaked` or `Unknown q3map_* directive` fails the row, and the offending lines are shown.
   The `-light` lightmap is a placeholder: q3map2 reads none of the physical light keys (`intensity`, `cone`, ...).
   sh-baker will write the real lightmap (task B5), so lighting is never judged here.
2. **Pack.** `<mod>.pk3` gets only:
   - `maps/<map>.bsp` and any external lightmaps (`maps/<map>/lm_*`);
   - `levelshots/<map>.jpg` or `.tga`, the loading-screen image. Without one the engine reports a missing image;
   - `scripts/*.shader` and `scripts/shaderlist.txt`;
   - images (`.tga`, `.jpg`, `.png`) under `textures/`.

   Nothing else goes in: no `.map`, `.prt`, `.srf`, `*.import` or READMEs.
3. **Run**, once per renderer, in another fresh basepath holding only the stock paks and the pk3, so no loose file in
   the source mod can hide a missing one. The generated `stockcheck.cfg` loads the map with `devmap`, then for each view
   in `maps/<map>.cameras` runs `setviewpos`, waits, prints `viewpos` and takes a screenshot. It quits after a final wait.
   The cvars are the defaults except `developer 1`, `logfile 2`, a windowed 1280×720 mode, and `cg_draw2D 0`,
   `cg_drawGun 0` and `con_notifytime 0` for clean screenshots.

A run fails on:
- `R_FindImageFile could not find` or `Couldn't find image file for shader` (a missing image);
- any line with `WARNING`, or any `<classname> doesn't have a spawn function`, that no allowlist entry matches;
- an abnormal exit, or no quit within 120 seconds;
- a view with no screenshot, no `viewpos` line, or a `viewpos` more than 256 units from its request (the teleport was
  ignored).

Only shaders the map uses are parsed by the engine, so only they are checked, and never `nodraw` ones such as caulk
and clip. Use every drawn shader you ship.

## Views

`maps/<map>.cameras` has one view per line: `name x y z yaw` (player origin and yaw in degrees; `#` starts a comment).
`setviewpos` has no pitch, and it pushes the player forward at 400 units/s for 160 ms, so the player drifts about
120 units along the yaw before stopping. Give each request at least 160 units of open floor ahead. The eye position
the engine reports is what counts: `views.txt` records it, and editor captures of a view use the `opengl1` line with
pitch 0. A request inside solid isn't caught: the game honours it and the player stays put, so check the screenshot.

## The allowlist

`allowlist.txt` holds one regular expression per line, searched in each log line with colour codes removed. Each
entry has a comment saying why its lines are benign. Add an entry only after reading the line and confirming it has
nothing to do with the content. A short list that fails on a benign warning is better than a broad one that hides a
real problem.

It started with two entries: `light_spot doesn't have a spawn function` and `light_sun doesn't have a spawn
function`. The editor's spot and sun lights stay in the BSP for `renderer_sh`, the stock game has no spawn function for
them and frees each with that line (`code/game/g_spawn.c:281`), and the map still plays. Any other classname without a
spawn function fails the run.

Missing images are stricter. A `WARNING` entry never excuses one, even when its pattern matches. Only an entry anchored
at both ends (`^...$`) that spells out the whole missing-image message literally (no wildcards) does, and only for an image the engine looks up
for itself and replaces with a built-in fallback. The one such entry is `gfx/2d/sunflare`, which `renderergl2` asks for
on every run (`renderergl2/tr_shader.cpp:3862`).

## Output

Each run writes `stockcheck-out/<timestamp>/`:

| File | Content |
|---|---|
| `result.txt` | the table printed at the end, with the engine path and the engine source's git commit in its header |
| `compile-bsp.log`, `compile-vis.log`, `compile-light.log` | q3map2 output |
| `<mod>.pk3` | the pack that was run |
| `opengl1-qconsole.log`, `opengl2-qconsole.log` | engine console logs |
| `opengl1-stdout.log`, `opengl2-stdout.log` | engine standard output |
| `<renderer>_<view>.png` | screenshots |
| `views.txt` | `view renderer eye_x eye_y eye_z yaw` per view and renderer; the `opengl1` lines are authoritative |
