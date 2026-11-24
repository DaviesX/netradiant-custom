# town-map Specification

## Purpose
The town level: stock Quake 3 content authored under `Q3.game` that the stock renderers run as a lightmapped level and
`renderer_sh` runs with PBR materials. This spec is its content contract: location, generation, materials, lights,
visibility conventions, compile, views and stock acceptance.

## Requirements

### Requirement: Town lives in the town mod directory
The town SHALL be stored under `content/town/`, laid out as a Quake 3 mod directory (`maps/`, `scripts/`, `textures/`,
`models/`, `levelshots/`). Its first block SHALL be `maps/town1.map`. It SHALL be usable by the editor under `Q3.game`
with the `town` mod and by ioquake3 with `+set fs_game town`, and SHALL need no game code beyond baseq3's.

#### Scenario: Editor opens it
- **WHEN** the editor runs under `Q3.game` with the `town` mod selected
- **THEN** `maps/town1.map` opens and every `textures/town/*` shader appears in the texture browser

#### Scenario: Engine loads it
- **WHEN** ioquake3 runs with `+set fs_game town +devmap town1` on the packed pk3
- **THEN** the map loads and the player spawns on the street

### Requirement: The block is one crossroads
`town1` SHALL be two streets crossing at right angles, giving four arms, each closed at its far end by a building.
Three of the four corners SHALL each hold one enterable interior, each entered from a different arm. The fourth corner
SHALL hold a lawn open to the sky. Every part SHALL be reachable from the player start without cheats.

#### Scenario: Inventory
- **WHEN** the map is inspected
- **THEN** it has four street arms, three interiors with one doorway each (two with a teleporting door, one open), and one lawn

#### Scenario: Walkable
- **WHEN** a player walks from the start
- **THEN** they can reach the end of every arm, the inside of every interior (two of them through their doors' teleports) and the lawn without `noclip`, and can return to the street from each interior

### Requirement: Town content is generated and reproducible
The map, the placeholder textures and the vegetation meshes SHALL be written by committed generator scripts that need
only Python 3 with Pillow, take no network or external art, and use fixed seeds. The outputs SHALL be committed, and
re-running the generators with the Pillow version recorded in them SHALL rewrite every one of them unchanged.

#### Scenario: Regeneration is a no-op
- **WHEN** the generators are run on a clean checkout with the Pillow version recorded in them
- **THEN** `git status` shows no change under `content/town/`

#### Scenario: Benchmark untouched
- **WHEN** the benchmark's vegetation generator is run after this change
- **THEN** `git status` shows no change under `content/benchmark/`

### Requirement: Lights are tuned in the editor and kept by the generator
The map's light entities and their targets SHALL be held in one committed lights file that the map generator reads.
The generator SHALL offer a pull mode that reads the light entities from a `town1.map` saved by the editor and
rewrites the lights file, so that a regenerated map carries the tuned lights. The README SHALL state that any other
editor edit is lost on regeneration.

#### Scenario: Round trip
- **WHEN** a light's `intensity` is changed in the editor, the map is saved, the pull mode is run and the map is regenerated
- **THEN** the regenerated map's light has the new `intensity`, and its brushes and models are those of the committed map

#### Scenario: Pull refuses a map without lights
- **WHEN** the pull mode is run on a map with no light entity
- **THEN** it exits non-zero and leaves the lights file unchanged

### Requirement: Town materials are vanilla shaders with PBR keywords
Every town material SHALL be a `scripts/town.shader` entry named `textures/town/*`, using only vanilla Quake 3 shader
keywords plus top-level `qer_pbr_*`. Emissive SHALL be an additive stage. The shaders SHALL contain no rend2 keyword,
no `q3map_sun` and no `q3map_surfacelight`. No image SHALL be named `<base>_n`, `<base>_nh` or `<base>_s`. The town
SHALL reference no shader or image outside `textures/town/` except stock pak content.

#### Scenario: Reserved suffix scan
- **WHEN** `content/town/textures/` is listed
- **THEN** no file ends in `_n`, `_nh` or `_s` before its extension

#### Scenario: No rend2 keywords
- **WHEN** the editor loads `town.shader`
- **THEN** it prints no rend2-keyword warning

#### Scenario: Every drawn surface is lightmapped
- **WHEN** the shaders the map draws are listed, leaving out sky, fog and the `nodraw` tool shaders
- **THEN** each one has a `$lightmap` stage

### Requirement: Visibility conventions
The map SHALL follow the town's visibility conventions:
- a sky ceiling no more than 256 units above the street;
- building shells of structural brushes, with ornament and props as detail brushes or models;
- a hint brush across every street arm;
- no area portal and no door entity;
- worldspawn `_blocksize` `0`.

#### Scenario: Convention check
- **WHEN** the map file is inspected
- **THEN** the sky brushes' undersides are at or below z = 256, every lamp post, kerb and trim brush is detail, each arm is crossed by at least one hint brush, and worldspawn has `_blocksize` `0`

### Requirement: Doors teleport to sealed interiors
Two interiors SHALL be sealed rooms with no opening to the street. Each SHALL be entered by walking into a static door
on its facade, which teleports the player into the room through stock `trigger_teleport` and `target_position`
entities, and left the same way through a door inside the room. Each destination SHALL face away from its door with at
least 160 units of clear floor ahead and SHALL lie outside every trigger.

#### Scenario: Entering and leaving on stock
- **WHEN** a player walks into a facade door in stock ioquake3, and then into the door inside the room
- **THEN** they arrive inside that door's room facing into it, and then back on the sidewalk outside the same facade door facing the street, without being teleported again on arrival

#### Scenario: Sealed from the street
- **WHEN** the compiled BSP's visibility is read
- **THEN** a point inside each sealed interior is hidden from every listed street point and from the other interiors

### Requirement: One interior has an open doorway
The third interior, the one with no light of its own, SHALL be joined to the street by an open doorway with no door,
so that street light reaches it in the bake. It MAY be visible from the street's clusters.

#### Scenario: Walk in
- **WHEN** a player walks through the open doorway
- **THEN** they enter the room without a teleport

### Requirement: Fog colour equals sky colour
One fog volume SHALL cover the block, and its `fogparms` colour SHALL equal the colour the sky shader draws, so that a building fading into fog meets the sky without a visible seam on the stock renderers and in the editor's lighting mode.

#### Scenario: Colours agree in the source
- **WHEN** the `fogparms` colour in `scripts/town.shader` is changed so that it differs from the colour the texture generator gives the sky image, and the texture generator is run
- **THEN** it exits non-zero, names both colours and writes no sky image

#### Scenario: Seam check
- **WHEN** the stock check's screenshot of a street view whose end building is still drawn through the fog (nearer than the fog's opacity distance) is sampled on `opengl1` and on `opengl2`, once in the sky and once on that building just below the ceiling
- **THEN** the two samples differ by no more than 8 in any 8-bit channel

