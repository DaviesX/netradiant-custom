# Spec Delta

## MODIFIED Requirements

### Requirement: The emissive strength keyword
`qer_pbr_emissiveStrength` SHALL multiply emissive radiance only. It SHALL have no effect on a shader without an emissive stage, except a sky shader, where it scales the sky radiance as sky-environment-light's requirement "Sky radiance" defines. A Quake 3 shader's emissive radiance SHALL be the decoded emissive texel × the emissive colour × `qer_pbr_emissiveStrength`, with no Quake 3 specific scale. It is added to the base pass in the same linear units as the light passes' output, before exposure and tonemapping.

#### Scenario: White glow at strength 1
- **WHEN** a shader's additive stage is a uniform white image with no `rgbGen const` and no `qer_pbr_emissiveStrength`, and it is viewed in lighting mode with ambient 0 and no light reaching it
- **THEN** its base pass writes a linear radiance of 1 in every channel before exposure and tonemapping

#### Scenario: Boosted glow
- **WHEN** a shader with an additive glow stage declares `qer_pbr_emissiveStrength 4`
- **THEN** its emissive radiance in lighting mode is four times that of the same shader without the keyword

#### Scenario: Sky strength
- **WHEN** a sky shader with no additive stage declares `qer_pbr_emissiveStrength 4`
- **THEN** it has no emissive stage, so the sky surface itself draws as before, and its sky radiance is four times that of strength 1
