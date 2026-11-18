# Spec Delta

## Purpose

The `-shbake` stage of q3map2 `-light`. It bakes the editor's physical lights with sh-baker on q3map2's own luxels, grid points and vertices. From that bake it writes the BSP lightmap pages, light grid and vertex colours for the stock renderers, and the SH side file for `renderer_sh`.

## ADDED Requirements

### Requirement: Stage option and build flag
`q3map2 -light -shbake <map>` SHALL run the stage in place of `-light`'s lighting. A q3map2 built without `SH_BAKER=1` SHALL stop with a message telling the user to rebuild with `SH_BAKER=1`, before loading the BSP. Without `-shbake`, q3map2's output SHALL be byte-identical to its output before this change, whichever way it was built.

#### Scenario: Built without the flag
- **WHEN** a q3map2 built without `SH_BAKER=1` is run with `-light -shbake` on the benchmark
- **THEN** it exits non-zero with a message naming `SH_BAKER=1`, and `bench.bsp` is unchanged

#### Scenario: Placeholder path unchanged
- **WHEN** the benchmark is compiled single-threaded with `-light -patchshadows` by q3map2 built before and after this change, with and without `SH_BAKER=1`
- **THEN** the BSPs are byte-identical apart from the q3map2 version stamp

### Requirement: Storage changes refused
With `-shbake`, q3map2 SHALL stop before baking, with a message naming the cause, if lightmaps would be stored in any way other than BSP-internal pages of the game's size with no deluxe pages. The causes are:
- `-deluxe`, `-deluxemode` or `-debugdeluxe`;
- `-external` or `-lightmapdir`;
- a page size set by `-lightmapsize` or `-extlmhacksize`, or by a used shader's `q3map_lightmapSize`, that isn't the game's size;
- `-trisoup`, which makes planar surfaces vertex-lit;
- `-bounce` above 0, `-approx` above 0, `-fill`, `-fillpink` or `-nolm`;
- a debug mode that writes debug colours into luxels: `-debug`, `-debugsurfaces`, `-debugaxis`, `-debugcluster`, `-debugorigin`, `-debugnormals`, `-border` or `-dirtdebug`.

#### Scenario: Deluxe refused
- **WHEN** `-light -shbake -deluxe` is run on the benchmark
- **THEN** q3map2 exits non-zero with a message naming `-deluxe`, before baking

#### Scenario: Shader page size refused
- **WHEN** a map uses a shader with `q3map_lightmapSize 256 256` and is run with `-light -shbake`
- **THEN** q3map2 exits non-zero with a message naming the shader and its size

### Requirement: Honoured and ignored options
With `-shbake`, q3map2 SHALL honour:
- the options that shape luxel mapping or page packing: `-super`, `-samplesize`, `-minsamplesize`, `-extravisnudge`, `-rawlightmapsizelimit`, `-nocollapse`, and the lightmap search and allocation options;
- `-shade` and `-shadeangle`, which smooth the normals the bake uses;
- `-nogrid` and `-novertex`, which leave out the grid and vertex points;
- `-export`, `-threads`, and options unrelated to lighting.

Every other `-light` option, which tunes only the replaced lighting or its byte conversion, SHALL have no effect, and q3map2 SHALL print one warning naming those given.

#### Scenario: Ignored options warned once
- **WHEN** `-light -shbake -samples 3 -patchshadows -gridscale 2` is run on the benchmark single-threaded
- **THEN** one warning names `-samples`, `-patchshadows` and `-gridscale`, and the BSP is byte-identical to the one from `-light -shbake` single-threaded

### Requirement: Bake quality options
With `-shbake`, `-shsamples N` SHALL set the rays per point (default 128), and `-shbounces N` the maximum path depth (default 5). A value below 1 SHALL stop q3map2 with a message naming the option.

#### Scenario: Samples option
- **WHEN** the benchmark is baked with `-shbake -shsamples 16`, and then with `-shsamples 0`
- **THEN** the first log reports 16 samples per point, and the second run exits non-zero naming `-shsamples`

