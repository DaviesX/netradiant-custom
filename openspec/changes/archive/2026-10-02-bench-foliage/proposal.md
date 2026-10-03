# Proposal

## Why

The town needs grass and trees: the author added them to task A10's Silent Hill look (2026-10-02), and nothing in the pipeline has been tried on vegetation yet. Vegetation brings
cases the benchmark doesn't cover:
- model surfaces instead of brushes;
- alpha-tested cards seen through other alpha-tested cards;
- cut-out shadows cast by a model;
- thousands of small lightmapped triangles.

The benchmark is the parity and regression asset for every later stage (plan §0.1). Foliage has to be in it before
A8, B5 and C2–C5 are judged against it. Otherwise each of them finds the vegetation problems separately, in the town.

## What Changes

- **Placeholder foliage assets.** A committed generator script writes the bark, leaf and grass textures and the tree
  and grass meshes (Wavefront OBJ) into the benchmark mod. The art is deliberately simple and reproducible. It is not
  final town art.
- **Trees** are `misc_model` meshes: a solid trunk and branches, plus a canopy of alpha-tested leaf cards that cast
  through their alpha.
- **Grass** is `misc_model` clumps of static billboards: crossed, alpha-tested quads. It is nonsolid and casts no
  shadow. It is not `deformVertexes autosprite`, which the editor, the shadow maps and the baker would all see as
  static. It is not scattered by `q3map_surfaceModel` either, which the editor can't preview.
- **New materials** in `scripts/bench.shader`: `bark`, `leaves`, `grass` and a `dirt` ground. All are lightmapped,
  so they are lit in the editor preview and in the BSP alike.
- **Map.** The east end of the street, past the fence, becomes a small dirt lawn with two trees and grass clumps.
  A few clumps also grow along the asphalt's edge by the kerbs, inside the fog. The existing brushes, lights, occlusion cases and views keep
  their meaning.
- **Light retune.** The author retuned the lights in the editor preview while this change was open: a dimmer,
  cool sun; the west point light halved; the north point light replaced by an upward ceiling-wash spot; and the
  sign spot made casting with a longer radius. The `benchmark-map` light requirement no longer pins the `pbr.game`
  values. It requires at least one point light and one non-casting spot, and the README's light table holds the
  values. The ceiling spot (`_shadows 0`) is now the non-casting case.
- **A fifth view** in `bench.cameras` frames the trees, the grass and their shadows.
- **Docs.** The benchmark README covers the foliage, its materials and the generator. `CLAUDE.md` A10 and plan §1
  (Stage 1) say the town needs grass and trees. `CLAUDE.md` Track A lists this change before A7.
- **C5 widened.** Plan §3.5 builds `renderer_sh`'s shadow casters only from collision-brush hulls, so nonsolid
  alpha-tested surfaces (the fence today, leaf cards here) can't cast in the engine. C5 (plan §3.5 and `CLAUDE.md`)
  also casts from alpha-tested draw surfaces under the editor's caster rule. This changes only the plan. No engine code
  changes here.
- **Exit:** the stock check passes on `opengl1` and `opengl2`, and the author accepts the screenshots again.
- **BREAKING:** none. No editor, q3map2 or engine code changes are planned. If the early checks find a preview gap,
  it is raised before any code changes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `benchmark-map`:
  - The material feature list gains vegetation: alpha-tested model surfaces that cast through their alpha, and
    alpha-tested surfaces that don't cast.
  - The scene gains trees and grass as models, built from a committed generator.
  - The documented views cover the foliage.
  - The author's screenshot review checks the foliage.
  - The lights are the author's retuned set, recorded in the README, rather than the `pbr.game` values. The
    non-casting case is any spot with `_shadows 0`.

## Impact

- `content/benchmark/`:
  - new `models/bench/` (generated OBJ meshes and their `.mtl` files);
  - new `textures/bench/` images (`bark_*`, `leaves`, `grass`, `dirt_*`);
  - `scripts/bench.shader`;
  - `maps/bench.map` (one asphalt brush split for the lawn, new `misc_model` entities);
  - `maps/bench.cameras`;
  - `levelshots/` unchanged;
  - `README.md`.
- New generator script `tools/benchmark/gen_foliage.py` (outside the mod, so the mod holds only content), which needs Python 3 with Pillow, already used by the
  stock check.
- The pk3 is unchanged in shape: q3map2 merges the model triangles into the BSP, so model files aren't packed. The
  stock check packs `textures/`, so the new images go in.
- `CLAUDE.md` (Track A entry, A10, C5) and `docs/pbr-plan/pbr-plan.tex` (§Stage 1, §3.5, world-building conventions), with the PDF rebuilt.
- Read-only dependencies: the bundled q3map2, the editor build under `install/`, the fork's ioquake3 build, and the
  stock check.
