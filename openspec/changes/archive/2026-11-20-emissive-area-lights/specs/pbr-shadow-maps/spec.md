# Spec Delta

## ADDED Requirements

### Requirement: Area light frustum shadows
A `light_area` with `_frustum` greater than 0 SHALL be rendered into a tile of the spot shadow atlas, with:
- a perspective projection from the rectangle's centre along its normal;
- a field of view of twice `_frustum`, clamped below 2 × (90° − 1.15°);
- a near plane of 4 map units and a far plane at its effective radius (max(`radius`, half-diagonal)).

The square frustum SHALL be aligned with the rectangle's right and up axes, roll included, as the gizmo's frustum pyramid is.

Its pass SHALL multiply its contribution by the shadow term sampled as for spot lights: penumbra factor 1, with NdotL taken toward the rectangle's centre, except that its bias SHALL be in map units (one shadow-tile texel at the receiver's distance, times (1 − NdotL) along the normal and times the slope clamped to [1, 3] toward the light) with no bias in normalised depth. A receiver in front of the light but beside the frustum SHALL be treated as shadowed, so a casting `light_area` lights only within its `_frustum` (an area light shades its whole front hemisphere, and treating that region as lit leaks light past occluders the map cannot see). Within the frustum, the shadow term SHALL be multiplied by 1 − smoothstep(`_frustum` − 10°, `_frustum`, θ) for each axis of the square frustum (θ the receiver's angle off the light's axis in that axis's plane, the lower edge clamped at 0°), so the light fades out toward the edge instead of stopping. A receiver nearer than the near plane SHALL be treated as lit. Spot lights keep treating receivers outside their map as lit. A `light_area` with `_frustum` 0 SHALL generate no shadow map, and its contribution SHALL NOT be attenuated by any shadow term.

#### Scenario: Pillar shadow from an area light
- **WHEN** a `light_area` with `_frustum 70` faces a wall with a pillar in between, inside the frustum
- **THEN** the wall shows the pillar's shadow

#### Scenario: Pillar shadow from the fireplace
- **WHEN** the benchmark's `fireplace` view is framed in lighting mode with shadows on, with the fireplace's `light_area` at `_frustum 70`
- **THEN** the floor behind the metal pillar, seen from the fire, is darker than with `_frustum` removed, and the floor beside that shadow is unchanged

#### Scenario: Shadow touches its caster
- **WHEN** the benchmark fireplace's `light_area` (`_frustum 70`) lights the floor at a grazing angle around the metal pillar, about 320 units away, with shadows on
- **THEN** the pillar's shadow on the floor starts at the pillar's base, with no lit gap, and the lit floor shows no shadow acne

#### Scenario: Recessed emitter's surround
- **WHEN** the benchmark fireplace's `light_area` (recessed 16 units behind its lintel, `_frustum 70`) is viewed in lighting mode with shadows on
- **THEN** the lintel's shadow on the ceiling reaches the north wall, with no lit strip between the shadow and the wall

#### Scenario: Soft frustum edge
- **WHEN** a wall in front of a `light_area` with `_frustum 70` crosses the frustum's edge
- **THEN** the light on the wall falls off smoothly between 60° and 70° off the axis, with no hard edge

#### Scenario: Beside the frustum
- **WHEN** a receiver lies in front of a `light_area` with `_frustum 45` but more than 45° from its axis
- **THEN** that light contributes nothing to it

#### Scenario: Emitter face does not shadow itself
- **WHEN** a `light_area` with `_frustum 70` lies on the face of the brush it was created from
- **THEN** the surfaces in front of it are lit, because the brush lies behind the near plane

#### Scenario: Frustum edited
- **WHEN** `_frustum` is changed from 0 to 45 in the entity inspector
- **THEN** a shadow map is generated for the light on the next redraw

## MODIFIED Requirements

### Requirement: Spot light shadow atlas
Each shadow-casting `light_spot` SHALL be rendered into a sub-rectangle of a single 2048 by 2048 depth atlas, 512 by 512 per light, addressed by a UV scale and offset. The light's view-projection SHALL be a perspective projection along the light's direction with a field of view covering twice the outer cone half-angle, a near plane of 4 map units, and a far plane at the light's `radius`. Shadow-casting `light_area` entities SHALL share this atlas, as specified in "Area light frustum shadows".

When more shadow-casting spot and area lights exist than the atlas holds, the renderer SHALL assign regions to the lights nearest the camera, spot and area lights alike, SHALL render the remainder unshadowed, and SHALL print a warning once naming the limit.

#### Scenario: Spot blocked by a wall
- **WHEN** a `light_spot` points at a wall with a pillar in between
- **THEN** the wall shows the pillar's shadow within the cone

#### Scenario: Atlas exhausted
- **WHEN** a map contains more shadow-casting spot and area lights than the atlas holds
- **THEN** the nearest lights are shadowed, the rest are lit unshadowed, and a warning is printed once
