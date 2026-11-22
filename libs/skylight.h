/* -------------------------------------------------------------------------------

   Pure helpers for the sky environment light (openspec change
   sky-environment-light): how a map's sky shader becomes a radiance L(w) and
   an L0-L2 spherical-harmonic radiance and irradiance, in the physical light
   units of the gamepack README.

   Header-only and free of editor dependencies, so that q3map2 (plan task B12)
   can include it; tested by libs/skylight_test.cpp through
   `make test-sky-light`.

   - SkySource: the raw sky shader data (farbox, sky stage image and colour,
     declared strength), filled by the editor's or q3map2's shader parser;
   - the image mean, with emissivelight.h's sRGB decode;
   - the farbox face-to-direction mapping of the stock renderer
     (ioq3 code/renderergl1/tr_sky.cpp) and the texel solid angles;
   - SkyRadiance: L(w) for a direction, which bakes use (never the SH);
   - the cube-face and analytic upper-hemisphere SH projections;
   - the cosine convolution and the irradiance E(n), clamped at 0;
   - the alphabetical choice among several sky shaders.

   Coordinates are Z-up map coordinates. The SH basis and order are
   sh-baker's SHCoeffs: Y00; Y1-1 (y), Y10 (z), Y11 (x); Y2-2 (xy), Y2-1 (yz),
   Y20 (3z^2 - 1), Y21 (xz), Y22 (x^2 - y^2).

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   -------------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emissivelight.h"
#include "math/vector.h"

namespace skylight
{

constexpr double c_pi = 3.14159265358979323846;

using Vec3 = DoubleVector3;
using emissivelight::ImageRGBA8;
using emissivelight::SrgbToLinear;

// --- Sky source -------------------------------------------------------------

/// the raw sky data of one shader that declares `skyparms`
struct SkySource
{
	std::string name;                   ///< the shader name
	std::string farbox;                 ///< the `skyparms` farbox path, "" for `-`
	std::string stageImage;             ///< the sky stage: the first map stage that is not `$lightmap`; "" when there is none
	Vec3 stageColour = Vec3( 1, 1, 1 ); ///< its `rgbGen const`, or white
	bool hasStrength = false;           ///< `qer_pbr_emissiveStrength` was declared
	double strength = 1;                ///< its value
};

/// \brief The strength the sky emits with: the declared `qer_pbr_emissiveStrength` when it is positive, else 0 (no keyword or a strength
/// of 0 or less emits nothing).
inline double EffectiveStrength( const SkySource& source ) {
	return source.hasStrength && source.strength > 0 && std::isfinite( source.strength ) ? source.strength : 0.0;
}

/// \brief s x C: the factor applied to the linear sky image T(w).
inline Vec3 RadianceScale( const SkySource& source ) {
	return source.stageColour * EffectiveStrength( source );
}

// --- Images -----------------------------------------------------------------

/// \brief The linear value of an RGBA8 texel, sRGB-decoded, alpha ignored.
inline Vec3 TexelLinear( const std::uint8_t* rgba ) {
	return Vec3( SrgbToLinear( rgba[0] / 255.0 ), SrgbToLinear( rgba[1] / 255.0 ), SrgbToLinear( rgba[2] / 255.0 ) );
}

/// \brief The unweighted mean linear texel M of \p image; zero for an empty image.
inline Vec3 ImageMean( const ImageRGBA8& image ) {
	if ( image.width <= 0 || image.height <= 0 || image.pixels == nullptr ) { return Vec3( 0, 0, 0 ); }
	Vec3 sum( 0, 0, 0 );
	const std::size_t count = std::size_t( image.width ) * std::size_t( image.height );
	for ( std::size_t i = 0; i < count; ++i ) sum += TexelLinear( image.pixels + i * 4 );
	return sum / double( count );
}

// --- Farbox faces -----------------------------------------------------------

/// the farbox faces in the stock renderer's load order (tr_shader.cpp ParseSkyParms, `suf[6]`)
enum Face
{
	c_faceRt = 0,
	c_faceBk = 1,
	c_faceLf = 2,
	c_faceFt = 3,
	c_faceUp = 4,
	c_faceDn = 5,
	c_faceCount = 6
};

/// \brief The image suffix of \p face: rt bk lf ft up dn.
inline const char* FaceSuffix( int face ) {
	static const char* const suffixes[c_faceCount] = { "rt", "bk", "lf", "ft", "up", "dn" };
	return suffixes[face];
}

namespace detail
{
/// tr_sky.cpp MakeSkyVec's st_to_vec: for each axis, which of b = ( s, t, 1 ) (1-based, negative = negated) goes to x, y and z. Axis 0 is
/// +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
constexpr int c_stToVec[6][3] = { { 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 } };
/// tr_sky.cpp kSkyTexOrder: axis i draws face c_skyTexOrder[i]
constexpr int c_skyTexOrder[6] = { 0, 2, 1, 3, 4, 5 };
} // namespace detail

/// \brief The axis (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z) on which the stock renderer draws \p face: rt +X, lf -X, bk +Y, ft -Y, up +Z,
/// dn -Z.
inline int FaceAxis( int face ) {
	for ( int axis = 0; axis < 6; ++axis ) {
		if ( detail::c_skyTexOrder[axis] == face ) { return axis; }
	}
	return -1;
}

/// \brief MakeSkyVec's direction for the face coordinates \p s, \p t in [-1, 1] on \p axis, not normalised (its major component is 1).
inline Vec3 AxisDirection( int axis, double s, double t ) {
	const double b[3] = { s, t, 1.0 };
	Vec3 out;
	for ( int j = 0; j < 3; ++j ) {
		const int k = detail::c_stToVec[axis][j];
		out[j] = k < 0 ? -b[-k - 1] : b[k - 1];
	}
	return out;
}

/// \brief The face coordinates s, t in [-1, 1] of the texel centre ( \p i + 0.5, \p j + 0.5 ) of a \p width x \p height face image,
/// column \p i from the left and row \p j from the top: MakeSkyVec's texture coordinates are ( ( s + 1 ) / 2, 1 - ( t + 1 ) / 2 ), with
/// t = 0 at the top row.
inline void TexelFaceCoords( int width, int height, double i, double j, double& s, double& t ) {
	s = 2.0 * ( i + 0.5 ) / width - 1.0;
	t = 1.0 - 2.0 * ( j + 0.5 ) / height;
}

/// \brief The unit direction of texel ( \p i, \p j ) of \p face, where the stock renderer draws it.
inline Vec3 TexelDirection( int face, int width, int height, int i, int j ) {
	double s, t;
	TexelFaceCoords( width, height, i, j, s, t );
	return vector3_normalised( AxisDirection( FaceAxis( face ), s, t ) );
}

/// \brief The exact solid angle subtended by the texel centred at face coordinates \p s, \p t on a \p width x \p height face: with
/// F( u, v ) = atan2( u v, sqrt( 1 + u^2 + v^2 ) ) the solid angle of [0, u] x [0, v] on the plane at distance 1, the texel's is
/// F( u1, v1 ) - F( u0, v1 ) - F( u1, v0 ) + F( u0, v0 ) over its corners. The texels of the six faces sum to 4 pi at any resolution.
inline double TexelSolidAngle( int width, int height, double s, double t ) {
	const double hu = 1.0 / width, hv = 1.0 / height; // half the texel size in face coordinates
	const auto F = []( double u, double v ) { return std::atan2( u * v, std::sqrt( 1.0 + u * u + v * v ) ); };
	const double u0 = s - hu, u1 = s + hu, v0 = t - hv, v1 = t + hv;
	return F( u1, v1 ) - F( u0, v1 ) - F( u1, v0 ) + F( u0, v0 );
}

// --- Spherical harmonics ----------------------------------------------------

/// nine RGB coefficients in sh-baker's SHCoeffs order
using SHCoeffs = std::array<Vec3, 9>;

/// \brief The nine real L0-L2 basis functions at the unit direction \p n, in sh-baker's order.
inline std::array<double, 9> SHBasis( const Vec3& n ) {
	const double x = n[0], y = n[1], z = n[2];
	return { 0.282095,
		     0.488603 * y,
		     0.488603 * z,
		     0.488603 * x,
		     1.092548 * x * y,
		     1.092548 * y * z,
		     0.315392 * ( 3.0 * z * z - 1.0 ),
		     1.092548 * x * z,
		     0.546274 * ( x * x - y * y ) };
}

/// the cosine lobe's zonal factors A0 = pi, A1 = 2 pi / 3, A2 = pi / 4
constexpr double c_cosineBand[3] = { c_pi, 2.0 * c_pi / 3.0, c_pi / 4.0 };

/// \brief The band (0, 1 or 2) of coefficient \p index.
inline int SHBand( int index ) { return index == 0 ? 0 : index < 4 ? 1
	                                                               : 2; }

inline SHCoeffs SHZero() {
	SHCoeffs c;
	c.fill( Vec3( 0, 0, 0 ) );
	return c;
}

/// \brief The radiance coefficients times the cosine lobe's Al, so that E(n) = max( sum c_i Y_i( n ), 0 ).
inline SHCoeffs ConvolveCosine( const SHCoeffs& radiance ) {
	SHCoeffs c;
	for ( int i = 0; i < 9; ++i ) c[i] = radiance[i] * c_cosineBand[SHBand( i )];
	return c;
}

/// \brief sum c_i Y_i( \p n ) for the unit \p n, unclamped.
inline Vec3 SHEvaluate( const SHCoeffs& coeffs, const Vec3& n ) {
	const std::array<double, 9> y = SHBasis( n );
	Vec3 sum( 0, 0, 0 );
	for ( int i = 0; i < 9; ++i ) sum += coeffs[i] * y[i];
	return sum;
}

/// \brief The irradiance E( n ) = sum Al Llm Ylm( n ) of the radiance coefficients \p radiance at the unit normal \p n, each channel clamped
/// below at 0.
inline Vec3 Irradiance( const SHCoeffs& radiance, const Vec3& n ) {
	const Vec3 e = SHEvaluate( ConvolveCosine( radiance ), n );
	return Vec3( std::max( e[0], 0.0 ), std::max( e[1], 0.0 ), std::max( e[2], 0.0 ) );
}

/// \brief The analytic projection of a radiance \p L that is uniform over the upper hemisphere (z > 0) and black below:
/// L00 = 0.282095 x 2 pi L, L10 = 0.488603 x pi L, all others 0 (L20 integrates to 0 over the hemisphere).
inline SHCoeffs ProjectUpperHemisphere( const Vec3& L ) {
	SHCoeffs c = SHZero();
	c[0] = L * ( 0.282095 * 2.0 * c_pi );
	c[2] = L * ( 0.488603 * c_pi );
	return c;
}

/// \brief The projection of a uniform radiance \p L over the whole sphere: L00 = 0.282095 x 4 pi L.
inline SHCoeffs ProjectUniformSphere( const Vec3& L ) {
	SHCoeffs c = SHZero();
	c[0] = L * ( 0.282095 * 4.0 * c_pi );
	return c;
}

/// \brief The projection of the farbox \p faces (in FaceSuffix order, each a non-empty image, rows top to bottom) times \p scale: each
/// texel's linear value at its TexelDirection, weighted by TexelSolidAngle. Faces of different sizes are each projected at their own
/// resolution.
inline SHCoeffs ProjectFarbox( const std::array<ImageRGBA8, c_faceCount>& faces, const Vec3& scale ) {
	SHCoeffs c = SHZero();
	for ( int face = 0; face < c_faceCount; ++face ) {
		const ImageRGBA8& image = faces[face];
		if ( image.width <= 0 || image.height <= 0 || image.pixels == nullptr ) { continue; }
		const int axis = FaceAxis( face );
		for ( int j = 0; j < image.height; ++j ) {
			for ( int i = 0; i < image.width; ++i ) {
				double s, t;
				TexelFaceCoords( image.width, image.height, i, j, s, t );
				const Vec3 dir = vector3_normalised( AxisDirection( axis, s, t ) );
				const double w = TexelSolidAngle( image.width, image.height, s, t );
				const Vec3 L = TexelLinear( image.pixels + ( std::size_t( j ) * image.width + i ) * 4 );
				const std::array<double, 9> y = SHBasis( dir );
				for ( int k = 0; k < 9; ++k ) c[k] += L * ( y[k] * w );
			}
		}
	}
	for ( Vec3& v : c ) v = Vec3( v[0] * scale[0], v[1] * scale[1], v[2] * scale[2] );
	return c;
}

// --- Sky radiance -----------------------------------------------------------

/// The sky's radiance L( w ) = s x C x T( w ). Bakes and other radiance consumers use this, never the truncated SH, which is negative below
/// the horizon of a cloud-layer sky. The farbox images are borrowed, not owned.
struct SkyRadiance
{
	enum class Kind
	{
		None,   ///< emits nothing
		Farbox, ///< six faces over the whole sphere
		Cloud,  ///< the sky stage's image mean over the upper hemisphere, black below
	};
	Kind kind = Kind::None;
	Vec3 scale = Vec3( 0, 0, 0 );                ///< s x C
	std::array<ImageRGBA8, c_faceCount> faces{}; ///< Farbox: in FaceSuffix order, rows top to bottom
	Vec3 mean = Vec3( 0, 0, 0 );                 ///< Cloud: the mean linear texel M

	/// \brief L( \p dir ) for any non-zero direction: the farbox texel the stock renderer draws in that direction (nearest), or the cloud
	/// mean above the horizon.
	Vec3 operator()( const Vec3& dir ) const {
		const auto scaled = [this]( const Vec3& v ) { return Vec3( v[0] * scale[0], v[1] * scale[1], v[2] * scale[2] ); };
		if ( kind == Kind::Cloud ) { return dir[2] > 0 ? scaled( mean ) : Vec3( 0, 0, 0 ); }
		if ( kind != Kind::Farbox ) { return Vec3( 0, 0, 0 ); }
		// the major axis, then the inverse of AxisDirection on it
		int major = 0;
		for ( int j = 1; j < 3; ++j ) {
			if ( std::fabs( dir[j] ) > std::fabs( dir[major] ) ) { major = j; }
		}
		if ( !( std::fabs( dir[major] ) > 0 ) ) { return Vec3( 0, 0, 0 ); }
		const int axis = major * 2 + ( dir[major] < 0 ? 1 : 0 );
		const Vec3 d = dir / std::fabs( dir[major] );
		double b[3] = { 0, 0, 0 };
		for ( int j = 0; j < 3; ++j ) {
			const int k = detail::c_stToVec[axis][j];
			b[std::abs( k ) - 1] = k < 0 ? -d[j] : d[j];
		}
		const ImageRGBA8& image = faces[detail::c_skyTexOrder[axis]];
		if ( image.width <= 0 || image.height <= 0 || image.pixels == nullptr ) { return Vec3( 0, 0, 0 ); }
		const int i = std::clamp( static_cast<int>( std::floor( ( b[0] + 1.0 ) * 0.5 * image.width ) ), 0, image.width - 1 );
		const int j = std::clamp( static_cast<int>( std::floor( ( 1.0 - b[1] ) * 0.5 * image.height ) ), 0, image.height - 1 );
		return scaled( TexelLinear( image.pixels + ( std::size_t( j ) * image.width + i ) * 4 ) );
	}
};

/// \brief A farbox sky's radiance; \p faces must stay alive while it is used.
inline SkyRadiance FarboxRadiance( const SkySource& source, const std::array<ImageRGBA8, c_faceCount>& faces ) {
	SkyRadiance r;
	r.kind = EffectiveStrength( source ) > 0 ? SkyRadiance::Kind::Farbox : SkyRadiance::Kind::None;
	r.scale = RadianceScale( source );
	r.faces = faces;
	return r;
}

/// \brief A cloud-layer sky's radiance from its sky stage image.
inline SkyRadiance CloudRadiance( const SkySource& source, const ImageRGBA8& stageImage ) {
	SkyRadiance r;
	r.kind = EffectiveStrength( source ) > 0 ? SkyRadiance::Kind::Cloud : SkyRadiance::Kind::None;
	r.scale = RadianceScale( source );
	r.mean = ImageMean( stageImage );
	return r;
}

/// \brief The L0-L2 radiance coefficients of \p radiance: the solid-angle-weighted cube projection for a farbox, the analytic hemisphere for a
/// cloud layer, zero for none.
inline SHCoeffs ProjectRadiance( const SkyRadiance& radiance ) {
	switch ( radiance.kind ) {
	case SkyRadiance::Kind::Farbox:
		return ProjectFarbox( radiance.faces, radiance.scale );
	case SkyRadiance::Kind::Cloud:
		return ProjectUpperHemisphere(
			Vec3( radiance.scale[0] * radiance.mean[0], radiance.scale[1] * radiance.mean[1], radiance.scale[2] * radiance.mean[2] ) );
	default:
		return SHZero();
	}
}

// --- Choosing the sky -------------------------------------------------------

/// \brief \p a < \p b by shader name, compared without case (ties broken by the exact name, so the order is total).
inline bool NameLess( const std::string& a, const std::string& b ) {
	const auto lower = []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); };
	const std::size_t n = std::min( a.size(), b.size() );
	for ( std::size_t i = 0; i < n; ++i ) {
		const char ca = lower( a[i] ), cb = lower( b[i] );
		if ( ca != cb ) { return ca < cb; }
	}
	if ( a.size() != b.size() ) { return a.size() < b.size(); }
	return a < b;
}

/// \brief \p candidates sorted with NameLess and with duplicates removed (names equal without case count once).
inline std::vector<std::string> SortCandidates( std::vector<std::string> candidates ) {
	std::sort( candidates.begin(), candidates.end(), NameLess );
	const auto equalNoCase = []( const std::string& a, const std::string& b ) {
		if ( a.size() != b.size() ) { return false; }
		for ( std::size_t i = 0; i < a.size(); ++i ) {
			if ( std::tolower( static_cast<unsigned char>( a[i] ) ) != std::tolower( static_cast<unsigned char>( b[i] ) ) ) { return false; }
		}
		return true;
	};
	candidates.erase( std::unique( candidates.begin(), candidates.end(), equalNoCase ), candidates.end() );
	return candidates;
}

/// \brief The map's sky shader among \p candidates (the shaders that declare `skyparms`): the alphabetically first, compared without case;
/// std::nullopt when there is none.
inline std::optional<std::string> ChooseSky( const std::vector<std::string>& candidates ) {
	if ( candidates.empty() ) { return std::nullopt; }
	return *std::min_element( candidates.begin(), candidates.end(), NameLess );
}

} // namespace skylight
