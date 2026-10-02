# Tasks

## 1. Local setup and fixture mod

- [x] 1.1 In `~/.netradiant/1.6.0/Q3.game/local.pref` (the directory name follows the `.game` file name; copy the old `q3.game` prefs if present), set `EnginePath` to `/home/davis/q3game/q3data/`. In `global.pref`, set `gamefile` to `Q3.game`. Verify: the editor starts under Quake III Arena and the texture browser lists baseq3 textures from `pak0.pk3`.
- [x] 1.2 Create the fixture mod `content/fixtures/` (`scripts/pbrfixture.shader`, `scripts/shaderlist.txt`, small images under `textures/fixture/` whose names avoid `_n`, `_nh` and `_s`) and symlink it as `/home/davis/q3game/q3data/fixtures`. One shader per spec scenario:
  - lightmap first;
  - texture first;
  - `qer_pbr_normal` only;
  - a rend2 `stage normalMap` stage;
  - glow with `rgbGen const`;
  - an environment add stage;
  - each of `metallicFactor` alone, `roughnessFactor` alone, and a map with factors;
  - a fence (`alphaFunc GE128`, `cull none`, `surfaceparm trans`, `surfaceparm alphashadow`);
  - an `LT128` stage;
  - `nolightmap` additive;
  - blended glass with `surfaceparm trans`;
  - a sky with a `qer_pbr_` keyword;
  - a stage-less scripted shader;
  - a solid caulk (`nodraw`) shader;
  - `qer_pbr_emissiveStrength 4`;
  - a malformed factor.

  Verify: with `fs_game fixtures` selected, every fixture shader appears in the texture browser.
- [x] 1.3 Record the baseline before any code change: under Q3.game with plain baseq3, write the loaded-shader count and the `Error parsing shader` lines from `radiant.log` to `content/fixtures/baseline.txt`. Also capture textured-mode screenshots of three baseq3 shaders and two fixtures with the `NETRADIANT_CAMERA_*` automation, and, under `pbr.game`, a lighting-mode screenshot of its bench from the documented camera with shadows on. Verify: the file and the six PNGs exist.

## 2. Quake 3 stage parsing and PBR derivation (A1)

- [x] 2.1 In `plugins/shaders/shaders.cpp`, add the per-stage record and collect it at depth 2 in `parseQuake3`: map / `clampMap` / first `animMap` frame, `blendFunc` (shorthand and two-factor), `rgbGen const`, `tcGen environment`, `alphaFunc`, and the rend2 keyword warning (once per shader, naming the keyword). Skip all other keywords. Verify: the shader count and errors match `baseline.txt` exactly, and the rend2 fixture prints exactly one warning.
- [x] 2.2 Read the seven top-level `qer_pbr_*` keywords. A malformed argument keeps the default and warns with the shader name. Verify: the keyword, three-component factor and malformed-argument fixtures behave as specified (a temporary debug dump of the template fields, removed before the group is complete).
- [x] 2.3 Derive the base colour, emissive (map and colour), alpha function and reference, blended and cull, per the spec rules. Verify: each derivation fixture yields the specified fields in the debug dump.
- [x] 2.4 Set classification (any `qer_pbr_` keyword) and the "from `.mtr`" flag. Restrict the PBR editor-image loader and the `getBump()` suppression to `.mtr`. Verify: the textured-mode screenshots match the 1.3 baseline pixel for pixel, and `pbr.game`'s bench still loads every material.
- [x] 2.5 Implement the Quake 3 defaults: a constant white metallic-roughness image when absent, with the metallic factor defaulting to 0 and roughness to 1, and both factors defaulting to 1 when a map is declared. Verify: the factor fixtures' resolved factors and textures match the spec table in the debug dump (rendering is verified in 7.1).
- [x] 2.6 Add `QER_NOLIGHTMAP`, `QER_SURFTRANS` and `QER_ALPHASHADOW`, and set `QER_NOSHADOWS` for `noshadows`, `hint` and `trigger` in `parseQuake3`. Compute "lightmapped", "blended" and "preview lit", expose `IShader::isPreviewLit()`, and load PBR textures in `realiseLighting` for shaders that are PBR-classified or preview lit. `.mtr` materials are always preview lit. Verify: the debug dump lists the lightmapped fixtures, the fence and bare textures as preview lit, and the `nolightmap`, sky (including the one with a `qer_pbr_` keyword), glass and stage-less fixtures as not. Every `.mtr` bench material is preview lit.
- [x] 2.7 Store the stage-derived preview alpha function and reference separately from `qer_alphafunc`, expose `IShader::getPreviewAlphaFunc()`, and return mask, blend or opaque from `getAlphaMode()` for Quake 3 shaders. Verify: the fence fixture without `qer_alphafunc` has the same textured-mode flags as in the baseline (no `QER_ALPHATEST`), and its preview function is `eGEqual` 0.5.
- [x] 2.8 Fill the emissive strength from `qer_pbr_emissiveStrength` (default 1) and the emissive factor from the emissive stage's `rgbGen const` (white when absent, black without an emissive stage), with no Quake 3 specific scale. Verify: the strength-4 fixture's resolved strength is exactly 4× the strength-1 glow's, and the strength-1 white glow resolves to factor (1, 1, 1) and strength 1.

