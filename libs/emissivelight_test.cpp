/* -------------------------------------------------------------------------------

   Tests for the emissive-light helpers (libs/emissivelight.h). Built and run by
   `make test-emissive-light`; exits 0 when every check passes.

   The cases follow task 3.1 of openspec change emissive-area-lights and the
   scenarios of its emissive-light-derivation spec that need no editor. The
   attachment test is also checked against q3map2's ShAreaRectAttached
   (tools/quake3/q3map2/light_sh_model.cpp) on random surfaces.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   -------------------------------------------------------------------------------
 */

#include "emissivelight.h"

#include "../tools/quake3/q3map2/light_sh_model.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace emissivelight;

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
#define CHECK_STR( a, b ) \
	do { \
		const std::string a_ = ( a ), b_ = ( b ); \
		Report( a_ == b_, __FILE__, __LINE__, std::string( #a " == " #b ": \"" ) + a_ + "\" vs \"" + b_ + "\"" ); \
	} while ( 0 )

/// the angle between two vectors, in degrees
double AngleBetween( const Vec3& a, const Vec3& b ) {
	return std::atan2( vector3_length( vector3_cross( a, b ) ), vector3_dot( a, b ) ) * c_rad2deg;
}

/// a w x h image filled by \p fill( x, y ) -> RGBA
template<typename Fill>
std::vector<std::uint8_t> MakePixels( int w, int h, Fill fill ) {
	std::vector<std::uint8_t> pixels( w * h * 4 );
	for ( int y = 0; y < h; ++y )
		for ( int x = 0; x < w; ++x ) {
			const std::array<std::uint8_t, 4> c = fill( x, y );
			for ( int i = 0; i < 4; ++i ) pixels[( y * w + x ) * 4 + i] = c[i];
		}
	return pixels;
}

std::vector<Vec2> UVRect( double s0, double t0, double s1, double t1 ) {
	return { Vec2( s0, t0 ), Vec2( s1, t0 ), Vec2( s1, t1 ), Vec2( s0, t1 ) };
}

/// the four corners of an axis-aligned rectangle in the plane x = \p x, CCW seen from +X
std::vector<Vec3> QuadX( double x, double y0, double y1, double z0, double z1 ) {
	return { Vec3( x, y0, z0 ), Vec3( x, y1, z0 ), Vec3( x, y1, z1 ), Vec3( x, y0, z1 ) };
}

// --- Footprint and radiance -----------------------------------------------

void TestUniformImage() {
	g_test = "Uniform image";
	CHECK_NEAR( SrgbToLinear( 188 / 255.0 ), 0.5029, 1e-4 );
	const auto pixels = MakePixels( 1, 1, []( int, int ) { return std::array<std::uint8_t, 4>{ 188, 188, 188, 255 }; } );
	const ImageRGBA8 image{ 1, 1, pixels.data() };
	const Vec3 m = FootprintMean( image, UVRect( 0, 0, 1, 1 ), false );
	const Vec3 l = EmissiveRadiance( 4, Vec3( 1, 0.5, 0.25 ), m );
	CHECK_VEC_NEAR( l, Vec3( 4 * 0.5029 * 1, 4 * 0.5029 * 0.5, 4 * 0.5029 * 0.25 ), 0.001 );
	// the same over a tiled, skewed footprint
	const Vec3 m2 = FootprintMean( image, { Vec2( -3.2, 0.4 ), Vec2( 5.1, 1.7 ), Vec2( 2.0, 9.3 ) }, false );
	CHECK_VEC_NEAR( m2, m, 1e-12 );
}

void TestFootprintNotWholeImage() {
	g_test = "Footprint, not the whole image";
	// 8 x 4, left half black, right half white
	const auto pixels = MakePixels( 8, 4, []( int x, int ) {
		const std::uint8_t v = x < 4 ? 0 : 255;
		return std::array<std::uint8_t, 4>{ v, v, v, 255 };
	} );
	const ImageRGBA8 image{ 8, 4, pixels.data() };
	CHECK_VEC_NEAR( FootprintMean( image, UVRect( 0, 0, 0.5, 1 ), false ), Vec3( 0, 0, 0 ), 1e-12 );
	CHECK_VEC_NEAR( FootprintMean( image, UVRect( 0.5, 0, 1, 1 ), false ), Vec3( 1, 1, 1 ), 1e-12 );
	CHECK_VEC_NEAR( FootprintMean( image, UVRect( 0, 0, 1, 1 ), false ), Vec3( 0.5, 0.5, 0.5 ), 0.01 );
}

void TestTiledFace() {
	g_test = "Tiled face";
	// two texels along s: black, then white; repeated 3.5 times from s = 0
	const auto pixels = MakePixels( 2, 1, []( int x, int ) {
		const std::uint8_t v = x == 0 ? 0 : 255;
		return std::array<std::uint8_t, 4>{ v, v, v, 255 };
	} );
	const ImageRGBA8 image{ 2, 1, pixels.data() };
	const Vec3 m = FootprintMean( image, UVRect( 0, 0, 3.5, 1 ), false );
	CHECK_NEAR( m[0], 3.0 / 7.0, 0.01 );
	CHECK_NEAR( m[1], 3.0 / 7.0, 0.01 );
	CHECK_NEAR( m[2], 3.0 / 7.0, 0.01 );

	// the same through a face's affine mapping: a 224 x 64 face, s = y / 64
	const std::vector<Vec3> face = QuadX( 0, 0, 224, 0, 64 );
	const auto map = AffineUVFromTriangle( face[0], face[1], face[2], Vec2( 0, 0 ), Vec2( 3.5, 0 ), Vec2( 3.5, 1 ) );
	CHECK( map.has_value() );
	if ( map ) {
		std::vector<Vec2> uvs;
		for ( const Vec3& v : face ) uvs.push_back( ( *map )( v ) );
		CHECK_NEAR( uvs[3][0], 0, 1e-12 );
		CHECK_NEAR( uvs[3][1], 1, 1e-12 );
		CHECK_NEAR( FootprintMean( image, uvs, false )[0], 3.0 / 7.0, 0.01 );
	}
}

void TestNoRgbGenConst() {
	g_test = "No rgbGen const";
	const auto pixels = MakePixels( 4, 4, []( int, int ) { return std::array<std::uint8_t, 4>{ 255, 255, 255, 255 }; } );
	const ImageRGBA8 image{ 4, 4, pixels.data() };
	const Vec3 l = EmissiveRadiance( 1, Vec3( 1, 1, 1 ), FootprintMean( image, UVRect( 0, 0, 1, 1 ), false ) );
	CHECK_VEC_NEAR( l, Vec3( 1, 1, 1 ), 1e-12 );
}

void TestAlphaWeight() {
	g_test = "SRC_ALPHA ONE weight";
	const auto pixels = MakePixels( 1, 1, []( int, int ) { return std::array<std::uint8_t, 4>{ 255, 255, 255, 51 }; } );
	const ImageRGBA8 image{ 1, 1, pixels.data() };
	CHECK_VEC_NEAR( FootprintMean( image, UVRect( 0, 0, 1, 1 ), true ), Vec3( 0.2, 0.2, 0.2 ), 1e-12 );
	CHECK_VEC_NEAR( FootprintMean( image, UVRect( 0, 0, 1, 1 ), false ), Vec3( 1, 1, 1 ), 1e-12 );
}

// --- Rectangle test -------------------------------------------------------

void TestBoxFace() {
	g_test = "Box face";
	// the six sides of the box [-64, 64] x [-32, 32] x [0, 128]
	const std::vector<std::vector<Vec3>> sides = {
		QuadX( 64, -32, 32, 0, 128 ),
		QuadX( -64, 32, -32, 0, 128 ),
		{ Vec3( -64, 32, 0 ), Vec3( 64, 32, 0 ), Vec3( 64, 32, 128 ), Vec3( -64, 32, 128 ) },
		{ Vec3( 64, -32, 0 ), Vec3( -64, -32, 0 ), Vec3( -64, -32, 128 ), Vec3( 64, -32, 128 ) },
		{ Vec3( -64, -32, 128 ), Vec3( -64, 32, 128 ), Vec3( 64, 32, 128 ), Vec3( 64, -32, 128 ) },
		{ Vec3( -64, -32, 0 ), Vec3( 64, -32, 0 ), Vec3( 64, 32, 0 ), Vec3( -64, 32, 0 ) },
	};
	for ( const auto& side : sides ) CHECK( RectangleCorners( side ).has_value() );
	// a rotated box side, off the grid
	const double a = 0.3;
	const Vec3 u( std::cos( a ), std::sin( a ), 0 ), v( -std::sin( a ) * 0.6, std::cos( a ) * 0.6, 0.8 );
	CHECK( RectangleCorners( { u * 0, u * 100, u * 100 + v * 40, v * 40 } ).has_value() );
}

void TestCollinearVertex() {
	g_test = "Collinear vertex";
	// a T-junction vertex on the bottom edge, and one 0.05 units off the top edge
	const std::vector<Vec3> w = { Vec3( 0, 0, 0 ), Vec3( 0, 40, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 100, 50 ), Vec3( 0, 0, 50 ) };
	const auto corners = RectangleCorners( w );
	CHECK( corners.has_value() );
	const std::vector<Vec3> w2 = { Vec3( 0, 0, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 100, 50 ), Vec3( 0, 30, 50.05 ), Vec3( 0, 0, 50 ) };
	CHECK( RectangleCorners( w2 ).has_value() );
	// a fifth vertex 1 unit off the edge is a real corner
	const std::vector<Vec3> w3 = { Vec3( 0, 0, 0 ), Vec3( 0, 50, -1 ), Vec3( 0, 100, 0 ), Vec3( 0, 100, 50 ), Vec3( 0, 0, 50 ) };
	CHECK( !RectangleCorners( w3 ).has_value() );
}

void TestTrapezoidRefused() {
	g_test = "Trapezoid refused";
	CHECK( !RectangleCorners( { Vec3( 0, 0, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 90, 50 ), Vec3( 0, 10, 50 ) } ).has_value() );
	// a triangle and a parallelogram
	CHECK( !RectangleCorners( { Vec3( 0, 0, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 50, 50 ) } ).has_value() );
	CHECK( !RectangleCorners( { Vec3( 0, 0, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 110, 50 ), Vec3( 0, 10, 50 ) } ).has_value() );
	// a corner 1 degree off square is refused, 0.25 degrees accepted
	const auto skewed = []( double degrees ) {
		const double t = std::tan( degrees * c_deg2rad ) * 50;
		return std::vector<Vec3>{ Vec3( 0, 0, 0 ), Vec3( 0, 100, 0 ), Vec3( 0, 100 + t, 50 ), Vec3( 0, t, 50 ) };
	};
	CHECK( !RectangleCorners( skewed( 1.0 ) ).has_value() );
	CHECK( RectangleCorners( skewed( 0.25 ) ).has_value() );
}

// --- Basis ----------------------------------------------------------------

void TestSignBasis() {
	g_test = "Sign basis";
	const Basis b = AngleVectors( Vec3( 0, 270, 0 ) );
	CHECK_VEC_NEAR( b.forward, Vec3( 0, -1, 0 ), 1e-12 );
	CHECK_VEC_NEAR( b.right, Vec3( -1, 0, 0 ), 1e-12 );
	CHECK_VEC_NEAR( b.up, Vec3( 0, 0, 1 ), 1e-12 );
	const Vec3 angles = AnglesFromBasis( Vec3( 0, -1, 0 ), Vec3( -1, 0, 0 ) );
	CHECK_VEC_NEAR( angles, Vec3( 0, 270, 0 ), 1e-9 );
	CHECK_STR( FormatAngles( angles ), "0 270 0" );
	// angles 0 0 0: forward +X, right -Y, up +Z; right = forward x up
	const Basis b0 = AngleVectors( Vec3( 0, 0, 0 ) );
	CHECK_VEC_NEAR( b0.right, Vec3( 0, -1, 0 ), 1e-12 );
	CHECK_VEC_NEAR( vector3_cross( b0.forward, b0.up ), b0.right, 1e-12 );
}

void TestBasisRoundTrip() {
	g_test = "Basis round trip";
	std::mt19937 rng( 20261120 );
	std::normal_distribution<double> gauss;
	double worst = 0;
	for ( int i = 0; i < 1000; ++i ) {
		const Vec3 f = vector3_normalised( Vec3( gauss( rng ), gauss( rng ), gauss( rng ) ) );
		Vec3 r = Vec3( gauss( rng ), gauss( rng ), gauss( rng ) );
		r = vector3_normalised( r - f * vector3_dot( r, f ) );
		const Vec3 u = vector3_cross( r, f );
		const Vec3 angles = AnglesFromBasis( f, r );
		// evaluated directly, and after writing the key with %g
		Vec3 parsed;
		const std::string text = FormatAngles( angles );
		const bool ok = std::sscanf( text.c_str(), "%lf %lf %lf", &parsed[0], &parsed[1], &parsed[2] ) == 3;
		CHECK( ok );
		for ( const Vec3& a : { angles, parsed } ) {
			const Basis b = AngleVectors( a );
			worst = std::max( { worst, AngleBetween( b.forward, f ), AngleBetween( b.right, r ), AngleBetween( b.up, u ) } );
		}
	}
	CHECK( worst <= 0.01 );
	std::printf( "  basis round trip: worst error %.3g degrees over 1000 pairs\n", worst );
	// straight up and down
	for ( const double z : { 1.0, -1.0 } ) {
		const Vec3 f( 0, 0, z ), r = vector3_normalised( Vec3( 0.6, -0.8, 0 ) );
		const Basis b = AngleVectors( AnglesFromBasis( f, r ) );
		CHECK( AngleBetween( b.forward, f ) <= 0.01 );
		CHECK( AngleBetween( b.right, r ) <= 0.01 );
		CHECK( AngleBetween( b.up, vector3_cross( r, f ) ) <= 0.01 );
	}
}

// --- Created area light ---------------------------------------------------

void TestBenchmarkSignFrame() {
	g_test = "Benchmark sign";
	// front face: plane y = 248, normal -Y, x in [-96, 96], z in [104, 136]
	const std::vector<Vec3> face = { Vec3( 96, 248, 104 ), Vec3( -96, 248, 104 ), Vec3( -96, 248, 136 ), Vec3( 96, 248, 136 ) };
	const auto corners = RectangleCorners( face );
	CHECK( corners.has_value() );
	if ( !corners ) { return; }
	const Rect rect = AreaRectFromCorners( *corners, Vec3( 0, -1, 0 ) );
	CHECK_VEC_NEAR( rect.centre, Vec3( 0, 248, 120 ), 1e-9 );
	CHECK_VEC_NEAR( rect.forward, Vec3( 0, -1, 0 ), 1e-12 );
	CHECK_VEC_NEAR( rect.right, Vec3( -1, 0, 0 ), 1e-12 );
	CHECK_VEC_NEAR( rect.up, Vec3( 0, 0, 1 ), 1e-12 );
	CHECK_NEAR( rect.width, 192, 1e-9 );
	CHECK_NEAR( rect.height, 32, 1e-9 );
	CHECK_STR( FormatAngles( AnglesFromBasis( rect.forward, rect.right ) ), "0 270 0" );
	CHECK_NEAR( AreaLightRadius( rect.width, rect.height ), 256, 0 );
	// Re-derive finds the face it was created from
	const AttachRect attach = AttachRectFromAngles( rect.centre, Vec3( 0, 270, 0 ), rect.width, rect.height );
	CHECK( FaceAttached( attach, face, Vec3( 0, -1, 0 ) ) );
	CHECK_NEAR( ClipPolygonToRect( face, rect ).area, 192 * 32, 1e-6 );

	// a horizontal face: right along the longer edge, up sideways
	const std::vector<Vec3> ceiling = { Vec3( 0, 0, 64 ), Vec3( 0, 32, 64 ), Vec3( 128, 32, 64 ), Vec3( 128, 0, 64 ) };
	const Rect flat = AreaRectFromCorners( *RectangleCorners( ceiling ), Vec3( 0, 0, -1 ) );
	CHECK_NEAR( flat.width, 128, 1e-9 );
	CHECK_NEAR( flat.height, 32, 1e-9 );
	CHECK_NEAR( std::fabs( flat.right[0] ), 1, 1e-12 );
	CHECK_NEAR( flat.up[2], 0, 1e-12 );
	// a sloped face: right horizontal, up pointing up
	const Vec3 slope = vector3_normalised( Vec3( 1, 0, 1 ) );
	const std::vector<Vec3> sloped = { Vec3( 0, 0, 0 ), slope * 20, slope * 20 + Vec3( 0, 100, 0 ), Vec3( 0, 100, 0 ) };
	const Rect tilted = AreaRectFromCorners( *RectangleCorners( sloped ), vector3_normalised( Vec3( -1, 0, 1 ) ) );
	CHECK_NEAR( tilted.width, 100, 1e-9 );
	CHECK_NEAR( tilted.right[2], 0, 1e-12 );
	CHECK( tilted.up[2] > 0 );
}

void TestBenchmarkFireplaceFrame() {
	g_test = "Benchmark fireplace";
	// fire face: plane y = 736, normal -Y, x in [-232, -136], z in [16, 64]
	const std::vector<Vec3> face = { Vec3( -136, 736, 16 ), Vec3( -232, 736, 16 ), Vec3( -232, 736, 64 ), Vec3( -136, 736, 64 ) };
	const auto corners = RectangleCorners( face );
	CHECK( corners.has_value() );
	if ( !corners ) { return; }
	const Rect rect = AreaRectFromCorners( *corners, Vec3( 0, -1, 0 ) );
	CHECK_VEC_NEAR( rect.centre, Vec3( -184, 736, 40 ), 1e-9 );
	CHECK_VEC_NEAR( rect.right, Vec3( -1, 0, 0 ), 1e-12 );
	CHECK_VEC_NEAR( rect.up, Vec3( 0, 0, 1 ), 1e-12 );
	CHECK_NEAR( rect.width, 96, 1e-9 );
	CHECK_NEAR( rect.height, 48, 1e-9 );
	CHECK_STR( FormatAngles( AnglesFromBasis( rect.forward, rect.right ) ), "0 270 0" );
	CHECK_NEAR( AreaLightRadius( rect.width, rect.height ), 256, 0 );
	const AttachRect attach = AttachRectFromAngles( rect.centre, Vec3( 0, 270, 0 ), rect.width, rect.height );
	CHECK( FaceAttached( attach, face, Vec3( 0, -1, 0 ) ) );
	// the jamb's inner face beside the opening is perpendicular to the rectangle: not attached
	const std::vector<Vec3> jamb = { Vec3( -232, 720, 16 ), Vec3( -232, 736, 16 ), Vec3( -232, 736, 64 ), Vec3( -232, 720, 64 ) };
	CHECK( !FaceAttached( attach, jamb, Vec3( 1, 0, 0 ) ) );
}

void TestLongEmitterRadius() {
	g_test = "Long emitter radius";
	CHECK_NEAR( 2 * 0.5 * std::sqrt( 1024.0 * 1024.0 + 64.0 * 64.0 ), 1026.0, 0.01 );
	CHECK_NEAR( AreaLightRadius( 1024, 64 ), 1032, 0 );
	CHECK_NEAR( AreaLightRadius( 192, 32 ), 256, 0 );
	CHECK_NEAR( AreaLightRadius( 64, 64 ), 256, 0 );
}

void TestRadianceSplit() {
	g_test = "Radiance split";
	const auto s = SplitRadiance( Vec3( 2.8, 0.98, 0.7 ) );
	CHECK( s.has_value() );
	if ( s ) {
		CHECK_NEAR( s->intensity, 2.8, 1e-12 );
		CHECK_VEC_NEAR( s->colour, Vec3( 1, 0.35, 0.25 ), 1e-12 );
	}
	CHECK( !SplitRadiance( Vec3( 0, 0, 0 ) ).has_value() );
}

// --- Proxies --------------------------------------------------------------

void TestOpaqueEmitter() {
	g_test = "Opaque emitter";
	// a 10 x 100 face in the plane x = 0, normal +X, centred at ( 0, 50, 5 )
	const std::vector<Vec3> face = QuadX( 0, 0, 100, 0, 10 );
	ProxySurface s;
	s.radiance = Vec3( 2, 1, 0.5 );
	s.area = PolygonArea( face );
	s.normal = Vec3( 1, 0, 0 );
	s.centroid = PolygonCentroid( face );
	CHECK_NEAR( s.area, 1000, 1e-9 );
	const Proxy p = DeriveProxy( { s } );
	CHECK( p.kind == ProxyKind::Spot );
	const auto split = SplitRadiance( p.flux );
	CHECK( split.has_value() );
	if ( split ) {
		CHECK_NEAR( split->intensity, 2000 * c_pi, 1e-6 );
		CHECK_VEC_NEAR( split->colour, Vec3( 1, 0.5, 0.25 ), 1e-12 );
	}
	CHECK_VEC_NEAR( p.origin, Vec3( 8, 50, 5 ), 1e-9 );
	CHECK_VEC_NEAR( AngleVectors( p.angles ).forward, Vec3( 1, 0, 0 ), 1e-12 );
	CHECK_STR( FormatAngles( p.angles ), "0 0 0" );
	// a face looking down: pitch 90
	ProxySurface down = s;
	down.normal = Vec3( 0, 0, -1 );
	CHECK_VEC_NEAR( AngleVectors( DeriveProxy( { down } ).angles ).forward, Vec3( 0, 0, -1 ), 1e-12 );
}

void TestTwoSidedBillboard() {
	g_test = "Two-sided billboard";
	ProxySurface s;
	s.radiance = Vec3( 2, 1, 0.5 );
	s.area = 1000;
	s.normal = Vec3( 0, 1, 0 );
	s.centroid = Vec3( 10, 20, 30 );
	s.allAdditive = true;
	const Proxy p = DeriveProxy( { s } );
	CHECK( p.kind == ProxyKind::Point );
	const auto split = SplitRadiance( p.flux );
	CHECK( split.has_value() );
	if ( split ) { CHECK_NEAR( split->intensity, 4000 * c_pi, 1e-6 ); }
	CHECK_VEC_NEAR( p.origin, Vec3( 10, 28, 30 ), 1e-9 );
}

void TestFacesPointingApart() {
	g_test = "Faces pointing apart";
	// the cube [0, 20] x [10, 30] x [20, 40], centre ( 10, 20, 30 )
	const Vec3 lo( 0, 10, 20 ), hi( 20, 30, 40 );
	std::vector<ProxySurface> surfaces;
	for ( int axis = 0; axis < 3; ++axis )
		for ( int side = 0; side < 2; ++side ) {
			const int a = ( axis + 1 ) % 3, b = ( axis + 2 ) % 3;
			std::vector<Vec3> face;
			for ( const auto& uv :
			      { std::array<int, 2>{ 0, 0 }, std::array<int, 2>{ 1, 0 }, std::array<int, 2>{ 1, 1 }, std::array<int, 2>{ 0, 1 } } ) {
				Vec3 v;
				v[axis] = side ? hi[axis] : lo[axis];
				v[a] = uv[0] ? hi[a] : lo[a];
				v[b] = uv[1] ? hi[b] : lo[b];
				face.push_back( v );
			}
			ProxySurface s;
			s.radiance = Vec3( 1, 1, 1 );
			s.area = PolygonArea( face );
			s.normal = Vec3( 0, 0, 0 );
			s.normal[axis] = side ? 1 : -1;
			s.centroid = PolygonCentroid( face );
			surfaces.push_back( s );
		}
	const Proxy p = DeriveProxy( surfaces );
	CHECK( p.kind == ProxyKind::Point );
	CHECK_VEC_NEAR( p.origin, Vec3( 10, 20, 30 ), 1e-9 );
	CHECK_NEAR( SplitRadiance( p.flux )->intensity, c_pi * 6 * 400, 1e-6 );
	// the empty set derives nothing
	CHECK( !SplitRadiance( DeriveProxy( {} ).flux ).has_value() );
}

// A tube of radius r about the z axis from z0 to z1, \p segments around and covering \p arc radians, tessellated into outward-wound
// triangles as a patch would be.
std::vector<std::array<Vec3, 3>> TubeTriangles( const Vec3& centre, double r, double z0, double z1, int segments, double arc ) {
	std::vector<std::array<Vec3, 3>> tris;
	for ( int i = 0; i < segments; ++i ) {
		const double t0 = arc * i / segments, t1 = arc * ( i + 1 ) / segments;
		const Vec3 a = centre + Vec3( r * std::cos( t0 ), r * std::sin( t0 ), z0 );
		const Vec3 b = centre + Vec3( r * std::cos( t1 ), r * std::sin( t1 ), z0 );
		const Vec3 c = centre + Vec3( r * std::cos( t1 ), r * std::sin( t1 ), z1 );
		const Vec3 d = centre + Vec3( r * std::cos( t0 ), r * std::sin( t0 ), z1 );
		tris.push_back( { a, b, c } );
		tris.push_back( { a, c, d } );
	}
	return tris;
}

void TestPatchEmitter() {
	g_test = "Patch emitter";
	// a closed patch cylinder passed as ONE surface: radius 16 about ( 100, 200, 0 ), z 300 to 364, 16 segments
	const Vec3 centre( 100, 200, 0 );
	const auto tube = TubeTriangles( centre, 16, 300, 364, 16, 2 * c_pi );
	const ProxySurface s = ProxySurfaceFromTriangles( Vec3( 1, 1, 1 ), false, tube );
	// the area of the inscribed 16-gon prism, and a mean normal of about zero
	const double area = 16 * 2 * 16 * std::sin( c_pi / 16 ) * 64;
	CHECK_NEAR( s.area, area, 1e-6 );
	CHECK( vector3_length( s.normal ) < 1e-9 );
	CHECK_VEC_NEAR( s.centroid, Vec3( 100, 200, 332 ), 1e-9 );
	const Proxy p = DeriveProxy( { s } );
	CHECK( p.kind == ProxyKind::Point );
	CHECK_VEC_NEAR( p.origin, Vec3( 100, 200, 332 ), 1e-9 );
	CHECK_NEAR( SplitRadiance( p.flux )->intensity, c_pi * area, 1e-6 );
	// one surface per triangle derives the same light
	std::vector<ProxySurface> perTriangle;
	for ( const auto& t : tube ) perTriangle.push_back( ProxySurfaceFromTriangles( Vec3( 1, 1, 1 ), false, { t } ) );
	const Proxy q = DeriveProxy( perTriangle );
	CHECK( q.kind == ProxyKind::Point );
	CHECK_VEC_NEAR( q.origin, Vec3( 100, 200, 332 ), 1e-9 );

	// a half cylinder (0 to 180 degrees) facing +y: |n| = 2 r h of a total pi r h (ratio 0.64, above 0.1), so a spot along +y, 8 units off
	// the centroid
	const auto half = TubeTriangles( centre, 16, 300, 364, 32, c_pi );
	const ProxySurface h = ProxySurfaceFromTriangles( Vec3( 1, 1, 1 ), false, half );
	CHECK( vector3_length( h.normal ) > 0.6 && vector3_length( h.normal ) < 0.65 );
	const Proxy hp = DeriveProxy( { h } );
	CHECK( hp.kind == ProxyKind::Spot );
	CHECK_VEC_NEAR( AngleVectors( hp.angles ).forward, Vec3( 0, 1, 0 ), 1e-9 );
	CHECK_VEC_NEAR( hp.origin, h.centroid + Vec3( 0, 8, 0 ), 1e-9 );

	// a 340 degree arc: |n| / A = sin( 170 deg ) / ( 170 deg in radians ), about 0.058, below 0.1, so a light at the centroid
	const double arc = 340.0 * c_pi / 180.0;
	const auto nearlyClosed = TubeTriangles( centre, 16, 300, 364, 64, arc );
	const ProxySurface nc = ProxySurfaceFromTriangles( Vec3( 1, 1, 1 ), false, nearlyClosed );
	const Proxy ncp = DeriveProxy( { nc } );
	CHECK( ncp.kind == ProxyKind::Point );
	CHECK_VEC_NEAR( ncp.origin, nc.centroid, 1e-9 );
}

// --- Attachment -----------------------------------------------------------

void TestTwoFacesUnderOneRectangle() {
	g_test = "Two faces under one rectangle";
	// rectangle: centre origin, angles 0 0 0 (forward +X, right -Y, up +Z), 128 x 64
	const Rect rect{ Vec3( 0, 0, 0 ), Vec3( 1, 0, 0 ), Vec3( 0, -1, 0 ), Vec3( 0, 0, 1 ), 128, 64 };
	const AttachRect attach = AttachRectFromAngles( rect.centre, Vec3( 0, 0, 0 ), rect.width, rect.height );
	const auto white = MakePixels( 2, 2, []( int, int ) { return std::array<std::uint8_t, 4>{ 255, 255, 255, 255 }; } );
	const ImageRGBA8 image{ 2, 2, white.data() };
	// two coplanar faces each 64 x 96 (sticking out of the rectangle at top and bottom), strengths 2 and 4
	const std::vector<Vec3> faces[2] = { QuadX( 0, 0, 64, -48, 48 ), QuadX( 0, -64, 0, -48, 48 ) };
	const double strengths[2] = { 2, 4 };
	std::vector<Overlap> overlaps;
	for ( int i = 0; i < 2; ++i ) {
		CHECK( FaceAttached( attach, faces[i], Vec3( 1, 0, 0 ) ) );
		const ClippedPolygon clip = ClipPolygonToRect( faces[i], rect );
		CHECK_NEAR( clip.area, 64 * 64, 1e-9 );
		const auto map = AffineUVFromTriangle( faces[i][0], faces[i][1], faces[i][2], Vec2( 0, 0 ), Vec2( 0.5, 0 ), Vec2( 0.5, 0.75 ) );
		std::vector<Vec2> uvs;
		for ( const Vec3& v : clip.points ) uvs.push_back( ( *map )( v ) );
		overlaps.push_back( { clip.area, EmissiveRadiance( strengths[i], Vec3( 1, 1, 1 ), FootprintMean( image, uvs, false ) ) } );
	}
	CHECK_VEC_NEAR( overlaps[0].radiance, Vec3( 2, 2, 2 ), 1e-12 );
	CHECK_VEC_NEAR( overlaps[1].radiance, Vec3( 4, 4, 4 ), 1e-12 );
	const auto split = SplitRadiance( OverlapWeightedRadiance( overlaps ) );
	CHECK( split.has_value() );
	if ( split ) {
		CHECK_NEAR( split->intensity, 3, 1e-12 );
		CHECK_VEC_NEAR( split->colour, Vec3( 1, 1, 1 ), 1e-12 );
	}
}

void TestFootprintOfOverlap() {
	g_test = "Footprint of the overlap only";
	// a face whose left half (inside the rectangle) shows black and right half white: the overlap's mean is 0
	const auto pixels = MakePixels( 2, 1, []( int x, int ) {
		const std::uint8_t v = x == 0 ? 0 : 255;
		return std::array<std::uint8_t, 4>{ v, v, v, 255 };
	} );
	const ImageRGBA8 image{ 2, 1, pixels.data() };
	// face y in [-32, 96], z in [-16, 16] in plane x = 0, s = ( y + 32 ) / 128; rectangle covers y in [-32, 32]
	const std::vector<Vec3> face = QuadX( 0, -32, 96, -16, 16 );
	const Rect rect{ Vec3( 0, 0, 0 ), Vec3( 1, 0, 0 ), Vec3( 0, -1, 0 ), Vec3( 0, 0, 1 ), 64, 32 };
	const auto map = AffineUVFromTriangle( face[0], face[1], face[2], Vec2( 0, 0 ), Vec2( 1, 0 ), Vec2( 1, 1 ) );
	const ClippedPolygon clip = ClipPolygonToRect( face, rect );
	CHECK_NEAR( clip.area, 64 * 32, 1e-9 );
	std::vector<Vec2> uvs;
	for ( const Vec3& v : clip.points ) uvs.push_back( ( *map )( v ) );
	CHECK_VEC_NEAR( FootprintMean( image, uvs, false ), Vec3( 0, 0, 0 ), 1e-12 );
}

void TestTiltedFaceNotAttached() {
	g_test = "Tilted face not attached";
	const AttachRect attach = AttachRectFromAngles( Vec3( 0, 0, 0 ), Vec3( 0, 0, 0 ), 64, 64 );
	// a 20 x 20 face turned about the Z axis through its centre: its corners stay within 1 unit of the plane
	const auto turned = []( double degrees ) {
		const double c = std::cos( degrees * c_deg2rad ), s = std::sin( degrees * c_deg2rad );
		std::vector<Vec3> face;
		for ( const Vec3& v : QuadX( 0, -10, 10, -10, 10 ) ) face.push_back( Vec3( v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2] ) );
		return std::make_pair( face, Vec3( c, s, 0 ) );
	};
	const auto [face2, normal2] = turned( 2.0 );
	for ( const Vec3& v : face2 ) CHECK( std::fabs( v[0] ) < 1.0 );
	CHECK( !FaceAttached( attach, face2, normal2 ) );
	const auto [face05, normal05] = turned( 0.5 );
	CHECK( FaceAttached( attach, face05, normal05 ) );
	// facing the other way
	CHECK( !FaceAttached( attach, QuadX( 0, -10, 10, -10, 10 ), Vec3( -1, 0, 0 ) ) );
}

void TestLightMovedOff() {
	g_test = "Light moved off its face";
	const std::vector<Vec3> face = QuadX( 0, -32, 32, -32, 32 );
	CHECK( FaceAttached( AttachRectFromAngles( Vec3( 0, 0, 0 ), Vec3( 0, 0, 0 ), 64, 64 ), face, Vec3( 1, 0, 0 ) ) );
	CHECK( !FaceAttached( AttachRectFromAngles( Vec3( 20, 0, 0 ), Vec3( 0, 0, 0 ), 64, 64 ), face, Vec3( 1, 0, 0 ) ) );
	// moved sideways off the face, still coplanar
	CHECK( !FaceAttached( AttachRectFromAngles( Vec3( 0, 200, 0 ), Vec3( 0, 0, 0 ), 64, 64 ), face, Vec3( 1, 0, 0 ) ) );
}

void TestTriangleOverlapDecides() {
	g_test = "Triangle overlap decides attachment";
	// rectangle: centre origin, forward +X, right -Y, up +Z, 2 x 2, so the 2D frame is ( -y, z )
	const Rect rect{ Vec3( 0, 0, 0 ), Vec3( 1, 0, 0 ), Vec3( 0, -1, 0 ), Vec3( 0, 0, 1 ), 2, 2 };
	const AttachRect attach = AttachRectFromAngles( rect.centre, Vec3( 0, 0, 0 ), 2, 2 );
	// a square face whose corner overlaps the rectangle's corner [0.96, 1]^2 (area 0.0016); its fan diagonal from the first vertex splits
	// that overlap into two triangles of 0.0008 each
	const auto at = []( double a, double b ) { return Vec3( 0, -a, b ); };
	const std::vector<Vec3> face = { at( 0.96, 0.96 ), at( 5, 0.96 ), at( 5, 5 ), at( 0.96, 5 ) };
	const double polygonOverlap = ClipPolygonToRect( face, rect ).area;
	CHECK_NEAR( polygonOverlap, 0.0016, 1e-9 );
	CHECK( polygonOverlap > c_attachMinArea );
	CHECK( !FaceAttached( attach, face, Vec3( 1, 0, 0 ) ) );
	// the same face fanned from another vertex keeps a triangle over the whole overlap
	const std::vector<Vec3> refanned = { face[1], face[2], face[3], face[0] };
	CHECK( FaceAttached( attach, refanned, Vec3( 1, 0, 0 ) ) );
}

/// FaceAttached against q3map2's ShAreaRectAttached on random faces near every threshold
void TestMatchesQ3map2() {
	g_test = "Matches ShAreaRectAttached";
	std::mt19937 rng( 7 );
	std::uniform_real_distribution<double> unit( 0, 1 );
	std::normal_distribution<double> gauss;
	int attached = 0, mismatches = 0;
	const int cases = 20000;
	for ( int i = 0; i < cases; ++i ) {
		const Vec3 angles( unit( rng ) * 180 - 90, unit( rng ) * 360, unit( rng ) * 360 );
		const Vec3 origin( gauss( rng ) * 500, gauss( rng ) * 500, gauss( rng ) * 500 );
		const double width = 0.05 + unit( rng ) * 200, height = 0.05 + unit( rng ) * 200;
		const AttachRect attach = AttachRectFromAngles( origin, angles, width, height );
		const Basis b = AngleVectors( angles );

		// a convex polygon in the rectangle's frame: points on an ellipse, tilted and offset along forward
		const int n = 3 + static_cast<int>( unit( rng ) * 6 );
		const double cx = ( unit( rng ) - 0.5 ) * ( width + 4 ), cy = ( unit( rng ) - 0.5 ) * ( height + 4 );
		const double scale = unit( rng ) < 0.5 ? 0.02 + unit( rng ) * 0.1 : 1 + unit( rng ) * 60;
		const double tilt = unit( rng ) * 2.0 * c_deg2rad, tiltDir = unit( rng ) * 2 * c_pi;
		const double offset = ( unit( rng ) - 0.5 ) * 2.4;
		const Vec3 normal =
			vector3_normalised( b.forward + ( b.right * std::cos( tiltDir ) + b.up * std::sin( tiltDir ) ) * std::tan( tilt ) );
		const double flip = unit( rng ) < 0.1 ? -1 : 1;
		std::vector<Vec3> face;
		const double phase = unit( rng ) * 2 * c_pi;
		for ( int k = 0; k < n; ++k ) {
			const double a = phase + 2 * c_pi * k / n;
			const double x = cx + scale * std::cos( a ), y = cy + 0.7 * scale * std::sin( a );
			Vec3 p = origin + b.right * x + b.up * y;
			// onto the tilted plane through origin + forward * offset
			p -= normal * vector3_dot( p - ( origin + b.forward * offset ), normal );
			face.push_back( p );
		}
		const bool ours = FaceAttached( attach, face, normal * flip );

		ShAreaRect rect;
		rect.centre = attach.centre;
		rect.forward = attach.forward;
		rect.right = attach.right;
		rect.up = attach.up;
		rect.width = attach.width;
		rect.height = attach.height;
		std::vector<Vector3> positions, normals;
		std::vector<std::uint32_t> indices;
		for ( const Vec3& v : face ) {
			positions.push_back( Vector3( static_cast<float>( v[0] ), static_cast<float>( v[1] ), static_cast<float>( v[2] ) ) );
			normals.push_back( Vector3( static_cast<float>( normal[0] * flip ), static_cast<float>( normal[1] * flip ),
			                            static_cast<float>( normal[2] * flip ) ) );
		}
		for ( std::uint32_t k = 1; k + 1 < face.size(); ++k ) {
			indices.push_back( 0 );
			indices.push_back( k );
			indices.push_back( k + 1 );
		}
		const bool theirs = ShAreaRectAttached( rect, positions, normals, indices );
		attached += theirs;
		if ( ours != theirs ) { ++mismatches; }
	}
	CHECK( mismatches == 0 );
	// the sample exercises both outcomes
	CHECK( attached > cases / 20 && attached < cases - cases / 20 );
	std::printf( "  q3map2 attachment: %d of %d cases attached, %d mismatches\n", attached, cases, mismatches );
}

} // namespace

int main() {
	TestUniformImage();
	TestFootprintNotWholeImage();
	TestTiledFace();
	TestNoRgbGenConst();
	TestAlphaWeight();
	TestBoxFace();
	TestCollinearVertex();
	TestTrapezoidRefused();
	TestSignBasis();
	TestBasisRoundTrip();
	TestBenchmarkSignFrame();
	TestBenchmarkFireplaceFrame();
	TestLongEmitterRadius();
	TestRadianceSplit();
	TestOpaqueEmitter();
	TestTwoSidedBillboard();
	TestFacesPointingApart();
	TestPatchEmitter();
	TestTwoFacesUnderOneRectangle();
	TestFootprintOfOverlap();
	TestTiltedFaceNotAttached();
	TestLightMovedOff();
	TestTriangleOverlapDecides();
	TestMatchesQ3map2();

	if ( g_failures ) {
		std::fprintf( stderr, "emissivelight_test: %d of %d checks failed\n", g_failures, g_checks );
		return 1;
	}
	std::printf( "emissivelight_test: all %d checks passed\n", g_checks );
	return 0;
}
