# Design

## Context

The motivation is in proposal.md. Current state:

- **Sky parsing.** `plugins/shaders/shaders.cpp` parses `skyparms <farbox> …` into `m_skyBox` (empty for `-`) and sets `QER_SKY`. `getSkyBox()` captures the farbox as a GL cubemap for the textured-mode sky (`radiant/textures.cpp:349`, faces loaded `_ft _bk _up _dn _rt _lf` into +X…−Z, with the orientation fixed in `skybox_fp.glsl`). There is no CPU-side access to the six images.
- **Emissive data.** The Quake 3 stage list is parsed into `Quake3Stage`s, each with `m_map`, `m_constColour` and `m_colour`. Only the base and emissive stages survive into `ShaderTemplate`. `qer_pbr_emissiveStrength` is stored with a default of 1, and nothing records whether it was declared.
- **Base pass.** `pbr_base_vp/fp.glsl` computes emissive + `u_ambient` × base colour × occlusion. It has no normal and no metallic. Its state (`renderstate.cpp:3308`) has no `RENDER_LIGHTING`, so `GL_NORMAL_ARRAY` is off (`renderstate.cpp:2607`) and brushes upload no normals (`brush.h:102`). `Renderables_flush` calls `setParameters` only for skybox and Doom 3 light states, and `GLSLPBRMaterialProgram::setParameters` is empty, so no per-renderable transform reaches the base program. The modelview is view × object (`glMultMatrixf` at `renderstate.cpp:2882`). Light passes are per light (`RENDER_BUMP` buckets), so the base pass can't take the tangent frame without becoming per light.
- **The stock renderer's sky** (`ioq3/code/renderergl1/tr_sky.cpp`):
  - Face suffixes are `rt bk lf ft up dn` for `outerbox[0–5]` (`tr_shader.cpp` `ParseSkyParms`).
  - `MakeSkyVec`'s `st_to_vec` maps axis 0 to +X, 1 to −X, 2 to +Y, 3 to −Y, 4 to +Z and 5 to −Z, with in-face axes (s, t) per axis and t flipped. Axis i draws `outerbox[kSkyTexOrder[i]]` with `kSkyTexOrder = {0, 2, 1, 3, 4, 5}`, so `_rt` is +X, `_lf` −X, `_bk` +Y, `_ft` −Y, `_up` +Z and `_dn` −Z.
  - Cloud layers never draw the bottom face (`if (i == 5)`), and without `fullClouds` they stop near the horizon.
- **Shared code.** `libs/emissivelight.h` is the precedent for derivation helpers shared by the editor and q3map2, with a test target kept out of the install.

## Goals / Non-Goals

**Goals:**
- Write one derivation, testable outside the editor, that q3map2 (B12) can include and the README can describe completely for C6's MIT reimplementation.
- Keep the preview's cost to one SH evaluation per fragment, and recompute the sky only when the map or shaders change.

**Non-Goals:**
- Normal-mapped sky shading.
- Sky specular.
- Any visibility or occlusion estimate in the editor.
- Animated skies (`tcMod scroll` on clouds doesn't change a mean).
- More than one sky per map.

## Decisions

1. **The SH is computed on the CPU and evaluated per fragment from nine `vec3` uniforms.** The coefficients are pre-multiplied by Âₗ, so the shader computes E(n) = `max(Σ cᵢ Yᵢ(n), 0)` and then applies decision 3's factor (including the 1/π). The alternatives were an irradiance cubemap (more GL state, and a texture unit the base pass doesn't have spare) and evaluation in the light pass (per light, so wrong). Nine uniforms fit GLSL 1.20 comfortably.

