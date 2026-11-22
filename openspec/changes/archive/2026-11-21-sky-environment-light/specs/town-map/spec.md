# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
