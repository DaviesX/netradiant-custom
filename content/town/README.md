# Town

The `town` mod: the Silent Hill 1 style town, as ordinary Quake 3 content with PBR materials (plan §0.2, task A8). Its
first block is `maps/town1.map`, one crossroads. `/home/davis/q3game/q3data/town` is a symlink to this directory, so the
editor (under `Q3.game` with the `town` mod) and ioquake3 (`+set fs_game town`) both see it.

Stock acceptance: **2026-11-21**, by the author, after tuning the sun, the fog, the benches and the trash bins in the
editor and walking the block on stock ioquake3. The accepted state is stock check run `20261121-123233` (engine commit
`0135354f`), which passed every row on `opengl1` and `opengl2`. Two records are still missing: the frame rates of the
walk, and an editor "Re-derive light from emitter" pass over the attached lights ("Lights" below).

## Generated content

**`maps/town1.map` is generator output. Do not edit it in the editor, except to tune lights.** Three scripts write
everything here except `scripts/town.shader`, `scripts/shaderlist.txt`, `levelshots/town1.jpg` and this file:

| Command | Writes |
|---|---|
| `python3 tools/town/gen_textures.py` | every image under `textures/town/` and the meshes under `models/town/` |
| `python3 tools/town/gen_town.py` | `maps/town1.map`, `maps/town1.cameras` and `maps/town1.pvs`, from the layout table at the top of the script and the lights in `tools/town/town1_lights.json` |
| `python3 tools/town/gen_town.py --pull-lights content/town/maps/town1.map` | `tools/town/town1_lights.json`, from the light entities of a map the editor saved |

They need Python 3 with Pillow, use fixed seeds, and their outputs are committed. Re-running them with the Pillow
version recorded in their headers (12.3.0) rewrites every file unchanged. `gen_textures.py` calls the benchmark's
`tools/benchmark/gen_foliage.py` for the trees and grass, so the town's vegetation is the benchmark's until A10
replaces it.

To tune lights:

1. Open `town1.map` in the editor and change, add or delete light entities (and anything a light targets).
2. Save the map.
3. Pull: `python3 tools/town/gen_town.py --pull-lights content/town/maps/town1.map`.
4. Regenerate: `python3 tools/town/gen_town.py`.
5. Commit `town1_lights.json` and `town1.map` together.

The pull keeps every entity whose classname starts with `light`, and every entity a light targets. **Any other edit
made in the editor (a brush, a model, a spawn) is lost at step 4**: change the layout in `gen_town.py` instead. The pull
refuses a map with no light entity, or with a light whose target is missing, and leaves the lights file as it was.

## Layout

Everything is on the 8-unit grid. The numbers are the constants at the top of `gen_town.py`.

| Part | Where | Notes |
|---|---|---|
| Streets | two streets 320 wide along X and Y, crossing at the origin; each arm runs to ±1024 | a 192 roadway and two 64 sidewalks raised 8; floor at z = 0 |
| Sky ceiling | z = 256 over every outdoor space | facades end at the ceiling |
| Arm ends | a brick facade across each arm at ±1024 | |
| NW and NE corners | solid blocks | each has a teleporting door on its facade, 320 from the centre: NW on the west arm, NE on the north arm |
| NW and NE rooms | sealed rooms 384 × 384 × 160 below their corners, floor at z = -320 | no opening to the street; outside the fog volume |
| SW corner | a room 384 × 384 × 160 at street level | an open doorway 64 × 112 on the south arm, 320 from the centre; no light of its own |
| SE corner | a lawn 384 × 384 on dirt, open to the east arm | a chain-link fence with a 64 gap, two trees, three grass patches and three clumps |
| Street lamps | one per arm: 448 out on the west, north and east arms, 320 on the south arm, across the street from the SW doorway | post on the sidewalk, arm over the roadway, reflector cover 48 × 48 with an emissive inside |
| Benches | one against the wall beside each lamp post, 94 long | a wooden seat 26 deep, 22 above the sidewalk, a thin back rising to 48, and two metal legs, each 6 in from its end. It starts 32 units along the arm from the lamp post's centre, where the author moved the north arm's bench. The proportions are the ones the author tuned in the editor on the east arm's bench (2026-11-21), applied to all four |
| Trash bins | one per arm toward its end, 40 × 40 × 56 with an open top, oversized like the lamp covers (the height is the author's, tuned on the west arm's bin): 800 out on the west arm, 856 north, 776 east, 832 south | on the sidewalk 4 units off the wall, leaving 20 of its 64, so a player steps off the kerb to pass; opposite the lamp's sidewalk on the west and east arms, on the lamp's on the north and south arms, so no two arms match |
| Windows | opaque panes 64 × 80 on both facades of every arm, 480 and 736 out | not where the lawn opens |
| Hint brushes | two across each arm: where it meets the crossroads (160) and mid-arm (592) | |
| Fog | one volume over the whole street level, z = -8 to 264 | every face lies inside the hull, so the engine fogs the volume without a fog surface. Its opacity distance is 2048, the length of a whole street, so nothing in this block is ever fully fogged |
| Spawns | `info_player_start` at the crossroads; four `info_player_deathmatch`, 704 out on each arm | |

