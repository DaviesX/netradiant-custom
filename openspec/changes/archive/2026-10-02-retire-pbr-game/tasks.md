# Tasks

## 1. Baselines (before any change)

- [x] 1.1 On the current build, under `Q3.game` with `fs_game benchmark`, capture lighting-mode screenshots of `content/benchmark/maps/bench.map` with shadows on from the `sign` view (`NETRADIANT_CAMERA_ORIGIN="-96 52 50"`, `NETRADIANT_CAMERA_ANGLES="0 90 0"`) and from the `garden` view (the `opengl1` line of the latest `stockcheck-out/*/views.txt`, pitch 0). Write the exact origin and angles of both views into `content/fixtures/baseline.txt`. Every later capture (3.2, 6.1) uses those recorded values, not a fresh `views.txt`, because the engine's eye position drifts between stock-check runs. Use `-global-gamefile Q3.game -Q3.game-GameName benchmark -Q3.game-CameraRenderMode 4 -Q3.game-LightingShadows true`. Save them to `content/fixtures/baseline/` as `q3bench_sign_lighting.png` and `q3bench_garden_lighting.png`. Also record in `content/fixtures/baseline.txt` the `Loaded N shader definitions` and `Error parsing shader` lines from `radiant.log` for `fs_game benchmark` and for `fs_game fixtures`. Verify: both PNGs exist and show the lit bench (neon glow, fence cut-out shadow, blended glass), and `baseline.txt` has the new lines (view origins and angles, shader counts) with the date and commit.

## 2. Fold the survivors into the Quake 3 overlay

- [x] 2.1 Add `setup/data/gamepacks/pbr/Q3.game/default_build_menu.xml`: a copy of `games/NRCPack/Q3.game/default_build_menu.xml` with one comment line saying it is the overlay's copy, plus a `<separator />` and a build "Town: BSP -keeplights + VIS + light (placeholder)" at the end. The build runs `[q3map2] -meta -keeplights`, `[q3map2] -vis -saveprt` and `[q3map2] -light -patchshadows` on `"[MapFile]"`. Reinstall with `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks`. Verify:
  - `diff` against the NRCPack file shows only the comment and the added group;
  - in the editor under `Q3.game` with the `benchmark` mod, running the group on `bench.map` produces `bench.bsp` with no leak in the build output;
  - the BSP's entity lump contains the `light_sun` and both kinds of `light_spot` (`strings bench.bsp | grep -c light_spot` is 3).
  - *Done 2026-10-02:* the editor loaded the menu (no "failed to parse build menu" line, and no per-user `build_menu.xml` exists). Clicking the entry couldn't be automated: Qt ignores keystrokes sent to one window, and global keystrokes could reach the user's open editor. So the group's three commands were run from the shell exactly as the editor expands them. Result: exit 0, no leak, and the BSP holds 1 `light`, 3 `light_spot` and 1 `light_sun`.
- [x] 2.2 Write `setup/data/gamepacks/pbr/README.md` from `pbr.game/README.md`, following the design's "Docs" decision.
  - Keep: overlay install, light entities and units, shading and tonemap, shadows (constants table, caster rule, fallbacks), known differences.
  - Restate the parity procedure on `content/benchmark`'s `sign` view under `Q3.game`.
  - Rewrite every `.mtr` term in Quake 3 terms.
  - Drop the `.mtr` grammar, the compile shim, the glTF mapping table and the `pbr.game` install text.
  - Link `content/benchmark/README.md` for the bench.
  - Document the build menu group, and that a per-user `Q3.game/build_menu.xml` hides it.

  Verify: `grep -n 'mtr\|pbr\.game\|glTF' setup/data/gamepacks/pbr/README.md` finds nothing but a one-line history note. The parity command line runs as written and opens the bench in lighting mode at the `sign` view.
- [x] 2.3 Update the header comments of `setup/data/gamepacks/pbr/games/Q3.game` (point at the pack README) and `Q3.game/baseq3/_pbr_lights.ent` (it is now the only copy of the definitions; drop "keep the two in step"). Verify: the `diff` of `games/Q3.game` against `games/NRCPack/games/Q3.game` still shows only the comment and the `entities` line, and the editor under `Q3.game` lists `light`, `light_spot` and `light_sun` with the physical keys in the entity inspector.
  - *Done 2026-10-02:* both `diff`s are clean once the CRLF endings already in the committed overlay are ignored, and both files parse as XML. Only the comment changed in `_pbr_lights.ent`. The editor loads it with no entity-definition errors, and its lights still light the bench (`light_spot` with `target`). The inspector itself wasn't opened, because it can't be automated here.

## 3. Retire the `.mtr` language

