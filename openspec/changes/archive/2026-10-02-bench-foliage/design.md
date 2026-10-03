# Design

## Context

See `proposal.md` for motivation and `specs/benchmark-map/spec.md` for required behaviour. The facts that shape the
approach, checked in the code:

- **Model loading is shared.** q3map2 (`tools/quake3/q3map2/model.cpp:298-344`) and the editor
  (`plugins/assmodel/model.cpp:663-690`, the only model module installed) both load models through assimp, with the
  same flags (`FlipUVs`, `FlipWindingOrder`, `PreTransformVertices`, `GenNormals` only when the file has none) and
  the same Y-up to Z-up root rotation. One OBJ therefore lands the same way in both.
  - The rotation maps OBJ `(x, y, z)` to Quake `(x, -z, y)`.
- **Material to shader name** is the same rule in both (q3map2 `model.cpp:199-238`, editor `model.cpp:217-263`).
  - The `usemtl` name is the shader name when it starts with `textures/` or `models/`. Otherwise the `map_Kd` path
    wins.
  - The extension is stripped, and a name with no `/` gets the model's directory prepended.
- **Entity keys:** both honour `model`, `origin`, `angle`/`angles`, `modelscale`, `modelscale_vec` and `_remap*`.
- **q3map2 merges models.**
  - A `misc_model` with no `target` goes into worldspawn (`model.cpp:1494-1517`). Its triangles become BSP draw
    surfaces, and the model file is needed only at compile time.
  - `misc_model` entities are stripped from the entity lump unless `-keepmodels` is given (`bspfile_abstract.cpp:454-475`).
  - Stock baseq3's `SP_misc_model` frees the entity silently anyway (`g_misc.c:127-140`).
- **Model surfaces are vertex-lit by default.** They are forced vertex-lit triangle soups (`surface.cpp:408`, `:2332`).
  They get lightmaps only with `-meta` plus spawnflag 4, or with the shader keyword `q3map_forceMeta`
  (`shaders.cpp:1426`, gate at `surface_meta.cpp:812-839`).
- **Models are not solid by default.** A model is clipped only when its shader is solid or declares
  `q3map_clipModel`, and either spawnflag 2 is set or the shader declares `q3map_clipModel` (`model.cpp:970-999`).
  So `q3map_clipModel` clips even a `nonsolid` shader. Without it, `surfaceparm nonsolid` keeps a model unclipped.
- **q3map2 `-light` shadows:** worldspawn models cast and receive by default (`q3map2.h:118-121`). `trans` surfaces
  cast only with `alphashadow` (`light_trace.cpp:836-840`), which filters by the texture alpha.
- **Editor preview:**
  - Model surfaces go through the same lit path as brush faces: per-light passes chosen by material
    (`renderstate.cpp:3117-3175`), with lights collected per surface by bounding box (`assmodel/model.cpp:449-453`).
  - The shadow caster pass draws every visible instance, models included, filtered only by the material caster rule
    (`renderstate.cpp:1753-1765`). It applies the shader's preview alpha test to model surfaces (`:1858-1873`).
  - `cull none` disables culling per material in both passes (`:3118`, `:1845-1856`).
  - Model tangents are computed from the UVs (`assmodel/model.cpp:193-206`), so a normal map on a model is shaded.
- **The preview lights a Quake 3 shader** only when it has a `$lightmap` stage, is not blended, and declares none of
  `sky`, `fog` or `nolightmap` (`pbr-lighting-preview` spec). The world-building conventions (plan §World-building)
  already say "lightmapped surfaces only".
- **Layout.** The street runs along x from -512 to 512, with asphalt at y -192 to 192 and z 0. The kerbs are at z 8,
  and the sky ceiling is at z 256. The alpha-tested fence stands at x 300–304, y -160 to 160. The fog volume covers x
  -256 to 256 and y -192 to 192 (the asphalt only, not the kerbs) up to z 80. The `light_sun` (`angles 50 -35 0`) shines down toward +x and -y.

## Goals / Non-Goals

**Goals:**
- Vegetation that previews, compiles and runs the same way on all three targets, built only with today's editor and
  q3map2.
