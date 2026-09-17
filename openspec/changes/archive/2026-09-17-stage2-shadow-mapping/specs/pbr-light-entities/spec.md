## ADDED Requirements

### Requirement: Shadow casting key
A `light_spot` or `light_sun` entity SHALL read `_shadows` (boolean, default 1). When it is 0 the light SHALL be reported to the renderer as non-casting and SHALL generate no shadow map. A `light` entity SHALL ignore the key, because point lights do not cast shadows.

#### Scenario: Casting disabled
- **WHEN** a `light_spot` sets `_shadows 0`
- **THEN** it lights surfaces through occluders and no shadow map region is allocated for it

#### Scenario: Default casts
- **WHEN** a `light_sun` is placed with no `_shadows` key
- **THEN** it casts shadows

#### Scenario: Key edited
- **WHEN** `_shadows` is changed from 1 to 0 in the entity inspector
- **THEN** the camera redraws without that light's shadows

## MODIFIED Requirements

### Requirement: Renderer light interface carries PBR parameters
`RendererLight` SHALL expose the light type (point, spot, sun), intensity, cutoff radius, direction, inner and outer cone cosines, and whether the light casts shadows. Existing accessors (`colour`, `aabb`, `rotation`, `offset`, `isProjected`, `projection`) SHALL remain valid for the Doom 3 light type.

The renderer SHALL be able to associate a shadow description with a light for the duration of a frame: the light-space view-projection matrices, the atlas region as a UV scale and offset, and, for the sun, the cascade split distances. Light entities SHALL NOT be responsible for computing or storing that description.

#### Scenario: Point light culling
- **WHEN** a surface's bounds lie outside a point light's cutoff radius
- **THEN** the renderer does not issue a lighting pass for that surface and light

#### Scenario: Sun not culled
- **WHEN** a sun light exists
- **THEN** every surface with a lit material receives a pass for it regardless of bounds

#### Scenario: Casting flag reaches the renderer
- **WHEN** a `light_spot` has `_shadows 0`
- **THEN** the renderer sees the light as non-casting and skips its caster pass

### Requirement: Entity definitions ship with the gamepack
The gamepack SHALL ship entity definitions for `light`, `light_spot`, and `light_sun` declaring each key above with its default and a one-line description, so the entity inspector lists them. The definitions for `light_spot` and `light_sun` SHALL include `_shadows`.

#### Scenario: Inspector shows keys
- **WHEN** a `light_spot` is selected and the entity inspector is open
- **THEN** `cone`, `cone_inner`, `intensity`, `radius`, `_color`, and `_shadows` appear with their defaults
