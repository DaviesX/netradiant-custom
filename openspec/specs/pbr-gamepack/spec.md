# pbr-gamepack Specification

## Purpose
The `pbr` gamepack source: the overlay that gives the downloaded Quake III Arena pack its PBR game file, light definitions, build menu and documentation, and the parity criterion against the engine.

## Requirements

### Requirement: PBR gamepack layout
The repository's `pbr` gamepack source (`setup/data/gamepacks/pbr/`) SHALL be an overlay of the downloaded Quake III Arena pack. It SHALL contain `games/Q3.game`, `Q3.game/baseq3/_pbr_lights.ent`, `Q3.game/default_build_menu.xml` and a `README.md` at its root. It SHALL NOT contain a game of its own: no `games/pbr.game` and no `pbr.game/` directory. No `.game` file in the repository SHALL set `shaders="pbr"`.

#### Scenario: Gamepack installed
- **WHEN** the downloaded packs and then the `pbr` pack are installed, and the editor starts
- **THEN** the game selection dialog offers "Quake III Arena / Quake III: Team Arena" and no "PBR" entry

#### Scenario: Install leaves no PBR game
- **WHEN** `install-gamepack.sh setup/data/gamepacks/pbr <dest>` runs
- **THEN** it copies only `games/Q3.game` and the `Q3.game/` directory into `<dest>`

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

### Requirement: Engine parity criterion
The benchmark map in `content/benchmark/` SHALL have a documented parity view: camera position, angles, field of view, exposure, ambient and window size. The editor's lighting view from that position under `Q3.game` SHALL match the engine's direct-light-only render of the compiled map within a documented tolerance. The procedure and tolerance SHALL be recorded in the pack's root `README.md`.

#### Scenario: Parity check performed
- **WHEN** screenshots are taken from the documented view in the editor under `Q3.game` and in the engine
- **THEN** the README's procedure yields a pass

### Requirement: Shadow parameters documented in the gamepack README
The pack's root `README.md` SHALL document the shadow scheme so it can be compared against the engine: the caster culling mode, the normal-offset and slope-scaled bias constants, the PCF kernel, the cascade count, resolution, split lambda, texel snapping and near-plane padding, the spot atlas dimensions and per-light tile size, the area light's `_frustum` projection (field of view, its clamp, near and far planes, shared tile, NdotL toward the centre, receivers beside the frustum shadowed for area lights, lit for spots), and the editor's caster rule. It SHALL state which values match `sh-renderer` and which are editor-side choices.

#### Scenario: Reader can reproduce the scheme
- **WHEN** a reader follows the README's shadow section
- **THEN** every constant used by the editor's shadow path is listed with its value and its source

### Requirement: Area and proxy light model documented in the gamepack README
The pack's root `README.md` SHALL document:
- **the `light_area` model:** its keys, the `AngleVectors` basis with roll, radiance units, flux π × L × A, E ≈ L × A / d² on the normal far from the rectangle (and the exact form factor close to it, for example E = 0.752 for the defaults at 64 units), the cutoff from the centre and its clamp to the half-diagonal, one-sided emission, and the shading formula with the normalised form factor for diffuse (no 1/π, no Fresnel weight) and LTC GGX with the tables' Fresnel fit for specular;
- **the LTC tables:** the upstream repository (`github.com/selfshadow/ltc_code`), file (`fit/results/ltc.js`) and commit, the upstream licence's name and the paper citation it asks for, and that the licence's full notice installs beside the binary as `gl/ltc_ggx.LICENSE.txt` (the README is not installed, so it is not the shipped notice), the file format and layout of `setup/data/tools/gl/ltc_ggx.bin` (header, the two tables and their components), the indexing (u = perceptual roughness, v = √(1 − N·V), scale 63/64 and bias 0.5/64), its SHA-256, that `renderer_sh` reads the same file unchanged, and the diffuse-only fallback;
- **the derivation:** L̄ = `qer_pbr_emissiveStrength` × emissive colour (white without `rgbGen const`) × mean linear texel over the face's UV footprint, its split into `intensity` and `_color`, the created area light's `radius`, and the proxy flux k × π × Σ L̄ × A with k = 1 for opaque and k = 2 for all-additive emitters;
- **the proxies:** which selections make a hemisphere `light_spot` (`cone 90`, `cone_inner 89`, `_shadows 0`) and which a `light`, and their on-axis residual: about LA/(2d²) against the emitter's LA/d² for both kinds, with the right total flux, and a flat spot intensity off the axis where a Lambertian face falls as cos θ;
- **the three commands, the attachment test Re-derive uses, and their refusals;**
- **the difference from the bake:** `-shbake` and the placeholder `-light` ignore `light_area` entities and `light`, `light_spot` and `light_sun` entities whose `_proxy` starts with `1`, and sh-baker's area lights are two-sided while `light_area` is one-sided;
- **emitter drawing:** how the preview draws emitters it does not light.

#### Scenario: Reader can reproduce the area light
- **WHEN** a reader implements an area light from the README alone
- **THEN** every formula, constant, basis convention and table detail the editor uses is stated, with its source
