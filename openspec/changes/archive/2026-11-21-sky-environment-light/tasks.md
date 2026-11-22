# Tasks

## 1. Baseline

- [x] 1.1 Starting from the branch `sky-environment-light` (made from `master` at `f2c46c87` or later), do the following:
  - Build sh-baker's Release library, then the editor and q3map2 with `make SH_BAKER=1`, and install the overlay.
  - Run the stock check on clean `git archive` copies of the town and the benchmark.
  - Capture the town's `street`, `crossroads` and unlit-interior views, and the benchmark's `sign` view, in lighting mode with shadows on and ambient 0. Use the scripted capture from the gamepack README's parity section, with an isolated `HOME`.

  Verify that both stock checks pass, and record the screenshot paths in the PR notes.

## 2. Sky light helpers

- [x] 2.1 Write `libs/skylight.h`, header-only with no editor dependency, containing:
  - a plain `SkySource` struct (farbox name, sky stage image name, stage colour, declared strength);
  - L(ω) for a direction (design decision 8b);
  - the sRGB decode, reusing `emissivelight.h`'s if it can be shared without coupling;
  - the image mean;
  - the `tr_sky.cpp` face-to-direction mapping, the solid-angle weights, and the cube and analytic upper-hemisphere projections;
  - the Âₗ convolution and E(n) with the clamp;
  - the alphabetical candidate choice.

  Add `libs/skylight_test.cpp` and a `make test-sky-light` target, kept out of `$(INSTALLDIR)` as `test-emissive-light` is. Verify that `make test-sky-light` exits 0 with a case for each editor-free scenario of the `sky-environment-light` spec: "Legacy sky", "Strength and colour", "Uniform farbox", "Up face only", the irradiance half of "Orientation matches the drawn sky", "Open floor", "Uniform sphere", "Radiance below the horizon" and the choice in "Two skies".
- [x] 2.2 Check the face mapping in `skylight.h` against `ioq3/code/renderergl1/tr_sky.cpp` (`st_to_vec`, the suffix table and the t flip), and add a test that a texel at each face's (s, t) corners maps to the direction `MakeSkyVec` gives. Verify that the test passes and cites the `tr_sky.cpp` lines it mirrors.

## 3. Shader accessors and sky gathering

- [x] 3.1 In `plugins/shaders/shaders.cpp` and `include/ishaders.h`, add `getSkyBoxName()`, `getSkyStageName()`, `getSkyStageColour()` and `hasEmissiveStrength()` (design decision 5), filled during Quake 3 stage parsing. Verify with a debug print on scratch shaders:
  - `textures/town/sky` gives "", `textures/town/sky`, (1, 1, 1) and false before the town change;
  - a farbox shader gives its farbox path;
  - a stage with `rgbGen const` gives that colour;
  - a cloud sky with a second `blendFunc add` stage gives the first stage's name and colour;
  - textured mode is pixel-identical to the 1.1 captures' textured counterparts.
- [x] 3.2 In the editor, add the sky gatherer of design decision 4: a scene walk over brushes and patches, a dirty flag set by the scene-changed callback, shader realise or unrealise and the preference, the projection through `skylight.h`, and the warnings ("Two skies", "Missing face", missing cloud image). Verify on scratch maps under `Q3.game`, with the result read through a debug print of the nine coefficients:
  - the scenarios "One sky", "Two skies" (one warning, repeated only after a third sky is added), "surfaceparm sky alone", "No sky", "Missing face" and "Second cloud layer ignored" (the nine coefficients equal those of the same shader without the second stage);
  - "Orientation matches the drawn sky", using a six-face fixture with a distinct colour per face, in which the camera looking along +X in textured mode sees the `_rt` face and the gatherer's E is largest at +X;
  - one walk per scene change, not per frame (a debug counter).

## 4. Preview term and preference

- [x] 4.1 Add `RENDER_LIGHTING` to the preview's lit base state, so the normal array is on. Add the sky term to `pbr_base_vp.glsl` and `pbr_base_fp.glsl`: the world-space normal as `u_eye_to_world × normalize(gl_NormalMatrix × gl_Normal)`, with `u_eye_to_world` the transposed rotation of the `modelview` passed to `OpenGLShaderCache::render`, updated every lighting-mode frame whatever the shadow preference (not `g_shadowCameraView`; design decision 2), nine `vec3` uniforms, and the metallic read from the metallic-roughness map × the factor (design decisions 1–3). Every `enable()` of the shared base program sets the sky coefficients through `setMaterial`: the gathered ones for `eBase` (zeros when "Lighting sky" is off or there is no sky light), zeros for `eAdditiveEmitter` and `eOpaqueEmitter`. Verify the `pbr-lighting-preview` scenarios on scratch maps, reading linear radiance through a temporary debug readback of the HDR framebuffer (removed afterwards):
  - "Open floor under a cloud sky" (within 1%);
  - "Ceiling sees no sky light";
  - "Rotated model";
  - "Interiors are overlit";
  - "Legacy map unchanged" (the benchmark `sign` view is pixel-identical to 1.1);
  - an emitter and a sky surface unchanged, including an emitter drawn right after a sky-lit surface;
  - the sky term correct with "Lighting shadows" off;
  - textured mode pixel-identical to its baseline with the preview on and off, so the `RENDER_LIGHTING` change touches only the lit base pass.
