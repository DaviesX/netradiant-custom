# Tasks

The gate for each group (design decision 14):

- the editor and q3map2 build with `make SH_BAKER=1`, which links q3map2 against the submodule's Release `libsh_baker.so` (`tools/stockcheck/README.md:27-38`), with no new warnings. Never build with plain `make` between this and the stock check: plain `make` builds q3map2 with `SH_BAKER=0`, and through `light_sh.stamp` (`Makefile:599`, `:632`, `:636-648`) relinks `install/q3map2` against `light_sh_stub.o`, so the stock check's default `--q3map2` (`install/q3map2`, `stockcheck.py:54`) then fails at the light stage with "-shbake: this q3map2 was built without SH_BAKER=1". If a plain `make` did run, run `make binaries-q3map2 SH_BAKER=1` again before the stock check;
- from group 3 on, `make test-emissive-light` passes;
- `clang-format -i` has been run on every C++ file the group touched;
- the stock check (`tools/stockcheck/stockcheck.py`, with `--q3map2` the `SH_BAKER=1` build) passes on a clean `git archive` copy of the benchmark.

The stock check compiles with `-light -shbake` at the defaults, 128 samples and `-super 1`, the same light stage as the "Town (draft)" build; the "Town (pre-release)" (512, `-super 2`) and "Town (final)" (2048, `-super 2`) presets only lower noise and are not part of the gate. Every task's exit criterion includes the stock check. Ask before any push or PR. Scratch maps live under the scratchpad, never in `content/`.

## 1. Baseline

- [ ] 1.1 Create branch `emissive-area-lights` from `master` after updating it to the fork's `master` (`fork/master`, `061c548c` "Land q3map2 -shbake (B5)" or later), so it holds B5. Build sh-baker's Release library, then the editor and q3map2 with `make SH_BAKER=1`, install the overlay, and run the stock check on a clean `git archive` copy of the benchmark. Capture the editor's `sign` view in lighting mode with shadows off and ambient 0, using the scripted capture from the gamepack README's parity section, at the position the stock check recorded. Compile a scratch copy of the benchmark single-threaded with `-meta -keeplights`, `-vis -saveprt` and `-light -patchshadows`, and keep `bench.bsp` as the placeholder baseline. Compile a second scratch copy with `-meta -keeplights`, `-vis -saveprt` and `-light -shbake` at the defaults, and keep its `bench.bsp`, `maps/bench/bench.shsf` and the count in its `textures/bench/neon` unattached-emitter warning (expected 5: the front face, both ends, the top and the bottom) as the `-shbake` baseline. Verify that the stock check passes, and record the screenshot path, both BSP checksums and the warning count in the PR notes.

## 2. LTC tables

