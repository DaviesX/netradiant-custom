# Benchmark map

The `benchmark` mod: ordinary Quake 3 content that exercises every material feature, light type and occlusion case
the pipeline must preview, compile and render. It is the parity and regression asset for every later stage
(plan §0.1). `/home/davis/q3game/q3data/benchmark` is a symlink to this directory, so the editor (under `Q3.game`
with the `benchmark` mod) and ioquake3 (`+set fs_game benchmark`) both see it.

Stock acceptance: **2026-10-02**, by the author, from the stock check's screenshots of all five views on `opengl1`
and `opengl2` plus the editor's `garden` captures (run `20261002-173903`, engine commit `0135354f`). The run after the
review's grass-placement fix and the light retune (`20261002-202801`, ceiling spot cone 70/50) passed every row.

## Layout

| Path | Content |
|---|---|
| `maps/bench.map` | 38 brushes, one patch and 19 entities: a street between two buildings under an open sky, with a small lawn of trees and grass at its east end |
| `maps/bench.cameras` | the view requests the stock check visits |
| `scripts/bench.shader`, `scripts/shaderlist.txt` | 17 materials |
| `textures/bench/` | base colour images, normal maps (`*_nrm`), metallic-roughness (`*_mr`) and occlusion (`*_ao`) |
| `models/bench/` | the vegetation meshes (`tree`, `grass_clump`, `grass_patch`, OBJ with `.mtl`), used at compile time only |
| `levelshots/bench.jpg` | the loading-screen image, cut from the stock check's `opengl1` `sign` screenshot |

## Materials

| Shader | Exercises |
|---|---|
| `brick` | base colour with normal, metallic-roughness and occlusion maps |
| `asphalt`, `concrete` | rough dielectrics with normal maps |
| `plaster` | rough dielectric, no maps |
| `metal` | metal with normal and metallic-roughness maps (the patch arch, a pillar and a facade sheet) |
| `neon` | emissive: an additive stage with `rgbGen const ( 1 0.35 0.25 )` and `qer_pbr_emissiveStrength 8` |
| `fence` | alpha-tested (`alphaFunc GE128`), `cull none`, casts through its alpha (`surfaceparm alphashadow`) |
| `glass` | blended (`blendFunc blend`), unlit, casts nothing |
| `tarp` | double-sided opaque (`cull none`) |
| `sky` | `skyparms env/xnight2 - -`, pak0's dark night farbox, so the pk3 adds no sky images |
| `fog` | a fog volume over the street |
| `caulk`, `clip` | `nodraw` structure and player clip |
| `bark` | a normal-mapped model surface (tree trunks and branches), solid through `q3map_clipModel` |
| `leaves` | alpha-tested model cards, `cull none`, casting through their alpha (`surfaceparm alphashadow`) |
| `grass` | alpha-tested model cards, `cull none`, casting nothing (`surfaceparm trans` without `alphashadow`) |
| `dirt` | rough dielectric, no maps: the lawn's ground |

## Lights

The editor's physical light entities, previewed under `Q3.game` through the `pbr` entity module:

| Entity | Keys | Role |
|---|---|---|
| `light_sun` | `origin 0 0 200`, `angles 50 -35 0`, `intensity 2`, `_color 0.296178 0.309361 0.326665` | a cool blue-grey sun: casts the arch's and the canopies' shadows and leaves the interiors dark away from the doorways |
| `light` | `origin -300 0 150`, `intensity 200000`, `radius 256`, `_color 1 0.9 0.7` | a point light over the west street; point lights never cast |
| `light_spot` | `origin 0 -512 160`, `angles 90 0 0`, `cone 40`, `cone_inner 25`, `intensity 300000`, `radius 400`, `_color 0.933333 0.663249 0.479118` | downward over the free-standing pillar in the south interior, casting |
| `light_spot` | `origin 0 96 200`, `target sign_target`, `cone 30`, `cone_inner 20`, `intensity 250000`, `radius 1100`, `_color 1 0.6 0.5`, `_shadows 1` | aimed at the neon sign, casting |
| `light_spot` | `origin 0 512 150`, `angles 270 90 0`, `cone 70`, `cone_inner 50`, `intensity 200000`, `radius 600`, `_color 0.926665 0.8374 0.781491`, `_shadows 0` | aimed straight up to wash the north interior's ceiling; the non-casting case |

`cone` and `cone_inner` are half-angles. The editor clamps both to 89.9° (`plugins/entity/light.cpp`), so keep them
below 90. sh-baker and `renderer_sh` must clamp the same way.

The author retuned the light values in the editor preview on 2026-10-02 (change `bench-foliage`). Before that they
were the `pbr.game` bench's:
- the sun at irradiance 3, warm white (`1 0.95 0.85`);
- two point lights: `-300 0 150` at 400000, radius 512, and `0 512 150` at 200000, radius 400, which became the
  ceiling spot;
- the pillar spot with no `_color`;
- the sign spot non-casting at radius 400.

Stock Quake 3 frees `light_spot` and `light_sun` with a "doesn't have a spawn function" line; the stock check
allowlists exactly those two.

## Conversion from the `pbr.game` bench

The map (before the vegetation and the light retune, which came later), its entities and every key and value were copied unchanged from `setup/data/gamepacks/pbr/pbr.game/base/`.
A7 (change `retire-pbr-game`) deleted that pack; the source is in git history at `c572c087`. The materials were converted:

- `materials/bench.mtr` became `scripts/bench.shader`. Lightmapped materials are a `$lightmap` stage followed by the
  base colour with `blendFunc GL_DST_COLOR GL_ZERO`.
