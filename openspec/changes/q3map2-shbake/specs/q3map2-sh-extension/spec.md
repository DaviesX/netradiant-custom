# Spec Delta

## ADDED Requirements

### Requirement: Bake contents field
An extension SHALL record which light its SH holds: *full* (value 0), meaning direct plus indirect light; or *indirect only* (value 1), meaning everything except the direct light of point, spot and directional lights, with emissive surfaces' direct light kept. The reader SHALL return the field unchanged. A renderer uses it to decide whether to add its own direct light.

#### Scenario: Contents round-trip
- **WHEN** one extension is written with contents *full* and another with *indirect only*, and both are read back
- **THEN** the first reads as *full* and the second as *indirect only*, and their files differ only in the contents field

## MODIFIED Requirements

### Requirement: SH basis
The coefficients SHALL use the real SH basis with no Condon–Shortley phase, for a unit direction (x, y, z) in the caller's world frame pointing from the luxel towards the incoming light:

| Coefficient | Basis function |
| --- | --- |
| L0 | 0.282095 |
| L1m1, L10, L11 | 0.488603 y, 0.488603 z, 0.488603 x |
| L2m2, L2m1 | 1.092548 xy, 1.092548 yz |
| L20 | 0.315392 (3z² − 1) |
| L21, L22 | 1.092548 xz, 0.546274 (x² − y²) |

Each coefficient SHALL be the projection of the incident radiance onto its basis function: the integral of L_i(ω) Y(ω) dω over the hemisphere above the surface, not convolved with the cosine lobe. This is what sh-baker's `BakeSHLightMap` returns. For *full* contents the caller stores the sum of sh-baker's main and punctual-direct SH results, and for *indirect only* the main result alone. The caller may average and fill them with its storage weights, but SHALL NOT rescale or convolve them.

#### Scenario: Basis documented with the format
- **WHEN** a reader of the header comment of `q3map2_extension.h` looks for the meaning of coefficient L1m1
- **THEN** it finds the basis table above and the radiance-projection rule, without reference to sh-baker's source

### Requirement: Extension contents
An extension SHALL hold a bake-contents value, an ordered list of zero or more pages and one hull mesh, which may be empty. Each page SHALL have its own width and height, both at least 1. For each luxel it SHALL hold the nine SH coefficients as RGB, and one environment-visibility value. The coefficients are in the order L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22. Visibility is the fraction of the point's samples whose first ray reached the sky, in [0, 1]. The file has no "not baked" value: every luxel SHALL hold real data, so the caller SHALL fill luxels the bake skipped (with zeros or by dilation) before writing, instead of passing the bake's −1 marker through. The hull mesh SHALL hold vertex positions, triangle indices, and one index range per hull. Page order, page sizes, luxel placement and hull geometry SHALL be the caller's. The file SHALL store them as given, apart from the value encoding, and SHALL NOT reorder, resize, rescale or transform them.

#### Scenario: Caller order and sizes kept
- **WHEN** an extension is written with three pages of sizes 4×2, 1×1 and 3×5, in that order, and read back
- **THEN** the read extension has three pages with sizes 4×2, 1×1 and 3×5, in that order, and each luxel holds the values written at its index, as encoded in half precision

#### Scenario: Empty parts allowed
- **WHEN** an extension is written with no pages and a non-empty hull mesh, another with pages and an empty hull mesh, and a third with neither
- **THEN** all three write successfully and read back unchanged, and the third is exactly 28 bytes

### Requirement: Byte layout
The file SHALL be little-endian, with no padding beyond what is listed here, and SHALL end exactly after the last section:

1. A header of seven 32-bit fields: the magic bytes `S`,`H`,`S`,`F`; then, as `uint32`, the version (2), the bake contents (0 full, 1 indirect only), the page count, the hull count, the hull vertex count and the hull index count.
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
- **WHEN** an extension with contents *full* is written with one 1×1 page whose coefficient k (0-8) has RGB values (k, k + 0.5, −k) and whose visibility is 0.25 (so coefficient 0's blue is −0, encoded as `0x8000`, not `0x0000`), and a hull mesh of one hull (first index 0, count 3) with vertices (0,0,0), (1,0,0) and (0,1,0) and indices 0, 1, 2
- **THEN** the file's bytes equal a checked-in byte array built by hand from this layout, and its length is 28 + 64 + 8 + 36 + 12 = 148 bytes

### Requirement: Version rule
A writer SHALL always write version 2. A reader SHALL accept only version 2 and SHALL reject any other version, including version 1, which no released tool wrote. Any change to the layout or the meaning of the stored values SHALL use a new version number.

#### Scenario: Unknown version rejected
- **WHEN** a valid file's version field is changed to 1, and another's to 3, and each file is read
- **THEN** both reads fail, with a message naming the version

### Requirement: Writer refuses malformed input
The writer SHALL check its whole input before it creates any file. Input that only a caller bug can produce SHALL make it return false, with a message naming the field and, where there is one, the value and the page or hull index, and it SHALL create no file. This covers:
- a bake-contents value other than full or indirect only;
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

#### Scenario: Unknown contents refused
- **WHEN** an extension is written with a bake-contents value of 2
- **THEN** the write returns false with a message naming the contents field and the value 2, and no file exists at the path

### Requirement: Reader rejects malformed files
The reader SHALL return no result, with a message, for a file that cannot be opened, or whose magic is wrong, whose version is unknown, whose bake-contents value is neither 0 nor 1, which is shorter or longer than its header and page sizes imply, which has a page width or height of 0 or above 2^31 − 1, or whose values break the writer's rules: a non-finite `binary16` (infinity or NaN) or `float32` value, a visibility outside [0, 1], a negative L0 component, or hull ranges or indices the writer would refuse. It SHALL check the size the header implies against the actual file size, using 64-bit arithmetic, before it allocates storage for pages or hulls. It SHALL NOT abort on any file contents.

#### Scenario: Truncated file
- **WHEN** a valid file has its last byte removed and is read
- **THEN** the read returns no result, with a message that the file is shorter than expected

#### Scenario: Trailing bytes
- **WHEN** a valid file has one byte appended and is read
- **THEN** the read returns no result, with a message that the file is longer than expected

#### Scenario: Huge counts in a small file
- **WHEN** a 28-byte file has a valid magic, version and contents, and every count is 0xFFFFFFFF
- **THEN** the read returns no result, without allocating storage for those counts

#### Scenario: Non-finite value in the file
- **WHEN** a valid file has one coefficient's bytes changed to `binary16` infinity (`0x7C00`), and another has one hull position changed to a `float32` NaN, and each is read
- **THEN** both reads return no result, with a message giving the bad value and where it is

#### Scenario: Bad index in the file
- **WHEN** a valid file has one hull index changed to the vertex count, and is read
- **THEN** the read returns no result, with a message giving the bad index

#### Scenario: Bad contents in the file
- **WHEN** a valid file has its contents field changed to 7, and is read
- **THEN** the read returns no result, with a message naming the contents value
