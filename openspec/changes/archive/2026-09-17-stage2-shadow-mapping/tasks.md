## 1. Light interface and the `_shadows` key

- [x] 1.1 Add `castsShadows` to `RendererLightParams` in `include/irender.h` (default false) and document that the shadow description itself lives in the renderer, not the light
- [x] 1.2 Change `GLProgram::setLightParams` in `include/iglrender.h` to take `const RendererLight&` in place of the colour and params pair; update the no-op defaults
- [x] 1.3 Update the callers and implementers: `Renderables_flush` in `radiant/renderstate.cpp`, `GLSLPBRProgram`, `GLSLPBRMaterialProgram`, and the three existing programs
- [x] 1.4 In `plugins/entity/light.cpp`, read `_shadows` (default 1) for the PBR spot and sun branches, report it through `params()`, and trigger a renderer light-changed on edit; ignore the key for point lights
- [x] 1.5 Verify: toggling `_shadows` in the entity inspector reaches the renderer (log in a debug build)

## 2. Caster traversal

- [x] 2.1 Add `ShadowCasterRenderer : public Renderer` in `radiant/renderstate.cpp` collecting `OpenGLRenderable*` plus transform into a flat draw list, keeping the current `Shader*` on its state stack
- [x] 2.2 Add the caster walker: modelled on `RenderHighlighted` but calling `renderSolid` directly and never calling `viewChanged()`, driven through `ForEachVisible` with a light's `View`
- [x] 2.3 Implement caster exclusion by querying the collected `IShader` for `surfaceparm sky`, `nodraw`, `trigger`, `noshadows`, and for clip and hint surfaces
- [x] 2.4 Handle alpha-mask materials in the caster pass: bind the base colour texture and discard below the cutoff so masked texels do not occlude
- [ ] 2.5 Verify: with a temporary console dump, the caster list for a light contains brush, patch and model surfaces and excludes sky and clip

## 3. Depth target and caster program

- [x] 3.1 Write `setup/data/tools/gl/shadow_vp.glsl` and `shadow_fp.glsl`: position only, plus the alpha-mask discard path, in GLSL 1.20
- [x] 3.2 Add `GLSLShadowProgram` in `radiant/renderstate.cpp`, created and destroyed alongside the other PBR programs
- [x] 3.3 Allocate the two depth atlases (2048x2048, `GL_DEPTH_COMPONENT24`, compare mode `GL_COMPARE_R_TO_TEXTURE`, func `GL_LEQUAL`, `GL_LINEAR`) with an FBO each, lazily on entering lighting mode in a `pbr` game, released on leaving it
- [x] 3.4 Implement the depth-only draw: colour writes off, `GL_LEQUAL`, back-face culling, per-tile viewport and scissor, one-texel guard band between tiles
- [x] 3.5 Query `GL_MAX_TEXTURE_IMAGE_UNITS` and disable the shadow path with a single warning if it is below seven
- [x] 3.6 Implement the fallback: if atlas or FBO creation fails, render lighting mode unshadowed and warn once
- [x] 3.7 Verify: dump the sun atlas to a PNG from a debug build and confirm the scene silhouette appears

## 4. Cascade fitting

- [x] 4.1 Add `radiant/cascade.h` and `cascade.cpp`: pure maths, no GL
- [x] 4.2 Implement the split distances over the camera near and far with lambda 0.8, blending the logarithmic and uniform distributions
- [x] 4.3 Implement per-slice fitting: eight frustum-slice corners into light space, axis-aligned bounds, orthographic projection
- [x] 4.4 Implement texel snapping of the bounds centre to the cascade's world-units-per-texel grid, and pull the near plane back by 20 map units
- [x] 4.5 Verify: compare the computed splits and bounds against `sh-renderer/src/cascade.cpp` for one documented camera, on paper

## 5. Shadow generation and invalidation

- [x] 5.1 Add the frame shadow assignment table keyed on `const RendererLight*`, holding view-projections, atlas UV scale and offset, and the cascade splits
- [x] 5.2 Implement sun generation: pick the first casting `light_sun`, fit three cascades, render each into its quadrant of the sun atlas
- [x] 5.3 Implement spot generation: perspective view-projection per casting `light_spot` (fov twice the outer half-angle, near 4, far `radius`), assign tiles nearest-camera-first, warn once when the sixteen tiles are exhausted
- [x] 5.4 Add the dirty flags: geometry from `GlobalSceneGraph().addSceneChangedCallback`, lights from the existing `m_lightsChanged` path, camera from a comparison against the last cascade fit
- [x] 5.5 Expose `ShaderCache_updateShadows( const View& camera )` and call it from `Cam_Draw` before `HDR_begin()`, restoring the framebuffer binding and viewport afterwards
- [x] 5.6 Verify: a static scene with a static camera regenerates nothing after the first frame; moving a brush or editing a light key regenerates on the next redraw

## 6. Sampling in the lighting program

- [x] 6.1 Bind the sun and spot atlases to texture units 5 and 6 in `GLSLPBRProgram`; add the texel-size uniforms, since `textureSize` is unavailable in GLSL 1.20
- [x] 6.2 Upload per-renderable `localToShadow` matrices (three for the sun) and the third row of `localToView` for cascade selection, composed in `setLightParams`
- [x] 6.3 Port `InterleavedGradientNoise` and the nine-tap rotated Poisson disc from `sh-renderer/glsl/radiance.frag` into `pbr_fp.glsl` using `shadow2D`
- [x] 6.4 Port `ComputeShadow`: normal offset `(1 - NdotL) * 0.005`, slope-scaled bias `0.001 * tan` clamped to 0.003, out-of-range treated as lit, UV scale and offset applied for the atlas tile
- [x] 6.5 Port `ComputeSunShadow`: cascade selection from view-space depth, penumbra `2 / (layer + 1)`
- [x] 6.6 Multiply the light contribution by the shadow term for casting lights only; leave the base pass unshadowed
- [x] 6.7 Verify: a sealed room is dark under the sun while the street outside is lit; a pillar casts a spot shadow onto a wall; a point light still passes through a wall

## 7. Preference and gamepack

- [x] 7.1 Add the "Lighting shadows" camera preference (boolean, default on), registered like `LightingExposure`, redrawing the camera on change
- [x] 7.2 Declare `_shadows` on `light_spot` and `light_sun` in the gamepack `base/entities.ent`, with default and description
- [x] 7.3 Extend the benchmark map: a free-standing pillar casting a spot shadow, a patch arch as a caster, a sun-sealed interior, and one light with `_shadows 0`
- [x] 7.4 Recompile the benchmark map with "BSP + VIS" and confirm it is still leak-free
- [x] 7.5 Add the shadow section to the gamepack `README.md`: culling mode, bias constants, PCF kernel, cascade count and resolution, split lambda, snapping, near padding, atlas dimensions and tile size, excluded surface types, and which values are copied from `sh-renderer` versus chosen editor-side

## 8. Wrap-up

- [x] 8.1 Capture lighting-mode screenshots with and without shadows from the documented benchmark camera using the headless capture path
- [ ] 8.2 Confirm Quake 3, Doom 3 and Quake 4 gamepacks load and render identically to before in all draw modes, and that no shadow atlas is allocated for them
- [x] 8.3 Confirm the "Lighting shadows" preference off reproduces the Stage 1 image
- [x] 8.4 Add a changelog entry in `docs/changelog-custom.txt`
