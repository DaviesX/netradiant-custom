## MODIFIED Requirements

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

### Requirement: Shadow preference
The editor SHALL provide a camera preference "Lighting shadows" (boolean, default on) that affects only lighting draw mode while the PBR lighting preview is active. When it is off, no caster pass SHALL run and every light SHALL render unshadowed. Changing it SHALL redraw the camera.

#### Scenario: Shadows disabled
- **WHEN** the preference is turned off
- **THEN** the lighting view matches the Stage 1 result and no shadow maps are allocated

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
