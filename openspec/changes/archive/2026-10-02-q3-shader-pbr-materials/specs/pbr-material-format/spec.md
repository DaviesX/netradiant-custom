## ADDED Requirements

### Requirement: Quake 3 shaders carry PBR keywords
When the material language is `quake3`, the parser SHALL read these top-level keywords, case-insensitive, and SHALL ignore them inside stages. Each is optional.

| Keyword | Arguments | Default |
|---|---|---|
| `qer_pbr_normal` | texture path (tangent space, linear) | flat |
| `qer_pbr_metallicRoughness` | texture path (G = roughness, B = metallic) | see Quake 3 defaults |
| `qer_pbr_occlusion` | texture path (R = occlusion) | 1 |
| `qer_pbr_roughnessFactor` | float | see Quake 3 defaults |
| `qer_pbr_metallicFactor` | float | see Quake 3 defaults |
| `qer_pbr_baseColorFactor` | r g b | 1 1 1 |
| `qer_pbr_emissiveStrength` | float | 1 |

#### Scenario: Keywords read
- **WHEN** a shader declares `qer_pbr_normal textures/town/brick_nrm` and `qer_pbr_roughnessFactor 0.8`
- **THEN** the material exposes that normal map and a roughness factor of 0.8

#### Scenario: Base colour factor has three components
- **WHEN** a shader declares `qer_pbr_baseColorFactor 0.5 0.5 0.5`
- **THEN** the base colour factor is (0.5, 0.5, 0.5, 1)

#### Scenario: Malformed argument
- **WHEN** a shader declares `qer_pbr_roughnessFactor` with no number on the line
- **THEN** the factor keeps its default, the shader still loads, and a warning naming the shader is printed

### Requirement: Quake 3 stages are parsed for PBR data
When the material language is `quake3`, the parser SHALL read every stage's:
- map (`map`, `clampMap`, or the first frame of `animMap`);
- blend function;
- `rgbGen const`;
- `tcGen environment`;
- `alphaFunc`.

Other stage keywords SHALL be skipped without error, so the set of shaders that load, and their textured-mode appearance, is unchanged.

#### Scenario: Stage keywords the editor does not use
- **WHEN** a stage contains `tcMod scroll 0.1 0` and `rgbGen wave sin 0 1 0 1`
- **THEN** the shader loads with the same editor image and flags as before this change

### Requirement: rend2 stage keywords are reported
When a Quake 3 shader's stage contains a rend2-only keyword, the parser SHALL print one warning per shader naming the shader and the keyword. The stage containing it SHALL take no part in the derivations below (base colour, lightmap pairing, emissive, alpha), because its image is rend2 data such as a normal map, not a colour. The rend2-only keywords are `stage`, `normalMap`, `bumpMap`, `specularMap`, `specularReflectance`, `specularExponent`, `gloss`, `roughness`, `normalScale`, `specularScale` and `parallaxDepth`. The warning says that `renderergl1` rejects the shader and that PBR data belongs in `qer_pbr_*`.

#### Scenario: rend2 normal-map stage
- **WHEN** a stage contains `stage normalMap` and `map textures/town/brick_nrm`
- **THEN** one warning naming the shader and `stage` is printed, and the material has no normal map

### Requirement: Base colour comes from the lightmap-multiplied stage
The base colour SHALL be the map of the stage paired with the `$lightmap` stage by a multiplicative blend (`filter`, `GL_DST_COLOR GL_ZERO`, or `GL_ZERO GL_SRC_COLOR`), whichever of the pair comes first. A shader with a `$lightmap` stage but no such pair SHALL use its first non-lightmap stage map. A bare texture with no script SHALL use that texture. A scripted shader with no stages has no base colour.

#### Scenario: Lightmap first
- **WHEN** a shader has a `map $lightmap` stage followed by `map textures/town/brick_c` with `blendFunc GL_DST_COLOR GL_ZERO`
- **THEN** the base colour is `textures/town/brick_c`

