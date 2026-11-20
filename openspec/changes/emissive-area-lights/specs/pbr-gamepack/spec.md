# Spec Delta

## ADDED Requirements

### Requirement: Area and proxy light model documented in the gamepack README
The pack's root `README.md` SHALL document:
- **the `light_area` model:** its keys, the `AngleVectors` basis with roll, radiance units, flux π × L × A, E ≈ L × A / d² on the normal far from the rectangle (and the exact form factor close to it, for example E = 0.752 for the defaults at 64 units), the cutoff from the centre and its clamp to the half-diagonal, one-sided emission, and the shading formula with the normalised form factor for diffuse (no 1/π, no Fresnel weight) and LTC GGX with the tables' Fresnel fit for specular;
- **the LTC tables:** the upstream repository (`github.com/selfshadow/ltc_code`), file (`fit/results/ltc.js`) and commit, the upstream licence's name and the paper citation it asks for, and that the licence's full notice installs beside the binary as `gl/ltc_ggx.LICENSE.txt` (the README is not installed, so it is not the shipped notice), the file format and layout of `setup/data/tools/gl/ltc_ggx.bin` (header, the two tables and their components), the indexing (u = perceptual roughness, v = √(1 − N·V), scale 63/64 and bias 0.5/64), its SHA-256, that `renderer_sh` reads the same file unchanged, and the diffuse-only fallback;
- **the derivation:** L̄ = `qer_pbr_emissiveStrength` × emissive colour (white without `rgbGen const`) × mean linear texel over the face's UV footprint, its split into `intensity` and `_color`, the created area light's `radius`, and the proxy flux k × π × Σ L̄ × A with k = 1 for opaque and k = 2 for all-additive emitters;
- **the proxies:** which selections make a hemisphere `light_spot` (`cone 90`, `cone_inner 89`, `_shadows 0`) and which a `light`, and their on-axis residual: about LA/(2d²) against the emitter's LA/d² for both kinds, with the right total flux, and a flat spot intensity off the axis where a Lambertian face falls as cos θ;
- **the three commands, the attachment test Re-derive uses, and their refusals;**
- **the difference from the bake:** `-shbake` and the placeholder `-light` ignore `light_area` entities and `light`, `light_spot` and `light_sun` entities whose `_proxy` starts with `1`, and sh-baker's area lights are two-sided while `light_area` is one-sided;
- **emitter drawing:** how the preview draws emitters it does not light.

#### Scenario: Reader can reproduce the area light
- **WHEN** a reader implements an area light from the README alone
- **THEN** every formula, constant, basis convention and table detail the editor uses is stated, with its source

## MODIFIED Requirements

### Requirement: Shadow parameters documented in the gamepack README
The pack's root `README.md` SHALL document the shadow scheme so it can be compared against the engine: the caster culling mode, the normal-offset and slope-scaled bias constants, the PCF kernel, the cascade count, resolution, split lambda, texel snapping and near-plane padding, the spot atlas dimensions and per-light tile size, the area light's `_frustum` projection (field of view, its clamp, near and far planes, shared tile, NdotL toward the centre, receivers outside the frustum lit), and the editor's caster rule. It SHALL state which values match `sh-renderer` and which are editor-side choices.

#### Scenario: Reader can reproduce the scheme
- **WHEN** a reader follows the README's shadow section
- **THEN** every constant used by the editor's shadow path is listed with its value and its source