- The `.mtr` keys became top-level keywords: `normal` → `qer_pbr_normal`, `metallicroughness` →
  `qer_pbr_metallicRoughness`, `occlusion` → `qer_pbr_occlusion`, `*factor` → `qer_pbr_*Factor`, `emissivestrength` →
  `qer_pbr_emissiveStrength`. `emissive` and `emissivefactor` became the neon's additive stage. A factor of 1 next to a
  map was dropped, because a map's factors default to 1.
- `alphamode mask` became the standard alpha-tested form, `alphamode blend` became `blendFunc blend`, and `doublesided`
  became `cull none`.
- The glass's `baseColorFactor 0.8 0.9 1 0.35` is baked into `glass.tga`, because stock has no factor.
- `brick_n`, `asphalt_n`, `concrete_n` and `metal_n` were renamed `*_nrm`: rend2 loads `<base>_n`, `_nh` and `_s` by
  name. No image here may use those suffixes.
- Every TGA is stored bottom-up: ioquake3 ignores the top-down flag and would draw such images upside down.
- The old compile shim's `q3map_sun` is gone (the sun is the `light_sun`), and its sky now draws `env/xnight2`. The unused
  `trigger` material was dropped.

## Compiling

The stock check compiles with the bundled q3map2:

1. `-meta -keeplights`. `-keeplights` keeps every light entity in the BSP for `renderer_sh`.
2. `-vis -saveprt`.
3. `-light -patchshadows`. Patches cast, as in the editor.

The `-light` lightmap is a placeholder. q3map2 treats every classname starting with `light` as its own light and reads
none of the physical keys (`intensity`, `cone`, `cone_inner`), so the stock lighting looks nothing like the editor
preview. sh-baker writes the real lightmap from the same lights (task B5), and A9 judges how faithful it is.

## Vegetation

Two trees and grass stand on a dirt lawn east of the fence, and six grass clumps grow along the asphalt's edge inside
the fog. They are placeholder art for the town's vegetation (task A10), written by `tools/benchmark/gen_foliage.py`
(Python 3 with Pillow, fixed seed). Its outputs are committed, and re-running it rewrites them unchanged.

- **Models, not brushes.** Each tree and clump is a `misc_model` with `model` set to an OBJ under `models/bench/`. The
  editor draws it, and q3map2 merges its triangles into the BSP and drops the entity, so the pk3 carries no model file
  and stock Quake 3 never spawns one. `grass_patch` holds 25 clumps over about 128×128 units, and `grass_clump` holds one.
- **Grass is static crossed quads,** three per clump, not `deformVertexes autosprite`. The editor, the shadow maps and
  the baker would see a camera-facing sprite as static anyway.
- **Lightmapped.** q3map2 vertex-lights model triangles unless asked otherwise, so every vegetation shader carries
  `q3map_forceMeta` and has a `$lightmap` stage. That keeps it lit in the editor preview and gives it lightmaps in the
  BSP. `leaves` and `grass` use `q3map_lightmapSampleSize 32`, and the BSP still needs only three lightmap pages.
- **Collision.** `q3map_clipModel` on `bark` autoclips the trunks, so they stop the player. It clips even a `nonsolid`
  shader, so only `bark` has it. Leaves and grass are `surfaceparm nonsolid`.
- **Shadows.** In the editor preview, leaves cast through their alpha and grass casts nothing, which the `garden`
  captures show. The shaders declare this the same way to q3map2 (`alphashadow` on leaves, none on grass), but q3map2
  reads none of the physical light keys, so its placeholder lightmap doesn't demonstrate it. sh-baker follows the
  editor's caster rule, so B5 must apply the alpha test in its caster. `renderer_sh` builds its casters from
  collision-brush hulls today, so the canopy shadows (and the fence's) reach the engine only once C5 casts from
  alpha-tested draw surfaces.
- **Mesh conventions.** Both loaders read OBJ through assimp, rotate Y-up to Z-up and flip the winding. So the
  generator writes Y-up geometry with counter-clockwise front faces, explicit normals (bent outward from the canopy or
  clump centre), and `usemtl` set to the full shader name.

## Views

`maps/bench.cameras` requests five views:

| View | Shows |
|---|---|
| `sign` | from the `info_player_start` origin, the neon sign and the north doorway |
| `south-interior` | the pillar under the downward spot, the tarp, the sunless interior |
| `arch` | the arch's sun shadow on the west wall (the one-sided patch is culled from below, so the arch itself barely shows) |
| `street` | from the west end looking northeast: fog, sign, glass and fence in one frame |
| `garden` | from the lawn's south-east corner looking north-west: both trees, grass, and the far canopy's shadow on the dirt in the editor preview |

`setviewpos` pushes the player about 120 units forward before they stop, so the requested origin isn't where the
camera ends up. The stock check records the eye position the engine reports in `views.txt`. Editor captures of a view
use the `opengl1` line with pitch 0, for example `NETRADIANT_CAMERA_ORIGIN="-96 52 50"` and
`NETRADIANT_CAMERA_ANGLES="0 90 0"` for `sign`.

## Stock check

```
tools/stockcheck/stockcheck.py content/benchmark bench
```

It compiles, packs `benchmark.pk3`, runs it on `opengl1` and `opengl2`, and exits 0 when every row passes. See
`tools/stockcheck/README.md`. The author reviews its screenshots of every view on both renderers for content errors:
every shader renders with its own image; the fence, the leaves and the grass are masked; the trees and grass stand
upright with their cards visible from both sides; the glass is blended; and the sky draws. Lighting isn't judged.
