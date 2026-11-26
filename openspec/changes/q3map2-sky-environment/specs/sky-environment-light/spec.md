## MODIFIED Requirements

### Requirement: Sky light model documented in the gamepack README
The gamepack README SHALL describe:
- the sky light: which shader counts, the radiance formula, and the farbox face mapping, including the texel orientation within each face;
- the cloud-layer hemisphere (the sky stage only), the SH basis, order and constants, and the convolution;
- that bakes and other radiance consumers use L(ω), not the SH, and that `surfaceparm sky` without `skyparms` gives no sky light;
- how q3map2's `-shbake` uses it:
  - which sky it chooses from the BSP;
  - that both bakes get L(ω) as sampled texels, the full bake keeping direct and bounced sky light and the side file only bounced sky light, with `renderer_sh` adding the direct sky;
  - that sky shaders are never emitters in the bake;
  - the summary line and its cross-check;
  - that a change to the sky needs a rebake for its bounce;
- the "Lighting sky" preference and the overlighting of interiors in the editor.

The description SHALL be complete enough to reimplement the derivation without reading the editor's code.

#### Scenario: README section
- **WHEN** the gamepack README is read
- **THEN** it has a sky light section with each item above, and the town's strength is worked through as an example

#### Scenario: No stale caveat
- **WHEN** the gamepack README's sky light section is read after this change
- **THEN** it no longer says that q3map2 ignores the sky light or treats a sky's additive stage as an emitter