### Requirement: Light entities read with the physical model
The stage SHALL read `light`, `light_spot` and `light_sun` as the editor does (`plugins/entity/light.cpp`, `colour.h`):
- **Point:** I = `intensity`/(4π), default flux 50000; cut off beyond `radius` (default 256; unparsable or ≤ 0 gives 256); never casts.
- **Spot:** I = `intensity`/(2π(1 − cos `cone`)). `cone` defaults to 45° (unparsable or ≤ 0 gives 45). `cone_inner` defaults to 30° (unparsable or < 0 gives 30). Both are clamped to 89.9°, `cone_inner` to at most `cone`. Smoothstep falloff; `radius` as for point; casts unless `_shadows` is 0.
- **Sun:** irradiance `intensity` (default 3); casts unless `_shadows` is 0; only the first in map order is used.
- **Direction:** spots aim at their `target` entity. Otherwise spots and suns use `angles` (pitch yaw roll, positive pitch looks down, default 0 0 0, which is +X). Failing that they use `angle` (yaw only; −1 is up, −2 is down).
- **Colour:** `_color` is linear RGB, divided by its largest component; all zero gives white.

#### Scenario: Point light irradiance
- **WHEN** a map holds one `light` with `intensity` 50000 and `radius` 256, 64 units above the centre of a large white floor with an open sky, and is baked at `-super 1`
- **THEN** the floor luxel nearest below the light has the lightmap bytes of irradiance 50000/(4π·64²), within one byte, and luxels farther than 256 units from the light get no direct light from it

#### Scenario: Colour normalised
- **WHEN** two otherwise identical maps light a floor with `_color` `0.5 0.5 0.5` and with `_color` `1 1 1`
- **THEN** their lightmaps are byte-identical

#### Scenario: Second sun ignored
- **WHEN** a map holds two `light_sun` entities
- **THEN** only the first in map order lights the bake, and q3map2 prints a warning naming the second

### Requirement: Unsupported light inputs warned
The stage SHALL NOT read q3map2-only light inputs. It SHALL print one warning per kind found, naming it and one entity or shader that uses it. These are:
- a light's `spawnflags` or `fade`;
- worldspawn `_minlight`, `_minvertexlight`, `_mingridlight`, `_maxlight` and `_ambient`, and `_ambient` on any other entity;
- the shader keywords `q3map_sun`, `q3map_sunExt`, `q3map_skyLight` and `q3map_surfaceLight`;
- entities whose classname starts with `light` but isn't one of the three.

#### Scenario: Surfacelight warned
- **WHEN** a map uses a shader with `q3map_surfaceLight 1000`
- **THEN** the bake adds no light for it, and one warning names `q3map_surfaceLight` and the shader

#### Scenario: Minlight has no effect
- **WHEN** the benchmark's worldspawn is given `_minlight 64` in a scratch copy and baked
- **THEN** one warning names `_minlight`, and the lightmap is byte-identical to the bake without it

### Requirement: Draw-surface casters follow the editor's rule
A draw surface SHALL occlude in the bake only if its shader casts under the editor's caster rule. It never casts if the shader has `surfaceparm` sky, fog, water, slime, lava, playerclip, botclip, areaportal, noshadows, trigger or hint; non-solid nodraw; or trans without alphashadow. An alpha-tested surface SHALL cast through its base stage's `alphaFunc`. Copies q3map2 makes for `_skybox` portals SHALL be left out. Every other draw surface with a lightmap SHALL receive.

#### Scenario: Sky does not block the sun
- **WHEN** a room is closed above by a sky-shader ceiling and lit by a `light_sun` pointing down through it
- **THEN** the floor receives the sun's irradiance

#### Scenario: Glass passes light
- **WHEN** a pane with `surfaceparm trans` and no `alphashadow` stands between a casting spot and a wall
- **THEN** the wall behind the pane receives the spot's direct light

### Requirement: Solid hulls block light
The stage SHALL build one closed hull per brush of every model whose content shader is solid and whose non-bevel sides all use casting, non-alpha-tested shaders. Brushes of a model with an origin SHALL be moved by it, as its draw surfaces are. For the bake, each hull plane SHALL be moved inward by a fixed epsilon, so the visible draw surface is hit first. Hulls SHALL block light without shading or receiving. Patches and models SHALL have no hull.

