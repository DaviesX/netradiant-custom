## Context

Stage 1 added the `pbr` material language, physical light entities, a GGX lighting program and an HDR framebuffer with a tonemapped resolve. Lighting draw mode renders a depth-fill pass, a base pass (emissive plus ambient) and one additive pass per light, all into a half-float FBO created in `radiant/camwindow.cpp` and resolved by `ShaderCache_drawTonemap`.

Three properties of that code shape this design:

- **Lighting is computed in object space.** `GLSLPBRProgram::setLightParams` inverts the renderable's `localToWorld` and transforms the light origin, direction and viewer into local space. There is no world-space position in `pbr_fp.glsl`.
- **Shaders are GLSL 1.20.** No `texture()`, no `textureSize()`, no sampler arrays with dynamic indices, no integer texture-array samplers. Depth comparison is available through `shadow2D` and `GL_ARB_shadow`, which are GL 1.4 core and safely inside the `QOpenGLFunctions_2_0` binding.
- **The scene traversal is reusable.** `Scene_Render( Renderer&, const VolumeTest& )` in `radiant/renderer.h` drives any `Renderer` implementation over the graph with any `VolumeTest`, and `View` builds a frustum from an arbitrary projection and modelview, orthographic included. `OpenGLShaderCache` already owns the set of live `RendererLight*` and a `m_lightsChanged` flag.

The reference renderer is `sh-renderer` at commit `cdfa844`. Its `draw_shadow_map.cpp` renders depth with `glCullFace( GL_BACK )` and no polygon offset; `glsl/radiance.frag` compensates with a normal offset, a slope-scaled depth bias and a nine-tap rotated Poisson PCF; `cascade.cpp` fits three 1024-pixel cascades to camera frustum slices with a practical split at lambda 0.8, texel snapping and a padded near plane.

## Goals / Non-Goals

**Goals:**

- Sun and spot shadows in `pbr` lighting draw mode, sampled with the same bias and filter constants as `sh-renderer`, so a later parity pass has a chance of succeeding.
- One caster path that handles brushes, patches and models identically, with no per-geometry-type bias regime.
- Shadow maps regenerated only when their inputs change, so a static scene costs sampling only.
- No change to non-`pbr` games, other draw modes, or the 2D views.

**Non-Goals:**

- Point light (omni) shadows. Deferred; the atlas and light description are shaped so a cube or six-face path can be added without reworking them.
- Engine-side work. Plan section 2.4 (hull reconstruction from the BSP brush lumps in ioquake3) and the editor-versus-engine shadow comparison that depends on it are excluded.
- Second-depth (front-face-culled) shadow mapping. See the first decision.
- Contact-hardening or variance filtering; the filter is fixed-width PCF as in the reference.

## Decisions

### Port the reference renderer's bias scheme rather than second-depth

The plan's section 2.1 proposed front-face culling so that wall thickness supplies the bias and no constant needs tuning. Reading `draw_shadow_map.cpp` shows the engine does the opposite: it culls back faces and pays for it in the shader.

Second-depth was rejected for three reasons.

*Free-form geometry.* Second-depth is defined only for closed manifolds. Patches and model meshes have no back face, so a front-face-culled pass stores nothing or the wrong surface for them. Supporting both would mean two caster passes, two bias regimes, and a visible discontinuity wherever a prop meets a wall. The reference's scheme covers all three geometry types with one pass.

*Parity.* Dropping the ioquake3 work does not retire `sh-renderer` as the target for Stage 3, where baked indirect is combined with direct light in the same shader. Two different bias schemes produce a systematic difference concentrated at contacts and on thin geometry, which no exposure or ambient adjustment removes.

*The precondition already holds.* The engine's scheme is known to behave on Quake 3 geometry when walls are thick, and the town's walls are thick by construction. Second-depth's advantage is immunity to acne without tuning, which pays off precisely where walls are thin — the case this project does not have.

The residual cost is that the bias constants are inherited rather than derived, and they are only meaningful if the cascade ranges and resolution match the reference. Those values therefore become spec text, not tunables, and are recorded in the gamepack README.

### Collect casters with a dedicated `Renderer`, and skip `viewChanged()`

The caster pass reuses the existing traversal rather than introducing a geometry extraction API. A `ShadowCasterRenderer : public Renderer` implements `SetState` (to keep the current `Shader*` so its `IShader` can be queried for `surfaceparm`) and `addRenderable` (to append to a flat draw list), and ignores `PushState`/`PopState` bookkeeping beyond the shader stack, `Highlight` and `setLights`.

It is driven by a walker modelled on `RenderHighlighted` but deliberately **not** that class: `RenderHighlighted::pre` calls `renderable->viewChanged()`, which invalidates patch tessellation for the current view. Running it a second time per frame with a light's volume would thrash patch LOD against the camera's. The shadow walker calls `renderSolid( renderer, volume )` directly and never calls `viewChanged()`.

