# Spec Delta

## MODIFIED Requirements

### Requirement: One additive pass per light plus a base pass
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, plus the sky term specified in "Sky light in the base pass", and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff. Area passes SHALL integrate the light's radiance over its rectangle as specified in "Area lights are shaded with linearly transformed cosines", zero beyond the cutoff radius from the centre and behind the emitting side. A light with `_proxy 1` SHALL get the pass of its own type (point, spot or sun), exactly as without the key.

When the light casts shadows, the pass SHALL multiply its contribution by the light's shadow term before accumulation. The base pass SHALL NOT be attenuated by any shadow term, so emissive, ambient and sky light remain visible in shadow.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point, spot and area cutoff and there is no sun
- **THEN** it renders with only its base pass: emissive, ambient and sky light

#### Scenario: Two overlapping lights
- **WHEN** two point lights both reach a surface
- **THEN** the result equals the base pass plus the sum of the two light contributions

#### Scenario: Surface fully in shadow
- **WHEN** a surface lies inside the cone and radius of a casting spot light but behind an occluder
- **THEN** it renders with only its base pass: emissive, ambient and sky light

#### Scenario: Emissive visible in shadow
- **WHEN** an emissive surface is occluded from every light
- **THEN** it still emits, because the base pass is not shadowed

#### Scenario: Area light without a frustum ignores occluders
- **WHEN** a pillar stands between a `light_area` with `_frustum 0` and a wall
- **THEN** the wall behind the pillar is still lit by the area light

### Requirement: PBR lighting preview preference
The editor SHALL provide a per-game camera preference, "PBR lighting preview" (boolean):
- for games whose `.game` declares `type="q3"`, it is shown, editable and defaults to on;
- for every other game type, it is hidden.

The preview is active while the preference is on. No `.game` key forces it on. A change takes effect without a restart. The "Lighting exposure", "Lighting ambient", "Lighting sky" and "Lighting shadows" preferences SHALL be shown exactly when this preference is shown.

#### Scenario: Quake 3 default
- **WHEN** the editor starts under `Q3.game` with no saved value for the preference
- **THEN** lighting draw mode is available and renders with the PBR program

#### Scenario: Turned off
- **WHEN** the preference is turned off under `Q3.game`
- **THEN** lighting draw mode is no longer offered; if the camera was in it, it returns to textured mode without a restart

#### Scenario: Per game
- **WHEN** the preference is turned off under one Quake 3 based game and the editor is restarted under another
- **THEN** the other game still has the preference on

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active
- **THEN** the preference does not appear and lighting mode uses the existing Doom 3 program

## ADDED Requirements

### Requirement: Sky light in the base pass
While "Lighting sky" is on and the map has sky light (sky-environment-light), the base pass of every lit surface SHALL add (1 − metallic) × base colour × occlusion × E(n) / π. Here E is the sky irradiance and n is the surface's interpolated vertex normal, rotated to world space, without the normal map. The term SHALL NOT be shadowed. Emitters and unlit surfaces SHALL NOT receive it. The sky shader's own drawing SHALL NOT change.

#### Scenario: Open floor under a cloud sky
- **WHEN** a white (base colour 1), non-metallic, unoccluded floor faces +Z under a cloud-layer sky of radiance L, with ambient 0 and no light reaching it
- **THEN** its base pass writes a linear radiance of L within 1%, read before exposure and tonemapping

#### Scenario: Ceiling sees no sky light
- **WHEN** the same material faces −Z under that sky
- **THEN** its base pass adds no sky light

#### Scenario: Rotated model
- **WHEN** a lit `misc_model` with a +Z top face is rotated 90° about X, so that the top faces −Y
- **THEN** that face receives the sky light of a −Y normal, not of +Z

#### Scenario: Interiors are overlit
- **WHEN** a sealed room's floor faces +Z in a map whose sky emits
- **THEN** the floor receives the full open-sky light, because the preview has no sky visibility

#### Scenario: Legacy map unchanged
- **WHEN** a map whose sky shader has no `qer_pbr_emissiveStrength` is viewed in lighting mode
- **THEN** the image is identical to the one before this change

### Requirement: Sky light preference
The editor SHALL provide a per-game camera preference, "Lighting sky" (boolean, default on), that affects only lighting draw mode. Turning it off SHALL remove the sky term from the base pass and nothing else. A change SHALL redraw the camera. Editing the map or reloading shaders SHALL update the sky light at the next redraw, with no other action.

#### Scenario: Turned off
- **WHEN** "Lighting sky" is turned off
- **THEN** the lighting view equals a view of the same map whose sky shader has no `qer_pbr_emissiveStrength`

#### Scenario: Shader edited
- **WHEN** the sky shader's `qer_pbr_emissiveStrength` is doubled and shaders are reloaded
- **THEN** the sky term doubles at the next redraw