- Assets anyone can regenerate from one command, with no external art.

**Non-Goals:**
- Good-looking vegetation. The art is a placeholder.
- Wind or sway (`deformVertexes wave`). The editor and the shadow maps would see it as static, and that waits for A10
  in the town.
- Scattering at town scale (`q3map_surfaceModel`, foliage tools). That decision belongs to A8/A10.
- Changing editor, q3map2 or engine code. If one of the early checks (tasks group 1) fails, the change stops and the
  gap is raised with the user before any code changes.
- Judging lighting. As before, the `-light` lightmap is a placeholder.

## Decisions

### Foliage is `misc_model` meshes, not brushes or surface models

Trees and grass are OBJ meshes placed with `misc_model`. The editor draws them, q3map2 merges them into the BSP, and
stock never sees the entity.
- **Brushes and patches** can't make a canopy of crossed cards cheaply.
- **`q3map_surfaceModel`** scatters at compile time, invisible to the editor preview. That breaks preview parity and
  the "easiest path in the editor" rule.

### Grass is static crossed quads

Each grass clump is two or three vertical alpha-tested quads crossed at even angles.
- **`deformVertexes autosprite`/`autosprite2`** would turn to face the camera only in the engine. The editor, the
  shadow maps and the baker would all see a static quad, so the targets would disagree.
- Crossed quads look acceptable from every horizontal angle on all three targets.

### Every foliage shader is lightmapped, via `q3map_forceMeta`

Bark, leaves and grass all carry `q3map_forceMeta`, so q3map2 lightmaps the model triangles, and a `$lightmap` stage,
so the editor preview lights them.
- **Vertex lighting** (the q3map2 default for models, or `rgbGen vertex`) would leave the shaders unlit in the preview
  (no `$lightmap` stage). It would also break "lightmapped surfaces only", which B5 relies on to write SH per luxel.
- **Spawnflag 4** would also lightmap, but every placed entity would need it. A shader keyword can't be forgotten.
- Leaves and grass use `q3map_lightmapSampleSize 32`. That keeps their many small triangles cheap in the lightmap.
  The value is tuned if the compile reports overfull lightmap pages.

### Alpha-tested materials use the fence's form

`leaves` and `grass` copy the fence's two stages:
1. the image with `alphaFunc GE128` and `depthWrite`;
2. `$lightmap` with `blendFunc filter` and `depthFunc equal`.

Both also get `cull none`, `surfaceparm trans` and `surfaceparm nonsolid`. They differ in the caster rule:
- `leaves` adds `surfaceparm alphashadow` and casts through its alpha in the editor (observed). The same declaration
  tells q3map2 and the caster rule sh-baker follows to do so, but neither has been observed: q3map2 reads none of
  the physical light keys, and B5 doesn't exist yet;
- `grass` has no `alphashadow`, so it casts nothing anywhere. Grass blades are below shadow-map resolution, and this
  exercises the "trans without alphashadow" branch on a lit, alpha-tested model surface.

### Bark is solid, with a normal map

`bark` is a lightmapped opaque material with `qer_pbr_normal textures/bench/bark_nrm` and `q3map_clipModel`.
- The normal map is the bench's first normal-mapped model surface. It checks the editor's UV-derived model tangents.
- `q3map_clipModel` makes trunks block the player without any spawnflag. Leaves and grass stay nonsolid through
  `surfaceparm nonsolid`.
- The trunk is a low-sided prism, so the generated clip brushes stay few.

### Dirt ground for the lawn

`dirt` is a lightmapped rough dielectric with a base colour only. The asphalt brush is split at x 304, and the part
east of the fence becomes dirt, so grass doesn't grow out of asphalt. The brush count rises by one.

### Generator script outside the mod

`tools/benchmark/gen_foliage.py` (Python 3 with Pillow, no other dependency, a fixed random seed) writes:
- textures: `bark_c`, `bark_nrm`, `leaves`, `grass` and `dirt_c` into `content/benchmark/textures/bench/`. RGBA where
  alpha-tested. Every TGA is stored bottom-up, as the README requires, and the script checks this after writing.
