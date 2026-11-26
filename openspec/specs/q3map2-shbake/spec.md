# q3map2-shbake Specification

## Purpose
The `-shbake` stage of q3map2 `-light`. It bakes the editor's physical lights with sh-baker on q3map2's own luxels, grid points and vertices, in two independent bakes over the same points. The full bake writes the BSP lightmap pages, light grid and vertex colours for the stock renderers. The indirect bake writes the SH side file for `renderer_sh`, which computes direct light at runtime.

## Requirements

### Requirement: Stage option and build flag
`q3map2 -light -shbake <map>` SHALL run the stage in place of `-light`'s lighting. A q3map2 built without `SH_BAKER=1` SHALL stop with a message telling the user to rebuild with `SH_BAKER=1`, before loading the BSP. Without `-shbake`, q3map2's output SHALL be byte-identical to its output before this change, whichever way it was built.

#### Scenario: Built without the flag
- **WHEN** a q3map2 built without `SH_BAKER=1` is run with `-light -shbake` on the benchmark
- **THEN** it exits non-zero with a message naming `SH_BAKER=1`, and `bench.bsp` is unchanged

#### Scenario: Placeholder path unchanged
- **WHEN** the benchmark is compiled single-threaded with `-light -patchshadows` by q3map2 built before and after this change, with and without `SH_BAKER=1`
- **THEN** the BSPs are byte-identical apart from the q3map2 version stamp

### Requirement: Bake toggles
`-shbake` SHALL run two bakes, each one sh-baker call over the same point list:
- the *full* bake, with `indirect_only` false;
- the *indirect* bake, with `indirect_only` true. It drops all direct light at the baked point: punctual, emissive and direct sky light. Bounces still see every light and the sky.

`-shnofull` SHALL skip the full bake and `-shnoindirect` the indirect bake. Both may be given, and then nothing is baked.
- Without the full bake, every lightmap luxel a surface uses and every baked vertex colour SHALL be the neutral 64 64 64, a multiplier of 1.0 on opengl1 in a window. The same luxels SHALL be used, and the same left at 0 0 0, as with the full bake. Every grid point outside solid SHALL get ambient 63 63 63, directed 0 0 0 and direction bytes 0 0: the brightest ambient the engine reaches. The ambient is the grid fit's ambient for a target of byte 64, which takes the clamp of 63; the directed light is 0, not the 18 the fit would give for that target, so that no direction is favoured. Grid points in solid stay black.
- Without the indirect bake, no side file SHALL be written. Every `-shbake` run SHALL first remove an earlier run's side file and L0 dumps for the map, so only its own outputs remain.

The two bakes SHALL NOT change each other's output.

#### Scenario: No indirect bake
- **WHEN** the benchmark is baked single-threaded with `-shbake` and with `-shbake -shnoindirect`
- **THEN** the BSPs are byte-identical, and the second run leaves no `maps/bench/bench.shsf`, even in the directory where the first run wrote one

#### Scenario: No full bake
- **WHEN** the benchmark is baked single-threaded with `-shbake` and with `-shbake -shnofull`
- **THEN** the side files are byte-identical, the BSPs have the same lightmap page count and the same lightmap index and luxel position on every surface, and every luxel a surface uses in the second BSP's pages is 64 64 64

#### Scenario: No full bake with sky light
- **WHEN** the town is baked single-threaded with `-shbake` and with `-shbake -shnofull`
- **THEN** the side files are byte-identical

#### Scenario: Neither bake
- **WHEN** the benchmark is baked with `-shbake -shnofull -shnoindirect`
- **THEN** the log reports no bake, every luxel a surface uses in the lightmap pages is 64 64 64, and no side file is written

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
With `-shbake`, `-shsamples N` SHALL set the exact number of rays per point (default 128), and `-shbounces N` the maximum path depth (default 5), for both bakes. q3map2 SHALL pass sh-baker a `confidence_threshold` of 0, so no point stops early. A value below 1 SHALL stop q3map2 with a message naming the option.

#### Scenario: Samples option
- **WHEN** the benchmark is baked with `-shbake -shsamples 16`, and then with `-shsamples 0`
- **THEN** the first log reports exactly 16 samples per point, and the second run exits non-zero naming `-shsamples`

