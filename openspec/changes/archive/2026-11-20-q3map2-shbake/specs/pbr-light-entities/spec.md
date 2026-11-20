# Spec Delta

## MODIFIED Requirements

### Requirement: Point light keys
A `light` entity SHALL read `origin`, `_color` (linear RGB, default 1 1 1), `intensity` (float, default 50000, radiant flux in the light model's arbitrary linear unit), and `radius` (float, default 256, cutoff distance in map units). The renderer SHALL receive the light as a point light with those values. The editor SHALL draw the existing radius sphere at `radius` when light radii display is enabled.

#### Scenario: Default point light
- **WHEN** a `light` entity is placed with no keys other than `origin`
- **THEN** it renders in lighting mode as a white point light of radiant flux 50000 (the model's linear unit) and cutoff 256

#### Scenario: Radius edited
- **WHEN** the `radius` key is changed in the entity inspector
- **THEN** the radius sphere and the light's culling bounds update immediately
