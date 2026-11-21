# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
The renderer SHALL emit a base pass per lit surface writing emissive times emissive factor and strength plus a constant ambient times base colour, and one additive pass per light affecting the surface. Point passes SHALL convert radiant flux to radiant intensity as flux / (4 pi) and spot passes as flux / (2 pi (1 - cosOuter)), then apply inverse-square falloff over the distance in map units, zero beyond the cutoff radius. Spot passes SHALL apply a smoothstep between the outer and inner cone cosines. The sun pass SHALL apply no falloff. Area passes SHALL integrate the light's radiance over its rectangle as specified in "Area lights are shaded with linearly transformed cosines", zero beyond the cutoff radius from the centre and behind the emitting side. A light with `_proxy 1` SHALL get the pass of its own type (point, spot or sun), exactly as without the key.

When the light casts shadows, the pass SHALL multiply its contribution by the light's shadow term before accumulation. The base pass SHALL NOT be attenuated by any shadow term, so emissive and ambient remain visible in shadow.

#### Scenario: Surface outside all lights
- **WHEN** a lit surface lies outside every point, spot and area cutoff and there is no sun
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

#### Scenario: Area light without a frustum ignores occluders
- **WHEN** a pillar stands between a `light_area` with `_frustum 0` and a wall
- **THEN** the wall behind the pillar is still lit by the area light

### Requirement: Unlit surfaces keep their render state
While the preview is active, shaders that aren't lit by the rule above, other than the emitters specified in "Emitters the preview does not light are drawn in linear HDR", SHALL render with the same render state they use in textured mode (texture, blend, alpha test, cull), receive no light passes, and be drawn into the preview's framebuffer, where exposure and the tonemap apply to them as to everything else.

#### Scenario: Sky
- **WHEN** the camera sees a sky shader in lighting mode
- **THEN** it draws with its textured-mode state and receives no light passes

#### Scenario: Blended glass
- **WHEN** a single-stage `blendFunc blend` glass shader is visible
- **THEN** it blends with its textured-mode state and receives no light passes