#### Scenario: Light does not leak through a seam
- **WHEN** two caulk-backed wall brushes meet at a T-junction and a spot shines at their outer side
- **THEN** no luxel on the inner side receives the spot's direct light

#### Scenario: Sky brush makes no hull
- **WHEN** a brush has a sky inner face and caulk on its other faces
- **THEN** it makes no hull, and the sun reaches the floor below it

### Requirement: Materials from shader text
Each draw surface's bake material SHALL come from its shader as the editor derives it:
- the base colour is the stage the lightmap multiplies, else the first non-lightmap stage with a map, times `qer_pbr_baseColorFactor`;
- alpha is the base stage's `alphaFunc` test of its image, and fully opaque without one;
- the emissive is the first additive stage that is not the lightmap, a white image or an environment map, coloured by its `rgbGen const`, with strength `qer_pbr_emissiveStrength`.

A shader SHALL emit without occluding only if every stage is additive. An image that can't be loaded SHALL fall back to white with a warning. The bake's shading is Lambert: normal, metallic-roughness and occlusion maps SHALL NOT change it.

#### Scenario: Emissive sign lights the wall
- **WHEN** the benchmark's neon-sign shader, with its additive stage and `qer_pbr_emissiveStrength`, is baked
- **THEN** the sign is an area light in the bake, and the wall luxels next to it are brighter than with the sign's emissive strength set to 0

#### Scenario: Opaque sign still occludes
- **WHEN** a shader has an opaque lightmapped base stage plus an additive glow stage, and a spot shines at it from in front
- **THEN** the wall behind it gets no direct light from the spot

### Requirement: Points baked in one call
The stage SHALL bake all of these as points in one sh-baker call:
- every super-luxel that `MapRawLightmap` mapped and didn't mark occluded;
- each light-grid point, twice, with opposite normals, unless `-nogrid` is given;
- every draw vertex of a surface whose shader doesn't have `q3map_noVertexLight`, unless `-novertex` is given.

It SHALL print the point counts and the bake's progress through q3map2's console. The output SHALL NOT depend on the thread count.

#### Scenario: Counts printed
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** the log names the number of luxel, grid and vertex points, and progress reaches 100%

#### Scenario: Thread count
- **WHEN** the benchmark is baked with `-threads 1` and with the default thread count
- **THEN** the BSPs and side files are byte-identical

### Requirement: Unbaked luxels flooded
Super-luxels the stage didn't bake, unmapped or marked occluded, SHALL take the average of their baked 3×3 neighbours, in colour, SH and visibility together, as `-light` floods them. Luxels still without a value SHALL be stored as 0.

#### Scenario: No black seam
- **WHEN** the benchmark is baked
- **THEN** no luxel along the wall–floor seams of the lit courtyard is black where its neighbours are lit

### Requirement: SH follows lightmap storage
SH page *i* of the side file SHALL be BSP lightmap page *i*, with the same size. Each side-file luxel SHALL belong to the same surface point as the BSP luxel at that position. SH and visibility SHALL be averaged and copied with the same weights as the colour in every storage step after the bake, including the neighbour fill of luxels with no sample. With `-shbake`, lightmaps SHALL NOT be collapsed into shared copies, stamped as solid colour, or approximated by vertex colour.

#### Scenario: Pages correspond
- **WHEN** the benchmark is baked and its side file read back
- **THEN** the side file has as many pages as the BSP has lightmap pages, each 128×128

#### Scenario: Indirect luxels match the lightmap
- **WHEN** a planar luxel outside every punctual light's reach is baked at `-super 1`, and its byte is compared with its side-file luxel
- **THEN** the lightmap byte equals the encoding of the irradiance convolved from the side-file SH at the plane normal, within one byte

### Requirement: Lightmap irradiance
The irradiance written for a luxel or vertex SHALL be the exact punctual-direct irradiance at its normal, plus the irradiance convolved from the rest of the bake's SH at that normal (Â₀ = π, Â₁ = 2π/3, Â₂ = π/4). With `-super N`, the luxel's value SHALL be the average of its super-luxels' irradiance.

