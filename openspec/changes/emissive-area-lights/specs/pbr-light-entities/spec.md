# Spec Delta

## ADDED Requirements

### Requirement: Area light keys
A `light_area` entity SHALL be a one-sided emitting rectangle. It SHALL read:
- `origin`: the rectangle's centre;
- `angles` (pitch yaw roll, positive pitch looking down, default 0 0 0) or `angle` (yaw only; −1 up, −2 down);
- `width` (float, default 64) and `height` (float, default 64), in map units;
- `intensity` (float, default 1): the radiance L, uniform over the rectangle, in the light model's linear unit;
- `_color` (linear RGB, default 1 1 1);
- `radius` (float, default 256): the cutoff distance from the centre.

Values that are not positive SHALL fall back to their defaults. The editor preview and `renderer_sh` SHALL use the effective radius max(`radius`, half-diagonal), where the half-diagonal is ½√(`width`² + `height`²), for culling, shading and the shadow far plane, so a `radius` smaller than the half-diagonal is clamped up to it without a warning. The basis SHALL be Quake's `AngleVectors` of `angles`:
- forward = (cos p cos y, cos p sin y, −sin p);
- right = (−sin r sin p cos y + cos r sin y, −sin r sin p sin y − cos r cos y, −sin r cos p);
- up = (cos r sin p cos y + sin r sin y, cos r sin p sin y − sin r cos y, cos r cos p).

The light SHALL emit along forward only. `width` SHALL lie along right and `height` along up. `target` SHALL be ignored. The editor SHALL draw the rectangle's outline and a normal arrow whether or not the light is selected. When the light is selected, the editor SHALL also draw its effective radius and, when `_frustum` is greater than 0, the shadow frustum out to the effective radius. The entity's bounds SHALL include the rectangle.

#### Scenario: Default area light
- **WHEN** a `light_area` is placed with no keys other than `origin`
- **THEN** it renders in lighting mode as a white 64 by 64 rectangle of radiance 1 facing +X, with cutoff 256 and no shadow

#### Scenario: Roll turns the rectangle
- **WHEN** a `light_area` with `width 128`, `height 16` and `angles 0 270 0` has its roll changed to 90
- **THEN** its normal stays −Y and the long side of the drawn rectangle turns from the X axis to the Z axis

#### Scenario: Size edited
- **WHEN** `width` is changed in the entity inspector
- **THEN** the drawn rectangle, the light's bounds and the lit area in lighting mode update immediately

#### Scenario: Target ignored
- **WHEN** a `light_area` has a `target` key naming an entity
- **THEN** its normal still follows `angles`

#### Scenario: Radius clamped to the half-diagonal
- **WHEN** a `light_area` with `width 128`, `height 128` and `radius 32` faces a wall 80 units away on its normal
- **THEN** the wall is lit, because the effective radius is the half-diagonal, 90.5

### Requirement: Area light shadow frustum
A `light_area` SHALL read `_frustum` (float, half-angle in degrees, default 0). When it is greater than 0, the light SHALL be reported to the renderer as casting, with `cosOuter` = cos(`_frustum`). When it is 0 or less, the light SHALL be non-casting. `_shadows` SHALL NOT affect a `light_area`.

#### Scenario: Frustum enables shadows
- **WHEN** `_frustum 60` is set on a `light_area`
- **THEN** the renderer sees the light as casting

#### Scenario: Shadows key ignored
- **WHEN** a `light_area` sets `_shadows 1` and no `_frustum`
- **THEN** it does not cast

### Requirement: Proxy light key
A `light`, `light_spot` or `light_sun` entity SHALL accept `_proxy` (boolean, default 0). The key marks a light that stands in for an emissive surface. A light has `_proxy 1` when its `_proxy` value starts with `1`, q3map2's reading of a boolean key, which the placeholder `-light` and `-shbake` (q3map2-shbake, "Attached emitter lights skipped") use on all three classes. The editor SHALL read the key the same way, only on `light` and `light_spot` and only for "Re-derive light from emitter", and SHALL ignore it on `light_sun`. `_proxy` on any other class SHALL be ignored. The editor preview, and later `renderer_sh`, SHALL render a light with `_proxy 1` exactly as the same light without the key, whatever its class. Only q3map2's light stages read the key, to leave the light out. The entity definitions SHALL offer the key on `light` and `light_spot` only, the kinds "Create proxy light from selection" creates; a sun is never an emitter's stand-in, and the key on it is honoured only so that every compile stage agrees.

