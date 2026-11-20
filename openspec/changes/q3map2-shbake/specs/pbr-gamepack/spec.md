# Spec Delta

## MODIFIED Requirements

### Requirement: Build menu invokes q3map2 BSP and VIS stages
The overlay's `Q3.game/default_build_menu.xml` SHALL hold every entry of the downloaded Quake III default build menu, unchanged and in order. After them it SHALL add a group that runs, with the bundled q3map2, `-meta -keeplights`, then `-vis -saveprt`, then `-light -shbake`. These are the stock check's compile stages. No entry SHALL depend on a tool outside the repository.

#### Scenario: Compile from the editor
- **WHEN** the designer opens `content/benchmark/maps/bench.map` under `Q3.game` with the `benchmark` mod and runs the town group, with q3map2 built with `SH_BAKER=1`
- **THEN** q3map2 produces `bench.bsp` with no leak and `maps/bench/bench.shsf`, and the BSP keeps the `light`, `light_spot` and `light_sun` entities

#### Scenario: Downloaded entries kept
- **WHEN** the overlay's menu is compared with the downloaded `Q3.game/default_build_menu.xml`
- **THEN** the only difference is the added group and its separator
