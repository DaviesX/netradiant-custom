# Design

## Context

The motivation is in proposal.md. Current state:

- **Parsing.** `plugins/shaders/shaders.cpp` sets `QER_FOG` for `surfaceparm fog` (line ~1707) and reads neither `fogparms` nor `q3map_fogDir`. Fog shaders are kept out of the preview's lit branch by `m_previewLit` (shaders.cpp ~1823) and out of the emitter branch by `renderstate.cpp` ~3365. In lighting mode a fog brush therefore draws its textured-mode state, a translucent `qer_trans` box, into the HDR target.
- **The sky in lighting mode.** A farbox sky uses the skybox program at `eSortFullbright` (`renderstate.cpp` ~3425). A cloud-layer sky like the town's has no cubemap and takes the default textured state (~3442), also at `eSortFullbright` when opaque. That state includes `RENDER_LIGHTING`, so the fixed-function camera headlight shades it: light 0 has ambient 0.4 and diffuse 0.4 along the view, on top of the model ambient of 0.2 (`camwindow.cpp` ~2181). Both kinds then go through exposure and the tonemap. Within a sort, states order by texture handle (`OpenGLState_less`), so the sky and other unlit opaque states draw in an arbitrary order.
- **The resolve.**
  - `OpenGLShaderCache::render` calls the resolve hook (`CamHDRResolve`) before the first state with `m_sort >= eSortHighlight` (`renderstate.cpp` ~1749).
  - `CamWnd::HDR_resolve` blits depth into the window target, swallowing errors (`camwindow.cpp` ~2086), then draws the tonemap (`tonemap_fp.glsl`: exposure, Hable U(x), ldr = U(x) / U(W) clamped, exact sRGB encode written to a non-sRGB window).
  - The window target is multisampled at the camera's default MSAA of 8 (`camwindow.cpp` ~173, ~1680). A blit into a multisampled draw framebuffer is `GL_INVALID_OPERATION`, so at the default setting the depth never arrives today, and overlays drawn after the resolve aren't occluded in lighting mode.
  - The HDR target is a `QOpenGLFramebufferObject` with one RGBA16F colour texture and `Attachment::Depth`, a renderbuffer that a shader can't sample.
  - Without framebuffer objects, lighting mode renders straight to the window and warns once ("Framebuffer availability fallback").
