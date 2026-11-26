## MODIFIED Requirements

### Requirement: Extension contents
An extension SHALL hold an ordered list of zero or more pages, one hull mesh, which may be empty, and one SH grid, which may have no probes. Each page SHALL have its own width and height, both at least 1. For each luxel it SHALL hold the nine SH coefficients as RGB, and one environment-visibility value. The coefficients are in the order L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22. Visibility is the cosine-weighted fraction of the point's samples whose first ray reached the sky, as sh-baker defines it for surface points (Σ eᵢ cos θᵢ / Σ cos θᵢ), in [0, 1]. The file has no "not baked" value: every luxel SHALL hold real data, so the caller SHALL fill luxels the bake skipped (with zeros or by dilation) before writing, instead of passing the bake's −1 marker through. The hull mesh SHALL hold vertex positions, triangle indices, and one index range per hull. The SH grid SHALL hold an origin, a spacing and three dimensions. For each probe it SHALL hold the nine SH coefficients as RGB, in the same order, one sky-visibility value, and a valid flag of 0 or 1. A probe's sky visibility is the uniform fraction of the point's samples whose first ray reached the sky, over the whole sphere, as sh-baker defines it for full-sphere points, in [0, 1]. A grid with no probes SHALL have all three dimensions 0. Page order, page sizes, luxel placement, hull geometry and the grid SHALL be the caller's. The file SHALL store them as given, apart from the value encoding, and SHALL NOT reorder, resize, rescale or transform them.

#### Scenario: Caller order and sizes kept
- **WHEN** an extension is written with three pages of sizes 4×2, 1×1 and 3×5, in that order, and read back
- **THEN** the read extension has three pages with sizes 4×2, 1×1 and 3×5, in that order, and each luxel holds the values written at its index, as encoded in half precision

#### Scenario: Empty parts allowed
- **WHEN** an extension is written with no pages and a non-empty hull mesh, another with pages and an empty hull mesh, a third with no probes and pages, and a fourth with no pages, no hulls and no probes
- **THEN** all four write successfully and read back unchanged, and the fourth is exactly 60 bytes

### Requirement: Byte layout
The file SHALL be little-endian, with no padding beyond what is listed here, and SHALL end exactly after the last section:

1. A header of six 32-bit fields: the magic bytes `S`,`H`,`S`,`F`; then, as `uint32`, the version (3), the page count, the hull count, the hull vertex count and the hull index count.
2. For each page in order:
   - `uint32` width and `uint32` height;
   - nine coefficient planes, in the coefficient order above, each holding width × height luxels of three `binary16` values (R, G, B);
   - one visibility plane of width × height `binary16` values.

   Within a plane, luxels run x fastest, then y, starting at row 0, in the caller's luxel order.
3. The hull ranges: for each hull, a `uint32` first index and a `uint32` index count.
4. The hull vertices: three `float32` values (x, y, z) per vertex.
5. The hull indices: one `uint32` per index.
6. The grid header: the origin as three `float32` values (x, y, z), the spacing as three `float32` values, and the dimensions as three `uint32` values (x, y, z).
7. The probe coefficients: for each probe in order, the nine coefficients in the coefficient order above, each three `float32` values (R, G, B).
8. The probe sky visibilities: one `float32` per probe, in probe order.
9. The valid flags: one `uint8` per probe, in probe order.

A page therefore takes 8 + 56 × width × height bytes, and the grid 36 + 113 × probes bytes. Every section up to the valid flags starts on a 4-byte boundary; the flags come last so the coefficients and visibilities stay aligned, and the file length need not be a multiple of 4. The writer SHALL produce these bytes on any host byte order.