- [x] 3.1 In `plugins/shaders/`, remove the following. Keep `loadPBRTexture` and all three reserved names (`_white`, `_black`, `_flat`): Quake 3 `qer_pbr_*` textures load through it.
  - `ShadersPBRAPI` and its module registration (`plugin.cpp`);
  - `SHADERLANGUAGE_PBR` (`shaders.h`);
  - `ShaderTemplate::parsePBR`, its declaration and dispatch, and `PBR_argFloat` (`shaders.cpp`);
  - `m_fromMtr` and its two uses: `realise()` always uses the default loader, and `getBump()` returns `m_pBump`.

  Update the `.mtr` wording in `include/ishaders.h` comments. Verify:
  - `make` builds without warnings in the touched files;
  - `grep -rn 'SHADERLANGUAGE_PBR\|parsePBR\|m_fromMtr\|ShadersPBR' plugins radiant include` is empty;
  - under `Q3.game`, the shader-definition counts and `Error parsing shader` lines for `benchmark` and `fixtures` equal the 1.1 values;
  - the textured-mode fixture screenshots match `content/fixtures/baseline/` pixel for pixel;
  - `git diff` shows `parseDoom3`, `ShadersDoom3API` and `ShadersQuake4API` untouched.
  - *Done 2026-10-02:* `plugin.cpp` and `shaders.h` are back to their state before PBR was added (59e235ac~1). In `shaders.cpp`, `getBump()`, `realise()` and the dispatch match the pre-PBR lines.
    - The grep for `parsePBR` also matches `ShaderTemplate_parsePBRKeyword`, the Quake 3 `qer_pbr_*` handler that stays. It was run as `::parsePBR`, which is empty.
    - Shader counts are 889, 912 and 906 with no parse errors, as in 1.1.
    - The four committed fixture PNGs still match, and `fixture_lmfirst` is identical to its 1.1 capture. The 1.1 bench captures are identical too.
- [x] 3.2 In `radiant/renderstate.cpp` and `renderstate.h`:
  - remove `ShaderCache_pbrLanguageGame()`;
  - make `ShaderCache_pbrGame()` return `g_pbrPreview && ShaderCache_pbrPreviewOffered()` and `ShaderCache_getPBRPreview()` return `g_pbrPreview`;
  - remove the blended branch of the lit-surface state (`isPreviewLit()` surfaces are never blended);
  - update the comments that mention `shaders="pbr"`, and the "in a pbr game" comment at `renderstate.h:56`.

  In `radiant/camwindow.cpp`, remove every `ShaderCache_pbrLanguageGame()` use:
  - the third term of `Camera_lightingModeOffered` (`:199`);
  - the early return in `PBRPreviewImport` (`:2637`);
  - the widget's disabled case (`:2728`, `:2734`);
  - `!ShaderCache_pbrLanguageGame()` in the registration gate (`:2852`).

  Reword the "PBR games" comments at `radiant/camwindow.h:77` and `radiant/camwindow.cpp:974` to say "while the PBR lighting preview is active". Update the `.mtr` sentence in `setup/data/tools/gl/shadow_fp.glsl`. Verify:
  - `make` builds, and `grep -rni 'pbrLanguageGame\|shaders="pbr"\|pbr game' radiant setup/data/tools include plugins` is empty;
  - lighting captures taken at the view origins and angles recorded in 1.1 match the 1.1 PNGs pixel for pixel;
  - the "PBR lighting preview" box under `Q3.game` is editable, and turning it off leaves lighting mode without a restart;
  - the preference is absent under a Quake 1 game.
  - *Done 2026-10-02:* `make` passes with no warnings.
    - The grep's remaining `pbr game` hits are four comments that point to "the pbr gamepack README" (`renderstate.cpp:1740`, `cascade.h:26`, `pbr_fp.glsl:10`, `irender.h:65`). They stay correct, because the pack keeps its name and the README is at its root.
    - The `sign` and `garden` captures are byte-identical to 1.1, glass included.
    - With `-Q3.game-LightingPBRPreview false` and `CameraRenderMode 4`, the editor starts in textured mode, identical to a mode-2 capture.
    - Checked by reading the code, because the preferences dialog can't be automated here: the checkbox no longer has a disabled case, `PBRPreviewImport` still leaves lighting mode when the preview turns off, and `ShaderCache_pbrPreviewOffered()` (`type="q3"` only) is unchanged, so Quake 1 still doesn't show the preference.

## 4. Delete `pbr.game`

- [x] 4.1 `git rm` `setup/data/gamepacks/pbr/games/pbr.game` and `setup/data/gamepacks/pbr/pbr.game/`. Delete the installed `install/gamepacks/pbr.game/` and `install/gamepacks/games/pbr.game`, then re-run `sh install-gamepack.sh setup/data/gamepacks/pbr install/gamepacks`. Verify:
  - `git ls-files setup/data/gamepacks/pbr` lists exactly `README.md`, `games/Q3.game`, `Q3.game/baseq3/_pbr_lights.ent` and `Q3.game/default_build_menu.xml`;
  - `grep -rl 'shaders="pbr"' setup install/gamepacks` is empty;
  - the installer's `cp` lines name only `Q3.game`;
  - `install/gamepacks/games/` holds no `pbr.game`, so the game selection dialog, which lists that directory, has no "PBR" entry;
  - the editor still starts under `Q3.game` and opens the bench.
  - *Done 2026-10-02:* 31 tracked files were removed with `git rm`, plus the 22 untracked Godot `.import` files beside the deleted textures.
    - `git ls-files` lists `games/Q3.game` and `_pbr_lights.ent`. The new `README.md` and `default_build_menu.xml` are on disk but not yet staged.
    - The grep is empty, the installer copies only `Q3.game`, and `install/gamepacks/games/` has no `pbr.game`.
    - The editor loads the bench: 39 primitives, 20 entities, 906 shader definitions.
    - The first lighting capture after the deletion differed from 1.1 in 69 pixels by one level (bottom-left). Three re-captures were byte-identical, so it was a one-off.