#### Scenario: No SH ringing on direct light
- **WHEN** a luxel of a large flat floor, the only geometry in the map, is lit only by a sun at 60° from its normal
- **THEN** its irradiance is the sun's irradiance × 0.5, within 1e-4 relative

### Requirement: Lightmap encoding
Lightmap bytes SHALL encode irradiance E as a white Lambert surface would show in the editor preview's tonemap at exposure 1, calibrated to opengl1 at default cvars in a window. Per channel: d = E/π; t = Uncharted2(d)/Uncharted2(11.2), clamped to [0, 1], with the preview's Hable constants; s = sRGB-encode(t); byte = round(63.75 × s). The encoding SHALL be documented in the gamepack README, with opengl2's known differences.

#### Scenario: Midtones
- **WHEN** a luxel's irradiance is π in every channel, and another's is π/2
- **THEN** their lightmap bytes are 37, 37, 37 and 29, 29, 29

#### Scenario: White point
- **WHEN** a luxel's irradiance is 11.2 × π or more in every channel
- **THEN** its lightmap bytes are 64, 64, 64, and never above 64

### Requirement: Light grid from the bake
Each grid point SHALL sum its two hemisphere bakes into full-sphere SH. Its direction d SHALL be the dominant L1 direction of the punctual-direct SH, or of the rest when there is no punctual light. Its ambient A is the mean irradiance over all normals of the non-punctual SH, and its directed D is the punctual SH's irradiance along d. A model lit by the grid in opengl1 SHALL get encode(A) facing away from d and encode(A + D) facing d. Two exceptions: values below the engine's fixed minimum ambient get that minimum, and ambient above the engine's reachable maximum is capped there. Grid points q3map2 finds in solid SHALL be black.

#### Scenario: Single sun
- **WHEN** an open map lit only by one `light_sun` over a grey ground plane is baked
- **THEN** an unoccluded grid point's direction points towards the sun within 2°, and its directed colour encodes the sun's irradiance, with the ground's bounced light only in its ambient

### Requirement: Vertex colours from the bake
Every baked draw vertex's colour SHALL be the lightmap encoding of its irradiance at the vertex normal. Vertex alpha, and the colours of `q3map_noVertexLight` surfaces, SHALL be unchanged.

#### Scenario: Vertex-lit model lit
- **WHEN** a vertex-lit model is placed in a spot light's cone and baked
- **THEN** its vertices facing the spot have non-zero colours, and those facing away from every light get only indirect light

### Requirement: Side file output
The stage SHALL write the side file to `maps/<name>/<name>.shsf` beside the BSP, through the q3map2 extension writer. It holds the SH pages and the un-shrunk hull mesh in BSP units, with hull triangles facing outward and q3map2's winding. A writer failure SHALL stop q3map2 with the writer's message. The stage SHALL NOT write any `lm_*.hdr` file.

#### Scenario: Side file written
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** `maps/bench/bench.shsf` exists and reads back with the extension reader, and no `lm_*.hdr` file exists under `maps/`

### Requirement: Side-file contents selection
By default the side file SHALL hold the full bake, marked *full*: the punctual-direct SH plus the rest. With `-shindirect` it SHALL hold only the rest, marked *indirect only*. Emissive direct light is kept in both. The BSP lightmap, grid and vertex colours SHALL be the same either way.

#### Scenario: Indirect only
- **WHEN** the benchmark is baked with and without `-shindirect`, single-threaded
- **THEN** the BSPs are byte-identical, the side files' contents fields read *indirect only* and *full*, and a luxel in a spot's direct beam has a lower L0 in the indirect-only file

### Requirement: Optional L0 dump
With `-shdumpl0`, the stage SHALL also write each side-file page's L0 as `maps/<name>/<name>_l0_<page>.hdr`, with the page number as four digits. These files SHALL NOT be packed by the stock check.

#### Scenario: Dump written
- **WHEN** the benchmark is baked with `-shbake -shdumpl0`
- **THEN** one `.hdr` file exists per side-file page, named `bench_l0_0000.hdr` onwards
