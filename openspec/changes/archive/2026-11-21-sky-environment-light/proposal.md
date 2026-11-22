# Proposal

## Why

The town is an outdoor level under a low sky. Today the editor preview lights it only with the sun, the lamps and a constant "Lighting ambient", so shadowed streets are flat and dark wherever the sun doesn't reach. A real sky lights every surface that can see it. Plan task A12 (§Sky light) defines the sky's radiance once, from the map's sky shader, in the same units as the physical lights (§Units), so that the editor preview, the full bake (B12) and `renderer_sh` (C6) agree. The editor comes first because B12 and C6 reuse its derivation, and because the author tunes light in the preview.

## What Changes

- **Sky radiance from the sky shader.** The map's sky shader (the one declaring `skyparms`; `surfaceparm sky` alone gives no sky light) emits radiance = `qer_pbr_emissiveStrength` × the sky stage's `rgbGen const` × the linear sky image. The image is the `skyparms` farbox's six faces, which cover the whole sphere. Without a farbox, it is the sky stage's image (the first non-`$lightmap` map stage; further cloud stages are ignored), as a uniform upper hemisphere with black below the horizon. The radiance is projected onto real L0–L2 spherical harmonics in Z-up map coordinates, using sh-baker's basis and order. A sky shader without `qer_pbr_emissiveStrength` emits nothing, so legacy maps are unchanged.
- **One sky per map.** The editor finds the sky shaders (those with `skyparms`) on the map's brush faces and patches. When there are several, it uses the alphabetically first and prints one console warning naming all of them.
- **Preview term.** Lit surfaces add unshadowed sky irradiance at their vertex normal (the base pass gains the normal array and turns eye-space normals back to world space): (1 − metallic) × base colour × occlusion × E_sky(n) / π, in the base pass. The editor has no sky visibility, so interiors are overlit. A new camera preference, "Lighting sky" (default on), turns the term off for interior work. "Lighting ambient" stays as it is and adds on top.
- **`qer_pbr_emissiveStrength` on a sky shader** now scales the sky's radiance. Before this change it had no effect on a shader without an additive stage.
- **Pure helpers** go in a header-only library, `libs/skylight.h`, tested by `make test-sky-light`, so that q3map2 (B12) can include the same code:
  - sRGB decode and the image means;
  - the `tr_sky.cpp` face mapping and solid-angle-weighted cube-face projection;
  - the analytic upper-hemisphere projection;
  - cosine convolution and irradiance evaluation;
  - L(ω) for a direction, which bakes use instead of the truncated SH;
  - the alphabetical choice among sky candidates.
- **The town gets sky light.** `textures/town/sky` gains `qer_pbr_emissiveStrength 14.6`. That makes the sky irradiance on an open, upward-facing surface half the town sun's `intensity 5` (largest channel 2.5 against 5). The image, the stages and the fog colour stay the same, so the stock look doesn't change. The town's preview scenarios are restated with "Lighting sky" off where they are about interiors.
- **Documentation.** The gamepack README documents the sky light model (derivation, SH convention and constants, the preference, the overlighting caveat), so that C6 can reimplement it under MIT from the description.

Out of scope: q3map2 and sh-baker (B11 and B12), the side file, `renderer_sh`, sky shadows or visibility in the editor, specular sky reflection, and normal-mapped sky shading.

## Capabilities

### New Capabilities
- `sky-environment-light`: how a map's sky shader becomes an L0–L2 SH radiance and irradiance:
  - choosing the map's sky;
  - the radiance definition for a farbox and for a cloud layer;
  - the projection, convolution and basis convention;
  - the header-only helpers and their tests;
  - the README documentation that B12 and C6 build on.

### Modified Capabilities
- `pbr-lighting-preview`:
  - The base pass adds the sky term.
  - The new "Lighting sky" preference is shown alongside the other lighting preferences.
- `pbr-material-format`: `qer_pbr_emissiveStrength` on a sky shader scales the sky radiance.
- `town-map`:
  - The town sky emits, at half the sun's strength.
  - The interior preview scenario is checked with "Lighting sky" off.

## Impact

- **Editor:**
  - `plugins/shaders/shaders.cpp` exposes the sky stage's image name and `rgbGen const` through `IShader` (`include/ishaders.h`).
  - `radiant/renderstate.cpp` and `setup/data/tools/gl/pbr_base_{vp,fp}.glsl` add the vertex normal, the nine SH uniforms and the sky term to the base pass.
  - `radiant/camwindow.cpp` adds the "Lighting sky" preference.
  - A new sky-gathering step, cached and invalidated on scene change and shader reload, finds and projects the map's sky.
- **New files:** `libs/skylight.h` and `libs/skylight_test.cpp`, plus a `test-sky-light` Makefile target that is kept out of the install tree.
- **Content:**
  - `content/town/scripts/town.shader` (one keyword).
  - `content/town/README.md`.
  - `setup/data/gamepacks/pbr/README.md`.
- **Stock targets:** unaffected. Vanilla ignores `qer_*` keywords, and q3map2's `-shbake` treats the town's sky as non-emissive, because it has no additive stage. A sky with an additive cloud stage plus `qer_pbr_emissiveStrength` would still count as an emitter in q3map2 until B12 excludes sky shaders from its emissive rule; this change documents that and changes nothing in q3map2. The stock check must still pass on the town and the benchmark.
- **Downstream:** B12 (q3map2 builds the full bake's `Environment` from this derivation) and C6 (runtime sky SH × baked visibility) depend on the README's description.
