## ADDED Requirements

### Requirement: Sky environment light in both bakes
`-shbake` SHALL light both bakes from the map's sky shader, with the derivation of the `sky-environment-light` capability.

**Which sky counts.** The candidates SHALL be the distinct shaders in the BSP's shader list that declare `skyparms`. A shader with `surfaceparm sky` but no `skyparms` SHALL be no candidate. The map's sky SHALL be the alphabetically first candidate by shader name, compared without case. With two or more candidates, the stage SHALL print one warning naming every candidate and the chosen one.

**Its radiance.** The sky's radiance L(ω) SHALL be `qer_pbr_emissiveStrength` × C × T(ω), as that capability defines it:
- the strength must be declared and positive;
- C is the sky stage's `rgbGen const`;
- T(ω) comes from the six farbox images when `skyparms` names a farbox (any first argument other than `-`), else from the sky stage's image mean over the upper hemisphere;
- the sky stage is the first map stage that is not `$lightmap` and uses no rend2 keyword.

**When there is no sky light.** In each of these cases the bakes SHALL get no sky light:
- there is no candidate;
- the sky has no declared positive strength;
- a farbox image fails to load, or the cloud image is missing or fails to load.

Sky images SHALL be loaded only when the sky's strength is declared and positive, so a legacy sky never warns about its images.

The last case prints one warning naming the image or the farbox. With no sky light, the stage's output SHALL be byte-identical to its output before this change, for any map whose sky shaders have no additive stage.

**How the bakes get it.** When the sky emits, both bakes SHALL get the sky as a radiance on escaping rays, so that:
- the full bake's lightmap pages, vertex colours and light grid hold direct and bounced sky light;
- the side file holds bounced sky light only: sky reached after at least one surface reflection.

sh-baker SHALL receive L(ω) sampled into texels, never the sky's SH, whose truncation is negative below a cloud layer's horizon. Sky surfaces SHALL stay non-casting and out of the bake, so rays leave through them.

**What is printed.** The scene summary SHALL print:
- the sky shader, its kind (farbox or cloud layer), its strength, and its irradiance at +Z from the derivation;
- the irradiance at +Z that the texels sampled for sh-baker imply.

If the texels' irradiance at ±X, ±Y or ±Z differs from the derivation's by more than 1% of the largest of the derivation's six irradiances, in any channel, the stage SHALL print a warning. The scale is the largest of the six so that a sky lit mostly from the side or from below, with little irradiance at +Z, draws no spurious warning.

#### Scenario: Open floor under a cloud sky
- **WHEN** a sealed scratch map whose walls and ceiling are a cloud-layer sky shader, with `qer_pbr_emissiveStrength` s and an image of mean linear value M, holds a floor and no lights, and is baked with `-shbake -shsamples 2048`
- **THEN** every floor luxel has the lightmap bytes the encoding gives for an irradiance of π s M, within one byte per channel, their mean is within 0.1 of the unrounded encoding, and their side-file L0 is exactly 0, since they see no surface that could bounce sky light to them (the bake is Monte Carlo: at the default 128 samples the bytes scatter by about ±2 around the same mean, hence the sample count)

#### Scenario: Bounced sky light in the side file
- **WHEN** the same map holds a tall white block on the floor, and the floor luxel beside the block is baked with and without the sky's `qer_pbr_emissiveStrength`
- **THEN** its side-file L0 is above 0 with the strength and exactly 0 without it

#### Scenario: Nothing from below a cloud horizon
- **WHEN** the open-floor map holds a white overhang whose underside faces down, above a black floor
- **THEN** the underside's lightmap bytes are 0 0 0

#### Scenario: Farbox orientation
- **WHEN** a farbox sky has a non-black `_rt` face and five black faces, and two walls of the same albedo face +X and −X under it, with nothing in front of either
- **THEN** the +X wall's lightmap bytes are above the −X wall's

#### Scenario: Legacy sky unchanged
- **WHEN** the benchmark, whose sky declares no `qer_pbr_emissiveStrength` and has no additive stage, is baked single-threaded with `-shbake` by q3map2 built before and after this change
- **THEN** the BSPs and the side files are byte-identical, apart from the q3map2 version stamp

#### Scenario: Legacy sky loads no images
- **WHEN** a map's sky names a farbox whose images don't exist and declares no `qer_pbr_emissiveStrength`
- **THEN** the stage prints no warning about the farbox or its images

#### Scenario: Missing farbox face
- **WHEN** a farbox sky with a strength lacks its `_dn` image
- **THEN** the stage warns once naming the farbox, and the output is byte-identical to that of the same map with the sky's strength removed

#### Scenario: Two skies
- **WHEN** a map uses `textures/b/sky` and `textures/a/sky`, both with `skyparms`
- **THEN** the stage warns once naming both, and the bake uses `textures/a/sky`

#### Scenario: The town's sky
- **WHEN** the town is baked with `-shbake`
- **THEN** the summary names `textures/town/sky` as a cloud layer of strength 14.6, and gives an irradiance at +Z of (1.518, 2.499, 2.006) within 0.1% from the derivation and within 1% from the texels, with no cross-check warning

## MODIFIED Requirements

### Requirement: Materials from shader text
Each draw surface's bake material SHALL come from its shader as the editor derives it:
- the base colour is the stage the lightmap multiplies, else the first non-lightmap stage with a map, times `qer_pbr_baseColorFactor`;
- alpha is the base stage's `alphaFunc` test of its image, and fully opaque without one;
- the emissive is the first additive stage that is not the lightmap, a white image or an environment map, coloured by its `rgbGen const`, with strength `qer_pbr_emissiveStrength`.