#### Scenario: Proxy renders as an ordinary light
- **WHEN** `_proxy 1` is added to a `light`
- **THEN** the lighting view is unchanged

#### Scenario: Proxy key reading
- **WHEN** lights carry `_proxy 1`, `_proxy 1.0`, `_proxy 0` and `_proxy true`
- **THEN** the first two have `_proxy 1` and the last two do not

### Requirement: Placeholder light stage skips attached lights
q3map2's `-light` without `-shbake` SHALL NOT create a light from a `light_area` entity, or from a `light`, `light_spot` or `light_sun` entity with `_proxy 1` (its value starts with `1`): the same entities `-shbake` leaves out. A `_proxy` key on any other `light*` class SHALL change nothing. Its output for a map without such entities SHALL be byte-identical to its output before this change. `-keeplights` SHALL keep these entities in the BSP.

#### Scenario: Area light adds no placeholder light
- **WHEN** a scratch map is compiled single-threaded with `-light -patchshadows`, with and without a `light_area`
- **THEN** the lightmap, light-grid and draw-vertex lumps of the two BSPs are byte-identical

#### Scenario: Proxy adds no placeholder light
- **WHEN** a scratch map is compiled single-threaded with `-light -patchshadows`, with and without a `light` carrying `_proxy 1`, again with and without a `light_spot` carrying `_proxy 1`, and again with and without a `light_sun` carrying `_proxy 1`
- **THEN** in each pair the lightmap, light-grid and draw-vertex lumps of the two BSPs are byte-identical

#### Scenario: Entities kept
- **WHEN** the benchmark is compiled with `-meta -keeplights`
- **THEN** the BSP's entity lump holds its `light_area`

### Requirement: Spot gizmo bounded by its radius
When a `light_spot` is selected, the editor SHALL draw its cone to the sphere of its `radius`: the rim centred at `radius` × cos θ along the axis, with rim radius `radius` × sin θ, where θ = min(`cone`, 89.9°). No part of the spot's gizmo SHALL lie farther than `radius` from its origin.

#### Scenario: Spot gizmo stays within its radius
- **WHEN** a hemisphere spot proxy (`cone 90`, `radius 300`) is selected
- **THEN** its drawn cone is a disc of radius about 300 through its origin, not a fan hundreds of times wider

## MODIFIED Requirements

### Requirement: PBR game type in the entity plugin
The entity plugin SHALL expose a game type named `pbr`, selected when a `.game` file sets `entities="pbr"` (the `entityclass` key selects the definition loader, not the entity module). In that mode light entities SHALL use the PBR light type described below. Other game types SHALL be unaffected.

#### Scenario: Game selects the pbr entity type
- **WHEN** the active `.game` file sets `entities="pbr"`
- **THEN** entities named `light`, `light_spot`, `light_sun` and `light_area` are created as PBR lights

#### Scenario: Other game types have no area light
- **WHEN** a game using the `quake3` entity module contains a `light_area`
- **THEN** it is a generic point entity with no light

### Requirement: Renderer light interface carries PBR parameters
`RendererLight` SHALL expose:
- the light type (point, spot, sun, area);
- intensity, cutoff radius and direction;
- the inner and outer cone cosines;
- whether the light casts shadows;
- for an area light, the rectangle's world-space half-extent axes (right × `width`/2 and up × `height`/2).

For an area light, the direction SHALL be the emitting normal, the origin the centre, and the outer cosine cos(`_frustum`). Existing accessors (`colour`, `aabb`, `rotation`, `offset`, `isProjected`, `projection`) SHALL remain valid for the Doom 3 light type.

An area light SHALL affect a surface only if the surface's bounds lie within the effective radius of the centre and not wholly behind the rectangle's plane. The reported cutoff radius of an area light SHALL be its effective radius. A brush face SHALL receive an area light when any corner of the rectangle lies strictly in front of the face's plane.

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

#### Scenario: Area light culled behind its plane
- **WHEN** a surface lies within an area light's radius but wholly behind its emitting side
- **THEN** the renderer issues no lighting pass for that surface and light

