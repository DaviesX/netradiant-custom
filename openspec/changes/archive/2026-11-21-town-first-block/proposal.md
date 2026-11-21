# Proposal

## Why

Task A8 (plan §0.2): the town does not exist yet. The benchmark is a test asset, not a level, and every later exit
criterion (C2's walkable town, A9's fallback verdict, A10's look, B8's "hard indoor map") refers to a town block. Nothing
blocks it: the materials, lights, `-shbake` and the stock check are all in place.

## What Changes

- A new mod directory `content/town/` (`fs_game town`) with the first block, `maps/town1.map`: one crossroads. Two
  streets cross under a low sky ceiling, four building corners close them in, three interiors open off different arms
  (two through teleporting doors, as in Silent Hill 1, and one through an open doorway), and a fenced lawn with trees and grass fills the fourth corner.
- The map is **generated**: `tools/town/gen_town.py` writes the `.map`, and `tools/town/gen_textures.py` writes the
  placeholder texture sets. Both use fixed seeds, and their outputs are committed. Lights are the one hand-tuned part:
  they live in `tools/town/town1_lights.json`, and the generator can pull them back from a map saved by the editor.
- `scripts/town.shader`: placeholder materials in the `qer_pbr_*` form from the first shader, plus the town's own
  sky, fog, caulk, clip, hint, skip and trigger shaders.
- The world-building conventions (plan §World-building) are in force and checked: structural shells, detail ornament,
  hint brushes across every arm, door interiors sealed off from the street's PVS and reached by `trigger_teleport`, fog colour equal to sky colour,
  worldspawn `_blocksize 0`, lightmapped surfaces only, and vegetation as lightmapped `misc_model` meshes.
- Lights: one `light_sun`, a shadow-casting `light_spot` pointing down from each street lamp, attached with `_proxy 1`
  to the emissive inside of the lamp's reflector cover (a non-planar emitter), a `light_area` on one interior's ceiling panel, a plain point `light` in another, and one interior with
  no light of its own, lit only by what spills through its open doorway (A10's flashlight room).
- `tools/town/check_pvs.py`: reads the compiled BSP and checks named point pairs for "hidden" or "visible". This is the test for "PVS behaving like an indoor level".
- `tools/benchmark/gen_foliage.py` takes the shader prefix and output directories as parameters so the town reuses its
  tree and grass meshes. The benchmark's outputs stay byte-identical.
- `maps/town1.cameras`, `levelshots/town1.jpg` and a `content/town/README.md`; the stock check passes on
  `content/town town1`. `CLAUDE.md` and the plan mark A8 done, and the plan's "area portals in doorways" convention becomes teleporting doors.

No editor, q3map2, sh-baker or engine code changes.

## Capabilities

### New Capabilities
- `town-map`: the town level's content contract: where it lives, how it is generated, its materials, lights,
  visibility conventions, compile, views and stock acceptance. Later blocks and A10 extend it.

### Modified Capabilities

None. The benchmark's vegetation generator keeps its behaviour (same outputs), and the stock check already takes any
mod and map.

## Impact

- New: `content/town/**`, `tools/town/gen_town.py`, `tools/town/gen_textures.py`, `tools/town/check_pvs.py`,
  `tools/town/town1_lights.json`.
- Changed: `tools/benchmark/gen_foliage.py` (parameters only), `CLAUDE.md`, `docs/pbr-plan/pbr-plan.tex` (status).
- Outside the repository: a `town` symlink in `/home/davis/q3game/q3data`, like `benchmark`'s.
- Needs q3map2 built with `SH_BAKER=1` and the fork's ioquake3 build, as the stock check already does.
- Unblocks C2, A9, A10 and B8's indoor measurements.
