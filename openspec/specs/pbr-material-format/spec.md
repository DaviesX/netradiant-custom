# pbr-material-format Specification

## Purpose
The `pbr` material language: `.mtr` grammar, editor classification, the material interface exposing PBR maps and factors, and colour space handling.

## Requirements

### Requirement: PBR material language is selectable per game
The shaders plugin SHALL expose a material language named `pbr`, selected when a `.game` file sets `shaders="pbr"`. In that mode the editor SHALL load material scripts from the `materials/` directory with the `.mtr` extension, SHALL NOT generate default shaders for bare textures, and SHALL NOT use `shaderlist.txt`.

#### Scenario: Game selects the pbr language
- **WHEN** the active `.game` file contains `shaders="pbr"`
- **THEN** the editor parses every `materials/*.mtr` file in the game's search paths using the PBR grammar and lists each defined material in the texture browser

#### Scenario: Other game types are unaffected
- **WHEN** the active `.game` file sets `shaders` to `quake3`, `doom3`, or `quake4`
- **THEN** material parsing behaves exactly as before this change

### Requirement: PBR material grammar
A PBR material SHALL be a material name followed by a brace-delimited block. Inside the block, the parser SHALL recognise the keywords in the table below, each on its own line, case-insensitive. Unknown keywords SHALL be skipped to the end of their line without error. Nested brace blocks SHALL be skipped entirely.

| Keyword | Arguments | Default |
|---|---|---|
| `basecolor` | texture path | none |
| `normal` | texture path | flat normal |
| `metallicroughness` | texture path (G = roughness, B = metallic) | white |
| `occlusion` | texture path (R = occlusion) | white |
| `emissive` | texture path | black |
| `basecolorfactor` | r g b a | 1 1 1 1 |
| `metallicfactor` | float | 1 |
| `roughnessfactor` | float | 1 |
| `emissivefactor` | r g b | 0 0 0 |
| `emissivestrength` | float | 1 |
| `alphamode` | `opaque` / `mask` / `blend` | `opaque` |
| `alphacutoff` | float | 0.5 |
| `doublesided` | none | off |
| `qer_editorimage` | texture path | falls back to `basecolor` |
| `qer_trans` | float | none |
| `qer_nocarve` | none | off |
| `surfaceparm` | name | none |

#### Scenario: Minimal material
- **WHEN** a material block contains only `basecolor textures/wall/brick_c`
- **THEN** the material resolves its base colour texture to that image, all other maps to their defaults, and the texture browser shows the base colour image

#### Scenario: Unknown keyword
- **WHEN** a material block contains a line beginning with `q3map_lightmapsamplesize 8`
- **THEN** the parser skips the line and the material loads without error

#### Scenario: Nested block
- **WHEN** a material block contains a nested `{ ... }` block
- **THEN** the parser skips the nested block and continues parsing the enclosing material

### Requirement: Editor classification flags from PBR materials
The parser SHALL map `surfaceparm` values `nodraw`, `nonsolid`, `water`, `lava`, `slime`, `fog`, `areaportal`, `playerclip`, `botclip`, and `sky` to the same editor flags the Quake 3 parser sets. `alphamode mask` SHALL set the alpha-test flag with the cutoff as its reference. `alphamode blend` SHALL set the translucent flag. `doublesided` SHALL set cull to none.

#### Scenario: Clip material
- **WHEN** a material contains `surfaceparm playerclip`
- **THEN** brushes using it are classified as clip in the editor and filtered by the clip filter

#### Scenario: Masked material
- **WHEN** a material contains `alphamode mask` and `alphacutoff 0.3`
- **THEN** the editor renders it with alpha test greater-or-equal 0.3 in textured and lighting modes

### Requirement: Material interface exposes PBR maps and factors
`IShader` SHALL expose accessors for the base colour, normal, metallic-roughness, occlusion, and emissive textures, and for the base colour factor, metallic factor, roughness factor, emissive factor, emissive strength, and alpha mode. When lighting is enabled, the PBR textures SHALL be loaded; when lighting is disabled they SHALL be released, matching the existing behaviour of the bump and specular textures.

#### Scenario: Lighting mode toggled
- **WHEN** the camera switches from textured to lighting draw mode
- **THEN** every in-use PBR material loads its normal, metallic-roughness, occlusion, and emissive textures
- **WHEN** the camera switches back to textured draw mode
- **THEN** those textures are released

### Requirement: Material files are the single source for the baker
The `.mtr` files SHALL be the only material description. The grammar SHALL remain a flat, line-oriented key-value format so the SH baker can parse the same files with a small reader, and `surfaceparm` and `q3map_` keys SHALL remain inside them.

#### Scenario: Baker reads a material
- **WHEN** the baker parses a `.mtr` file that the editor accepts
- **THEN** it resolves the same texture paths and factors as the editor without any intermediate export

### Requirement: Colour space handling
Base colour and emissive textures SHALL be treated as sRGB and decoded to linear in the lighting shader. Normal, metallic-roughness, and occlusion textures SHALL be sampled as linear data. The texture gamma preference SHALL NOT be applied to any texture referenced by a PBR material.

#### Scenario: Gamma preference set
- **WHEN** the texture gamma preference is set to a value other than 1.0 and a PBR material's textures are loaded
- **THEN** the uploaded texel data is identical to the source image

#### Scenario: Mid-grey base colour
- **WHEN** a base colour texture is uniform sRGB value 128 and the material is lit by a white light at normal incidence with unit irradiance
- **THEN** the shaded linear result before tonemapping is approximately 0.216 / pi, not 0.5 / pi
