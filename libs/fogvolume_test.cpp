/*
   Tests for libs/fogvolume.h (openspec change fog-preview, task 2.1):
   `make test-fog-volume`. One case per editor-free scenario of the fog-volume
   capability, plus comparisons against transcriptions of rend2's CalcFog and
   of opengl1's opaque fog path.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.
 */

#include "fogvolume.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace fogvolume;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check( bool ok, const char* what ) {
	++g_checks;
	if ( !ok ) {
		++g_failures;
		std::printf( "FAIL: %s\n", what );
	}
}

void checkNear( double a, double b, double tolerance, const char* what ) {
	++g_checks;
	if ( !( std::fabs( a - b ) <= tolerance ) ) {
		++g_failures;
		std::printf( "FAIL: %s: %.9g vs %.9g (tolerance %g)\n", what, a, b, tolerance );
	}
}

FogShader FogShaderOf( V3 colour, double distance ) {
	FogShader s;
	s.hasFog = true;
	s.colour = colour;
	s.distance = distance;
	s.fog = true;
	s.translucent = true; // surfaceparm trans, as the town's and the benchmark's fog shaders
	return s;
}

/// an axis-aligned box's six sides, in the side order given as axial indices (0..5 = -X, +X, -Y, +Y, -Z, +Z)
std::vector<Side> Box( V3 mins, V3 maxs, const FogShader& shader, const std::vector<int>& order = { 0, 1, 2, 3, 4, 5 } ) {
	std::vector<Side> sides;
	for ( int axis : order ) {
		Side side;
		side.shader = shader;
		switch ( axis ) {
		case 0: side.plane = Plane{ V3( -1, 0, 0 ), -mins.x }; break;
		case 1: side.plane = Plane{ V3( 1, 0, 0 ), maxs.x }; break;
		case 2: side.plane = Plane{ V3( 0, -1, 0 ), -mins.y }; break;
		case 3: side.plane = Plane{ V3( 0, 1, 0 ), maxs.y }; break;
		case 4: side.plane = Plane{ V3( 0, 0, -1 ), -mins.z }; break;
		default: side.plane = Plane{ V3( 0, 0, 1 ), maxs.z }; break;
		}
		sides.push_back( side );
	}
	return sides;
}

bool SameNormal( const V3& a, const V3& b ) {
	return std::fabs( a.x - b.x ) < 1e-9 && std::fabs( a.y - b.y ) < 1e-9 && std::fabs( a.z - b.z ) < 1e-9;
}

const V3 c_townColour( 0.200392, 0.258824, 0.231373 );

// ---------------------------------------------------------------------------
// Transcription of rend2's CalcFog (ioq3 code/renderergl2/glsl/generic_vp.glsl lines 209-221, uniforms from
// ComputeFogValues in code/renderergl2/tr_shade.cpp lines 737-776 and tcScale from tr_bsp.cpp), for a world surface
// (backEnd.ori = identity) seen from eye along forward. The shader writes sqrt( clamp( CalcFog, 0, 1 ) ) (line 277).
float Rend2CalcFog( const Volume& volume, const V3& eye, const V3& forward, const V3& position ) {
	const float tcScale = 1.0f / float( volume.distance * 8 );
	// fogDistanceVector = ( view axis, dot( origin - viewOrigin, axis[0] ) ) * tcScale
	const float fogDistance[4] = { float( forward.x ) * tcScale, float( forward.y ) * tcScale, float( forward.z ) * tcScale,
		                           float( -Dot( eye, forward ) ) * tcScale };
	float fogDepth[4] = { 0, 0, 0, 0 };
	float eyeT = 1; // non-surface fog always has eye inside
	if ( volume.hasSurface ) {
		// fog->surface = -plane normal, surface[3] = -dist; fogDepthVector[3] = -surface[3]
		fogDepth[0] = float( -volume.surface.normal.x );
		fogDepth[1] = float( -volume.surface.normal.y );
		fogDepth[2] = float( -volume.surface.normal.z );
		fogDepth[3] = float( volume.surface.dist );
		eyeT = float( eye.x ) * fogDepth[0] + float( eye.y ) * fogDepth[1] + float( eye.z ) * fogDepth[2] + fogDepth[3];
	}
	const float p[3] = { float( position.x ), float( position.y ), float( position.z ) };
	float s = ( p[0] * fogDistance[0] + p[1] * fogDistance[1] + p[2] * fogDistance[2] + fogDistance[3] ) * 8.0f;
	float t = p[0] * fogDepth[0] + p[1] * fogDepth[1] + p[2] * fogDepth[2] + fogDepth[3];
	const float eyeOutside = eyeT < 0.0f ? 1.0f : 0.0f;
	const float fogged = t >= eyeOutside ? 1.0f : 0.0f;
	t += 1e-6f;
	t *= fogged / ( t - eyeT * eyeOutside );
	const float fog = s * t;
	return std::sqrt( std::min( std::max( fog, 0.0f ), 1.0f ) );
}

