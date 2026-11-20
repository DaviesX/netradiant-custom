# Spec Delta

## MODIFIED Requirements

### Requirement: Applies at bake points and at bounces
Transmittance SHALL apply to every direct-light sample of a light that casts shadows: those taken at the bake points themselves, and those taken at each bounce of an indirect path. It SHALL apply to every light type sh-baker bakes (point, spot, directional, area). A light whose shadow-casting flag is off SHALL be evaluated with no visibility test (spec `sh-baker-light-model`).

#### Scenario: Bake point behind a card
- **WHEN** a single bake point is baked with a point light behind a 128/255-alpha quad and no indirect bounces
- **THEN** its L0 is 1 - 128/255 of its value without the quad, within the bake's noise tolerance

#### Scenario: Bounce point behind a card
- **WHEN** a path vertex at a bounce evaluates direct light from a point light behind a 128/255-alpha quad
- **THEN** that direct-light estimate is 1 - 128/255 of its value without the quad, within 1%

#### Scenario: Non-casting light behind a card
- **WHEN** the same bake point is baked with the point light's shadow-casting flag off
- **THEN** its punctual-direct irradiance equals its value without the quad