Alternative considered: extracting brush windings through `IBrush` and caching triangles. Rejected — the interface exposed to `radiant/` is too thin (no winding access), it would not cover patches or models, and it would duplicate tessellation the scene already maintains.

### Rebuild the caster list per regeneration, do not cache it across frames

`OpenGLRenderable*` and the `Matrix4&` handed to `addRenderable` are owned by scene instances and are only valid while the graph is unchanged. Holding them between frames would mean trusting that every mutation path signals a scene change before the next redraw.

Instead, the list is rebuilt by traversal each time any shadow map is regenerated, and is discarded at the end of that generation step. Regeneration is itself gated on the dirty flags below, so a static scene with a static camera does no traversal at all. When the camera is moving the sun cascades regenerate every frame, costing one extra traversal — comparable to what the camera render already does each frame.

If profiling later shows that traversal dominates, the list can be cached behind the same dirty flags without changing anything else. That is deliberately left out now.

### Invalidation: two dirty flags, three inputs

- **Geometry dirty** — set from `GlobalSceneGraph().addSceneChangedCallback`. Invalidates both atlases.
- **Lights dirty** — set from the existing `OpenGLShaderCache::changed( RendererLight& )`, which already drives `m_lightsChanged`. Invalidates both atlases.
- **Camera moved** — compared against the origin, angles and viewport used for the last cascade fit. Invalidates the sun atlas only, because cascades are fitted to the camera frustum; the spot atlas is camera-independent.

### Two atlases, addressed by UV scale and offset

GLSL 1.20 forbids `u_sun_shadow_maps[layer]` with a non-constant `layer`, which is how the reference selects a cascade. The reference's `ComputeShadow` already takes `uv_scale` and `uv_offset`, so the same effect is achieved by packing:

- **Sun atlas** — one 2048x2048 depth texture holding three 1024x1024 cascades in a 2x2 grid; the fourth quadrant is unused.
- **Spot atlas** — one 2048x2048 depth texture holding sixteen 512x512 tiles.

Both are `GL_DEPTH_COMPONENT24` with `GL_TEXTURE_COMPARE_MODE = GL_COMPARE_R_TO_TEXTURE`, `GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL` and `GL_LINEAR` filtering, giving free 2x2 hardware PCF under each of the nine taps. They bind to texture units 5 and 6, above the five the material already uses.

A one-texel guard band is left unwritten between tiles and the PCF kernel radius is clamped so a tap cannot reach a neighbouring tile.

Alternative considered: `GL_EXT_texture_array` with `sampler2DArrayShadow`. Rejected — it needs GLSL 1.30 or an extension pragma, and the whole Stage 1 shader set is 1.20.

### Pass the light, not the light's parameters, to the program

`GLProgram::setLightParams( viewer, localToWorld, colour, params )` becomes `setLightParams( viewer, localToWorld, const RendererLight& )`. The program reads `light.colour()` and `light.params()` as before, and additionally looks the light up in the frame's shadow assignment table to obtain its matrices and atlas rectangle.

The alternative — putting matrices and atlas coordinates into `RendererLightParams` — was rejected because that struct is filled by `plugins/entity/light.cpp`, which has no business knowing about atlases. The entity reports only the `_shadows` flag; the renderer owns everything else. The shadow assignment table is keyed on `const RendererLight*` and is rebuilt each generation step.

### Light-space matrices are composed per renderable

Because shading is in object space, the shader cannot use a world-space position. `setLightParams` already has `localToWorld`, so it uploads `localToShadow = shadowViewProjection * localToWorld` per renderable, one matrix per cascade for the sun. Cascade selection needs view-space depth, so the third row of `localToView` is uploaded as a `vec4` and the depth is `abs( dot( row, vec4( localPos, 1.0 ) ) )`.

This keeps `pbr_fp.glsl` in object space and leaves the Stage 1 lighting maths untouched.

### Caster exclusion is by surface parameter, not by a new material key

Sky must not cast: with a low sky ceiling the sun would otherwise be fully occluded everywhere. `surfaceparm sky`, `fog`, `nodraw`, `trigger` and `noshadows`, plus clip, botclip, areaportal and hint surfaces, are excluded by querying the `IShader` the `ShadowCasterRenderer` has on its state stack. No new material keyword is introduced, because `surfaceparm noshadows` already exists in the Quake 3 vocabulary the PBR parser retains.

Fog was added to that list after a caster dump on the benchmark map showed `bench/fog` contributing occluders. A fog brush's faces bound a volume the light travels *through*; treating them as surfaces that stop it drops a hard shadow of the volume's silhouette, which is wrong in a way no amount of bias tuning fixes.

