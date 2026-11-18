# q3map2-sh-extension Specification

## Purpose
The q3map2 SH extension, stored as the SH side file, carries a level's baked SH lightmap pages and its solid hull mesh from q3map2 to `renderer_sh`. q3map2 defines the file and writes and reads it. sh-baker only supplies the per-point bake results that fill its pages.

## Requirements

### Requirement: Extension contents
An extension SHALL hold an ordered list of zero or more pages and one hull mesh, which may be empty. Each page SHALL have its own width and height, both at least 1. For each luxel it SHALL hold the nine SH coefficients as RGB, and one environment-visibility value. The coefficients are in the order L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22. Visibility is the fraction of the point's samples whose first ray reached the sky, in [0, 1]. The file has no "not baked" value: every luxel SHALL hold real data, so the caller SHALL fill luxels the bake skipped (with zeros or by dilation) before writing, instead of passing the bake's −1 marker through. The hull mesh SHALL hold vertex positions, triangle indices, and one index range per hull. Page order, page sizes, luxel placement and hull geometry SHALL be the caller's. The file SHALL store them as given, apart from the value encoding, and SHALL NOT reorder, resize, rescale or transform them.

#### Scenario: Caller order and sizes kept
- **WHEN** an extension is written with three pages of sizes 4×2, 1×1 and 3×5, in that order, and read back
- **THEN** the read extension has three pages with sizes 4×2, 1×1 and 3×5, in that order, and each luxel holds the values written at its index, as encoded in half precision

#### Scenario: Empty parts allowed
- **WHEN** an extension is written with no pages and a non-empty hull mesh, another with pages and an empty hull mesh, and a third with neither
- **THEN** all three write successfully and read back unchanged, and the third is exactly 24 bytes

### Requirement: SH basis
The coefficients SHALL use the real SH basis with no Condon–Shortley phase, for a unit direction (x, y, z) in the caller's world frame pointing from the luxel towards the incoming light:

| Coefficient | Basis function |
| --- | --- |
| L0 | 0.282095 |
| L1m1, L10, L11 | 0.488603 y, 0.488603 z, 0.488603 x |
| L2m2, L2m1 | 1.092548 xy, 1.092548 yz |
| L20 | 0.315392 (3z² − 1) |
| L21, L22 | 1.092548 xz, 0.546274 (x² − y²) |

Each coefficient SHALL be the projection of the incident radiance onto its basis function: the integral of L_i(ω) Y(ω) dω over the hemisphere above the surface, not convolved with the cosine lobe. This is what sh-baker's `BakeSHLightMap` returns, and the caller copies it in unchanged.

#### Scenario: Basis documented with the format
- **WHEN** a reader of the header comment of `q3map2_extension.h` looks for the meaning of coefficient L1m1
- **THEN** it finds the basis table above and the radiance-projection rule, without reference to sh-baker's source

### Requirement: Byte layout
The file SHALL be little-endian, with no padding beyond what is listed here, and SHALL end exactly after the last section:

1. A header of six 32-bit fields: the magic bytes `S`,`H`,`S`,`F`; then, as `uint32`, the version (1), the page count, the hull count, the hull vertex count and the hull index count.
2. For each page in order:
   - `uint32` width and `uint32` height;
   - nine coefficient planes, in the coefficient order above, each holding width × height luxels of three `binary16` values (R, G, B);
   - one visibility plane of width × height `binary16` values.

   Within a plane, luxels run x fastest, then y, starting at row 0, in the caller's luxel order.
3. The hull ranges: for each hull, a `uint32` first index and a `uint32` index count.
4. The hull vertices: three `float32` values (x, y, z) per vertex.
5. The hull indices: one `uint32` per index.

A page therefore takes 8 + 56 × width × height bytes, and every section starts on a 4-byte boundary. The writer SHALL produce these bytes on any host byte order.