- [ ] 2.1 Clone `github.com/selfshadow/ltc_code` into the scratchpad and read its `LICENSE`. If it allows redistribution with attribution, vendor `fit/results/ltc.js` (the 2017 GGX fit, arrays `g_ltc_1` and `g_ltc_2`) and the licence into `tools/ltc/`. Otherwise stop and ask the user (design open question 1). Write `tools/ltc/README.md` with the repository, the file, the commit hash of the clone (recorded from `git rev-parse HEAD`, not from memory), the paper citation the licence asks for, and the layout and indexing of design decision 7, checked against `packTab` in `fit/fitLTC.cpp` and `webgl/shaders/ltc/ltc_quad.fs`. Verify that `tools/ltc/LICENSE`, `tools/ltc/ltc.js` and the README are present and that the README's hash names a commit of the upstream repository.
- [ ] 2.2 Write `tools/ltc/make_ltc_bin.py`, which reads `tools/ltc/ltc.js` and writes `setup/data/tools/gl/ltc_ggx.bin` in the format of design decision 7 (header `LTC1`, 64, 64, 2, then `g_ltc_1` and `g_ltc_2` as two 64 × 64 RGBA little-endian `float32` tables in upstream's entry order, each value the `float32` nearest to the printed decimal). Commit the output. Verify that re-running the script reproduces the file byte for byte, that the file is 16 + 131072 bytes, that every value equals the `float32` of upstream's decimal, and that table 2's third component is 0 everywhere.
- [ ] 2.3 Create `setup/data/tools/gl/ltc_ggx.LICENSE.txt` as a copy of `tools/ltc/LICENSE` (its full copyright notice, conditions and disclaimer, unchanged) followed by the paper citation the licence asks for, so the notice installs next to `ltc_ggx.bin` (design decision 7, "Licence notice"). Commit it. Verify, after `make SH_BAKER=1`, that `install/gl/ltc_ggx.LICENSE.txt` exists beside `install/gl/ltc_ggx.bin` and that its leading part is byte-identical to `tools/ltc/LICENSE`.

## 3. Pure helpers

- [ ] 3.1 Add the header-only `libs/emissivelight.h` with:
  - `AngleVectors` (pitch, yaw, roll) and its inverse from a forward and right pair;
  - the rectangle test (design decision 10);
  - the sRGB decode and the area-weighted footprint mean over a wrapped RGBA8 image, with the optional alpha weight;
  - polygon area, and the clip of a planar polygon to a rectangle with the overlap area;
  - the attachment test of q3map2-shbake, matching `ShAreaRectAttached` (`tools/quake3/q3map2/light_sh_model.cpp:336-388`) applied to one brush face: the normal within 1° of forward, every vertex within 1 unit of the plane, and some triangle of the face's fan triangulation, clipped to the rectangle, covering more than 0.001 square units (a per-triangle test, not the whole polygon's overlap);
  - the radiance split into `intensity` and `_color`, and the created area light's `radius`;
  - the proxy flux with k per surface and the proxy kind (hemisphere spot or point).

  Add `libs/emissivelight_test.cpp` and a `test-emissive-light` Makefile target modelled on `test-q3map2-extension`, kept out of `$(INSTALLDIR)`. Verify that the target passes, with cases for:
  - the derivation spec's "Uniform image", "Footprint, not the whole image", "Tiled face", "No rgbGen const", "Box face", "Collinear vertex", "Trapezoid refused", "Long emitter radius", "Opaque emitter", "Two-sided billboard", "Faces pointing apart", "Two faces under one rectangle" and "Tilted face not attached";
  - "Basis round trip" over 1000 random pairs;
  - the derivation spec's "Triangle overlap decides attachment" (a face whose polygon overlaps the rectangle by more than 0.001 square units while each fan triangle's clipped overlap is at most 0.001 is not attached, as in `ShAreaRectAttached`);
  - the sign basis: `angles 0 270 0` gives forward −Y, right −X and up +Z.

## 4. Entity module and renderer light interface

- [ ] 4.1 In `include/irender.h`, add `RendererLightParams::eArea` and the `right` and `up` half-extent axes, initialised to zero, and update the field comments. Verify that the editor builds (with `make SH_BAKER=1`, per the gate) and that lighting mode on the benchmark matches the 1.1 screenshot pixel for pixel.
- [ ] 4.2 In `plugins/entity/entity.cpp:56-62`, route `light_area` to `New_Light` under `eGameTypePBR`. In `light.cpp`:
  - map the classname to `eArea` (`:1399-1401`);
  - add `width`, `height` and `_frustum` observers with the defaults and fallbacks of the spec;
  - register `_shadows` only for spot and sun;
  - add a `_proxy` observer for point and spot only (the editor ignores the key on `light_sun`; it is read only by Re-derive);
  - add `anglesBasis()` with roll, refreshed by `angles` and `angle`;
  - set the area default `intensity` to 1;
  - make `castsShadows()` mean `_frustum > 0` for area lights;
  - fill `right`, `up`, `cosOuter` and the effective radius max(`radius`, half-diagonal) in `pbrParams()`, and size `pbrAABB()` by the effective radius.

  The `_proxy` observer reads the key as "the value starts with `1`". Verify on a scratch map under `Q3.game` that a `light_area` is a light entity (not generic), that a debug print of its params gives the spec's "Default area light" values and the "Radius clamped to the half-diagonal" radius, that `_shadows 1` on it leaves `castsShadows` false, the scenarios "Frustum enables shadows" and "Proxy key reading", "Proxy renders as an ordinary light" (adding `_proxy 1` to a benchmark `light` leaves the lighting view pixel-identical to the 1.1 screenshot), and "Other game types have no area light" (under a pack using the `quake3` entity module, for example OpenArena, a `light_area` is a generic point entity with no light).