The map has 47 structural brushes (8 of them hints, one the fog), 138 detail brushes, four `trigger_teleport` brushes
and no patch. Worldspawn sets `_blocksize 0`. There is no area portal and no door entity.

### Doors teleport

Walking into the door on the NW or NE facade takes the player into that corner's room, and the door inside the room
takes them back, as in Silent Hill 1. Each door is a static slab in a metal frame with a `trigger_teleport` 16 deep in
front of it, targeting a `target_position` 48 units out from the door at the other end, facing away from it.

- A sealed room shares no portal with the street, so the PVS hides it from every street cluster wherever its door is.
- Stock Quake 3 plays its teleport sound and flash at both ends and pushes the player forward for 160 ms. A quiet
  transition needs game code (the open baseq3-or-mod decision).
- A player standing on a destination is telefragged by the next arrival.
- The trigger shader carries `surfaceparm trigger`. Without it q3map2 treats the trigger brush as solid, and
  `-shbake` makes it a shadow-casting hull in the lightmap and in the side file's hull mesh: an invisible box in
  front of each door. With it the light stage reported 188 hulls where it had reported 192 (before the benches and bins, which bring it to 224).
- Checked on stock ioquake3 (2026-11-21, both renderers) by a `setviewpos` into each of the four triggers: each
  moved the player to the other end, facing away from the door there, about 120 to 145 units out from it, and
  the player was not teleported again.

The SW room is the exception: a walk-in room, so that street light spills through its doorway in the bake. A sealed
room without a light would bake fully black, which is unplayable on stock.

## Materials

`scripts/town.shader`: vanilla Quake 3 stages plus top-level `qer_pbr_*` keywords. Every image is a placeholder.

| Shader | Use | Maps and notes |
|---|---|---|
| `asphalt` | roadway | normal map |
| `sidewalk` | sidewalks, the SW threshold | normal map |
| `brick` | facades | normal, metallic-roughness and occlusion maps |
| `window` | facade windows | opaque, roughness 0.15 |
| `door` | the four teleporting doors | fitted once to each door face |
| `metal` | lamp posts, door and window frames, the panel's sides, bench legs, trash bins | metallic-roughness map |
| `lamp` | the inside of a lamp's reflector cover | emissive: additive `lamp_e`, `rgbGen const ( 1 1 1 )`, `qer_pbr_emissiveStrength 16` |
| `fence` | the lawn's fence | alpha-tested, `cull none`, solid, casts through its alpha (`surfaceparm alphashadow`) |
| `plaster` | interior walls and ceilings | |
| `woodfloor` | interior floors, bench seats and backs | normal map |
| `panel` | the NW room's ceiling light | emissive: additive `panel_e`, `qer_pbr_emissiveStrength 8` |
| `bark`, `leaves`, `grass` | the vegetation's model surfaces | as the benchmark's: `q3map_forceMeta`, lightmapped; bark solid through `q3map_clipModel` |
| `dirt` | the lawn's ground | |
| `sky` | the ceiling | a flat cloud layer (`skyparms - 512 -`) drawing `sky.tga`, one colour; `qer_pbr_emissiveStrength 14.6` makes it the sky light ("Lights") |
| `fog` | the fog volume | `fogparms ( 0.200392 0.258824 0.231373 ) 2048`, a dark green-grey the author set on 2026-11-21 |
| `caulk`, `clip`, `hint`, `skip`, `trigger` | tool shaders | `nodraw`; the pk3 needs no `common.shader`. `trigger` also has `surfaceparm trigger` (see "Doors teleport") |