// ---------------------------------------------------------------------------
// Transcription of opengl1's opaque fog path: R_InitFogTable and R_FogFactor (ioq3 code/renderergl1/tr_image.cpp
// lines 1035-1082), the 256 x 32 fog image of R_CreateFogImage (lines 1089-1111, IMGFLAG_CLAMPTOEDGE, alpha = 255 d
// truncated to a byte), RB_CalcFogTexCoords (code/renderergl1/tr_shade_calc.cpp lines 784-866) and the fog pass's
// GL_LINEAR sampling (tr_shade.cpp line 576), evaluated at one point instead of interpolated per vertex.
struct Opengl1Fog
{
	float table[256];
	std::uint8_t alpha[32][256];

	static float FogFactor( const float* table, float s, float t ) {
		s -= 1.0f / 512;
		if ( s < 0 ) {
			return 0;
		}
		if ( t < 1.0f / 32 ) {
			return 0;
		}
		if ( t < 31.0f / 32 ) {
			s *= ( t - 1.0f / 32.0f ) / ( 30.0f / 32.0f );
		}
		s *= 8;
		if ( s > 1.0f ) {
			s = 1.0f;
		}
		return table[int( s * ( 256 - 1 ) )];
	}

	Opengl1Fog() {
		for ( int i = 0; i < 256; ++i ) {
			table[i] = float( std::pow( float( i ) / ( 256 - 1 ), 0.5f ) );
		}
		for ( int x = 0; x < 256; ++x ) {
			for ( int y = 0; y < 32; ++y ) {
				const float d = FogFactor( table, ( x + 0.5f ) / 256, ( y + 0.5f ) / 32 );
				alpha[y][x] = std::uint8_t( 255 * d );
			}
		}
	}

	/// RB_CalcFogTexCoords for one world-space point
	static void TexCoords( const Volume& volume, const V3& eye, const V3& forward, const V3& v, float& s, float& t ) {
		const float tcScale = 1.0f / float( volume.distance * 8 );
		float fogDistance[4] = { float( forward.x ) * tcScale, float( forward.y ) * tcScale, float( forward.z ) * tcScale,
			                     float( -Dot( eye, forward ) ) * tcScale };
		float fogDepth[4] = { 0, 0, 0, 0 };
		float eyeT;
		if ( volume.hasSurface ) {
			fogDepth[0] = float( -volume.surface.normal.x );
			fogDepth[1] = float( -volume.surface.normal.y );
			fogDepth[2] = float( -volume.surface.normal.z );
			fogDepth[3] = float( volume.surface.dist );
			eyeT = float( eye.x ) * fogDepth[0] + float( eye.y ) * fogDepth[1] + float( eye.z ) * fogDepth[2] + fogDepth[3];
		}
		else {
			eyeT = 1;
		}
		const bool eyeOutside = eyeT < 0;
		fogDistance[3] += 1.0f / 512;
		s = float( v.x ) * fogDistance[0] + float( v.y ) * fogDistance[1] + float( v.z ) * fogDistance[2] + fogDistance[3];
		t = float( v.x ) * fogDepth[0] + float( v.y ) * fogDepth[1] + float( v.z ) * fogDepth[2] + fogDepth[3];
		if ( eyeOutside ) {
			if ( t < 1.0f ) {
				t = 1.0f / 32;
			}
			else {
				t = 1.0f / 32 + 30.0f / 32 * t / ( t - eyeT );
			}
		}
		else {
			if ( t < 0 ) {
				t = 1.0f / 32;
			}
			else {
				t = 31.0f / 32;
			}
		}
	}

