# NetRadiant-custom: Silent Hill 1 style town level

This fork extends the editor and q3map2 to author a **Silent Hill 1 style town level for Quake 3**, not a new game. The design plan is `docs/pbr-plan/pbr-plan.tex` (revision 6). It is the source of truth; the section numbers below (§) refer to it. Build the PDF with `latexmk -pdf` in `docs/pbr-plan/`.

## Targets (plan §1)

1. **Authoring:** NetRadiant-custom under `Q3.game`, with PBR materials written as text. q3map2 compiles.
2. **Stock:** ioquake3 with `cl_renderer opengl1` or `opengl2` at default cvars runs the same pk3 as plain lightmapped Quake 3 content, so the level can be uploaded to lvlworld.com. It uses **no rend2 features** (no normal/specular maps, `r_pbr` or `lm_*.hdr`), and should look as faithful to the enhanced target as a lightmap allows. There is no original look to reproduce: the shipped pk3 only has to run on vanilla and carry everything `renderer_sh` needs. When choices tie, the easiest path in the editor wins.
3. **Enhanced:** the customised ioquake3 (`../ioq3-custom/ioq3`) with `renderer_sh` (the new sh-renderer, `../ioq3-custom/sh-renderer`) runs the same pk3. PBR levels are developed for this target.

Original non-PBR Quake 3 levels keep running on `opengl2`. Every enhancement must be ignored by the stock target (plan §Compatibility contract).

## Ground rules

- **Materials** are stock `scripts/*.shader` files with vanilla Quake 3 stages only. All PBR data goes in top-level `qer_pbr_*` keywords: `qer_pbr_normal`, `qer_pbr_metallicRoughness`, `qer_pbr_occlusion`, `qer_pbr_roughnessFactor`, `qer_pbr_metallicFactor`, `qer_pbr_baseColorFactor`, `qer_pbr_emissiveStrength`. Emissive uses an additive stage. No rend2 keywords (`stage normalMap`, `specularMap`, `roughness`, ...): they make `renderergl1` reject the shader. Never name an image `<base>_n`, `_nh` or `_s`, because rend2 auto-loads those. See §Material authoring.
- **Lights** are the editor's physical light entities, unchanged from Stage 1/2: `light` (point, `intensity` flux, `radius` cutoff), `light_spot` (`cone`, `cone_inner`, `target` or `angles`) and `light_sun` (irradiance, one honoured), with `_shadows` on spots and suns. Point lights never cast. `Q3.game` gets them through the overlay in `setup/data/gamepacks/pbr/` (`games/Q3.game` with `entities="pbr"`, `Q3.game/baseq3/_pbr_lights.ent`); install it with `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks` after the downloaded packs. The editor preview, sh-baker and `renderer_sh` share this model (§Units). Stock ioquake3 prints `light_spot`/`light_sun` "doesn't have a spawn function" once per entity; that is harmless and allowlisted.
- **The vanilla lightmap** is written by sh-baker (B5) from the same lights. Until then q3map2 `-light` makes a placeholder that isn't expected to match the preview.
- **Packaging:** one pk3 per level (BSP, shaders, base textures, lightmaps, PBR maps, later the SH side file).
- **`pbr.game` is frozen.** No new content goes into it and no new `.mtr` files. Its light entities live on under `Q3.game`.
- `ioq3-map-exporter` and `ioq3-materialize` (in `../ioq3-custom`) were an unsuccessful earlier attempt and are not part of the pipeline.
- **Editor changes go through openspec changes** (`openspec/`), as the archived stage1/stage2 did.
- **sh-baker changes go as PRs to `github.com/DaviesX/sh-baker`**, which the user reviews and merges. Ask before pushing or opening any PR.
- **Once task A5 exists, every task's exit criterion includes the stock check.**

## Task sequence

Three tracks can run in parallel. Within a track, do the tasks in order. `needs:` lists dependencies on other tracks.

### Track A: editor and content (this repo)

A1–A6b are specified as openspec changes: `q3-shader-pbr-materials` (A1–A3 and A6), then `q3-bench-and-stock-check` (A4–A5), then `bench-foliage` (A6b).