#### Scenario: Golden bytes
- **WHEN** an extension is written with:
  - one 1×1 page whose coefficient k (0-8) has RGB values (k, k + 0.5, −k) and whose visibility is 0.25 (so coefficient 0's blue is −0, encoded as `0x8000`, not `0x0000`);
  - a hull mesh of one hull (first index 0, count 3) with vertices (0,0,0), (1,0,0) and (0,1,0) and indices 0, 1, 2;
  - a 2×1×1 grid with origin (−64, 0, 8) and spacing (64, 64, 128), whose probe 0 is valid with coefficient k having RGB values (k, k + 0.25, k + 0.5) and sky visibility 0.375, and whose probe 1 is invalid with every coefficient and its sky visibility 0
- **THEN** the file's bytes equal a checked-in byte array built by hand from this layout, and its length is 24 + 64 + 8 + 36 + 12 + 36 + 216 + 8 + 2 = 406 bytes

### Requirement: Version rule
A writer SHALL always write version 3. A reader SHALL accept only version 3 and SHALL reject any other version, including version 1, which has no SH grid, and version 2, which has no probe sky visibility. Any change to the layout or the meaning of the stored values SHALL use a new version number. One exception is on record: the page visibility plane became cosine-weighted within version 2 (change `sh-baker-sky-radiance`), because no reader of version 2 existed. Version 3 is the first version a renderer reads, so no such exception SHALL be made for it.

#### Scenario: Unknown version rejected
- **WHEN** a valid file's version field is changed to 1, another's to 2 and a third's to 4, and each file is read
- **THEN** all three reads fail, with a message naming the version

### Requirement: Value encoding
The writer SHALL encode each page's SH coefficient and visibility value as IEEE 754 `binary16`, rounding to the nearest value and to even on ties, including into and within the subnormal range. A finite value whose magnitude exceeds 65504 SHALL be clamped to ±65504. The sign of zero SHALL be kept. The writer SHALL store hull positions and the grid's origin, spacing, probe coefficients and probe sky visibilities as `float32` exactly as given. The reader SHALL decode `binary16` values exactly into `float`.

#### Scenario: Representable values round-trip exactly
- **WHEN** values that are exact in `binary16` (0, −0, −0.5, 1, 2048, 65504, the smallest subnormal 2^−24, the largest subnormal 1023 × 2^−24) are written and read back
- **THEN** each read value equals the written value bit for bit

#### Scenario: Every half value round-trips
- **WHEN** each of the 63488 finite `binary16` bit patterns is decoded to `float` and encoded again
- **THEN** each encodes to its own bit pattern

#### Scenario: Rounding and clamping
- **WHEN** the values 1 + 2^−11 (a tie), 1 + 3 × 2^−11 (a tie), 1 + 2^−11 + 2^−20, 2^−25 (a tie), 3 × 2^−25 (a tie), 1e6 and −1e6 are written and read back
- **THEN** they read as 1, 1 + 2^−9, 1 + 2^−10, 0, 2^−23, 65504 and −65504

#### Scenario: Probe coefficients exact
- **WHEN** a probe coefficient of 1e6, one of 1 + 2^−20, and a probe sky visibility of 1 − 2^−20 are written and read back
- **THEN** all three read back bit for bit, unclamped and unrounded

### Requirement: Writer refuses malformed input
The writer SHALL check its whole input before it creates any file. Input that only a caller bug can produce SHALL make it return false, with a message naming the field and, where there is one, the value and the page, hull or probe index, and it SHALL create no file. This covers:
- a page width or height below 1;
- coefficient or visibility counts that differ from what width × height needs;
- a non-finite coefficient, visibility value, position, grid origin, grid spacing or probe sky visibility;
- a visibility value or probe sky visibility outside [0, 1], or a negative L0 component in a page luxel or a valid probe (the bake's not-baked marker fails these; real bakes produce neither, since L0 is a sum of non-negative radiance);
- a hull index count that is not a multiple of 3, or is zero for a hull;
- hull ranges that are not contiguous in order, from index 0 to the end of the index list;
- an index not below the vertex count;
- grid dimensions of which some but not all are 0;
- a grid spacing component not above 0, when the grid has probes;
- probe coefficient, sky-visibility or valid-flag counts that differ from what the dimensions need;
- a valid flag other than 0 or 1, or an invalid probe with a coefficient or sky visibility other than 0;
- any count that does not fit in a `uint32`, the probe count included.

#### Scenario: Wrong luxel count
- **WHEN** page 1, of width 4 and height 2, is given 7 luxels of coefficients
- **THEN** the write returns false with a message naming page 1 and the counts 7 and 8, and no file exists at the path

#### Scenario: Not-baked marker refused
- **WHEN** a page is written with a luxel still holding the bake's not-baked marker (every coefficient −1, visibility −1)
- **THEN** the write returns false with a message naming the page, the luxel index and the visibility value

#### Scenario: Index out of range
- **WHEN** a hull mesh with 3 vertices has index 3
- **THEN** the write returns false with a message naming the index and the vertex count

#### Scenario: Gap between hulls
- **WHEN** a hull mesh has hull 0 at (first 0, count 3) and hull 1 at (first 6, count 3) over 9 indices
- **THEN** the write returns false with a message naming hull 1 and the expected first index 3

#### Scenario: Wrong probe count
- **WHEN** a grid of dimensions 2, 2, 2 is given 7 probes, another 8 probes of coefficients but 7 sky visibilities
- **THEN** both writes return false with a message naming the grid field and the counts 7 and 8, and no file exists at the path

#### Scenario: Invalid probe with data
- **WHEN** a grid's probe 3 has valid flag 0 and an L0 red of 0.5, another grid's probe 3 has valid flag 0 and a sky visibility of 0.5, and a third grid's probe 3 has valid flag 2
- **THEN** all three writes return false with a message naming probe 3 and the bad value, and no file exists at the path

#### Scenario: Bad probe sky visibility
- **WHEN** a grid's valid probe 2 has sky visibility 1.5, another's −0.25, and a third's NaN
- **THEN** all three writes return false with a message naming probe 2 and the value, and no file exists at the path

### Requirement: Reader rejects malformed files
The reader SHALL return no result, with a message, for any of these files: one that cannot be opened; one whose magic is wrong or whose version is unknown; one shorter or longer than its header, page, hull and grid sizes imply; one with a page width or height of 0 or above 2^31 − 1; and one whose values break the writer's rules. Those values are: a non-finite `binary16` (infinity or NaN) or `float32` value, a page visibility or probe sky visibility outside [0, 1], a negative L0 component in a page luxel or a valid probe, hull ranges or indices the writer would refuse, grid dimensions or spacing the writer would refuse, a valid flag other than 0 or 1, and an invalid probe with a coefficient or sky visibility other than 0. Before it allocates storage for pages, hulls or probes, the reader SHALL check the size that the header and the grid dimensions imply against the actual file size, with arithmetic that cannot overflow. It SHALL NOT abort on any file contents.

#### Scenario: Truncated file
- **WHEN** a valid file has its last byte removed and is read
- **THEN** the read returns no result, with a message that the file is shorter than expected

#### Scenario: Trailing bytes
- **WHEN** a valid file has one byte appended and is read
- **THEN** the read returns no result, with a message that the file is longer than expected

#### Scenario: Huge counts in a small file
- **WHEN** a 24-byte file has a valid magic and version, and every count is 0xFFFFFFFF
- **THEN** the read returns no result, without allocating storage for those counts

#### Scenario: Huge grid in a small file
- **WHEN** a 60-byte file has a valid header with no pages or hulls, and grid dimensions 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
- **THEN** the read returns no result, without overflowing or allocating storage for the probes

#### Scenario: Non-finite value in the file
- **WHEN** four valid files are each changed in one value and read: one coefficient's bytes set to `binary16` infinity (`0x7C00`), one hull position set to a `float32` NaN, one probe coefficient set to a `float32` NaN, and one probe sky visibility set to a `float32` NaN
- **THEN** all four reads return no result, with a message giving the bad value and where it is

#### Scenario: Bad index in the file
- **WHEN** a valid file has one hull index changed to the vertex count, and is read
- **THEN** the read returns no result, with a message giving the bad index

#### Scenario: Bad valid flag in the file
- **WHEN** a valid file with a grid has one valid flag changed to 2, and is read
- **THEN** the read returns no result, with a message naming the probe and the flag value

#### Scenario: Bad probe sky visibility in the file
- **WHEN** a valid file has a valid probe's sky visibility changed to 1.5, and another has an invalid probe's sky visibility changed to 0.5, and each is read
- **THEN** both reads return no result, with a message naming the probe and the value

### Requirement: SH grid meaning
The SH grid SHALL be a regular lattice of probes for lighting models. Probe (x, y, z) SHALL sit at origin + (x · spacing_x, y · spacing_y, z · spacing_z), in the caller's world space and units. Probes SHALL run x fastest, then y, then z, so probe i is the caller's light-grid point i. A probe's coefficients SHALL be the projection of incident radiance from the whole sphere, not one hemisphere. Its sky visibility SHALL be the uniform fraction of its first rays that reached the sky over the whole sphere, unlike the page luxels' cosine-weighted value, since a probe has no normal. A renderer lights with it as the sky's irradiance at the shaded normal times the probe's sky visibility (blended like the coefficients). A probe whose valid flag is 0 has no data: its coefficients and sky visibility SHALL be 0, and a renderer SHALL leave it out when it blends neighbouring probes. q3map2 writes its BSP light grid here: origin is `gridMins`, spacing is the BSP's `gridSize`, the dimensions are `gridBounds`, and a probe is invalid when its grid point is still inside solid after q3map2's nudge. The file SHALL store the grid as given and SHALL NOT check that it matches any BSP.

#### Scenario: Probe order kept
- **WHEN** a 2×3×2 grid whose probe i has L0 red i and sky visibility i / 16 is written and read back
- **THEN** the read grid has dimensions 2, 3, 2, and probe i, at x = i mod 2, y = (i / 2) mod 3 and z = i / 6, has L0 red i and sky visibility i / 16