- **Stock fog, opengl1.**
  - `RB_CalcFogTexCoords` (`tr_shade_calc.cpp`) gives each vertex s = (view-axis depth) × tcScale, with tcScale = 1/(8 d) and d raised to 1 (`tr_bsp.cpp` ~1735). It gives t from the fog surface plane: eyeT = 1 when there is no surface, and the eye-outside branch scales by t / (t − eyeT).
  - Opaque surfaces get a fog pass that samples the 256 × 32 `tr.fogImage` with `GL_LINEAR`, at those per-vertex coordinates. Each texel is `R_FogFactor` (`tr_image.cpp`), which is √(min(8 s × fraction, 1)) through a 256-entry table. The pass blends `fogparms` × `identityLight` (1 at default windowed cvars, `ColorBytes4`) with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` onto 8-bit pixels.
  - When a shader's first stage is blended, its stages are modulated by the fog instead of getting a pass.
  - A volume's visible faces are drawn as fog-only surfaces with an `FP_LE` fog pass.
  - A sky's cloud layer is fogged when its surface lies in a volume, at the cloud box's distance. A farbox is never fogged. `isSky` comes only from `skyparms`.
- **Stock fog, opengl2.** `CalcFog` in `generic_vp.glsl` and `fogpass_vp.glsl` is the same model with an analytic √, still per vertex. At default cvars rend2 fogs inside its HDR framebuffer and then applies its own auto-exposure tonemap. The town's sky measured 60 80 71 there, against 51 66 59 on opengl1.
- **q3map2.**
  - **Content shader** (`map.cpp` `SetBrushContents`): the first side's shader. A liquid side replaces any non-liquid content shader. A fog side replaces any shader that is neither liquid nor fog. Playerclip and solidity only decide among non-fog shaders. A brush is a fog volume when its content shader has `fogparms` (`fog.cpp` ~576).
  - **Order:** a brush is opaque unless any side sets `C_TRANSLUCENT` (`map.cpp` ~537; `games.cpp`: `trans`, `trigger`, `playerclip`, `monsterclip`, `alphashadow`, `lightfilter`, `nodrop`, `botclip`, `clusterportal`, `donotenter`, `areaportal`, `origin`, and the liquids). `FinishBrush` puts opaque brushes at the head of their entity's list, reversing them, and appends translucent ones (`map.cpp` ~822). `MoveBrushesToWorld` then splices each `func_group`'s opaque brushes to the head of the world list and appends its translucent ones (~1264). The result is: groups' opaque brushes (later groups first, map order within), worldspawn's opaque brushes reversed, worldspawn's translucent brushes, then the groups' translucent ones. `CreateMapFogs` walks that list, then the other brush entities. A later fog overwrites an earlier one's assignment.
  - **Fog surface:** `q3map_fogDir` picks the first non-bevel side, in index order −X, +X, −Y, +Y, −Z, +Z and then the rest, with a strictly most negative dot product, starting from 0 (`fog.cpp` ~586). When none is negative, the following rule applies instead: the first axial side from j = 5 down to 0 (+Z, −Z, +Y, −Y, +X, −X) with a non-empty `visibleHull` and not `nodraw`, then any other side, else −1 (`writebsp.cpp` ~425). The town's volume has every face inside the hull, so it has no surface.
  - **Assignment** (`FogDrawSurfaces`, `FogForBounds`):
    - brush faces are chopped at the fog brush;
    - patches are split only at its ±Z planes;
    - world triangle and meta surfaces (models) are fogged whole by the last volume their bounds touch;
    - brush-model surfaces take the volume with the largest bounding-box overlap;
    - faces flush with a fog side and facing out of it are not fogged;
    - `q3map_noFog` surfaces never are.
- **Precedent.** `radiant/skyenvironment.cpp` is a cached scene walker, invalidated by the scene-changed callback and the shader observer. `libs/skylight.h` holds the pure helpers with a test target outside the install. The A12 accessors already give the sky stage's `rgbGen const` (`getSkyStageColour`).

## Goals / Non-Goals

**Goals:**
- Show the author the fog opengl1 shows, in the preview where they tune it, with the `fogparms` colour on screen exactly as authored and the sky at its own bytes.
- Keep glass, glows and other translucent surfaces blending over the sky as stock does.
- Settle the fog/tonemap order for `renderer_sh` (C3a), with a written model it can reimplement.
- Cost no extra full-screen pass, and recompute volumes only when the map or shaders change.

**Non-Goals:**
- Global fog.
- q3map2's per-surface assignment, `visibleHull` test and non-axial side order.
- Fog faces.
- `q3map_noFog`.
- Fogging the sky.
- Fogging translucent or all-additive surfaces by their own depth.
- Fogging overlays.
- opengl2's colour processing.
- A cloud layer's dome mapping and extra cloud stages.

## Decisions

1. **Fog after the tonemap, in display space.** opengl1 blends 8-bit fog colour over 8-bit pixels. Doing the same after the editor's tonemap keeps the authored `fogparms` colour exact on screen and keeps "fog colour equals sky colour" true in the preview. opengl2 fogs before its own tonemap, so across the stock renderers it is the seam that carries over, not the exact colours. opengl1 is the reference for bytes. The decision is recorded in the plan for C3a.

   Rejected: fog in linear HDR before the tonemap. The curve would bend the fog colour away from what the author set.

2. **The fog is computed in the tonemap shader, which also writes depth.** `tonemap_fp.glsl` keeps its curve and encode. Then, for pixels where depth < 1 and the pixel is not sky (decision 3), it:
   - reconstructs the world position from depth and the inverse view-projection, both taken in `OpenGLShaderCache::render` like `g_pbrEyeToWorld`;
   - finds the volume (decision 5);
   - computes f with the fog-volume formulas;
   - outputs mix(encoded, C, f).

   The same pass writes `gl_FragDepth` from the depth texture, with the depth test set to `GL_ALWAYS` and depth writes on. That replaces the blit, so overlays are occluded at any window multisampling, MSAA 8 included, which also fixes today's silent loss.

   With no volumes, or with "Lighting fog" off, the fog code is skipped by a uniform count of 0. The colour output is then bit-identical to today's resolve.

   Rejected:
   - A separate blended pass after the resolve. It costs more and needs the window target's depth.
   - Fogging in every material pass. Light passes are additive.

3. **The sky stays in the HDR pass, written as its inverse tonemap, and marks its own depth.** In lighting mode, shaders declaring `skyparms` get a dedicated program and keep their current sort. Translucent surfaces still blend over them later. `surfaceparm sky` without `skyparms` is not a sky in stock and stays an ordinary unlit shader. The program:
   - samples the farbox cubemap as `skybox_fp.glsl` does, or for a cloud layer the sky stage's image (A12's `getSkyStageName`, captured as a texture) at the face's texture coordinates;
   - multiplies the 8-bit texel by the sky stage's `rgbGen const` (white for a farbox), as stock does;
   - decodes sRGB, so y = decode(texel) × U(W);
   - writes x = U⁻¹(y) / exposure to attachment 0, the root of A(1 − z)x² + B(C − z)x + D(E − zF) = 0 with z = y + E/F, in [0, W] for texels from 0 to 255;
   - writes its fragment depth (`gl_FragCoord.z`) to the R32F sky attachment.

   The resolve treats a pixel as sky when the depth texture equals the sky attachment within 2⁻²³, about two steps of a 24-bit buffer, because the two differ only by quantisation. An opaque surface drawn later in front of the sky writes a nearer depth and so unmarks the pixel, whatever the draw order. A translucent surface writes no depth, so the pixel stays sky. The program has no fixed-function lighting, so the headlight is gone.

   A per-frame uniform tells the program whether the HDR target is bound. The fallback applies when framebuffer objects, any of the three attachments or two draw buffers are unavailable, or the framebuffer is incomplete. Without it (the fallback) it writes the plain texel × `rgbGen const` to the window and nothing else.

   Rejected:
   - Drawing the sky after the resolve. Glass and glows in front of it write no depth, so it would paint over them.
   - A sort slot after all opaque states. `ESort` has no free value there.
   - A plain 0/1 mask. Opaque overdraw in the same sort would leave it set.
   - Fogging cloud skies as stock does at the cloud box's distance. The town's rule makes the result invisible, and the editor's sky surface is not stock's cloud box.

4. **The HDR target is built directly with GL.** It has three textures, reallocated on resize like today:
   - RGBA16F colour;
   - R32F sky depth, cleared to −1 each frame;
   - a depth texture, `GL_DEPTH_COMPONENT24`, sampled by the resolve.

   Only the sky state draws to two buffers (`glDrawBuffers`, writing `gl_FragData[1]`). Every other state draws to attachment 0 alone. The depth blit is removed (decision 2).

   Rejected: keeping `QOpenGLFramebufferObject`. It can't attach a depth texture.

5. **Volumes, order and membership.**
   - **Order:** volumes are ordered exactly as q3map2's list leaves fog brushes (Context, "Order"), with translucency taken from any side's surfaceparms, as fog-volume's "Fog volumes" spells out.
   - **Membership:** a pixel's position belongs to a volume when it lies within ε = 1 + depth/512 units outside every plane. The same ε applies to the fog-surface test t(p) ≥ −ε with the eye inside. That absorbs the reconstruction error of a 24-bit depth buffer, so a floor flush with a fog face (the benchmark's street) is fogged evenly, as stock fogs it.
   - **Overlap:** the last containing volume wins.

   This matches q3map2's assignment for brush faces and its overwrite order. Everything else on the known-difference list in fog-volume's composite requirement differs, and the README lists it.

6. **The fog surface follows q3map2 without `visibleHull`.**
   - With a non-zero `q3map_fogDir` on the content shader and some side's dot product negative: the first non-bevel side, in the order −X, +X, −Y, +Y, −Z, +Z, then the others in map order, with the most negative dot product.
   - Otherwise: +Z, −Z, +Y, −Y, +X, −X, then non-axial sides in map order, skipping `nodraw`.

   This differs from stock whenever q3map2 would skip the chosen side as hidden, or orders non-axial sides differently after its axial swap. Both differences show only with the camera outside the volume. The town's volume has no surface in stock and a +Z surface here.

   Rejected: reading the compiled BSP's fog lump. It goes stale when the map is edited and doesn't exist before the first compile.

7. **Analytic √ per pixel, compared against opengl1's real path.** The editor follows rend2's analytic form, evaluated per pixel. The test compares it with a transcription of opengl1's opaque path, `R_FogFactor` sampled through a 256 × 32 image with linear filtering and edge clamping: within 0.05 in f for f ≥ 0.1 with no surface or the eye inside (about 0.047 at f = 0.1), and printed without a bound with the eye outside a surfaced volume. There the clamp of depth/d at 8 and the 32-row filtering of the fraction differ by up to about 0.32 (measured by the test). The measured figures go into the README. Per-vertex interpolation in both stock renderers is a further, geometry-dependent difference, documented and not measured.

8. **Volumes go to the shader in a small float texture.** It is RGBA32F, one texel per plane, plus per-volume rows for colour, distance, surface plane and the eye term. That keeps clear of GLSL 1.20's uniform minimum and avoids dynamic uniform indexing, which the existing shaders avoid. The limits are 8 volumes and 32 planes per volume. A brush over 32 planes is ignored, with a warning when it first exceeds the limit, re-armed once it drops back. Volumes beyond 8 warn once per change of count.

9. **Fog faces are their own rule.** In lighting mode, a face whose shader declares `fogparms` is not drawn, before the lit, emitter and unlit rules are consulted. Its selection highlight still is. `surfaceparm fog` without `fogparms` changes nothing.

10. **A separate gatherer, the same pattern as the sky.** `radiant/fogvolumes.cpp` walks brushes. For each it applies the content-shader rule and builds the planes (outward, in world space) and the fog surface through `libs/fogvolume.h`, then orders the volumes. It is marked dirty by the scene-changed callback, the shader observer and the preference, and updated at the start of each lighting-mode draw. The per-frame eye term t(e) is computed at upload, not in the walk.

11. **"Lighting fog" preference, per game, default on.** It is registered and shown like "Lighting sky". Off sets the volume count to 0. Without the HDR target there is no fog, and the existing fallback warning names fog too.

## Risks / Trade-offs

- [The sky changes in lighting mode even without fog] → intended: it now shows its own bytes, as opengl1 does. "Legacy map unchanged" in `pbr-lighting-preview` is restated relative to "Lighting sky" off. The README's parity procedure ignores sky pixels until `renderer_sh` has a sky (C3a).
- [Half-float precision in the inverse tonemap] → task 4.2 checks every sky pixel against the image within 1, at exposures of 0.25, 1 and 4.
- [The resolve's depth write replaces the blit] → overlays become occluded at MSAA 8, a visible improvement. Task 4.1 checks occlusion at MSAA 0 and 8.
- [Translucent and all-additive surfaces are fogged by the depth behind them; glass in front of the sky is not fogged] → small differences from stock, documented, and covered by scenarios.
- [Entity boxes and light gizmos drawn before the resolve get fogged] → acceptable; they are already tonemapped today.
- [The q3map2 assignment differences] → documented.
- [The editor gives the town's volume a fog surface stock doesn't have] → only with the camera outside the volume; documented.

## Migration Plan

No data migration. Maps and shaders are unchanged. The visible changes in lighting mode are the sky's colour, the fog, and overlays occluded at MSAA 8. "Lighting fog" off restores the fog-free view.

## Open Questions

None blocking. The tonemap order (decision 1) was proposed to the user as the default before this change was started, and the user approved the redesign after review. If fog before the tonemap is preferred, decisions 1–3 change.