	/// GL_LINEAR, GL_CLAMP_TO_EDGE sample of the fog image's alpha, as 0..1
	double Sample( float s, float t ) const {
		const double u = double( s ) * 256 - 0.5, w = double( t ) * 32 - 0.5;
		const double fu = std::floor( u ), fw = std::floor( w );
		const double au = u - fu, aw = w - fw;
		const auto texel = [&]( int x, int y ) {
			x = std::clamp( x, 0, 255 );
			y = std::clamp( y, 0, 31 );
			return double( alpha[y][x] ) / 255;
		};
		const int x0 = int( fu ), y0 = int( fw );
		return ( 1 - aw ) * ( ( 1 - au ) * texel( x0, y0 ) + au * texel( x0 + 1, y0 ) )
		     + aw * ( ( 1 - au ) * texel( x0, y0 + 1 ) + au * texel( x0 + 1, y0 + 1 ) );
	}

	double Amount( const Volume& volume, const V3& eye, const V3& forward, const V3& point ) const {
		float s, t;
		TexCoords( volume, eye, forward, point, s, t );
		return Sample( s, t );
	}
};

// ---------------------------------------------------------------------------

void TestContentAndOrder() {
	const FogShader townFog = FogShaderOf( c_townColour, 2048 );

	{ // Town volume
		const std::vector<Side> sides = Box( V3( -1032, -1032, -8 ), V3( 1032, 1032, 264 ), townFog );
		const int content = ContentSide( sides );
		check( content >= 0 && sides[content].shader.hasFog, "Town volume: content shader declares fogparms" );
		const Volume v = MakeVolume( sides, content );
		checkNear( v.colour.x, 0.200392, 1e-9, "Town volume: colour r" );
		checkNear( v.colour.y, 0.258824, 1e-9, "Town volume: colour g" );
		checkNear( v.colour.z, 0.231373, 1e-9, "Town volume: colour b" );
		checkNear( v.distance, 2048, 0, "Town volume: distance" );
		check( v.hasSurface && SameNormal( v.surface.normal, V3( 0, 0, 1 ) ), "Town volume: the editor's fog surface is +Z" );
		check( Contains( v, V3( -783, 0, 50 ), 1 ), "Town volume: the street eye is inside" );

		// Sealed room outside the volume (rooms are below z = -8)
		const std::vector<Volume> volumes{ v };
		check( ChooseVolume( volumes, V3( -609, 510, -269 ), 300 ) == -1, "Sealed room outside the volume" );
		check( ChooseVolume( volumes, V3( -609, 510, -40 ), 300 ) == -1, "Sealed room: under the floor, beyond the tolerance" );
	}

	{ // Floor flush with a fog face: the benchmark's street, floor in the plane of the bottom face
		const std::vector<Side> sides = Box( V3( -512, -512, 0 ), V3( 512, 512, 256 ), FogShaderOf( V3( 0.45, 0.45, 0.5 ), 1024 ) );
		const std::vector<Volume> volumes{ MakeVolume( sides, ContentSide( sides ) ) };
		check( ChooseVolume( volumes, V3( 100, 100, 0 ), 400 ) == 0, "Floor flush with a fog face: on the plane" );
		// 24-bit depth reconstruction error at 400 units is far below ε = 1 + 400/512
		check( ChooseVolume( volumes, V3( 100, 100, -1.5 ), 400 ) == 0, "Floor flush with a fog face: within the tolerance" );
		check( ChooseVolume( volumes, V3( 100, 100, -2 ), 400 ) == -1, "Floor flush with a fog face: beyond the tolerance" );
	}

	// overlap scenarios: A covers x in [0, 200], B x in [100, 300]; the point x = 150 lies in both
	const auto overlapWinner = [&]( const std::vector<OrderEntity>& entities, const std::vector<std::vector<std::vector<Side>>>& brushes,
	                                const V3& point ) {
		std::vector<Volume> volumes;
		std::vector<BrushKey> keys;
		for ( const BrushKey& key : FogOrder( entities ) ) {
			const std::vector<Side>& sides = brushes[key.entity][key.brush];
			const int content = ContentSide( sides );
			if ( content >= 0 && sides[content].shader.hasFog ) {
				volumes.push_back( MakeVolume( sides, content ) );
				keys.push_back( key );
			}
		}
		const int chosen = ChooseVolume( volumes, point, 100 );
		return chosen < 0 ? BrushKey{ -1, -1 } : keys[chosen];
	};
	const V3 inBoth( 150, 50, 50 );
	{ // Overlapping translucent volumes
		const FogShader trans = FogShaderOf( V3( 1, 0, 0 ), 512 );
		const std::vector<std::vector<std::vector<Side>>> brushes{ { Box( V3( 0, 0, 0 ), V3( 200, 100, 100 ), trans ),
			                                                         Box( V3( 100, 0, 0 ), V3( 300, 100, 100 ), trans ) } };
		std::vector<OrderEntity> entities{ OrderEntity{ EntityKind::Worldspawn, { true, true } } };
		check( overlapWinner( entities, brushes, inBoth ) == BrushKey{ 0, 1 }, "Overlapping translucent volumes: the later in map order" );
	}
	{ // Overlapping opaque volumes
		FogShader opaque = FogShaderOf( V3( 0, 1, 0 ), 512 );
		opaque.translucent = false;
		const std::vector<std::vector<std::vector<Side>>> brushes{ { Box( V3( 0, 0, 0 ), V3( 200, 100, 100 ), opaque ),
			                                                         Box( V3( 100, 0, 0 ), V3( 300, 100, 100 ), opaque ) } };
		std::vector<OrderEntity> entities{ OrderEntity{ EntityKind::Worldspawn, { false, false } } };
		check( overlapWinner( entities, brushes, inBoth ) == BrushKey{ 0, 0 }, "Overlapping opaque volumes: the earlier in map order" );
	}
	{ // Opaque func_group fog under a worldspawn fog
		FogShader opaque = FogShaderOf( V3( 0, 0, 1 ), 512 );
		opaque.translucent = false;
		const std::vector<std::vector<std::vector<Side>>> brushes{ { Box( V3( 0, 0, 0 ), V3( 200, 100, 100 ), opaque ) },
			                                                       { Box( V3( 100, 0, 0 ), V3( 300, 100, 100 ), opaque ) } };
		std::vector<OrderEntity> entities{ OrderEntity{ EntityKind::Worldspawn, { false } }, OrderEntity{ EntityKind::FuncGroup, { false } } };
		check( overlapWinner( entities, brushes, inBoth ) == BrushKey{ 0, 0 }, "Opaque func_group fog under a worldspawn fog: worldspawn wins" );
	}
	{ // the full order on a mixed map
		std::vector<OrderEntity> entities{
			OrderEntity{ EntityKind::Worldspawn, { false, true, false, true } }, // 0: opaque 0, 2; translucent 1, 3
			OrderEntity{ EntityKind::FuncGroup, { false, true, false } },        // 1
			OrderEntity{ EntityKind::Other, { false, true, false } },            // 2
			OrderEntity{ EntityKind::FuncGroup, { true, false } },               // 3
		};
		const std::vector<BrushKey> expected{
			{ 3, 1 }, // 1. later groups first
			{ 1, 0 },
			{ 1, 2 }, //    each group's in map order
			{ 0, 2 },
			{ 0, 0 }, // 2. worldspawn opaque reversed
			{ 0, 1 },
			{ 0, 3 }, // 3. worldspawn translucent
			{ 1, 1 },
			{ 3, 0 }, // 4. groups' translucent in group order
			{ 2, 2 },
			{ 2, 0 },
			{ 2, 1 }, // 5. other entities
		};
		check( FogOrder( entities ) == expected, "Volume order: the five parts on a mixed map" );
	}

	{ // Fog face over a non-solid side: first side a nonsolid trigger shader, the others fog
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), FogShaderOf( V3( 0.5, 0.5, 0.5 ), 256 ) );
		sides[0].shader = FogShader();
		sides[0].shader.translucent = true; // surfaceparm trigger
		const int content = ContentSide( sides );
		check( content > 0 && sides[content].shader.hasFog, "Fog face over a non-solid side: a fog volume" );
		check( Translucent( sides ), "Fog face over a non-solid side: translucent through its trigger side" );
	}
	{ // Liquid wins
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), FogShaderOf( V3( 0.5, 0.5, 0.5 ), 256 ) );
		sides[3].shader = FogShader();
		sides[3].shader.liquid = sides[3].shader.translucent = true; // surfaceparm water, no fogparms
		const int content = ContentSide( sides );
		check( content == 3 && !sides[content].shader.hasFog, "Liquid wins: no fog volume" );
	}
	{ // Fogged water
		FogShader water = FogShaderOf( V3( 0.1, 0.2, 0.3 ), 300 );
		water.fog = false;
		water.liquid = true;
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), FogShader() );
		sides[2].shader = water;
		const int content = ContentSide( sides );
		check( content == 2 && sides[content].shader.hasFog, "Fogged water: a fog volume" );
		const Volume v = MakeVolume( sides, content );
		check( v.colour.z == 0.3 && v.distance == 300, "Fogged water: the water shader's colour and distance" );
	}
	{ // Clamps
		const V3 c = ClampColour( V3( 1.5, 0.5, -0.2 ) );
		check( c.x == 1 && c.y == 0.5 && c.z == 0, "Clamps: colour (1, 0.5, 0)" );
		check( ClampDistance( 0.5 ) == 1 && ClampDistance( 2048 ) == 2048, "Clamps: distance raised to 1" );
	}
	{ // Too many volumes, too many planes
		std::vector<Candidate> nine( 9, Candidate{ 6, "" } );
		std::vector<int> over;
		int found;
		const std::vector<int> honoured = SelectVolumes( nine, over, found );
		check( honoured.size() == 8 && found == 9 && over.empty() && honoured.back() == 7, "Too many volumes: the first 8 of 9" );
		std::vector<Candidate> planes{ Candidate{ 6, "a" }, Candidate{ 33, "b" }, Candidate{ 32, "c" } };
		const std::vector<int> kept = SelectVolumes( planes, over, found );
		check( kept == std::vector<int>{ 0, 2 } && over == std::vector<int>{ 1 } && found == 2, "Too many planes: the 33-plane brush is no volume" );
	}
}

