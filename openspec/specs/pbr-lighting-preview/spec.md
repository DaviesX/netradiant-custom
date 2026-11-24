# pbr-lighting-preview Specification

## Purpose
The camera lighting draw mode for the PBR lighting preview (a per-game preference for Quake 3 games): physically based BRDF passes, HDR framebuffer, exposure and tonemapped resolve.

## Requirements

### Requirement: Lighting draw mode renders PBR materials with a physically based BRDF
When the camera is in lighting draw mode and the PBR lighting preview is active, each lit surface SHALL be shaded with the metallic-roughness BRDF: GGX normal distribution, Smith-Schlick geometry, Schlick Fresnel with F0 = mix(0.04, basecolor, metallic), and Disney diffuse. Area light passes are the exception: they SHALL use the Lambert form factor for diffuse and the LTC fit of GGX for specular, as specified below. Lit surfaces are those whose Quake 3 shader is lightmapped and opaque, as specified below. Normal mapping SHALL use the per-vertex tangent frame already supplied by brushes and patches.

#### Scenario: Metallic sphere
- **WHEN** a patch sphere uses a lightmapped shader with `qer_pbr_metallicFactor 1` and `qer_pbr_roughnessFactor 0.2` under a single point light
- **THEN** it shows a tight specular highlight tinted by its base colour and no diffuse term

#### Scenario: Rough dielectric
- **WHEN** a surface uses a lightmapped shader with `qer_pbr_metallicFactor 0` and `qer_pbr_roughnessFactor 1`
- **THEN** it shows a diffuse falloff with a broad, dim highlight

#### Scenario: Quake 3 shader with normal map
- **WHEN** a Quake 3 shader with `qer_pbr_normal` is viewed in lighting mode under `Q3.game`
- **THEN** it is shaded by the BRDF with its normal map applied

### Requirement: One additive pass per light plus a base pass
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, plus the sky term specified in "Sky light in the base pass", and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff. Area passes SHALL integrate the light's radiance over its rectangle as specified in "Area lights are shaded with linearly transformed cosines", zero beyond the cutoff radius from the centre and behind the emitting side. A light with `_proxy 1` SHALL get the pass of its own type (point, spot or sun), exactly as without the key.

When the light casts shadows, the pass SHALL multiply its contribution by the light's shadow term before accumulation. The base pass SHALL NOT be attenuated by any shadow term, so emissive, ambient and sky light remain visible in shadow.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point, spot and area cutoff and there is no sun
- **THEN** it renders with only its base pass: emissive, ambient and sky light

#### Scenario: Two overlapping lights
- **WHEN** two point lights both reach a surface
- **THEN** the result equals the base pass plus the sum of the two light contributions

#### Scenario: Surface fully in shadow
- **WHEN** a surface lies inside the cone and radius of a casting spot light but behind an occluder
- **THEN** it renders with only its base pass: emissive, ambient and sky light

#### Scenario: Emissive visible in shadow
- **WHEN** an emissive surface is occluded from every light
- **THEN** it still emits, because the base pass is not shadowed

#### Scenario: Area light without a frustum ignores occluders
- **WHEN** a pillar stands between a `light_area` with `_frustum 0` and a wall
- **THEN** the wall behind the pillar is still lit by the area light

### Requirement: HDR framebuffer and tonemapped resolve
In lighting draw mode the scene SHALL render into an off-screen target sized to the camera viewport, with three attachments:
- a half-float RGBA colour attachment;
- a sky-depth attachment, recording where a sky is the nearest depth-writing surface;
- a depth attachment the resolve can sample.

After the scene, the renderer SHALL draw a full-screen resolve that multiplies by exposure, applies the Uncharted 2 (Hable) tonemap operator with white point 11.2, encodes to sRGB, applies the fog of "Fog volumes in the preview", and writes to the window. The resolve SHALL also write the scene's depth into the window target, whatever the window's multisampling. Overlays (selection, grid, text) SHALL be drawn after the resolve so that they are neither tonemapped nor fogged, and are occluded by the scene.

#### Scenario: Bright light
- **WHEN** a point light of 10000 W sits close to a white wall
- **THEN** the wall shows a smooth roll-off to white rather than a hard clipped disc

#### Scenario: Viewport resized
- **WHEN** the camera window is resized
- **THEN** the framebuffer is reallocated to the new size before the next frame

#### Scenario: Textured mode unaffected
- **WHEN** the camera is in textured, solid, or wireframe draw mode
- **THEN** rendering goes directly to the window as before this change

