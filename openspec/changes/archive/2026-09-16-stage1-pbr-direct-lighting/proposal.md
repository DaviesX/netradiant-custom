## Why

NetRadiant-custom can only describe materials in Quake 3, Doom 3, or Quake 4 syntax and previews lighting with a Blinn-Phong omni-light program into an 8-bit window buffer. The target project is a Silent Hill 1 style game on an ioquake3-derived engine that uses glTF-style metallic-roughness materials, physically based direct lighting, and later shadow maps and baked SH indirect lighting. Stage 1 gives level designers a material format, light entities, and an editor preview that match the engine, so the first benchmark map can be authored with final storage formats before shadow mapping (Stage 2) and SH indirect (Stage 3) build on top.

## What Changes

- Add a fourth material language, `pbr`, to the shaders plugin: a brace-compatible text format modelled on the glTF metallic-roughness schema, selectable via `shaders="pbr"` in the `.game` file.
- Extend the material interface (`IShader`) with base colour, normal, metallic-roughness, occlusion, and emissive textures plus scalar factors, alpha mode, and double-sided flag. Existing `getDiffuse`/`getBump`/`getSpecular` accessors remain for the Doom 3 and Quake 4 paths.
- Add a `pbr` game type to the entity plugin with point, spot, and sun light entities carrying intensity, radius, cone angles, and direction. Extend `RendererLight` with intensity, type, direction, and cone so the renderer receives them.
- Replace the lighting draw mode's Blinn-Phong program with a GGX / Smith / Schlick / Disney-diffuse program ported from the reference renderer, keeping the editor's one-additive-pass-per-light structure. Add a base pass for emissive plus constant ambient.
- Render the lighting draw mode into a half-float framebuffer and resolve it to the window with exposure and a tonemap operator.
- Define colour space handling: base colour and emissive decoded from sRGB in the shader; normal, metallic-roughness, occlusion sampled linear. Texture gamma preference must not alter PBR textures.
- Add a `pbr` gamepack (game file, entity definitions, build menu, shader list) and a benchmark map exercising every map type and light type.
- **BREAKING** for none: existing game types and draw modes are unchanged.

## Capabilities

### New Capabilities
- `pbr-material-format`: The `pbr` material language, its keywords, defaults, colour-space rules, and how the editor resolves textures from it.
- `pbr-light-entities`: Point, spot, and sun light entity definitions for the `pbr` game type, their keys and units, and how they reach the renderer.
- `pbr-lighting-preview`: The lighting draw mode for `pbr` games: HDR framebuffer, GGX lighting passes, emissive/ambient base pass, exposure and tonemap resolve.
- `pbr-gamepack`: The gamepack layout, the benchmark map, and the parity criterion against the engine.

### Modified Capabilities
<!-- No existing specs in openspec/specs/. -->

## Impact

- `plugins/shaders/shaders.cpp`, `shaders.h`, `plugin.cpp`: new parser, template fields, module registration.
- `include/ishaders.h`: extended `IShader` interface. All implementers (only `CShader` in the shaders plugin) must be updated.
- `include/irender.h`, `include/iglrender.h`: extended `RendererLight` and `GLProgram::setParameters`. Implementers in `plugins/entity/light.cpp` and `radiant/renderstate.cpp`.
- `plugins/entity/entity.h`, `entity.cpp`, `light.cpp`, `plugin.cpp`: new game type and light type.
- `radiant/renderstate.cpp`: new GLSL programs, pass construction for PBR materials, HDR target and resolve.
- `radiant/camwindow.cpp`: framebuffer lifecycle around the lighting draw mode; exposure preference.
- `radiant/textures.cpp`: gamma bypass for PBR textures.
- `setup/data/tools/gl/`: new GLSL files.
- New gamepack directory shipped outside the repo, plus a benchmark map.
- OpenGL: framebuffer objects are needed. The GL binding is `QOpenGLFunctions_2_0`; `QOpenGLFramebufferObject` from Qt covers the requirement without changing the binding. Half-float colour attachments require `GL_ARB_texture_float` or GL 3.0, which every target GPU has.
- Out of scope: shadow mapping, lightmap UVs, SH indirect, q3map2 changes, engine changes.
