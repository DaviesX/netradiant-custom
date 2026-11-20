# Spec Delta

## MODIFIED Requirements

### Requirement: Benchmark compile flags
The map SHALL compile with the bundled q3map2 using `-meta -keeplights`, then `-vis -saveprt`, then `-light -shbake`, with no leak and no `Unknown q3map_* directive` warning. sh-baker bakes the lightmap (the full bake) and the side file (the indirect bake) from the editor's lights. A9 judges the lightmap's fidelity to the preview.

#### Scenario: Compile
- **WHEN** the stock check compiles `bench.map`
- **THEN** q3map2 reports no leak and no unknown directive, the BSP's entity lump retains the light entities, and `maps/bench/bench.shsf` is written

### Requirement: Single pk3 packaging
The benchmark SHALL package as one `benchmark.pk3`. It contains the BSP and its lightmaps, the SH side file `maps/bench/bench.shsf`, the levelshot `levelshots/bench.jpg`, `scripts/*.shader` and `shaderlist.txt`, and every image under `textures/`, the PBR maps included. The levelshot is the loading-screen image; without it the engine prints `Couldn't find image file for shader levelshots/bench.tga`. It contains no other files (no `.map`, `.prt`, editor metadata, `*.import` files or `.hdr` dumps).

#### Scenario: Pack contents
- **WHEN** `benchmark.pk3` is listed
- **THEN** every entry is a BSP, a lightmap image, the side file, the levelshot, a shader script, the shader list or an image under `textures/`, and every image a stage references is present

### Requirement: Stock screenshots reviewed by the author
The author SHALL review the stock check's screenshots of every view on both renderers for content errors: every shader renders with its own image (no missing-texture or default image), the fence, the leaves and the grass are masked, the trees and grass stand upright with their cards visible from both sides, the glass is blended and the sky draws. Lighting fidelity SHALL NOT be judged here; A9 judges it against the editor preview. The acceptance date SHALL be recorded in `content/benchmark/README.md`.

#### Scenario: Acceptance
- **WHEN** the stock check's screenshots are shown to the author
- **THEN** the author accepts them, or the content is fixed and the check is re-run