- [ ] 4.3 Add one-sided rejection to `pbrTestAABB` for `eArea`, and the four-corner test to `Face_addLight` (`radiant/brush.h:3101-3109`) for `eArea` lights. Verify the light-entities spec scenarios "Area light culled behind its plane", "Emitter face not lit by its own light" and "Face cutting the rectangle" with a temporary pass-count print on a scratch map, then remove the print.
- [ ] 4.4 Draw `eArea` in `RenderPBRLightShape`: the rectangle outline and normal arrow whether or not the light is selected, in the camera and the 2D views, and the effective-radius sphere and `_frustum` pyramid (out to the effective radius) when it is selected. Grow `localAABB` by the rectangle, and fire `m_boundsChanged()` on `width`, `height`, `angles` and `angle`. Change the `eSpot` gizmo (`light.cpp:1066-1109`) to draw its cone to the radius sphere: the rim centred at radius × cos θ along the axis with rim radius radius × sin θ, θ = min(`cone`, 89.9°), instead of radius × tan θ at distance radius (`:1072`). Verify the scenarios "Roll turns the rectangle", "Size edited", "Target ignored" and "Spot gizmo stays within its radius" by eye on a scratch map, and that the rectangle stays drawn while its centre marker is off-screen.

## 5. Gamepack definitions

- [ ] 5.1 In `setup/data/gamepacks/pbr/Q3.game/baseq3/_pbr_lights.ent`, add `<point name="light_area">` with its keys, defaults and one-line descriptions, plus notes (one-sided, flux πLA, casts only with `_frustum`, ignored by the bake). Add `<boolean key="_proxy" value="0">` to `light` and `light_spot` (not `light_sun`; the light-entities spec "Proxy light key"). Update the header comment and the `games/Q3.game` comment to name four classes. Reinstall the overlay. Verify the spec scenarios "Inspector shows keys", "Quake 3 light definition" and "Area light definition" in the entity inspector.

## 6. Area light pass

