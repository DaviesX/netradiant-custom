# Spec Delta

## MODIFIED Requirements

### Requirement: Log failure conditions
A run SHALL fail when:
- its console log contains `R_FindImageFile could not find` or `Couldn't find image file for shader`, unless an allowlist entry anchored at both ends names that exact line as a literal string (no wildcards) (only for an image the engine itself looks up and replaces with a built-in fallback, never for an image the content references);
- its log contains a line with `WARNING`, or a game line `<classname> doesn't have a spawn function`, that matches no entry of the reviewed allowlist (`tools/stockcheck/allowlist.txt`: one regular expression per line, each with a comment saying why it is benign);
- the engine exits abnormally or does not quit within a timeout;
- a screenshot is missing.

Only shaders the map uses are parsed by the engine, so only they are checked.

The allowlist SHALL start with three entries, for `light_spot doesn't have a spawn function`, `light_sun doesn't have a spawn function` and `light_area doesn't have a spawn function`: the stock game prints them once per such entity, and they are harmless because the editor's light entities are only kept in the BSP for `renderer_sh`. Further entries need a reviewed comment. A general `WARNING` entry SHALL NOT excuse a missing-image line: only an entry that spells out the missing-image message and matches the whole line does.

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
- **WHEN** the benchmark's `light_spot`, `light_sun` and `light_area` entities make the game print their spawn-function lines
- **THEN** the run does not fail on them

#### Scenario: Unknown entity class
- **WHEN** a scratch copy of the map contains an entity with classname `func_notreal`
- **THEN** the run fails on its spawn-function line