Rules, as for the benchmark: no rend2 keyword, no `q3map_sun` or `q3map_surfacelight`, no image named `<base>_n`,
`_nh` or `_s` (normal maps are `*_nrm`), every TGA stored bottom-up, and every drawn surface except sky and fog has a
`$lightmap` stage.

**Fog colour equals sky colour.** The sky image is the flat colour `FOG_RGB` = 51 66 59 in `gen_textures.py`, and
the fog's `fogparms` colour is the same value over 255. The shader is written by hand, so `gen_textures.py` reads its
`fogparms` line and refuses to run, writing nothing, when the two differ. To change the colour, change both.

The emissive images `lamp_e` and `panel_e` are flat, so an attached light's derived value does not depend on how a
face maps the image.

## Lights

The editor's physical light entities, held in `tools/town/town1_lights.json`:

| Entity | Keys | Role |
|---|---|---|
| `light_sun` | `origin 0 0 200`, `angles 50 145 0`, `intensity 5`, `_color 0.973327 0.60087 0.441245` | a warm sun the author tuned in the preview on 2026-11-21; it travels north-west, so it enters the SW doorway |
| `light_spot` × 4 | `origin` under each cover (`-448 -48 180`, `-48 448 180`, `448 48 180`, `48 -320 180`), `angles 90 0 0`, `cone 60`, `cone_inner 45`, `radius 512`, `intensity 209104`, `_color 1 0.672443 0.304987`, `_proxy 1`, `_shadows 1` | each street lamp's proxy: pointing straight down, casting, attached to the cover's emissive inside |
| `light_area` | `origin -560 560 -168`, `angles 90 0 0`, `width 64`, `height 64`, `radius 512`, `intensity 8`, `_color 0.806952 0.871367 1`, `_frustum 70` | the NW room's ceiling panel, facing down |
| `light` | `origin 560 560 -200`, `intensity 150000`, `radius 400`, `_color 1 0.85 0.65` | the NE room's bare point light; it has no emitter, so it bakes directly |
| sky light | `textures/town/sky`'s `qer_pbr_emissiveStrength 14.6` | the sky as an environment light, half the sun's strength ("The sky light" below) |

The SW room has no light.

**The attached lights' `intensity` and `_color` were computed, not yet derived in the editor.** They follow the
editor's formula (`libs/emissivelight.h`): the shader's strength × its `rgbGen const` × the emissive image's linear
colour gives the radiance, a proxy's flux is π × radiance × emitting area (4160 square units per cover), the largest
channel becomes `intensity` and the rest `_color`, printed at six significant digits. So "Re-derive light from
emitter" should leave them unchanged, but that has not been run. To check: select each lamp's spot with its cover's
five emissive faces, and the `light_area`, run the command, and save; a pull must then leave
`town1_lights.json` unchanged. Re-derive again after any change to `lamp_e`, `panel_e` or their shaders' strength.

### The sky light

The sky shader lights the map as an environment light (gamepack README, "Sky light"). It is a cloud layer with no
`rgbGen const` over the flat `sky.tga`, sRGB 51 66 59, whose linear mean is `M = (0.03310, 0.05448, 0.04374)`. An
open floor facing up receives `E = π × s × M`. The strength is set so that the largest channel is half the sun's
`intensity 5`: `π × s × 0.05448 = 2.5` gives `s = 14.6067`, and the shader declares `qer_pbr_emissiveStrength 14.6`,
for `E = (1.518, 2.499, 2.006)` on an open floor. A facade gets half that, a ceiling none. `make test-sky-light` checks
it against the committed `sky.tga` and `town1_lights.json` ("Half the sun"). Change the strength whenever the sun's
`intensity` changes.

