# sh-baker-environment-light Specification

## Purpose
How sh-baker's point bake uses a scene environment: the sky radiance returned by rays that escape the scene, in the full and the indirect bake, and how a point's environment visibility is defined, so the full bake carries direct and bounced sky light, the indirect bake carries bounced sky light, and the visibility is what `renderer_sh` multiplies its direct sky light by.

## Requirements

### Requirement: Environment radiance on escaped rays
When the scene has an environment and the bake is not `indirect_only`, a ray that escapes the scene SHALL return the environment's radiance in the ray's direction. This SHALL hold for a bake point's own sample rays, for rays after any number of bounces, and for rays continuing past alpha cut-outs (a ray that passes a cut-out continues at the next depth, so light through a cut-out needs at least 1 bounce). Without an environment, an escaped ray SHALL return zero. There is no separate switch: the caller decides by setting or clearing `Scene::environment`.

#### Scenario: Open floor under a uniform sky
- **WHEN** a surface point facing up on an open floor is baked in full with a texture environment of uniform radiance L
- **THEN** its L0 is √π · L and its irradiance is π · L, within the bake's sampling noise

#### Scenario: Bounced sky light
- **WHEN** a surface point facing down sits above an infinite Lambertian floor of albedo a with nothing else in the scene and no lights, and is baked in full with a texture environment of uniform radiance L and at least 1 bounce
- **THEN** its L0 is √π · a · L within the bake's sampling noise

#### Scenario: Sky through a cut-out
- **WHEN** a point facing up under an alpha-0 quad is baked in full with a uniform environment and at least 1 bounce
- **THEN** its L0 equals that of the same point without the quad, within the bake's sampling noise

### Requirement: Indirect-only bakes carry only bounced sky radiance
In an `indirect_only` bake, an escaped ray SHALL return the environment's radiance only when its path has reflected off at least one surface. A path that reaches the sky directly, or only by passing alpha cut-outs, is direct sky light and SHALL return zero. So the indirect bake carries bounced sky light, and the direct sky light is left to the runtime (sky irradiance at the normal × visibility).

#### Scenario: Indirect bake of an open point
- **WHEN** a surface point facing up on an open floor is baked `indirect_only` with a uniform environment
- **THEN** its L0 is exactly 0 and its visibility is exactly 1

#### Scenario: Indirect bake keeps bounced sky
- **WHEN** the point of "Bounced sky light" is baked `indirect_only`
- **THEN** its L0 is √π · a · L within the bake's sampling noise, as in the full bake

#### Scenario: Indirect bake through a cut-out
- **WHEN** the point of "Sky through a cut-out" is baked `indirect_only`
- **THEN** its L0 is exactly 0

### Requirement: Radiance lookup
The radiance returned for a direction SHALL be the function whose texel-centre (or, for Preetham, exact) samples the environment's SH projection integrates, so the two agree within the projection's quadrature error. For a texture environment it SHALL be the equirect texture's value, bilinearly filtered, wrapping in longitude and clamped in latitude, with its texels used as stored (any intensity multiplier is already applied at load). For a Preetham environment it SHALL be the projection's capped Perez formula, zero below the horizon.

#### Scenario: Lookup agrees with the projection
- **WHEN** a 64×32 texture environment with a smooth colour gradient is integrated over the sphere by evaluating the lookup on a fine grid and projecting onto SH
- **THEN** the result matches the environment's existing SH projection within 1% of L0

#### Scenario: Equirect orientation
- **WHEN** a texture environment that is red in its top row and blue in its bottom row is looked up in the environment frame's +Y and −Y directions
- **THEN** +Y returns red and −Y returns blue

### Requirement: Environment frame
An environment SHALL have a rotation from world directions to its own frame, the identity by default. The lookup SHALL apply it before evaluating the environment in its Y-up convention, so a Z-up scene can use the Y-up equirect convention by passing a rotation that maps world +Z to +Y. The frame SHALL NOT change the environment's stored SH coefficients.

#### Scenario: Z-up scene
- **WHEN** an environment that is bright only near its +Y pole is given a rotation mapping world +Z to +Y, and a surface point facing world +Z is baked in full with no geometry
- **THEN** the point receives the bright light, and a point facing world −Z receives none

### Requirement: No environment keeps output
For a scene without an environment, a bake SHALL give the same SH and irradiance as the bake before this change for the same scene, points and configuration. The environment lookup SHALL draw no random numbers. A scene with an environment gains sky light in both bakes, including in the CLI, whose glTF loader derives a Preetham environment from the brightest directional light when the scene names no skybox.

#### Scenario: Existing tests
- **WHEN** sh-baker's test suite is run after this change
- **THEN** every pre-existing test passes; the pinned default-bake references, which have no environment, keep their SH and irradiance words

#### Scenario: CLI fixtures gain sky light
- **WHEN** the CLI fixtures (`box`, `MultiLights`, `layers`, `notangent`) are baked after this change with the same arguments (checked by the landing gate, not the unit suite)
- **THEN** their saved glTF and binary files are byte-identical to the commit before, and their SH and irradiance differ by the derived sky's light

### Requirement: Cosine-weighted visibility for surface points
A surface point's environment visibility SHALL be Σ eᵢ cos θᵢ / Σ cos θᵢ over its sample rays, where eᵢ is 1 if the sample's first ray escapes the scene and 0 otherwise, and θᵢ is the angle between that ray and the point's normal. It SHALL lie in [0, 1]. If Σ cos θᵢ is 0, the visibility SHALL be 0. An alpha cut-out still counts as blocking the first ray.

#### Scenario: Skylight hole
- **WHEN** a point facing up lies under an opaque ceiling with a circular hole directly above it whose edge is 60° from the normal, and is baked with 4096 samples
- **THEN** its visibility is 0.75 (sin² 60°) within 0.02, where the uniform fraction would be 0.5

#### Scenario: Fully open and fully closed
- **WHEN** one point facing up has no geometry around it and another lies inside a closed box
- **THEN** their visibilities are exactly 1 and exactly 0

### Requirement: Uniform visibility for full-sphere points
A full-sphere point's environment visibility SHALL stay the fraction of its sample rays whose first ray escapes the scene, with no cosine weighting, since the point has no normal.

#### Scenario: Probe above a floor
- **WHEN** a full-sphere point above an infinite opaque floor is baked with 4096 samples
- **THEN** its visibility is 0.5 within 0.02

### Requirement: Visibility independent of sky radiance
With `confidence_threshold` at or below 0 (every point takes exactly `samples` rays, as q3map2 always passes), a point's environment visibility SHALL NOT depend on whether sky radiance is returned: for the same scene geometry, points and configuration, a bake with an environment and the same bake with the environment removed SHALL give bit-identical visibility. Changing how visibility is weighted SHALL NOT change any SH coefficient.

#### Scenario: Same visibility with and without sky radiance
- **WHEN** a scene with lights, cut-outs and an environment is baked in full with `confidence_threshold` 0, once with the environment and once without it
- **THEN** the two bakes' visibility values are bit-identical
