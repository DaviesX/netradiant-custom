# Design

## Context

See `proposal.md` for motivation and `specs/` for required behaviour. Plan rev. 6 (§Compatibility contract, §Verification, §0.1–0.4) sets the rules. The facts that shape the approach:

- **Bench source.** It lives in `setup/data/gamepacks/pbr/pbr.game/base/` (`install/` is build output). That covers 37 brushes, 1 patch and 8 entities, 14 materials in `materials/bench.mtr` (including an unused `bench/trigger`), and a q3map2 shim, `scripts/bench.shader`.
  - The shim carries `q3map_sun 1 0.95 0.85 120 30 50` for the old q3map2 lightmap. The editor doesn't read `q3map_sun`, and the bench's sun is its `light_sun`, so the converted sky drops the line.
  - The lights are two `light` (`intensity` 400000 and 200000), a downward `light_spot` (`angles 90 0 0`, `cone 40`), a sign `light_spot` targeting `sign_target` with `_shadows 0`, and one `light_sun` (`angles 50 -35 0`, `intensity 3`).
  - No brush uses `bench/trigger`, so the converted bench drops it.
  - `brick_n`, `asphalt_n`, `concrete_n` and `metal_n` are reserved suffixes.
- **q3map2** (`tools/quake3/q3map2/`):
  - q3map2 prints `******* leaked *******` in lowercase on a leak and still exits 0 (`leakfile.cpp:123`). It always adds `~/.q3a` to its search paths unless `-fs_homepath` is given (`path_init.cpp:402`).
  - `-keeplights` is parsed by the BSP stage (`bsp.cpp:658`) and stored as `_keepLights` in worldspawn for the later stages (`writebsp.cpp:228–234`).
  - Quake 3's default leaves patches out of the shadow casters; `-patchshadows` turns them on (`light.cpp:2217`, `:2732`), matching the editor, where patches cast.
  - Every entity whose classname starts with `light` is one of q3map2's lights (`light.cpp:330`), read with its own keys (`_light`/`light`, default 300). It ignores `intensity`, `cone` and `cone_inner`, so the `-light` lightmap of the bench is a placeholder.
