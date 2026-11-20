# Spec Delta

## ADDED Requirements

### Requirement: Area light frustum shadows
A `light_area` with `_frustum` greater than 0 SHALL be rendered into a tile of the spot shadow atlas, with:
- a perspective projection from the rectangle's centre along its normal;
- a field of view of twice `_frustum`, clamped below 2 × (90° − 1.15°);
- a near plane of 4 map units and a far plane at its effective radius (max(`radius`, half-diagonal)).

Its pass SHALL multiply its contribution by the shadow term sampled as for spot lights: penumbra factor 1, with NdotL taken toward the rectangle's centre. A receiver outside the frustum SHALL be treated as lit. A `light_area` with `_frustum` 0 SHALL generate no shadow map, and its contribution SHALL NOT be attenuated by any shadow term.

#### Scenario: Pillar shadow from the sign
- **WHEN** a `light_area` with `_frustum 70` faces a wall with a pillar in between, inside the frustum
- **THEN** the wall shows the pillar's shadow

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
