# Spec Delta

## MODIFIED Requirements

### Requirement: Benchmark lights are the editor's physical lights
The benchmark SHALL light the scene with the editor's physical light entities:
- one `light_sun`;
- at least one point light (`light`, with `intensity` and `radius`);
- at least two `light_spot` entities, one aimed by `angles` and one by `target`;
- at least one `light_spot` setting `_shadows 0`;
- one `light_area` on the neon sign's front face, created with "Create area light from selected face", so that "Re-derive light from emitter" leaves its keys unchanged. The sign's other neon faces (the ends, top and bottom) get no light.

The light keys and values SHALL be those recorded in `content/benchmark/README.md`, which the author tunes in the editor preview.

#### Scenario: Entity inventory
- **WHEN** the map's entities are listed
- **THEN** they contain one `light_sun`, at least one `light`, at least two `light_spot` entities and one `light_area` whose keys and values match the README's light table, at least one spot sets `_shadows 0`, and every `target` resolves

#### Scenario: Lights preview under Q3.game
- **WHEN** the map is opened in lighting mode under `Q3.game` with `fs_game benchmark`
- **THEN** the sun, every point light, every spot light and the area light light the scene

#### Scenario: Sign light matches its emitter
- **WHEN** the sign's `light_area` is selected and "Re-derive light from emitter" is run
- **THEN** its `intensity` and `_color` keep their values, and the saved map is unchanged
