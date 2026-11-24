# Spec Delta

## MODIFIED Requirements

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

### Requirement: Framebuffer availability fallback
If framebuffer objects, any of the three attachments (half-float colour, 32-bit float sky depth, sampleable depth), or two draw buffers are unavailable on the current context, or the framebuffer is incomplete, the renderer SHALL fall back to rendering lighting mode directly to the window without tonemapping and without fog, and SHALL print one warning to the console that says the tonemap and fog are off. Sky surfaces SHALL then draw unlit at their own colour, the sky image's texel × the sky stage's `rgbGen const`.

#### Scenario: Unsupported context
- **WHEN** framebuffer creation fails
- **THEN** lighting mode still draws, without tonemapping or fog, a warning is printed once, and no further errors occur

### Requirement: Exposure preference
The editor SHALL provide a camera preference "Lighting exposure" (float, default 1.0) and an ambient preference "Lighting ambient" (float, default 0.02) that affect only lighting draw mode. Changing either SHALL redraw the camera. Exposure SHALL NOT change the sky's colour (requirement "Sky surfaces show their own colour") or the fog colour.

#### Scenario: Exposure changed
- **WHEN** the exposure preference is doubled
- **THEN** the lighting view brightens by one stop before tonemapping, except the sky's pixels and the fog colour, whose bytes don't change

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

## ADDED Requirements

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