- models: `tree.obj` (trunk and a few branches in `bark`, about 30 leaf cards in `leaves`), `grass_clump.obj` (one
  clump) and `grass_patch.obj` (about 25 clumps scattered over roughly 128×128 units) into
  `content/benchmark/models/bench/`, each with a `.mtl`.

Model details:
- Each `usemtl` is the full shader name (`textures/bench/bark`, ...), so neither loader looks for an image path.
- Card normals are per-vertex normals bent outward from the canopy or clump centre (each vertex's normal is the
  normalised offset from that centre, with its up component kept). This is the usual foliage shading trick. Both
  loaders shade with the explicit normals (q3map2 `model.cpp:1380-1384`, the editor through assimp), and the card's
  winding stays that of its face.
- The OBJ is written Y-up, with counter-clockwise front faces and explicit normals, so both loaders' rotation and
  winding flip give upright, front-facing geometry.
- The outputs are committed, so the editor and the stock check never run the script. Re-running it reproduces them
  byte for byte.

It lives under `tools/`, not in the mod, because the `benchmark-map` spec allows only content in the mod.

### Placement

Positions are starting values, tuned in the editor preview:
- **Trees:** two `tree` instances on the lawn, around (380, 110) and (340, -40), with different `angle` and
  `modelscale`. The canopy radius is about 60 and its bottom about 110 up, above the fence's 96. Each tree is below
  the sky ceiling (under 230 units tall). The sun comes from the northwest at 50° and throws each canopy shadow about
  125 units toward +x and -y. They land around (480, 40) and (440, -110), on the dirt rather than the east wall.
- **Grass:** three or four `grass_patch` instances cover the lawn. Five or six `grass_clump` instances grow along
  the asphalt's edge by the kerbs (|y| 170–185, x -256 to 256), inside the fog volume, which doesn't cover the kerbs
  themselves. The street clumps check fog over an alpha-tested model surface and lighting from the west point light.
- **Untouched:** the fence, the sign, the arch and the interiors keep their sightlines, so existing views and
  occlusion cases keep their meaning.

### A fifth view

`garden` is requested from the south kerb at the lawn's east end, at roughly (450, -230, 32), yaw 105 (north-northwest).
The `setviewpos` push of about 125 units ends near (418, -109). That leaves more than 100 units to the nearer trunk
(340, -40) and keeps the eye clear of its canopy, and the floor ahead is open (grass is nonsolid). From there tree A is
near the centre of the frame, tree B is about 33° to the left, and tree A's canopy shadow is about 40° to the right,
inside the 90° field of view. The existing
views stay as they are. If `street` now frames the kerb clumps, that is welcome but not required.

## Early check findings (tasks 1.1–1.3, 2026-10-02)

A scratch probe was a hand-written OBJ with a 64×64 quad in `textures/bench/fence` and a 32×96×32 box in
`textures/bench/brick`. It was placed twice on the east lawn, once plain and once with `_remap` to a fence copy that
adds `q3map_forceMeta`. It was compiled by the stock check and captured in the editor. The scratch files were then
deleted.

- **Orientation:**
  - The boxes stand upright, show their outer faces, and sit in the same place in the editor and on `opengl1`/`opengl2`.
  - The Y-up, counter-clockwise OBJ convention holds in both loaders.
- **Masking and shadows:**
  - The quads are alpha-masked in both.
  - In the editor's lighting mode with shadows on, the boxes cast solid shadows and the quads cast only a sparse
    cut-out pattern. Diffing against a shadows-off capture confirms the shadow is the quad's.
  - The editor shades both sides of a `cull none` card alike (dark grey for the metallic fence).
  - In the engine, the `q3map_forceMeta` quad reads the same from both sides, because it is one lightmapped surface.
    The plain quad is vertex-lit and reads much brighter from the front.
  - No back-face mismatch was found that needs a C4 note. Lighting isn't judged on the placeholder lightmap.
- **Lightmaps (read from the BSP):**
  - Without `q3map_forceMeta`, the model surfaces are vertex-lit triangle soups (type 3, no lightmap).
  - With it, the quad is a lightmapped planar surface.
  - The BSP's entity lump has no `misc_model`, and the pk3 has no `models/` entry.
