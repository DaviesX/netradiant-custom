# Spec Delta

## MODIFIED Requirements

### Requirement: Benchmark materials are vanilla shaders with PBR keywords
Every benchmark material SHALL be a `scripts/bench.shader` entry using only vanilla Quake 3 shader keywords plus top-level `qer_pbr_*`. Normal maps SHALL be declared with `qer_pbr_normal`. Emissive SHALL be an additive stage, with `qer_pbr_emissiveStrength` where the `.mtr` had `emissivestrength`. The shaders SHALL contain no `q3map_sun` and no `q3map_surfacelight`. No image SHALL be named `<base>_n`, `<base>_nh` or `<base>_s`.

#### Scenario: Every feature present
- **WHEN** the shader file is read
- **THEN** it exercises:
  - a base colour with normal, metallic-roughness and occlusion;
  - a rough dielectric;
  - a metal;
  - an emissive with `rgbGen const`;
  - an alpha-tested surface with `cull none`;
  - a blended surface;
  - a double-sided opaque surface;
  - sky, fog, caulk and clip;
  - a normal-mapped opaque model surface;
  - an alpha-tested model surface with `cull none` that casts through its alpha (`surfaceparm trans` with `surfaceparm alphashadow`);
  - an alpha-tested model surface with `cull none` that casts no shadow (`surfaceparm trans` without `surfaceparm alphashadow`).

#### Scenario: No rend2 keywords
- **WHEN** the editor loads the shader file
- **THEN** it prints no rend2-keyword warning

#### Scenario: Reserved suffix scan
- **WHEN** the texture directory is listed
- **THEN** no file ends in `_n`, `_nh` or `_s` before its extension

### Requirement: Benchmark lights are the editor's physical lights
The benchmark SHALL light the scene with the editor's physical light entities:
- one `light_sun`;
- at least one point light (`light`, with `intensity` and `radius`);
- at least two `light_spot` entities, one aimed by `angles` and one by `target`;
- at least one `light_spot` setting `_shadows 0`.

The light keys and values SHALL be those recorded in `content/benchmark/README.md`, which the author tunes in the editor preview.

#### Scenario: Entity inventory
- **WHEN** the map's entities are listed
- **THEN** they contain one `light_sun`, at least one `light` and at least two `light_spot` entities whose keys and values match the README's light table, at least one spot sets `_shadows 0`, and every `target` resolves

#### Scenario: Lights preview under Q3.game
- **WHEN** the map is opened in lighting mode under `Q3.game` with `fs_game benchmark`
- **THEN** the sun, every point light and every spot light the scene

### Requirement: Benchmark occlusion cases
The benchmark SHALL keep these occlusion cases:
- a free-standing pillar under a spot light;
- a patch arch casting under the sun;
- two interiors sealed from the sun away from their doorways;
- a non-casting spot light.

#### Scenario: Interior is dark in the preview
- **WHEN** the map is viewed in lighting mode with shadows on under `Q3.game`
- **THEN** the interior floors away from the doorways receive no sunlight

#### Scenario: Arch shadow in the preview
- **WHEN** the map is viewed in lighting mode with shadows on under `Q3.game`
- **THEN** the street under the patch arch shows the arch's sun shadow

### Requirement: Documented views
`maps/bench.cameras` SHALL list named view requests, one per line: name, player origin x y z, yaw. It SHALL include `sign`, from the `info_player_start` origin facing the emissive sign, and the views together SHALL show the fence, the glass, both trees, grass on the lawn and a tree canopy's shadow on the ground. The authoritative position of a view is the eye position recorded by the stock check's `opengl1` run, and editor captures of a view SHALL use that recorded position and yaw from the same check run.

#### Scenario: Shared views
- **WHEN** the editor capture of `sign` is taken at the position the stock check recorded
- **THEN** both images frame the same scene within the difference in field of view

#### Scenario: Foliage view
- **WHEN** the editor capture of the view that frames the foliage is taken in lighting mode with shadows on, at the position the stock check recorded
- **THEN** it shows both trees, grass and a canopy shadow on the ground, and the stock screenshots of that view show the same trees and grass

### Requirement: Stock screenshots reviewed by the author
The author SHALL review the stock check's screenshots of every view on both renderers for content errors: every shader renders with its own image (no missing-texture or default image), the fence, the leaves and the grass are masked, the trees and grass stand upright with their cards visible from both sides, the glass is blended and the sky draws. Lighting SHALL NOT be judged, because the lightmap is a placeholder. The acceptance date SHALL be recorded in `content/benchmark/README.md`.

#### Scenario: Acceptance
- **WHEN** the stock check's screenshots are shown to the author
- **THEN** the author accepts them, or the content is fixed and the check is re-run

## ADDED Requirements

### Requirement: Benchmark vegetation is model content
The benchmark SHALL contain trees and grass as `misc_model` entities that reference meshes under `models/bench/`. They SHALL need no `-keepmodels` and no game code, so that q3map2 merges their triangles into the BSP and stock Quake 3 never spawns them. Each tree SHALL have an opaque trunk that blocks the player and a canopy of alpha-tested leaf cards. Grass SHALL be clumps of static crossed alpha-tested quads that do not block the player.

#### Scenario: Merged into the BSP
- **WHEN** the stock check compiles and packs the map
- **THEN** the pk3 contains no model file and the BSP's entity lump contains no `misc_model`, yet both renderers draw the trees and the grass

#### Scenario: Previewed in the editor
- **WHEN** the map is opened in lighting mode with shadows on under `Q3.game` with `fs_game benchmark`
- **THEN** the trees and the grass are drawn upright where they stand in the engine, lit by the scene's lights, with leaf and grass cards masked by their alpha

#### Scenario: Leaves cast through their alpha, grass casts nothing
- **WHEN** the sun shines on a tree standing in grass, in lighting mode with shadows on
- **THEN** the canopy's shadow on the ground is broken up by the leaf cards' alpha, the trunk casts a solid shadow, and the grass casts no shadow

#### Scenario: Collision
- **WHEN** the player walks into a tree trunk and then through a grass clump in the engine
- **THEN** the trunk stops the player and the grass does not

### Requirement: Vegetation is lightmapped
Every vegetation shader SHALL have a `$lightmap` stage, and q3map2 SHALL give its model surfaces lightmaps rather than vertex lighting, so that the editor preview lights them and later lightmap bakes cover them.

#### Scenario: Lit in the preview
- **WHEN** a tree or grass clump stands inside a light's radius in lighting mode
- **THEN** it receives that light's pass, like a brush face

#### Scenario: Lightmapped in the BSP
- **WHEN** the compiled BSP's draw surfaces for the vegetation shaders are inspected
- **THEN** each has a lightmap index rather than being vertex-lit only

### Requirement: Vegetation assets are generated from a committed script
The vegetation's textures and meshes SHALL be written by one committed generator script that needs only Python 3 with Pillow, takes no network or external art, and uses a fixed seed. Its outputs SHALL be committed, so the editor and the stock check never need to run it. Its images SHALL follow the benchmark's image rules: stored bottom-up, and no reserved suffix.

#### Scenario: Regeneration
- **WHEN** the script is re-run with the Pillow version recorded in it
- **THEN** it rewrites the committed textures and meshes without changing them

#### Scenario: Image orientation
- **WHEN** the generated TGA images are read
- **THEN** each is stored bottom-up
