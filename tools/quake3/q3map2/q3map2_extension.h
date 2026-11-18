/* -------------------------------------------------------------------------------

   The q3map2 SH extension, stored as the SH side file: a level's baked SH
   lightmap pages and its solid hull mesh, written by q3map2 (-shbake) and read
   by renderer_sh.

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
   Byte layout, version 1. Little-endian, no padding beyond what is listed, and
   the file ends exactly after the hull indices:

     header, 24 bytes:
       char[4]  magic 'S','H','S','F'
       uint32   version (1)
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

   Every section starts on a 4-byte boundary. The bytes are the same on any
   host byte order.

   Version rule: the writer always writes version 1, and the reader accepts
   only version 1. Any change to the layout or the meaning of the values takes
   a new version number.

   SH basis: real SH with no Condon-Shortley phase, for a unit direction
   (x, y, z) in the caller's world frame, pointing from the luxel towards the
   incoming light:
     L0   0.282095                 L2m2 1.092548 x y
     L1m1 0.488603 y               L2m1 1.092548 y z
     L10  0.488603 z               L20  0.315392 (3 z^2 - 1)
     L11  0.488603 x               L21  1.092548 x z
                                   L22  0.546274 (x^2 - y^2)
   Each coefficient is the projection of the incident radiance onto its basis
   function, the integral of L_i(w) Y(w) dw over the hemisphere above the
   surface; it is not convolved with the cosine lobe. This is what sh-baker's
   BakeSHLightMap returns.

   Visibility is the fraction of the luxel's samples whose first ray reached
   the sky, in [0, 1].

   Values: coefficients and visibility are IEEE 754 binary16, rounded to
   nearest, ties to even (subnormals included), with the sign of zero kept. A
   finite value whose magnitude exceeds 65504 is clamped to +-65504, so very
   bright luxels clip. Positions are float32, stored as given. The reader
   decodes binary16 exactly.

   No "not baked" value: visibility is in [0, 1] and L0 is never negative, so
   the bake's marker (every coefficient -1, visibility -1) is refused. Fill
   luxels the bake skipped (with zeros or by dilation) before writing.

   Hulls: the un-shrunk closed hulls of the casting brushes, in the caller's
   world space and units. Each hull's triangles face outward, with front faces
   wound clockwise seen from outside (q3map2's winding). Each hull is one
   contiguous index range, and the ranges cover the index list in order. The
   file neither checks nor changes closure, convexity or winding.

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

struct SHQ3Map2Extension
{
	std::vector<SHQ3Map2ExtensionPage> pages;
	SHHullMesh hull_mesh;
};

// Writes `ext` to `path`. The whole input is checked first; input only a
// caller bug can produce returns false, with a message naming the field and,
// where there is one, the value and the page or hull index, and creates no
// file:
//   - a page width or height below 1;
//   - coefficient or visibility counts that differ from width * height needs;
//   - a non-finite coefficient, visibility value or position;
//   - a visibility outside [0, 1], or a negative L0 component;
//   - a hull index count that is not a multiple of 3, or zero for a hull;
//   - hull ranges that are not contiguous in order from index 0 to the end of
//     the index list;
//   - an index not below the vertex count;
//   - any count that does not fit in a uint32.
// The bytes are written to "<path>.tmp" and renamed over `path` once
// complete. If that fails, returns false with a message naming the path,
// removes the temporary file and leaves any file at `path` unchanged. Two
// writers to the same path at once are not supported.
bool SaveSHQ3Map2Extension( const SHQ3Map2Extension& ext, const char* path, std::string* error );

// Reads an extension. Returns std::nullopt, with a message in `*error`, for a
// file that cannot be opened, or with a wrong magic, an unknown version, a
// size other than its header and page sizes imply, a page width or height of
// 0 or above 2^31 - 1, or values the writer would refuse. Sizes are checked
// against the file before any page or hull storage is allocated.
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
