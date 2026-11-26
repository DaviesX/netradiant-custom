# Spec Delta

## MODIFIED Requirements

### Requirement: Extension contents
An extension SHALL hold an ordered list of zero or more pages, one hull mesh, which may be empty, and one SH grid, which may have no probes. Each page SHALL have its own width and height, both at least 1. For each luxel it SHALL hold the nine SH coefficients as RGB, and one environment-visibility value. The coefficients are in the order L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22. Visibility is the cosine-weighted fraction of the point's samples whose first ray reached the sky, as sh-baker defines it for surface points (Σ eᵢ cos θᵢ / Σ cos θᵢ), in [0, 1]. The file has no "not baked" value: every luxel SHALL hold real data, so the caller SHALL fill luxels the bake skipped (with zeros or by dilation) before writing, instead of passing the bake's −1 marker through. The hull mesh SHALL hold vertex positions, triangle indices, and one index range per hull. The SH grid SHALL hold an origin, a spacing and three dimensions, and for each probe the nine SH coefficients as RGB, in the same order, and a valid flag of 0 or 1. A grid with no probes SHALL have all three dimensions 0. Page order, page sizes, luxel placement, hull geometry and the grid SHALL be the caller's. The file SHALL store them as given, apart from the value encoding, and SHALL NOT reorder, resize, rescale or transform them.

#### Scenario: Caller order and sizes kept
- **WHEN** an extension is written with three pages of sizes 4×2, 1×1 and 3×5, in that order, and read back
- **THEN** the read extension has three pages with sizes 4×2, 1×1 and 3×5, in that order, and each luxel holds the values written at its index, as encoded in half precision

#### Scenario: Empty parts allowed
- **WHEN** an extension is written with no pages and a non-empty hull mesh, another with pages and an empty hull mesh, a third with no probes and pages, and a fourth with no pages, no hulls and no probes
- **THEN** all four write successfully and read back unchanged, and the fourth is exactly 60 bytes

### Requirement: Version rule
A writer SHALL always write version 2. A reader SHALL accept only version 2 and SHALL reject any other version, including version 1, which has no SH grid. Any change to the layout or the meaning of the stored values SHALL use a new version number, with one recorded exception: the visibility plane became cosine-weighted within version 2 (change `sh-baker-sky-radiance`), because no reader of version 2 existed.

#### Scenario: Unknown version rejected
- **WHEN** a valid file's version field is changed to 1, and another's to 3, and each file is read
- **THEN** both reads fail, with a message naming the version
