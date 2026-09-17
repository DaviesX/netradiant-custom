## ADDED Requirements

### Requirement: PBR gamepack layout
A gamepack named `pbr.game` SHALL be provided with `games/pbr.game` declaring `type="q3"`, `shaders="pbr"` (material module), `entities="pbr"` (entity module), `entityclass="quake3"` with `entityclasstype="xml"` (definition loader and format), `basegame="base"`, `shaderpath="materials"`, and standard Quake 3 map, brush, patch, archive, and texture types. The gamepack directory SHALL contain `default_build_menu.xml`, `base/entities.ent` (XML definitions, the only format that carries per-key defaults for the inspector), and `base/default_shaderlist.txt`.

#### Scenario: Gamepack installed
- **WHEN** the gamepack is copied into `gamepacks/` and the editor starts
- **THEN** "PBR" appears in the game selection dialog and selecting it loads the PBR material and entity modules

### Requirement: Build menu invokes q3map2 BSP and VIS stages
The default build menu SHALL provide at least "BSP", "BSP + VIS", and "BSP + VIS + light (fast)" entries invoking the bundled q3map2 with `-meta`, `-vis`, and `-light -fast` respectively. No entry SHALL depend on a tool outside the repository.

#### Scenario: Compile from the editor
- **WHEN** the designer runs "BSP + VIS" on a saved map
- **THEN** q3map2 produces a `.bsp` next to the map with no errors

### Requirement: Benchmark map
A benchmark map SHALL ship with the gamepack containing: one street block bounded by two building shells with enterable interiors, a low sky ceiling above head height, one fog volume, one `light_sun`, at least two `light` and two `light_spot` entities, and materials that exercise base colour, normal, metallic-roughness, occlusion, emissive, mask alpha, and double-sided. The map SHALL compile leak-free.

#### Scenario: Map compiles
- **WHEN** the benchmark map is compiled with "BSP + VIS"
- **THEN** q3map2 reports no leak and produces a BSP

#### Scenario: Every material type visible
- **WHEN** the benchmark map is opened in lighting draw mode
- **THEN** at least one surface of each listed material feature is visible from the map's spawn position

### Requirement: Engine parity criterion
The benchmark map SHALL include a documented camera position and exposure. The editor lighting view from that position SHALL match the engine's direct-light-only render of the compiled map within a documented tolerance. The comparison procedure and tolerance SHALL be recorded in a `README.md` in the gamepack.

#### Scenario: Parity check performed
- **WHEN** screenshots are taken from the documented position in editor and engine
- **THEN** the README's procedure yields a pass
