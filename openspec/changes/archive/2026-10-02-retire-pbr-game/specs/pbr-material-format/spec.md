## ADDED Requirements

### Requirement: Quake 3 shaders are the only PBR material language
The shaders plugin SHALL offer no `pbr` material language. PBR material data SHALL come only from Quake 3 shaders (`shaders="quake3"`) through the `qer_pbr_*` keywords and stages this capability specifies. The `doom3` and `quake4` languages SHALL keep reading their own `materials/*.mtr` files exactly as before this change.

#### Scenario: Quake 3 shaders load as before
- **WHEN** the editor starts under `Q3.game` with the `benchmark` mod
- **THEN** the number of loaded shader definitions and the `Error parsing shader` lines in the log are the same as before this change, and every `textures/bench/*` shader carries the same PBR data

#### Scenario: Doom 3 materials unaffected
- **WHEN** a Doom 3 or Quake 4 gamepack is active
- **THEN** its `.mtr` materials load with the same editor images and flags as before this change

## MODIFIED Requirements

### Requirement: Material files are the single source for the baker
The `scripts/*.shader` files the editor reads SHALL be the only material description. For Quake 3 shaders, the editor's rules for base colour, emissive, PBR keywords and defaults (this capability) are the reference that q3map2's SH stage and `renderer_sh` implement in their own parsers.

#### Scenario: Baker reads a material
- **WHEN** q3map2's SH stage reads a `scripts/*.shader` entry that the editor accepts
- **THEN** it resolves the same texture paths and factors as the editor, from the shader text, without any intermediate export

#### Scenario: Another consumer reads a Quake 3 shader
- **WHEN** q3map2's SH stage or `renderer_sh` reads a `scripts/*.shader` entry that the editor classifies as PBR
- **THEN** applying this capability's rules yields the same base colour, maps, factors and defaults as the editor

### Requirement: Colour space handling
Base colour and emissive textures SHALL be treated as sRGB and decoded to linear in the lighting shader. Normal, metallic-roughness, and occlusion textures SHALL be sampled as linear data. The texture gamma preference SHALL NOT be applied to any texture loaded for lighting-mode shading. The textured-mode editor image of a Quake 3 shader SHALL keep the gamma preference.

#### Scenario: Gamma preference set
- **WHEN** the texture gamma preference is set to a value other than 1.0 and a PBR material's textures are loaded
- **THEN** the uploaded texel data is identical to the source image

#### Scenario: Mid-grey base colour
- **WHEN** a base colour texture is uniform sRGB value 128 and the material is lit by a white light at normal incidence with unit irradiance
- **THEN** the shaded linear result before tonemapping is approximately 0.216 / pi, not 0.5 / pi

#### Scenario: Quake 3 textured mode unchanged
- **WHEN** the texture gamma preference is not 1.0 and a Quake 3 PBR shader is shown in textured mode
- **THEN** its editor image has the gamma preference applied, exactly as before this change

### Requirement: Quake 3 shader PBR defaults
For a Quake 3 shader without `qer_pbr_metallicRoughness`, metallic SHALL equal `qer_pbr_metallicFactor` (default 0) and roughness SHALL equal `qer_pbr_roughnessFactor` (default 1). With the map, both factors SHALL default to 1 and multiply its B and G channels. An absent occlusion map SHALL behave as 1, and an absent normal map as flat.

#### Scenario: Unadorned shader
- **WHEN** a lightmapped Quake 3 shader declares no metallic-roughness map and no factors
- **THEN** it renders as a fully rough dielectric

#### Scenario: Factor alone makes metal
- **WHEN** a Quake 3 shader declares only `qer_pbr_metallicFactor 1`
- **THEN** it renders as a fully rough metal

#### Scenario: Factor alone sets roughness
- **WHEN** a Quake 3 shader declares only `qer_pbr_roughnessFactor 0.4`
- **THEN** it renders as a dielectric with roughness 0.4

#### Scenario: Map with factors
- **WHEN** a shader declares `qer_pbr_metallicRoughness` and `qer_pbr_metallicFactor 0.5`
- **THEN** metallic is half of the map's B channel, and roughness is the map's G channel

### Requirement: The emissive strength keyword
`qer_pbr_emissiveStrength` SHALL multiply emissive radiance only. It SHALL have no effect on a shader without an emissive stage. A Quake 3 shader's emissive radiance SHALL be the decoded emissive texel × the emissive colour × `qer_pbr_emissiveStrength`, with no Quake 3 specific scale. It is added to the base pass in the same linear units as the light passes' output, before exposure and tonemapping.

#### Scenario: White glow at strength 1
- **WHEN** a shader's additive stage is a uniform white image with no `rgbGen const` and no `qer_pbr_emissiveStrength`, and it is viewed in lighting mode with ambient 0 and no light reaching it
- **THEN** its base pass writes a linear radiance of 1 in every channel before exposure and tonemapping

#### Scenario: Boosted glow
- **WHEN** a shader with an additive glow stage declares `qer_pbr_emissiveStrength 4`
- **THEN** its emissive radiance in lighting mode is four times that of the same shader without the keyword

## REMOVED Requirements

### Requirement: PBR material language is selectable per game
**Reason**: `pbr.game` was the only game with `shaders="pbr"`, and it is deleted. Its `.mtr` materials can't load on the stock target. The `quake3` language carries the same data in `qer_pbr_*` keywords.
**Migration**: Write materials as `scripts/*.shader` entries with `qer_pbr_*` keywords under `Q3.game`. `content/benchmark/README.md` maps each `.mtr` keyword to its Quake 3 form.

### Requirement: PBR material grammar
**Reason**: The `.mtr` parser is retired with the `pbr` language.
**Migration**: See the Quake 3 requirements of this capability: `qer_pbr_*` keywords, the base colour stage, the additive emissive stage, and alpha and cull from stages.

### Requirement: Editor classification flags from PBR materials
**Reason**: These flags came from `.mtr` materials only. Quake 3 shaders set the same editor flags through the stock `surfaceparm`, `qer_trans`, `alphaFunc` and `cull` keywords.
**Migration**: `alphamode mask` becomes a stage `alphaFunc`, `alphamode blend` becomes `blendFunc blend`, and `doublesided` becomes `cull none`.
