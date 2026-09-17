## 1. Material interface and parser

- [x] 1.1 Add `SHADERLANGUAGE_PBR` to `plugins/shaders/shaders.h` and PBR texture/factor fields to `ShaderTemplate` in `plugins/shaders/shaders.cpp`
- [x] 1.2 Implement `ShaderTemplate::parsePBR` with the keyword table from the spec, unknown-line skipping, nested-block skipping, and surfaceparm/alphamode/doublesided flag mapping
- [x] 1.3 Route `ParseShaderFile` to `parsePBR` when the language is PBR
- [x] 1.4 Extend `IShader` in `include/ishaders.h` with `getBaseColor/getNormal/getMetallicRoughness/getOcclusion/getEmissive` and factor/alpha-mode accessors; bump the interface version constant
- [x] 1.5 Implement the new accessors in `CShader`; load PBR textures in `realiseLighting` and release them in `unrealiseLighting`; make `getBump` return null for PBR materials
- [x] 1.6 Register `ShadersPBRAPI` in `plugins/shaders/plugin.cpp` (`materials/`, `.mtr`, no default shaders, no shaderlist)
- [x] 1.7 Add a gamma-bypass flag to the texture load path in `radiant/textures.cpp` and set it for textures requested by PBR materials
- [x] 1.8 Verify: a test `.mtr` with every keyword loads without console errors and the texture browser shows the editor image

## 2. Light entities

- [x] 2.1 Add `eGameTypePBR` to `plugins/entity/entity.h` and `LIGHTTYPE_PBR` to `plugins/entity/light.h`; select it in `Entity_Construct`
- [x] 2.2 Register entity module `pbr` (selected by `entities="pbr"`) in `plugins/entity/plugin.cpp` that constructs with `eGameTypePBR`
- [x] 2.3 Add `RendererLightParams` struct and `RendererLight::params()` virtual to `include/irender.h`; implement a point-light default in the existing Doom 3 `Light`
- [x] 2.4 In `plugins/entity/light.cpp`, add the PBR branch: key observers for `intensity`, `radius`, `cone`, `cone_inner`; classname-based type detection for `light`, `light_spot`, `light_sun`; `params()` implementation; sun `testAABB` always true
- [x] 2.5 Draw the spot cone when selected and reuse the radius sphere for PBR point lights
- [x] 2.6 Warn once in the console when more than one `light_sun` exists
- [ ] 2.7 Verify: place each light type, edit keys in the inspector, confirm the renderer receives updated params (log in a debug build)

## 3. Renderer

- [x] 3.1 Add `GLProgram::setLightParams` to `include/iglrender.h` with no-op defaults in the three existing programs
- [x] 3.2 Write `setup/data/tools/gl/pbr_vp.glsl` (position, texcoords, tangent frame, world position) in GLSL 1.20
- [x] 3.3 Write `pbr_fp.glsl`: sRGB decode of base colour and emissive, normal mapping, GGX/Smith/Schlick/Disney BRDF, point/spot/sun branches with the specified falloff, one light per pass
- [x] 3.4 Write `pbr_base_fp.glsl`: emissive times factor and strength plus ambient times base colour
- [x] 3.5 Add `GLSLPBRProgram` and `GLSLPBRBaseProgram` in `radiant/renderstate.cpp`, binding five textures and the uniform set; create/destroy alongside the existing programs
- [x] 3.6 In `OpenGLShader::construct`, add a branch for PBR materials: depth-fill pass, base pass, additive light pass with `RENDER_BUMP | RENDER_PROGRAM | RENDER_LIGHTING`; honour alpha mask and double-sided
- [x] 3.7 In `OpenGLShaderPass::flush`, call `setLightParams` with the light's params before each lit draw
- [x] 3.8 Verify: benchmark-style test scene shows metallic and rough dielectric responses as described in the spec scenarios

## 4. HDR framebuffer and resolve

- [x] 4.1 Write `tonemap_vp.glsl` and `tonemap_fp.glsl` (exposure multiply, Uncharted 2 operator ported from `sh-renderer/glsl/tonemap.frag`, sRGB encode)
- [x] 4.2 Add `GLSLTonemapProgram` in `radiant/renderstate.cpp`
- [x] 4.3 In `radiant/camwindow.cpp`, create a `QOpenGLFramebufferObject` (RGBA16F, depth) lazily when entering lighting mode with a PBR language; resize on viewport change; release on leaving lighting mode
- [x] 4.4 In `Cam_Draw`, bind the FBO for the scene render, unbind, draw the full-screen resolve, then draw overlays as before
- [x] 4.5 Add "Lighting exposure" and "Lighting ambient" camera preferences; redraw on change
- [x] 4.6 Implement the fallback: if FBO creation fails, render directly and warn once
- [ ] 4.7 Verify: bright light rolls off smoothly; resizing the window keeps the image correct; Doom 3 gamepack lighting mode is unchanged

## 5. Gamepack and benchmark map

- [x] 5.1 Create the `pbr` gamepack: `games/pbr.game`, `pbr.game/default_build_menu.xml`, `pbr.game/base/entities.ent` (XML, carries key defaults), `pbr.game/base/default_shaderlist.txt`
- [x] 5.2 Write entity definitions for `light`, `light_spot`, `light_sun`, and worldspawn keys used by the map
- [x] 5.3 Author test materials covering base colour, normal, metallic-roughness, occlusion, emissive, mask alpha, and double-sided
- [x] 5.4 Build the benchmark map: street block, two building interiors, low sky ceiling, fog volume, one sun, two point and two spot lights
- [x] 5.5 Compile with "BSP + VIS"; fix leaks until clean
- [x] 5.6 Write the gamepack `README.md`: `intensity` as flux and irradiance (sun), raw map-unit distances, engine key mapping, tonemap operator, documented camera position and exposure, parity procedure and tolerance
- [ ] 5.7 Verify: parity screenshots from editor and engine direct-light path pass the documented tolerance

## 6. Wrap-up

- [ ] 6.1 Confirm Quake 3, Doom 3, and Quake 4 gamepacks load and render identically to before in all draw modes
- [x] 6.2 Add a changelog entry in `docs/changelog-custom.txt`