#### Scenario: Editor seam check
- **WHEN** the editor's lighting-mode capture of the `street` view (shadows on, ambient 0, "Lighting fog" on) is sampled at the image's horizontal centre, once in the sky and once on the far facade just below the ceiling
- **THEN** the two samples differ by no more than 8 in any 8-bit channel, and the sky sample is 51 66 59 within 1

### Requirement: Town lights are the editor's physical lights
The map SHALL be lit only by `light`, `light_spot`, `light_sun` and `light_area` entities and by its sky shader's sky light (requirement "Town sky light"). It SHALL hold one
`light_sun`, a downward shadow-casting `light_spot` with `_proxy 1` at each street lamp, one interior lit by a `light_area`, one lit by a point
`light`, and one interior with no light of its own. Every emissive surface SHALL have an attached light (`light_area`,
or `_proxy 1`) whose `intensity` and `_color` "Re-derive light from emitter" leaves unchanged.

#### Scenario: No unattached emitter
- **WHEN** the map is compiled with `-light -shbake`
- **THEN** q3map2 prints no unattached-emitter warning

#### Scenario: Attached lights match their emitters
- **WHEN** "Re-derive light from emitter" is run on each attached light in the editor
- **THEN** the saved map is unchanged

#### Scenario: The open interior is lit by spill only
- **WHEN** the stock check's two screenshots of the unlit interior (one toward the doorway, one toward the wall farthest from it) are examined on either renderer
- **THEN** the floor inside the doorway is visibly lit, the wall farthest from the doorway is darker than that floor, and no surface in the room is pure black

#### Scenario: Lights preview under Q3.game
- **WHEN** the map is opened in lighting mode with shadows on and "Lighting sky" off
- **THEN** every lamp lights a pool of street beneath it and nothing above its cover, its post casts a shadow, and the unlit interior is dark away from its doorway