#### Scenario: Overlays occluded at the default multisampling
- **WHEN** a selected brush stands behind a wall in lighting mode with the camera's default MSAA of 8
- **THEN** its selection overlay is hidden by the wall, as it is at MSAA 0

#### Scenario: No fog volumes
- **WHEN** a map without fog volumes and without a sky shader is viewed in lighting mode
- **THEN** the image is identical to the one before this change at camera MSAA 0

### Requirement: Exposure preference
The editor SHALL provide a camera preference "Lighting exposure" (float, default 1.0) and an ambient preference "Lighting ambient" (float, default 0.02) that affect only lighting draw mode. Changing either SHALL redraw the camera. Exposure SHALL NOT change the sky's colour (requirement "Sky surfaces show their own colour") or the fog colour.

#### Scenario: Exposure changed
- **WHEN** the exposure preference is doubled
- **THEN** the lighting view brightens by one stop before tonemapping, except the sky's pixels and the fog colour, whose bytes don't change

### Requirement: Framebuffer availability fallback
If framebuffer objects, any of the three attachments (half-float colour, 32-bit float sky depth, sampleable depth), or two draw buffers are unavailable on the current context, or the framebuffer is incomplete, the renderer SHALL fall back to rendering lighting mode directly to the window without tonemapping and without fog, and SHALL print one warning to the console that says the tonemap and fog are off. Sky surfaces SHALL then draw unlit at their own colour, the sky image's texel × the sky stage's `rgbGen const`.

#### Scenario: Unsupported context
- **WHEN** framebuffer creation fails
- **THEN** lighting mode still draws, without tonemapping or fog, a warning is printed once, and no further errors occur

### Requirement: Non-PBR games keep the existing lighting program
When the active game is a Doom 3 or Quake 4 game (material language `doom3` or `quake4`), lighting draw mode SHALL continue to use the existing Blinn-Phong omni program and direct-to-window rendering.

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active and lighting mode is enabled
- **THEN** rendering is identical to the behaviour before this change

### Requirement: PBR lighting preview preference
The editor SHALL provide a per-game camera preference, "PBR lighting preview" (boolean):
- for games whose `.game` declares `type="q3"`, it is shown, editable and defaults to on;
- for every other game type, it is hidden.

The preview is active while the preference is on. No `.game` key forces it on. A change takes effect without a restart. The "Lighting exposure", "Lighting ambient", "Lighting sky", "Lighting fog" and "Lighting shadows" preferences SHALL be shown exactly when this preference is shown.

#### Scenario: Quake 3 default
- **WHEN** the editor starts under `Q3.game` with no saved value for the preference
- **THEN** lighting draw mode is available and renders with the PBR program

#### Scenario: Turned off
- **WHEN** the preference is turned off under `Q3.game`
- **THEN** lighting draw mode is no longer offered; if the camera was in it, it returns to textured mode without a restart

#### Scenario: Per game
- **WHEN** the preference is turned off under one Quake 3 based game and the editor is restarted under another
- **THEN** the other game still has the preference on

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active
- **THEN** the preference does not appear and lighting mode uses the existing Doom 3 program

### Requirement: Lightmapped opaque shaders are lit in the preview
While the preview is active, a Quake 3 shader SHALL be shaded by the metallic-roughness BRDF exactly when it meets all of these:
- it is lightmapped: a `$lightmap` stage, or a bare texture with no script;
- it is not blended;
- it does not declare `surfaceparm sky`, `fog` or `nolightmap`.

This applies whether or not the shader is classified as PBR. A non-PBR shader SHALL use its base colour and the Quake 3 defaults. Alpha-tested shaders SHALL be lit, with their alpha test applied. No lit surface is blended.

#### Scenario: Bare baseq3 texture
- **WHEN** a brush face uses a baseq3 texture with no `qer_pbr_` keywords, near a light
- **THEN** it is lit and shaded like a fully rough dielectric, not drawn fullbright

#### Scenario: PBR keyword on a sky shader
- **WHEN** a sky shader declares a `qer_pbr_` keyword
- **THEN** it is still drawn as a sky and receives no light passes

#### Scenario: Shadowed stock surface
- **WHEN** a lit face lies behind an occluder from a casting light
- **THEN** it receives only the base pass from that light

### Requirement: Unlit surfaces keep their render state
While the preview is active, the following exceptions apply:
- emitters, as "Emitters the preview does not light are drawn in linear HDR" specifies;
- sky surfaces, which draw as "Sky surfaces show their own colour" specifies;
- fog faces, which "Fog faces are not drawn" removes.

