## Context

NetRadiant-custom's material system lives in `plugins/shaders`, with three compiled-in languages picked by the `shaders` attribute of the `.game` file. The Doom 3 path already resolves diffuse, bump, and specular textures and hands them to the renderer through `IShader::getDiffuse/getBump/getSpecular`. The camera's lighting draw mode (`cd_lighting` in `radiant/camwindow.cpp`) sets `RENDER_BUMP | RENDER_PROGRAM`, and `OpenGLShader::construct` in `radiant/renderstate.cpp` builds two passes per bump material: a depth-fill pass and an additive pass with `GLSLBumpProgram`, which the render loop runs once per light that overlaps the surface. Light entities implement `RendererLight` (`include/irender.h`) with colour, bounds, rotation, and an optional projection. Everything draws straight into the `QOpenGLWidget` back buffer via `QOpenGLFunctions_2_0`; there is no framebuffer object anywhere.

Stage 1 must deliver a glTF-style material format, physically based direct lighting, and physical light units, with storage formats final enough that Stage 2 (shadow maps) and Stage 3 (SH indirect) do not force a redesign. The reference implementation is the `sh-renderer` fragment shader.

## Goals / Non-Goals

**Goals:**
- A `pbr` material language and gamepack that need no code changes to author new materials.
- Point, spot, and sun lights with physical units that the engine will read unchanged.
- A lighting preview whose BRDF, units, and colour handling match the engine's direct path.
- HDR accumulation with exposure and tonemap so physical units do not clip.
- Keep every existing game type and draw mode byte-for-byte unchanged in behaviour.

**Non-Goals:**
- Shadow mapping, lightmap UVs, SH indirect, Forward+ tiling, SSAO.
- q3map2 or engine changes.
- Expression parameters, `guide` templates, or tables from the Doom 3 grammar.
- Editing materials from inside the editor.

## Decisions

### D1. Fourth language in the existing shaders plugin, not a new plugin
Add `SHADERLANGUAGE_PBR` and `ShaderTemplate::parsePBR`, register `ShadersPBRAPI` in `plugins/shaders/plugin.cpp` with `materials/` and `.mtr`, no default shaders, no shaderlist. Alternative: a separate plugin module implementing `ShaderSystem`. Rejected because `CShader`, texture caching, realise/unrealise, and the active-shader notification are all in this plugin and would have to be duplicated.

### D2. Flat keyword grammar, no expressions
The PBR parser is a flat key-value block. It does not copy the Doom 3 expression evaluator (`ShaderValue`, `TextureExpression` with `$parm`). The baker reads the same `.mtr` files directly, so there is no export step and the grammar must stay trivially parseable. Alternative: reuse the Doom 3 parser and add keywords. Rejected because the Doom 3 stage-block parser and `parseTemplateInstance` carry complexity that glTF materials never need, and the baker must parse the same files with a small reader.

### D3. Extend `IShader` rather than overload `getDiffuse/getBump/getSpecular`
Add `getBaseColor/getNormal/getMetallicRoughness/getOcclusion/getEmissive` and factor accessors. For a PBR material, `getTexture` returns the editor image (falling back to base colour) so textured mode and the texture browser work unchanged. `getBump` returns null for PBR materials so the existing bump-pass branch in `OpenGLShader::construct` is not taken. Alternative: map base colour onto `getDiffuse` and normal onto `getBump` to reuse the existing pass. Rejected because the pass would still bind the Blinn-Phong program and the metallic-roughness map has no slot.

### D4. Extend `RendererLight` and `GLProgram::setParameters` with a light descriptor
Add a `RendererLightParams` struct (type, origin, direction, intensity, radius, cosInner, cosOuter) returned by a new `RendererLight::params()` virtual. The default implementation reports type `eDoom3`, which keeps the existing attenuation-texture path; PBR lights report point, spot or sun. Add `GLProgram::setLightParams( viewer, localToWorld, colour, params )` alongside the existing `setParameters` (viewer and local transform are needed to shade in object space like the existing program). `Renderables_flush` calls `setLightParams` for non-Doom 3 lights and the existing `setParameters` path otherwise. Per-material factors reach the shared programs through a small per-material `GLProgram` front (`GLSLPBRMaterialProgram`) that the render loop enables when the pass is reached. Alternative: change `setParameters`'s signature. Rejected because three existing programs implement it and a second call keeps the diff local.

### D5. Sun as an entity, culled as always-on
`light_sun` is an entity so it is visible, selectable, and saved in the map like any light. Its `RendererLight::testAABB` returns true and its bounds are the world, so the existing per-surface light culling includes it everywhere. Alternative: worldspawn keys. Rejected because it has no visual handle and cannot be rotated with the manipulator.

### D6. New `pbr` entity game type, not new light keys on the Quake 3 light
`plugins/entity/entity.h` gains `eGameTypePBR` and `LIGHTTYPE_PBR`. The `Light` class already branches on light type for key observers; the PBR branch registers `intensity`, `radius`, `cone`, `cone_inner`, `angles` and `angle`, and reads `target` through the instance's existing targeting keys. Alternative: extend the Quake 3 light with optional keys. Rejected because Quake 3's `light` key means brightness in q3map2 units and reusing it would confuse the parity criterion.