A shader that declares `skyparms` SHALL have no emissive: its light comes only from the sky environment light, so its cloud stages, additive or not, never make it an area light or draw the unattached-emitter warning.

A shader SHALL emit without occluding only if every stage is additive, or if it doesn't cast under the caster rule. An image that can't be loaded SHALL fall back to white with a warning. The bake's shading is Lambert: normal, metallic-roughness and occlusion maps SHALL NOT change it.

#### Scenario: Emissive sign lights the wall
- **WHEN** the benchmark's neon-sign shader, with its additive stage and `qer_pbr_emissiveStrength`, is baked
- **THEN** the sign is an area light in the bake, and the wall luxels next to it are brighter than with the sign's emissive strength set to 0

#### Scenario: Opaque sign still occludes
- **WHEN** a shader has an opaque lightmapped base stage plus an additive glow stage, and a spot shines at it from in front
- **THEN** the wall behind it gets no direct light from the spot

#### Scenario: Additive cloud layer is no emitter
- **WHEN** a map's sky shader declares `skyparms - 512 -`, no `qer_pbr_emissiveStrength` and a second stage with `blendFunc add`, and is baked single-threaded with `-shbake`
- **THEN** no unattached-emitter warning names it, and the BSP and side file are byte-identical to those of the same map with that stage removed

### Requirement: Bake toggles
`-shbake` SHALL run two bakes, each one sh-baker call over the same point list:
- the *full* bake, with `indirect_only` false;
- the *indirect* bake, with `indirect_only` true. It drops all direct light at the baked point: punctual, emissive and direct sky light. Bounces still see every light and the sky.

`-shnofull` SHALL skip the full bake and `-shnoindirect` the indirect bake. Both may be given, and then nothing is baked.
- Without the full bake, every lightmap luxel a surface uses and every baked vertex colour SHALL be the neutral 64 64 64, a multiplier of 1.0 on opengl1 in a window. The same luxels SHALL be used, and the same left at 0 0 0, as with the full bake. Every grid point outside solid SHALL get ambient 63 63 63, directed 0 0 0 and direction bytes 0 0: the brightest ambient the engine reaches. The ambient is the grid fit's ambient for a target of byte 64, which takes the clamp of 63; the directed light is 0, not the 18 the fit would give for that target, so that no direction is favoured. Grid points in solid stay black.
- Without the indirect bake, no side file SHALL be written. Every `-shbake` run SHALL first remove an earlier run's side file and L0 dumps for the map, so only its own outputs remain.

The two bakes SHALL NOT change each other's output.

#### Scenario: No indirect bake
- **WHEN** the benchmark is baked single-threaded with `-shbake` and with `-shbake -shnoindirect`
- **THEN** the BSPs are byte-identical, and the second run leaves no `maps/bench/bench.shsf`, even in the directory where the first run wrote one

#### Scenario: No full bake
- **WHEN** the benchmark is baked single-threaded with `-shbake` and with `-shbake -shnofull`
- **THEN** the side files are byte-identical, the BSPs have the same lightmap page count and the same lightmap index and luxel position on every surface, and every luxel a surface uses in the second BSP's pages is 64 64 64

#### Scenario: No full bake with sky light
- **WHEN** the town is baked single-threaded with `-shbake` and with `-shbake -shnofull`
- **THEN** the side files are byte-identical

#### Scenario: Neither bake
- **WHEN** the benchmark is baked with `-shbake -shnofull -shnoindirect`
- **THEN** the log reports no bake, every luxel a surface uses in the lightmap pages is 64 64 64, and no side file is written

### Requirement: Side file output
When the indirect bake runs, the stage SHALL write the side file to `maps/<name>/<name>.shsf` beside the BSP, through the q3map2 extension writer.

**What it holds.** It always holds the indirect bake: no direct light at the baked point, whether punctual, emissive or from the sky. It does hold bounced sky light when the map's sky emits. It holds:
- the SH pages;
- the un-shrunk hull mesh in BSP units, with hull triangles facing outward and q3map2's winding;
- the SH grid.

**The SH grid.** It has the BSP light grid's origin, spacing and dimensions, and one probe per BSP grid point in the same order (x fastest, then y, then z). Each probe holds that point's full-sphere SH from the indirect bake. A probe whose grid point is still in solid after q3map2's nudge SHALL be marked invalid, with zero coefficients. With `-nogrid` the SH grid has no probes, dimensions 0, 0, 0, and origin and spacing 0.

**Failures and other files.** A writer failure SHALL stop q3map2 with the writer's message. The stage SHALL NOT write any `lm_*.hdr` file.

#### Scenario: Side file written
- **WHEN** the benchmark is baked with `-shbake`
- **THEN** `maps/bench/bench.shsf` exists and reads back with the extension reader, and no `lm_*.hdr` file exists under `maps/`

#### Scenario: No direct light in the side file
- **WHEN** a floor luxel in a spot's direct beam is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: No direct sky light in the side file
- **WHEN** a floor luxel under open sky in the town is baked with `-shbake`
- **THEN** its side-file L0 is far below what the full bake's irradiance at that luxel implies

#### Scenario: SH grid matches the BSP grid
- **WHEN** the benchmark is baked with `-shbake` and its side file read back
- **THEN** the SH grid's dimensions and probe count equal the BSP light grid's, a probe in a lit room is valid with non-zero L0, and a probe whose grid point is black in the BSP because it is in solid is invalid
