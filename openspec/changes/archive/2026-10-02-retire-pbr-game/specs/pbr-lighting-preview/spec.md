## MODIFIED Requirements

### Requirement: Lighting draw mode renders PBR materials with a physically based BRDF
When the camera is in lighting draw mode and the PBR lighting preview is active, each lit surface SHALL be shaded with the metallic-roughness BRDF: GGX normal distribution, Smith-Schlick geometry, Schlick Fresnel with F0 = mix(0.04, basecolor, metallic), and Disney diffuse. Lit surfaces are those whose Quake 3 shader is lightmapped and opaque, as specified below. Normal mapping SHALL use the per-vertex tangent frame already supplied by brushes and patches.

#### Scenario: Metallic sphere
- **WHEN** a patch sphere uses a lightmapped shader with `qer_pbr_metallicFactor 1` and `qer_pbr_roughnessFactor 0.2` under a single point light
- **THEN** it shows a tight specular highlight tinted by its base colour and no diffuse term

#### Scenario: Rough dielectric
- **WHEN** a surface uses a lightmapped shader with `qer_pbr_metallicFactor 0` and `qer_pbr_roughnessFactor 1`
- **THEN** it shows a diffuse falloff with a broad, dim highlight

#### Scenario: Quake 3 shader with normal map
- **WHEN** a Quake 3 shader with `qer_pbr_normal` is viewed in lighting mode under `Q3.game`
- **THEN** it is shaded by the BRDF with its normal map applied

### Requirement: PBR lighting preview preference
The editor SHALL provide a per-game camera preference, "PBR lighting preview" (boolean):
- for games whose `.game` declares `type="q3"`, it is shown, editable and defaults to on;
- for every other game type, it is hidden.

The preview is active while the preference is on. No `.game` key forces it on. A change takes effect without a restart. The "Lighting exposure", "Lighting ambient" and "Lighting shadows" preferences SHALL be shown exactly when this preference is shown.

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

### Requirement: Lightmapped opaque shaders are lit in the preview
While the preview is active, a Quake 3 shader SHALL be shaded by the metallic-roughness BRDF exactly when it meets all of these:
- it is lightmapped: a `$lightmap` stage, or a bare texture with no script;
- it is not blended;
- it does not declare `surfaceparm sky`, `fog` or `nolightmap`.

This applies whether or not the shader is classified as PBR. A non-PBR shader SHALL use its base colour and the Quake 3 defaults. Alpha-tested shaders SHALL be lit, with their alpha test applied. No lit surface is blended.

#### Scenario: Bare baseq3 texture
- **WHEN** a brush face uses a baseq3 texture with no `qer_pbr_` keywords, near a light
- **THEN** it is lit and shaded like a fully rough dielectric, not drawn fullbright

#### Scenario: PBR keyword on a sky shader
- **WHEN** a sky shader declares a `qer_pbr_` keyword
- **THEN** it is still drawn as a sky and receives no light passes

#### Scenario: Shadowed stock surface
- **WHEN** a lit face lies behind an occluder from a casting light
- **THEN** it receives only the base pass from that light