### Alpha-blended materials cast a solid shadow

A material with `alphaMode blend` is drawn into the depth map at full opacity, so the benchmark map's glass casts the
shadow of an opaque pane. This is deliberate for this stage, not an oversight.

Doing better needs stochastic or otherwise order-independent transparency in the caster pass: a blended surface
occludes *partially*, and a single depth buffer cannot represent partial occlusion. The reference renderer does not
solve this either, so attempting it here would be an editor-side invention that parity work would later have to undo,
and a wrong-but-matching shadow is worth more to this stage than a better-but-diverging one.

The escape hatch already exists and costs nothing: `surfaceparm noshadows` on the material removes it from the caster
set entirely. For glass that is usually the right answer, and it is what the gamepack README tells authors to do.
Revisit only if the engine grows a transparent-shadow path to match.

### Where the code lives

Shadow generation sits in `radiant/renderstate.cpp`, next to the other GL programs and the light registry it needs. `radiant/camwindow.cpp` calls a small API (`ShaderCache_updateShadows( const View& camera )`) before `HDR_begin()`, so the caster pass owns the GL framebuffer binding and viewport before the HDR target is bound, and restores both afterwards.

The cascade fitting maths goes in a separate `radiant/cascade.h`/`.cpp` — it is pure matrix and bounds arithmetic with no GL, it is the part most likely to be wrong, and keeping it separate makes it readable against `sh-renderer/src/cascade.cpp`.

## Risks / Trade-offs

- **Bias constants inherited without their context** → The reference's `0.005` normal offset is in raw scene units, which at map-unit scale is almost nothing; the real work is done by the normalised-depth bias, whose world-space effect depends entirely on the cascade depth range. Mitigation: pin cascade count, resolution, split lambda and near padding to the reference values in the spec, and record in the gamepack README which constants are copied and which are editor-side.
- **Sun cascades regenerate on every camera movement** → Three full-scene depth passes per frame while free-looking. Mitigation: texel snapping already stabilises the fit; if this is too slow, cache the caster list behind the existing dirty flags, or refit only when the camera moves past a threshold. Measured before optimising.
- **A scene mutation that does not signal a change** → Stale shadows until the next redraw that does signal. Mitigation: the caster list is rebuilt from the live graph at every regeneration, so a stale map is at worst one frame behind, and map load and undo both signal.
- **Texture unit pressure** → Five material textures plus two shadow atlases is seven units. Well inside what any target GPU provides, but `GL_MAX_TEXTURE_IMAGE_UNITS` is queried at program creation and the shadow path disables itself with a single warning if it is below seven.
- **Patch tessellation thrash** → Addressed by not calling `viewChanged()` in the shadow walker, but a patch whose `renderSolid` internally depends on the last view could still tessellate against the light's volume. Mitigation: verify patch LOD behaviour in the benchmark map with a patch arch as a caster; fall back to rendering patches at fixed tessellation in the caster pass if needed.
- **Peter-panning from the inherited bias** → The normal offset and slope-scaled bias detach contact shadows slightly, which second-depth would have avoided on thick walls. Accepted deliberately: this is the artifact the engine already has, and matching it is the point.
- **Spot atlas capacity** → Sixteen casting spots. A dense town interior may exceed that. Mitigation: nearest-to-camera assignment with a one-time warning, so the failure is graceful and visible rather than silent.

## Migration Plan

No data migration; the change is additive and gated behind `pbr` games and lighting draw mode. Suggested landing order, each step leaving the editor working:

1. Atlas allocation, the depth-only program and the caster traversal, verified by dumping the sun atlas to a PNG.
2. Cascade fitting, verified against `sh-renderer/src/cascade.cpp` on paper before it is wired to anything.
3. Sun sampling in `pbr_fp.glsl`, with the spot path still unshadowed.
4. Spot atlas and sampling.
5. `_shadows` key, gamepack entity definitions, preference.
6. Benchmark map geometry and README.

Rollback is the "Lighting shadows" preference, which disables generation and sampling entirely and returns the Stage 1 image.

## Open Questions

- The reference's spot atlas tile size is not visible in the files read; 512x512 in a 2048x2048 atlas is an editor-side choice. If engine parity is attempted in a later stage, this may need to change to whatever `draw_shadow_map.cpp` allocates at runtime.
- The reference clamps roughness at 0.05 and uses a penumbra factor of `2 / (layer + 1)` for the sun. The equivalent factor for spot lights is not visible in `radiance.frag` as read; this design assumes 1.0 and flags it for confirmation against the engine.
- Whether the editor's camera near and far distances match the ones the engine uses for its cascade range. If they differ, the split distances differ even with the same lambda, and the inherited normalised-depth bias means something different. Worth confirming before the parity attempt in a later stage.
