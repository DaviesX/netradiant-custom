# Proposal

## Why

`Q3.game` is now the only authoring target. It has the `qer_pbr_*` materials, the lighting preview, the physical light entities and the benchmark mod (A1–A6b). The frozen `pbr.game` and its `.mtr` language are a second, stock-incompatible way to author the same thing. They keep alive a `.mtr` parser, a forced-on preview path and a duplicate benchmark that nothing uses. Its README is also still where the light units, the shading model and the shadow constants are documented. This change is task A7 (plan rev. 6, §2.2 step 5). It moves what survives into the Quake 3 pack and deletes the rest.

## What Changes

- **Build menu.** The overlay in `setup/data/gamepacks/pbr/` gains `Q3.game/default_build_menu.xml`: the downloaded Quake III menu unchanged, plus a group that compiles the town the way the stock check does (`-meta -keeplights`, `-vis -saveprt`, `-light -patchshadows`). `-shbake` joins that group in B5. `pbr.game`'s own three entries are already covered by the downloaded menu, so they are dropped.
- **Benchmark map.** `content/benchmark/` is already the benchmark (A4, A6b). The frozen copy under `pbr.game/base/` (map, `.mtr`, compile shim, textures) is deleted with the pack. The git history keeps it.
- **Docs.** `pbr.game/README.md` becomes `setup/data/gamepacks/pbr/README.md`, the README of the overlay pack. It keeps:
  - installing the overlay;
  - the light entities and units;
  - shading and tonemap;
  - shadows and the caster rule;
  - the engine parity procedure, now on `content/benchmark`'s documented view under `Q3.game`.

  It drops the `.mtr` grammar, the `.mtr` compile shim and the glTF-exporter mapping table (the exporter is abandoned). The plan's references to `install/gamepacks/pbr.game/README.md` move to the new path, and §2.2 step 5 is marked done.
- **Retire the `.mtr` language.** The shaders plugin no longer registers the `pbr` material module. `SHADERLANGUAGE_PBR`, the `.mtr` parser and the `.mtr`-only paths go:
  - the "from `.mtr`" flag;
  - the gamma-free editor image;
  - the `getBump()` suppression;
  - the renderer's lit-and-blended state, which only `.mtr` materials reached.

  `IShader` keeps every PBR accessor, because Quake 3 shaders fill them.
- **Preview preference.** With no `shaders="pbr"` game left, the "PBR lighting preview" preference is never forced on. It is a plain per-game preference for `type="q3"` games.
- **Delete `pbr.game`.** `games/pbr.game` and the `pbr.game/` directory are removed from the pack source, and their installed copies from `install/gamepacks/`. The `pbr` entity module, `games/Q3.game` and `Q3.game/baseq3/_pbr_lights.ent` stay. `_pbr_lights.ent` becomes the only copy of the light definitions.
- **BREAKING:** a `.game` file with `shaders="pbr"` no longer loads. The game dialog no longer offers "PBR". `.mtr` files are no longer read under any game. Doom 3 and Quake 4 `.mtr` materials go through their own languages and are unaffected.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `pbr-gamepack`: describes the overlay pack: its layout (no `pbr.game`), the Quake III build menu with the town group, the README at the pack root, and the parity criterion on the `content/benchmark` view. The benchmark map requirement is removed, because `benchmark-map` owns it.
- `pbr-material-format`: the `pbr` language, the `.mtr` grammar and the `.mtr` classification flags are removed. The baker-source, colour-space, defaults and emissive-strength requirements lose their `.mtr` clauses. Emissive strength is defined without reference to `.mtr`.
- `pbr-lighting-preview`: lit surfaces are Quake 3 shaders only, and the preference has no forced-on case. Scenarios that used `.mtr` keywords are restated with `qer_pbr_*`.
- `pbr-shadow-maps`: the caster rule loses its `.mtr` glass example. It now says what the editor already does: `playerclip`, `botclip` and `trigger` never cast, whether or not the face is `nodraw`.
- `benchmark-map`: the materials requirement states emissive strength without reference to the `.mtr` it was converted from.
- `pbr-light-entities`: the definitions ship only as `Q3.game/baseq3/_pbr_lights.ent`, and the light model is stated for the `pbr` entity module rather than for "`pbr` games".

## Impact

- Code:
  - `plugins/shaders/plugin.cpp`, `shaders.h`, `shaders.cpp`: module, enum value, parser, `.mtr` paths;
  - `radiant/renderstate.cpp` and `renderstate.h`: forced-on preview, blended lit state;
  - `radiant/camwindow.cpp`: preference widget;
  - comments in `include/ishaders.h`, `radiant/cascade.h` and `setup/data/tools/gl/shadow_fp.glsl`.
- Data:
  - `setup/data/gamepacks/pbr/`: delete `games/pbr.game` and `pbr.game/`; add `README.md` and `Q3.game/default_build_menu.xml`; update the comments in `games/Q3.game` and `_pbr_lights.ent`;
  - locally, delete `install/gamepacks/pbr.game/` and `install/gamepacks/games/pbr.game`.
- Docs:
  - `docs/pbr-plan/pbr-plan.tex` (paths, step 5 done) and its PDF;
  - `content/benchmark/README.md` (its pointers to the frozen copy);
  - `CLAUDE.md` (tick A7, retire the "`pbr.game` is frozen" rule).
- Unaffected: q3map2, the engine, the entity plugin, the light code, `content/benchmark/` content, the stock check, and Doom 3, Quake 4 and every other game.
- Exit criterion: the stock check passes on `content/benchmark bench`, and the bench's lighting-mode capture under `Q3.game` matches a capture taken before the change.