- [ ] 6.1 Load `gl/ltc_ggx.bin` into one 64 × 128 `GL_RGBA32F` texture on unit 7 in `OpenGLShaderCache::realise()`, and delete it in `unrealise()`. Bind it each frame from `Cam_Draw` in PBR lighting mode through a new `ShaderCache_bindLightingTables()`, whatever the shadow preference. Set its sampler uniform before `GLSLProgram_validate`. Check `GL_ARB_texture_float` (or GL 3.0) and 8 texture image units, and warn once on failure. Verify that the texture is created once per realise, that toggling the preview off and on leaks no texture (count in a debug build), and that the "Missing table file" scenario prints one warning and still draws.
- [ ] 6.2 In `GLSLPBRProgram::setLightParams`, upload type 4 and the object-space half axes (`u_light_half_right`, `u_light_half_up`, not normalised). In `pbr_fp.glsl`, add the area branch of design decision 8: the one-sided and radius rejection, the horizon clip with constant indices, the normalised form factor (upstream's `IntegrateEdge`) for diffuse, LTC GGX for specular with the table-2 Fresnel, and diffuse only when the tables are unavailable. Keep the file free of `#version` and of GLSL 1.20-only functions. Verify:
  - the shading formula against the preview spec's "Area lights are shaded with linearly transformed cosines" term by term: (1 − metallic) × base × FF with no 1/π and no (1 − F), S weighted by F0 × t2.x + (1 − F0) × t2.y, the corner order and frame, and the table coordinates;
  - the spec scenarios "Nothing behind the light" and "Highlight on a glossy floor" on scratch maps;
  - "Matches the point model far away" and "Close to the rectangle" by reading back the linear HDR value of a white Lambertian wall (diffuse term only, via the fallback path or a debug switch) against its outgoing radiance L × FF, 0.002033 and 0.2395, within the scenarios' tolerances;
  - "Area light without a frustum ignores occluders";
  - the light-entities scenario "Area light under Quake 3" with the overlay installed;
  - the point, spot and sun passes still match the 1.1 screenshot pixel for pixel with no area light present.

## 7. Area shadows

- [ ] 7.1 In `Shadow_generate` (`radiant/renderstate.cpp:2142-2158`), push `eArea` lights that cast into the spot list. Keep `Shadow_generateSpot` driven by `cosOuter`, and change the over-subscription warning to "spot and area lights". In `pbr_fp.glsl`, take the area light's shadow NdotL toward the rectangle's centre with penumbra 1. Verify the shadow-maps spec scenarios "Pillar shadow from the sign", "Emitter face does not shadow itself", "Frustum edited" and "Atlas exhausted" (17 casting lights, mixing spots and area lights) on scratch maps.

## 8. Emitters in linear HDR

- [ ] 8.1 In `plugins/shaders/shaders.cpp` and `include/ishaders.h`, add `hasEmissive()`, `getEmissiveName()`, `isAllAdditive()` and `getEmissiveSrcAlpha()`, and extend `loadsLightingTextures()` to shaders with an emissive stage that are all-additive or `nolightmap`. Add fixtures to `content/fixtures/`: an additive flame, a `SRC_ALPHA ONE` glow, a `nolightmap` sign and a black `rgbGen const` glow. Verify with a temporary dump that the accessors give the expected values for these fixtures and for `textures/bench/neon`, and that the parse count and errors match `content/fixtures/baseline.txt`.
- [ ] 8.2 In `OpenGLShader::construct`, add the emitter branch of design decision 12, and turn `GLSLPBRMaterialProgram`'s `m_base` into an enum that covers the additive and opaque emitter kinds. Verify:
  - the preview spec scenarios "Additive flame" and "Nolightmap sign" with the fixtures;
  - that "Sky" and "Blended glass" draw as before;
  - that textured mode is unchanged for the fixtures (screenshots before and after).

## 9. Commands

- [ ] 9.1 Add `Patch::getTesselation()` (read-only), then the three commands and their menu items, shown only for `entities="pbr"`, in `radiant/entity.cpp`, using `libs/emissivelight.h`, `IShader::getEmissiveName()` and `GlobalTexturesCache().loadImage()`. Each command is one `UndoableCommand` and creates entities with `GlobalEntityCreator().createEntity`. Create proxy and Re-derive on a proxy share one gathering function (design decision 11). Verify every scenario of the `emissive-light-derivation` spec on scratch maps, the benchmark sign included ("Benchmark sign": compare `intensity` with the helper's footprint mean × 8, and `radius` 256), and "Spot proxy on its axis" by reading the wall's irradiance from the spot pass through a temporary debug switch that outputs E × NdotL instead of the outgoing radiance (removed afterwards). Verify too that "Hidden in other games" holds under a Doom 3 pack.

## 10. q3map2 placeholder and stock check

- [ ] 10.1 In `tools/quake3/q3map2/light.cpp` `CreateEntityLights` (`:327-335`), skip `light_area`, and `light`, `light_spot` and `light_sun` with `boolForKey( "_proxy" )`, the set `ShIsAttachedEmitterLight` leaves out of `-shbake` (design decision 13). In `tools/quake3/q3map2/light_sh_model.cpp:268-269`, change the comment's stale reference "emissive-area-lights design.md:249" to "emissive-area-lights design decision 13" (a comment-only change). Rebuild with `make SH_BAKER=1` (or `make binaries-q3map2 SH_BAKER=1`). Verify the light-entities spec scenarios "Area light adds no placeholder light" and "Proxy adds no placeholder light" on scratch maps, that the 1.1 placeholder baseline is byte-identical apart from the version stamp, and that the 1.1 `-shbake` baseline is byte-identical too.
- [ ] 10.2 Add `^light_area doesn't have a spawn function$` to `tools/stockcheck/allowlist.txt`, with a comment in the style of the spot and sun entries. Update `tools/stockcheck/README.md` ("The allowlist") to name three entries. Verify the stock-check spec scenario "Light entities in the map" with a scratch copy of the benchmark holding a `light_area`, and that "Unknown entity class" still fails.

## 11. Benchmark sign

- [ ] 11.1 Open `content/benchmark/maps/bench.map` under `Q3.game`, select the sign's front face (`bench.map:228`), and run "Create area light from selected face". Save. Update `content/benchmark/README.md`: the entity count (20), the light table row for the `light_area`, and the allowlist paragraph. Verify:
  - the benchmark spec scenarios "Entity inventory", "Lights preview under Q3.game" and "Sign light matches its emitter";
  - the preview spec scenario "Neon sign lights the wall", captured from the `sign` view as in 1.1 and saved beside the 1.1 screenshot;
  - "Entities kept" in the compiled BSP;
  - q3map2-shbake's scenario "Area light ignored silently" on the real benchmark: compile scratch copies with `-meta -keeplights`, `-vis -saveprt` and `-light -shbake` at the defaults, with and without the sign's `light_area` (the 1.1 `-shbake` baseline is the copy without it). The lightmap, light-grid and draw-vertex lumps and `maps/bench/bench.shsf` are byte-identical (the entity lumps differ by the light), no warning line names `light_area`, and the `textures/bench/neon` unattached-emitter warning counts 4 (both ends, the top and the bottom) against the baseline's 5;
  - that the stock check passes on a clean `git archive` copy.

## 12. Documentation and close

- [ ] 12.1 Update `setup/data/gamepacks/pbr/README.md` per the gamepack spec:
  - the layout line;
  - the light table with `light_area` and `_proxy`;
  - the area model and LTC section: the shading formula, the upstream repository, file and commit, the licence's name, the paper citation it asks for and a pointer to the installed notice `gl/ltc_ggx.LICENSE.txt` (task 2.3; the README itself is not installed, so it is not the shipped notice), the layout and indexing, the SHA-256 of `ltc_ggx.bin`, that `renderer_sh` reads the same file unchanged, and the diffuse-only fallback;
  - the derivation, the commands, the proxy kinds and their on-axis residual (about 2× dim);
  - shading (emitter drawing);
  - shadows (the `_frustum` row and the shared atlas);
  - the build menu note on the placeholder skip, naming the classes and the "starts with `1`" reading.

  Verify the spec scenarios "Reader can reproduce the area light" and "Reader can reproduce the scheme" by checking every constant in design decisions 5, 7-9 and 11 against the README.
- [ ] 12.2 Update `CLAUDE.md`: the ground rule on spawn-function lines names `light_area` too, and A11 is ticked. Update the plan's §2.2 step 6 status to done, and rebuild the PDF with `latexmk -pdf`. Run `clang-format -i` on every touched C++ file, and rebuild with `make SH_BAKER=1` (not plain `make`, per the gate). Verify the exit criterion, which is that the sign lights the wall in the preview (11.1 screenshot) and the stock check passes on a clean `git archive` copy. Then ask the user before pushing or opening the PR.