- **Incidental findings:**
  - q3map2 printed `ERROR: OBJ: No object detected to attach a new mesh instance.` for an OBJ with no `o`/`g` line, yet
    the model compiled. The generator writes `o` lines.
  - The editor prints `Texture load failed: "textures/"` for the unmodified bench too, so the probe didn't cause it.
  - A view request at (370, -240) didn't move. The player box overlapped the facade metal sheet (x 200–400,
    y -256 to -248), so `garden` must keep clear of it. Its request at x 450 does.

## Verification evidence (tasks 3.1–4.2, 2026-10-02)

- **Stock check** `stockcheck-out/20261002-173903`:
  - compile, `opengl1` and `opengl2` all PASS;
  - `garden` was requested at (475, -175, 24), yaw 115, and the recorded eye is (425, -69, 50), yaw 114;
  - bark, leaves, grass and dirt are all lightmapped planar surfaces;
  - the BSP still uses 3 lightmap pages;
  - there is no `misc_model` in the entity lump and no `models/` entry in the pk3.
- **Collision** `stockcheck-out/20261002-174007`, a temporary copy of the map with its own view requests:
  - on open asphalt, the push carried the player 117 units;
  - through a grass patch, it also carried the player 117 units, so grass doesn't block;
  - toward the trunk at (320, 10), it stopped at y -11, against the trunk's autoclip.
- **Editor captures** at the recorded `garden` eye, and from an overview above the lawn, in lighting mode with
  shadows on:
  - both trees, lit grass, and cut-out canopy shadows on the dirt;
  - these are in the contact sheet `stockcheck-out/20261002-173903/review-sheet.png`, which the author accepted on
    2026-10-02.
- **Review fix:** after review, the grass patches and two street clumps were moved so that no card reaches into a
  wall or kerb. Extents were computed from the placed meshes:
  - lawn patches stay within x 306–510 and y -189 to 183;
  - street clumps stay within |y| < 191, short of the kerbs at |y| 192.
- **Light retune (task 3.3):**
  - inventory: one `light_sun`, one `light`, three `light_spot`, with the ceiling spot `_shadows 0`, and `sign_target`
    resolves;
  - the stock check re-ran after it and after the grass fix (`stockcheck-out/20261002-193809`), and every row passed.

## Risks / Trade-offs

- [Back faces of `cull none` cards are lit with the front-face normal in one target and not the other] → The early
  check (task 1.2) compares the editor and the engine from both sides of a card. Any mismatch is recorded as a
  preview gap for C4. It doesn't block the content.
- [Many small lightmapped triangles fill lightmap pages] → `q3map_lightmapSampleSize 32` on leaves and grass. The
  compile log's lightmap count is compared before and after.
- [Assimp splits or reorders the meshes differently in the two loaders] → Both run the same flags. q3map2 adds only
  `SplitLargeMeshes` (at `maxSurfaceVerts`) and `RemoveComponent`, which change neither the geometry nor the materials.
- [`renderer_sh` can't reproduce the canopy shadow as the plan stands] → Plan §3.5 (C5) builds engine casters only
  from collision-brush hulls. The nonsolid leaf cards have none, and the fence has the same gap today. The trunk
  would cast only through its autoclip brushes. With the user's agreement (2026-10-02), this change widens C5: the
  engine also casts from alpha-tested draw surfaces under the editor's caster rule (task 5.2). Until C5 lands,
  canopy shadows are parity only between the editor and sh-baker.
- [`q3map_clipModel` on an imperfect trunk prints `not autoclipped` warnings] → The trunk is a closed convex-per-face
  prism. The stock check doesn't read q3map2 warnings other than leaks and unknown directives, but the compile log is
  reviewed for them.
- [The generator isn't byte-reproducible across Pillow versions] → The committed outputs are the source of truth, and
  the script records the Pillow version it was run with in its header comment.

## Migration Plan

Content only. Rolling back is a revert of the commit. The archived `benchmark-map` acceptance date in the README is
replaced by the new acceptance date.