- [x] **A1** Parse the `qer_pbr_*` keywords and additive-stage emissive in the Quake 3 shader parser (`plugins/shaders/shaders.cpp`), and fill the `IShader` PBR accessors. `isPBR()` becomes "declares any `qer_pbr_` keyword". (§2.2 step 1)
- [x] **A2** Add the `qer_pbr_*` keywords to the Shader Editor's completion table (`radiant/gtkdlgs.cpp`). (§2.2 step 2)
- [x] **A3** Replace the `g_pbrGame` game-identity test (`radiant/renderstate.cpp:2202`) with a preference, so lighting mode works under `Q3.game`. After this, switch the editor's `gamefile` to `Q3.game`. (§2.2 step 3)
- [x] **A4** Copy the benchmark into the `benchmark` mod (`content/benchmark/`): `bench.mtr` → `scripts/bench.shader` using `qer_pbr_` keywords. The lights stay as they are. (§0.1)
- [x] **A5** Build the stock check `tools/stockcheck/stockcheck.py`: the fork's ioquake3 build, `cl_renderer` opengl1 and opengl2 at default cvars, clean temp basepath. It fails on unallowlisted warnings and spawn-function messages, on missing images, on leaks and on q3map2's `Unknown q3map_* directive`. (§0.4, §Verification)
- [x] **A6** Give `Q3.game` the physical light entities: the overlay in `setup/data/gamepacks/pbr/` (`games/Q3.game` with `entities="pbr"`, `Q3.game/baseq3/_pbr_lights.ent`). No light-code change. Part of `q3-shader-pbr-materials`. (§2.2 step 4)
- [x] **A6b** Vegetation in the benchmark (openspec change `bench-foliage`): placeholder trees and grass as lightmapped `misc_model` meshes from `tools/benchmark/gen_foliage.py`. (§1, §World-building)
- [ ] **A7** Fold the build menu, benchmark map and docs into the Quake 3 pack. Retire `SHADERLANGUAGE_PBR` and the `.mtr` parser, then delete `pbr.game`. Keep the `pbr` entity module and the overlay, since `Q3.game` uses them. (§2.2 step 5)
- [ ] **A8** Author the first town block under `Q3.game`. Compile with `-meta -keeplights`, `-vis`, `-light -patchshadows` (placeholder lightmap until B5), leak-free, and pass the stock check. (§0.2)
- [ ] **A9** Measure the stock fallback: the sh-baker lightmap on opengl1 and opengl2 at defaults against the editor preview and `renderer_sh`. Deliver a written verdict on its fidelity plus screenshots, and tune the lightmap encoding if needed. needs: B5. (§0.3)
- [ ] **A10** The Silent Hill look: fog, darkness budget, carried flashlight, and grass and trees built as in the benchmark (lightmapped `misc_model` meshes, grass as static crossed quads). The flashlight needs the game-code decision. (§1)

### Track B: SH baking in q3map2

- [ ] **B1** sh-baker PR: library boundary. Public header with no tinygltf/Embree/glog/gflags types, a logging callback, optional glTF module, no `-march=native` by default. (§4.2)
- [ ] **B2** sh-baker PR: multi-page UV layouts. Per-geometry page index; xatlas allowed to emit several pages. (§4.2)
- [ ] **B3** sh-baker PR: bake session that builds the BVH and light trees once, then bakes per page, with progress and cancel callbacks. (§4.2)
- [ ] **B4** sh-baker PR: the SH side-file format (L0–L2 per page), with an optional `.hdr` L0 dump for inspection. No rend2 layout. (§4.2)
- [ ] **B5** q3map2 `-shbake` stage (`light_sh.cpp`, opt-in `SH_BAKER=1` in the Makefile). q3map2's shader parser learns `qer_pbr_*` and the editor's caster rule. Bakes from the editor's physical lights and writes the irradiance into the BSP lightmap pages, light grid and vertex colours (encoding chosen to look good on vanilla), plus the SH side file; never `lm_*.hdr`. Replaces `-light`'s lighting math; q3map2 stays the host. needs: B4, A8, A5. (§4.3)

### Track C: `renderer_sh` (in `../ioq3-custom`)

- [ ] **C1** The `ri.*` refactor: route logging, cvars, file I/O and allocation through `ri.*`, and take over an existing GL context. (§3.1)
- [ ] **C2** Tier 1: boot and walk the town under `cl_renderer sh`, with materials taken from shader text. needs: A8. (§3.3)
- [ ] **C3** Tier 2: sky, fog volumes, multi-stage shaders, marks, polys. (§3.3)
- [ ] **C4** Direct-light parity with the editor preview's physical light model. needs: A6. (§3.4)
- [ ] **C5** Shadow parity: hull reconstruction, plus alpha-tested draw surfaces (fence, leaf cards) casting through their alpha test under the editor's caster rule. (§3.5)
- [ ] **C6** SH at runtime: all bands from the side file. needs: B5. (§4.4)

### Open decisions (ask the user)

- **Game code:** baseq3 or a small mod. This blocks the flashlight in A10. (§Open decisions)
- **Parser for `renderer_sh`:** reuse `libioq3_map` or write a new one. This blocks C2. (§Open decisions)
