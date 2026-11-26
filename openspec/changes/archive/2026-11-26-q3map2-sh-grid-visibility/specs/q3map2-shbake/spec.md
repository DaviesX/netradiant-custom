## MODIFIED Requirements

### Requirement: Side file output
When the indirect bake runs, the stage SHALL write the side file to `maps/<name>/<name>.shsf` beside the BSP, through the q3map2 extension writer.

**What it holds.** It always holds the indirect bake: no direct light at the baked point, whether punctual, emissive or from the sky. It does hold bounced sky light when the map's sky emits. It holds:
- the SH pages;
- the un-shrunk hull mesh in BSP units, with hull triangles facing outward and q3map2's winding;
- the SH grid.

**The SH grid.** It has the BSP light grid's origin, spacing and dimensions, and one probe per BSP grid point in the same order (x fastest, then y, then z). Each probe holds that point's full-sphere SH from the indirect bake. It also holds that point's sky visibility from the indirect bake: the uniform fraction of the point's first rays that escaped, over the whole sphere. Visibility depends only on the geometry the rays trace, so a probe gets it whether or not the map's sky emits. A probe SHALL be marked invalid, with zero coefficients and zero sky visibility, when its grid point is still in solid after q3map2's nudge, or when its SH or visibility is non-finite. With `-nogrid` the SH grid has no probes, dimensions 0, 0, 0, and origin and spacing 0.

**Failures and other files.** A writer failure SHALL stop q3map2 with the writer's message. The stage SHALL NOT write any `lm_*.hdr` file.

#### Scenario: Side file written
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** `maps/bench/bench.shsf` exists and reads back with the extension reader, and no `lm_*.hdr` file exists under `maps/`

#### Scenario: No direct light in the side file
- **WHEN** a floor luxel in a spot's direct beam is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: No direct sky light in the side file
- **WHEN** a floor luxel under open sky in the town is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: SH grid matches the BSP grid
- **WHEN** the benchmark is baked with `-shbake` and its side file read back
- **THEN** the SH grid's dimensions and probe count equal the BSP light grid's, a probe in a lit room is valid with non-zero L0, and a probe whose grid point is black in the BSP because it is in solid is invalid, with sky visibility 0

#### Scenario: Probe sky visibility outdoors and indoors
- **WHEN** the town is baked with `-shbake` and its side file read back
- **THEN** a valid probe in the open crossroads, at least one grid step above the street and clear of walls, has a sky visibility above 0.25, and every valid probe inside a sealed room (no opening to the sky) has a sky visibility of exactly 0

#### Scenario: Probe sky visibility without an emitting sky
- **WHEN** a map is baked with `-shbake` once with a sky that has `qer_pbr_emissiveStrength` and once with the same sky without it
- **THEN** the two side files' probe sky visibilities and valid flags are equal, and the first bake has some valid probe with a sky visibility above 0
