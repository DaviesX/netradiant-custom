# pbr-lighting-preview Specification

## Purpose
The camera lighting draw mode for `pbr` games: physically based BRDF passes, HDR framebuffer, exposure and tonemapped resolve.

## Requirements

### Requirement: Lighting draw mode renders PBR materials with a physically based BRDF
When the camera is in lighting draw mode and the active material language is `pbr`, each surface with a PBR material SHALL be shaded with the metallic-roughness BRDF: GGX normal distribution, Smith-Schlick geometry, Schlick Fresnel with F0 = mix(0.04, basecolor, metallic), and Disney diffuse. Normal mapping SHALL use the per-vertex tangent frame already supplied by brushes and patches.

#### Scenario: Metallic sphere
- **WHEN** a patch sphere uses a material with `metallicfactor 1` and `roughnessfactor 0.2` under a single point light
- **THEN** it shows a tight specular highlight tinted by its base colour and no diffuse term

#### Scenario: Rough dielectric
- **WHEN** a surface uses `metallicfactor 0` and `roughnessfactor 1`
- **THEN** it shows a diffuse falloff with a broad, dim highlight

### Requirement: One additive pass per light plus a base pass
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point and spot cutoff and there is no sun
- **THEN** it renders with only its emissive and ambient base pass

#### Scenario: Two overlapping lights
- **WHEN** two point lights both reach a surface
- **THEN** the result equals the base pass plus the sum of the two light contributions

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
When the active material language is `doom3` or `quake4`, lighting draw mode SHALL continue to use the existing Blinn-Phong omni program and direct-to-window rendering.

#### Scenario: Doom 3 game
- **WHEN** a Doom 3 gamepack is active and lighting mode is enabled
- **THEN** rendering is identical to the behaviour before this change