The sky light changes nothing else: the image, the stages, `skyparms` and the fog colour are as before, so the stock
look and the fog seam are unchanged, and since the shader has no additive stage, q3map2 `-shbake` doesn't take it for
an emitter.

The editor preview has no sky visibility, so it lights every interior as if it were open to the sky. Make interior
preview checks (the unlit SW room being dark away from its doorway, the sealed rooms under their own lights) with the
camera preference "Lighting sky" off.

### The lamp proxy and its residual

A lamp's emitter is the inside of its cover: a 40 × 40 ceiling at z = 192 and four 40 × 16 inner sides, 4160 square
units in all, every face wholly exposed. It is not planar, so it takes a proxy, not a `light_area`.

The proxy is not the one "Create proxy light from selection" would make (a hemisphere spot, `cone 90`, `_shadows 0`):

- It casts. A 90° cone cannot, and the lamp is where the street needs a shadow: the post's falls across the sidewalk
  and up the facade in the preview.
- Its cone is 60°. A spot's intensity is Φ / (2π(1 − cos cone)), which at 60° is Φ/π, a Lambertian emitter's on-axis
  intensity. So the pool under the lamp matches the baked emission, where the hemisphere proxy is about 2× dim.
- **Residual:** the preview and `renderer_sh` give no direct lamp light beyond 60° from the axis (312 units from the
  point below the lamp, at street level). The bake still has the cover's grazing light there. A9 judges it.

"Re-derive light from emitter" rewrites only `intensity` and `_color`, so `cone`, `cone_inner`, `_shadows` and the
origin (12 units below the cover's ceiling, which is 7 below the emitter's centroid) survive it.

`-shbake` leaves the five attached lights out ("5 attached lights left out (1 light_area, 4 _proxy)") and takes their
light from the emissive faces: the stock lightmap gets the emitters' direct and bounced light, the side file gets the
bounce, and `renderer_sh` adds the direct part from the lights at runtime. No emitter is unattached, so `-shbake`
prints no emitter warning.

Stock Quake 3 frees `light_spot`, `light_sun` and `light_area` with a "doesn't have a spawn function" line; the stock
check's allowlist already covers those three, and the town adds no entry.

## Compiling

The overlay's Town builds, or by hand with the bundled q3map2 (built with `SH_BAKER=1`):

1. `-meta -keeplights`
2. `-vis -saveprt`
3. `-light -shbake`, plus the preset's flags

Measured on 2026-11-21 on the accepted map, with the editor open on the same machine:

| Build | Light stage | Luxel points | Points per bake | Indirect + full bake | Light stage total |
|---|---|---|---|---|---|
| Town (draft) | `-shsamples 128` | 29458 | 64330 | 1.4 s + 1.8 s | 3 s |
| Town (pre-release) | `-shsamples 512 -super 2` | 108344 | 230401 | 18.2 s + 22.8 s | 41 s |
| Town (final) | `-shsamples 2048 -super 2` | 108344 | 230401 | 39.0 s + 46.5 s | 86 s |

Each build exits 0, leaks nothing and writes `maps/town1/town1.shsf` (3.4 MB). The BSP is 0.49 MB with 3 lightmap
pages and a light grid of 5445 points, of which 1333 are baked and the rest lie in solid.

## PVS check

```
python3 tools/town/check_pvs.py content/town/maps/town1.bsp content/town/maps/town1.pvs
```

Run it after compiling (it needs the `-vis` stage). It locates each named point of `maps/town1.pvs` in the BSP and
compares the cluster visibility of each pair with the pair's mark. It exits 0 only if every pair matches, and a point
in solid fails. Doors and entities play no part in the PVS; only structural brushes and hint brushes do.

