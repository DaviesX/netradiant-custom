# sh-baker-light-model Specification

## Purpose
sh-baker's support for the editor's physical light model and for q3map2's light-grid probes, so a q3map2 bake lights a level the way the editor preview and `renderer_sh` do. It covers range cutoff, cone falloff, range-aware light selection (merged in PR #65, ad8f39f) full-sphere points (PR #67, fdad058) and a fixed sample count (PR #68, 8f181ba). Every light casts shadows in a bake: sh-baker has no per-light shadow switch, and it returns no separate punctual-direct result.

## Requirements

### Requirement: Range cutoff
A point or spot light SHALL have a range. A light with a range above 0 SHALL add nothing to a shaded point farther from the light than its range, both at the baked point and at every bounce. A range of 0, the default, SHALL mean no cutoff. Directional and area lights SHALL have no range.

#### Scenario: Point beyond range is dark
- **WHEN** a scene holds one point light of range 256 above an open floor and nothing else, and points at distances 200 and 300 from the light are baked
- **THEN** the point at 200 has a positive L0, and the point at 300 has an L0 of exactly 0

#### Scenario: Default range unchanged
- **WHEN** the same scene is baked with the light's range left at its default
- **THEN** both points have a positive L0

### Requirement: Cone falloff mode
A spot light SHALL have a cone falloff mode: *linear*, the default, which keeps today's falloff, or *smoothstep*. With smoothstep, the cone factor SHALL be t² (3 − 2t), where t = clamp((cos θ − cos outer) / (cos inner − cos outer), 0, 1) and θ is the angle between the spot's direction and the direction to the shaded point. With smoothstep, when cos inner ≤ cos outer, the factor SHALL be 1 for cos θ ≥ cos outer and 0 otherwise. Linear mode SHALL keep today's formula unchanged.

#### Scenario: Smoothstep midpoint
- **WHEN** a smoothstep spot light with an outer half-angle of 45° and an inner half-angle of 30° lights a point at the angle where t = 0.25
- **THEN** the spot's incident irradiance at the point equals 0.15625 times that at a point at the same distance and incidence on the spot's axis, within 1e-4 relative

#### Scenario: Linear kept by default
- **WHEN** a spot light whose falloff mode is left at its default lights a point at the angle where t = 0.25
- **THEN** the cone factor is 0.25, as before this change

#### Scenario: Equal cones give a hard edge
- **WHEN** a smoothstep spot light has equal inner and outer half-angles of 30°, and points just inside and just outside 30° are baked
- **THEN** the inside point gets the full on-axis factor, the outside point gets 0, and no value is NaN or infinite

### Requirement: Range-aware light selection
A light-tree node's bounds SHALL carry the largest range of the lights below it, or no range if any of them is unranged. The node SHALL get zero importance at a shaded point farther than that range from its bounds. A lone ranged light is therefore never picked beyond its range. Lights that share a node can still be picked out of range, and then add nothing. When the shaded point is beyond the range of the tree's root, light selection SHALL choose only among directional lights there, or sample nothing if there are none, with sampling and its probability agreeing. Lights' emitter bounds SHALL be unchanged.

#### Scenario: Out-of-range lights not sampled
- **WHEN** a scene holds one point light of range 64 and a sun, and a point 500 units from the point light is baked with one light sample per estimate
- **THEN** every light sample at that point selects the sun

### Requirement: Full-sphere points
A surface point SHALL have a full-sphere flag, false by default. When it is set, the point SHALL be a probe in open space rather than a surface:
- its sample rays SHALL be uniform over the whole sphere in world space, weighted 4π per sample instead of 2π, and its normal and tangent SHALL NOT be used;
- next-event estimation at the point SHALL pass a zero normal to the light tree and SHALL count light from every direction, with no rejection by the normal's cosine;
- only the baked point itself changes: the bounces of its paths SHALL shade as before.

Its SH SHALL be the projection of incident radiance from the whole sphere onto the SH basis, with no cosine. Its irradiance value has no meaning, and callers SHALL NOT use it. The flag works the same in a full bake and with `indirect_only`.

#### Scenario: Probe between opposed lights
- **WHEN** a full-sphere point in empty space lies midway between two point lights pointing at each other along one axis, with no geometry
- **THEN** its SH matches the analytic full-sphere projection of the two lights, with L1 along the axis cancelling for equal lights, within the bake's sampling noise

#### Scenario: Edge-on sun counted
- **WHEN** a full-sphere point whose normal field is set at right angles to a sun's direction is baked with no geometry
- **THEN** its L0 is the sun's irradiance × 0.282095, within the bake's sampling noise, and is not 0

#### Scenario: Uniform surroundings
- **WHEN** a full-sphere point is baked at the centre of a closed box of inward-facing emissive quads of uniform radiance L and albedo 0, with no other geometry
- **THEN** its L0 is 2√π · L, and its L1 and L2 coefficients are 0, within the bake's sampling noise

#### Scenario: Surface points unchanged
- **WHEN** full-sphere points are appended after the surface points of a bake
- **THEN** each surface point's result is bit-identical to the same bake without them, since each point is seeded by its index

### Requirement: Fixed sample count
A `BakeConfig::confidence_threshold` at or below 0 SHALL turn off the sensor's adaptive stop, so every valid point takes exactly `samples` rays. A threshold above 0 SHALL keep the adaptive stop as before; the default stays 0.01.

#### Scenario: Zero-variance point takes every sample
- **WHEN** a point lit only by one unshadowed point light, whose per-sample L0 is constant, is baked with `samples` 128 and `confidence_threshold` 0
- **THEN** it takes 128 samples, where the default threshold stops it at the 64-sample minimum

### Requirement: Defaults keep existing output
With every new light field, the full-sphere flag and `confidence_threshold` at their defaults, a bake SHALL give bit-identical results to the bake before this change for the same scene, points and configuration, wherever the earlier bake's result was finite. There are two exceptions from change `sh-baker-sky-radiance` on (spec `sh-baker-environment-light`): surface points' environment visibility is cosine-weighted, while their SH, irradiance and full-sphere points' visibility stay pinned; and a scene with an environment gains sky light. The CLI SHALL set no new field, so its output changes only where its scene has an environment (the glTF loader derives one from the brightest directional light when no skybox is named).

#### Scenario: Existing tests pass
- **WHEN** sh-baker's test suite is run after this change
- **THEN** every test that existed before it passes; the changed assertions are: `IndirectProbeIsSumOfHemispheres` drops its visibility identity (probe = mean of the two hemispheres), the tracer tests adapt to the first-ray callback's new signature without changing what they assert about radiance, and the pinned default-bake references (`kDefaultsFull` and `kDefaultsIndirectOnly`) have their surface points' visibility words re-recorded with the cosine-weighted visibility (sh-baker PR #70), while their SH and irradiance words stay as recorded at `046ff2e`
