/* -------------------------------------------------------------------------------

   The q3map2 SH extension, stored as the SH side file: a level's baked SH
   lightmap pages, its solid hull mesh and its SH grid, written by q3map2
   (-shbake) and read by renderer_sh.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "math/vector.h"

/*
   Byte layout, version 3. Little-endian, no padding beyond what is listed, and
   the file ends exactly after the last valid flag:

     header, 24 bytes:
       char[4]  magic 'S','H','S','F'
       uint32   version (3)
       uint32   page count
       uint32   hull count
       uint32   hull vertex count
       uint32   hull index count
     for each page, in order (8 + 56 * width * height bytes):
       uint32   width   (>= 1)
       uint32   height  (>= 1)
       9 coefficient planes, in the order L0, L1m1, L10, L11, L2m2, L2m1, L20,
         L21, L22; each holds width * height luxels of three binary16 values
         (R, G, B)
       1 visibility plane of width * height binary16 values
       Within a plane, luxels run x fastest, then y, starting at row 0.
     hull ranges: per hull, uint32 first index, uint32 index count
     hull vertices: per vertex, float32 x, y, z
     hull indices: one uint32 per index
     SH grid (36 + 113 * N bytes, N = nx * ny * nz probes):
       float32[3]  origin (x, y, z): the position of probe 0
       float32[3]  spacing (x, y, z)
       uint32[3]   dimensions nx, ny, nz (all 0 or all >= 1)
       coefficients: per probe in order, the nine coefficients in the page
         order above, each three float32 values (R, G, B)
       sky visibilities: one float32 per probe in order, in [0, 1]
       valid flags: one uint8 per probe in order, 0 or 1

   Every section up to the valid flags starts on a 4-byte boundary; the flags
   come last so the probe coefficients and sky visibilities stay aligned, and
   the file length need not be a multiple of 4. The bytes are the same on any
   host byte order.

   Version rule: the writer always writes version 3, and the reader accepts
   only version 3; it rejects version 1 (B4's layout, which has no SH grid),
   version 2 (no probe sky visibility) and every other version. Any change to
   the layout or the meaning of the values takes a new version number. One
   exception was made in the past: page visibility became cosine-weighted
   within version 2 (sh-baker 297a88f), because no reader of version 2
   existed. Version 3 is the first version a renderer reads, so it takes no
   such exception.

   Contents: the file always holds the indirect bake, sh-baker's
   BakeSHLightMap with indirect_only set: no direct light at the baked point,
   neither punctual (point, spot, directional) nor emissive. Bounced light
   from every light, emissive surfaces included, is kept, and so is bounced
   sky light when the scene has an environment (sky reached after at least
   one surface reflection). The renderer adds all direct light itself, the
   direct sky light as the sky's irradiance at the normal times visibility.

   SH basis: real SH with no Condon-Shortley phase, for a unit direction
   (x, y, z) in the caller's world frame, pointing from the luxel or probe
   towards the incoming light:
     L0   0.282095                 L2m2 1.092548 x y
     L1m1 0.488603 y               L2m1 1.092548 y z
     L10  0.488603 z               L20  0.315392 (3 z^2 - 1)
     L11  0.488603 x               L21  1.092548 x z
                                   L22  0.546274 (x^2 - y^2)
   Each coefficient is the projection of the incident radiance onto its basis
   function, the integral of L_i(w) Y(w) dw; it is not convolved with the
   cosine lobe. For a page luxel the integral is over the hemisphere above the
   surface; for a grid probe it is over the whole sphere. This is what
   sh-baker's BakeSHLightMap returns, for surface points and for full_sphere
   points. The caller may average and fill page luxels with its storage
   weights, but does not rescale or convolve the coefficients.

   Visibility is the cosine-weighted fraction of the luxel's samples whose
   first ray reached the sky, sum(e_i cos t_i) / sum(cos t_i) with e_i 1 for
   an escaped ray and t_i its angle to the normal, in [0, 1]: the weight for
   the sky's irradiance at the normal. This is sh-baker's surface-point
   visibility (from 297a88f; before it, the uniform fraction of escapes).

   Values: page coefficients and visibility are IEEE 754 binary16, rounded to
   nearest, ties to even (subnormals included), with the sign of zero kept. A
   finite value whose magnitude exceeds 65504 is clamped to +-65504, so very
   bright luxels clip. Hull positions and the grid's origin, spacing, probe
   coefficients and probe sky visibilities are float32, stored as given,
   unclamped. The reader decodes
   binary16 exactly.

   No "not baked" value: visibility is in [0, 1] and L0 is never negative, so
   the bake's marker (every coefficient -1, visibility -1) is refused. Fill
   luxels the bake skipped (with zeros or by dilation) before writing.

   Hulls: the un-shrunk closed hulls of the casting brushes, in the caller's
   world space and units. Each hull's triangles face outward, with front faces
   wound clockwise seen from outside (q3map2's winding). Each hull is one
   contiguous index range, and the ranges cover the index list in order. The
   file neither checks nor changes closure, convexity or winding.

   SH grid: a regular lattice of probes for lighting models, in the caller's
   world space and units. Probe (x, y, z) sits at origin + (x * spacing_x,
   y * spacing_y, z * spacing_z), and probes run x fastest, then y, then z, so
   probe i is the caller's light-grid point i. q3map2 writes its BSP light
   grid: origin is gridMins, spacing is the BSP's gridSize, the dimensions are
   gridBounds, and a probe is invalid when its grid point is still in solid
   after q3map2's nudge. A probe's sky visibility is the uniform fraction of
   its first rays that reached the sky over the whole sphere (sh-baker's
   full_sphere visibility), unlike the page luxels' cosine-weighted value,
   since a probe has no normal; a renderer lights with the sky's irradiance at
   the shaded normal times the probe sky visibility, blended like the
   coefficients. A probe whose valid flag is 0 has no data: its coefficients
   and sky visibility are 0, and a renderer leaves it out when it blends
   neighbouring probes. A grid with no probes (-nogrid) has dimensions 0, 0, 0
   and is 36 bytes; its origin and spacing are stored as given (q3map2 passes
   0). The file stores the grid as given and does not check it against any
   BSP.

   Errors: nothing here logs, exits or throws. Each function returns its
   outcome and, on failure, a message in `*error`; the caller decides what to
   do with it (q3map2 passes a writer failure to Error()).
 */