### D7. HDR via `QOpenGLFramebufferObject`
The GL binding is `QOpenGLFunctions_2_0`, which has no framebuffer entry points. Qt's `QOpenGLFramebufferObject` with `GL_RGBA16F` internal format and a combined depth-stencil attachment provides the target without changing the binding. The camera creates it lazily in `Cam_Draw` when in lighting mode with a PBR material language, resizes on viewport change, and releases it when leaving lighting mode. Alternative: bump the binding to `QOpenGLFunctions_3_0`. Rejected for Stage 1 because it touches every compilation unit that includes `igl.h`; it can be revisited in Stage 2 if shadow maps need more than the FBO class offers.

### D8. Resolve pass inside the render loop, before overlays
`Cam_Draw` binds the FBO and clears; the render loop (`OpenGLShaderCache::render`) calls a resolve hook when it reaches the first bucket sorted at or above `eSortHighlight`, so the scene passes go to the HDR target while selection highlights and overlays are drawn afterwards into the window target. The hook blits depth to the window target (so overlays keep occlusion), rebinds it and draws a full-screen quad with `GLSLTonemapProgram` sampling the FBO colour texture. Tonemap operator: Uncharted 2 (Hable) with white point 11.2, ported from `sh-renderer/glsl/tonemap.frag`. Exposure and ambient are camera preferences registered next to the existing camera globals.

### D9. Light units and falloff
`intensity` is radiant flux in watts for point and spot lights and irradiance in W/m^2 for the sun. Point radiant intensity is `flux / (4 pi)`; spot radiant intensity is `flux / (2 pi (1 - cosOuter))` so narrowing the cone concentrates the same flux. Distances stay in raw map units: `sh-renderer` divides by the squared scene distance without any metre conversion, and the Quake 3 spatial scale is kept. Point and spot lights use `radiantIntensity / d^2`, zero beyond `radius` (the engine has no window term; it culls lights per tile by `range`). The README records how the exporter must map keys to `KHR_lights_punctual` (intensity × 200, `range` = `radius`). Spot uses `smoothstep(cosOuter, cosInner, cosAngle)`. Sun uses `intensity` directly as irradiance.

### D10. Colour space
Base colour and emissive are decoded from sRGB in the fragment shader with the exact piecewise transfer function, not `pow(2.2)`. The gamma table in `radiant/textures.cpp` is bypassed for textures loaded through a PBR material by passing a flag through `LoadImageCallback`. Alternative: `GL_SRGB8_ALPHA8` internal format. Rejected because it needs `GL_EXT_texture_sRGB` handling in the loader and gives no benefit at 8 bits.

### D11. GLSL files
Four new files under `setup/data/tools/gl/`: `pbr_vp.glsl`, `pbr_fp.glsl` (one light), `pbr_base_fp.glsl` (emissive plus ambient, shares the vertex shader), and `tonemap_vp.glsl` / `tonemap_fp.glsl`. GLSL 1.20 with `varying` and `gl_TextureMatrix` to match the existing programs and the 2.0 binding. The BRDF functions are copied from `radiance.frag` with `in`/`out` rewritten.

## Risks / Trade-offs

- [Half-float attachments unsupported on a driver] → fall back to direct rendering with a one-time warning; parity check still runs in the engine.
- [Per-light additive passes with GGX cost more than Blinn-Phong] → the editor already culls lights per surface; benchmark map keeps light counts realistic; no tiling in Stage 1.
- [Tonemap operator mismatch with the engine] → operator and exposure are recorded in the gamepack README and both sides use the same formula; parity check catches drift.
- [Bypassing the gamma table only for PBR textures leaves a mixed state if a PBR material references an image also used by a non-PBR path] → in a `pbr` game only PBR materials exist, so the case does not arise.
- [`IShader` change breaks out-of-tree plugins] → none are known; the interface version constant is bumped.
- [The `Light` class in `plugins/entity/light.cpp` is 2000 lines and already branches three ways] → the PBR branch is added as a fourth `LightType` with its own key-observer block; no existing branch is edited.
- [Passes that bind textures on units 1-4 left the client-active texture unit at 4, so fixed-function texcoord pointers landed on the wrong unit and the driver crashed] → `OpenGLState_apply` resets the active unit to 0 after binding.
- [PBR base pass doubles as the depth fill] → the base pass writes depth with the alpha test applied, so masked materials get correct holes; no separate `zfill` pass is emitted for PBR materials.

## Migration Plan

Additive only. No existing gamepack, map, or preference changes meaning. Rollback is removing the `pbr` gamepack; the code paths are unreachable for other games.

## Open Questions

- Resolved against `sh-renderer` (commit cdfa844, 2026-07-01): tonemap is Uncharted 2 (Hable, W = 11.2), the BRDF matches `ComputeDirectBRDF` (roughness >= 0.05, denominator + 0.001, kD = (1 - F)(1 - metallic)), distances are raw scene units, lights have no window term, emissive with no texture is `factor x strength`.
- The engine reads occlusion from the metallic-roughness R channel and fixes the alpha cutoff at 0.5; the editor keeps the separate `occlusion` map and `alphacutoff` key (documented in the README).
