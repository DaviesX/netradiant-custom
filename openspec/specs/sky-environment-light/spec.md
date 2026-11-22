# sky-environment-light Specification

## Purpose
Defines how a Quake 3 map's sky shader becomes an environment light: which sky shader counts, its radiance in the physical light units, and its L0–L2 spherical-harmonic radiance and irradiance. The editor preview uses it today, and the full bake (B12) and `renderer_sh` (C6) reuse it.

## Requirements

### Requirement: The map's sky shader
The editor SHALL take as candidates the distinct shaders that declare `skyparms` on the map's brush faces and patches (all entities, hidden ones included). A shader with `surfaceparm sky` but no `skyparms` SHALL be no candidate and gives no sky light. It SHALL use the alphabetically first by shader name, compared without case. When there are two or more candidates, it SHALL print one console warning naming every candidate and the chosen one. It SHALL print that warning again only when the set of candidates changes. A map with no candidate has no sky light.

#### Scenario: One sky
- **WHEN** a map's ceiling uses `textures/town/sky` and no other sky shader appears
- **THEN** `textures/town/sky` is the map's sky shader and no warning is printed

#### Scenario: Two skies
- **WHEN** a map uses `textures/b/sky` and `textures/a/sky` on different brushes
- **THEN** `textures/a/sky` is used, and the console warns once and names both

#### Scenario: surfaceparm sky alone
- **WHEN** a map's only sky-like shader declares `surfaceparm sky` and `qer_pbr_emissiveStrength 4` but no `skyparms`
- **THEN** the map has no sky light

#### Scenario: No sky
- **WHEN** a sealed interior map has no sky shader
- **THEN** it has no sky light and no warning is printed

### Requirement: Sky radiance
The sky shader's radiance in direction ω SHALL be L(ω) = `qer_pbr_emissiveStrength` × C × T(ω):
- C is the sky stage's `rgbGen const`, or white without one. The sky stage is the shader's first map stage that is not `$lightmap`.
- T(ω) is the sky image in linear RGB, as the next two requirements define it.

Texels SHALL be decoded from sRGB with the piecewise curve, and their alpha SHALL be ignored. A sky shader without `qer_pbr_emissiveStrength`, or with a strength of 0 or less, SHALL emit nothing.

#### Scenario: Legacy sky
- **WHEN** a stock baseq3 sky shader without `qer_pbr_emissiveStrength` is the map's sky
- **THEN** its sky radiance is zero everywhere

#### Scenario: Strength and colour
- **WHEN** a cloud-layer sky's image is a single sRGB value of 188, its stage has `rgbGen const ( 1 0.5 0.25 )` and its strength is 4
- **THEN** L(ω) is 4 × 0.5029 × (1, 0.5, 0.25) within 0.001 for every direction above the horizon

