/* -------------------------------------------------------------------------------

   Tests for the sky light helpers (libs/skylight.h). Built and run by
   `make test-sky-light`; exits 0 when every check passes.

   The cases follow tasks 2.1, 2.2 and 6.1 of openspec change
   sky-environment-light and the scenarios of its sky-environment-light spec
   that need no editor. The farbox face mapping is checked against a
   transcription of the stock renderer's MakeSkyVec
   (ioq3 code/renderergl1/tr_sky.cpp).

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   -------------------------------------------------------------------------------
 */

#include "skylight.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace skylight;

namespace
{

// --- Harness --------------------------------------------------------------

int g_failures = 0;
int g_checks = 0;
const char* g_test = "";

void Report( bool ok, const char* file, int line, const std::string& what ) {
	++g_checks;
	if ( !ok ) {
		++g_failures;
		std::fprintf( stderr, "%s:%d: [%s] check failed: %s\n", file, line, g_test, what.c_str() );
	}
}

#define CHECK( cond ) Report( ( cond ), __FILE__, __LINE__, #cond )
#define CHECK_NEAR( a, b, tol ) \
	do { \
		const double a_ = ( a ), b_ = ( b ); \
		Report( std::fabs( a_ - b_ ) <= ( tol ), __FILE__, __LINE__, std::string( #a " ~= " #b ": " ) + std::to_string( a_ ) + " vs " + std::to_string( b_ ) ); \
	} while ( 0 )
#define CHECK_VEC_NEAR( a, b, tol ) \
	do { \
		const Vec3 av_ = ( a ), bv_ = ( b ); \
		CHECK_NEAR( av_[0], bv_[0], tol ); \
		CHECK_NEAR( av_[1], bv_[1], tol ); \
		CHECK_NEAR( av_[2], bv_[2], tol ); \
	} while ( 0 )

/// an owned RGBA8 image
struct OwnedImage
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> pixels;

	OwnedImage() = default;
	OwnedImage( int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b ) : width( w ), height( h ), pixels( std::size_t( w ) * h * 4 ) {
		for ( std::size_t i = 0; i < pixels.size(); i += 4 ) {
			pixels[i] = r, pixels[i + 1] = g, pixels[i + 2] = b, pixels[i + 3] = 255;
		}
	}
	std::uint8_t* texel( int i, int j ) { return pixels.data() + ( std::size_t( j ) * width + i ) * 4; }
	ImageRGBA8 view() const {
		ImageRGBA8 v;
		v.width = width;
		v.height = height;
		v.pixels = pixels.data();
		return v;
	}
};

std::array<ImageRGBA8, c_faceCount> Views( const std::array<OwnedImage, c_faceCount>& faces ) {
	std::array<ImageRGBA8, c_faceCount> v;
	for ( int f = 0; f < c_faceCount; ++f ) v[f] = faces[f].view();
	return v;
}

/// normals over the sphere: the six axes, the eight cube corners and the twelve edge midpoints
std::vector<Vec3> TestNormals() {
	std::vector<Vec3> n;
	for ( int x = -1; x <= 1; ++x )
		for ( int y = -1; y <= 1; ++y )
			for ( int z = -1; z <= 1; ++z )
				if ( x != 0 || y != 0 || z != 0 ) { n.push_back( vector3_normalised( Vec3( x, y, z ) ) ); }
	return n;
}

double MaxChannel( const Vec3& v ) { return std::max( { v[0], v[1], v[2] } ); }

SkySource CloudSource( double strength, const Vec3& colour = Vec3( 1, 1, 1 ) ) {
	SkySource s;
	s.name = "textures/test/sky";
	s.stageImage = "textures/test/sky";
	s.stageColour = colour;
	s.hasStrength = true;
	s.strength = strength;
	return s;
}

// --- tr_sky.cpp transcription ---------------------------------------------

/// tr_sky.cpp:277-325, MakeSkyVec, with boxSize 1 and DrawSkyBox's sky_min 0 and sky_max 1 (tr_sky.cpp:362-363): face coordinates \p s, \p t
/// in [-1, 1] on \p axis to a direction and texture coordinates
void MakeSkyVec( float s, float t, int axis, float outSt[2], float outXYZ[3] ) {
	// tr_sky.cpp:280-286
	static int st_to_vec[6][3] = { { 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 } };
	float b[3] = { s, t, 1 };
	for ( int j = 0; j < 3; j++ ) {
		const int k = st_to_vec[axis][j];
		outXYZ[j] = k < 0 ? -b[-k - 1] : b[k - 1];
	}
	s = ( s + 1 ) * 0.5f;
	t = ( t + 1 ) * 0.5f;
	s = std::clamp( s, 0.0f, 1.0f );
	t = std::clamp( t, 0.0f, 1.0f );
	t = 1.0f - t; // tr_sky.cpp:322
	outSt[0] = s;
	outSt[1] = t;
}
/// tr_sky.cpp:330, kSkyTexOrder: axis i draws outerbox[kSkyTexOrder[i]]
const int kSkyTexOrder[6] = { 0, 2, 1, 3, 4, 5 };
/// tr_shader.cpp:1189, ParseSkyParms' suf[6]: outerbox[i] is <farbox>_<suf[i]>
const char* const suf[6] = { "rt", "bk", "lf", "ft", "up", "dn" };

// --- Minimal TGA reader ---------------------------------------------------

/// \brief An uncompressed true-colour TGA (type 2, 24 or 32 bits, either row order) as RGBA8, rows top to bottom; false with \p error set
/// for anything else.
bool ReadTGA( const char* path, OwnedImage& out, std::string& error ) {
	std::ifstream file( path, std::ios::binary );
	if ( !file ) {
		error = std::string( "cannot open " ) + path;
		return false;
	}
	const std::vector<std::uint8_t> data( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	if ( data.size() < 18 ) {
		error = "file shorter than a TGA header";
		return false;
	}
	const int idLength = data[0], colourMapType = data[1], imageType = data[2];
	const int width = data[12] | ( data[13] << 8 ), height = data[14] | ( data[15] << 8 );
	const int bits = data[16], descriptor = data[17];
	if ( imageType != 2 || colourMapType != 0 || ( bits != 24 && bits != 32 ) || width <= 0 || height <= 0 ) {
		error = "not an uncompressed true-colour TGA of 24 or 32 bits (type " + std::to_string( imageType ) + ", " + std::to_string( bits ) + " bits, colour map " + std::to_string( colourMapType ) + ")";
		return false;
	}
	if ( descriptor & 0x10 ) {
		error = "right-to-left TGA rows are not supported";
		return false;
	}
	const int bpp = bits / 8;
	const std::size_t offset = 18 + idLength;
	if ( data.size() < offset + std::size_t( width ) * height * bpp ) {
		error = "TGA pixel data truncated";
		return false;
	}
	const bool topDown = ( descriptor & 0x20 ) != 0;
	out = OwnedImage( width, height, 0, 0, 0 );
	for ( int row = 0; row < height; ++row ) {
		const int j = topDown ? row : height - 1 - row;
		for ( int i = 0; i < width; ++i ) {
			const std::uint8_t* p = data.data() + offset + ( std::size_t( row ) * width + i ) * bpp;
			std::uint8_t* q = out.texel( i, j );
			q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = bpp == 4 ? p[3] : 255;
		}
	}
	return true;
}

/// \brief The `intensity` of the first `light_sun` in the lights JSON at \p path (a minimal scan: the first "intensity" after
/// "light_sun"); NaN when not found.
double SunIntensity( const char* path ) {
	std::ifstream file( path );
	const std::string text( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	const std::size_t sun = text.find( "\"light_sun\"" );
	if ( sun == std::string::npos ) { return std::nan( "" ); }
	const std::size_t key = text.find( "\"intensity\"", sun );
	if ( key == std::string::npos ) { return std::nan( "" ); }
	std::size_t p = text.find( ':', key );
	if ( p == std::string::npos ) { return std::nan( "" ); }
	++p;
	while ( p < text.size() && ( text[p] == ' ' || text[p] == '"' ) ) ++p;
	return std::strtod( text.c_str() + p, nullptr );
}

/// \brief The `qer_pbr_emissiveStrength` of shader \p name in the shader file at \p path (a minimal scan of that shader's braces);
/// NaN when the shader or the keyword is missing.
double ShaderStrength( const char* path, const char* name ) {
	std::ifstream file( path );
	const std::string text( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	std::size_t at = 0;
	const std::string key( name );
	while ( ( at = text.find( key, at ) ) != std::string::npos ) {
		const bool lineStart = at == 0 || text[at - 1] == '\n';
		const std::size_t end = at + key.size();
		if ( lineStart && ( end == text.size() || std::isspace( static_cast<unsigned char>( text[end] ) ) ) ) {
			const std::size_t open = text.find( '{', end ), close = text.find( "\n}", end );
			if ( open == std::string::npos || close == std::string::npos ) { return std::nan( "" ); }
			const std::size_t k = text.find( "qer_pbr_emissiveStrength", open );
			if ( k == std::string::npos || k > close ) { return std::nan( "" ); }
			return std::strtod( text.c_str() + k + std::strlen( "qer_pbr_emissiveStrength" ), nullptr );
		}
		at = end;
	}
	return std::nan( "" );
}

// --- Cases ----------------------------------------------------------------

void TestLegacySky() {
	g_test = "Legacy sky";
	SkySource s = CloudSource( 4 );
	s.hasStrength = false;
	const OwnedImage image( 8, 8, 200, 200, 200 );
	const SkyRadiance r = CloudRadiance( s, image.view() );
	CHECK( r.kind == SkyRadiance::Kind::None );
	for ( const Vec3& n : TestNormals() ) {
		CHECK_VEC_NEAR( r( n ), Vec3( 0, 0, 0 ), 0 );
		CHECK_VEC_NEAR( Irradiance( ProjectRadiance( r ), n ), Vec3( 0, 0, 0 ), 0 );
	}
	// a strength of 0 or less emits nothing either
	for ( double strength : { 0.0, -2.0 } ) {
		const SkyRadiance z = CloudRadiance( CloudSource( strength ), image.view() );
		CHECK( z.kind == SkyRadiance::Kind::None );
		CHECK_VEC_NEAR( z( Vec3( 0, 0, 1 ) ), Vec3( 0, 0, 0 ), 0 );
	}
	// a farbox without the keyword too
	std::array<OwnedImage, c_faceCount> faces;
	faces.fill( OwnedImage( 4, 4, 200, 200, 200 ) );
	const SkyRadiance f = FarboxRadiance( s, Views( faces ) );
	CHECK( f.kind == SkyRadiance::Kind::None );
	CHECK_VEC_NEAR( Irradiance( ProjectRadiance( f ), Vec3( 0, 0, 1 ) ), Vec3( 0, 0, 0 ), 0 );
}

void TestStrengthAndColour() {
	g_test = "Strength and colour";
	const OwnedImage image( 8, 8, 188, 188, 188 );
	const SkyRadiance r = CloudRadiance( CloudSource( 4, Vec3( 1, 0.5, 0.25 ) ), image.view() );
	CHECK_NEAR( SrgbToLinear( 188 / 255.0 ), 0.5029, 0.0001 );
	const Vec3 expected = Vec3( 1, 0.5, 0.25 ) * ( 4 * 0.5029 );
	for ( const Vec3& n : TestNormals() ) {
		if ( n[2] > 0 ) { CHECK_VEC_NEAR( r( n ), expected, 0.001 ); }
	}
	CHECK_VEC_NEAR( r( vector3_normalised( Vec3( 1, 0, 0.01 ) ) ), expected, 0.001 );
}

void TestUniformFarbox() {
	g_test = "Uniform farbox";
	// faces of different sizes, each projected at its own resolution
	const int sizes[c_faceCount] = { 16, 8, 32, 16, 64, 4 };
	std::array<OwnedImage, c_faceCount> faces;
	for ( int f = 0; f < c_faceCount; ++f ) faces[f] = OwnedImage( sizes[f], sizes[f], 120, 120, 120 );
	const double v = SrgbToLinear( 120 / 255.0 );
	const double s = 3.0;
	SkySource source = CloudSource( s );
	source.farbox = "env/test";
	const SkyRadiance r = FarboxRadiance( source, Views( faces ) );
	const SHCoeffs sh = ProjectRadiance( r );
	for ( const Vec3& n : TestNormals() ) {
		const Vec3 e = Irradiance( sh, n );
		for ( int c = 0; c < 3; ++c ) CHECK_NEAR( e[c], c_pi * s * v, 0.005 * c_pi * s * v );
	}
	// the texel solid angles cover the sphere
	double total = 0;
	for ( int f = 0; f < c_faceCount; ++f ) {
		for ( int j = 0; j < sizes[f]; ++j )
			for ( int i = 0; i < sizes[f]; ++i ) {
				double fs, ft;
				TexelFaceCoords( sizes[f], sizes[f], i, j, fs, ft );
				total += TexelSolidAngle( sizes[f], sizes[f], fs, ft );
			}
	}
	CHECK_NEAR( total, 4 * c_pi, 0.005 * 4 * c_pi );
}

void TestUpFaceOnly() {
	g_test = "Up face only";
	std::array<OwnedImage, c_faceCount> faces;
	faces.fill( OwnedImage( 16, 16, 0, 0, 0 ) );
	faces[c_faceUp] = OwnedImage( 16, 16, 255, 255, 255 );
	SkySource source = CloudSource( 1 );
	source.farbox = "env/test";
	const SHCoeffs sh = ProjectRadiance( FarboxRadiance( source, Views( faces ) ) );
	const double up = MaxChannel( Irradiance( sh, Vec3( 0, 0, 1 ) ) );
	for ( const Vec3& n : TestNormals() ) {
		if ( n[2] < 1 ) { CHECK( MaxChannel( Irradiance( sh, n ) ) < up ); }
	}
	CHECK( MaxChannel( Irradiance( sh, Vec3( 0, 0, -1 ) ) ) < 0.05 * up );
}

void TestOrientation() {
	g_test = "Orientation matches the drawn sky (irradiance)";
	std::array<OwnedImage, c_faceCount> faces;
	faces.fill( OwnedImage( 16, 16, 0, 0, 0 ) );
	faces[c_faceRt] = OwnedImage( 16, 16, 255, 255, 255 );
	SkySource source = CloudSource( 1 );
	source.farbox = "env/test";
	const SHCoeffs sh = ProjectRadiance( FarboxRadiance( source, Views( faces ) ) );
	const double px = MaxChannel( Irradiance( sh, Vec3( 1, 0, 0 ) ) );
	for ( const Vec3& n : { Vec3( -1, 0, 0 ), Vec3( 0, 1, 0 ), Vec3( 0, -1, 0 ), Vec3( 0, 0, 1 ), Vec3( 0, 0, -1 ) } ) {
		CHECK( MaxChannel( Irradiance( sh, n ) ) < px );
	}
	// each face lights the axis the stock renderer draws it on
	const Vec3 axes[6] = { Vec3( 1, 0, 0 ), Vec3( -1, 0, 0 ), Vec3( 0, 1, 0 ), Vec3( 0, -1, 0 ), Vec3( 0, 0, 1 ), Vec3( 0, 0, -1 ) };
	for ( int f = 0; f < c_faceCount; ++f ) {
		std::array<OwnedImage, c_faceCount> one;
		one.fill( OwnedImage( 8, 8, 0, 0, 0 ) );
		one[f] = OwnedImage( 8, 8, 255, 255, 255 );
		const SHCoeffs c = ProjectRadiance( FarboxRadiance( source, Views( one ) ) );
		int best = 0;
		for ( int a = 1; a < 6; ++a ) {
			if ( MaxChannel( Irradiance( c, axes[a] ) ) > MaxChannel( Irradiance( c, axes[best] ) ) ) { best = a; }
		}
		CHECK( best == FaceAxis( f ) );
	}
	CHECK( FaceAxis( c_faceRt ) == 0 );
	CHECK( FaceAxis( c_faceLf ) == 1 );
	CHECK( FaceAxis( c_faceBk ) == 2 );
	CHECK( FaceAxis( c_faceFt ) == 3 );
	CHECK( FaceAxis( c_faceUp ) == 4 );
	CHECK( FaceAxis( c_faceDn ) == 5 );
}

void TestOpenFloor() {
	g_test = "Open floor";
	const Vec3 L( 0.7, 0.3, 0.1 );
	const SHCoeffs sh = ProjectUpperHemisphere( L );
	for ( int c = 0; c < 3; ++c ) {
		const double piL = c_pi * L[c];
		CHECK_NEAR( Irradiance( sh, Vec3( 0, 0, 1 ) )[c], piL, 0.005 * piL );
		CHECK_NEAR( Irradiance( sh, Vec3( 1, 0, 0 ) )[c], piL / 2, 0.005 * piL );
		CHECK_NEAR( Irradiance( sh, Vec3( 0, -1, 0 ) )[c], piL / 2, 0.005 * piL );
		CHECK_NEAR( Irradiance( sh, vector3_normalised( Vec3( 1, 1, 0 ) ) )[c], piL / 2, 0.005 * piL );
		CHECK_NEAR( Irradiance( sh, Vec3( 0, 0, -1 ) )[c], 0, 0.005 * piL );
	}
	// through the cloud path from an image
	const OwnedImage image( 4, 4, 188, 188, 188 );
	const SkyRadiance r = CloudRadiance( CloudSource( 2 ), image.view() );
	const double l = 2 * SrgbToLinear( 188 / 255.0 );
	CHECK_NEAR( Irradiance( ProjectRadiance( r ), Vec3( 0, 0, 1 ) )[0], c_pi * l, 0.005 * c_pi * l );
	// the analytic projection agrees with a numerical one of the same hemisphere
	SHCoeffs numeric = SHZero();
	const int n = 400;
	for ( int a = 0; a < n; ++a ) {
		const double cosTheta = ( a + 0.5 ) / n; // uniform in z over (0, 1)
		for ( int b = 0; b < n; ++b ) {
			const double phi = 2 * c_pi * ( b + 0.5 ) / n;
			const double sinTheta = std::sqrt( 1 - cosTheta * cosTheta );
			const Vec3 d( sinTheta * std::cos( phi ), sinTheta * std::sin( phi ), cosTheta );
			const std::array<double, 9> y = SHBasis( d );
			for ( int k = 0; k < 9; ++k ) numeric[k] += L * ( y[k] * 2 * c_pi / ( double( n ) * n ) );
		}
	}
	for ( int k = 0; k < 9; ++k ) CHECK_VEC_NEAR( numeric[k], sh[k], 1e-3 );
}

void TestUniformSphere() {
	g_test = "Uniform sphere";
	const Vec3 L( 0.4, 0.5, 0.6 );
	const SHCoeffs sh = ProjectUniformSphere( L );
	CHECK_VEC_NEAR( sh[0], L * ( 0.282095 * 4 * c_pi ), 1e-12 );
	for ( int k = 1; k < 9; ++k ) CHECK_VEC_NEAR( sh[k], Vec3( 0, 0, 0 ), 0 );
	for ( const Vec3& n : TestNormals() ) CHECK_VEC_NEAR( Irradiance( sh, n ), L * c_pi, 1e-4 );
	// a uniform farbox projects to the same: L00 only
	std::array<OwnedImage, c_faceCount> faces;
	faces.fill( OwnedImage( 32, 32, 255, 255, 255 ) );
	SkySource source = CloudSource( 1 );
	source.farbox = "env/test";
	const SHCoeffs f = ProjectRadiance( FarboxRadiance( source, Views( faces ) ) );
	CHECK_NEAR( f[0][0], 0.282095 * 4 * c_pi, 0.005 * 0.282095 * 4 * c_pi );
	for ( int k = 1; k < 9; ++k ) CHECK_NEAR( f[k][0], 0, 1e-6 );
}

void TestRadianceBelowHorizon() {
	g_test = "Radiance below the horizon";
	const OwnedImage image( 4, 4, 255, 255, 255 );
	const SkyRadiance r = CloudRadiance( CloudSource( 1 ), image.view() );
	CHECK_VEC_NEAR( r( Vec3( 0, 0, -1 ) ), Vec3( 0, 0, 0 ), 0 );
	CHECK_VEC_NEAR( r( vector3_normalised( Vec3( 1, 0, -0.01 ) ) ), Vec3( 0, 0, 0 ), 0 );
	CHECK_VEC_NEAR( r( Vec3( 0, 0, 1 ) ), Vec3( 1, 1, 1 ), 1e-12 );
	// the truncated SH radiance there is -L/4, which is why consumers use L( w )
	CHECK_NEAR( SHEvaluate( ProjectRadiance( r ), Vec3( 0, 0, -1 ) )[0], -0.25, 1e-3 );
}

void TestTwoSkies() {
	g_test = "Two skies";
	CHECK( *ChooseSky( { "textures/b/sky", "textures/a/sky" } ) == "textures/a/sky" );
	CHECK( *ChooseSky( { "textures/b/sky", "Textures/A/sky" } ) == "Textures/A/sky" );
	CHECK( *ChooseSky( { "textures/B/sky", "textures/a/sky", "textures/c" } ) == "textures/a/sky" );
	CHECK( !ChooseSky( {} ) );
	const std::vector<std::string> sorted = SortCandidates( { "textures/b/sky", "textures/a/sky", "textures/A/sky" } );
	CHECK( sorted.size() == 2 );
	CHECK( sorted.size() == 2 && sorted[1] == "textures/b/sky" );
}

void TestFaceMappingAgainstTrSky() {
	g_test = "Face mapping against tr_sky.cpp";
	for ( int f = 0; f < c_faceCount; ++f ) CHECK( std::string( FaceSuffix( f ) ) == suf[f] );
	// faces with a distinct colour per texel, so a texel can be identified
	const int w = 8, h = 4;
	std::array<OwnedImage, c_faceCount> faces;
	for ( int f = 0; f < c_faceCount; ++f ) {
		faces[f] = OwnedImage( w, h, 0, 0, 0 );
		for ( int j = 0; j < h; ++j )
			for ( int i = 0; i < w; ++i ) {
				std::uint8_t* p = faces[f].texel( i, j );
				p[0] = std::uint8_t( 10 + 40 * f ), p[1] = std::uint8_t( 10 + 30 * i ), p[2] = std::uint8_t( 10 + 60 * j );
			}
	}
	SkySource source = CloudSource( 1 );
	source.farbox = "env/test";
	const SkyRadiance r = FarboxRadiance( source, Views( faces ) );
	// MakeSkyVec at points just inside each face's corners and over a grid; the texel the stock renderer samples there
	const float coords[] = { -0.999f, -0.6f, -0.2f, 0.3f, 0.7f, 0.999f };
	for ( int axis = 0; axis < 6; ++axis ) {
		const int face = kSkyTexOrder[axis];
		for ( float s : coords ) {
			for ( float t : coords ) {
				float st[2], xyz[3];
				MakeSkyVec( s, t, axis, st, xyz );
				const int i = std::min( int( st[0] * w ), w - 1 ), j = std::min( int( st[1] * h ), h - 1 );
				const Vec3 dir = vector3_normalised( Vec3( xyz[0], xyz[1], xyz[2] ) );
				// skylight's direction for that texel lies within the texel's footprint of MakeSkyVec's
				const Vec3 texelDir = TexelDirection( face, w, h, i, j );
				CHECK( vector3_dot( texelDir, dir ) > std::cos( 0.5 ) );
				// and L( w ) in MakeSkyVec's direction reads that texel
				CHECK_VEC_NEAR( r( dir ), TexelLinear( faces[face].texel( i, j ) ), 1e-12 );
			}
		}
		// the four corner texels map exactly to MakeSkyVec at their centres
		for ( int corner = 0; corner < 4; ++corner ) {
			const int i = corner & 1 ? w - 1 : 0, j = corner & 2 ? h - 1 : 0;
			const float s = 2.0f * ( i + 0.5f ) / w - 1.0f;
			const float t = 1.0f - 2.0f * ( j + 0.5f ) / h; // the inverse of tr_sky.cpp:322's flip
			float st[2], xyz[3];
			MakeSkyVec( s, t, axis, st, xyz );
			CHECK_NEAR( st[0] * w, i + 0.5, 1e-4 );
			CHECK_NEAR( st[1] * h, j + 0.5, 1e-4 );
			CHECK_VEC_NEAR( TexelDirection( face, w, h, i, j ), vector3_normalised( Vec3( xyz[0], xyz[1], xyz[2] ) ), 1e-6 );
		}
	}
}

void TestHalfTheSun() {
	g_test = "Half the sun";
	OwnedImage image;
	std::string error;
	if ( !ReadTGA( SKYLIGHT_TEST_ROOT "/content/town/textures/town/sky.tga", image, error ) ) {
		Report( false, __FILE__, __LINE__, "reading sky.tga: " + error );
		return;
	}
	const double sun = SunIntensity( SKYLIGHT_TEST_ROOT "/tools/town/town1_lights.json" );
	CHECK( std::isfinite( sun ) );
	// textures/town/sky in content/town/scripts/town.shader: a cloud layer over sky.tga, no rgbGen const
	const double strength = ShaderStrength( SKYLIGHT_TEST_ROOT "/content/town/scripts/town.shader", "textures/town/sky" );
	if ( !std::isfinite( strength ) ) {
		Report( false, __FILE__, __LINE__, "textures/town/sky declares no qer_pbr_emissiveStrength" );
		return;
	}
	SkySource source = CloudSource( strength );
	source.name = source.stageImage = "textures/town/sky";
	const SkyRadiance r = CloudRadiance( source, image.view() );
	const Vec3 e = Irradiance( ProjectRadiance( r ), Vec3( 0, 0, 1 ) );
	std::printf( "Half the sun: sky.tga %dx%d, strength %g, E(+Z) = %.4f %.4f %.4f, sun intensity %g\n", image.width, image.height,
	             strength, e[0], e[1], e[2], sun );
	CHECK_NEAR( MaxChannel( e ), sun / 2, 0.01 * sun / 2 );
}

} // namespace

int main() {
	TestLegacySky();
	TestStrengthAndColour();
	TestUniformFarbox();
	TestUpFaceOnly();
	TestOrientation();
	TestOpenFloor();
	TestUniformSphere();
	TestRadianceBelowHorizon();
	TestTwoSkies();
	TestFaceMappingAgainstTrSky();
	TestHalfTheSun();
	std::printf( "%d checks, %d failed\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
