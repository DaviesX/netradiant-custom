# Proposal

## Why

PBR levels are authored as stock `scripts/*.shader` text under `Q3.game` and are developed for `renderer_sh` (sh-renderer). The same pk3 must still run on stock ioquake3 (`opengl1`/`opengl2`) as plain lightmapped Quake 3 content (plan rev. 6, §1, §Compatibility contract). Today the editor only knows PBR through the frozen `pbr.game`:
- the Quake 3 shader parser ignores every stage and has no `qer_pbr_*` keywords;
- the lighting preview is gated on a game-identity string test, so `Q3.game` has no lighting mode.

This change (tasks A1–A3 and A6, plan rev. 6 §2.2 steps 1–4) makes the Quake 3 shader language carry PBR data in `qer_pbr_*` keywords only, turns the preview on under `Q3.game`, and gives `Q3.game` the editor's physical light entities. It is additive: `pbr.game` keeps working until A7.

## What Changes

- **PBR keywords (A1).** The Quake 3 parser reads seven top-level keywords: `qer_pbr_normal`, `qer_pbr_metallicRoughness`, `qer_pbr_occlusion`, `qer_pbr_roughnessFactor`, `qer_pbr_metallicFactor`, `qer_pbr_baseColorFactor` and `qer_pbr_emissiveStrength`. Every stock renderer skips top-level `qer*` keywords.
- **Vanilla stage parsing (A1).** `parseQuake3` gains a parser for vanilla stage keywords. From them it derives:
  - the base colour, i.e. the image the `$lightmap` stage multiplies;
  - the emissive, i.e. the first additive stage that isn't a lightmap or environment stage, with its `rgbGen const`;
  - the alpha test, with its `alphaFunc` and threshold;
  - the cull mode;
  - whether the shader is lightmapped.
- **No rend2 keywords.** The parser does not read rend2's stage keywords (`stage normalMap`, `specularMap`, `roughness`, …). Finding one prints a warning, because `renderergl1` rejects such a shader and the compatibility contract forbids them.
- **Defaults for Quake 3 shaders.**
  - Without a metallic-roughness map: metallic = `qer_pbr_metallicFactor` (default 0) and roughness = `qer_pbr_roughnessFactor` (default 1).
  - With a map: the factors default to 1 and multiply it, as in glTF.
  - Occlusion defaults to 1, and the normal map to flat.
  - `.mtr` keeps its existing defaults.
- **Emissive strength.** Emissive radiance = texel × `rgbGen const` × `qer_pbr_emissiveStrength`, in the same units as the `.mtr` `emissivestrength`.
- **Classification.** `isPBR()` for Quake 3 shaders means "declares any `qer_pbr_` keyword".
- **Shader Editor completion (A2).** The seven keywords join the completion table, with argument hints.
- **Lighting preview preference (A3).** The `g_pbrGame` test is replaced by a per-game preference, "PBR lighting preview". It is shown and defaults to on for `type="q3"` games, is hidden for every other game type, is forced on for `shaders="pbr"` until A7, and applies live.
- **What the preview lights.** Every lightmapped opaque Quake 3 shader is lit, PBR or not, and non-PBR ones render as rough dielectrics. Lightmapped means a `$lightmap` stage, or a bare texture with no script. Sky, fog, `nolightmap`, blended and stage-less scripted shaders are drawn unlit, as in textured mode. `.mtr` materials stay lit exactly as today.
- **Shadow caster rule.** Solid brush faces cast even when `nodraw` (caulk). `trans` surfaces cast only with `alphashadow`, through their alpha test. Sky, fog, hint, areaportal, liquids and `noshadows` never cast. The caster pass honours each shader's alpha function. This rule is the editor's own; sh-baker follows it (plan rev. 6), and it is not meant to reproduce q3map2's lightmap shadows.
- **Physical lights under `Q3.game` (A6).** The editor's existing physical light entities (`light`, `light_spot`, `light_sun`, with `intensity`, `radius`, `cone`, `cone_inner`, `_shadows`) become available under `Q3.game`. A tracked overlay in `setup/data/gamepacks/pbr/` installs a `games/Q3.game` that selects the `pbr` entity module and a `Q3.game/baseq3/_pbr_lights.ent` with their definitions. The light model, the light pass and shadows are unchanged. Stock ioquake3 prints `light_spot doesn't have a spawn function` (and the same for `light_sun`) once per entity at map load; that is harmless and allowlisted by the stock check.
- **Fixture mod.** A separate fixtures mod (`content/fixtures/`) holds the parser test shaders, so no test content ends up in a shipped pk3.
- **Out of scope.**
  - The vanilla lightmap. q3map2 `-light` stays a placeholder until sh-baker writes it (B5).
  - Retiring `.mtr` and `pbr.game` (A7). The `pbr` entity module stays, because `Q3.game` now uses it.
- **BREAKING (local install only):** under `Q3.game`, `light` entities read the physical keys instead of q3map2's `light` value, and the q3map2 light-radii display is replaced by the PBR light display. Doom 3, Quake 4 and the other game types, textured mode everywhere, and `pbr.game` behave as before.

## Capabilities

### New Capabilities

- `shader-editor-completion`: the Shader Editor's keyword completion table and which keyword families it covers.

### Modified Capabilities

- `pbr-material-format`: the Quake 3 language gains PBR keywords, stage-derived base colour, emissive and alpha, PBR classification, defaults, emissive scaling and the rend2-keyword warning. The language-selection, colour-space and single-source requirements are updated to cover Quake 3 shaders.
- `pbr-lighting-preview`: the preview is gated by a per-game preference, lightmapped opaque Quake 3 shaders are lit, and unlit surfaces keep their state.
- `pbr-shadow-maps`: shadow generation follows the preview preference instead of "`pbr` game", and the caster set and its alpha test follow the editor's caster rule.
- `pbr-light-entities`: `Q3.game` selects the `pbr` entity module through the overlay, and the overlay ships the light definitions for it.

## Impact

- `plugins/shaders/shaders.cpp`:
  - `ShaderTemplate::parseQuake3` gains stage parsing, the keywords, the derivations and the warning;
  - template flags for "PBR", "from `.mtr`" and "preview lit";
  - `CShader::realiseLighting` loads PBR textures for preview-lit shaders;
  - new constant images for the Quake 3 defaults.
- `include/ishaders.h`: `IShader::isPreviewLit()`, the `QER_NOLIGHTMAP`, `QER_SURFTRANS`, `QER_ALPHASHADOW` and `QER_SURFSKY` flags, and the preview alpha function.
- `radiant/renderstate.cpp`:
  - `ShaderCache_pbrGame()` reads the preference;
  - the PBR draw path is gated on `isPreviewLit()` and honours the preview alpha function;
  - the caster set follows the editor's caster rule, and the caster pass applies the preview alpha function (`setup/data/tools/gl/shadow_fp.glsl`).
- `radiant/camwindow.cpp`: the preference, live toggle, and the visibility of the related widgets.
- `radiant/gtkdlgs.cpp`: completion entries.
- `setup/data/gamepacks/pbr/`: new `games/Q3.game` (the downloaded file with `entities="pbr"`) and `Q3.game/baseq3/_pbr_lights.ent`.
- New `content/fixtures/` mod with test shaders and images.
- Docs: plan rev. 6 already describes this, and its §2.2 status is updated on completion.
- Local setup: `gamefile` becomes `Q3.game`, and `EnginePath` becomes `/home/davis/q3game/q3data/`.
- Unaffected: q3map2, the engine, the entity plugin code, and the `.mtr` parser.