Every other shader that isn't lit by the rule above SHALL render with the same render state it uses in textured mode (texture, blend, alpha test, cull), receive no light passes, and be drawn into the preview's framebuffer, where exposure and the tonemap apply to it as to everything else.

#### Scenario: Sky
- **WHEN** the camera sees a sky shader in lighting mode
- **THEN** a shader declaring `skyparms` receives no light passes and shows its own colour, while one with `surfaceparm sky` but no `skyparms` draws with its textured-mode state like any other unlit shader

#### Scenario: Blended glass
- **WHEN** a single-stage `blendFunc blend` glass shader is visible
- **THEN** it blends with its textured-mode state and receives no light passes

### Requirement: Area lights are shaded with linearly transformed cosines
An area light's pass SHALL shade each fragment at position P, with normal N, view direction V, base colour, metallic and perceptual roughness r (the clamped roughness the point pass uses), from the light's rectangle, which has centre c, normal n and half-extent axes hr = right × `width`/2 and hu = up × `height`/2. The outgoing radiance SHALL be zero when dot(P − c, n) ≤ 0 or |P − c| exceeds the effective radius. Otherwise it SHALL be

Lo = L × `_color` × ( (1 − metallic) × baseColor × FF + S × (F0 × t2.x + (1 − F0) × t2.y) ) × shadow term

where:
- F0 = mix(0.04, baseColor, metallic);
- the rectangle's corners are taken in the order c − hr − hu, c + hr − hu, c + hr + hu, c − hr + hu, relative to P, in the frame T1 = normalize(V − N (N·V)), T2 = N × T1, N;
- FF is the clipped, normalised form factor: the cosine-weighted solid angle of the rectangle above the fragment's horizon, divided by π, so FF lies in [0, 1], a receiver facing an infinite emitter gets FF = 1, and the irradiance is E = π × L × FF. It SHALL be computed by clipping the polygon to the horizon and summing the edge integral of the upstream LTC code (`IntegrateEdge`, which includes the 1/(2π)), clamped at 0;
- S is the same integral after transforming the corners by the inverse matrix M⁻¹ from table 1;
- t2 is table 2, and both tables are read at u = r and v = √(1 − N·V), with N·V clamped to [0, 1].

The diffuse term SHALL have no 1/π and no (1 − F) Fresnel weight, because there is no single half-vector over an area light.

The tables SHALL be read from `setup/data/tools/gl/ltc_ggx.bin`, converted unchanged from the GGX fit in `fit/results/ltc.js` of `github.com/selfshadow/ltc_code` (the 2017 revision of Heitz, Dupuy, Hill and Neubelt 2016, with the Fresnel fit of Hill and Heitz 2017):
- a 16-byte header (`LTC1`, then width 64, height 64 and table count 2 as little-endian `uint32`);
- then two 64 × 64 RGBA little-endian `float32` tables, entry a + 64 × t holding roughness index a and angle index t;
- table 1: the four variable entries of M⁻¹ normalised by its middle element, so that M⁻¹ has columns (t1.x, 0, t1.y), (0, 1, 0) and (t1.z, 0, t1.w);
- table 2: magnitude, Fresnel, 0 and the sphere-horizon term;
- indexed at (u, v) × 63/64 + 0.5/64, with u the perceptual roughness (upstream fits alpha = u²) and v = √(1 − N·V).

This file SHALL be the single source of the tables for the editor and `renderer_sh`, which reads it unchanged. The gamepack README SHALL record its SHA-256, the upstream repository, file and commit. The upstream licence's full copyright notice, conditions and disclaimer, with the paper citation it asks for, SHALL be in `setup/data/tools/gl/ltc_ggx.LICENSE.txt`, which installs beside the binary as `gl/ltc_ggx.LICENSE.txt`.

#### Scenario: Fireplace lights the room
- **WHEN** the benchmark is opened in lighting mode under `Q3.game` with `fs_game benchmark`, and the `fireplace` view is framed
- **THEN** the floor in front of the hearth, the firebox's sides, the ceiling and the walls facing the fire show an orange glow that is brightest near the fire, the wall behind the fire's plane gets none of it, and the glow disappears when the fireplace's `light_area` is deleted

#### Scenario: Nothing behind the light
- **WHEN** a surface lies within an area light's radius, behind the rectangle's plane (on the side its normal points away from), and faces the rectangle
- **THEN** both its diffuse and its specular terms from that light are 0

#### Scenario: Matches the point model far away
- **WHEN** a white Lambertian wall (metallic 0, roughness 1, base colour 1) faces a 16 by 16 area light of radiance 1 at a distance of 200 units on its normal, with ambient 0
- **THEN** the irradiance of its diffuse term, π × L × FF, is within 1% of L × A / d² = 0.0064 (the exact value is 0.006386)

