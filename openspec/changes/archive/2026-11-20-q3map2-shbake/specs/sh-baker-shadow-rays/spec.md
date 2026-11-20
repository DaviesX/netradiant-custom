# Spec Delta

## MODIFIED Requirements

### Requirement: Applies at bake points and at bounces
Transmittance SHALL apply to every direct-light sample: those taken at the bake points themselves, and those taken at each bounce of an indirect path. It SHALL apply to every light type sh-baker bakes (point, spot, directional, area). Every light casts shadows in a bake; sh-baker has no per-light switch to turn the visibility test off. Full-sphere points (spec `sh-baker-light-model`) SHALL apply it as surface points do.

#### Scenario: Bake point behind a card
- **WHEN** a single bake point is baked with a point light behind a 128/255-alpha quad and no indirect bounces
- **THEN** its L0 is 1 - 128/255 of its value without the quad, within the bake's noise tolerance

#### Scenario: Bounce point behind a card
- **WHEN** a path vertex at a bounce evaluates direct light from a point light behind a 128/255-alpha quad
- **THEN** that direct-light estimate is 1 - 128/255 of its value without the quad, within 1%
