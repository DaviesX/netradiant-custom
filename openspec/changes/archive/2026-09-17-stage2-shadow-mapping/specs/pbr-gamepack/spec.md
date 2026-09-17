## MODIFIED Requirements

### Requirement: Benchmark map
A benchmark map SHALL ship with the gamepack containing: one street block bounded by two building shells with enterable interiors, a low sky ceiling above head height, one fog volume, one `light_sun`, at least two `light` and two `light_spot` entities, and materials that exercise base colour, normal, metallic-roughness, occlusion, emissive, mask alpha, and double-sided. The map SHALL compile leak-free.

The map SHALL additionally contain geometry that demonstrates occlusion: at least one free-standing pillar or prop casting a spot shadow onto a wall or floor, at least one patch surface positioned to cast, and an interior that is sealed from the sun so its floor is unlit by it. At least one light SHALL set `_shadows 0` so the non-casting path is exercised.

#### Scenario: Map compiles
- **WHEN** the benchmark map is compiled with "BSP + VIS"
- **THEN** q3map2 reports no leak and produces a BSP

#### Scenario: Every material type visible
- **WHEN** the benchmark map is opened in lighting draw mode
- **THEN** at least one surface of each listed material feature is visible from the map's spawn position

#### Scenario: Shadows visible from the documented camera
- **WHEN** the benchmark map is opened in lighting draw mode at the documented camera position
- **THEN** a sun shadow and a spot shadow are both visible in the same view

## ADDED Requirements

### Requirement: Shadow parameters documented in the gamepack README
The gamepack `README.md` SHALL document the shadow scheme so it can be compared against the engine later: the caster culling mode, the normal-offset and slope-scaled bias constants, the PCF kernel, the cascade count, resolution, split lambda, texel snapping and near-plane padding, the spot atlas dimensions and per-light tile size, and the surface types excluded from the caster set. It SHALL state which of these are chosen to match `sh-renderer` and which are editor-side choices that may need revisiting when engine parity is attempted.

#### Scenario: Reader can reproduce the scheme
- **WHEN** a reader follows the README's shadow section
- **THEN** every constant used by the editor's shadow path is listed with its value and its source
