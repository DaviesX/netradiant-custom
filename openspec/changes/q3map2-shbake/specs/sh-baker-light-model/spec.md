# Spec Delta

## Purpose

sh-baker's support for the editor's physical light model, so a q3map2 bake lights a level the way the editor preview and `renderer_sh` do. It covers range cutoff, cone falloff, per-light shadow casting, and a separate, exact punctual-direct result for callers that store direct and indirect light apart.

## ADDED Requirements

### Requirement: Range cutoff
A point or spot light SHALL have a range. A light with a range above 0 SHALL add nothing to a shaded point farther from the light than its range, both at the baked point and at every bounce. A range of 0, the default, SHALL mean no cutoff. Directional and area lights SHALL have no range.

#### Scenario: Point beyond range is dark
- **WHEN** a scene holds one point light of range 256 above an open floor and nothing else, and points at distances 200 and 300 from the light are baked
- **THEN** the point at 200 has a positive L0, and the point at 300 has an L0 of exactly 0

#### Scenario: Default range unchanged
- **WHEN** the same scene is baked with the light's range left at its default
- **THEN** both points have a positive L0

### Requirement: Cone falloff mode
A spot light SHALL have a cone falloff mode: *linear*, the default, which keeps today's falloff, or *smoothstep*. With smoothstep, the cone factor SHALL be t² (3 − 2t), where t = clamp((cos θ − cos outer) / (cos inner − cos outer), 0, 1) and θ is the angle between the spot's direction and the direction to the shaded point. With smoothstep, when cos inner ≤ cos outer, the factor SHALL be 1 for cos θ ≥ cos outer and 0 otherwise. Linear mode SHALL use the same hard edge when cos inner equals cos outer, and keep today's formula otherwise.

#### Scenario: Smoothstep midpoint
- **WHEN** a smoothstep spot light with an outer half-angle of 45° and an inner half-angle of 30° lights a point at the angle where t = 0.25
- **THEN** the point's punctual-direct irradiance equals 0.15625 times that of a point at the same distance and incidence on the spot's axis, within 1e-4 relative

#### Scenario: Linear kept by default
- **WHEN** a spot light whose falloff mode is left at its default lights a point at the angle where t = 0.25
- **THEN** the cone factor is 0.25, as before this change

#### Scenario: Equal cones give a hard edge
- **WHEN** a spot light has equal inner and outer half-angles of 30°, in either mode, and points just inside and just outside 30° are baked
- **THEN** the inside point gets the full on-axis factor, the outside point gets 0, and no value is NaN or infinite

### Requirement: Per-light shadow casting
Every light SHALL have a flag for casting shadows, true by default. A light that doesn't cast SHALL light a point without any visibility test between them, both at the baked point and at every bounce. A light that casts SHALL keep today's visibility test, including alpha transmittance.

#### Scenario: Non-casting light ignores a blocker
- **WHEN** an opaque quad sits between a point light and a baked point, and the scene is baked once with the light casting and once with it not casting
- **THEN** the point's punctual-direct irradiance is 0 when the light casts, and equals the unblocked value when it doesn't

### Requirement: Range-aware light selection
A light-tree node SHALL get zero importance at a shaded point that is beyond the range of every ranged light it holds, if it holds no unranged light. When the whole non-directional set has zero importance at a point, light selection SHALL choose only among directional lights there, with sampling and its probability agreeing. Lights' emitter bounds SHALL be unchanged.

#### Scenario: Out-of-range lights not sampled
- **WHEN** a scene holds one point light of range 64 and a sun, and a point 500 units from the point light is baked with one light sample per estimate
- **THEN** every light sample at that point selects the sun

### Requirement: Separate punctual-direct result
A bake option SHALL split off the punctual lights' direct light at each baked point. With it set:
- the main SH result SHALL omit that light, as an indirect-only bake does;
- a second SH result SHALL hold it, as the sum over punctual lights of incident radiance projected onto the SH basis;
- a third result SHALL hold its irradiance at the point's normal: the sum of E⊥ · max(0, n · ω) · visibility over punctual lights.

Each light SHALL be evaluated once, with one visibility test and no random sampling. Area lights' direct light SHALL stay in the main result.

#### Scenario: Split sums to the full bake
- **WHEN** a scene with a casting point light, a sun and an emissive quad is baked once normally and once with the split option, with the same samples and seed
- **THEN** for every baked point, the main result plus the punctual-direct SH equals the normal bake's result within the bake's sampling noise, measured as a relative L0 difference below 5% at 1024 samples

#### Scenario: Exact punctual irradiance
- **WHEN** a split bake holds one point light of radiant intensity I at distance r straight above a baked point on an open floor
- **THEN** the point's punctual-direct irradiance is I / r² in every channel, within 1e-5 relative

#### Scenario: Punctual direct is deterministic
- **WHEN** the same split bake is run with 16 samples and with 1024 samples
- **THEN** the punctual-direct SH and irradiance of the two bakes are equal bit for bit

#### Scenario: Skipped points keep the marker
- **WHEN** a split bake includes a point with a negative material id
- **THEN** its main and punctual-direct SH hold the not-baked marker (every coefficient −1), and its punctual irradiance is −1 in every channel

### Requirement: Defaults keep existing output
With every new light field and the split option at their defaults, a bake SHALL give bit-identical results to the bake before this change for the same scene, points and configuration, wherever the earlier bake's result was finite. The CLI SHALL set no new field, so its output is unchanged.

#### Scenario: Existing tests pass
- **WHEN** sh-baker's test suite is run after this change
- **THEN** every test that existed before it passes unchanged
