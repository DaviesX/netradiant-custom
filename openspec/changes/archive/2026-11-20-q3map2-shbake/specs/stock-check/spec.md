# Spec Delta

## MODIFIED Requirements

### Requirement: Isolated compile
The check SHALL copy the given mod directory into a fresh temporary basepath next to links to `baseq3/pak0–8.pk3`, and compile there with a fresh temporary homepath. It SHALL use `-meta -keeplights`, then `-vis -saveprt`, then `-light -shbake` with its default of both bakes (the full bake for the lightmap, grid and vertex colours, the indirect bake for the side file), which needs a q3map2 built with `SH_BAKER=1`. It SHALL fail when q3map2 exits non-zero, reports a leak (`leaked`, case-insensitive), or prints `Unknown q3map_* directive`, or when the light stage leaves no side file at `maps/<map>/<map>.shsf`. Before compiling, the check SHALL delete any side file and `*_l0_*.hdr` dump left in the copied mod. The pk3 SHALL include the side file. The compile SHALL read only the given mod directory and the stock paks, never `~/.q3a`.

#### Scenario: Unknown directive
- **WHEN** a shader used by the map in a scratch copy of the mod contains `q3map_notARealDirective`
- **THEN** the compile step fails and the offending q3map2 line is shown

#### Scenario: Side file packed
- **WHEN** the check passes on the benchmark
- **THEN** the packed pk3 holds `maps/bench/bench.shsf`, and no `.hdr` file

#### Scenario: q3map2 without SH baking
- **WHEN** the check is pointed at a q3map2 built without `SH_BAKER=1`
- **THEN** the compile step fails, showing q3map2's message naming `SH_BAKER=1`

#### Scenario: Stale side file ignored
- **WHEN** the source mod already holds `maps/bench/bench.shsf` and the light stage is made to write none (for example with `-shnoindirect` added in a scratch run)
- **THEN** the compile step fails for the missing side file
