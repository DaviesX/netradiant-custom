# pbr-lighting-preview Specification

## Purpose
The camera lighting draw mode for the PBR lighting preview (a per-game preference for Quake 3 games): physically based BRDF passes, HDR framebuffer, exposure and tonemapped resolve.

## Requirements

### Requirement: Lighting draw mode renders PBR materials with a physically based BRDF
When the camera is in lighting draw mode and the PBR lighting preview is active, each lit surface SHALL be shaded with the metallic-roughness BRDF: GGX normal distribution, Smith-Schlick geometry, Schlick Fresnel with F0 = mix(0.04, basecolor, metallic), and Disney diffuse. Lit surfaces are those whose Quake 3 shader is lightmapped and opaque, as specified below. Normal mapping SHALL use the per-vertex tangent frame already supplied by brushes and patches.

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
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff.

When the light casts shadows, the pass SHALL multiply its contribution by the light's shadow term before accumulation. The base pass SHALL NOT be attenuated by any shadow term, so emissive and ambient remain visible in shadow.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point and spot cutoff and there is no sun
- **THEN** it renders with only its emissive and ambient base pass

#### Scenario: Two overlapping lights
- **WHEN** two point lights both reach a surface
- **THEN** the result equals the base pass plus the sum of the two light contributions

#### Scenario: Surface fully in shadow
- **WHEN** a surface lies inside the cone and radius of a casting spot light but behind an occluder
- **THEN** it renders with only its emissive and ambient base pass

#### Scenario: Emissive visible in shadow
- **WHEN** an emissive surface is occluded from every light
- **THEN** it still emits, because the base pass is not shadowed

### Requirement: HDR framebuffer and tonemapped resolve
In lighting draw mode the scene SHALL render into an off-screen half-float RGBA colour attachment with a depth attachment sized to the camera viewport. After the scene, the renderer SHALL draw a full-screen resolve that multiplies by exposure, applies the Uncharted 2 (Hable) tonemap operator with white point 11.2, encodes to sRGB, and writes to the window. Overlays (selection, grid, text) SHALL be drawn after the resolve so they are not tonemapped.

#### Scenario: Bright light
- **WHEN** a point light of 10000 W sits close to a white wall
- **THEN** the wall shows a smooth roll-off to white rather than a hard clipped disc

#### Scenario: Viewport resized
- **WHEN** the camera window is resized
- **THEN** the framebuffer is reallocated to the new size before the next frame

#### Scenario: Textured mode unaffected
- **WHEN** the camera is in textured, solid, or wireframe draw mode
- **THEN** rendering goes directly to the window as before this change

### Requirement: Exposure preference
The editor SHALL provide a camera preference "Lighting exposure" (float, default 1.0) and an ambient preference "Lighting ambient" (float, default 0.02) that affect only lighting draw mode. Changing either SHALL redraw the camera.

#### Scenario: Exposure changed
- **WHEN** the exposure preference is doubled
- **THEN** the lighting view brightens by one stop before tonemapping

### Requirement: Framebuffer availability fallback
If framebuffer objects or half-float colour attachments are unavailable on the current context, the renderer SHALL fall back to rendering lighting mode directly to the window without tonemapping and SHALL print a warning once to the console.

#### Scenario: Unsupported context
- **WHEN** framebuffer creation fails
- **THEN** lighting mode still draws, a warning is printed once, and no further errors occur

### Requirement: Non-PBR games keep the existing lighting program
When the active game is a Doom 3 or Quake 4 game (material language `doom3` or `quake4`), lighting draw mode SHALL continue to use the existing Blinn-Phong omni program and direct-to-window rendering.

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active and lighting mode is enabled
- **THEN** rendering is identical to the behaviour before this change

### Requirement: PBR lighting preview preference
The editor SHALL provide a per-game camera preference, "PBR lighting preview" (boolean):
- for games whose `.game` declares `type="q3"`, it is shown, editable and defaults to on;
- for every other game type, it is hidden.

The preview is active while the preference is on. No `.game` key forces it on. A change takes effect without a restart. The "Lighting exposure", "Lighting ambient" and "Lighting shadows" preferences SHALL be shown exactly when this preference is shown.

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
While the preview is active, shaders that aren't lit by the rule above SHALL render with the same render state they use in textured mode (texture, blend, alpha test, cull), receive no light passes, and be drawn into the preview's framebuffer, where exposure and the tonemap apply to them as to everything else.

#### Scenario: Sky
- **WHEN** the camera sees a sky shader in lighting mode
- **THEN** it draws with its textured-mode state and receives no light passes

#### Scenario: Blended glass
- **WHEN** a single-stage `blendFunc blend` glass shader is visible
- **THEN** it blends with its textured-mode state and receives no light passes