#### Scenario: Close to the rectangle
- **WHEN** the same wall faces a 64 by 64 area light of radiance 1 at a distance of 64 units on its normal
- **THEN** FF is 0.2395 within 0.5%, so the irradiance is 0.752 and not L × A / d² = 1

#### Scenario: Highlight on a glossy floor
- **WHEN** a floor with `qer_pbr_roughnessFactor 0.2` lies below an area light
- **THEN** it shows a stretched specular reflection shaped by the rectangle, not a round point highlight

### Requirement: LTC table availability fallback
If float textures or eight texture image units are unavailable on the current context, or `ltc_ggx.bin` is missing or malformed, area lights SHALL be shaded with the diffuse term alone, and the editor SHALL print one warning to the console. Every other light SHALL be unaffected.

#### Scenario: Missing table file
- **WHEN** `gl/ltc_ggx.bin` is removed from the install and a map with a `light_area` is viewed in lighting mode
- **THEN** the area light still lights surfaces diffusely, one warning names the file, and no further errors occur

### Requirement: Emitters the preview does not light are drawn in linear HDR
While the preview is active, a shader with an emissive stage that the preview does not light SHALL be drawn in the preview's framebuffer with its emissive in linear light: the sRGB-decoded texel × `rgbGen const` × `qer_pbr_emissiveStrength`, as the base pass computes it for lit emitters. This applies when the shader is not sky or fog and either every map stage is additive or it declares `surfaceparm nolightmap`.
- If every map stage is additive, the emissive SHALL be added to the frame with the stage's blend source factor (`GL_ONE`, or `GL_SRC_ALPHA` for a `GL_SRC_ALPHA GL_ONE` stage) and destination `GL_ONE`, with a depth test and no depth write, after opaque surfaces.
- Otherwise, if the shader is not blended (no `qer_trans` and no `blendFunc blend` base stage), the surface SHALL be drawn opaque as the sRGB-decoded base colour × base colour factor plus the emissive.
- A blended shader that is not all-additive SHALL keep its textured-mode state.

Such surfaces SHALL receive no light passes. A shader whose emissive stage has `rgbGen const ( 0 0 0 )` has no emissive.

#### Scenario: Additive flame
- **WHEN** a quad uses a shader whose only stage is `map textures/x/flame` with `blendFunc add` and `qer_pbr_emissiveStrength 4`
- **THEN** it adds 4 × the linear texel to what lies behind it, and it does not occlude what lies behind it

#### Scenario: Nolightmap sign
- **WHEN** a face uses a `surfaceparm nolightmap` shader with an opaque base stage and an additive glow stage, `rgbGen const ( 1 0 0 )` and `qer_pbr_emissiveStrength 8`
- **THEN** it shows its base fullbright in linear light plus 8 × the red glow, and doubling the strength doubles the glow

#### Scenario: Blended nolightmap window with a glow
- **WHEN** a face uses a `surfaceparm nolightmap` shader with `qer_trans 0.5`, a base stage and an additive glow stage
- **THEN** it is drawn as in textured mode, blended and without depth write, and it hides nothing behind it

### Requirement: Sky light in the base pass
While "Lighting sky" is on and the map has sky light (sky-environment-light), the base pass of every lit surface SHALL add (1 − metallic) × base colour × occlusion × E(n) / π. Here E is the sky irradiance and n is the surface's interpolated vertex normal, rotated to world space, without the normal map. The term SHALL NOT be shadowed. Emitters and unlit surfaces SHALL NOT receive it. The sky term SHALL NOT change how the sky surface itself draws.

#### Scenario: Open floor under a cloud sky
- **WHEN** a white (base colour 1), non-metallic, unoccluded floor faces +Z under a cloud-layer sky of radiance L, with ambient 0 and no light reaching it
- **THEN** its base pass writes a linear radiance of L within 1%, read before exposure and tonemapping

#### Scenario: Ceiling sees no sky light
- **WHEN** the same material faces −Z under that sky
- **THEN** its base pass adds no sky light

#### Scenario: Rotated model
- **WHEN** a lit `misc_model` with a +Z top face is rotated 90° about X, so that the top faces −Y
- **THEN** that face receives the sky light of a −Y normal, not of +Z

#### Scenario: Interiors are overlit
- **WHEN** a sealed room's floor faces +Z in a map whose sky emits
- **THEN** the floor receives the full open-sky light, because the preview has no sky visibility

