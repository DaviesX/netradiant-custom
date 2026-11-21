# Design

## Context

See proposal.md for the motivation. What exists and constrains the approach:

- `content/benchmark/` is the model for a mod directory: `maps/`, `scripts/`, `textures/`, `models/`, `levelshots/`, a
  `.cameras` file, a README, and a symlink from `/home/davis/q3game/q3data/`.
- `tools/stockcheck/stockcheck.py <mod> <map>` already takes any mod. It copies the whole mod into its compile root
  (so models are found), packs only BSP, lightmaps, side file, levelshot, shader scripts and `textures/` images, and
  fails on a missing levelshot image, a leak, or an unallowlisted warning.
- `tools/benchmark/gen_foliage.py` writes trees and grass, but its mod path and shader names (`textures/bench/...`)
  are module constants.
- `content/fixtures/make_map.py` shows the brush-writing form the editor and q3map2 both accept.
- Attached-light values come from the editor's "Create area light from selected face", "Create proxy light from
  selection" and "Re-derive light from emitter" commands. They have no command-line form.
- The benchmark carries its own caulk and clip, so a mod's tool shaders are its own. The town needs hint, skip and
  trigger too. q3map2 recognises hint and skip by `surfaceparm hint` and `surfaceparm skip`.
- `-shbake` adds every BSP model's brush hulls as occluders (`light_sh.cpp` loops over `bspModels`), so a closed
  `func_door` would block baked light.
- baseq3 spawns `trigger_teleport` and `target_position`. Its `TeleportPlayer` plays the teleport sound and effect at
  both ends, pushes the player forward at 400 units/s for 160 ms, and telefrags whoever stands at the destination.

Decided with the user before this design: the geometry is script-generated, the materials are generated placeholders,
the block is one crossroads, and it lives at `content/town` as `town1`.

## Goals / Non-Goals

**Goals:**
- A walkable, leak-free block that passes the stock check and obeys every visibility convention.
- A generator simple enough that the next block is a new layout table, not a new script.
- An objective PVS test.

**Non-Goals:**
- The Silent Hill look: fog density, darkness budget, flashlight, final vegetation (A10).
- Judging the lightmap against the preview (A9).
- Real art. Every texture is a placeholder.
- Any change to the editor, q3map2, sh-baker, the stock check or the engine.
- A silent or faded door transition. That needs game code, which is the open baseq3-or-mod decision.
- A far plane (`_farplanedist`) at the fog opacity distance. It belongs with the fog density, which A10 tunes, and it
  would change which street clusters see each other, so the PVS list would have to follow it.
- Real glass windows, though the layout no longer forbids them.
- Items, weapons and bot support (`.aas`). The block gets deathmatch spawns and nothing more.

## Decisions

### 1. Layout

All coordinates are on an 8-unit grid; the generator holds them in one table.

- **Streets.** Two streets 320 wide (a 192 roadway and two 64 sidewalks raised 8) along the X and Y axes, crossing at
  the origin. Each arm runs out to ±1024. The street floor is at z = 0.
- **Ceiling.** Sky brushes with their underside at z = 256 over every outdoor space. Facades end at the ceiling.
- **Corners.** The four quadrants from 160 to 1024 on both axes are building blocks. Where a corner has no
  street-level interior it is one solid structural block with brick on its street faces and caulk elsewhere; around
  the SW room and the lawn the walls are 16 thick or more.
- **Arm ends.** A structural facade across each arm at ±1024.
- **Interiors.** Three rooms, about 384 × 384 × 160 inside, each with a structural shell (walls, floor, ceiling). Their
  doorways are on the west (NW), north (NE) and south (SW) arms, each centred 320 units from the centre, so the east
  arm has none.
  - NW and NE are sealed rooms below their own corner, with the floor at z = -320, outside the fog brush. Their
    facades carry a static door (§5).
  - SW is at street level behind its facade, joined to the south arm by an open doorway.
- **Lawn.** SE: a 384 × 384 yard on dirt, set back from the east arm behind a fence with a gap, open to the sky
  ceiling, with two trees and grass. The fence is a solid detail brush with the alpha-tested fence shader.