The 20 pairs:

| Mark | Pairs | Why |
|---|---|---|
| hidden | the far end of each X arm from the far end of each Y arm (4) | the corner blocks and the mid-arm hints: entering one street drops the far half of the crossing one |
| hidden | each sealed room from the four arm ends, the crossroads and the SW room (12), and from the other sealed room (1) | a sealed room shares no portal with anything |
| visible | the two ends of the X street, and of the Y street (2) | a straight street is one line of sight; these also prove the check can read "visible" |
| visible | the SW room from the south arm outside its doorway (1) | the open doorway |

The SW room is deliberately not marked hidden from anything: it is open to the street.

On 2026-11-21 the map compiled to 16 clusters, and q3map2 reported `Average clusters visible: 9.00 (56.250%/total)`.
That number is one to watch as the town grows, not a gate.

## Views

`maps/town1.cameras` requests eight views. The stock check records the eye position the engine reports after
`setviewpos`'s push (run `20261121-123233`, `opengl1`):

| View | Eye, yaw | Shows |
|---|---|---|
| `street` | -783 0 50, 0 | the whole X street from its west end: the far facade, 1807 units away and about 88% fogged, meeting the sky |
| `crossroads` | -305 -29 50, 14 | the crossroads from the west arm, with the NW door on the left |
| `lamp` | -31 -533 50, 79 | the south arm's lamp, its pool of light, the bench beside its post and the SW doorway |
| `nw-room` | -609 510 -269, 45 | the NW room under its ceiling panel |
| `ne-room` | 510 510 -269, 45 | the NE room under its point light |
| `sw-room` | -383 -320 50, 0 | the SW room looking east at its open doorway: the street outside and the light spilling onto the floor |
| `sw-room-back` | -357 -320 50, 180 | the SW room looking west from the doorway: the wall farthest from it |
| `lawn` | 438 -40 50, -60 | the lawn through its fence from the east arm: trees and grass |

## Stock check

```
tools/stockcheck/stockcheck.py content/town town1
```

Run `20261121-123233` (engine commit `0135354f`) passed every row on `opengl1` and `opengl2`, with
`tools/stockcheck/allowlist.txt` unchanged. `town.pk3` (2.3 MB, 36 files) holds the BSP, the side file, `levelshots/town1.jpg`
(cut from run `20261121-123158`'s `opengl1` `crossroads` screenshot), the two scripts and the 31 images.

Two things are measured from its screenshots:

**Fog and sky seam** (`street` view, 1280×720): the sky at pixel (640, 60) and the far facade at (640, 330). That
facade is 1807 units away, about 88% of the fog's opacity distance, so it is a building still being drawn where it
meets the sky.

| Renderer | Sky | Far facade | Largest difference |
|---|---|---|---|
| `opengl1` | 51 66 59 | 48 62 55 | 4 |
| `opengl2` | 60 80 71 | 57 75 66 | 5 |

Both are within the limit of 8 per channel. With the opacity distance at 2048 this is the only sightline long enough
to test the seam; the end facade of a single arm, at most 1184 units from the crossroads, stands out from the sky by
about 20 per channel.

**Spill into the SW room:** the brightest floor texel inside the doorway (`sw-room`, the window x 340–760, y 520–600),
the mean of the west wall (`sw-room-back`, x 440–840, y 200–400), and the darkest texel of both screenshots.

| Renderer | Floor inside the doorway | West wall | Darkest texel |
|---|---|---|---|
| `opengl1` | 66 50 34 | 16 20 18 | 10 13 11 |
| `opengl2` | 79 59 39 | 16 21 18 | 9 13 11 |

The floor inside the doorway is lit, the far wall is darker, and nothing is pure black. The fog lifts every value in
the room toward its colour, the far wall's most, so the real contrast is larger than these numbers.

These figures are from the accepted run.

The author accepted the block on 2026-11-21. The frame rates of the walk (`cg_drawFPS 1`, both renderers) were not
written down; add them here when next measured.