## 3. Shader Editor completion (A2)

- [x] 3.1 In `radiant/gtkdlgs.cpp`, add the seven `qer_pbr_*` entries to `g_shaderGeneralFormats` (`c_colorKeyLv1`, `c_pageQER`, `%t` or `%f` hints). Verify manually in the Shader Editor:
  - `qer_pbr_` offers all seven at shader level and none inside a stage;
  - `qer_pbr_normal ` offers VFS paths;
  - `stag` inside a stage doesn't offer `stage`;
  - `qer_editorI` still completes.

## 4. PBR lighting preview preference (A3)

- [x] 4.1 Register the per-game "PBR lighting preview" preference in `radiant/camwindow.cpp`: shown and default on for `type="q3"`, hidden for every other type, forced on for `shaders="pbr"`. Make `ShaderCache_pbrGame()` read it, and remove the `.game` `shaders` string test from `ShaderCache_Construct`. Verify: under Q3.game lighting mode is offered on first start, `local.pref` gains the key after the preferences are saved, and the preference and widgets are absent under a Quake 1 or Doom 3 gamepack.
- [x] 4.2 Implement the live toggle: leave lighting mode if it's active, run unrealise/realise around the value change, and release the shadow atlases and HDR target. Verify: toggling five times in lighting mode never restarts, crashes or triggers `GlobalOpenGL_debugAssertNoErrors` in a debug build, and the camera ends in textured mode when it's off.
- [x] 4.3 Gate the PBR draw path at `radiant/renderstate.cpp:3054` on preview active + `isPreviewLit()` + a base colour, and use the preview alpha function and reference instead of the hard-coded `GL_GEQUAL`. Verify, under Q3.game in lighting mode with `fs_game fixtures`:
  - a baseq3 wall is ambient-shaded, not fullbright;
  - the fence is masked and double sided;
  - the `LT128` fixture discards its opaque texels;
  - the sky, glass, `nolightmap` and stage-less fixtures render with their textured-mode state.

  The factor fixtures need a light to show metal and roughness; that check is in 7.1.
- [x] 4.4 Update the caster pass: apply the editor's caster rule (spec) in `Shadow_materialCasts` and the preview alpha function in the caster draw and `setup/data/tools/gl/shadow_fp.glsl`. Update the caster list in `setup/data/gamepacks/pbr/pbr.game/README.md`. Verify:
  - a debug dump of the caster decision for every fixture matches the spec's rule (fence with `alphashadow`: cast with its alpha test; glass with `trans`: no cast; `LT128`: cast with "less"; caulk: cast; sky, fog, `nolightmap` additive: no cast);
  - under `pbr.game`, the bench's shadows are unchanged (its `.mtr` materials set neither `trans` nor `alphashadow`).

  The visual check of Quake 3 casters needs lights and is part of 7.1.