- **Windows.** Opaque dark panes on the facades (detail, lightmapped).
- **Street furniture.** Detail brushes in the existing shaders, added at the author's request: a wooden bench on
  metal legs against the wall beside each lamp post, and a square open-topped metal trash bin on each arm's sidewalk
  toward its end, at a different position and side on each arm.
- **Spawns.** One `info_player_start` at the crossroads and four `info_player_deathmatch`, one per arm.

Alternative considered: a street segment (too close to the benchmark to test hint brushes at a junction) and a full
block (four times the layout work before C2 can use it). The user chose the crossroads.

### 2. One generator for the map, lights in a side file

`tools/town/gen_town.py` writes `content/town/maps/town1.map` from the layout table, plus `maps/town1.cameras` and
`maps/town1.pvs` (the same coordinates feed all three, so a layout change cannot strand a camera in a wall).

Lights are the part the author tunes by eye, so they are data, not code: `tools/town/town1_lights.json` holds every
`light*` entity and every entity a light targets, as plain key/value maps. `gen_town.py` appends them to the map.
`gen_town.py --pull-lights <saved.map>` parses the saved map's entities, keeps those whose classname starts with
`light` and the targets they name, and rewrites the JSON with sorted keys. Everything else in the saved map is ignored.

- **Why not hand-edit the generated map and drop the generator?** The next blocks reuse it, and a layout fix (a leak,
  a door moved) must not cost the tuned lights or be redone by hand.
- **Why not keep lights in the script?** Derived intensities are produced by editor commands; copying numbers into
  Python by hand invites drift.
- **Consequence:** the committed `town1.map` is always generator output. The README says so in its first section.

The map is written in the editor's own number format (integers where exact) so that an editor load and save without
edits changes as little as possible. It need not be byte-identical after an editor save; only the pull matters.

### 3. Attached lights are derived in the editor, once

The generator emits each lamp's `light_spot` and the ceiling panel's `light_area` with geometry-determined keys
(`origin`, `angles`, `cone`, `cone_inner`, `radius`, `_proxy 1`, `_shadows 1` for a lamp; `origin`, `angles`, `width`,
`height`, `radius`, `_frustum` for the panel) and placeholder `intensity` and `_color`. The author then runs "Re-derive light from emitter" on each in the editor, saves, and pulls. From then on
the JSON holds the derived values, and re-deriving is a no-op, which the spec's scenario checks.

Alternative considered: re-implement the derivation (strength × `rgbGen const` × mean linear texel × area) in Python.
Rejected: a second copy of `libs/emissivelight` that could disagree with the editor, for five lights.

The `light_area` goes on a ceiling panel that faces down into the room, so the room's floor and walls are in front of
its plane (the benchmark's lesson with the neon sign).

A lamp's emitter is the inside of its reflector cover: a hood of detail brushes, metal outside, with the `lamp`
shader on its inner top face and its four inner side faces, each face wholly exposed so the derived flux counts no
hidden area. That emitter is not planar, so it cannot take a
`light_area`; it takes a proxy. The proxy is a `light_spot` pointing straight down (`angles 90 0 0`), 7 units below
the emitter's centroid (in the map: 12 below the cover's ceiling, on the grid), with `_proxy 1`, `_shadows 1`, `cone 60` and `cone_inner 45`. Decided with the user in review.

This differs from what "Create proxy light from selection" would write (the hemisphere spot: `cone 90`, `_shadows 0`),
on purpose:
- A 90° cone cannot cast, and the lamp is where the street needs a shadow (the plan's "use a spot where occlusion
  matters").
- "Re-derive light from emitter" rewrites only `intensity` (the flux Φ) and `_color`, so the generator's `cone`,
  `cone_inner` and `_shadows` survive it.
- A spot's intensity is Φ / (2π(1 − cos cone)). At `cone 60` that is Φ/π, a Lambertian emitter's on-axis intensity,
  so the pool under the lamp matches the baked emission where the hemisphere proxy is about 2× dim. The cost is that
  the preview and `renderer_sh` give no direct lamp light beyond 60° from the axis, where the bake still gives the
  cover's grazing light. The README records this as the lamp's residual; A9 judges it.

The bake leaves the proxy out and takes the lamp's light from the cover: the stock lightmap gets its direct and
bounced light, the side file gets the bounce, and `renderer_sh` adds the direct part from the spot at runtime.

### 4. Textures and shaders

`tools/town/gen_textures.py` writes into `content/town/textures/town/` with the benchmark generators' helpers (noise,
normal-from-height, bottom-up TGA writer) and a fixed seed. Like `gen_foliage.py`, it records in its header the Pillow
version it was last run with; byte-identical output is only promised for that version. Sets: `asphalt`, `sidewalk`, `brick` (with `_nrm`, `_mr`,
`_ao`), `plaster`, `woodfloor`, `door`, `window`, `metal` (lamp posts), `lamp` (`_c` and `_e`), `panel` (`_c` and
`_e`), `fence`, `dirt`, `bark`, `leaves`, `grass`, and editor images for the tool shaders.

