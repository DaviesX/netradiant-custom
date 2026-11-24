# Spec Delta

## MODIFIED Requirements

### Requirement: Fog colour equals sky colour
One fog volume SHALL cover the block, and its `fogparms` colour SHALL equal the colour the sky shader draws, so that a building fading into fog meets the sky without a visible seam on the stock renderers and in the editor's lighting mode.

#### Scenario: Colours agree in the source
- **WHEN** the `fogparms` colour in `scripts/town.shader` is changed so that it differs from the colour the texture generator gives the sky image, and the texture generator is run
- **THEN** it exits non-zero, names both colours and writes no sky image

#### Scenario: Seam check
- **WHEN** the stock check's screenshot of a street view whose end building is still drawn through the fog (nearer than the fog's opacity distance) is sampled on `opengl1` and on `opengl2`, once in the sky and once on that building just below the ceiling
- **THEN** the two samples differ by no more than 8 in any 8-bit channel

#### Scenario: Editor seam check
- **WHEN** the editor's lighting-mode capture of the `street` view (shadows on, ambient 0, "Lighting fog" on) is sampled at the image's horizontal centre, once in the sky and once on the far facade just below the ceiling
- **THEN** the two samples differ by no more than 8 in any 8-bit channel, and the sky sample is 51 66 59 within 1