void TestSurface() {
	const FogShader fog = FogShaderOf( V3( 0.5, 0.5, 0.5 ), 512 );
	// sides in a scrambled map order, so the rules can't lean on it
	const std::vector<int> scrambled{ 3, 5, 0, 4, 1, 2 };
	const auto surfaceNormal = [&]( const V3& dir ) {
		const std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), fog, scrambled );
		const int side = FogSurfaceSide( sides, dir );
		return side < 0 ? V3( 0, 0, 0 ) : sides[side].plane.normal;
	};
	check( SameNormal( surfaceNormal( V3() ), V3( 0, 0, 1 ) ), "Box over a street: +Z" );
	check( SameNormal( surfaceNormal( V3( 1, 0, 0 ) ), V3( -1, 0, 0 ) ), "Explicit direction: -X" );
	check( SameNormal( surfaceNormal( V3( 1, 1, 0 ) ), V3( -1, 0, 0 ) ), "Tie on direction: -X" );
	check( SameNormal( surfaceNormal( V3( 0, 0, 0 ) ), V3( 0, 0, 1 ) ), "Direction with no opposed side: zero direction" );
	{ // no side opposes ( 0 0 1 ) once the -Z side is gone: fall back to the axial order
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), fog );
		sides.erase( sides.begin() + 4 ); // drop -Z
		const int side = FogSurfaceSide( sides, V3( 0, 0, 1 ) );
		check( side >= 0 && SameNormal( sides[side].plane.normal, V3( 0, 0, 1 ) ), "Direction with no opposed side: axial fallback" );
	}
	{ // nodraw +Z and bevels skipped
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), fog );
		sides[5].shader.nodraw = true;
		int side = FogSurfaceSide( sides, V3() );
		check( side >= 0 && SameNormal( sides[side].plane.normal, V3( 0, 0, -1 ) ), "Fog surface: a nodraw +Z side is skipped for -Z" );
		sides[4].bevel = true;
		side = FogSurfaceSide( sides, V3() );
		check( side >= 0 && SameNormal( sides[side].plane.normal, V3( 0, 1, 0 ) ), "Fog surface: bevels are skipped" );
	}
	{ // non-axial sides come after the axial ones, in map order
		FogShader nodraw = fog;
		nodraw.nodraw = true;
		std::vector<Side> sides = Box( V3( 0, 0, 0 ), V3( 64, 64, 64 ), nodraw );
		Side slanted;
		slanted.shader = fog;
		slanted.plane = Plane{ V3( 0.6, 0, 0.8 ), 60 };
		sides.insert( sides.begin(), slanted );
		const int side = FogSurfaceSide( sides, V3() );
		check( side == 0, "Fog surface: the first drawn non-axial side when every axial side is nodraw" );
	}
}

