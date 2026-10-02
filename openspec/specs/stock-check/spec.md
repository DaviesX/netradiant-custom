# stock-check Specification

## Purpose
An automated check that a map compiles and its pk3 runs cleanly on the stock Quake 3 target (ioquake3 with `cl_renderer opengl1` and `opengl2`), the lvlworld fallback. It is the arbiter of the compatibility contract and part of every task's exit criterion.

## Requirements

### Requirement: Single command over a mod
The stock check SHALL be one command taking a mod directory and a map name. It SHALL compile the map, pack the pk3, run the engine matrix, and exit 0 only if every step passes. The engine binary, q3map2 and Quake 3 base path SHALL be overridable by options, with defaults documented in the tool's README.

#### Scenario: Benchmark passes
- **WHEN** `tools/stockcheck/stockcheck.py content/benchmark bench` is run on the accepted benchmark
- **THEN** it prints a table with every row PASS and exits 0

#### Scenario: Missing engine
- **WHEN** the configured engine binary does not exist
- **THEN** the check exits non-zero with a message naming the path, before compiling

### Requirement: Isolated compile
The check SHALL copy the given mod directory into a fresh temporary basepath next to links to `baseq3/pak0–8.pk3`, and compile there with a fresh temporary homepath. It SHALL use `-meta -keeplights`, then `-vis -saveprt`, then `-light -patchshadows`. It SHALL fail when q3map2 exits non-zero, reports a leak (`leaked`, case-insensitive), or prints `Unknown q3map_* directive`. The compile SHALL read only the given mod directory and the stock paks, never `~/.q3a`.

#### Scenario: Unknown directive
- **WHEN** a shader used by the map in a scratch copy of the mod contains `q3map_notARealDirective`
- **THEN** the compile step fails and the offending q3map2 line is shown

### Requirement: Run matrix
The check SHALL run the engine twice: `cl_renderer opengl1` and `cl_renderer opengl2`. All other cvars SHALL be at their defaults, except `developer 1`, `logfile 2`, a fixed windowed 1280×720 mode, `com_introPlayed 1` (no intro cinematic), and HUD, gun and console notify text off for screenshots.

#### Scenario: Two rows
- **WHEN** the check completes
- **THEN** the result table has a compile row and exactly two engine rows, labelled by renderer

### Requirement: Isolated run environment
Each run SHALL use a fresh temporary basepath containing only links to `baseq3/pak0–8.pk3` and the packed pk3 in the mod directory, plus a fresh temporary homepath. No loose file from the source mod directory SHALL be visible to the engine.

#### Scenario: Loose file cannot mask a missing image
- **WHEN** an image a used shader references exists loose in the source mod but was excluded from the pk3
- **THEN** the run reports the missing image and fails

### Requirement: Views, readback and screenshots
Each run SHALL load the map with `devmap`. For every view in the map's `.cameras` file it SHALL:
1. teleport with `setviewpos`;
2. wait for the player to settle;
3. print the eye position with `viewpos`;
4. take a screenshot.

It SHALL wait for the last screenshot to be written, then quit. The check SHALL record each view's reported eye position and yaw beside its screenshot, saved as PNG and named by renderer and view. A run SHALL fail when a view's recorded eye position is more than 256 units from its requested origin, which means the teleport was ignored. The `opengl1` run's recorded positions are the authoritative ones.

#### Scenario: Screenshot set
- **WHEN** the benchmark has four documented views
- **THEN** the output contains eight PNG screenshots and a views file with eight recorded eye positions

#### Scenario: Dropped teleport
- **WHEN** a view's `viewpos` line is missing, or reports a position more than 256 units from the requested origin
- **THEN** the run fails, naming the view

### Requirement: Log failure conditions
A run SHALL fail when:
- its console log contains `R_FindImageFile could not find` or `Couldn't find image file for shader`, unless an allowlist entry anchored at both ends names that exact line as a literal string (no wildcards) (only for an image the engine itself looks up and replaces with a built-in fallback, never for an image the content references);
- its log contains a line with `WARNING`, or a game line `<classname> doesn't have a spawn function`, that matches no entry of the reviewed allowlist (`tools/stockcheck/allowlist.txt`: one regular expression per line, each with a comment saying why it is benign);
- the engine exits abnormally or does not quit within a timeout;
- a screenshot is missing.

Only shaders the map uses are parsed by the engine, so only they are checked.

The allowlist SHALL start with two entries, for `light_spot doesn't have a spawn function` and `light_sun doesn't have a spawn function`: the stock game prints them once per such entity, and they are harmless because the editor's light entities are only kept in the BSP for `renderer_sh`. Further entries need a reviewed comment. A general `WARNING` entry SHALL NOT excuse a missing-image line: only an entry that spells out the missing-image message and matches the whole line does.

#### Scenario: rend2 keyword in a used shader
- **WHEN** a shader used by the map in a scratch copy contains `stage normalMap` inside a stage
- **THEN** the `opengl1` run fails with `renderergl1`'s unknown-parameter warning shown, while `opengl2`, which accepts the keyword, passes

#### Scenario: Allowlisted warning
- **WHEN** the log contains a warning matching an allowlist entry
- **THEN** the run does not fail on it

#### Scenario: Engine fallback image
- **WHEN** `renderergl2` prints `Couldn't find image file for shader gfx/2d/sunflare`, an image stock paks lack that it replaces with its flare image (`tr_shader.cpp:3862`)
- **THEN** the run does not fail on it, because the allowlist names that exact line

#### Scenario: Content image cannot be allowlisted by a warning entry
- **WHEN** a used shader's image is missing and the allowlist holds only `WARNING` entries
- **THEN** the run fails on the missing image

#### Scenario: Light entities in the map
- **WHEN** the benchmark's `light_spot` and `light_sun` entities make the game print their spawn-function lines
- **THEN** the run does not fail on them

#### Scenario: Unknown entity class
- **WHEN** a scratch copy of the map contains an entity with classname `func_notreal`
- **THEN** the run fails on its spawn-function line

### Requirement: Engine identity recorded
The check SHALL record, in its output directory and table header, the engine binary path and the git commit of the engine source tree it was built from. The default engine SHALL be the fork's build at `ioq3-custom/ioq3/build/Release`.

#### Scenario: Header
- **WHEN** the check runs with the default engine
- **THEN** the table header shows the fork's commit hash
