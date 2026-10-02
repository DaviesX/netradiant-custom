# Design

## Context

See `proposal.md` for motivation and the deltas under `specs/` for the required behaviour. Plan rev. 6 (§Compatibility contract, §Material authoring) is the source of the content rules. The code facts that shape the approach:

- **`ShaderTemplate::parseQuake3`** (`plugins/shaders/shaders.cpp:1298`) reads only depth-1 keywords. Every depth-2 line is consumed and ignored. Each iteration calls `tokeniser.nextLine()` before `getToken()`, so the rest of a keyword's line is skipped implicitly.
- **The PBR data path already exists.**
  - `ShaderTemplate` has `m_pbr`, `m_baseColor`, `m_normal`, `m_metallicRoughness`, `m_occlusion`, `m_emissive` and the factor fields, filled by `parsePBR` today.
  - `CShader::realiseLighting` loads them through `pbrTextureLoader()`, which bypasses gamma, when `m_pbr` is set.
  - `IShader` exposes them (`include/ishaders.h:137–153`).
  - The renderer's PBR path (`radiant/renderstate.cpp:3054`) needs `g_pbrGame && isPBR() && getBaseColor() != 0`. It hard-codes the alpha test as `GL_GEQUAL` with `getAlphaCutoff()` (`:3072`). Today every `.mtr` material takes it, blended ones included.
- **The shadow caster set.** `Shadow_materialCasts` (`renderstate.cpp:1697`) already casts solid `nodraw` (caulk) and excludes sky, fog, clip, botclip, areaportal and `QER_NOSHADOWS`. The caster pass applies the alpha mask only for `eAlphaMask` with a hard-coded "alpha ≥ cutoff" (`:1797`, `setup/data/tools/gl/shadow_fp.glsl:21`).
- **`parseQuake3` sets fewer flags than `parsePBR`.** It never sets `QER_NOSHADOWS` for `noshadows`, `hint` or `trigger`, and has no flag for `surfaceparm trans` or `alphashadow`.
- **`m_pbr` has two side effects beyond classification.** `getBump()` returns 0, and `m_pTexture` (the textured-mode editor image) is loaded with the PBR loader.
- **The PBR lighting shader reads metallic from B and roughness from G** of the metallic-roughness texture, times the factors (`setup/data/tools/gl/pbr_fp.glsl`).
- **The texture cache is keyed by (loader, name)** (`radiant/textures.cpp:327`).
- **`g_pbrGame` is set once** in `ShaderCache_Construct` (`renderstate.cpp:2202`) and read in nine places across `renderstate.cpp` and `camwindow.cpp`. Programs are created in `OpenGLShaderCache::realise()`, and `setLightingEnabled` already toggles through `unrealise()`/`realise()`.
- **The completion table is depth-aware.** `g_shaderGeneralFormats` and `g_shaderStageFormats` are selected by brace depth (`radiant/gtkdlgs.cpp:2081`).
- **Light entities.** The `pbr` entity module (`plugins/entity/plugin.cpp:171`) is the `quake3` module plus physical lights: `light`, `light_spot` and `light_sun` become PBR lights (`entity.cpp:59`, `:432`). It is chosen by the `.game` file's `entities` key. Its definitions live in `setup/data/gamepacks/pbr/pbr.game/base/entities.ent`.
- **Entity definitions** are loaded from every `.ent`/`.def` file in `gamepacks/<game>.game/<basegame>/`, sorted by file name, and the first class of a given name wins (`radiant/eclass.cpp:180–215`).
- **`Q3.game` is downloaded**, not tracked (`games/NRCPack`, installed by `install-gamepacks.sh`). `install-gamepack.sh <pack> <dest>` copies a pack's `games/*.game` files and `*.game` directories over the destination, merging directories.
- **Engine behaviour.**
  - A scripted shader with no stages draws nothing.
  - A bare texture becomes an implicit lightmap × texture shader.
  - An unknown stage keyword rejects the whole shader in both `renderergl1` and `renderergl2` (`renderergl2/tr_shader.cpp:1267`).

## Goals / Non-Goals

**Goals:**

- One Quake 3 parse feeds textured mode (unchanged) and the lighting preview.
- No change to `.mtr` parsing, to Doom 3, Quake 4 or other game types, or to textured mode under any game.
- The preview toggles without a restart.

**Non-Goals:**