- **Engine** (the fork's build at `ioq3-custom/ioq3/build/Release`, commit `0135354f`):
  - Renderer libraries load from the binary's directory, so the basepath can be throwaway, and `cl_renderer opengl1`/`opengl2` both exist.
  - Game code comes from `pak8.pk3`'s QVMs at default `vm_*` settings.
  - `setviewpos x y z yaw` needs cheats (`devmap`) and has no pitch argument (`code/game/g_cmds.c:1699`). It calls `TeleportPlayer`, which sets the velocity to 400 along the yaw with a 160 ms knockback hold (`g_misc.c:88–93`), so the player drifts before stopping.
  - A missing stage image prints `WARNING: R_FindImageFile could not find` (`renderergl2/tr_shader.cpp:656`). `Couldn't find image file for shader` (`:3336`, developer only) covers implicit shaders. An unknown stage keyword prints a warning and rejects the shader (`:1267`).
  - The engine parses a shader body only when something requests it (`tr_shader.cpp:3300`).
  - The game prints `<classname> doesn't have a spawn function` for each entity with no spawn function (`code/game/g_spawn.c:281`), which covers `light_spot` and `light_sun`. `light` has `SP_light`, which frees it silently. The map still loads and plays.
- **Editor screenshot automation** already exists: `NETRADIANT_CAMERA_SCREENSHOT`, `_ORIGIN` and `_ANGLES` (`radiant/camwindow.cpp:1688`).
- **Tooling.** There's no Xvfb. Python 3 with Pillow 12 is available, and the desktop has an NVIDIA GL 4.6 context.

## Goals / Non-Goals

**Goals:**

- A bench that both stock renderers accept and that previews in the editor under `Q3.game` (materials and lights) with no `pbr.game` involvement.
- One reproducible command that answers "does the lvlworld build run cleanly?", with artifacts a human can look at.

**Non-Goals:**

- Lighting fidelity of the stock fallback. The `-light` lightmap is a placeholder until sh-baker writes the real one (B5), and A9 judges that.
- Re-tuning light values. The bench keeps the values tuned in the editor preview.
- The enhanced run (`cl_renderer sh`), which is added at C2.
- CI or headless operation.
- Changing the frozen `pbr.game` bench.

## Decisions

### Copy the bench, don't move it

`pbr.game` keeps its bench until A7 deletes the pack. The copy is the new source of truth. A note at the top of the old README points at it.

### Conversion rules

- **Lightmapped materials** (brick, asphalt, concrete, plaster, metal, tarp): a `$lightmap` stage, then the base colour stage with `blendFunc GL_DST_COLOR GL_ZERO`.
  - Normal, metallic-roughness and occlusion maps become `qer_pbr_normal`, `qer_pbr_metallicRoughness` and `qer_pbr_occlusion`.
  - Factors become `qer_pbr_*Factor`. Where the `.mtr` had `metallicfactor 1` with a map, the factor is omitted, since a map's factors default to 1.
  - Tarp adds `cull none`.
- **Neon:** a lightmapped base stage (`neon_c`), then an additive stage `map textures/bench/neon_e`, `blendFunc add`, `rgbGen const ( 1 0.35 0.25 )` (the old `emissivefactor`), plus `qer_pbr_emissiveStrength 8` (the old `emissivestrength`, which means the same thing). No `q3map_surfacelight`: the placeholder lightmap doesn't need it, and sh-baker takes emissive surfaces as area lights itself.
- **Fence**, the standard alpha-tested lightmapped form:
  1. `map textures/bench/fence`, `alphaFunc GE128`, `depthWrite`;
  2. `map $lightmap`, `blendFunc filter`, `depthFunc equal`;
  3. `cull none`, `surfaceparm alphashadow`, `surfaceparm trans`, `surfaceparm nonsolid`.
- **Glass:** one stage, `map textures/bench/glass`, `blendFunc blend`, `surfaceparm trans`. The old `baseColorFactor` alpha is baked into the image, because stock has no factor. It's unlit in the preview, which is correct for a blended surface.
- **Sky, fog, caulk, clip:** copied from the shim, with the sky's `q3map_sun` line removed. The shim's sky (`skyparms - 512 -`, no stages) draws nothing in stock Quake 3, so the converted sky uses pak0's `env/xnight2` farbox (`skyparms env/xnight2 - -`): it ships with every Quake 3 install, adds no image to the pk3, and stays dark. The editor draws it with its textured-mode state, as before.
- **Lights:** copied unchanged, keys and values included. The editor reads them through the `pbr` entity module that `Q3.game` uses after `q3-shader-pbr-materials`; `renderer_sh` reads them from the BSP entity lump kept by `-keeplights`.

### Views: requested in a file, measured by the engine

`maps/bench.cameras` holds `name x y z yaw` view requests. After each `setviewpos`, the generated cfg waits about two seconds' worth of frames, which covers the 160 ms knockback plus friction, then runs `viewpos` (`cg_consolecmds.c:74`: integer eye origin and yaw), then `screenshot`. After the last screenshot it waits again before `quit`, so the file is written. The check parses each `viewpos` line and writes `views.txt` (name, renderer, eye x y z, yaw).

`viewpos` prints even when `setviewpos` was ignored, so a run fails when a recorded position is more than 256 units from its request. The knockback drift is about 130 units at most. The `sign` request equals the spawn point, so an ignored teleport there is harmless. The editor capture of a view uses the `opengl1` run's values with pitch 0, because drift varies from run to run.

The three requests are:
- `sign`: `-96 -64 24 90`;
- `south-interior`: near the pillar under the downward spot;
- `arch`: facing the west patch arch.

Each view is placed so the drift ends in open space.

### Isolated compile and run

1. Make a temporary root with `baseq3/` holding symlinks to `pak0–8.pk3`, and `benchmark/` holding a copy of the mod directory.
2. Run q3map2 with `-fs_basepath <tmp> -fs_homepath <tmp>/home -fs_game benchmark` on the copied `.map`, so the compile sees exactly the tree under test and never `~/.q3a`.
3. Pack `benchmark.pk3` from the compiled copy.
4. For each renderer, make a second, fresh root with the pak symlinks and only `benchmark/benchmark.pk3`. Write `stockcheck.cfg` into the homepath's `benchmark/`.
5. Launch `ioquake3` from its own directory with `+set fs_basepath <root> +set fs_homepath <root>/home +set fs_game benchmark +set cl_renderer <r> +set developer 1 +set logfile 2 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_fullscreen 0 +set cg_draw2D 0 +set cg_drawGun 0 +set com_introPlayed 1 +exec stockcheck.cfg`, with a 120-second timeout.
6. Convert the TGA screenshots to PNG with Pillow, and copy the logs, PNGs and `views.txt` to `stockcheck-out/<timestamp>/`.

### Packaging by allowlist

The packer includes only:
- `maps/<map>.bsp` and q3map2's external lightmaps, if any;
- `scripts/*.shader` and `scripts/shaderlist.txt`;
- image files (`.tga`, `.jpg`, `.png`) under `textures/`.

Everything else is left out: `.map`, `.prt`, `.srf`, `*.import`, READMEs. Missing images surface as engine warnings, not as packer errors.

### The allowlist starts with the two spawn-function messages

It starts with exactly two entries, `light_spot doesn't have a spawn function` and `light_sun doesn't have a spawn function`, each with a comment citing `g_spawn.c:281`. The check matches every line of this form, not only `WARNING` lines, so any other classname without a spawn function still fails the run. Every further entry is added only after a human reads the warning and records why it's benign. A short list with a failing benign warning is better than a broad list hiding a real one.

## Risks / Trade-offs

- **`wait` counts are frames, and framerates vary.** → Waits are generous. A missing `viewpos` or screenshot fails the run explicitly.
- **Auto-exposure at `opengl2` defaults (`r_hdr 1`) makes screenshots vary between runs.** → They're for human review only; nothing is measured from them in this change.
- **The placeholder lightmap looks nothing like the preview.** → Accepted. Lighting isn't judged here; A9 judges the sh-baker lightmap after B5.
- **Benign engine warnings unrelated to the content** (sound, music) could fail every run. → Allowlist entries, each justified in a comment.
- **Windows open on the desktop while the check runs.** → Accepted (decided). The README says not to touch the mouse.
- **The fork's renderers may drift from upstream.** → Accepted (decided). Every run records the commit.
- **Only shaders the map uses are checked**, and the engine never parses `nodraw` shaders such as caulk and clip. → The benchmark uses every drawn shader it ships. The town follows the same convention.

## Migration Plan

New files only, plus `.gitignore` entries. Rollback is deleting `content/benchmark/` and `tools/stockcheck/`.
