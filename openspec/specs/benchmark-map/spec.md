# benchmark-map Specification

## Purpose
The Quake 3 benchmark map: stock content that exercises every material feature, light type and occlusion case the pipeline must preview, compile and render. It is the parity and regression asset for every later stage.

## Requirements

### Requirement: Benchmark lives in the benchmark mod directory
The benchmark SHALL be stored in the repository under `content/benchmark/`, laid out as a Quake 3 mod directory (`maps/`, `scripts/`, `textures/`). It SHALL be usable by the editor under `Q3.game` with `fs_game benchmark` and by ioquake3 with `+set fs_game benchmark`. It SHALL need no game code beyond baseq3's, and SHALL contain no test-only content.

#### Scenario: Editor opens it
- **WHEN** the editor runs under `Q3.game` with the engine path set to the Quake 3 install and the `benchmark` mod selected
- **THEN** `maps/bench.map` opens and every `textures/bench/*` shader appears in the texture browser

#### Scenario: Engine loads it
- **WHEN** ioquake3 runs with `+set fs_game benchmark +devmap bench` on the packed pk3
- **THEN** the map loads and the player spawns

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
  - sky, fog, caulk and clip.

#### Scenario: No rend2 keywords
- **WHEN** the editor loads the shader file
- **THEN** it prints no rend2-keyword warning

#### Scenario: Reserved suffix scan
- **WHEN** the texture directory is listed
- **THEN** no file ends in `_n`, `_nh` or `_s` before its extension

### Requirement: Benchmark lights are the editor's physical lights
The benchmark SHALL light the scene with the editor's physical light entities, copied unchanged from the `pbr.game` bench:
- one `light_sun`;
- at least two point lights (`light`, with `intensity` and `radius`);
- at least two `light_spot` entities, one aimed by `angles` and one by `target`, the latter setting `_shadows 0`.

The light keys and values SHALL equal those of the `pbr.game` bench.

#### Scenario: Entity inventory
- **WHEN** the map's entities are listed
- **THEN** they contain one `light_sun`, two `light` and two `light_spot` entities whose keys and values match the `pbr.game` bench, and every `target` resolves

#### Scenario: Lights preview under Q3.game
- **WHEN** the map is opened in lighting mode under `Q3.game` with `fs_game benchmark`
- **THEN** the sun, both point lights and both spots light the scene

### Requirement: Benchmark occlusion cases
The benchmark SHALL keep the occlusion cases of the `pbr.game` bench:
- a free-standing pillar under a spot light;
- a patch arch casting under the sun;
- two interiors sealed from the sun away from their doorways;
- the non-casting spot aimed at the emissive sign.

#### Scenario: Interior is dark in the preview
- **WHEN** the map is viewed in lighting mode with shadows on under `Q3.game`
- **THEN** the interior floors away from the doorways receive no sunlight

#### Scenario: Arch shadow in the preview
- **WHEN** the map is viewed in lighting mode with shadows on under `Q3.game`
- **THEN** the street under the patch arch shows the arch's sun shadow

### Requirement: Benchmark compile flags
The map SHALL compile with the bundled q3map2 using `-meta -keeplights`, then `-vis -saveprt`, then `-light -patchshadows`, with no leak and no `Unknown q3map_* directive` warning. The `-light` lightmap is a placeholder that is not required to resemble the preview.

#### Scenario: Compile
- **WHEN** the stock check compiles `bench.map`
- **THEN** q3map2 reports no leak and no unknown directive, and the BSP's entity lump retains the light entities

### Requirement: Documented views
`maps/bench.cameras` SHALL list named view requests, one per line: name, player origin x y z, yaw. It SHALL include `sign`, from the `info_player_start` origin facing the emissive sign, and the views together SHALL show the fence and the glass. The authoritative position of a view is the eye position recorded by the stock check's `opengl1` run, and editor captures of a view SHALL use that recorded position and yaw from the same check run.

#### Scenario: Shared views
- **WHEN** the editor capture of `sign` is taken at the position the stock check recorded
- **THEN** both images frame the same scene within the difference in field of view

### Requirement: Single pk3 packaging
The benchmark SHALL package as one `benchmark.pk3`. It contains the BSP and its lightmaps, the levelshot `levelshots/bench.jpg`, `scripts/*.shader` and `shaderlist.txt`, and every image under `textures/`, the PBR maps included. The levelshot is the loading-screen image; without it the engine prints `Couldn't find image file for shader levelshots/bench.tga`. It contains no other files (no `.map`, `.prt`, editor metadata or `*.import` files).

#### Scenario: Pack contents
- **WHEN** `benchmark.pk3` is listed
- **THEN** every entry is a BSP, a lightmap image, the levelshot, a shader script, the shader list or an image under `textures/`, and every image a stage references is present

### Requirement: Stock screenshots reviewed by the author
The author SHALL review the stock check's screenshots of every view on both renderers for content errors: every shader renders with its own image (no missing-texture or default image), the fence is masked, the glass is blended and the sky draws. Lighting SHALL NOT be judged, because the lightmap is a placeholder. The acceptance date SHALL be recorded in `content/benchmark/README.md`.

#### Scenario: Acceptance
- **WHEN** the stock check's screenshots are shown to the author
- **THEN** the author accepts them, or the content is fixed and the check is re-run