#### Scenario: Texture first
- **WHEN** a shader has `map textures/town/brick_c` followed by `map $lightmap` with `blendFunc filter`
- **THEN** the base colour is `textures/town/brick_c`

#### Scenario: Bare texture
- **WHEN** a brush face uses a texture with no shader script
- **THEN** the base colour is that texture

### Requirement: Emissive comes from the first additive stage
The emissive map SHALL be the map of the first stage that meets all of these:
- its blend is `add`, `GL_ONE GL_ONE` or `GL_SRC_ALPHA GL_ONE`;
- its map is neither `$lightmap` nor `$whiteimage`;
- it doesn't use `tcGen environment`.

The emissive colour SHALL be the stage's `rgbGen const`, or white otherwise. A shader without such a stage SHALL have no emission.

#### Scenario: Glow stage
- **WHEN** a shader's last stage is `map textures/town/neon_e` with `blendFunc add` and `rgbGen const ( 1 0.4 0.3 )`
- **THEN** the emissive map is `textures/town/neon_e` with colour (1, 0.4, 0.3)

#### Scenario: Environment reflection is not emission
- **WHEN** an additive stage uses `tcGen environment`
- **THEN** it is not the emissive stage

### Requirement: Quake 3 PBR classification
A Quake 3 shader SHALL be classified as PBR when, and only when, it declares at least one `qer_pbr_` keyword. Classification SHALL NOT depend on image file names or on rend2 keywords.

#### Scenario: Normal map keyword only
- **WHEN** a shader declares only `qer_pbr_normal`
- **THEN** it is classified as PBR

#### Scenario: Sibling file is not a declaration
- **WHEN** a shader declares nothing PBR, but a file `<base>_n.tga` exists next to its base colour image
- **THEN** it is not classified as PBR

### Requirement: Quake 3 shader PBR defaults
For a Quake 3 shader without `qer_pbr_metallicRoughness`, metallic SHALL equal `qer_pbr_metallicFactor` (default 0) and roughness SHALL equal `qer_pbr_roughnessFactor` (default 1). With the map, both factors SHALL default to 1 and multiply its B and G channels. An absent occlusion map SHALL behave as 1, and an absent normal map as flat. `.mtr` materials SHALL keep their existing defaults.

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

### Requirement: Alpha and cull from Quake 3 shaders
A Quake 3 shader's preview alpha test SHALL follow its base colour stage's `alphaFunc`:
- `GT0`: alpha greater than 0;
- `LT128`: alpha less than 0.5;
- `GE128`: alpha greater than or equal to 0.5.

The preview alpha test SHALL apply in lighting mode and in the shadow caster pass. `qer_alphafunc` SHALL keep driving textured mode and the editor's alpha-test flag; the stage `alphaFunc` SHALL NOT set that flag. The shader SHALL be blended when its base colour stage has a blending blend function (`blend`, `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA`) or the shader declares `qer_trans`. `cull none`, `cull twosided` or `cull disable` SHALL make it double sided.

#### Scenario: Fence
- **WHEN** a shader's base colour stage has `alphaFunc GE128` and the shader has `cull none`
- **THEN** it renders with texels below alpha 0.5 discarded, from both sides

#### Scenario: Less-than test
- **WHEN** a base colour stage has `alphaFunc LT128`
- **THEN** texels with alpha 0.5 or more are discarded

#### Scenario: Textured mode unchanged
- **WHEN** a shader has a stage `alphaFunc GE128` and no `qer_alphafunc`
- **THEN** textured mode and the editor's filters treat it exactly as before this change

### Requirement: The emissive strength keyword
`qer_pbr_emissiveStrength` SHALL multiply emissive radiance only. It SHALL have no effect on a shader without an emissive stage. A Quake 3 shader's emissive radiance SHALL be the decoded emissive texel × the emissive colour × `qer_pbr_emissiveStrength`, in the same units as a `.mtr` material's `emissive` × `emissivefactor` × `emissivestrength`, with no Quake 3 specific scale.

