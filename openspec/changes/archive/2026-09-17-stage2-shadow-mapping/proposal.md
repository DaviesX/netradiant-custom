## Why

Stage 1 gave `pbr` games a physically based direct-lighting preview, but every light in it is unoccluded: a spot inside a closed room still lights the street outside, and the sun reaches surfaces under a sealed roof. Level designers cannot judge a Silent Hill style town — whose whole read depends on what is *not* lit — from a preview with no shadows. Stage 2 adds sun and spot shadow mapping to the lighting draw mode, matching the reference renderer's scheme so the editor stays a faithful preview as later stages add baked indirect lighting.

## What Changes

- Add a depth-only shadow caster pass that traverses the scene from each shadow-casting light and renders solid geometry into a depth target. Casters are brush faces, patches and model surfaces; sky, clip, trigger and nodraw surfaces are excluded.
- Add a cascaded shadow map for the sun: 3 cascades fitted to slices of the camera frustum, packed into one depth atlas, with practical split distribution (lambda 0.8), texel-snapped centres and a padded near plane.
- Add a shadow atlas for spot lights, each light occupying a sub-rectangle addressed by a UV scale and offset.
- Extend the PBR light pass to attenuate its contribution by a shadow term sampled with a 9-tap Poisson PCF kernel rotated by interleaved gradient noise, using the reference renderer's normal-offset and slope-scaled bias.
- Cache the shadow maps across frames and regenerate them only when the scene graph or a light changes, so camera movement costs sampling only. Sun cascades additionally regenerate when the camera moves, because they are fitted to its frustum.
- Add a `_shadows` key (default 1) to `light_spot` and `light_sun` so an individual light can be made non-casting, and a "Lighting shadows" camera preference that disables shadow generation and sampling for the whole scene.
- Extend `RendererLightParams` with the per-light shadow description (view-projection matrices, atlas rectangle, cascade splits) and change `GLProgram::setLightParams` to receive the `RendererLight` so the lighting program can bind it.
- Point lights (`light`) do not cast shadows in this change; omni shadows are deferred.
- Out of scope: engine-side work. Plan section 2.4 (hull reconstruction from the BSP brush lumps in ioquake3) and the editor-versus-engine shadow parity check that depends on it are not part of this change.
- **BREAKING** for none: non-`pbr` games, other draw modes, and the Doom 3 lighting path are unchanged.

## Capabilities

### New Capabilities
- `pbr-shadow-maps`: The shadow caster pass, the sun cascade atlas and spot atlas, the sampling and bias scheme, the caching and invalidation rules, and the shadow preferences.

### Modified Capabilities
- `pbr-light-entities`: `light_spot` and `light_sun` gain a `_shadows` key; `RendererLightParams` carries the shadow description through to the renderer.
- `pbr-lighting-preview`: the additive light pass multiplies its contribution by the shadow term for casting lights; a new "Lighting shadows" preference joins exposure and ambient.
- `pbr-gamepack`: entity definitions declare `_shadows`; the benchmark map and README cover shadow authoring and verification.

## Impact

- `include/irender.h`: `RendererLightParams` gains shadow fields; implementers in `plugins/entity/light.cpp` must fill them.
- `include/iglrender.h`: `GLProgram::setLightParams` signature changes to take `const RendererLight&`. Implementers are `GLSLPBRProgram`, `GLSLPBRMaterialProgram` and the three no-op programs in `radiant/renderstate.cpp`.
- `include/renderable.h`, `radiant/renderer.h`: the caster traversal reuses the existing `Renderer` and `Scene_Render` interfaces; a new `Renderer` implementation collects casters.
- `radiant/renderstate.cpp`: shadow atlas allocation and lifetime, the caster pass and its depth program, cascade fitting, invalidation driven by the existing `m_lightsChanged` flag and a scene-changed callback, and the new uniforms on the PBR light program.
- `radiant/camwindow.cpp`: shadow generation is driven before the scene render inside lighting draw mode; new "Lighting shadows" preference; camera movement marks the sun cascades stale.
- `setup/data/tools/gl/`: new `shadow_vp.glsl` / `shadow_fp.glsl` for the caster pass; `pbr_fp.glsl` gains the shadow sampling functions ported from `sh-renderer/glsl/radiance.frag`.
- `plugins/entity/light.cpp`: read `_shadows`; report it through `params()`.
- Gamepack (`setup/data/gamepacks/pbr/`): `entities.ent` declares `_shadows`; the benchmark map gains geometry that demonstrates occlusion; the README documents the bias constants and cascade parameters.
- OpenGL: depth textures with `GL_TEXTURE_COMPARE_MODE` (shadow samplers) are required. The existing `QOpenGLFunctions_2_0` binding plus `GL_ARB_shadow` / `GL_ARB_depth_texture` covers this; the GLSL 1.20 `shadow2D` builtin is used rather than GLSL 1.30 `texture`, and cascades share one atlas rather than a sampler array because GLSL 1.20 forbids dynamic sampler indexing.
- Stage 1 verification tasks 2.7, 4.7, 5.7 and 6.1 remain open and are not addressed here.
