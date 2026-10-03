## MODIFIED Requirements

### Requirement: Benchmark materials are vanilla shaders with PBR keywords
Every benchmark material SHALL be a `scripts/bench.shader` entry using only vanilla Quake 3 shader keywords plus top-level `qer_pbr_*`. Normal maps SHALL be declared with `qer_pbr_normal`. Emissive SHALL be an additive stage, with `qer_pbr_emissiveStrength` where the glow needs more than the stage colour. The shaders SHALL contain no `q3map_sun` and no `q3map_surfacelight`. No image SHALL be named `<base>_n`, `<base>_nh` or `<base>_s`.

#### Scenario: Every feature present
- **WHEN** the shader file is read
- **THEN** it exercises:
  - a base colour with normal, metallic-roughness and occlusion;
  - a rough dielectric;
  - a metal;
  - an emissive with `rgbGen const`;
  - an alpha-tested surface with `cull none`;
  - a blended surface;
  - a double-sided opaque surface;
  - sky, fog, caulk and clip;
  - a normal-mapped opaque model surface;
  - an alpha-tested model surface with `cull none` that casts through its alpha (`surfaceparm trans` with `surfaceparm alphashadow`);
  - an alpha-tested model surface with `cull none` that casts no shadow (`surfaceparm trans` without `surfaceparm alphashadow`).

#### Scenario: No rend2 keywords
- **WHEN** the editor loads the shader file
- **THEN** it prints no rend2-keyword warning

#### Scenario: Reserved suffix scan
- **WHEN** the texture directory is listed
- **THEN** no file ends in `_n`, `_nh` or `_s` before its extension
