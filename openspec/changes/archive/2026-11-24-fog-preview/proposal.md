# Proposal

## Why

Fog is the town's main look (plan §World-building: "Fog is the primary aesthetic"). The author tunes `fogparms` against the sky, but the editor's lighting mode doesn't render fog. It draws a fog volume as a translucent box, and draws the sky lit by the camera headlight and through the tonemap, so the town's sky doesn't show `sky.tga`'s 51 66 59, the value opengl1 shows. The editor preview therefore can't show the effect the stock renderers show, or the fog seam the town's "Fog colour equals sky colour" rule exists for. The plan also leaves open where fog goes relative to the tonemap, for `renderer_sh` (C3a). The editor is where the author judges fog, so the decision belongs here.

## What Changes

- **Fog volumes in lighting mode.** A brush whose content shader (q3map2's rule: a fog face wins over solid faces) declares `fogparms` fogs what lies inside it with the stock formula. A pixel whose nearest depth-writing surface lies inside the volume blends towards the fog colour by √(min(depth × fraction / d, 1)), where:
  - depth is the distance along the view axis and d is the `fogparms` distance;
  - fraction is 1 when the volume has no fog surface; with one, it depends on which side of the surface's plane the eye and the point lie (the point must be below it when the eye is, and when the eye is above, it is the share of the view ray below the plane), as in `tr_shade_calc.cpp`'s `RB_CalcFogTexCoords`.

  The fog surface is the side named by `q3map_fogDir`, otherwise q3map2's axial priority (+Z, −Z, +Y, −Y, +X, −X). Volumes are ordered as q3map2 orders fog brushes, and where they overlap the last wins, as q3map2 assigns it. A point within a small depth-scaled tolerance of a fog face counts as inside. Faces whose shader declares `fogparms` are no longer drawn in lighting mode.
- **Fog after the tonemap (decision).** Fog is applied in display space after the tonemap curve and the sRGB encode, inside the resolve, as opengl1 blends fog onto 8-bit pixels. The `fogparms` colour the author picks is therefore exactly the colour on screen. The decision is recorded in the plan for `renderer_sh`'s C3a. opengl2 at default cvars fogs before its own auto-exposure tonemap, so across the stock renderers it is the seam that carries over, not the exact colours.
- **The sky shows its own bytes.** In lighting mode a sky (a shader declaring `skyparms`) stays in its usual place in the HDR pass, so glass and glows still blend over it. It is drawn unlit, with a value the tonemap maps back to exactly the sky image's bytes: the 8-bit texel × the sky stage's `rgbGen const`, decoded from sRGB, inverse-tonemapped and divided by exposure. A cloud layer uses its sky stage's image. It also records its own depth, so that the fog leaves sky pixels alone but still fogs any opaque surface drawn in front of them. This changes how the sky looks in lighting mode even without fog: for the town, 51 66 59 instead of a headlit, tonemapped value.
- **The HDR target grows.** It gains a depth texture and a sky-depth attachment that the resolve samples, replacing today's depth renderbuffer. The resolve writes the scene's depth into the window itself instead of blitting it. The blit fails at the camera's default MSAA of 8, so overlays in lighting mode are now occluded there too.
- **New preference "Lighting fog"** (per game, default on), shown with the other lighting preferences. Off removes the fog and nothing else. Without the HDR target (the existing fallback) there is no fog, and the sky draws unlit at its own colour, texel × `rgbGen const`.
- **Pure helpers** go in a header-only library, `libs/fogvolume.h`: the fog factor, the fraction and the visible-side choice. It is tested by `make test-fog-volume` against a transcription of `RB_CalcFogTexCoords`, `R_FogFactor` and rend2's `CalcFog`.
- **Documentation.** The gamepack README gains a "Fog" section with the model, the visible-side rule, the tonemap order and the known differences from stock: per-pixel versus per-surface assignment, the editor never fogging the sky, glass and all-additive surfaces fogged by the depth behind them, and the fog surface where q3map2 would skip a hidden side. The town README records the editor's fog seam. The plan and `CLAUDE.md` gain the decision and a new Track A task, A13.

Out of scope: `renderer_sh` (C3a implements the same model there), q3map2, global fog (`_fog` on worldspawn), `fogparms` on non-brush geometry, and fog on overlays and highlights.

## Capabilities

### New Capabilities
- `fog-volume`: how a Quake 3 fog volume fogs a point. Covers:
  - the fog factor and the fog-surface fraction;
  - the visible-side rule and the volume membership test;
  - the order of fog and tonemap;
  - the header-only helpers and their tests;
  - the README documentation that `renderer_sh` (C3a) builds on.

### Modified Capabilities
- `pbr-lighting-preview`:
  - The tonemapped resolve gains the fog and writes depth, and the HDR target gains depth and sky-depth attachments.
  - Exposure no longer applies to the sky or the fog colour.
  - Fog faces are not drawn, whatever the other preview rules say.
  - Sky surfaces draw unlit, with the value that tonemaps back to their bytes.
  - "Legacy map unchanged" under "Sky light in the base pass" is restated relative to "Lighting sky" off.
  - The new "Lighting fog" preference is listed with the other lighting preferences.
  - New requirements cover fog volumes in the preview and the fog preference.
- `pbr-material-format`: `fogparms` (colour and distance) and `q3map_fogDir` are read by the editor's Quake 3 parser.
- `town-map`: "Fog colour equals sky colour" gains an editor seam check in lighting mode.

## Impact

- **Editor:**
  - `plugins/shaders/shaders.cpp` and `include/ishaders.h`: `fogparms` colour and distance, and `q3map_fogDir`, through new `IShader` accessors.
  - A new fog-volume gatherer, cached and invalidated like the sky light's.
  - `radiant/camwindow.cpp`: the HDR target built with depth and sky-depth textures, the resolve's depth write in place of the blit, the fog volumes uploaded for the resolve, and the "Lighting fog" preference.
  - `radiant/renderstate.cpp`: the lighting-mode sky program and state, fog faces not drawn, and the fog inputs to the tonemap program.
  - `setup/data/tools/gl/tonemap_fp.glsl` gains the fog; new `sky_lit_vp.glsl` and `sky_lit_fp.glsl`.
- **New files:** `libs/fogvolume.h`, `libs/fogvolume_test.cpp`, and a `test-fog-volume` Makefile target kept out of the install.
- **Content:** none. The town's and the benchmark's fog shaders are already stock-correct. `content/town/README.md` gains the editor seam numbers.
- **Docs:** `setup/data/gamepacks/pbr/README.md`, `docs/pbr-plan/pbr-plan.tex` (and the PDF), `CLAUDE.md`.
- **Stock targets:** unaffected, because nothing in the pk3 changes. The stock check must still pass.
- **Downstream:** C3a (`renderer_sh` fog) follows the recorded model and order.