`scripts/town.shader` is written by hand in `bench.shader`'s form, with `scripts/shaderlist.txt` listing `town`.

- **Sky.** `surfaceparm sky`, `skyparms - 512 -` and one stage drawing a flat 8×8 image of the fog colour. Chosen
  over a pak0 farbox because the convention needs sky colour = fog colour, and a flat sky has one colour.
- **Fog.** One fog brush enclosing the street level of the whole block (the SW interior and the lawn included, the
  two sealed rooms below it excluded), with all six faces outside the playable space. Chosen over per-street volumes because Quake 3 draws seams and sorting errors where fog brushes meet and
  where a fog face is seen from inside another. Opacity distance 1024, the benchmark's value, to start with; the author
  set 2048 and a dark green-grey colour on 2026-11-21, after which only a whole street's length reaches fog colour. The colour is one constant in `gen_textures.py`, which paints the sky image with it. The shader is hand-written, so
  the generator also reads the `fogparms` line of `textures/town/fog` from `town.shader` and refuses to run if its
  colour differs from the constant.
  The SW interior is therefore slightly fogged (a 384-unit room reaches about a third of opacity); A10 owns the
  density.
- **The trigger shader has `surfaceparm trigger`**, not `nodraw` alone. q3map2 keeps a `nodraw`-only brush solid, and
  `-shbake` then bakes each teleport trigger as a shadow-casting hull and writes it to the side file's hull mesh.
  Found in the implementation review.
- **Tool shaders.** `caulk`, `clip`, `hint`, `skip`, `trigger` under `textures/town/`, so the pk3 needs nothing
  from a `common.shader`.

### 5. Hints, teleporting doors and the open doorway

- **Hints.** One hint brush across each arm where it meets the crossroads (|coord| = 160) and one mid-arm
  (|coord| = 592): the hint face on the splitting plane, `skip` on the rest. They span floor to ceiling and
  facade to facade.
- **Teleporting doors.** A door is 64 wide and 112 high: a detail-brush slab with the `door` shader, 4 units proud of
  the wall inside an 8-unit metal frame, and a `trigger_teleport` brush of the same size and 16 deep in front of it, targeting a
  `target_position`. The NW and NE rooms each have one on the facade and one inside the room for the way back.
  - A destination stands 48 units out from its own door, facing away from it, so the arrival push carries the player
    away from the trigger. The street side has 270 units of clear floor ahead and the room side over 300.
  - Why not `func_door` with an area portal, as the plan says: the PVS ignores doors, so a room near the crossroads
    stays in the PVS of far street clusters; a closed door's hull also blocks the bake; and a door that loads a
    separate room is what Silent Hill 1 does. A sealed room shares no portal with the street, so it is hidden from
    every street cluster wherever its door sits. Decided with the user in review. The plan's convention text is
    updated to match (tasks group 6).
  - The cost on stock: the teleport sound and flash at both ends, and no view through a doorway. `renderer_sh` builds
    can replace it once the game-code decision is made.
- **The open doorway.** SW has no door: a 64 × 112 opening in a structural frame. A sealed room without a light bakes
  fully black (lightmap, vertex colours and light grid), which is unplayable on stock. With the doorway open, sun and
  lamp light spill in and the room is dark only away from it. It is also the doorway-spill case that B8's and B10's
  measurements name. The south arm's lamp stands across the street from this doorway. The room may be visible from
  the street's clusters; the PVS list asks nothing of it. Decided with the user in review.

### 6. Lights

