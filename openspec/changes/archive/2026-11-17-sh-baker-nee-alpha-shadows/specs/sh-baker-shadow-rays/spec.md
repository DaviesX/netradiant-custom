# Spec Delta

## Purpose

How sh-baker's direct-light visibility treats the surfaces a shadow ray crosses, so that fences and leaf cards cast shadows with holes, consistent with how the path tracer's indirect rays see through alpha.

## ADDED Requirements

### Requirement: Shadow rays pass through alpha
A direct-light sample's visibility SHALL be a transmittance in [0, 1]: the product of `1 - alpha` over every surface the shadow ray crosses. Alpha SHALL be the same coverage the indirect rays read at the hit point: the alpha channel of the material's albedo at the hit's texture UV. A texture without an alpha channel, or an empty one, is opaque. The transmittance SHALL be uncoloured.

#### Scenario: Half-covered card
- **WHEN** a receiver point faces a point light through one quad whose albedo alpha is 128/255 everywhere
- **THEN** its direct light is 1 - 128/255 of its value without the quad, within 1%

#### Scenario: Two cards multiply
- **WHEN** the same receiver sees the light through two such quads, one behind the other
- **THEN** its direct light is (1 - 128/255)² of its value without the quads, within 1%

#### Scenario: Fully cut-out card
- **WHEN** the quad's albedo alpha is 0 everywhere
- **THEN** the direct light equals its value without the quad, within 1%

#### Scenario: Cut-out fence
- **WHEN** a floor is lit by a directional light through a fence quad whose RGBA albedo is opaque on half its texels and has alpha 0 on the other half, in stripes much wider than a texel
- **THEN** floor points behind the open stripes get the unshadowed direct light and points behind the opaque stripes get none

### Requirement: Opaque and material-less hits block
A shadow ray that crosses a surface with alpha 1, or a material-less occluder, SHALL have transmittance 0 and contribute no direct light, whatever else it crosses.

#### Scenario: Opaque RGBA card
- **WHEN** the quad's albedo is RGBA with alpha 255 everywhere
- **THEN** the receiver gets no direct light from that light

#### Scenario: Hull behind a cut-out
- **WHEN** a material-less closed box sits between the alpha-0 quad and the light
- **THEN** the receiver gets no direct light from that light

#### Scenario: Plain RGB card
- **WHEN** the quad's albedo is RGB, with no alpha channel
- **THEN** the receiver gets no direct light from that light

#### Scenario: Opaque surface just behind a cut-out
- **WHEN** an opaque RGBA quad lies 0.0005 units behind an alpha-0 quad on the shadow ray
- **THEN** the shadow ray's transmittance is 0

### Requirement: Only surfaces between the point and the light count
Transmittance SHALL account only for surfaces the shadow ray crosses within its extent: between the shaded point and the sampled light position, or the whole ray for a directional light or sky. A surface beyond the light SHALL not attenuate it.

#### Scenario: Card beyond the light
- **WHEN** an opaque quad lies on the line from the receiver through a point light, but farther away than the light
- **THEN** the receiver's direct light equals its value without the quad, within 1%

### Requirement: Applies at bake points and at bounces
Transmittance SHALL apply to every direct-light sample: those taken at the bake points themselves, and those taken at each bounce of an indirect path. It SHALL apply to every light type sh-baker bakes (point, spot, directional, area).

#### Scenario: Bake point behind a card
- **WHEN** a single bake point is baked with a point light behind a 128/255-alpha quad and no indirect bounces
- **THEN** its L0 is 1 - 128/255 of its value without the quad, within the bake's noise tolerance

#### Scenario: Bounce point behind a card
- **WHEN** a path vertex at a bounce evaluates direct light from a point light behind a 128/255-alpha quad
- **THEN** that direct-light estimate is 1 - 128/255 of its value without the quad, within 1%

### Requirement: Walk terminates
The visibility walk SHALL end after a bounded number of surface crossings. A shadow ray that crosses more than 64 surfaces with alpha below 1 SHALL count as blocked.

#### Scenario: Many stacked cards
- **WHEN** a shadow ray crosses 100 parallel alpha-0 quads before reaching the light
- **THEN** the visibility walk returns and its transmittance is 0, while the same ray through 64 such quads has transmittance 1

### Requirement: Output unchanged without translucent crossings
Where no shadow ray crosses a surface with alpha below 1, every bake result SHALL be byte-identical to the commit before this change, and the visibility test SHALL draw no random numbers. The pre-existing tests SHALL pass with the same results, changing only to pass the new argument.

#### Scenario: Plain scenes
- **WHEN** the CLI bakes `data/box/scene.gltf` and `data/MultiLights/scene.gltf` with fixed arguments before and after the change
- **THEN** every output file is byte-identical

#### Scenario: Existing tests
- **WHEN** the pre-existing tests are built and run against the changed library
- **THEN** each one has the same result as on the commit before the change, and the diff of those tests touches only the NEE calls' argument lists
