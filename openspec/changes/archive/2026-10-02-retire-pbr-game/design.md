# Design

## Context

See proposal.md for why. The current state that shapes the approach:

- **Shaders plugin.** `plugins/shaders/plugin.cpp` registers four language modules. `ShadersPBRAPI` (`Name "pbr"`) sets `g_shaderLanguage = SHADERLANGUAGE_PBR`, the `materials/` path, the `mtr` extension, no default shaders and no shaderlist. `ParseShaderFile` dispatches on the language to `ShaderTemplate::parsePBR` (`shaders.cpp:1828`).
- **`.mtr`-only code in the shared template.**
  - `parsePBR` sets `m_fromMtr`, which picks the gamma-free loader for the editor image (`realise()`, `shaders.cpp:1105`) and suppresses `getBump()` (`:1001`).
  - `PBR_argFloat` is used only by `parsePBR`. `PBR_readLineArguments` is shared with the Quake 3 `qer_pbr_*` path.
  - `loadPBRTexture` serves `_white`, `_black` and `_flat` for both languages.
- **Shared PBR fields.** The PBR fields of `ShaderTemplate` and every `IShader` PBR accessor are also filled by the Quake 3 path (`deriveQuake3`, `ShaderTemplate_parsePBRKeyword`): base colour factor, emissive factor, alpha mode and cutoff, double sided.
- **Renderer.**
  - `ShaderCache_pbrLanguageGame()` (`renderstate.cpp:469`) forces the preview on for `shaders="pbr"`. It is read in `ShaderCache_pbrGame()`, `ShaderCache_getPBRPreview()` and `camwindow.cpp` (`PBRPreviewImport`, and the preference widget, where it shows the box ticked and disabled).
  - The lit-surface state (`renderstate.cpp:3117`) has a blended branch (`getAlphaMode() == eAlphaBlend`). Only `.mtr` materials reach it, because a Quake 3 shader is preview lit only when not blended (`deriveQuake3`).
- **Gamepack source.** `setup/data/gamepacks/pbr/` holds the frozen `pbr.game` (game file, build menu, `entities.ent`, `.mtr`, compile shim, map, textures, README) and the `Q3.game` overlay (`games/Q3.game`, `Q3.game/baseq3/_pbr_lights.ent`). `install-gamepacks.sh` installs the pack last, and `install-gamepack.sh` copies `games/*.game` and every `*.game/` directory.
- **Build menu loading** (`radiant/build.cpp:1275`). The editor reads `<settings>/<game>/build_menu.xml` if it exists, and the gamepack's `default_build_menu.xml` otherwise. Today `~/.netradiant/1.6.0/Q3.game/` has no `build_menu.xml`. The downloaded Q3 menu (`games/NRCPack/Q3.game/default_build_menu.xml`) is identical to the installed one, and both `q3map2` and `mbspc` are bundled in `install/`.
- **Local state.**
  - `global.pref` already selects `Q3.game`.
  - `install/gamepacks/pbr.game/` and `install/gamepacks/games/pbr.game` are installed copies. Re-running the installer doesn't remove them.
  - `~/.netradiant/1.6.0/pbr.game/` holds stale per-game preferences.

## Goals / Non-Goals

**Goals:**
- No path in the editor reads `.mtr` for Quake 3 based games, and nothing can select a `pbr` material language.
- The bench under `Q3.game` renders identically before and after, in textured and lighting mode.
- The light-model, shading and shadow documentation survives in one place that the code comments already point to ("the pbr gamepack README").

**Non-Goals:**
- No change to the `IShader` interface. Its PBR accessors are Quake 3 data now.
- No renaming of the `pbr` entity module, the `setup/data/gamepacks/pbr/` directory or the `pbr-*` capabilities.
- No change to the light code, the light defaults, q3map2, the stock check or `content/benchmark/` content.
- The pre-existing mismatch between the `light` intensity default in `pbr-light-entities` (100) and in `_pbr_lights.ent` (50000) is out of scope. It isn't introduced or touched here.
- `-shbake` in the build menu is B5's.

## Decisions

### Keep the `pbr` names
The overlay directory stays `setup/data/gamepacks/pbr/`, the entity module stays `pbr`, and the capabilities keep their names.
- **Why:** each is still accurate. The pack provides the PBR light entities and documentation, and the module is still called `pbr`.
- **Cost of renaming:** paths in `install-gamepacks.sh`, `CLAUDE.md`, the plan, five specs and several source comments would change, and no behaviour would.
- **Alternative rejected:** rename to `q3-overlay`.

### Build menu: a verbatim copy of the downloaded menu plus one group
`Q3.game/default_build_menu.xml` in the overlay is the downloaded file byte for byte, plus a header comment line and, at the end, a separator and a build named "Town: BSP -keeplights + VIS + light (placeholder)". It runs:
- `[q3map2] -meta -keeplights "[MapFile]"`;
- `[q3map2] -vis -saveprt "[MapFile]"`;
- `[q3map2] -light -patchshadows "[MapFile]"`.

These are the stock check's stages. Keeping them identical means a compile from the editor is the compile the check gates. The name says "placeholder" so nobody judges the stock lighting from it before B5.

Alternatives rejected:
- **Leave the menu alone.** `-keeplights` is easy to forget, and without it `renderer_sh` loses every light.
- **Replace the downloaded entries.** That breaks the "Quake 3 behaves as downloaded" rule the `games/Q3.game` overlay already follows.
- **A per-user `build_menu.xml`.** It isn't tracked, so it doesn't survive a fresh install.

The copy can drift from NRCPack. The same is true of `games/Q3.game`, and the same `diff` check catches it.

### Docs: one README at the pack root
`setup/data/gamepacks/pbr/README.md` replaces `pbr.game/README.md`. It isn't installed, because `install-gamepack.sh` copies only `games/` and `*.game/`.