### Requirement: Town vegetation is lightmapped model meshes
The lawn SHALL hold at least two trees and grass as `misc_model` entities referencing meshes under `models/town/`,
which q3map2 merges into the BSP. Their shaders SHALL carry `q3map_forceMeta` and a `$lightmap` stage. Grass SHALL be
static crossed quads.

#### Scenario: Merged and lightmapped
- **WHEN** the map is compiled
- **THEN** the BSP holds no `misc_model` entity, and the bark, leaves and grass surfaces have lightmaps

### Requirement: Town compile
The map SHALL compile with the bundled q3map2 using `-meta -keeplights`, then `-vis -saveprt`, then `-light -shbake`,
with no leak and no `Unknown q3map_* directive` warning, and SHALL write `maps/town1/town1.shsf`.

#### Scenario: Draft build
- **WHEN** the three stages run at the "Town (draft)" settings
- **THEN** each exits 0, no stage prints `leaked`, and the side file exists

### Requirement: The PVS behaves like an indoor level
A committed check SHALL read the compiled BSP and a committed list of named point pairs, each marked hidden or visible,
and SHALL exit non-zero if any pair's cluster visibility differs from its mark. The list SHALL mark as hidden at least:
the far end of each arm from the far end of each perpendicular arm, and each sealed interior from every arm and from
the other interiors. It SHALL mark as visible at least the two ends of each arm. A listed point that lies in solid SHALL fail the check.

#### Scenario: Check passes
- **WHEN** the check runs on the compiled `town1.bsp`
- **THEN** it exits 0 and prints each pair's result

#### Scenario: Point in solid
- **WHEN** a listed point is inside a brush
- **THEN** the check exits non-zero and names the point

### Requirement: Town views
`maps/town1.cameras` SHALL list view requests in the stock check's format. It SHALL include a view along a whole
street, a view of an arm's end building near the fog's opacity distance against the sky, a view of the crossroads, a view under a street lamp, at least one view
inside each interior and one of the lawn. The README SHALL describe what each view shows.

#### Scenario: Views resolve
- **WHEN** the stock check runs
- **THEN** every view yields a screenshot on both renderers, and each reported position is within 256 units of its request

### Requirement: Town passes the stock check
`tools/stockcheck/stockcheck.py content/town town1` SHALL exit 0, with no allowlist entry added for the town. The pk3
SHALL hold the BSP, its lightmaps, the side file, `levelshots/town1.jpg`, the shader scripts and the images under
`textures/`, and nothing else.

#### Scenario: Stock check
- **WHEN** the stock check is run on the committed town
- **THEN** every row is PASS on `opengl1` and `opengl2`

### Requirement: Stock acceptance by the author
The author SHALL walk the block on stock ioquake3 with `opengl1` and `opengl2` at default cvars and review the stock
check's screenshots, and the README SHALL record the date, the run and the engine commit. Acceptance requires that the
frame rate holds at the default `com_maxfps` cap along the whole walk and that the screenshots show no content error.

#### Scenario: Acceptance recorded
- **WHEN** the README is read
- **THEN** it names the accepting run, its date and the engine commit, and the measured frame rate on both renderers

### Requirement: Town sky light
`textures/town/sky` SHALL declare a `qer_pbr_emissiveStrength` that makes the sky irradiance on an open, upward-facing surface half the town sun's `intensity`, in the largest channel and within 1%. Today that is 14.6 against `intensity 5`. Its image, stages and `skyparms` SHALL stay as they are, so the "Fog colour equals sky colour" seam and the stock look don't change. `content/town/README.md` SHALL state the strength and how it follows from the sun.

#### Scenario: Half the sun
- **WHEN** the sky light helpers evaluate `textures/town/sky` with the committed `sky.tga` at normal +Z
- **THEN** the largest channel of E is 2.5 within 1%, half of the `light_sun` `intensity` 5 in `town1_lights.json`

#### Scenario: Streets in sun shadow are lit by the sky
- **WHEN** the `street` and crossroads views are captured in lighting mode with shadows on and ambient 0, once with "Lighting sky" on and once off
- **THEN** sunlit and sun-shadowed street surfaces are both brighter with the sky on, and the sky surface and the fog look the same in both captures

#### Scenario: Stock unchanged
- **WHEN** the stock check runs on the town after the change
- **THEN** every row is PASS, and the `-light -shbake` log has no warning that names `textures/town/sky`. q3map2 doesn't treat `textures/town/sky` as an emitter, because it has no additive stage (a sky with an additive cloud stage and a strength would count as one until B12).