#### Scenario: Golden bytes
- **WHEN** an extension is written with one 1×1 page whose coefficient k (0-8) has RGB values (k, k + 0.5, −k) and whose visibility is 0.25 (so coefficient 0's blue is −0, encoded as `0x8000`, not `0x0000`), and a hull mesh of one hull (first index 0, count 3) with vertices (0,0,0), (1,0,0) and (0,1,0) and indices 0, 1, 2
- **THEN** the file's bytes equal a checked-in byte array built by hand from this layout, and its length is 24 + 64 + 8 + 36 + 12 = 144 bytes

### Requirement: Version rule
A writer SHALL always write version 1. A reader SHALL accept only version 1 and SHALL reject any other version. Any change to the layout or the meaning of the stored values SHALL use a new version number.

#### Scenario: Unknown version rejected
- **WHEN** a valid file's version field is changed to 2 and the file is read
- **THEN** the read fails, with a message naming the version

### Requirement: Value encoding
The writer SHALL encode each SH coefficient and visibility value as IEEE 754 `binary16`, rounding to the nearest value and to even on ties, including into and within the subnormal range. A finite value whose magnitude exceeds 65504 SHALL be clamped to ±65504. The sign of zero SHALL be kept. The writer SHALL store positions as `float32` exactly as given. The reader SHALL decode `binary16` values exactly into `float`.

#### Scenario: Representable values round-trip exactly
- **WHEN** values that are exact in `binary16` (0, −0, −0.5, 1, 2048, 65504, the smallest subnormal 2^−24, the largest subnormal 1023 × 2^−24) are written and read back
- **THEN** each read value equals the written value bit for bit

#### Scenario: Every half value round-trips
- **WHEN** each of the 63488 finite `binary16` bit patterns is decoded to `float` and encoded again
- **THEN** each encodes to its own bit pattern

#### Scenario: Rounding and clamping
- **WHEN** the values 1 + 2^−11 (a tie), 1 + 3 × 2^−11 (a tie), 1 + 2^−11 + 2^−20, 2^−25 (a tie), 3 × 2^−25 (a tie), 1e6 and −1e6 are written and read back
- **THEN** they read as 1, 1 + 2^−9, 1 + 2^−10, 0, 2^−23, 65504 and −65504

### Requirement: Writer refuses malformed input
The writer SHALL check its whole input before it creates any file. Input that only a caller bug can produce SHALL make it return false, with a message naming the field and, where there is one, the value and the page or hull index, and it SHALL create no file. This covers:
- a page width or height below 1;
- coefficient or visibility counts that differ from what width × height needs;
- a non-finite coefficient, visibility value or position;
- a visibility value outside [0, 1], or a negative L0 component (the bake's not-baked marker fails both; real bakes produce neither, since L0 is a sum of non-negative radiance);
- a hull index count that is not a multiple of 3, or is zero for a hull;
- hull ranges that are not contiguous in order, from index 0 to the end of the index list;
- an index not below the vertex count;
- any count that does not fit in a `uint32`.

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

### Requirement: Atomic write
The writer SHALL write the file under a temporary name, `<path>.tmp`, and rename it to the target path only once all bytes are written, replacing any existing file there. If it cannot open, write or rename the file, it SHALL return false with a message naming the path, remove its temporary file, and leave any existing file at the target path unchanged. On success it SHALL return true. Two writers to the same path at once are not supported.

#### Scenario: Missing directory
- **WHEN** the target path's directory does not exist
- **THEN** the write returns false with a message naming the path, and creates no file

#### Scenario: Rename failure cleans up
- **WHEN** the target path is an existing directory, so the temporary file is written but the rename fails
- **THEN** the write returns false, the directory and its contents are unchanged, and no temporary file remains

#### Scenario: Existing file replaced only on success
- **WHEN** a valid file exists at the target path and a second, different extension is written to it successfully
- **THEN** the file at the path holds the second extension's bytes, and no temporary file remains

### Requirement: Reader rejects malformed files
The reader SHALL return no result, with a message, for a file that cannot be opened, or whose magic is wrong, whose version is unknown, which is shorter or longer than its header and page sizes imply, which has a page width or height of 0 or above 2^31 − 1, or whose values break the writer's rules: a non-finite `binary16` (infinity or NaN) or `float32` value, a visibility outside [0, 1], a negative L0 component, or hull ranges or indices the writer would refuse. It SHALL check the size the header implies against the actual file size, using 64-bit arithmetic, before it allocates storage for pages or hulls. It SHALL NOT abort on any file contents.

#### Scenario: Truncated file
- **WHEN** a valid file has its last byte removed and is read
- **THEN** the read returns no result, with a message that the file is shorter than expected

#### Scenario: Trailing bytes
- **WHEN** a valid file has one byte appended and is read
- **THEN** the read returns no result, with a message that the file is longer than expected

#### Scenario: Huge counts in a small file
- **WHEN** a 24-byte file has a valid magic and version, and every count is 0xFFFFFFFF
- **THEN** the read returns no result, without allocating storage for those counts

#### Scenario: Non-finite value in the file
- **WHEN** a valid file has one coefficient's bytes changed to `binary16` infinity (`0x7C00`), and another has one hull position changed to a `float32` NaN, and each is read
- **THEN** both reads return no result, with a message giving the bad value and where it is

#### Scenario: Bad index in the file
- **WHEN** a valid file has one hull index changed to the vertex count, and is read
- **THEN** the read returns no result, with a message giving the bad index

### Requirement: Hull mesh meaning
The hull mesh SHALL hold the un-shrunk closed hulls of the casting brushes, in the caller's world space and units, unchanged. Every hull's triangles SHALL face outward, with each front face wound clockwise when seen from outside the hull, the same as q3map2's draw surfaces. The file SHALL NOT check that hulls are closed or convex, and SHALL NOT change their winding. The caller is responsible for both.

#### Scenario: Winding kept
- **WHEN** a hull mesh with indices 0, 1, 2 is written and read back
- **THEN** the read indices are 0, 1, 2, in that order

### Requirement: L0 inspection dump
The module SHALL write one page's L0 coefficients as a Radiance RGBE `.hdr` image of the page's width and height. Row 0 of the image SHALL be page row 0, and the values SHALL be the L0 RGB values unscaled, with negative components written as 0 and components above 65504 written as 65504 (the range the extension stores, which also keeps the RGBE exponent in its byte). It SHALL refuse a page with a non-finite L0 component, returning false with a message and writing nothing. It SHALL refuse a target whose file name begins with `lm_` (in any letter case), returning false with a message and writing nothing. It SHALL check the page's sizes and counts as the writer does. It SHALL return false with a message on an I/O failure.

#### Scenario: Dump matches L0
- **WHEN** a 4×2 page whose L0 at luxel (x, y) is (x, y, 1) is dumped and the image is read back with an RGBE decoder
- **THEN** the image is 4×2, and each component of each pixel differs from its luxel's L0 component by at most 1/128 of that pixel's largest L0 component (RGBE shares one exponent per pixel)

#### Scenario: Non-finite and huge L0
- **WHEN** a page whose L0 green is infinity is dumped, and then the same page with L0 (2^127, 1, 0)
- **THEN** the first dump returns false naming the luxel and value and creates no file, and the second succeeds with red read back within 1/128 of 65504

#### Scenario: rend2 name refused
- **WHEN** a page is dumped to `maps/town/LM_0000.hdr`
- **THEN** the dump returns false and no file is created

### Requirement: Self-contained module
The module's header SHALL include only standard library headers and `math/vector.h` from `libs`, and the module SHALL compile under q3map2's flags (`-std=c++20 -fno-exceptions -fno-rtti`). It SHALL NOT log, exit or throw: every outcome reaches the caller as a return value and a message. It SHALL link into a program with no other q3map2 object files. It SHALL be linked into q3map2, and q3map2's output SHALL not change until a caller uses it.

#### Scenario: Test program
- **WHEN** `make test-q3map2-extension` is run
- **THEN** it builds `q3map2_extension_test` from the module and its test file alone, runs it, and the run exits 0 with every check passing

#### Scenario: q3map2 output unchanged
- **WHEN** the benchmark map is compiled by the stock check with q3map2 built before and after this change
- **THEN** the stock check passes, and the compiled BSPs are compared with any difference reported