#### Scenario: Legacy map unchanged
- **WHEN** a map whose sky shader has no `qer_pbr_emissiveStrength` is viewed in lighting mode
- **THEN** the image is identical to the same view with "Lighting sky" off

### Requirement: Sky light preference
The editor SHALL provide a per-game camera preference, "Lighting sky" (boolean, default on), that affects only lighting draw mode. Turning it off SHALL remove the sky term from the base pass and nothing else. A change SHALL redraw the camera. Editing the map or reloading shaders SHALL update the sky light at the next redraw, with no other action.

#### Scenario: Turned off
- **WHEN** "Lighting sky" is turned off
- **THEN** the lighting view equals a view of the same map whose sky shader has no `qer_pbr_emissiveStrength`

#### Scenario: Shader edited
- **WHEN** the sky shader's `qer_pbr_emissiveStrength` is doubled and shaders are reloaded
- **THEN** the sky term doubles at the next redraw

### Requirement: Fog volumes in the preview
While "Lighting fog" is on, lighting mode SHALL fog the map's fog volumes (fog-volume) in the resolve, after the tonemap curve and the sRGB encode. Each pixel SHALL be composited as fog-volume's "Fog is composited in display space after the tonemap" defines, using its nearest depth-writing surface and the camera's position and view direction. Editing the map or reloading shaders SHALL update the fog volumes at the next redraw, with no other action, and SHALL NOT walk the scene on frames where nothing changed.

#### Scenario: Town street seam
- **WHEN** the town's `street` view is captured in lighting mode with shadows on, ambient 0 and "Lighting fog" on
- **THEN** the far facade just below the ceiling at the image's horizontal centre and the sky just above it differ by no more than 8 in any 8-bit channel

#### Scenario: Interiors outside the volume
- **WHEN** the camera is in one of the town's sealed rooms
- **THEN** the view is identical with "Lighting fog" on and off

#### Scenario: Fog shader edited
- **WHEN** the town fog's `fogparms` distance is halved and shaders are reloaded
- **THEN** the next redraw fogs with the new distance

### Requirement: Sky surfaces show their own colour
In lighting mode, surfaces whose shader declares `skyparms` SHALL be drawn in their usual place in the scene, unlit and without the camera headlight, whatever the lit, emitter and unlit rules would do with the shader. Each sky pixel's final colour SHALL equal the sky image's texel × the sky stage's `rgbGen const` (white for a farbox), in 8-bit sRGB, within 1 in each channel, whatever the exposure. The sky image is the farbox face, or for a cloud layer the sky stage's image (sky-environment-light: the first map stage that is not `$lightmap`) at the face's texture coordinates. The `rgbGen const` multiplies the 8-bit texel, as stock does, before anything else. The sky SHALL be marked so that the fog leaves it alone, but not where an opaque surface is drawn in front of it, in whichever order the two are drawn. Translucent surfaces drawn later SHALL still blend over it.

#### Scenario: Town sky
- **WHEN** the town's sky is seen in lighting mode at any exposure
- **THEN** every sky pixel is `sky.tga`'s 51 66 59 within 1

#### Scenario: Farbox sky
- **WHEN** the benchmark's farbox sky is seen in lighting mode
- **THEN** its pixels equal the farbox images' texels in the drawn directions within 1

#### Scenario: Glass and glows in front of the sky
- **WHEN** a blended glass pane or an additive glow lies between the camera and a sky face
- **THEN** it still draws over the sky

### Requirement: Fog faces are not drawn
In lighting mode, a face whose shader declares `fogparms` SHALL NOT be drawn, whatever the preview's other rules (lit, emitter or unlit) would do with its shader. Its selection highlight SHALL still be drawn. Textured mode and the 2D views are unchanged.

#### Scenario: Fog face
- **WHEN** the camera sees a face whose shader declares `fogparms` in lighting mode
- **THEN** the face is not drawn, while a selection highlight on it still is

#### Scenario: Lightmapped fog shader
- **WHEN** a shader declares `fogparms` and a `$lightmap` stage but not `surfaceparm fog`
- **THEN** its faces are still not drawn in lighting mode

### Requirement: Fog preference
The editor SHALL provide a per-game camera preference, "Lighting fog" (boolean, default on), that affects only lighting draw mode. Turning it off SHALL remove the fog and nothing else: fog faces stay undrawn, and the sky still shows its own colour. A change SHALL redraw the camera.

#### Scenario: Turned off
- **WHEN** "Lighting fog" is turned off on a map whose fog brushes carry the fog shader on every face, such as the town or the benchmark
- **THEN** the lighting view equals the same view of the map with its fog brushes deleted