#### Scenario: Exact sample count
- **WHEN** a black floor in a sky shell, partly shaded by a black wall and lit by one point light, is baked single-threaded with `-shsamples 128` and again with `-shsamples 64`
- **THEN** the two side files differ in the visibility of some partly shaded luxels, which they could not if every point stopped at 64 samples (each point's first 64 samples are the same in both bakes, since it is seeded by its index)

### Requirement: Light entities read with the physical model
The stage SHALL read `light`, `light_spot` and `light_sun` as the editor does (`plugins/entity/light.cpp`, `colour.h`):
- **Point:** I = `intensity`/(4π), default flux 50000; cut off beyond `radius` (default 256; unparsable or ≤ 0 gives 256), through sh-baker's `Light::range`.
- **Spot:** I = `intensity`/(2π(1 − cos `cone`)). `cone` defaults to 45° (unparsable or ≤ 0 gives 45). `cone_inner` defaults to 30° (unparsable or < 0 gives 30). Both are clamped to 89.9°, `cone_inner` to at most `cone`. Smoothstep falloff (`ConeFalloff::kSmoothstep`, a hard edge when the cones are equal); `radius` as for point.
- **Sun:** irradiance `intensity` (default 3); only the first in map order is used.
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

### Requirement: Every light shadowed in the bake
Both bakes SHALL shadow every light, point lights and lights with `_shadows 0` included, because sh-baker has no per-light shadow switch. `_shadows` MAY still be parsed but SHALL NOT change the bake, and no warning SHALL be printed for it. The editor preview and `renderer_sh`'s runtime direct light keep the caster rule (point lights never cast; `_shadows 0` turns a spot or sun off). This difference reaches the stock lightmap's direct light and, slightly, the bounced light in both bakes; A9 judges it.

#### Scenario: Point light shadowed
- **WHEN** an opaque pillar stands between a `light` and a floor, inside its `radius`
- **THEN** the floor luxels in the pillar's shadow get no direct light from it in the full bake

#### Scenario: Shadows key has no effect
- **WHEN** two otherwise identical maps light a floor past a pillar with a `light_spot` with `_shadows 0` and with `_shadows 1`
- **THEN** their lightmap, light-grid and draw-vertex lumps and their side files are byte-identical (the entity lumps differ by the key)

### Requirement: Unsupported light inputs warned
The stage SHALL NOT read q3map2-only light inputs. It SHALL print one warning per kind found, naming it and one entity or shader that uses it. These are:
- a light's `spawnflags` or `fade`;
- worldspawn `_minlight`, `_minvertexlight`, `_mingridlight`, `_maxlight` and `_ambient`, and `_ambient` on any other entity;
- the shader keywords `q3map_sun`, `q3map_sunExt`, `q3map_skyLight` and `q3map_surfaceLight`;
- entities whose classname starts with `light` but isn't one of the three or `light_area`.

#### Scenario: Surfacelight warned
- **WHEN** a map uses a shader with `q3map_surfaceLight 1000`
- **THEN** the bake adds no light for it, and one warning names `q3map_surfaceLight` and the shader

#### Scenario: Minlight has no effect
- **WHEN** the benchmark's worldspawn is given `_minlight 64` in a scratch copy and baked
- **THEN** one warning names `_minlight`, and the lightmap is byte-identical to the bake without it

### Requirement: Attached emitter lights skipped
The stage SHALL NOT add `light_area` entities or lights with `_proxy 1` to either bake, and SHALL print no warning for them: the emitter's surface is already an area light in the bake, so adding them would count its direct light twice in the full bake (plan revision 7). A `light`, `light_spot` or `light_sun` has `_proxy 1` when its `_proxy` value starts with `1`, q3map2's reading of a boolean key, so the placeholder `-light` of openspec change emissive-area-lights skips the same lights. For each shader with emissive surfaces that have no attached light, the stage SHALL print one warning naming the shader and the number of such surfaces. A surface is attached when a `light_area`'s rectangle is coplanar with it (within 1 unit and 1°) and overlaps it, or when a `_proxy` light's origin lies inside the surface's bounds grown by 64 units on every axis. The warning SHALL NOT fail the stock check.

#### Scenario: Proxy not counted twice
- **WHEN** a scratch map's emissive sign is baked with and without a `light` carrying `_proxy 1` in front of it
- **THEN** the two bakes' lightmap, light-grid and draw-vertex lumps and side files are byte-identical

#### Scenario: Area light ignored silently
- **WHEN** the benchmark, whose fireplace's fire face carries a `light_area`, is baked with and without that `light_area`
- **THEN** the two bakes' lightmap, light-grid and draw-vertex lumps (every lump but the entity lump) and side files are byte-identical, and no warning names `light_area`; with the `light_area`, no warning names `textures/bench/fire`, and the warning for `textures/bench/neon` counts the sign's 5 neon faces, which have no light attached

#### Scenario: Unattached emitter warned
- **WHEN** a map has three surfaces with an emissive shader and no attached light
- **THEN** one warning names that shader and the count 3

### Requirement: Draw-surface casters follow the editor's rule
A draw surface SHALL occlude in the bake only if its shader casts under the editor's caster rule. It never casts if the shader has `surfaceparm` sky, fog, water, slime, lava, playerclip, botclip, areaportal, noshadows, trigger or hint; non-solid nodraw; or trans without alphashadow. An alpha-tested surface SHALL cast through its base stage's `alphaFunc`. Copies q3map2 makes for `_skybox` portals SHALL be left out. Every other draw surface with a lightmap SHALL receive.

#### Scenario: Sky does not block the sun
- **WHEN** a room is closed above by a sky-shader ceiling and lit by a `light_sun` pointing down through it
- **THEN** the floor receives the sun's irradiance

#### Scenario: Glass passes light
- **WHEN** a pane with `surfaceparm trans` and no `alphashadow` stands between a spot and a wall
- **THEN** the wall behind the pane receives the spot's direct light

### Requirement: Solid hulls block light
The stage SHALL build one closed hull per brush of every model whose content shader is solid and whose non-bevel sides all use casting, non-alpha-tested shaders. Brushes of a model with an origin SHALL be moved by it, as its draw surfaces are. For the bake, each hull plane of a drawn side SHALL be moved inward by a fixed epsilon, so the visible draw surface is hit first; `nodraw` sides SHALL stay in place, so caulk-backed brushes meeting at a seam leave no gap. Hulls SHALL block light without shading or receiving. Patches and models SHALL have no hull.

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

A shader that declares `skyparms` SHALL have no emissive: its light comes only from the sky environment light, so its cloud stages, additive or not, never make it an area light or draw the unattached-emitter warning.

A shader SHALL emit without occluding only if every stage is additive, or if it doesn't cast under the caster rule. An image that can't be loaded SHALL fall back to white with a warning. The bake's shading is Lambert: normal, metallic-roughness and occlusion maps SHALL NOT change it.

#### Scenario: Emissive sign lights the wall
- **WHEN** the benchmark's neon-sign shader, with its additive stage and `qer_pbr_emissiveStrength`, is baked
- **THEN** the sign is an area light in the bake, and the wall luxels next to it are brighter than with the sign's emissive strength set to 0

#### Scenario: Opaque sign still occludes
- **WHEN** a shader has an opaque lightmapped base stage plus an additive glow stage, and a spot shines at it from in front
- **THEN** the wall behind it gets no direct light from the spot

#### Scenario: Additive cloud layer is no emitter
- **WHEN** a map's sky shader declares `skyparms - 512 -`, no `qer_pbr_emissiveStrength` and a second stage with `blendFunc add`, and is baked single-threaded with `-shbake`
- **THEN** no unattached-emitter warning names it, and the BSP and side file are byte-identical to those of the same map with that stage removed

### Requirement: Points baked in one list
The stage SHALL build one point list and give the same list, in the same order, to each bake it runs, so point *i* of one bake is point *i* of the other. The list holds:
- every super-luxel that `MapRawLightmap` mapped and didn't mark occluded;
- each light-grid point once, as a full-sphere point (sh-baker's `SurfacePoint::full_sphere`), unless `-nogrid` is given;
- every draw vertex of a surface whose shader doesn't have `q3map_noVertexLight`, unless `-novertex` is given.

It SHALL print the point counts, and each bake's name and progress, through q3map2's console. The output SHALL NOT depend on the thread count.

#### Scenario: Counts printed
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** the log names the number of luxel, grid and vertex points, and the progress of the full and the indirect bake each reaches 100%

#### Scenario: Thread count
- **WHEN** the benchmark is baked with `-threads 1` and with the default thread count
- **THEN** the BSPs and side files are byte-identical

### Requirement: Unbaked luxels flooded
Super-luxels the stage didn't bake, unmapped or marked occluded, SHALL take the average of their baked 3×3 neighbours, in colour (from the full bake), SH and visibility (from the indirect bake) together, as `-light` floods them. Luxels still without a value SHALL be stored as 0.

#### Scenario: No black seam
- **WHEN** the benchmark is baked
- **THEN** no luxel along the wall–floor seams of the lit courtyard is black where its neighbours are lit

### Requirement: SH follows lightmap storage
SH page *i* of the side file SHALL be BSP lightmap page *i*, with the same size. Each side-file luxel SHALL belong to the same surface point as the BSP luxel at that position. SH and visibility SHALL be averaged and copied with the same weights as the colour in every storage step after the bake, including the neighbour fill of luxels with no sample. The page layout (which surface goes to which page and where) SHALL NOT depend on the lit bytes, so it is the same with and without the full bake. With `-shbake`, lightmaps SHALL NOT be collapsed into shared copies, stamped as solid colour, approximated by vertex colour, or shared or placed by comparing their bytes.

#### Scenario: Pages correspond
- **WHEN** the benchmark is baked and its side file read back
- **THEN** the side file has as many pages as the BSP has lightmap pages, each 128×128

#### Scenario: Identical lightmaps not shared
- **WHEN** a map holds two separate, identical brushes lit identically, so their lightmaps have equal bytes, and is baked with `-shbake`
- **THEN** each surface keeps its own lightmap area, and the layout matches the one from `-shbake -shnofull`, where every used luxel is 64 64 64

### Requirement: Lightmap irradiance
The irradiance written for a luxel or vertex SHALL be the full bake's per-point irradiance (sh-baker's `SHCoeffs::irradiance`, a cosine-weighted Monte Carlo estimate at the point's normal), with no SH convolution and so no SH ringing. With `-super N`, the luxel's value SHALL be the average of its super-luxels' irradiance, which also reduces its noise.

#### Scenario: Sun at an angle
- **WHEN** a luxel of a large flat floor, the only geometry in the map, is lit only by a sun of irradiance 2π at 60° from its normal, and is baked at `-super 1`
- **THEN** its lightmap bytes are those of irradiance π, 37 37 37, within one byte

#### Scenario: No light from behind
- **WHEN** the same floor is lit only by a sun 100° from its normal, below its horizon
- **THEN** its lightmap bytes are 0 0 0

### Requirement: Lightmap encoding
Lightmap bytes SHALL encode irradiance E as a white Lambert surface would show in the editor preview's tonemap at exposure 1, calibrated to opengl1 at default cvars in a window. Per channel: d = E/π; t = Uncharted2(d)/Uncharted2(11.2), clamped to [0, 1], with the preview's Hable constants; s = sRGB-encode(t); byte = round(63.75 × s). The encoding SHALL be documented in the gamepack README, with opengl2's known differences.

#### Scenario: Midtones
- **WHEN** a luxel's irradiance is π in every channel, and another's is π/2
- **THEN** their lightmap bytes are 37, 37, 37 and 29, 29, 29

#### Scenario: White point
- **WHEN** a luxel's irradiance is 11.2 × π or more in every channel
- **THEN** its lightmap bytes are 64, 64, 64, and never above 64

### Requirement: Light grid from the bake
Each grid point's full-sphere SH from the full bake SHALL be fit as ambient plus one directional light. Its direction d SHALL be the normalised, luminance-weighted L1 vector, or +Z when that is unusable. Its directed D per channel is max(0, L1 · d)/0.488603, the irradiance of a delta light along d. Its ambient A is the irradiance of L0 after subtracting that delta light's L0 (D × 0.282095), clamped at 0. A model lit by the grid in opengl1 SHALL get encode(A) facing away from d and encode(A + D) facing d. Two exceptions: values below the engine's fixed minimum ambient get that minimum, and ambient above the engine's reachable maximum is capped there. An all-zero ambient SHALL become 1 1 1, with or without directed light, since the engine skips a grid cell whose ambient is zero as one in a wall. Grid points q3map2 finds in solid SHALL be black.

#### Scenario: Single sun
- **WHEN** an open map lit only by one `light_sun` over a ground plane of albedo 0 is baked
- **THEN** an unoccluded grid point's direction points towards the sun within 2°, its directed colour encodes the sun's irradiance, and its ambient is 1 1 1 (the zero ambient raised by the ambient-1 rule)

#### Scenario: Grid point in the dark
- **WHEN** a closed room with no light is baked
- **THEN** its grid points outside solid have ambient 1 1 1 (the ambient-1 rule) and directed 0 0 0, and grid points in solid stay 0 0 0

### Requirement: Vertex colours from the bake
Every baked draw vertex's colour SHALL be the lightmap encoding of its full-bake irradiance at the vertex normal. Vertex alpha, and the colours of `q3map_noVertexLight` surfaces, SHALL be unchanged.

#### Scenario: Vertex-lit model lit
- **WHEN** a vertex-lit model is placed in a spot light's cone and baked
- **THEN** its vertices facing the spot have non-zero colours, and those facing away from every light get only indirect light

### Requirement: Side file output
When the indirect bake runs, the stage SHALL write the side file to `maps/<name>/<name>.shsf` beside the BSP, through the q3map2 extension writer.

**What it holds.** It always holds the indirect bake: no direct light at the baked point, whether punctual, emissive or from the sky. It does hold bounced sky light when the map's sky emits. It holds:
- the SH pages;
- the un-shrunk hull mesh in BSP units, with hull triangles facing outward and q3map2's winding;
- the SH grid.

**The SH grid.** It has the BSP light grid's origin, spacing and dimensions, and one probe per BSP grid point in the same order (x fastest, then y, then z). Each probe holds that point's full-sphere SH from the indirect bake. It also holds that point's sky visibility from the indirect bake: the uniform fraction of the point's first rays that escaped, over the whole sphere. Visibility depends only on the geometry the rays trace, so a probe gets it whether or not the map's sky emits. A probe SHALL be marked invalid, with zero coefficients and zero sky visibility, when its grid point is still in solid after q3map2's nudge, or when its SH or visibility is non-finite. With `-nogrid` the SH grid has no probes, dimensions 0, 0, 0, and origin and spacing 0.

**Failures and other files.** A writer failure SHALL stop q3map2 with the writer's message. The stage SHALL NOT write any `lm_*.hdr` file.

#### Scenario: Side file written
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** `maps/bench/bench.shsf` exists and reads back with the extension reader, and no `lm_*.hdr` file exists under `maps/`

#### Scenario: No direct light in the side file
- **WHEN** a floor luxel in a spot's direct beam is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: No direct sky light in the side file
- **WHEN** a floor luxel under open sky in the town is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: SH grid matches the BSP grid
- **WHEN** the benchmark is baked with `-shbake` and its side file read back
- **THEN** the SH grid's dimensions and probe count equal the BSP light grid's, a probe in a lit room is valid with non-zero L0, and a probe whose grid point is black in the BSP because it is in solid is invalid, with sky visibility 0

#### Scenario: Probe sky visibility outdoors and indoors
- **WHEN** the town is baked with `-shbake` and its side file read back
- **THEN** a valid probe in the open crossroads, at least one grid step above the street and clear of walls, has a sky visibility above 0.25, and every valid probe inside a sealed room (no opening to the sky) has a sky visibility of exactly 0

#### Scenario: Probe sky visibility without an emitting sky
- **WHEN** a map is baked with `-shbake` once with a sky that has `qer_pbr_emissiveStrength` and once with the same sky without it
- **THEN** the two side files' probe sky visibilities and valid flags are equal, and the first bake has some valid probe with a sky visibility above 0

### Requirement: Optional L0 dump
With `-shdumpl0`, when the indirect bake runs, the stage SHALL also write each side-file page's L0 as `maps/<name>/<name>_l0_<page>.hdr`, with the page number as four digits. These files SHALL NOT be packed by the stock check.

#### Scenario: Dump written
- **WHEN** the benchmark is baked with `-shbake -shdumpl0`
- **THEN** one `.hdr` file exists per side-file page, named `bench_l0_0000.hdr` onwards

#### Scenario: Nothing to dump
- **WHEN** the benchmark is baked with `-shbake -shnoindirect -shdumpl0`
- **THEN** q3map2 warns that `-shdumpl0` has no indirect bake to dump, and writes no `.hdr` file

### Requirement: Sky environment light in both bakes
`-shbake` SHALL light both bakes from the map's sky shader, with the derivation of the `sky-environment-light` capability.

**Which sky counts.** The candidates SHALL be the distinct shaders in the BSP's shader list that declare `skyparms`. A shader with `surfaceparm sky` but no `skyparms` SHALL be no candidate. The map's sky SHALL be the alphabetically first candidate by shader name, compared without case. With two or more candidates, the stage SHALL print one warning naming every candidate and the chosen one.

**Its radiance.** The sky's radiance L(ω) SHALL be `qer_pbr_emissiveStrength` × C × T(ω), as that capability defines it:
- the strength must be declared and positive;
- C is the sky stage's `rgbGen const`;
- T(ω) comes from the six farbox images when `skyparms` names a farbox (any first argument other than `-`), else from the sky stage's image mean over the upper hemisphere;
- the sky stage is the first map stage that is not `$lightmap` and uses no rend2 keyword.

**When there is no sky light.** In each of these cases the bakes SHALL get no sky light:
- there is no candidate;
- the sky has no declared positive strength;
- a farbox image fails to load, or the cloud image is missing or fails to load.

Sky images SHALL be loaded only when the sky's strength is declared and positive, so a legacy sky never warns about its images.

The last case prints one warning naming the image or the farbox. With no sky light, the stage's output SHALL be byte-identical to its output before this change, for any map whose sky shaders have no additive stage.

**How the bakes get it.** When the sky emits, both bakes SHALL get the sky as a radiance on escaping rays, so that:
- the full bake's lightmap pages, vertex colours and light grid hold direct and bounced sky light;
- the side file holds bounced sky light only: sky reached after at least one surface reflection.

sh-baker SHALL receive L(ω) sampled into texels, never the sky's SH, whose truncation is negative below a cloud layer's horizon. Sky surfaces SHALL stay non-casting and out of the bake, so rays leave through them.

**What is printed.** The scene summary SHALL print:
- the sky shader, its kind (farbox or cloud layer), its strength, and its irradiance at +Z from the derivation;
- the irradiance at +Z that the texels sampled for sh-baker imply.

If the texels' irradiance at ±X, ±Y or ±Z differs from the derivation's by more than 1% of the largest of the derivation's six irradiances, in any channel, the stage SHALL print a warning. The scale is the largest of the six so that a sky lit mostly from the side or from below, with little irradiance at +Z, draws no spurious warning.

#### Scenario: Open floor under a cloud sky
- **WHEN** a sealed scratch map whose walls and ceiling are a cloud-layer sky shader, with `qer_pbr_emissiveStrength` s and an image of mean linear value M, holds a floor and no lights, and is baked with `-shbake -shsamples 2048`
- **THEN** every floor luxel has the lightmap bytes the encoding gives for an irradiance of π s M, within one byte per channel, their mean is within 0.1 of the unrounded encoding, and their side-file L0 is exactly 0, since they see no surface that could bounce sky light to them (the bake is Monte Carlo: at the default 128 samples the bytes scatter by about ±2 around the same mean, hence the sample count)

#### Scenario: Bounced sky light in the side file
- **WHEN** the same map holds a tall white block on the floor, and the floor luxel beside the block is baked with and without the sky's `qer_pbr_emissiveStrength`
- **THEN** its side-file L0 is above 0 with the strength and exactly 0 without it

#### Scenario: Nothing from below a cloud horizon
- **WHEN** the open-floor map holds a white overhang whose underside faces down, above a black floor
- **THEN** the underside's lightmap bytes are 0 0 0

#### Scenario: Farbox orientation
- **WHEN** a farbox sky has a non-black `_rt` face and five black faces, and two walls of the same albedo face +X and −X under it, with nothing in front of either
- **THEN** the +X wall's lightmap bytes are above the −X wall's

#### Scenario: Legacy sky unchanged
- **WHEN** the benchmark, whose sky declares no `qer_pbr_emissiveStrength` and has no additive stage, is baked single-threaded with `-shbake` by q3map2 built before and after this change
- **THEN** the BSPs and the side files are byte-identical, apart from the q3map2 version stamp

#### Scenario: Legacy sky loads no images
- **WHEN** a map's sky names a farbox whose images don't exist and declares no `qer_pbr_emissiveStrength`
- **THEN** the stage prints no warning about the farbox or its images

#### Scenario: Missing farbox face
- **WHEN** a farbox sky with a strength lacks its `_dn` image
- **THEN** the stage warns once naming the farbox, and the output is byte-identical to that of the same map with the sky's strength removed

#### Scenario: Two skies
- **WHEN** a map uses `textures/b/sky` and `textures/a/sky`, both with `skyparms`
- **THEN** the stage warns once naming both, and the bake uses `textures/a/sky`

#### Scenario: The town's sky
- **WHEN** the town is baked with `-shbake`
- **THEN** the summary names `textures/town/sky` as a cloud layer of strength 14.6, and gives an irradiance at +Z of (1.518, 2.499, 2.006) within 0.1% from the derivation and within 1% from the texels, with no cross-check warning