// One hull: `index_count` indices starting at `first_index`.
struct SHHullRange
{
	uint32_t first_index = 0;
	uint32_t index_count = 0;
};

struct SHHullMesh
{
	std::vector<Vector3> positions;  // caller's world space and units
	std::vector<uint32_t> indices;   // outward, clockwise from outside
	std::vector<SHHullRange> hulls;  // contiguous, covering `indices`
};

// One page of width * height luxels, x fastest, then y.
struct SHQ3Map2ExtensionPage
{
	int width = 0, height = 0;
	// width * height * 27 values, luxel-major as in sh-baker's SHCoeffs:
	// coefficient k, channel c of luxel j is coeffs[( j * 9 + k ) * 3 + c].
	std::vector<float> coeffs;
	std::vector<float> visibility;  // width * height, each in [0, 1]
};

// A lattice of nx * ny * nz probes, x fastest, then y, then z.
struct SHGrid
{
	Vector3 origin = Vector3( 0, 0, 0 );   // position of probe 0
	Vector3 spacing = Vector3( 0, 0, 0 );  // each > 0 when there are probes
	uint32_t dimensions[3] = { 0, 0, 0 };  // all 0 (no probes) or all >= 1
	// probes * 27 values, probe-major as in sh-baker's SHCoeffs:
	// coefficient k, channel c of probe i is coeffs[( i * 9 + k ) * 3 + c].
	std::vector<float> coeffs;
	std::vector<float> visibility; // one per probe, in [0, 1]; 0 when invalid
	std::vector<uint8_t> valid;    // one per probe, 0 or 1
};

struct SHQ3Map2Extension
{
	std::vector<SHQ3Map2ExtensionPage> pages;
	SHHullMesh hull_mesh;
	SHGrid grid;
};

// Writes `ext` to `path`. The whole input is checked first; input only a
// caller bug can produce returns false, with a message naming the field and,
// where there is one, the value and the page, hull or probe index, and
// creates no file:
//   - a page width or height below 1;
//   - coefficient or visibility counts that differ from width * height needs;
//   - a non-finite coefficient, visibility value, position, grid origin,
//     grid spacing or probe sky visibility;
//   - a visibility or probe sky visibility outside [0, 1], or a negative L0
//     component in a page luxel or a valid probe;
//   - a hull index count that is not a multiple of 3, or zero for a hull;
//   - hull ranges that are not contiguous in order from index 0 to the end of
//     the index list;
//   - an index not below the vertex count;
//   - grid dimensions of which some but not all are 0;
//   - a grid spacing component not above 0, when the grid has probes;
//   - probe coefficient, sky-visibility or valid-flag counts that differ from
//     what the dimensions need;
//   - a valid flag other than 0 or 1, or an invalid probe with a coefficient
//     or sky visibility other than 0;
//   - any count that does not fit in a uint32, the probe count included.
// The bytes are written to "<path>.tmp" and renamed over `path` once
// complete. If that fails, returns false with a message naming the path,
// removes the temporary file and leaves any file at `path` unchanged. Two
// writers to the same path at once are not supported.
bool SaveSHQ3Map2Extension( const SHQ3Map2Extension& ext, const char* path, std::string* error );

// Reads an extension. Returns std::nullopt, with a message in `*error`, for a
// file that cannot be opened, or with a wrong magic, an unknown version, a
// size other than its header, page, hull and grid sizes imply, a page width or
// height of 0 or above 2^31 - 1, or values the writer would refuse (a
// non-finite binary16 or float32 included). Sizes are checked against the
// file, with arithmetic that cannot overflow, before any page, hull or probe
// storage is allocated.
std::optional<SHQ3Map2Extension> LoadSHQ3Map2Extension( const char* path, std::string* error );

// Writes `page`'s L0 RGB as a Radiance RGBE .hdr image for inspection: row 0
// is page row 0, values are unscaled, negative components are written as 0
// and components above 65504 as 65504. A non-finite L0 component is refused. Refuses a file name beginning with "lm_" in any letter case (rend2
// auto-loads maps/<name>/lm_*.hdr), writing nothing. The page's sizes and
// counts are checked as the writer checks them. Returns false with a message
// on any refusal or I/O failure.
bool SaveSHQ3Map2ExtensionPageL0Hdr( const SHQ3Map2ExtensionPage& page, const char* path, std::string* error );

namespace q3map2_extension_internal
{
// IEEE 754 binary16 conversion, exposed for tests. FloatToHalf clamps finite
// values to +-65504 and rounds to nearest even; HalfToFloat is exact.
uint16_t FloatToHalf( float value );
float HalfToFloat( uint16_t bits );
}
