# Tasks

## 1. Mod directory and textures

- [x] 1.1 Create `content/town/` (`maps/`, `scripts/`, `textures/town/`, `models/town/`, `levelshots/`) and the symlink `/home/davis/q3game/q3data/town`; verify `ls -L /home/davis/q3game/q3data/town` lists the directories
- [x] 1.2 Give `tools/benchmark/gen_foliage.py`'s `main()` parameters for the mod directory, texture and model subdirectories and the three shader names, defaulting to the benchmark's; verify running it leaves `git status content/benchmark` clean
- [x] 1.3 Write `tools/town/gen_textures.py` (fixed seed, Pillow, bottom-up TGAs) for the sets in design §4, calling `gen_foliage` for `models/town/` and the vegetation textures, with the Pillow version of its last run recorded in its header; verify a second run with that version changes nothing and no file ends in `_n`, `_nh` or `_s`
- [x] 1.4 Write `scripts/town.shader` and `scripts/shaderlist.txt`: the materials, flat sky, fog (its `fogparms` colour equal to `gen_textures.py`'s sky constant, which the generator checks), caulk, clip, hint, skip, trigger; verify a grep finds no rend2 keyword, `q3map_sun` or `q3map_surfacelight`, every drawn non-sky, non-fog shader has a `$lightmap` stage, and `gen_textures.py` exits non-zero when the `fogparms` colour is temporarily changed
- [x] 1.5 Start `content/town/README.md` with the layout table, the materials table and the generator commands; verify each documented command runs as written

## 2. Map generator

- [x] 2.1 Write `tools/town/gen_town.py` with the layout table and brush writer: streets, sidewalks, facades, arm ends, sky ceiling, caulk hulls, worldspawn `_blocksize 0`; verify `q3map2 -meta` on its output prints no `leaked`
- [x] 2.2 Add the three interiors with structural shells: the NW and NE rooms sealed below their corners with facade and room doors, `trigger_teleport` brushes and `target_position` destinations (design §5), and the SW room at street level with its open doorway; verify `-meta` prints no `leaked`, keeps both sealed rooms, and the `.map` has four triggers whose targets all resolve
- [x] 2.3 Add detail ornament (kerbs, window panes, trim, lamp posts, arms and reflector covers with `lamp` on their inner faces only, ceiling panel), the fence and the lawn with its `misc_model` trees and grass; verify every such brush is detail in the `.map` and `-meta` drops the `misc_model` entities
- [x] 2.4 Add the hint brushes (design §5), the fog brush, the spawns; verify the `.map` has two hint brushes per arm and one fog brush whose faces lie outside the playable space and above the sealed rooms
- [x] 2.5 Add `tools/town/town1_lights.json` with the lights of design §6 (placeholder `intensity`/`_color` on the attached ones), emit them from the generator, and add `--pull-lights`; verify the spec's round-trip scenario with a hand-edited copy of the map, and that pulling from a map with no light exits non-zero and leaves the JSON unchanged
- [x] 2.6 Emit `maps/town1.cameras` (long street, crossroads, lamp, three interiors, lawn; 160 units of open floor ahead of each) and `maps/town1.pvs` from the layout table; verify a second generator run leaves `git status content/town` clean
- [x] 2.7 Document in the README that the committed map is generator output, the pull workflow, and what an editor edit loses; verify the documented pull command runs as written
- [x] 2.8 Add a bench beside each lamp post and a trash bin toward each arm's end as detail brushes (design §1); verify the map compiles without a leak, the PVS check and the stock check pass, and an editor capture shows both

## 3. PVS check

- [x] 3.1 Write `tools/town/check_pvs.py` (design §8): leaf lookup and cluster visibility; verify on the compiled `bench.bsp` with two hand-picked pairs (street to street visible, a point in a wall fails as in-solid)
- [x] 3.2 Compile `town1` with the draft stages and run the check; fix the layout or hints until it exits 0, and verify by temporarily flipping one `hidden` mark that it then exits non-zero
- [x] 3.3 Record the check's command, the pair list's meaning and q3map2's `Average clusters visible` line in the README; verify the command runs as written

## 4. Lights in the editor

- [x] 4.1 Open `town1` under `Q3.game` with the `town` mod (scratch HOME for any scripted launch); verify every `textures/town/*` shader shows in the texture browser, the console has no rend2-keyword warning, and lighting mode with shadows on lights the street under each lamp
- [ ] 4.2 Author step: run "Re-derive light from emitter" on the four lamp proxies and the `light_area`, tune strengths and the sun in the preview, save, run `--pull-lights`, regenerate; verify a second re-derive pass leaves the saved map unchanged and `-light -shbake` prints no unattached-emitter warning
- [x] 4.3 Record the light table in the README (entity, keys, role), as the benchmark's does, with the lamp proxy's residual against the hemisphere proxy (design §3); verify it matches `town1_lights.json`

## 5. Stock check and acceptance

- [x] 5.1 Run `tools/stockcheck/stockcheck.py content/town town1` without a levelshot to get screenshots, cut `levelshots/town1.jpg` from the `opengl1` crossroads view, and rerun; verify every row is PASS on both renderers with `tools/stockcheck/allowlist.txt` unchanged
- [x] 5.2 Check the SW interior's screenshots on both renderers against the spec's spill scenario (lit floor inside the doorway, darker far wall, no pure black); if it fails, move the south arm's lamp or widen the doorway and regenerate; verify with sampled pixel values written into the README
- [x] 5.3 Sample the long-street screenshots on both renderers for the fog/sky seam (spec scenario: within 8 per channel); if a renderer fails, adjust the sky image and record the reason in the README; verify with the sampled values written into the README
- [x] 5.4 Compile with "Town (pre-release)" and "Town (final)" light stages and record the three presets' bake times and luxel counts in the README; verify each stage exits 0 and writes `maps/town1/town1.shsf`
- [ ] 5.5 Author step: walk the block on stock ioquake3 with `opengl1` and `opengl2` at defaults and `cg_drawFPS 1` (every arm, both teleporting doors in both directions, the open doorway, every interior, the lawn), and review the screenshots for content errors; verify the README records the date, run, engine commit and frame rates
- [x] 5.6 Describe each view in the README and check `openspec validate town-first-block --strict` passes
- [x] 5.7 After 4.2, rerun the stock check and the three preset compiles, cut a new levelshot, and refresh the README's light table, bake table, seam and spill figures and run ids; verify every row is PASS and the README's numbers match the new run

## 6. Project records

- [x] 6.1 Mark A8 done in `CLAUDE.md` with a one-paragraph description of the town mod, its generators and the PVS check; verify the paths it names exist
- [x] 6.2 In `docs/pbr-plan/pbr-plan.tex`, replace "area portals in doorways" in §0.2 and the world-building conventions with teleporting doors to sealed interiors and the reason (design §5), update the status row for 0.2 in `docs/pbr-plan/pbr-plan.tex` and rebuild with `latexmk -pdf`; verify the build succeeds