2. **The vertex normal, not the normal map.** The base pass is not a `RENDER_BUMP` pass, so tangents aren't bound. The base state gains `RENDER_LIGHTING`, which enables the normal array so brushes, patches and models upload `gl_Normal`. With a program bound, the fixed-function lighting it also enables has no effect. The vertex program takes `normalize(gl_NormalMatrix × gl_Normal)`, the eye-space normal of the object's full modelview, and turns it back to world space with a `mat3` uniform, the transpose of the view matrix's rotation. The uniform is set from the pure camera view, the `modelview` argument of `OpenGLShaderCache::render` (`renderstate.cpp:1615`, loaded at line 1623), on every lighting-mode frame whatever the shadow preference. `g_shadowCameraView` is not used: it is only updated when shadows are on and the atlases exist. No per-renderable hook is needed, because the object transform is already in the modelview. A per-renderable `setParameters` call was rejected: `Renderables_flush` would need a new path for these states. The base, additive-emitter and opaque-emitter passes share one GL program (`g_pbrBaseGLSL`), and uniforms belong to the program, so every `enable()` sets the sky state explicitly through `setMaterial`: `eBase` passes the gathered coefficients, or zeros when "Lighting sky" is off or the map has no sky light; both emitter passes always pass zeros. Otherwise an emitter drawn after a lit surface would inherit its sky term. C6 does per-pixel sky with normal maps; the preview only needs the broad shape. The alternative of binding tangents in the base pass would mean restructuring the bucket logic for a small gain.

3. **Diffuse only: (1 − metallic) × base colour × occlusion × E/π.** This matches the light passes' Lambert normalisation (their diffuse is `kD × baseColor / π × …`). The constant ambient keeps its existing un-normalised form, unchanged, because the darkness budget is tuned against it. Metallic is read from the bound metallic-roughness map (blue channel, as the light pass does) × the factor. Sky specular is left out, because a mirror-like sky reflection needs a prefiltered environment.

4. **Sky selection is a scene walk, cached behind a dirty flag.** A walker over brushes and patches collects distinct shader names whose `IShader` flags include `QER_SKY` (set by `skyparms`, which every stock sky declares). `QER_SURFSKY` alone (`surfaceparm sky` without `skyparms`) is no candidate: it has no farbox or cloud layer to define a radiance from. It runs on the next lighting-mode draw after:
   - a scene change (`GlobalSceneGraph().addSceneChangedCallback`);
   - a shader realise or unrealise (shader reload);
   - a change to the "Lighting sky" preference.

   It projects only when the chosen shader's identity or source text changed, and it remembers the last candidate set so that the multiple-sky warning prints only when the set changes. The image loads go through `GlobalTexturesCache().loadImage()`, as `emissivelight` does. A scene walk per frame was rejected: it is cheap on the town but grows with map size.

5. **`IShader` gains raw accessors, not derived values:**
   - `getSkyBoxName()`: the farbox path, or "" for `-`;
   - `getSkyStageName()` and `getSkyStageColour()`: the first map stage that isn't `$lightmap`, and its `rgbGen const` or white;
   - `hasEmissiveStrength()`: whether the keyword was declared.

   The radiance and SH math stay in `libs/skylight.h`, so q3map2 can run the same code from its own shader parser (B12 fills the same plain struct from `shaderInfo_t`). `getSkyBox()` (the GL cubemap) is left alone.

6. **Farbox face mapping follows `tr_sky.cpp`, not the editor's cubemap upload.** `skylight.h` maps each face's texel (i, j) to a direction with the `st_to_vec` and `kSkyTexOrder` tables and the t flip, as the stock renderer draws it. The editor's GL cubemap uses a different face order plus a shader swizzle, so reusing it would bake in an indirection that q3map2 and `renderer_sh` don't share. Tasks 2.2 (against `tr_sky.cpp`'s `st_to_vec`) and 3.2 (a six-face fixture against the editor's drawn sky) check the orientation. The solid-angle weight of a texel is the exact solid angle it subtends: with F(u, v) = atan2(uv, √(1 + u² + v²)), the texel [u₀, u₁] × [v₀, v₁] in face coordinates ∈ [−1, 1]² weighs F(u₁, v₁) − F(u₀, v₁) − F(u₁, v₀) + F(u₀, v₀). (The midpoint form ΔuΔv / (1 + u² + v²)^{3/2} was rejected during implementation: it overestimates by 0.7% on 4×4 faces, outside the 0.5% of "Uniform farbox".) Faces of different sizes are each projected at their own resolution.