## 5. Plan and project docs

- [x] 5.1 In `docs/pbr-plan/pbr-plan.tex`:
  - change every `install/gamepacks/pbr.game/README.md` reference to `setup/data/gamepacks/pbr/README.md`;
  - mark §2.2 step 5 "*Done (`retire-pbr-game`).*" the way steps 1–4 are marked;
  - note in step 5 that `-shbake` joins the overlay menu's town group in 4.3;
  - restate emissive strength without `.mtr`, matching the modified `pbr-material-format` requirement: the rev. 6 bullet "Emissive strength has the `.mtr` meaning" (`:97–100`) and the material-authoring text at `:356–357`. Radiance is texel × `rgbGen const` colour × `qer_pbr_emissiveStrength`, added to the base pass in the light passes' linear units with no Quake 3 specific scale. Historical mentions of `.mtr` in the revision notes stay.

  Verify: `latexmk -pdf` builds in `docs/pbr-plan/`; `grep -n 'pbr.game/README' docs/pbr-plan/pbr-plan.tex` is empty; and no sentence outside the revision history defines a current quantity in terms of `.mtr`.
  - *Done 2026-10-02:* `latexmk` exits 0. Both README references were moved, the rev. 6 emissive bullet and the material table's emissive row were restated, and step 5 is marked done with the Town build and `-shbake`.
    - The remaining `.mtr` mentions are history, not definitions: revision notes, §0.1 conversion, §2.1 rationale, the "current state" snapshot (§1, like its other pre-A3 references), and the material table's "`.mtr` equivalent" mapping column.
- [x] 5.2 In `content/benchmark/README.md`, change the "Conversion from the `pbr.game` bench" text so it says the source was deleted by A7 and is in git history at `c572c087`. In `content/fixtures/baseline.txt`, mark the `pbrgame_bench_lighting.png` line and the `pbr.game` command line as history that can't be reproduced after A7, and keep the PNG. Verify: no sentence in the benchmark README says the pack "stays frozen", and `baseline.txt` labels both `pbr.game` lines as history.
- [x] 5.3 In `CLAUDE.md`, tick A7, replace the "`pbr.game` is frozen" ground rule with "`pbr.game` and the `.mtr` language are retired (A7); materials are `scripts/*.shader` only", and point the Lights rule at the pack README. Verify by reading.
- [x] 5.4 Edit the `## Purpose` lines of these main specs so none describes `pbr` games or `.mtr`:
  - `openspec/specs/pbr-gamepack/spec.md`: the Quake 3 overlay pack;
  - `openspec/specs/pbr-material-format/spec.md`: PBR data in Quake 3 shaders;
  - `openspec/specs/pbr-lighting-preview/spec.md`: a per-game preference for Quake 3 games;
  - `openspec/specs/pbr-light-entities/spec.md`: the `pbr` entity module.

  Verify: `openspec validate retire-pbr-game --strict` passes, and `grep -A1 -n '^## Purpose' openspec/specs/*/spec.md | grep -i 'mtr\|pbr. games'` is empty. The requirement bodies still mention `.mtr` until archive applies this change's deltas, including the one for `benchmark-map`.

## 6. Integration

- [x] 6.1 Rebuild from clean (`make`), reinstall the overlay, and run `tools/stockcheck/stockcheck.py content/benchmark bench`. Verify:
  - every row passes;
  - the 1.1 lighting captures and the fixture baselines match once more on the final build;
  - `git grep -n 'pbr\.game'` outside `openspec/`, `content/fixtures/baseline.txt` and the history notes returns nothing.
  - *Done 2026-10-02:* "from clean" meant deleting all 852 `*.o` and `*.d` files outside `install/` and running a full `make` (exit 0, no warnings). `make clean` wasn't run, because it removes `install/`, and the user's own editor was running from there.
    - Stock check run `20261002-213629` (engine `0135354f`): compile, `opengl1` and `opengl2` all PASS.
    - The `sign` and `garden` lighting captures are identical to the 1.1 PNGs. The four committed fixture PNGs match, and `fixture_lmfirst` is identical to its 1.1 capture.
    - The `git grep` hits left are history and retirement notes only: `CLAUDE.md` rule and A7 text, `content/benchmark/README.md` conversion section, and plan revision and decision text.