- [x] 4.5 Verify the `pbr.game` regression: its bench renders identically to the 1.3 baseline screenshot from its documented camera, with shadows.
- [x] 4.6 Verify the Doom 3 regression: a Doom 3 gamepack has no "PBR lighting preview" preference, and its lighting mode is unchanged.

- [x] 4.7 Review fixes: add `QER_SURFSKY` for `surfaceparm sky` (excluded from the preview-lit rule and the caster set), load lighting textures for every alpha-tested shader, and drop rend2-keyword stages from the derivations. Add the fixtures `skystage` (`surfaceparm sky` on an otherwise lightmapped opaque shader, no `skyparms`, no `nolightmap`) and `alphatrans` (`qer_trans`, `alphaFunc GE128`, no `surfaceparm trans`). Verify with a temporary caster dump: `skystage` doesn't cast and isn't lit; `alphatrans` casts through `eGEqual` 0.5 with a loaded base colour; the textured-mode screenshots still match the 1.3 baseline.

## 5. Physical lights under Q3.game (A6)

- [x] 5.1 Add the overlay to `setup/data/gamepacks/pbr/`: `games/Q3.game` (a copy of `games/NRCPack/games/Q3.game` with `entities="pbr"` and a comment explaining the overlay) and `Q3.game/baseq3/_pbr_lights.ent` (the `light`, `light_spot` and `light_sun` classes from `pbr.game/base/entities.ent`, plus `info_null` only if the stock file lacks it). Make `install-gamepacks.sh` install `setup/data/gamepacks/pbr` after the downloaded packs, and add the manual install step to `setup/data/gamepacks/pbr/pbr.game/README.md`. Install it with `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks`. Verify: `diff` between the overlay `Q3.game` and the downloaded one shows only the `entities` line and the comment, and `make install-data` leaves `install/gamepacks/games/Q3.game` with `entities="pbr"`.
- [x] 5.2 Under Q3.game, verify:
  - the entity menu lists `light_spot`, `light_sun` and the stock entities (`info_player_deathmatch`, weapons, items);
  - the inspector shows `intensity`, `radius`, `cone`, `cone_inner` and `_shadows` with the PBR defaults on a new `light_spot`;
  - in lighting mode with `fs_game fixtures`, a scratch map's `light`, `light_spot` and `light_sun` light a baseq3 floor, and the spot and sun cast shadows;
  - under a Quake 1 gamepack, light entities are unchanged.

## 6. Documentation

- [x] 6.1 Mark §2.2 steps 1–4 as done in `docs/pbr-plan/pbr-plan.tex`, and tick A1–A3 and A6 in `CLAUDE.md`. Verify: `latexmk -pdf` builds.
- [x] 6.2 At archive time, update the `## Purpose` of `openspec/specs/pbr-lighting-preview/spec.md` and `pbr-shadow-maps/spec.md` from "`pbr` games" to "the PBR lighting preview". Verify: `openspec validate --specs` passes.

## 7. Integration

- [x] 7.1 Under Q3.game with the preview on and `fs_game fixtures`, open a scratch map containing every fixture shader, two baseq3 textures, a caulk-roofed room, one `light`, one `light_spot` and one `light_sun`, in lighting mode. Verify:
  - everything renders as the specs describe;
  - the factor fixtures shade as specified: `metallicFactor 1` alone is a rough metal, `roughnessFactor 0.4` alone a glossier dielectric, and the map with factors halves metallic;
  - the fence casts its cut-out, the glass casts nothing, the `LT128` fixture occludes only where its alpha is below 0.5, and the caulk-roofed room's floor gets no sun;
  - `radiant.log` has no new errors relative to `baseline.txt` other than the intended rend2 warning and the malformed-factor warning.