7. **Cloud layer as a uniform upper hemisphere, projected analytically.** With M the mean linear texel and ℒ = s × C × M, the coefficients are:
   - L₀₀ = 0.282095 × 2π ℒ;
   - L₁₀ = 0.488603 × π ℒ;
   - all others zero (L₂₀ integrates to 0 over the hemisphere).

   This gives E(+Z) = πℒ, E(horizontal) = πℒ/2 and E(−Z) = 0 exactly. Only the sky stage counts; a second cloud stage is ignored, because the stock blend of two scrolling layers has no fixed mean worth modelling. The image mean ignores the dome mapping, which is the stock renderer's business and doesn't change a uniform image. Projecting the actual dome texel by texel was rejected: it needs the `cloudHeight` geometry and tcMods for little gain over the mean on sky images, which are low-frequency.

8. **The strength must be declared.** Without `qer_pbr_emissiveStrength`, the sky emits nothing, whatever the default of 1 does for emitters. This keeps every legacy baseq3 map's preview unchanged. Strengths of 0 or less also emit nothing.

8b. **Radiance consumers use L(ω), not the SH.** The truncated SH of a cloud sky is L/2 + ¾Lz in radiance, −L/4 at −Z. `skylight.h` therefore exposes L(ω) for a direction, and B12 hands sh-baker that (or its texels), never the SH. The SH is only for irradiance, where the truncation is exact for the hemisphere and good for farboxes.

8c. **q3map2 is left alone.** Its emissive rule (`light_sh_model.cpp:629`) doesn't exclude sky shaders, so a sky with an additive cloud stage and a strength would count as an emitter in `-shbake`. The town's sky has no additive stage. Excluding sky shaders belongs in B12, which first gives the strength a meaning in q3map2; this change records it in the README and in CLAUDE.md's B12 entry.

9. **Alphabetical choice among several skies.** Scene order depends on the brush order on load and on editing, so it would flicker. Alphabetical is deterministic, and q3map2 can apply the same rule in B12.

10. **The "Lighting sky" preference is per game, default on.** It is registered like "Lighting shadows" and shown and hidden with the other lighting preferences. Off removes only the sky term. It exists because interiors are overlit (no visibility), and because the town's existing interior acceptance scenario was judged without sky light.

11. **The town's strength.** `sky.tga` is a uniform sRGB (51, 66, 59), linear (0.03310, 0.05448, 0.04374). For E_G(+Z) = π × s × 0.05448 = 2.5 = ½ × sun `intensity` 5, s = 14.6067. The town gets `qer_pbr_emissiveStrength 14.6` (E_G = 2.4989, within 0.05%), so E = (1.519, 2.499, 2.006) on an open floor. "Strength of the sun" is read as the sun's `intensity`, the irradiance on a surface facing it, compared in the largest channel, just as `intensity` is the largest channel of a light's colour (README units). The sun's normalised `_color` is warm and the sky is green-grey, so the two hues differ, as intended.

## Risks / Trade-offs

- [Interiors overlit in the preview] → the "Lighting sky" preference, the README caveat and the restated town interior scenario. C6 fixes this in the engine with the baked visibility.
- [A sky of 2.5 against a sun of 5 brightens everything, including the darkness budget] → that is the user's chosen tuning. The ambient preference still works, and the author can lower the strength in the shader.
- [Vertex normals on coarse geometry (big brush faces) give flat sky shading] → acceptable for the preview. Normal maps still shade the direct lights.
- [Texel orientation within farbox faces wrong] → the town uses a cloud layer, so the farbox path has no content test. Tasks 2.2 (against `tr_sky.cpp`'s `st_to_vec`) and 3.2 (an asymmetric six-face fixture against the editor's drawn sky) check it.
- [A stock sky with an additive cloud layer counted as an emitter by `-shbake` if an author adds a strength before B12] → documented in the README; B12 excludes sky shaders.
- [`RENDER_LIGHTING` on the base pass changing textured mode or other passes] → it is added only to the preview's lit base state; task 4.1 checks textured mode and the benchmark's lighting view pixel for pixel.
- [q3map2 later deriving a different value] → B12 must include `libs/skylight.h`, not reimplement it. The README describes the derivation exactly for C6.
- [Multiple-sky warning spam while editing] → it prints only when the candidate set changes.

## Migration Plan

No data migration. Legacy maps are unaffected (decision 8). Revert by removing the keyword from `town.shader`, or turn the preference off.

## Open Questions

None. B12's choice between farbox and cloud for q3map2 follows this design.
