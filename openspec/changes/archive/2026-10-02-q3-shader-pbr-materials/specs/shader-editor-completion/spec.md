## Purpose

The Shader Editor's keyword completion: which shader keyword families it offers, where each may appear, and the argument hints shown while typing.

## ADDED Requirements

### Requirement: PBR keywords are completed
The Shader Editor SHALL offer completion with argument hints, at shader level only, for:
- `qer_pbr_normal %t`, `qer_pbr_metallicRoughness %t` and `qer_pbr_occlusion %t`;
- `qer_pbr_roughnessFactor %f`, `qer_pbr_metallicFactor %f` and `qer_pbr_emissiveStrength %f`;
- `qer_pbr_baseColorFactor %f %f %f`.

They SHALL be highlighted as editor directives, the same as `qer_editorImage`.

#### Scenario: Prefix typed
- **WHEN** the user types `qer_pbr_` at shader level
- **THEN** the completion list offers all seven keywords

#### Scenario: Texture argument
- **WHEN** the user completes `qer_pbr_normal `
- **THEN** texture-path completion from the VFS is offered for the argument

#### Scenario: Not inside a stage
- **WHEN** the user types `qer_pbr_` inside a stage block
- **THEN** none of the seven keywords is offered

### Requirement: No rend2 stage keywords are offered
The Shader Editor SHALL NOT offer rend2-only stage keywords (`stage`, `normalMap`, `specularMap`, `specularReflectance`, `specularExponent`, `gloss`, `roughness`, `parallaxDepth`, `normalScale`, `specularScale`), because the compatibility contract forbids them.

#### Scenario: Stage keyword
- **WHEN** the user types `stag` inside a stage block
- **THEN** `stage` is not offered

### Requirement: Existing completion is unchanged
Adding the PBR entries SHALL NOT remove or reorder existing Quake 3, q3map2 or editor-directive completions.

#### Scenario: Existing keyword
- **WHEN** the user types `qer_editorI`
- **THEN** `qer_editorImage` is still completed
