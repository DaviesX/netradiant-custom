# pbr-light-entities Specification

## Purpose
Physically based light entities (`light`, `light_spot`, `light_sun`) for `pbr` games and the renderer light interface that carries their parameters.

## Requirements

### Requirement: PBR game type in the entity plugin
The entity plugin SHALL expose a game type named `pbr`, selected when a `.game` file sets `entities="pbr"` (the `entityclass` key selects the definition loader, not the entity module). In that mode light entities SHALL use the PBR light type described below. Other game types SHALL be unaffected.

#### Scenario: Game selects the pbr entity type
- **WHEN** the active `.game` file sets `entities="pbr"`
- **THEN** entities named `light`, `light_spot`, and `light_sun` are created as PBR lights

### Requirement: Point light keys
A `light` entity SHALL read `origin`, `_color` (linear RGB, default 1 1 1), `intensity` (float, default 100, radiant flux in watts), and `radius` (float, default 256, cutoff distance in map units). The renderer SHALL receive the light as a point light with those values. The editor SHALL draw the existing radius sphere at `radius` when light radii display is enabled.

#### Scenario: Default point light
- **WHEN** a `light` entity is placed with no keys other than `origin`
- **THEN** it renders in lighting mode as a white point light of 100 W radiant flux and cutoff 256

#### Scenario: Radius edited
- **WHEN** the `radius` key is changed in the entity inspector
- **THEN** the radius sphere and the light's culling bounds update immediately

### Requirement: Spot light keys
A `light_spot` entity SHALL read the point light keys plus `angles` or `target` for direction, `cone` (outer half-angle in degrees, default 45), and `cone_inner` (inner half-angle in degrees, default 30). When `target` is set it SHALL take precedence over `angles`. The renderer SHALL receive direction and the cosines of both cone angles.

#### Scenario: Targeted spot
- **WHEN** a `light_spot` has `target` naming an entity
- **THEN** its direction points from its origin to that entity's origin and updates when either moves

#### Scenario: Cone drawn
- **WHEN** a `light_spot` is selected
- **THEN** the editor draws a cone at the outer angle extending to `radius`

### Requirement: Sun light keys
A `light_sun` entity SHALL read `angles` (direction the light travels), `_color` (default 1 1 1), and `intensity` (float, default 3, irradiance in watts per square metre on a surface facing the sun). At most one `light_sun` SHALL be honoured; if several exist the renderer SHALL use the first in map order and the editor SHALL warn in the console. The sun SHALL have no cutoff and SHALL be applied to every lit surface.

#### Scenario: Sun present
- **WHEN** a map contains one `light_sun` with `angles 60 30 0`
- **THEN** every surface in lighting mode receives a directional light from that direction

#### Scenario: Two suns
- **WHEN** a map contains two `light_sun` entities
- **THEN** only the first is used and a warning naming the second is printed to the console

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
