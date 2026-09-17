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
`RendererLight` SHALL expose the light type (point, spot, sun), intensity, cutoff radius, direction, and inner and outer cone cosines. Existing accessors (`colour`, `aabb`, `rotation`, `offset`, `isProjected`, `projection`) SHALL remain valid for the Doom 3 light type.

#### Scenario: Point light culling
- **WHEN** a surface's bounds lie outside a point light's cutoff radius
- **THEN** the renderer does not issue a lighting pass for that surface and light

#### Scenario: Sun not culled
- **WHEN** a sun light exists
- **THEN** every surface with a lit material receives a pass for it regardless of bounds

### Requirement: Entity definitions ship with the gamepack
The gamepack SHALL ship entity definitions for `light`, `light_spot`, and `light_sun` declaring each key above with its default and a one-line description, so the entity inspector lists them.

#### Scenario: Inspector shows keys
- **WHEN** a `light_spot` is selected and the entity inspector is open
- **THEN** `cone`, `cone_inner`, `intensity`, `radius`, and `_color` appear with their defaults