- Changing the light model, the light pass or the shadow scheme. Lights are used as they are.
- The vanilla lightmap (sh-baker, B5).
- Animated maps, `tcMod` and `rgbGen wave` in the preview. The first `animMap` frame and static texture coordinates are used.
- Fidelity for multi-stage blends beyond the base, lightmap and emissive stages. Detail stages are ignored in lighting mode.

## Decisions

### A separate stage record, not the Doom 3 `LayerTemplate`

The Quake 3 parser collects a small per-stage record:
- map name;
- blend source and destination, or a shorthand;
- `rgbGen const` colour;
- whether it uses `tcGen environment`;
- `alphaFunc`;
- whether a rend2 keyword was seen.

The derivation rules run over the stage list at the closing brace and fill the existing template fields plus three flags: lightmapped, blended, preview lit.

`m_layers` is the alternative, and it was rejected. `realiseLighting` turns every layer into a loaded texture, and rewrites `m_blendFunc` when exactly one layer exists. That would change the textured-mode blending of single-stage Quake 3 shaders.

### Split `m_pbr` into classification, origin and preview-lit

- `m_pbr` keeps meaning "is PBR" (`isPBR()`).
- A "from `.mtr`" flag alone selects the PBR loader for `m_pTexture` and suppresses `getBump()`, so Quake 3 shaders keep gamma on their textured-mode image, as the modified colour-space requirement says.
- A "preview lit" flag (lightmapped, not blended, not sky, fog or `nolightmap`) decides whether `realiseLighting` loads PBR textures. It is exposed as `IShader::isPreviewLit()`.

`.mtr` materials are always preview lit, which reproduces today's behaviour (blended `.mtr` glass stays lit). The renderer's condition becomes: preview active, `isPreviewLit()`, and a base colour exists. Gating on `isPreviewLit()` rather than `isPBR()` keeps PBR-classified Quake 3 sky and blended shaders on their textured-mode state. `realiseLighting` loads PBR textures for shaders that are PBR-classified *or* preview lit, so every in-use PBR material still loads its maps, as the existing interface requirement says.

`QER_NOLIGHTMAP` is added to the flag set so `surfaceparm nolightmap` is visible outside the parser, and `QER_SURFSKY` for `surfaceparm sky`, which the preview-lit rule and the caster rule need. `QER_SKY` stays owned by `skyparms`, so the editor's sky filter is unchanged. Alpha-tested shaders also load their lighting textures, because the caster pass samples the base colour even when the preview doesn't light them.

### Defaults as constant images and factors

Without `qer_pbr_metallicRoughness`, the shader uses a constant white metallic-roughness image (G = B = 1). Its metallic factor defaults to 0 and its roughness factor to 1, unless the shader declares them. With the map, both factors default to 1.

This needs no shader change: metallic = B × factor, roughness = G × factor, which gives the spec's defaults in every case. The base colour factor defaults to (1, 1, 1, 1). The normal map defaults to the existing `_flat` image, and occlusion to `_white`.

### A separate preview alpha function

`getAlphaFunc()` and `QER_ALPHATEST` stay owned by `qer_alphafunc` and textured mode, so the editor's filters and textured mode are unchanged. The stage-derived test is stored separately, as a preview alpha function (`eGreater`, `eLess` or `eGEqual`) plus a reference. It is exposed through a new `IShader::getPreviewAlphaFunc()`, and `getAlphaMode()` returns mask, blend or opaque for Quake 3 shaders. `.mtr` masks map to `eGEqual` and their cutoff.

Both PBR camera passes and the shadow caster pass use the preview function and reference instead of the hard-coded `GL_GEQUAL` and "alpha ≥ cutoff". The caster fragment program gains a comparison-mode uniform (greater, less, greater-or-equal) beside `u_alpha_cutoff`. `GT0` uses reference 0 with "greater", so a cutoff of 0 no longer means "mask off".

### The editor's caster rule

`Shadow_materialCasts` gains the rules the spec lists, using two new flags:
- `QER_SURFTRANS` (`surfaceparm trans`): never casts unless `QER_ALPHASHADOW` is also set, in which case it casts through its alpha test;
- `QER_ALPHASHADOW` (`surfaceparm alphashadow`).

`QER_LIQUID` shaders never cast. `parseQuake3` sets `QER_NOSHADOWS` for `noshadows`, `hint` and `trigger`, as `parsePBR` already does. Solid caulk already casts. The `pbr.game` README's caster list is updated to the new rule.