| Kept | Dropped |
|---|---|
| Installing the overlay | `.mtr` grammar |
| Light entities and units | `.mtr` compile shim |
| Shading and tonemap | glTF-exporter mapping table: the exporter is abandoned, and C4 makes `renderer_sh` read the entities |
| Shadows: constants, caster rule, fallbacks | Install instructions for `pbr.game` |
| Known differences from the engine | The benchmark description: `content/benchmark/README.md` is canonical, and the README links to it |
| Parity procedure, on the bench's documented view | |

The parity procedure now uses `content/benchmark`'s `sign` view under `Q3.game`. The command line becomes `-global-gamefile Q3.game -Q3.game-GameName benchmark -Q3.game-CameraRenderMode 4`, and the view is the `opengl1` eye position from the stock check's `views.txt` at pitch 0, as the benchmark README already prescribes for editor captures. The tolerance and the comparison method are unchanged.

`.mtr` wording in the shading text is restated in Quake 3 terms. For example, the base pass becomes "emissive × emissive colour × `qer_pbr_emissiveStrength`".

### Code removal: language, parser and the paths only `.mtr` reached
| Removed | Kept |
|---|---|
| `ShadersPBRAPI` and its module registration | `IShader` PBR accessors |
| `SHADERLANGUAGE_PBR` | `ShaderTemplate` PBR fields |
| `ShaderTemplate::parsePBR` and its dispatch | `PBR_readLineArguments` |
| `PBR_argFloat` | `loadPBRTexture` with all three reserved names, `_white`, `_black` and `_flat` |
| `m_fromMtr`, so the editor image always uses the default (gamma) loader and `getBump()` returns `m_pBump` | |
| `ShaderCache_pbrLanguageGame()` and its uses | |
| The lit-and-blended branch of the lit-surface state | |

- **Preview.** `ShaderCache_pbrGame()` becomes `g_pbrPreview && ShaderCache_pbrPreviewOffered()`, and `ShaderCache_getPBRPreview()` returns `g_pbrPreview`. `ShaderCache_pbrLanguageGame()` has five callers outside `renderstate.cpp`, all in `camwindow.cpp`:
  - `Camera_lightingModeOffered` (`:199`) drops its third term;
  - `PBRPreviewImport` (`:2637`) drops its early return;
  - the preference registration (`:2728`, `:2734`) loses its disabled case;
  - the second registration gate (`:2852`) drops `!ShaderCache_pbrLanguageGame()`.
- **`_black` stays.** `loadPBRTexture` also loads the textures named by Quake 3 `qer_pbr_*` keywords, so `_black` is a reserved name a `.shader` file can use. Removing it would change behaviour shader authors can see, even though no content uses it today.
- **Why remove the blended branch.** The spec now says no lit surface is blended. A dead branch that sets translucent sort and blend state would mislead the next reader, for example C4 porting the lit state to `renderer_sh`.
- **Why `m_fromMtr` can go without changing Quake 3 output.** It is false for every Quake 3 shader, so textured-mode images, which the 1.3 baseline guards, are unchanged by construction.
- **Alternative rejected: keep the parser behind a build flag.** Nothing reads `.mtr` any more, and the plan retires it outright.

### Verification by before/after captures
Baselines are captured before any code change with the existing `NETRADIANT_CAMERA_*` automation:
- the bench under `Q3.game` (`benchmark` mod) in lighting mode with shadows on, from the `sign` and `garden` views;
- the shader-definition count and `Error parsing shader` lines for `fs_game benchmark` and `fs_game fixtures`.

The exact origin and angles of both views are written to `baseline.txt` and reused for every later capture. The engine's recorded eye position drifts by a unit or so between stock-check runs, so re-reading `views.txt` would break the comparison.

After the change these must match pixel for pixel and line for line. The old `pbr.game` baseline (`pbrgame_bench_lighting.png` and its `baseline.txt` lines) stays, labelled as history that can't be reproduced after A7. The existing textured-mode fixture baselines in `content/fixtures/baseline/` must still match. The stock check must pass.

Doom 3 and Quake 4 can't be exercised: no game data is installed for them. Their invariance is shown by the diff instead: `parseDoom3`, `ShadersDoom3API` and `ShadersQuake4API` are untouched, and the language dispatch keeps both branches.

## Risks / Trade-offs

- [A stale `install/` or settings tree still has `pbr.game`, whose `shaders="pbr"` now names a missing module] → the tasks delete `install/gamepacks/pbr.game/` and `install/gamepacks/games/pbr.game`, and the game dialog no longer lists it. `global.pref` already selects `Q3.game`. `~/.netradiant/1.6.0/pbr.game/` is left in place: it is inert without the game file.
- [A per-user `Q3.game/build_menu.xml` hides the new group] → none exists on this machine today. The README says to delete it (or add the group by hand) if the group is missing.
- [Removing the blended lit branch changes output if some Quake 3 shader is both preview lit and blended] → `deriveQuake3` makes these exclusive. The before/after bench capture includes the blended glass, which proves it.
- [The overlay menu drifts from NRCPack's] → task verification diffs them, as for `games/Q3.game`.
- [The git history is the only copy of the frozen bench and `.mtr`] → accepted. `content/benchmark/README.md` documents the conversion, and commit `c572c087` is the last with the pack intact.

## Migration Plan

1. Capture the baselines (above) on the current build.
2. Land the data, docs and code changes.
3. Re-run `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks`, delete the two installed `pbr.game` paths, and rebuild.
4. Compare the captures, run the stock check, and check the game dialog.

Rollback: revert the commit and re-run the installer, which restores `pbr.game` in `install/gamepacks/`.