void TestAmount() {
	const V3 forward( 1, 0, 0 );
	{ // Eye inside a volume without a surface
		Volume v;
		v.distance = 2048;
		checkNear( FogAmount( v, V3( 0, 0, 0 ), forward, V3( 512, 0, 0 ) ), 0.5, 1e-12, "Eye inside a volume without a surface: f = 0.5" );
		// Opaque at the distance
		checkNear( FogAmount( v, V3( 0, 0, 0 ), forward, V3( 2048, 0, 0 ) ), 1, 1e-12, "Opaque at the distance" );
		checkNear( FogAmount( v, V3( 0, 0, 0 ), forward, V3( 4000, 30, 0 ) ), 1, 1e-12, "Opaque beyond the distance" );
		// Point behind the eye
		check( FogAmount( v, V3( 0, 0, 0 ), forward, V3( -10, 0, 0 ) ) == 0 && FogAmount( v, V3( 0, 0, 0 ), forward, V3( 0, 50, 0 ) ) == 0,
		       "Point behind the eye: f = 0" );
	}
	Volume surfaced;
	surfaced.distance = 1000;
	surfaced.hasSurface = true;
	surfaced.surface = Plane{ V3( 0, 0, 1 ), 0 }; // fog below z = 0
	{                                             // Eye outside above a surfaced volume
		const V3 eye( 0, 0, 100 ), point( 300, 0, -50 );
		checkNear( Fraction( true, SurfaceT( surfaced, eye ), SurfaceT( surfaced, point ), 1 ), 50.0 / 150.0, 1e-12,
		           "Eye outside above a surfaced volume: fraction 50 / 150" );
		const V3 dir = point - eye;
		const double len = std::sqrt( Dot( dir, dir ) );
		const V3 view = dir * ( 1 / len );
		checkNear( FogAmount( surfaced, eye, view, point ), std::sqrt( len * ( 50.0 / 150.0 ) / 1000 ), 1e-12, "Eye outside: f" );
		// within 1 below the plane the point is unfogged
		check( Fraction( true, -100, 0.5, 1 ) == 0, "Eye outside: t(p) < 1 gives no fog" );
	}
	{ // eye below the surface plane (inside), point above it beyond the tolerance: f = 0
		const V3 eye( 0, 0, -50 ), point( 200, 0, 20 );
		check( FogAmount( surfaced, eye, forward, point ) == 0, "Eye below the surface, point above: f = 0" );
		check( FogAmount( surfaced, eye, forward, V3( 200, 0, 0.5 ) ) > 0, "Eye below the surface, point within ε above: fogged" );
	}
}