The rule borrows q3map2's flag names but is the editor's own. q3map2 decides occlusion differently (only `trans`/`alphashadow`, image alpha instead of `alphaFunc`, brush entities off by default), and that no longer matters: sh-baker writes the shipped lightmap and follows the editor's rule (plan rev. 6).

### Emissive strength keeps the `.mtr` meaning

`qer_pbr_emissiveStrength` fills `m_emissiveStrength` directly, and the stage's `rgbGen const` fills `m_emissiveFactor` (white when absent). Emissive radiance is texel × factor × strength in both languages, so there is no Quake 3 specific scale.

### Physical lights under `Q3.game` through an overlay pack

The light code needs no change: `Q3.game` only has to select the `pbr` entity module and see the light definitions. Both come from files in the tracked `setup/data/gamepacks/pbr/` pack:
- `games/Q3.game`: the downloaded file with `entities="pbr"` and a comment saying why;
- `Q3.game/baseq3/_pbr_lights.ent`: `light`, `light_spot` and `light_sun` as in `pbr.game`'s `entities.ent`. Its name sorts before `entities.ent`, so its `light` replaces the stock definition.

`install-gamepacks.sh`, which the default `make` target runs, installs the downloaded packs and then this pack, so the overlay always lands after the stock `Q3.game` and a rebuild can't leave a half-installed state (stock `Q3.game` with the PBR `light` definition). The manual command `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks` does the same for an existing install; the pack README says so.

The `pbr` module also turns `lightJunior` into a PBR point light (`entity.cpp:56`). It is a q3map2-only entity the town doesn't use, so this is accepted.

Alternatives considered:
- Making the `quake3` module create PBR lights. Rejected: it would change `light` for every Quake 3 based game.
- Choosing the light type from the preview preference. Rejected: entity nodes are built once per map, so a live preference would need rebuilding them.

### The preference replaces `g_pbrGame` behind the existing accessor

`ShaderCache_pbrGame()` keeps its name and call sites. It returns true:
- when the preference is on for a `type="q3"` game; or
- when the `.game` declares `shaders="pbr"`.

The preference is registered with the other camera preferences, so it lands in the per-game `local.pref`.

The setter does three things:
1. If the camera is in lighting mode and the preview turns off, it switches to textured mode.
2. It runs the shader cache's `unrealise()`, updates the value, then `realise()`, which creates or destroys the PBR programs.
3. It releases the shadow atlases and the HDR target.

The related widgets are shown for exactly the games that show the preference. That is a static property of the game type, so the preferences page needn't be rebuilt.

### Completion entries follow the table's conventions

The seven `qer_pbr_*` entries go in `g_shaderGeneralFormats` with `c_colorKeyLv1` and the `c_pageQER` page. No stage entries are added.

### Fixtures live in their own mod

`content/fixtures/` is a mod directory of its own (`fs_game fixtures`). It holds one shader per spec scenario, including the deliberately invalid rend2 keyword case. It never ships, and the stock check doesn't pack it. The benchmark mod (`q3-bench-and-stock-check`) stays clean content.

## Risks / Trade-offs

- **The stage parser might reject shaders that loaded before.** → It skips unknown keywords and only reads tokens it recognises. A task records the baseq3 `pak0` shader count and the parse errors before and after.
- **Every baseq3 texture becomes lit in lighting mode, which costs more passes.** → This only applies in lighting mode, which Quake 3 games didn't have before. Per-light culling is unchanged.
- **The rend2-keyword warning is noisy on third-party rend2 content** (e.g. custom maps in `baseq3`). → It prints once per shader, and only for shaders whose script is parsed.
- **Overwriting the downloaded `Q3.game`.** → `install-gamepacks.sh` installs the overlay last, so rebuilding keeps it. Installing the downloaded packs by hand without it restores the stock file; the pack README says to re-run the overlay step.
- **`light` changes meaning under `Q3.game`.** → Only in this local install. Stock Quake 3 maps opened under `Q3.game` show their `light` entities with physical defaults; they still compile with q3map2 as before, because the map file is unchanged.
- **Detail stages are dropped in lighting mode.** → Accepted. Textured mode still shows them.

## Migration Plan

Additive. Under `Q3.game` the user gains lighting mode, a preference that defaults to on, and physical light entities. Rollback is reverting the change and re-running `install-gamepacks.sh` (which then no longer installs the overlay) after deleting `install/gamepacks/Q3.game/baseq3/_pbr_lights.ent`, which leaves `pbr.game` untouched. The local setup steps are in `tasks.md` group 1.
