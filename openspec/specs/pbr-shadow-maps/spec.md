# pbr-shadow-maps Specification

## Purpose
Shadow maps for the lighting draw mode of the PBR lighting preview: the depth-only caster pass, sun cascades and the spot atlas, the sampling and bias scheme ported from the reference renderer, and the caching, preference and fallback rules that govern them.

## Requirements

### Requirement: Shadow caster pass
When the camera is in lighting draw mode, the PBR lighting preview is active and shadows are enabled, the renderer SHALL render a depth-only pass for each shadow-casting light. The pass SHALL traverse the scene graph culled by that light's volume, SHALL write depth only with colour writes disabled, SHALL use `GL_LEQUAL` depth comparison, and SHALL cull back faces, except for materials declared double sided or `cull none`, which have no back face to cull and SHALL be drawn with culling disabled.

Casters are brush faces, patch surfaces and model surfaces, chosen by the editor's caster rule, which the SH baker follows:
- **Never cast:** `surfaceparm` `sky`, `fog`, `hint`, `areaportal`, `noshadows`, `water`, `slime` or `lava`, and `surfaceparm trans` without `surfaceparm alphashadow`.
- **Cast solid:** a `nodraw` face whose shader is not `nonsolid`, `playerclip`, `botclip` or `trigger` (caulk on a solid brush).
- **Cast through their alpha test:** alpha-tested shaders, including `trans` shaders with `alphashadow`, using the shader's preview alpha function.
- **Cast:** every other drawn surface, opaque or blended, whether or not its brush is solid. A blended surface without `surfaceparm trans` (such as `pbr.game`'s `.mtr` glass) casts solid, as before this change.

#### Scenario: Room interior occluded from an outside sun
- **WHEN** a sealed brush room stands under a casting sun in lighting draw mode
- **THEN** the interior floor receives no sun contribution while the roof outside does

#### Scenario: Caulked roof seals
- **WHEN** a room's roof brush has caulk on its sun-facing face and a textured ceiling face below
- **THEN** the interior floor receives no sun contribution

#### Scenario: Sky does not occlude
- **WHEN** the benchmark map's low sky ceiling lies between the sun and the street
- **THEN** the street is lit by the sun, because sky surfaces are excluded from the caster set

#### Scenario: Patch casts
- **WHEN** a patch arch stands between a casting spot light and a wall
- **THEN** the wall shows the arch's shadow

#### Scenario: Alpha mask casts through
- **WHEN** a double-sided, alpha-tested fence with `surfaceparm trans` and `surfaceparm alphashadow` stands between a spot light and the ground
- **THEN** the ground shows the fence's cut-out pattern rather than a solid rectangle

#### Scenario: Sky without skyparms does not occlude
- **WHEN** a ceiling uses a Quake 3 shader with `surfaceparm sky` and stages but no `skyparms`
- **THEN** it casts no shadow

#### Scenario: Unlit alpha-tested shader casts through its alpha test
- **WHEN** an alpha-tested Quake 3 shader that is not lit by the preview (for example one with `qer_trans`) stands between a light and the ground
- **THEN** the ground shows its cut-out pattern, not a solid shadow

#### Scenario: Glass does not cast
- **WHEN** a `surfaceparm trans` glass pane without `alphashadow` stands between a light and a wall
- **THEN** the wall receives the light unshadowed

#### Scenario: Less-than alpha test casts inverted
- **WHEN** an alpha-tested caster uses `alphaFunc LT128`
- **THEN** only its texels with alpha below 0.5 occlude

### Requirement: Sun cascaded shadow maps
A shadow-casting `light_sun` SHALL be rendered into three cascades. The cascades SHALL be packed into a single depth atlas texture, each occupying a 1024 by 1024 region addressed by a UV scale and offset, because GLSL 1.20 forbids dynamic indexing of a sampler array.

Split distances SHALL be computed over the camera's near distance and a shadow distance, which SHALL be the lesser of the camera's far distance and 4096 map units. The cap is required because the editor's far clip is the world diagonal (about 227000 map units), which would leave the last cascade at hundreds of map units per texel; surfaces beyond the shadow distance SHALL render lit. Split distances SHALL be computed over that range by blending logarithmic and uniform distributions with lambda 0.8, as `split = lambda * near * pow(far / near, p) + (1 - lambda) * (near + (far - near) * p)`. Each cascade's orthographic bounds SHALL be fitted to the axis-aligned bounds, in light space, of that camera frustum slice's eight corners. The bounds centre SHALL be snapped to the cascade's world-units-per-texel grid so shadow edges do not shimmer as the camera moves, and the near plane SHALL be pulled back by 20 map units so casters behind the slice are included.

#### Scenario: Cascade selection
- **WHEN** a fragment's view-space depth exceeds the first split distance and not the second
- **THEN** it samples the second cascade region of the atlas

#### Scenario: No shimmer on camera pan
- **WHEN** the camera pans slowly across a sunlit street
- **THEN** shadow edges stay stable rather than crawling along the texel grid

#### Scenario: Caster behind the slice
- **WHEN** a wall stands between the sun and a camera frustum slice but outside that slice
- **THEN** it still occludes, because the cascade's near plane is padded

### Requirement: Spot light shadow atlas
Each shadow-casting `light_spot` SHALL be rendered into a sub-rectangle of a single 2048 by 2048 depth atlas, 512 by 512 per light, addressed by a UV scale and offset. The light's view-projection SHALL be a perspective projection along the light's direction with a field of view covering twice the outer cone half-angle, a near plane of 4 map units, and a far plane at the light's `radius`.

When more shadow-casting spot lights exist than the atlas holds, the renderer SHALL assign regions to the lights nearest the camera, SHALL render the remainder unshadowed, and SHALL print a warning once naming the limit.

#### Scenario: Spot blocked by a wall
- **WHEN** a `light_spot` points at a wall with a pillar in between
- **THEN** the wall shows the pillar's shadow within the cone

#### Scenario: Atlas exhausted
- **WHEN** a map contains more shadow-casting spot lights than the atlas holds
- **THEN** the nearest lights are shadowed, the rest are lit unshadowed, and a warning is printed once

### Requirement: Shadow sampling and bias
The light pass SHALL multiply a casting light's contribution by a shadow term in the range 0 to 1, computed with the reference renderer's scheme.

The sampled position SHALL be offset along the surface normal by `normal * (1 - NdotL) * 0.005` before projection into light space. The depth comparison SHALL subtract a slope-scaled bias of `0.001 * sqrt(1 - NdotL * NdotL) / NdotL` clamped to the range 0 to 0.003 in normalised depth. A fragment whose projected coordinates fall outside the 0 to 1 range on any axis SHALL be treated as fully lit.

The term SHALL be the mean of nine hardware depth comparisons taken on a Poisson disc rotated per pixel by interleaved gradient noise, scaled by the atlas texel size and a per-light penumbra factor. The sun's penumbra factor SHALL be `2 / (cascade index + 1)`.

#### Scenario: Soft edge
- **WHEN** a shadow boundary is examined at close range
- **THEN** it shows a dithered soft edge several texels wide rather than a hard staircase

#### Scenario: No acne on a lit wall
- **WHEN** a thick brush wall faces the sun at a grazing angle
- **THEN** its lit face shows no self-shadow striping

#### Scenario: Outside the cascade range
- **WHEN** a surface lies beyond the last cascade's far distance
- **THEN** it renders fully lit rather than fully shadowed

### Requirement: Point lights do not cast shadows
A `light` entity SHALL NOT generate a shadow map and its contribution SHALL NOT be attenuated by any shadow term. Its `_shadows` key, if present, SHALL be ignored.

#### Scenario: Point light through a wall
- **WHEN** a `light` sits on one side of a wall and a surface on the other side lies within its radius
- **THEN** that surface still receives the point light's contribution

### Requirement: Shadow map caching and invalidation
Shadow maps SHALL persist between frames and SHALL be regenerated only when their inputs change. The spot atlas SHALL be regenerated when the scene graph changes or any light changes. The sun cascades SHALL additionally be regenerated when the camera's position, orientation or viewport changes, because they are fitted to the camera frustum.

#### Scenario: Camera orbit with a static scene
- **WHEN** the camera orbits a scene with one spot light and nothing is edited
- **THEN** the spot's shadow map is generated once and only sampled on later frames

#### Scenario: Brush moved
- **WHEN** a brush is dragged in the 2D view
- **THEN** the camera's shadows update to match on the next redraw

#### Scenario: Light key edited
- **WHEN** a `light_spot`'s `cone` is changed in the entity inspector
- **THEN** its shadow map is regenerated with the new frustum

### Requirement: Shadow preference
The editor SHALL provide a camera preference "Lighting shadows" (boolean, default on) that affects only lighting draw mode while the PBR lighting preview is active. When it is off, no caster pass SHALL run and every light SHALL render unshadowed. Changing it SHALL redraw the camera.

#### Scenario: Shadows disabled
- **WHEN** the preference is turned off
- **THEN** the lighting view matches the Stage 1 result and no shadow maps are allocated

### Requirement: Shadow capability fallback
If depth textures or hardware shadow comparison are unavailable on the current context, the renderer SHALL render lighting draw mode without shadows and SHALL print a warning once to the console.

#### Scenario: Unsupported context
- **WHEN** shadow atlas creation fails
- **THEN** lighting mode still draws unshadowed, a warning is printed once, and no further errors occur

### Requirement: Shadows are confined to PBR lighting mode
Shadow generation and sampling SHALL occur only in lighting draw mode while the PBR lighting preview is active. The Doom 3 and Quake 4 lighting path, the textured, solid and wireframe draw modes, and the 2D views SHALL be unaffected.

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active and lighting mode is enabled
- **THEN** rendering is identical to the behaviour before this change and no shadow maps are allocated

#### Scenario: Textured mode
- **WHEN** the camera is switched from lighting mode to textured mode
- **THEN** no caster pass runs

#### Scenario: Preview turned off
- **WHEN** the PBR lighting preview preference is turned off
- **THEN** both shadow atlases are released