| Light | Where | Notes |
|---|---|---|
| `light_sun` | one | dim and cool, the benchmark's colour as a starting point |
| `light_spot`, `_proxy 1`, `_shadows 1`, `cone 60` | pointing down, 12 units below the cover's ceiling (7 below the emitter's centroid) of each of four street lamps, one per arm | post on the sidewalk and an arm over the roadway: metal detail brushes; cover: a detail-brush hood, `lamp` on its inner faces |
| `light_area` | NW interior's ceiling panel | `_frustum 70`, the benchmark's value |
| `light` | NE interior | a bare point light with no emitter, so it bakes directly |
| none | SW interior | no light of its own; lit only by spill through its open doorway, for A10's flashlight |

Starting intensities come from the benchmark's table; the author retunes them in the preview and pulls.

### 7. Vegetation reuses the benchmark's generator

`gen_foliage.py`'s `MOD`, `TEXTURES`, `MODELS` and the three shader names become parameters of its `main()` (defaults:
the benchmark's). `gen_textures.py` calls it for `models/town/` and `textures/town/`. The benchmark's outputs must
stay byte-identical, checked with `git status` after running it. The seed stays the same, so the town's trees are the
benchmark's trees; A10 replaces them.

### 8. The PVS check

`tools/town/check_pvs.py <bsp> <pairs file>` parses the IBSP v46 planes, nodes, leafs and visibility lumps with
`struct`, walks the node tree to find each point's leaf, and tests the cluster bit. `maps/town1.pvs` has one pair per
line: `nameA x y z  nameB x y z  hidden|visible`. A point whose leaf has cluster -1 is in solid and fails.

- **Why a script and not `r_showtris` screenshots?** Cluster visibility is exact and needs no window.
- Hidden pairs: each arm's far end from each perpendicular arm's far end, and each sealed room from every arm and from
  the other rooms.
- Visible pairs are listed too (each arm's two ends, the SW interior and the arm outside its doorway) so that a check that
  finds everything hidden, for example from a wrong lump offset, fails.
- It is not folded into the stock check: the stock check answers "does it run on stock", and the benchmark has no
  PVS expectations.

q3map2's `Average clusters visible` line goes into the README as a number to watch, not a gate.

### 9. Frame rate

The exit criterion's "playable framerate" is judged by the author on a walk with `cg_drawFPS 1` on both stock
renderers. Automating it would need a recorded demo and `timedemo`, which is not worth building for one small block;
C2 will need engine-side timing anyway.

## Risks / Trade-offs

- [The fog brush covers the SW interior] → Accepted for A8; density at 1024 is light over a room's depth, and A10
  decides.
- [The stock teleport's sound and flash read as Quake, not Silent Hill] → Accepted on stock for A8; the game-code
  decision owns a quiet transition.
- [A player is teleported back at once, or telefragged in multiplayer] → Destinations lie outside every trigger and
  face away; the acceptance walk tests both directions of both doors. Telefrags at a door are accepted.
- [The light grid and lightmap now span two levels] → Record the BSP's lightmap page count and grid size in the
  README; nothing gates on them.
- [The sky and fog colours differ on screen although equal in the shader] → The seam scenario measures both
  renderers. If one differs (overbright or sRGB handling), adjust the sky image's value for stock and record why.
- [Hint planes through the open doorway or lamp posts fragment the BSP] → Posts and trim are detail; hint brushes stop at the
  facades.
- [The lamp proxy's flux, derived from a small emissive cover, is too dim or bright for the street] → Tune the
  shader's `qer_pbr_emissiveStrength`, re-derive, pull. The light follows the emitter, never the reverse.
- [An editor save reorders or reformats the lights] → The pull writes sorted keys, so the JSON is stable.
- [`-shbake` on a larger map is slower than the benchmark's 1.6 s draft] → Record the three presets' times in the
  README; nothing gates on them.
- [The generator becomes the place where look decisions hide] → Layout numbers live in one table at the top, and
  the README documents each.

## Open Questions

- Whether q3map2's hull pass should skip the brush models of trigger entities whatever their shader. Stock
  `common/trigger` is `nodraw` only, so a map using it gets the shadow-casting boxes this map avoids with
  `surfaceparm trigger`. That is a B5 follow-up in q3map2, outside this change.

- Whether the sealed rooms should later move back behind their facades, at street level, for a `renderer_sh` build that
  walks through real doors. Nothing in A8 depends on it.