### Requirement: Farbox sky
When `skyparms` names a farbox, T(ω) SHALL be the six images `<farbox>_rt`, `_bk`, `_lf`, `_ft`, `_up` and `_dn`. The stock renderer draws them on +X, +Y, −X, −Y, +Z and −Z in map coordinates respectively (`_rt` +X, `_lf` −X, `_bk` +Y, `_ft` −Y; `tr_sky.cpp`'s `kSkyTexOrder`), and each texel SHALL lie in the direction where the stock renderer draws it. Each texel SHALL weigh by the solid angle it subtends. Cloud stages SHALL be ignored when a farbox is present. If any of the six images fails to load, the console SHALL print one warning naming the farbox, and the sky SHALL emit nothing.

#### Scenario: Uniform farbox
- **WHEN** all six faces are the same uniform colour of linear value v, and the strength is s
- **THEN** the sky irradiance is π × s × v within 0.5% for every normal

#### Scenario: Up face only
- **WHEN** only the `_up` face is non-black
- **THEN** the sky irradiance at normal +Z is larger than at any other normal, and the irradiance at −Z is less than 5% of the irradiance at +Z

#### Scenario: Orientation matches the drawn sky
- **WHEN** only the `_rt` face is non-black, and the editor camera looks along +X in textured mode
- **THEN** the camera sees the non-black face, and the sky irradiance at normal +X is larger than at −X, ±Y and ±Z

#### Scenario: Missing face
- **WHEN** a farbox's `_dn` image is missing
- **THEN** the console warns once naming the farbox, and the map has no sky light

### Requirement: Cloud-layer sky
When `skyparms` names no farbox (`-`), T(ω) SHALL be the mean linear texel M of the sky stage's image for every ω with a positive Z component, and black below the horizon. Only the sky stage counts: further cloud stages, additive or blended, SHALL be ignored. The image is read from disk, and the mean is unweighted over the whole image. If the image fails to load, the console SHALL print one warning naming it, and the sky SHALL emit nothing.

#### Scenario: Second cloud layer ignored
- **WHEN** a cloud-layer sky has a second stage with `blendFunc add`
- **THEN** its sky radiance is the same as without that stage

#### Scenario: Open floor
- **WHEN** a cloud-layer sky has radiance L above the horizon
- **THEN** the sky irradiance is π L at normal +Z, π L / 2 at any horizontal normal and 0 at −Z, each within 0.5% of π L

### Requirement: Spherical-harmonic convention
The sky radiance SHALL be projected onto the nine real L0–L2 spherical harmonics, in RGB, in Z-up map coordinates. They are ordered and scaled as sh-baker's `SHCoeffs`: Y₀₀ = 0.282095; then 0.488603 × y, × z and × x; then 1.092548 × xy, 1.092548 × yz, 0.315392 × (3z² − 1), 1.092548 × xz and 0.546274 × (x² − y²). The sky irradiance at unit normal n SHALL be E(n) = Σ Âₗ × Lₗₘ × Yₗₘ(n), with Â₀ = π, Â₁ = 2π/3 and Â₂ = π/4, and each channel clamped below at 0. The SH SHALL be used only for irradiance: a consumer that needs radiance in a direction (B12's full bake, on escaping rays) SHALL use L(ω) itself, never the truncated SH radiance, which is negative below the horizon of a cloud-layer sky.

#### Scenario: Uniform sphere
- **WHEN** the radiance is a constant L in every direction
- **THEN** the only non-zero coefficient is L₀₀ = 0.282095 × 4π × L, and E(n) = π L for every normal

#### Scenario: Radiance below the horizon
- **WHEN** a consumer asks for the radiance of a cloud-layer sky in direction −Z
- **THEN** it gets 0 from L(ω), not the −L/4 the truncated SH would give

### Requirement: Sky light helpers are tested
These SHALL live in a header-only library with no editor dependency, `libs/skylight.h`:
- the sRGB decode and the image mean;
- the farbox face-to-direction mapping and the solid-angle weights;
- L(ω) for a direction, and the cube and upper-hemisphere projections;
- the cosine convolution and E(n);
- the choice among sky candidates.

q3map2 SHALL be able to include it. `make test-sky-light` SHALL build and run its tests, and the target SHALL stay out of the installed tree.

#### Scenario: Tests pass
- **WHEN** `make test-sky-light` runs
- **THEN** it exits 0, with a case for each scenario of this capability that needs no editor

### Requirement: Sky light model documented in the gamepack README
The gamepack README SHALL describe:
- the sky light: which shader counts, the radiance formula, and the farbox face mapping, including the texel orientation within each face;
- the cloud-layer hemisphere (the sky stage only), the SH basis, order and constants, and the convolution;
- that bakes and other radiance consumers use L(ω), not the SH, and that `surfaceparm sky` without `skyparms` gives no sky light;
- that q3map2 still treats a sky with an additive stage and a strength as an emitter until B12 excludes sky shaders;
- the "Lighting sky" preference and the overlighting of interiors in the editor.

The description SHALL be complete enough to reimplement the derivation without reading the editor's code.

#### Scenario: README section
- **WHEN** the gamepack README is read
- **THEN** it has a sky light section with each item above, and the town's strength is worked through as an example
