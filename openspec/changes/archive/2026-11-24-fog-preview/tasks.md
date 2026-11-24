# Tasks

## 1. Baseline

- [x] 1.1 On the branch `fog-preview` (made from `master` at `33c4020d` or later):
  - Build sh-baker's Release library, then the editor and q3map2 with `make SH_BAKER=1`, and install the overlay.
  - Run the stock check on clean copies of the town and the benchmark.
  - Capture the town's `street`, `crossroads` and `nw-room` views and the benchmark's `sign` and `street` views, in lighting mode (shadows on, ambient 0) and in textured mode, with an isolated `HOME` and the scripted capture from the gamepack README's parity section.
  - Capture the town `street` lighting view a second time at camera MSAA 8.

  Verify that both stock checks pass and that a repeated capture is pixel-identical, and record the paths in the PR notes.

## 2. Fog helpers

- [x] 2.1 Write `libs/fogvolume.h`, header-only with no editor dependency, containing:
  - a plain volume struct (outward planes, colour, distance, optional fog-surface plane);
  - the content-shader choice (q3map2's cascade over sides with liquid and fog flags) and the volume order of fog-volume's "Fog volumes" (per-side translucency, `func_group` splicing, other brush entities);
  - the `fogparms` clamps;
  - the fog-surface choice of fog-volume's "The fog surface", from a list of sides (outward normal, `nodraw` and bevel flags, q3map2 side order) and an optional fog direction;
  - the point-in-volume test with its tolerance ε = 1 + depth/512, the last-volume choice, and the 8-volume and 32-plane limits;
  - t(x), the fraction, and the fog amount f;
  - the display-space composite.

  Add `libs/fogvolume_test.cpp` and a `make test-fog-volume` target, kept out of `$(INSTALLDIR)` as `test-sky-light` is. The test includes transcriptions, each citing the ioq3 lines it mirrors, of:
  - rend2's `CalcFog` with its `sqrt`;
  - opengl1's opaque path: `RB_CalcFogTexCoords`, and the 256 × 32 fog image built by `R_FogFactor` and `R_InitFogTable`, sampled with linear filtering.

  Verify that `make test-fog-volume` exits 0, with:
  - a case for each editor-free scenario of the fog-volume spec:
    - "Town volume" from the town's numbers, "Sealed room outside the volume", "Floor flush with a fog face", the three overlap scenarios, "Fog face over a non-solid side" (which is also translucent through its trigger side), "Liquid wins", "Fogged water", "Clamps", and "Too many volumes" and "Too many planes" as the selection rules;
    - "Box over a street", "Explicit direction", "Tie on direction" and "Direction with no opposed side";
    - the four fog-amount scenarios, plus a formula case with the eye below the surface plane and the point above it (f = 0);
    - "Fullbright wall" and "Fog meets the sky" as composite arithmetic;
  - agreement within 1e-5 with `CalcFog` over a grid of eye and point positions, for volumes with and without a fog surface and with the eye on both sides;
  - agreement with opengl1's path within 0.05 in f wherever f ≥ 0.1, for no surface and for the eye inside, with the largest difference printed for the README;
  - the eye-outside differences from opengl1's path printed without a bound.
- [x] 2.2 Add a "Fog" section to `setup/data/gamepacks/pbr/README.md`, covering:
  - the model: volumes, membership and limits, the fog-surface rule and its difference from q3map2, and the formulas with the stock function names;
  - the display-space composite after the tonemap, why, and opengl1 as the reference;
  - every known difference listed in fog-volume's composite requirement, with opengl1's figure from 2.1, and opengl2's own tonemap;
  - the town's volume as a worked example.

  Verify by recomputing f for the town's far facade (depth 1807, eye inside) from the README's text alone, with a few lines of Python, and checking that it matches the 2.1 test.

## 3. Fog parameters in the shader parser

- [x] 3.1 In `plugins/shaders/shaders.cpp` and `include/ishaders.h`, parse `fogparms ( r g b ) d` and `q3map_fogDir ( x y z )`, and expose them through new `IShader` accessors: whether fog parameters exist, the colour, the distance, the optional direction, and the q3map2 content classes (liquid, fog, translucent). A malformed `fogparms` gives one warning per shader and no parameters. Verify with a temporary debug print on scratch shaders:
  - "Town fog" gives (0.200392, 0.258824, 0.231373), 2048 and no direction;
  - the benchmark's fog gives (0.45, 0.45, 0.5) and 1024;
  - a `q3map_fogDir ( 1 0 0 )` shader gives that direction;
  - "Malformed fogparms" warns once and gives none;
  - "Content classes", plus `surfaceparm water` (liquid and translucent) and the town fog (fog and translucent);
  - textured-mode captures are pixel-identical to 1.1.

## 4. The HDR target and the sky

- [x] 4.1 Rebuild the HDR target with direct GL calls (design decision 4): RGBA16F colour, R32F sky depth cleared to −1 each frame, and a `GL_DEPTH_COMPONENT24` depth texture, reallocated on resize. Draw buffer 0 alone by default. Replace the depth blit with a `gl_FragDepth` write from the depth texture in the tonemap pass (`GL_ALWAYS`, depth writes on). Leave the fog out in this task. Verify:
  - "No fog volumes" in its 4.1 form: every 1.1 lighting capture at MSAA 0 is pixel-identical to 1.1, entity boxes, light gizmos and selection overlays included;
  - "Overlays occluded at the default multisampling": a selected brush behind a wall is hidden at MSAA 8 as at MSAA 0;
  - resizing the camera window reallocates without GL errors;
  - "Unsupported context": with the target forced to fail through a temporary switch, lighting mode draws without tonemap or fog, and one warning names both.
- [x] 4.2 Add the lighting-mode sky program and state for shaders declaring `skyparms` (design decision 3): `sky_lit_vp.glsl` and `sky_lit_fp.glsl`, for a farbox (cubemap as `skybox_fp.glsl`) and for a cloud layer (the sky stage's image × its `rgbGen const`). They write the inverse tonemap divided by exposure to attachment 0 and `gl_FragCoord.z` to the sky attachment, with no fixed-function lighting, the existing sort, and a per-frame uniform for the fallback. Stop drawing faces whose shader declares `fogparms` in lighting mode, ahead of the lit, emitter and unlit rules (design decision 9). Verify:
  - "Town sky": every town sky pixel is 51 66 59 within 1, at exposure 1, 0.25 and 4;
  - "Farbox sky": the benchmark's sky pixels against the farbox texels in the drawn directions, using the orientation established in A12;
  - "Glass and glows in front of the sky", on a scratch map with a glass pane and an additive glow against a sky face;
  - "Fog face" and "Lightmapped fog shader": not drawn, and the highlight drawn when selected;
  - a shader with `surfaceparm sky` but no `skyparms` drawn as an ordinary unlit shader;
  - "Exposure changed": doubling exposure leaves the sky's bytes unchanged;
  - the fallback's sky (through the 4.1 switch): unlit at its own bytes;
  - every non-sky, non-fog-face pixel of the 1.1 lighting captures unchanged, compared through a mask;
  - textured mode pixel-identical to 1.1;
  - "Legacy map unchanged" still holding.

## 5. Fog volumes in the preview

- [x] 5.1 Add the fog-volume gatherer of design decision 10 (`radiant/fogvolumes.cpp`). It walks brushes, chooses each one's content shader, and builds the planes and fog surface through `libs/fogvolume.h`. It warns as the limits require, has a dirty flag set by the scene-changed callback, the shader observer and the preference, and updates at the start of each lighting-mode draw. Verify with a temporary debug print on scratch maps under `Q3.game`:
  - "Town volume", with the editor's fog surface +Z;
  - the benchmark's box with its +Z surface;
  - "Explicit direction";
  - the three overlap scenarios (an opaque pair, a translucent pair, and an opaque `func_group` brush under a worldspawn one, the winner as the order rule says);
  - "Fog face over a non-solid side" and "Liquid wins";
  - "Too many volumes" (one warning, repeated only when the count changes) and "Too many planes";
  - a brush with a fog face and solid faces counting as a volume, and a liquid face overriding it;
  - one walk per scene change, not per frame.
- [x] 5.2 Add the fog to `tonemap_fp.glsl` (design decisions 2 and 8):
  - the volume texture, the inverse view-projection, eye and forward vector, taken in `OpenGLShaderCache::render`;
  - the depth and sky-depth textures, with depth 1 and sky pixels (depth equal to the sky depth within 2⁻²³, design decision 3) skipped;
  - a volume count of 0 skipping all fog code.

  Verify on scratch maps by reading window pixels from captures, using an opaque `nolightmap` fullbright wall of known tonemapped value P:
  - "Fullbright wall" at f = 0, 0.5 and 1, within 2 per channel of the composite;
  - "Eye outside above a surfaced volume" against the formula;
  - the overlap scenarios with two colours;
  - "Floor flush with a fog face": the benchmark street floor fogged evenly, with no speckle (no pixel deviating from its neighbours' fog by more than 2);
  - "Opaque surface in front of the sky", with an unlit opaque surface that shares the sky's sort;
  - "Exposure changed": the fog colour's bytes unchanged at exposure 2;
  - "Fog meets the sky", within 1;
  - "Glass in front of the sky" (the glass pixels not fogged);
  - an additive glow over an opaque base in fog matching the composite within 2;
  - "Interiors outside the volume" in the town's `nw-room` view;
  - "No fog volumes" (a scratch map with neither fog nor sky, pixel-identical to the same map before this change);
  - the benchmark's `sign` view, whose camera is inside the benchmark's fog box, fogged with "Lighting fog" on and pixel-identical to its 4.2 capture with it off;
  - the MSAA 8 street capture.
- [x] 5.3 Add the per-game "Lighting fog" preference (default on) in `radiant/camwindow.cpp`, shown with the other lighting preferences, redrawing the camera and dirtying the gatherer. Add the preference to the README's "Fog" section, and add `-Q3.game-LightingFog false` to the README's parity procedure and its command line, because the documented `sign` view is inside the benchmark's fog and `renderer_sh`'s direct-light path has no fog. Also make the procedure ignore sky pixels, since the editor's sky now shows its own bytes and `renderer_sh` has no matching sky until C3a. Verify:
  - "Turned off" (pixel-identical to the same map with its fog brushes deleted);
  - "Fog shader edited" (halving the distance and reloading changes the readback as the formula predicts);
  - that the preference is hidden under a Doom 3 pack.

## 6. Town

- [x] 6.1 Capture the town's `street` and `crossroads` views in lighting mode (shadows on, ambient 0) with "Lighting fog" on and off. Verify:
  - "Town street seam" and town-map's "Editor seam check" (the sky at the horizontal centre near the top, and the far facade just below the ceiling, within 8 per channel; the sky 51 66 59 within 1);
  - that the off capture shows the larger seam.

  Record the numbers in `content/town/README.md` next to the stock seam table, as the editor row. Show the on and off captures to the user before 7.1.

## 7. Integration

- [x] 7.1 Run the stock check on clean copies of the town and the benchmark, and `make test-fog-volume`, `make test-sky-light` and `make test-emissive-light`. Verify that every stock check row is PASS and that all three test targets exit 0.
- [x] 7.2 Update the docs:
  - `CLAUDE.md`: add Track A task A13 (this change, ticked), and mark C3a's fog/tonemap order as decided (fog after the tonemap in display space with opengl1 as the reference, the fog-volume model, the README's "Fog").
  - The plan: the same decision in §World-building's fog item, C3a and the task table, plus A13. Rebuild the PDF with `latexmk -pdf`.

  Format the touched C++ files by the repo policy (`git clang-format` on changed lines, new files whole), and rebuild with `make SH_BAKER=1`. Verify that the build is clean and the PDF builds with no LaTeX warnings and no new overfull boxes. Then ask the user before pushing or opening the PR.

## 8. Follow-ups (from the implementation review, not blocking)

- [ ] 8.1 Key the 32-plane warning by something stable across edits (today `entity N brush M`, so deleting an earlier brush renumbers the over-limit one and it warns again), and profile `FogVolumes_update` on a large map; the walk evaluates every brush's B-rep after each scene change while lighting mode is on.