void TestComposite() {
	const V3 C = c_townColour;
	{ // Fullbright wall
		const V3 P( 200, 100, 50 );
		for ( double f : { 0.0, 0.5, 1.0 } ) {
			const V3 out = Composite( P, f, C );
			checkNear( out.x, ( 1 - f ) * 200 + f * 255 * C.x, 1e-9, "Fullbright wall: r" );
			checkNear( out.z, ( 1 - f ) * 50 + f * 255 * C.z, 1e-9, "Fullbright wall: b" );
		}
	}
	{ // Fog meets the sky: a surface at the fog's distance (f = 1) beside the sky image 51 66 59
		Volume v;
		v.distance = 2048;
		v.colour = C;
		const double f = FogAmount( v, V3( 0, 0, 0 ), V3( 1, 0, 0 ), V3( 2048, 0, 0 ) );
		const V3 out = Composite( V3( 180, 20, 90 ), f, C );
		const double sky[3] = { 51, 66, 59 };
		checkNear( std::round( out.x ), sky[0], 1, "Fog meets the sky: r" );
		checkNear( std::round( out.y ), sky[1], 1, "Fog meets the sky: g" );
		checkNear( std::round( out.z ), sky[2], 1, "Fog meets the sky: b" );
	}
	{ // the town's far facade: depth 1807, eye inside, no stock surface
		Volume v;
		v.distance = 2048;
		const double f = FogAmount( v, V3( -783, 0, 50 ), V3( 1, 0, 0 ), V3( 1024, 0, 200 ) );
		checkNear( f, std::sqrt( 1807.0 / 2048.0 ), 1e-12, "Town far facade: f = sqrt( 1807 / 2048 )" );
		std::printf( "town far facade: f = %.6f\n", f );
	}
}

