## MODIFIED Requirements

### Requirement: One additive pass per light plus a base pass
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff.

When the light casts shadows, the pass SHALL multiply its contribution by the light's shadow term before accumulation. The base pass SHALL NOT be attenuated by any shadow term, so emissive and ambient remain visible in shadow.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point and spot cutoff and there is no sun
- **THEN** it renders with only its emissive and ambient base pass

#### Scenario: Two overlapping lights
- **WHEN** two point lights both reach a surface
- **THEN** the result equals the base pass plus the sum of the two light contributions

#### Scenario: Surface fully in shadow
- **WHEN** a surface lies inside the cone and radius of a casting spot light but behind an occluder
- **THEN** it renders with only its emissive and ambient base pass

#### Scenario: Emissive visible in shadow
- **WHEN** an emissive surface is occluded from every light
- **THEN** it still emits, because the base pass is not shadowed
