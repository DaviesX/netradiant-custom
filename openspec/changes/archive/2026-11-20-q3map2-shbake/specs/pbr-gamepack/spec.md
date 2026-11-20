# Spec Delta

## MODIFIED Requirements

### Requirement: Build menu invokes q3map2 BSP and VIS stages
The overlay's `Q3.game/default_build_menu.xml` SHALL hold every entry of the downloaded Quake III default build menu, unchanged and in order. After them it SHALL add three builds, "Town (draft)", "Town (pre-release)" and "Town (final)", each of which runs, with the bundled q3map2, `-meta -keeplights`, then `-vis -saveprt`, then a light stage with `-light -shbake` and no bake toggle, so both bakes run and the result is the shippable one for stock ioquake3 and `renderer_sh`. The light stages SHALL differ only in quality: `-shsamples 128` for the draft, `-shsamples 512 -super 2` for the pre-release build and `-shsamples 2048 -super 2` for the final build. The first two stages and the draft's light stage at its defaults (`-light -shbake`) are the stock check's compile stages. No entry SHALL depend on a tool outside the repository.

#### Scenario: Compile from the editor
- **WHEN** the designer opens `content/benchmark/maps/bench.map` under `Q3.game` with the `benchmark` mod and runs any of the three Town builds, with q3map2 built with `SH_BAKER=1`
- **THEN** q3map2 produces `bench.bsp` with no leak and `maps/bench/bench.shsf`, the BSP keeps the `light`, `light_spot` and `light_sun` entities, and the light log reports exactly the build's samples per point

#### Scenario: Presets differ only in the light stage
- **WHEN** the three Town builds are expanded
- **THEN** their first two commands are identical, and their light stages differ only in `-shsamples` and `-super`

#### Scenario: Downloaded entries kept
- **WHEN** the overlay's menu is compared with the downloaded `Q3.game/default_build_menu.xml`
- **THEN** the only difference, apart from XML comments and line endings, is the three added builds and their separator
