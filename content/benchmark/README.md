# Benchmark map

The `benchmark` mod: ordinary Quake 3 content that exercises every material feature, light type and occlusion case
the pipeline must preview, compile and render. It is the parity and regression asset for every later stage
(plan §0.1). `/home/davis/q3game/q3data/benchmark` is a symlink to this directory, so the editor (under `Q3.game`
with the `benchmark` mod) and ioquake3 (`+set fs_game benchmark`) both see it.

Stock acceptance: **2026-10-02**, by the author, from the stock check's screenshots of all four views on `opengl1`
and `opengl2` (engine commit `0135354f`).

## Layout

| Path | Content |
|---|---|
| `maps/bench.map` | 37 brushes, one patch and seven entities: a street between two buildings under an open sky |
| `maps/bench.cameras` | the view requests the stock check visits |
| `scripts/bench.shader`, `scripts/shaderlist.txt` | 13 materials |
| `textures/bench/` | base colour images, normal maps (`*_nrm`), metallic-roughness (`*_mr`) and occlusion (`*_ao`) |
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

## Lights

The editor's physical light entities, previewed under `Q3.game` through the `pbr` entity module:

| Entity | Role |
|---|---|
| `light_sun` | `angles 50 -35 0`, irradiance 3: casts the arch's shadow and leaves the interiors dark away from the doorways |
| `light` ×2 | point lights at `-300 0 150` (400000) and `0 512 150` (200000); point lights never cast |
| `light_spot` | downward (`angles 90 0 0`, cone 40) over the free-standing pillar in the south interior |
| `light_spot` | aimed by `target` at the neon sign, with `_shadows 0` (the non-casting case) |

Stock Quake 3 frees `light_spot` and `light_sun` with a "doesn't have a spawn function" line; the stock check
allowlists exactly those two.

## Conversion from the `pbr.game` bench

The map, its entities and every key and value were copied unchanged from `setup/data/gamepacks/pbr/pbr.game/base/`,
which stays frozen until A7 deletes it. The materials were converted:

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

## Views

`maps/bench.cameras` requests four views:

| View | Shows |
|---|---|
| `sign` | from the `info_player_start` origin, the neon sign and the north doorway |
| `south-interior` | the pillar under the downward spot, the tarp, the sunless interior |
| `arch` | the arch's sun shadow on the west wall (the one-sided patch is culled from below, so the arch itself barely shows) |
| `street` | from the west end looking northeast: fog, sign, glass and fence in one frame |

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
every shader renders with its own image, the fence is masked, the glass is blended and the sky draws. Lighting isn't
judged.