#### Scenario: White glow at strength 1
- **WHEN** a shader's additive stage is a uniform white image with no `rgbGen const` and no `qer_pbr_emissiveStrength`
- **THEN** its emissive radiance equals that of a `.mtr` material with a white emissive map, `emissivefactor 1 1 1` and `emissivestrength 1`

#### Scenario: Boosted glow
- **WHEN** a shader with an additive glow stage declares `qer_pbr_emissiveStrength 4`
- **THEN** its emissive radiance in lighting mode is four times that of the same shader without the keyword

## MODIFIED Requirements

### Requirement: PBR material language is selectable per game
The shaders plugin SHALL expose a material language named `pbr`, selected when a `.game` file sets `shaders="pbr"`. In that mode the editor SHALL load material scripts from the `materials/` directory with the `.mtr` extension, SHALL NOT generate default shaders for bare textures, and SHALL NOT use `shaderlist.txt`. The `quake3` language SHALL carry PBR data as specified by the Quake 3 requirements of this capability.

#### Scenario: Game selects the pbr language
- **WHEN** the active `.game` file contains `shaders="pbr"`
- **THEN** the editor parses every `materials/*.mtr` file in the game's search paths using the PBR grammar and lists each defined material in the texture browser

#### Scenario: Other game types are unaffected
- **WHEN** the active `.game` file sets `shaders` to `doom3` or `quake4`
- **THEN** material parsing behaves exactly as before this change

#### Scenario: Quake 3 loads the same shaders
- **WHEN** the active `.game` file sets `shaders="quake3"`
- **THEN** the same set of shaders loads as before this change, with the same editor images and flags, and additionally carries the derived PBR data

### Requirement: Colour space handling
Base colour and emissive textures SHALL be treated as sRGB and decoded to linear in the lighting shader. Normal, metallic-roughness, and occlusion textures SHALL be sampled as linear data. The texture gamma preference SHALL NOT be applied to any texture loaded for lighting-mode shading, nor to the editor image of a `.mtr` material. The textured-mode editor image of a Quake 3 shader SHALL keep the gamma preference, as before this change.

#### Scenario: Gamma preference set
- **WHEN** the texture gamma preference is set to a value other than 1.0 and a PBR material's textures are loaded
- **THEN** the uploaded texel data is identical to the source image

#### Scenario: Mid-grey base colour
- **WHEN** a base colour texture is uniform sRGB value 128 and the material is lit by a white light at normal incidence with unit irradiance
- **THEN** the shaded linear result before tonemapping is approximately 0.216 / pi, not 0.5 / pi

#### Scenario: Quake 3 textured mode unchanged
- **WHEN** the texture gamma preference is not 1.0 and a Quake 3 PBR shader is shown in textured mode
- **THEN** its editor image looks exactly as before this change

### Requirement: Material files are the single source for the baker
The material scripts the editor reads SHALL be the only material description: `scripts/*.shader` for the `quake3` language and `materials/*.mtr` for the `pbr` language. The `.mtr` grammar SHALL remain a flat, line-oriented key-value format so the SH baker can parse the same files with a small reader, and `surfaceparm` and `q3map_` keys SHALL remain inside them. For Quake 3 shaders, the editor's rules for base colour, emissive, PBR keywords and defaults (this capability) are the reference that q3map2's SH stage and `renderer_sh` implement in their own parsers.

#### Scenario: Baker reads a material
- **WHEN** the baker parses a `.mtr` file that the editor accepts
- **THEN** it resolves the same texture paths and factors as the editor without any intermediate export

#### Scenario: Another consumer reads a Quake 3 shader
- **WHEN** q3map2's SH stage or `renderer_sh` reads a `scripts/*.shader` entry that the editor classifies as PBR
- **THEN** applying this capability's rules yields the same base colour, maps, factors and defaults as the editor