void TestPack() {
	Volume v;
	v.colour = V3( 0.25, 0.5, 0.75 );
	v.distance = 300;
	v.hasSurface = true;
	v.surface = Plane{ V3( 0, 0, 1 ), 64 };
	v.planes = { Plane{ V3( 1, 0, 0 ), 10 }, Plane{ V3( 0, 0, 1 ), 64 } };
	const std::vector<float> t = PackVolumes( { v }, V3( 0, 0, 100 ) );
	check( t.size() == std::size_t( c_texWidth * c_maxVolumes * 4 ), "Pack: texture size" );
	check( t[0] == 0.25f && t[3] == 300.f, "Pack: colour and distance" );
	check( t[4 + 2] == 1.f && t[4 + 3] == 64.f, "Pack: surface plane" );
	check( t[8] == 1.f && t[9] == 2.f && t[10] == -36.f, "Pack: has surface, plane count, t(e)" );
	check( t[12] == 1.f && t[15] == 10.f && t[18] == 1.f, "Pack: planes" );
}

/// sample eye/point grids, comparing FogAmount with the transcriptions
void TestStockComparisons() {
	const Opengl1Fog opengl1;
	Volume bare;
	bare.distance = 1024;
	Volume surfaced = bare;
	surfaced.hasSurface = true;
	surfaced.surface = Plane{ V3( 0, 0, 1 ), 512 }; // fog below z = 512
	// short distances, so that depth / d passes opengl1's cap of 8
	Volume bareShort = bare, surfacedShort = surfaced;
	bareShort.distance = surfacedShort.distance = 128;

	double rend2Max = 0, gl1InsideMax = 0, gl1OutsideMax = 0;
	int gl1InsideCount = 0, gl1OutsideCount = 0;
	const V3 forwards[3] = { V3( 1, 0, 0 ), V3( 0.8, 0, -0.6 ), V3( 0, 0.6, -0.8 ) };
	for ( const Volume* volume : { &bare, &surfaced, &bareShort, &surfacedShort } ) {
		for ( double eyeZ : { -200.0, 100.0, 500.0, 600.0, 900.0, 1500.0 } ) {
			const V3 eye( 10, -20, eyeZ );
			const bool eyeOutside = volume->hasSurface && SurfaceT( *volume, eye ) < 0;
			for ( const V3& forward : forwards ) {
				for ( double a = -1500; a <= 3000; a += 37.5 ) {
					for ( double b = -900; b <= 900; b += 75 ) {
						for ( double z = -600; z <= 520; z += 70 ) {
							const V3 point( eye.x + a, eye.y + b, z );
							const double depth = Dot( point - eye, forward );
							if ( depth <= 0 ) {
								continue;
							}
							// rend2 has no tolerance: compare away from the eye-inside ε band
							const double tp = volume->hasSurface ? SurfaceT( *volume, point ) : 1;
							const bool inBand = volume->hasSurface && !eyeOutside && tp < 0 && tp >= -MembershipTolerance( depth );
							const double mine = FogAmount( *volume, eye, forward, point );
							if ( !inBand ) {
								rend2Max = std::max( rend2Max, std::fabs( mine - Rend2CalcFog( *volume, eye, forward, point ) ) );
							}
							// opengl1 only fogs surfaces assigned to the volume, so only points in it (below the surface plane) count
							if ( inBand || tp < 0 ) {
								continue;
							}
							const double gl1 = opengl1.Amount( *volume, eye, forward, point );
							if ( mine >= 0.1 || gl1 >= 0.1 ) {
								const double diff = std::fabs( mine - gl1 );
								if ( eyeOutside ) {
									gl1OutsideMax = std::max( gl1OutsideMax, diff );
									++gl1OutsideCount;
								}
								else {
									gl1InsideMax = std::max( gl1InsideMax, diff );
									++gl1InsideCount;
								}
							}
						}
					}
				}
			}
		}
	}
	checkNear( rend2Max, 0, 1e-5, "CalcFog agreement over the grid" );
	checkNear( gl1InsideMax, 0, 0.05, "opengl1 agreement, no surface or eye inside, f >= 0.1" );
	std::printf( "CalcFog: largest difference %.3g\n", rend2Max );
	std::printf( "opengl1, no surface or eye inside (f >= 0.1, %d samples): largest difference %.4f\n", gl1InsideCount, gl1InsideMax );
	std::printf( "opengl1, eye outside a surfaced volume (f >= 0.1, %d samples, no bound): largest difference %.4f\n", gl1OutsideCount,
	             gl1OutsideMax );
}

} // namespace

int main() {
	TestContentAndOrder();
	TestSurface();
	TestAmount();
	TestComposite();
	TestPack();
	TestStockComparisons();
	std::printf( "%d checks, %d failures\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