#### Scenario: Emitter face not lit by its own light
- **WHEN** a `light_area` lies on a brush face's plane, facing out of the brush
- **THEN** that face, and the brush's opposite face, receive no pass for it

#### Scenario: Face cutting the rectangle
- **WHEN** a floor face's plane passes below an area light's centre but above its lowest corners, so part of the rectangle lies in front of the face
- **THEN** the floor face receives a pass for the light

### Requirement: Entity definitions ship with the gamepack
The `pbr` gamepack source SHALL ship entity definitions for `light`, `light_spot`, `light_sun` and `light_area` as `Q3.game/baseq3/_pbr_lights.ent`, declaring each key above with its default and a one-line description, so the entity inspector lists them. The definitions for `light_spot` and `light_sun` SHALL include `_shadows`. The definitions for `light` and `light_spot` SHALL include `_proxy`. The definition for `light_area` SHALL include `width`, `height`, `angles`, `intensity` (described as radiance), `_color`, `radius` and `_frustum`, and its notes SHALL state that it is one-sided and that its flux is π × `intensity` × `width` × `height`. The file SHALL be named so it loads before the stock `entities.ent` and its `light` replaces the stock definition. It SHALL be the only copy of these definitions in the repository. The stock definitions of all other Quake 3 entities SHALL remain available.

#### Scenario: Inspector shows keys
- **WHEN** a `light_spot` is selected under `Q3.game` with the overlay installed and the entity inspector is open
- **THEN** `cone`, `cone_inner`, `intensity`, `radius`, `_color`, `_shadows` and `_proxy` appear with their defaults

#### Scenario: Quake 3 light definition
- **WHEN** a `light` is selected under `Q3.game` with the overlay installed
- **THEN** the entity inspector shows `intensity`, `radius` and `_proxy` with the PBR defaults, and `info_player_deathmatch` and the weapon entities are still listed in the entity menu

#### Scenario: Area light definition
- **WHEN** a `light_area` is selected under `Q3.game` with the overlay installed
- **THEN** the entity inspector shows `width`, `height`, `angles`, `intensity`, `_color`, `radius` and `_frustum` with their defaults, and no `_shadows`

### Requirement: Shadow casting key
A `light_spot` or `light_sun` entity SHALL read `_shadows` (boolean, default 1). When it is 0 the light SHALL be reported to the renderer as non-casting and SHALL generate no shadow map. A `light` entity SHALL ignore the key, because point lights do not cast shadows. A `light_area` entity SHALL ignore the key, because `_frustum` alone decides whether it casts.

#### Scenario: Casting disabled
- **WHEN** a `light_spot` sets `_shadows 0`
- **THEN** it lights surfaces through occluders and no shadow map region is allocated for it

#### Scenario: Default casts
- **WHEN** a `light_sun` is placed with no `_shadows` key
- **THEN** it casts shadows

#### Scenario: Key edited
- **WHEN** `_shadows` is changed from 1 to 0 in the entity inspector
- **THEN** the camera redraws without that light's shadows

### Requirement: Quake 3 game uses the PBR entity module
The repository SHALL provide, in the `pbr` gamepack source (`setup/data/gamepacks/pbr/`), a `games/Q3.game` identical to the downloaded Quake III Arena game file except that it sets `entities="pbr"`. Installing that pack over the downloaded gamepacks SHALL make `light`, `light_spot`, `light_sun` and `light_area` PBR lights under `Q3.game`, with the keys, light model and shadow behaviour this capability specifies for the `pbr` entity module. Every other setting of `Q3.game`, and every other game, SHALL be unaffected.

#### Scenario: Spot light under Quake 3
- **WHEN** the overlay is installed, the editor runs under `Q3.game` with the PBR lighting preview on, and a `light_spot` targets an `info_null` above a lightmapped floor
- **THEN** lighting mode shows the spot's cone of light on the floor, with shadows

#### Scenario: Area light under Quake 3
- **WHEN** the overlay is installed, the editor runs under `Q3.game` with the PBR lighting preview on, and a `light_area` faces a lightmapped wall
- **THEN** lighting mode shows a soft patch of light on the wall in front of the rectangle and none behind it

#### Scenario: Other Quake 3 based games unchanged
- **WHEN** a Quake 3 based game other than `Q3.game` (for example OpenArena) is active
- **THEN** its `light` entities behave exactly as before this change
