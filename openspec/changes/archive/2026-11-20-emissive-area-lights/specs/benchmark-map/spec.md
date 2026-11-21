# Spec Delta

## MODIFIED Requirements

### Requirement: Benchmark lights are the editor's physical lights
The benchmark SHALL light the scene with the editor's physical light entities:
- one `light_sun`;
- at least one point light (`light`, with `intensity` and `radius`);
- at least two `light_spot` entities, one aimed by `angles` and one by `target`;
- at least one `light_spot` setting `_shadows 0`;
- one `light_area` on the fireplace's fire face, created with "Create area light from selected face", with `radius` raised and `_frustum` set by hand so that it reaches the pillar and the floor and casts the pillar's shadow, and with `intensity` and `_color` such that "Re-derive light from emitter" leaves its keys unchanged. The neon sign gets no light: its five neon faces stay unattached emitters.

The light keys and values SHALL be those recorded in `content/benchmark/README.md`, which the author tunes in the editor preview.

#### Scenario: Entity inventory
- **WHEN** the map's entities are listed
- **THEN** they contain one `light_sun`, at least one `light`, at least two `light_spot` entities and one `light_area` whose keys and values match the README's light table, at least one spot sets `_shadows 0`, and every `target` resolves

#### Scenario: Lights preview under Q3.game
- **WHEN** the map is opened in lighting mode under `Q3.game` with `fs_game benchmark`
- **THEN** the sun, every point light, every spot light and the area light light the scene

#### Scenario: Fireplace light matches its emitter
- **WHEN** the fireplace's `light_area` is selected and "Re-derive light from emitter" is run
- **THEN** its `intensity` and `_color` keep their values, its hand-set `radius` and `_frustum` are kept, and the saved map is unchanged

### Requirement: Documented views
`maps/bench.cameras` SHALL list named view requests, one per line: name, player origin x y z, yaw. It SHALL include `sign`, from the `info_player_start` origin facing the emissive sign, and `fireplace`, which frames the fire, the metal pillar and the floor between them, and the views together SHALL show the fence, the glass, both trees, grass on the lawn and a tree canopy's shadow on the ground. The authoritative position of a view is the eye position recorded by the stock check's `opengl1` run, and editor captures of a view SHALL use that recorded position and yaw from the same check run.

#### Scenario: Shared views
- **WHEN** the editor capture of `sign` is taken at the position the stock check recorded
- **THEN** both images frame the same scene within the difference in field of view

#### Scenario: Foliage view
- **WHEN** the editor capture of the view that frames the foliage is taken in lighting mode with shadows on, at the position the stock check recorded
- **THEN** it shows both trees, grass and a canopy shadow on the ground, and the stock screenshots of that view show the same trees and grass

#### Scenario: Fireplace view
- **WHEN** the stock check screenshots `fireplace` on both renderers
- **THEN** the fire shows in the opening with its own image, and the surround, the pillar, the floor and the walls show theirs

## ADDED Requirements

### Requirement: Benchmark fireplace
The north interior SHALL hold a one-sided fireplace against the inner face of the north wall, in line with the metal pillar so that the pillar stands in its light: a brick and concrete surround (hearth slab, jambs, lintel and mantel shelf) around a recessed opening whose back face is the only `textures/bench/fire` surface and faces −Y, into the room. The firebox's other faces SHALL NOT be emissive, and faces that can't be seen SHALL be caulk. The brushes SHALL be axis-aligned on the 8-unit grid and SHALL follow the map's existing brushes, so that existing brush numbers are unchanged. `textures/bench/fire` SHALL follow the neon's pattern: a `$lightmap` stage, the base colour, and an additive emissive stage with `rgbGen const`, plus `qer_pbr_emissiveStrength`. Its images SHALL be written by a committed generator script with a fixed seed that needs only Python 3 with Pillow and follows the benchmark's image rules.

#### Scenario: One emitter face
- **WHEN** the map's brush faces are listed
- **THEN** exactly one face uses `bench/fire`, on plane y = 736 facing −Y, and the map compiles with no leak

#### Scenario: Fire images regenerate
- **WHEN** the fire generator is re-run with the Pillow version recorded in it
- **THEN** it rewrites `fire_c.tga` and `fire_e.tga` without changing them, each stored bottom-up