- [x] 4.2 Add the per-game "Lighting sky" preference (default on) in `radiant/camwindow.cpp`, shown and hidden with the other lighting preferences, redrawing the camera and dirtying the sky gatherer. Verify:
  - "Turned off" (pixel-identical to a capture with the keyword removed);
  - "Shader edited" (the readback doubles after a shader reload);
  - that the preference is hidden under a Doom 3 pack.

## 5. Documentation

- [x] 5.1 Add a sky light section to `setup/data/gamepacks/pbr/README.md`, covering:
  - which shader counts, and the radiance formula with the declared-strength rule;
  - the farbox face mapping and in-face texel orientation, as a table;
  - the cloud hemisphere, the SH basis, order, constants and Âₗ;
  - the base-pass formula, the preference and the overlighting caveat;
  - that bakes use L(ω), not the SH, that `surfaceparm sky` without `skyparms` and second cloud stages give no sky light, and that q3map2 counts a sky with an additive stage and a strength as an emitter until B12;
  - the town's strength worked as an example.

  Note in the preview section that ambient and sky add together. Verify the `sky-environment-light` scenario "README section", and that a reader can reproduce the town's 2.4989 from the README's formulas alone (recompute it with a few lines of Python taken from the README, not from `skylight.h`).

## 6. Town

- [x] 6.1 Add `qer_pbr_emissiveStrength 14.6` to `textures/town/sky` in `content/town/scripts/town.shader`, with a comment that ties it to the sun. In `content/town/README.md`, add the sky light to the materials and lights tables, with the derivation from the sun's `intensity 5`, and restate the interior preview check as made with "Lighting sky" off. Add a test case in `libs/skylight_test.cpp` that reads the committed `content/town/textures/town/sky.tga` with a minimal TGA reader in the test file (uncompressed true-colour, type 2, 24 or 32 bits, either row order, as Pillow writes it; anything else fails the test with a message), so the test stays editor-free, and the sun's `intensity` from `tools/town/town1_lights.json`, and asserts that E's largest channel at +Z is half the sun within 1% ("Half the sun"). Verify:
  - that `make test-sky-light` passes;
  - that `gen_textures.py` still runs clean (the fog check is unaffected);
  - that the editor prints no rend2-keyword warning for `town.shader`.
- [x] 6.2 Capture the town's `street` and `crossroads` views in lighting mode with shadows on and ambient 0, once with "Lighting sky" on and once off, with an isolated `HOME`. Capture the unlit interior with the sky off and shadows on. Verify:
  - "Streets in sun shadow are lit by the sky": with the sky on, region means of a sunlit and a sun-shadowed street patch are both higher, while the sky and fog regions differ by at most 1/255;
  - "Lights preview under Q3.game" holds with the sky off;
  - that the captures are saved beside the 1.1 baseline and listed in the PR notes.

  Show the on and off captures to the user for a look before 7.1.

## 7. Integration

- [x] 7.1 Run the stock check on clean `git archive` copies of the town and the benchmark, and `make test-sky-light` and `make test-emissive-light`. Verify:
  - that every stock check row is PASS ("Stock unchanged");
  - that the town's `-light -shbake` log names `textures/town/sky` in no warning;
  - that both test targets exit 0.
- [x] 7.2 Update `CLAUDE.md` (tick A12 and point it at this change; add to B12 that q3map2's emissive rule must exclude sky shaders and that it hands sh-baker L(ω), not the SH) and the plan's §Sky light status. In CLAUDE.md's A12 entry and in `pbr-plan.tex` (the sky emission line near 488 and §Sky light's Source near 1528), change the sky source from `surfaceparm sky` to the shader that declares `skyparms`, and note that the sky stage alone counts. Then rebuild the PDF with `latexmk -pdf`. Format the touched C++ files by the repo policy (`git clang-format` on changed lines, new files whole) and rebuild with `make SH_BAKER=1`. Verify that the build is clean and the PDF builds without warnings. Then ask the user before pushing or opening the PR.
