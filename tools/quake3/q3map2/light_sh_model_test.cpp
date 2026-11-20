/* -------------------------------------------------------------------------------

   Tests for the -shbake stage's pure rules (light_sh_model.h). Built and run by
   `make test-q3map2-shbake-model`; exits 0 when every check passes.

   The cases follow tasks 4.2-4.5 of openspec change q3map2-shbake and the
   editor's spec scenarios (pbr-light-entities, pbr-material-format,
   pbr-shadow-maps).

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "light_sh_model.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{

// --- Harness --------------------------------------------------------------

int g_failures = 0;
int g_checks = 0;
const char* g_test = "";

void Report( bool ok, const char* file, int line, const std::string& what ){
	++g_checks;
	if ( !ok ) {
		++g_failures;
		std::fprintf( stderr, "%s:%d: [%s] check failed: %s\n", file, line, g_test, what.c_str() );
	}
}

#define CHECK( cond ) Report( ( cond ), __FILE__, __LINE__, #cond )
#define CHECK_MSG( cond, msg ) Report( ( cond ), __FILE__, __LINE__, std::string( #cond ) + " -- " + ( msg ) )
// Checks |a - b| <= tol, printing both values if not.
#define CHECK_NEAR( a, b, tol ) \
	do { \
		const double a_ = ( a ), b_ = ( b ); \
		Report( std::fabs( a_ - b_ ) <= ( tol ), __FILE__, __LINE__, \
		        std::string( #a " ~= " #b ": " ) + std::to_string( a_ ) + " vs " + std::to_string( b_ ) ); \
	} while ( 0 )
#define CHECK_EQ_INT( a, b ) \
	do { \
		const long a_ = ( a ), b_ = ( b ); \
		Report( a_ == b_, __FILE__, __LINE__, std::string( #a " == " #b ": " ) + std::to_string( a_ ) + " vs " + std::to_string( b_ ) ); \
	} while ( 0 )

constexpr double c_pi = 3.14159265358979323846;

double Deg( double degrees ){
	return degrees * c_pi / 180.0;
}

#define CHECK_VEC_NEAR( v, x, y, z, tol ) \
	do { \
		CHECK_NEAR( ( v )[0], ( x ), ( tol ) ); \
		CHECK_NEAR( ( v )[1], ( y ), ( tol ) ); \
		CHECK_NEAR( ( v )[2], ( z ), ( tol ) ); \
	} while ( 0 )

/// a light built from a key map, at the origin unless given
ShLight Light( ShLightType type, const std::map<std::string, std::string>& keys,
               const Vector3& origin = Vector3( 0, 0, 0 ), const Vector3* target = nullptr ){
	return ShLightFromKeys( type, origin, [&keys]( const char* key ) -> const char* {
		const auto it = keys.find( key );
		return it == keys.end() ? "" : it->second.c_str();
	}, target );
}

ShStage Stage( const char* map, std::vector<std::string> blend = {} ){
	ShStage stage;
	stage.map = map;
	if ( !blend.empty() ) {
		ShStageParseBlendFunc( stage, blend );
	}
	return stage;
}

ShShaderDesc Shader( std::vector<ShStage> stages ){
	ShShaderDesc shader;
	shader.name = "textures/test/shader";
	shader.stages = std::move( stages );
	return shader;
}

/// the SH projection of a delta light of irradiance e (per channel) arriving from unit direction w
ShCoeffs9 DeltaSH( const Vector3& w, const Vector3& e ){
	ShCoeffs9 sh;
	const std::array<float, 9> y = ShBasis( w );
	for ( int i = 0; i < 9; ++i )
		sh[i] = Vector3( e[0] * y[i], e[1] * y[i], e[2] * y[i] );
	return sh;
}

ShCoeffs9 ZeroSH(){
	ShCoeffs9 sh;
	sh.fill( Vector3( 0, 0, 0 ) );
	return sh;
}

/// constant radiance l over the whole sphere: only L0, = l * Y0 * 4 pi
ShCoeffs9 ConstantSH( float l ){
	ShCoeffs9 sh = ZeroSH();
	sh[0] = Vector3( float( l * c_shBasis0 * 4 * c_pi ) );
	return sh;
}

/// the sum of two SH, coefficient by coefficient
ShCoeffs9 operator+( const ShCoeffs9& a, const ShCoeffs9& b ){
	ShCoeffs9 sh;
	for ( int i = 0; i < 9; ++i )
		sh[i] = a[i] + b[i];
	return sh;
}

/// the irradiance whose lightmap byte is exactly `byte` (smallest such), found by bisection
float IrradianceForByte( int byte ){
	double lo = 0, hi = 11.2 * c_pi;
	for ( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( ShLightmapByte( float( mid ) ) >= byte ? hi : lo ) = mid;
	}
	return float( hi * 1.0001 );
}

// --- 4.1 --------------------------------------------------------------------

void TestPlaceholder(){
	CHECK( ShLightTypeFromClassname( "light" ) == ShLightType::Point );
	CHECK( ShLightTypeFromClassname( "LIGHT_SPOT" ) == ShLightType::Spot );
	CHECK( ShLightTypeFromClassname( "light_sun" ) == ShLightType::Sun );
	CHECK( !ShLightTypeFromClassname( "light_flare" ) );
	CHECK( !ShLightTypeFromClassname( "lightJunior" ) );
	CHECK( !ShLightTypeFromClassname( nullptr ) );
}

// --- 4.2 Light conversions --------------------------------------------------

void TestParsers(){
	float f;
	CHECK( ShParseFloat( "1.5", f ) && f == 1.5f );
	CHECK( ShParseFloat( "-2e3", f ) && f == -2000.0f );
	CHECK( !ShParseFloat( "", f ) );
	CHECK( !ShParseFloat( nullptr, f ) );
	CHECK( !ShParseFloat( "abc", f ) );
	CHECK( !ShParseFloat( "1.5 ", f ) ); // stringio: nothing may follow
	int i;
	CHECK( ShParseInt( "0", i ) && i == 0 );
	CHECK( !ShParseInt( "0.5", i ) );
	Vector3 v;
	CHECK( ShParseVector3( "1 2 3", v ) && v[0] == 1 && v[1] == 2 && v[2] == 3 );
	CHECK( !ShParseVector3( " 1 2 3", v ) );
	CHECK( !ShParseVector3( "1 2", v ) );
	CHECK( !ShParseVector3( "1,2,3", v ) );
	CHECK( !ShParseVector3( "1 2 3 4", v ) );
}

void TestDefaults(){
	const ShLight point = Light( ShLightType::Point, {} );
	CHECK( point.type == ShLightType::Point );
	CHECK_NEAR( point.intensity, 50000, 0 );
	CHECK_NEAR( point.radiantIntensity, 50000 / ( 4 * c_pi ), 1e-3 );
	CHECK_NEAR( point.range, 256, 0 );
	CHECK_VEC_NEAR( point.colour, 1, 1, 1, 0 );
	CHECK( !point.castsShadows );

	const ShLight spot = Light( ShLightType::Spot, {} );
	CHECK_NEAR( spot.intensity, 50000, 0 );
	CHECK_NEAR( spot.range, 256, 0 );
	CHECK_NEAR( spot.cosOuter, std::cos( Deg( 45 ) ), 1e-6 );
	CHECK_NEAR( spot.cosInner, std::cos( Deg( 30 ) ), 1e-6 );
	CHECK_NEAR( spot.radiantIntensity, 50000 / ( 2 * c_pi * ( 1 - std::cos( Deg( 45 ) ) ) ), 1e-1 );
	CHECK( spot.castsShadows );
	CHECK_VEC_NEAR( spot.direction, 1, 0, 0, 1e-6 ); // angles 0 0 0 is +X

	const ShLight sun = Light( ShLightType::Sun, {} );
	CHECK_NEAR( sun.intensity, 3, 0 );
	CHECK_NEAR( sun.range, 0, 0 ); // unbounded
	CHECK( sun.castsShadows );
	CHECK_VEC_NEAR( sun.direction, 1, 0, 0, 1e-6 );
}

void TestFallbacks(){
	for ( const char* bad : { "abc", "0", "-5", "" } )
	{
		CHECK_MSG( Light( ShLightType::Point, { { "radius", bad } } ).range == 256, bad );
		CHECK_MSG( Light( ShLightType::Spot, { { "radius", bad } } ).range == 256, bad );
	}
	CHECK_NEAR( Light( ShLightType::Point, { { "radius", "100" } } ).range, 100, 0 );

	for ( const char* bad : { "abc", "0", "-1", "" } )
		CHECK_NEAR( Light( ShLightType::Spot, { { "cone", bad } } ).cosOuter, std::cos( Deg( 45 ) ), 1e-6 );
	for ( const char* bad : { "abc", "-1", "" } )
		CHECK_NEAR( Light( ShLightType::Spot, { { "cone_inner", bad } } ).cosInner, std::cos( Deg( 30 ) ), 1e-6 );
	// 0 is a valid inner cone: the whole cone fades
	CHECK_NEAR( Light( ShLightType::Spot, { { "cone_inner", "0" } } ).cosInner, 1, 1e-6 );

	CHECK_NEAR( Light( ShLightType::Point, { { "intensity", "x" } } ).intensity, 50000, 0 );
	CHECK_NEAR( Light( ShLightType::Sun, { { "intensity", "x" } } ).intensity, 3, 0 );
	CHECK_NEAR( Light( ShLightType::Sun, { { "intensity", "7.5" } } ).intensity, 7.5, 0 );
}

void TestConeClamp(){
	const ShLight wide = Light( ShLightType::Spot, { { "cone", "120" }, { "cone_inner", "100" } } );
	CHECK_NEAR( wide.cosOuter, std::cos( Deg( 89.9 ) ), 1e-6 );
	CHECK_NEAR( wide.cosInner, std::cos( Deg( 89.9 ) ), 1e-6 );
	CHECK( wide.cosOuter > 0 ); // never a hemisphere or more

	// inner wider than outer: inner <= outer
	const ShLight inverted = Light( ShLightType::Spot, { { "cone", "20" }, { "cone_inner", "40" } } );
	CHECK_NEAR( inverted.cosOuter, std::cos( Deg( 20 ) ), 1e-6 );
	CHECK_NEAR( inverted.cosInner, std::cos( Deg( 20 ) ), 1e-6 );
}

void TestAngles(){
	// positive pitch looks down
	const ShLight down = Light( ShLightType::Spot, { { "angles", "45 90 0" } } );
	CHECK_VEC_NEAR( down.direction, 0, std::cos( Deg( 45 ) ), -std::sin( Deg( 45 ) ), 1e-6 );
	const ShLight straightDown = Light( ShLightType::Sun, { { "angles", "90 0 0" } } );
	CHECK_VEC_NEAR( straightDown.direction, 0, 0, -1, 1e-6 );
	// roll is ignored
	CHECK_VEC_NEAR( Light( ShLightType::Sun, { { "angles", "0 0 77" } } ).direction, 1, 0, 0, 1e-6 );
	// default and unparsable are +X
	CHECK_VEC_NEAR( Light( ShLightType::Sun, { { "angles", "garbage" } } ).direction, 1, 0, 0, 1e-6 );
}

void TestAngle(){
	CHECK_VEC_NEAR( Light( ShLightType::Spot, { { "angle", "90" } } ).direction, 0, 1, 0, 1e-6 );
	CHECK_VEC_NEAR( Light( ShLightType::Spot, { { "angle", "180" } } ).direction, -1, 0, 0, 1e-6 );
	CHECK_VEC_NEAR( Light( ShLightType::Spot, { { "angle", "-1" } } ).direction, 0, 0, 1, 1e-6 );  // up
	CHECK_VEC_NEAR( Light( ShLightType::Sun, { { "angle", "-2" } } ).direction, 0, 0, -1, 1e-6 );  // down
	CHECK_VEC_NEAR( Light( ShLightType::Spot, { { "angle", "x" } } ).direction, 1, 0, 0, 1e-6 );
	// angles wins over angle
	CHECK_VEC_NEAR( Light( ShLightType::Spot, { { "angles", "90 0 0" }, { "angle", "-1" } } ).direction, 0, 0, -1, 1e-6 );
}

void TestTarget(){
	const Vector3 origin( 0, 0, 100 );
	const Vector3 target( 100, 0, 0 );
	const ShLight spot = Light( ShLightType::Spot, { { "angles", "0 90 0" } }, origin, &target );
	const double s = 1 / std::sqrt( 2.0 );
	CHECK_VEC_NEAR( spot.direction, s, 0, -s, 1e-6 );
	// a target at the light's own origin falls back to the angles
	const ShLight same = Light( ShLightType::Spot, { { "angles", "0 90 0" } }, origin, &origin );
	CHECK_VEC_NEAR( same.direction, 0, 1, 0, 1e-6 );
	// the sun ignores target
	const ShLight sun = Light( ShLightType::Sun, { { "angles", "0 90 0" } }, origin, &target );
	CHECK_VEC_NEAR( sun.direction, 0, 1, 0, 1e-6 );
}

void TestColour(){
	CHECK_VEC_NEAR( Light( ShLightType::Point, { { "_color", "0.5 0.5 0.5" } } ).colour, 1, 1, 1, 0 );
	CHECK_VEC_NEAR( Light( ShLightType::Point, { { "_color", "0 0 0" } } ).colour, 1, 1, 1, 0 );
	CHECK_VEC_NEAR( Light( ShLightType::Point, { { "_color", "2 1 0" } } ).colour, 1, 0.5, 0, 1e-7 );
	CHECK_VEC_NEAR( Light( ShLightType::Point, { { "_color", "red" } } ).colour, 1, 1, 1, 0 );
	CHECK_VEC_NEAR( Light( ShLightType::Point, { { "_color", " 1 0 0" } } ).colour, 1, 1, 1, 0 ); // stringio rejects a leading space

	// spec scenario "Colour normalised": 0.5 0.5 0.5 and 1 1 1 light identically
	const ShLight half = Light( ShLightType::Point, { { "_color", "0.5 0.5 0.5" } }, Vector3( 0, 0, 64 ) );
	const ShLight full = Light( ShLightType::Point, { { "_color", "1 1 1" } }, Vector3( 0, 0, 64 ) );
	const Vector3 a = ShLightIrradiance( half, Vector3( 10, 0, 0 ), Vector3( 0, 0, 1 ) );
	const Vector3 b = ShLightIrradiance( full, Vector3( 10, 0, 0 ), Vector3( 0, 0, 1 ) );
	CHECK( a[0] == b[0] && a[1] == b[1] && a[2] == b[2] );
}

void TestShadows(){
	CHECK( Light( ShLightType::Spot, {} ).castsShadows );
	CHECK( !Light( ShLightType::Spot, { { "_shadows", "0" } } ).castsShadows );
	CHECK( Light( ShLightType::Spot, { { "_shadows", "1" } } ).castsShadows );
	CHECK( Light( ShLightType::Spot, { { "_shadows", "x" } } ).castsShadows ); // unparsable casts
	CHECK( !Light( ShLightType::Sun, { { "_shadows", "0" } } ).castsShadows );
	CHECK( Light( ShLightType::Sun, { { "_shadows", "1" } } ).castsShadows );
	// point lights never cast
	CHECK( !Light( ShLightType::Point, { { "_shadows", "1" } } ).castsShadows );
	CHECK( !Light( ShLightType::Point, {} ).castsShadows );
}

void TestPointIrradiance(){
	// a 50000 W light 64 units above the floor: E = 50000 / (4 pi 64^2)
	const ShLight light = Light( ShLightType::Point, { { "intensity", "50000" }, { "radius", "256" } }, Vector3( 0, 0, 64 ) );
	const Vector3 up( 0, 0, 1 );
	const double expected = 50000 / ( 4 * c_pi * 64 * 64 );
	const Vector3 e = ShLightIrradiance( light, Vector3( 0, 0, 0 ), up );
	CHECK_NEAR( e[0], expected, expected * 1e-6 );
	CHECK_NEAR( e[2], expected, expected * 1e-6 );
	CHECK_NEAR( expected, 0.971, 1e-3 ); // README: about 1 at 64 units
	// cut off beyond radius, kept at it
	CHECK( ShLightIrradiance( light, Vector3( 300, 0, 0 ), up )[0] == 0 );
	CHECK( ShLightIrradiance( light, Vector3( 0, 0, 64 - 257 ), up )[0] == 0 );
	const Vector3 atRadius = ShLightIrradiance( light, Vector3( 0, 0, 64 - 256 ), up ); // d = 256 exactly
	CHECK_NEAR( atRadius[0], 50000 / ( 4 * c_pi * 256 * 256 ), 1e-7 );
	// facing away: n.L <= 0
	CHECK( ShLightIrradiance( light, Vector3( 0, 0, 0 ), Vector3( 0, 0, -1 ) )[0] == 0 );
}

void TestSpotAndSunIrradiance(){
	// spot pointing down, 100 units above the floor
	const ShLight spot = Light( ShLightType::Spot, { { "angles", "90 0 0" }, { "cone", "45" }, { "cone_inner", "30" }, { "radius", "1000" } }, Vector3( 0, 0, 100 ) );
	const Vector3 up( 0, 0, 1 );
	const double I = 50000 / ( 2 * c_pi * ( 1 - std::cos( Deg( 45 ) ) ) );
	CHECK_NEAR( ShLightIrradiance( spot, Vector3( 0, 0, 0 ), up )[0], I / 1e4, I / 1e4 * 1e-5 );
	// outside the outer cone: dark
	const double far = 100 * std::tan( Deg( 50 ) );
	CHECK( ShLightIrradiance( spot, Vector3( float( far ), 0, 0 ), up )[0] == 0 );
	// halfway between the cones in cosine: smoothstep(0.5) = 0.5
	const double cosMid = 0.5 * ( std::cos( Deg( 45 ) ) + std::cos( Deg( 30 ) ) );
	const double x = 100 * std::tan( std::acos( cosMid ) );
	const double d2 = 100 * 100 + x * x;
	CHECK_NEAR( ShLightIrradiance( spot, Vector3( float( x ), 0, 0 ), up )[0], I * 0.5 * cosMid / d2, I / d2 * 1e-3 );

	// equal cones: a hard edge, no NaN
	const ShLight hard = Light( ShLightType::Spot, { { "angles", "90 0 0" }, { "cone", "30" }, { "cone_inner", "30" }, { "radius", "1000" } }, Vector3( 0, 0, 100 ) );
	const double inside = 100 * std::tan( Deg( 29.9 ) ), outside = 100 * std::tan( Deg( 30.1 ) );
	const Vector3 in = ShLightIrradiance( hard, Vector3( float( inside ), 0, 0 ), up );
	CHECK( std::isfinite( in[0] ) && in[0] > 0 );
	CHECK( ShLightIrradiance( hard, Vector3( float( outside ), 0, 0 ), up )[0] == 0 );

	// spec "No SH ringing on direct light": a sun 60 degrees from the normal gives half its irradiance
	const ShLight sun = Light( ShLightType::Sun, { { "angles", "30 0 0" }, { "intensity", "4" } } );
	CHECK_NEAR( ShLightIrradiance( sun, Vector3( 0, 0, 0 ), up )[0], 2.0, 2.0 * 1e-4 );
	CHECK_NEAR( ShLightIrradiance( sun, Vector3( 1e5f, 0, 0 ), up )[0], 2.0, 2.0 * 1e-4 ); // no falloff
}

// --- 4.3 Casters and materials ----------------------------------------------

void TestCasterRule(){
	CHECK( ShShaderCasts( ShSurfaceParms() ) ); // an ordinary wall
	const auto without = []( bool ShSurfaceParms::*flag ){
		ShSurfaceParms parms;
		parms.*flag = true;
		return ShShaderCasts( parms );
	};
	CHECK( !without( &ShSurfaceParms::skyParms ) );
	CHECK( !without( &ShSurfaceParms::sky ) );  // sky without skyparms
	CHECK( !without( &ShSurfaceParms::fog ) );
	CHECK( !without( &ShSurfaceParms::water ) );
	CHECK( !without( &ShSurfaceParms::slime ) );
	CHECK( !without( &ShSurfaceParms::lava ) );
	CHECK( !without( &ShSurfaceParms::playerClip ) );
	CHECK( !without( &ShSurfaceParms::botClip ) );
	CHECK( !without( &ShSurfaceParms::areaPortal ) );
	CHECK( !without( &ShSurfaceParms::noShadows ) );
	CHECK( !without( &ShSurfaceParms::trigger ) );
	CHECK( !without( &ShSurfaceParms::hint ) );
	CHECK( !without( &ShSurfaceParms::trans ) ); // glass does not cast
	CHECK( without( &ShSurfaceParms::alphaShadow ) );
	CHECK( without( &ShSurfaceParms::nonsolid ) ); // non-solid but drawn still casts

	ShSurfaceParms fence;
	fence.trans = fence.alphaShadow = true;
	CHECK( ShShaderCasts( fence ) );

	ShSurfaceParms caulk;
	caulk.nodraw = true;
	CHECK( ShShaderCasts( caulk ) ); // solid nodraw casts
	caulk.nonsolid = true;
	CHECK( !ShShaderCasts( caulk ) ); // non-solid nodraw isn't there
}

void TestBlendFuncParsing(){
	CHECK( Stage( "a", { "add" } ).adds() );
	CHECK( Stage( "a", { "GL_ONE", "GL_ONE" } ).adds() );
	CHECK( Stage( "a", { "gl_src_alpha", "gl_one" } ).adds() );
	CHECK( !Stage( "a", { "GL_DST_COLOR", "GL_ONE" } ).adds() );
	CHECK( Stage( "a", { "filter" } ).multiplies() );
	CHECK( Stage( "a", { "GL_DST_COLOR", "GL_ZERO" } ).multiplies() );
	CHECK( Stage( "a", { "GL_ZERO", "GL_SRC_COLOR" } ).multiplies() );
	CHECK( Stage( "a", { "blend" } ).blends() );
	CHECK( Stage( "a", { "GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA" } ).blends() );
	ShStage none;
	CHECK( !ShStageParseBlendFunc( none, { "nonsense" } ) && !none.blend );
	CHECK( !Stage( "a" ).adds() && !Stage( "a" ).multiplies() );
	ShStage alpha;
	ShStageParseAlphaFunc( alpha, "ge128" );
	CHECK( alpha.alphaFunc == ShAlphaFunc::GE128 );
	ShStageParseAlphaFunc( alpha, "GT1" ); // unknown: unchanged
	CHECK( alpha.alphaFunc == ShAlphaFunc::GE128 );
}

/// one stage from lines of words, the first word of each the keyword
ShStage ParsedStage( const std::vector<std::vector<std::string>>& lines ){
	ShStage stage;
	for ( const auto& line : lines )
		ShStageParseLine( stage, line[0].c_str(), std::vector<std::string>( line.begin() + 1, line.end() ) );
	return stage;
}

void TestStageParseLine(){
	// spec "Glow stage": map, blendFunc add, rgbGen const ( 1 0.4 0.3 )
	const ShStage glow = ParsedStage( { { "map", "textures/town/neon_e" }, { "blendFunc", "add" }, { "rgbGen", "const", "(", "1", "0.4", "0.3", ")" } } );
	CHECK( glow.map == "textures/town/neon_e" && glow.adds() && !glow.rend2 );
	CHECK( glow.constColour );
	CHECK_VEC_NEAR( glow.colour, 1, 0.4, 0.3, 1e-7 );
	// fewer than three numbers: not a const colour; other rgbGen forms are ignored
	CHECK( !ParsedStage( { { "rgbGen", "const", "(", "1", "0.4", ")" } } ).constColour );
	CHECK( !ParsedStage( { { "rgbGen", "wave", "sin", "0", "1", "0", "1" } } ).constColour );
	// clampMap, and animMap's first frame
	CHECK( ParsedStage( { { "clampMap", "textures/town/a" } } ).map == "textures/town/a" );
	CHECK( ParsedStage( { { "animMap", "10", "textures/town/f1", "textures/town/f2" } } ).map == "textures/town/f1" );
	CHECK( ParsedStage( { { "animMap", "10" } } ).map.empty() );
	// tcGen / texGen environment, and a later tcGen clears it
	CHECK( ParsedStage( { { "tcGen", "environment" } } ).tcGenEnvironment );
	CHECK( ParsedStage( { { "texGen", "ENVIRONMENT" } } ).tcGenEnvironment );
	CHECK( !ParsedStage( { { "tcGen", "environment" }, { "tcGen", "base" } } ).tcGenEnvironment );
	// alphaFunc
	CHECK( ParsedStage( { { "alphaFunc", "LT128" } } ).alphaFunc == ShAlphaFunc::LT128 );
	CHECK( ParsedStage( { { "alphaFunc" } } ).alphaFunc == ShAlphaFunc::None );
	// spec "Stage keywords the editor does not use": tcMod, rgbGen wave change nothing
	const ShStage plain = ParsedStage( { { "map", "textures/town/brick_c" }, { "tcMod", "scroll", "0.1", "0" }, { "rgbGen", "wave", "sin", "0", "1", "0", "1" } } );
	CHECK( plain.map == "textures/town/brick_c" && !plain.blend && !plain.constColour && !plain.tcGenEnvironment && !plain.rend2 );
	// spec "rend2 normal-map stage": `stage normalMap` marks the stage, which then takes no part
	const ShStage normal = ParsedStage( { { "stage", "normalMap" }, { "map", "textures/town/brick_nrm" } } );
	CHECK( normal.rend2 && !normal.hasMap() );
	CHECK( ShStageIsRend2Keyword( "SPECULARMAP" ) && ShStageIsRend2Keyword( "parallaxDepth" ) );
	CHECK( !ShStageIsRend2Keyword( "map" ) && !ShStageIsRend2Keyword( nullptr ) );
	// a rend2 keyword line is not parsed further: "roughness" isn't a blendFunc
	const ShMaterialRules r = ShDeriveMaterial( Shader( { normal, ParsedStage( { { "map", "textures/town/brick_c" } } ),
	                                                     ParsedStage( { { "map", "$lightmap" }, { "blendFunc", "filter" } } ) } ) );
	CHECK( r.baseStage == 1 && r.baseImage == "textures/town/brick_c" );
}

void TestBaseColour(){
	// lightmap first, then the texture multiplied onto it
	{
		const ShMaterialRules r = ShDeriveMaterial( Shader( { Stage( "$lightmap" ), Stage( "textures/town/brick_c", { "GL_DST_COLOR", "GL_ZERO" } ) } ) );
		CHECK( r.baseStage == 1 && r.baseImage == "textures/town/brick_c" );
		CHECK( r.casts && !r.additive && !r.emits() && !r.alphaTested() );
	}
	// texture first, then the lightmap with filter
	{
		const ShMaterialRules r = ShDeriveMaterial( Shader( { Stage( "textures/town/brick_c" ), Stage( "$lightmap", { "filter" } ) } ) );
		CHECK( r.baseStage == 0 && r.baseImage == "textures/town/brick_c" );
	}
	// both pairings: the earlier one
	{
		const ShMaterialRules r = ShDeriveMaterial( Shader( { Stage( "first" ), Stage( "$lightmap", { "filter" } ), Stage( "second", { "filter" } ) } ) );
		CHECK( r.baseImage == "first" );
	}
	// a lightmap with no multiplying pair: the first non-lightmap map
	{
		const ShMaterialRules r = ShDeriveMaterial( Shader( { Stage( "$lightmap" ), Stage( "detail", { "blend" } ), Stage( "glow", { "add" } ) } ) );
		CHECK( r.baseStage == 1 && r.baseImage == "detail" );
	}
	// $whiteimage base
	{
		const ShMaterialRules r = ShDeriveMaterial( Shader( { Stage( "$whiteimage" ), Stage( "$lightmap", { "filter" } ) } ) );
		CHECK( r.baseImage == "_white" );
	}
	// a bare texture is its own base colour; a scripted shader without stages has none
	{
		ShShaderDesc bare;
		bare.name = "textures/town/plain";
		bare.scripted = false;
		const ShMaterialRules r = ShDeriveMaterial( bare );
		CHECK( r.baseImage == "textures/town/plain" && r.casts );
		ShShaderDesc empty;
		empty.name = "textures/common/caulk";
		empty.parms.nodraw = true;
		const ShMaterialRules c = ShDeriveMaterial( empty );
		CHECK( c.baseStage == -1 && c.baseImage.empty() && c.casts && !c.additive );
	}
	// a rend2 stage takes no part
	{
		ShStage normal = Stage( "textures/town/brick_nrm" );
		normal.rend2 = true;
		const ShMaterialRules r = ShDeriveMaterial( Shader( { normal, Stage( "textures/town/brick_c" ), Stage( "$lightmap", { "filter" } ) } ) );
		CHECK( r.baseImage == "textures/town/brick_c" );
	}
}

void TestAlphaTestedFence(){
	ShStage fence = Stage( "textures/town/fence" );
	fence.alphaFunc = ShAlphaFunc::GE128;
	ShShaderDesc shader = Shader( { fence, Stage( "$lightmap", { "filter" } ) } );
	shader.parms.trans = shader.parms.alphaShadow = true;
	const ShMaterialRules r = ShDeriveMaterial( shader );
	CHECK( r.casts && r.alphaTested() && r.alphaFunc == ShAlphaFunc::GE128 );
	CHECK( r.baseImage == "textures/town/fence" && !r.additive );

	// an alphaFunc on a stage that isn't the base doesn't make the shader alpha-tested
	ShStage glow = Stage( "glow", { "add" } );
	glow.alphaFunc = ShAlphaFunc::GT0;
	CHECK( !ShDeriveMaterial( Shader( { Stage( "base" ), Stage( "$lightmap", { "filter" } ), glow } ) ).alphaTested() );

	// a blended surface without trans casts solid
	const ShMaterialRules pane = ShDeriveMaterial( Shader( { Stage( "textures/town/pane", { "blend" } ) } ) );
	CHECK( pane.casts && !pane.alphaTested() && !pane.additive );
}

void TestEmissive(){
	// spec "Glow stage": the last stage, additive, with rgbGen const
	ShStage glow = Stage( "textures/town/neon_e", { "add" } );
	glow.constColour = true;
	glow.colour = Vector3( 1, 0.4f, 0.3f );
	ShShaderDesc sign = Shader( { Stage( "textures/town/neon_c" ), Stage( "$lightmap", { "filter" } ), glow } );
	sign.emissiveStrength = 4;
	const ShMaterialRules r = ShDeriveMaterial( sign );
	CHECK( r.emissiveStage == 2 && r.emissiveImage == "textures/town/neon_e" );
	CHECK_VEC_NEAR( r.emissiveColour, 1, 0.4, 0.3, 1e-7 );
	CHECK_NEAR( r.emissiveStrength, 4, 0 );
	CHECK( r.emits() );
	// an opaque base with an additive glow is not additive: it occludes
	CHECK( !r.additive && r.casts );

	// without rgbGen const the colour is white; strength 0 doesn't emit
	ShShaderDesc dark = Shader( { Stage( "base" ), Stage( "$lightmap", { "filter" } ), Stage( "glow", { "GL_ONE", "GL_ONE" } ) } );
	dark.emissiveStrength = 0;
	const ShMaterialRules d = ShDeriveMaterial( dark );
	CHECK_VEC_NEAR( d.emissiveColour, 1, 1, 1, 0 );
	CHECK( d.emissiveStage == 2 && !d.emits() );

	// environment-mapped and white-image additive stages are skipped
	ShStage env = Stage( "textures/town/chrome_env", { "add" } );
	env.tcGenEnvironment = true;
	const ShMaterialRules e = ShDeriveMaterial( Shader( { Stage( "base" ), Stage( "$lightmap", { "filter" } ), env, Stage( "$whiteimage", { "add" } ), Stage( "real_glow", { "GL_SRC_ALPHA", "GL_ONE" } ) } ) );
	CHECK( e.emissiveStage == 4 && e.emissiveImage == "real_glow" );
	const ShMaterialRules onlyEnv = ShDeriveMaterial( Shader( { Stage( "base" ), Stage( "$lightmap", { "filter" } ), env } ) );
	CHECK( onlyEnv.emissiveStage == -1 && !onlyEnv.emits() );
	CHECK_VEC_NEAR( onlyEnv.emissiveColour, 0, 0, 0, 0 );

	// every stage additive: emits and doesn't occlude
	const ShMaterialRules flare = ShDeriveMaterial( Shader( { Stage( "textures/town/flare", { "add" } ), Stage( "textures/town/flare2", { "GL_ONE", "GL_ONE" } ) } ) );
	CHECK( flare.additive && flare.emits() && flare.emissiveStage == 0 );
	// the additive lightmap pass alone isn't emission, though the shader is additive
	const ShMaterialRules lm = ShDeriveMaterial( Shader( { Stage( "$lightmap", { "add" } ) } ) );
	CHECK( lm.additive && !lm.emits() );
}

// --- 4.4 SH, encoding, grid, alpha, UVs -------------------------------------

void TestShBasis(){
	// the basis matches sh-baker's order and constants
	const std::array<float, 9> y = ShBasis( Vector3( 0, 0, 1 ) );
	CHECK_NEAR( y[0], 0.282095, 1e-7 );
	CHECK_NEAR( y[2], 0.488603, 1e-7 );
	CHECK_NEAR( y[6], 0.630784, 1e-6 );
	CHECK( y[1] == 0 && y[3] == 0 && y[4] == 0 && y[5] == 0 && y[7] == 0 );
	const std::array<float, 9> yx = ShBasis( Vector3( 1, 0, 0 ) );
	CHECK_NEAR( yx[3], 0.488603, 1e-7 );
	CHECK_NEAR( yx[8], 0.546274, 1e-7 );
}

void TestEncoding(){
	CHECK_EQ_INT( ShLightmapByte( float( c_pi ) ), 37 );
	CHECK_EQ_INT( ShLightmapByte( float( c_pi / 2 ) ), 29 );
	CHECK_EQ_INT( ShLightmapByte( float( 11.2 * c_pi ) ), 64 );
	CHECK_EQ_INT( ShLightmapByte( float( 100 * c_pi ) ), 64 );
	CHECK_EQ_INT( ShLightmapByte( std::numeric_limits<float>::infinity() ), 64 );
	CHECK_EQ_INT( ShLightmapByte( 0 ), 0 );
	CHECK_EQ_INT( ShLightmapByte( -1 ), 0 );
	CHECK_EQ_INT( ShLightmapByte( std::numeric_limits<float>::quiet_NaN() ), 0 );
	const ShBytes3 b = ShLightmapBytes( Vector3( float( c_pi ), float( c_pi / 2 ), float( 20 * c_pi ) ) );
	CHECK( b[0] == 37 && b[1] == 29 && b[2] == 64 );
	// monotonic and never above 64
	int previous = 0;
	bool monotonic = true, bounded = true;
	for ( int i = 0; i <= 20000; ++i )
	{
		const int byte = ShLightmapByte( float( i * 0.002 ) );
		monotonic = monotonic && byte >= previous;
		bounded = bounded && byte <= 64;
		previous = byte;
	}
	CHECK( monotonic && bounded );
	// the curve reaches 1 at the white point; the sRGB encode is exact
	CHECK_NEAR( ShHable( 11.2 ) / ShHable( 11.2 ), 1, 0 );
	CHECK_NEAR( ShSrgbEncode( 0.5 ), 0.735357, 1e-6 );
	CHECK_NEAR( ShSrgbDecode( ShSrgbEncode( 0.2 ) ), 0.2, 1e-12 );
}

void TestGridSingleSun(){
	// one sun, towards w, irradiance E-perp per channel, alone in the full-sphere SH
	const Vector3 w = vector3_normalised( Vector3( 1, 2, 3 ) );
	const Vector3 sunE( 2.0f, 1.6f, 1.2f );
	const ShGridFit sun = ShFitGridPoint( DeltaSH( w, sunE ) );
	CHECK_VEC_NEAR( sun.direction, w[0], w[1], w[2], 1e-4 );
	for ( int c = 0; c < 3; ++c )
	{
		CHECK_NEAR( sun.directed[c], sunE[c], sunE[c] * 1e-4 );  // D = E-perp
		CHECK_NEAR( sun.ambient[c], 0, sunE[c] * 1e-5 );          // the sun's own L0 share is taken out
	}
	// the sun plus a constant bounce: the bounce is the ambient, the sun is still the directed light
	const float bounce = 0.05f;
	const ShGridFit fit = ShFitGridPoint( DeltaSH( w, sunE ) + ConstantSH( bounce ) );
	CHECK_VEC_NEAR( fit.direction, w[0], w[1], w[2], 1e-4 );
	for ( int c = 0; c < 3; ++c )
	{
		CHECK_NEAR( fit.directed[c], sunE[c], sunE[c] * 1e-4 );
		CHECK_NEAR( fit.ambient[c], c_pi * bounce, 1e-5 );
	}
	// realised in opengl1: facing the sun gives enc(A + D), within a byte step
	for ( int c = 0; c < 3; ++c )
	{
		const float lit = ShGridRealisedLit( fit.ambientBytes[c], fit.directedBytes[c] );
		CHECK_NEAR( lit, 4 * ShLightmapByte( fit.ambient[c] + fit.directed[c] ), 2.5 );
	}
	// two suns from different directions: D along the weighted L1 never exceeds what the SH holds, ambient >= 0
	const ShGridFit two = ShFitGridPoint( DeltaSH( Vector3( 1, 0, 0 ), Vector3( 1, 1, 1 ) ) + DeltaSH( Vector3( 0, 1, 0 ), Vector3( 1, 1, 1 ) ) );
	CHECK_VEC_NEAR( two.direction, std::sqrt( 0.5 ), std::sqrt( 0.5 ), 0, 1e-6 );
	CHECK_NEAR( two.directed[0], std::sqrt( 2.0 ), 1e-5 );
	CHECK_NEAR( two.ambient[0], c_pi * c_shBasis0 * c_shBasis0 * ( 2 - std::sqrt( 2.0 ) ), 1e-5 );
}

void TestGridConstantSky(){
	// constant radiance L over the whole sphere: ambient pi L only, no directed, direction +Z
	const float L = 0.7f;
	const ShGridFit sky = ShFitGridPoint( ConstantSH( L ) );
	CHECK_VEC_NEAR( sky.direction, 0, 0, 1, 0 );
	CHECK_VEC_NEAR( sky.directed, 0, 0, 0, 0 );
	CHECK_VEC_NEAR( sky.ambient, c_pi * L, c_pi * L, c_pi * L, c_pi * L * 1e-5 );
	for ( int c = 0; c < 3; ++c )
		CHECK_EQ_INT( sky.directedBytes[c], 0 );
	// an L1 too small to normalise gives +Z; nothing at all gives +Z and black
	ShCoeffs9 tiny = ConstantSH( L );
	tiny[3] = Vector3( 1e-20f );
	CHECK_VEC_NEAR( ShFitGridPoint( tiny ).direction, 0, 0, 1, 0 );
	const ShGridFit dark = ShFitGridPoint( ZeroSH() );
	CHECK_VEC_NEAR( dark.direction, 0, 0, 1, 0 );
	CHECK_VEC_NEAR( dark.ambient, 0, 0, 0, 0 );
	// a non-finite SH doesn't give a non-finite fit
	ShCoeffs9 bad = ZeroSH();
	bad[3] = Vector3( std::numeric_limits<float>::quiet_NaN() );
	const ShGridFit badFit = ShFitGridPoint( bad );
	CHECK_VEC_NEAR( badFit.direction, 0, 0, 1, 0 );
	CHECK( badFit.ambientBytes[0] == 1 && badFit.directedBytes[0] == 0 ); // the ambient-1 rule
}

void TestNeutralBytes(){
	// -shnofull (design decisions 1, 5 and 6)
	const ShBytes3 lm = ShNeutralLightmapBytes();
	CHECK( lm[0] == 64 && lm[1] == 64 && lm[2] == 64 );
	const ShGridFit neutral = ShGridNeutralFit();
	CHECK( neutral.ambientBytes[0] == 63 && neutral.ambientBytes[1] == 63 && neutral.ambientBytes[2] == 63 );
	CHECK( neutral.directedBytes[0] == 0 && neutral.directedBytes[1] == 0 && neutral.directedBytes[2] == 0 );
	CHECK_VEC_NEAR( neutral.direction, 0, 0, 1, 0 );
	// the ambient is the fit's for an ambient target of byte 64; the fit would also put the gap between the
	// ceiling and 4 * 64 into directed along +Z (round((256 - 183.2) / 4) = 18), which the neutral leaves out
	const ShGridFit bright = ShFitGridPoint( ConstantSH( 1e6f ) );
	CHECK_EQ_INT( ShLightmapByte( bright.ambient[0] ), 64 );
	for ( int c = 0; c < 3; ++c )
	{
		CHECK_EQ_INT( bright.ambientBytes[c], neutral.ambientBytes[c] );
		CHECK_EQ_INT( bright.directedBytes[c], 18 );
	}
	CHECK_VEC_NEAR( bright.direction, neutral.direction[0], neutral.direction[1], neutral.direction[2], 0 );
	CHECK_NEAR( ShGridRealisedAmbient( neutral.ambientBytes[0] ), 183.2, 1e-3 ); // the engine's ambient ceiling
}

void TestGridEngineModel(){
	// sub-floor ambient: target(A) below the engine floor of 32, so the ambient byte is 0, raised to 1 by the
	// ambient-1 rule, and the lit face is measured from what byte 1 realises (34.4), not from target(A)
	{
		const float A = IrradianceForByte( 5 );     // target 20 < 32
		const float AD = IrradianceForByte( 30 );   // target 120
		CHECK_EQ_INT( ShLightmapByte( A ), 5 );
		CHECK_EQ_INT( ShLightmapByte( AD ), 30 );
		const ShCoeffs9 main = ConstantSH( float( A / c_pi ) );
		const ShCoeffs9 punctual = DeltaSH( Vector3( 0, 0, 1 ), Vector3( AD - A ) );
		const ShGridFit fit = ShFitGridPoint( main + punctual );
		CHECK_EQ_INT( fit.directedBytes[0], 21 );   // round((120 - 34.4) / 4), not (120 - 20) / 4 = 25
		CHECK_EQ_INT( fit.ambientBytes[0], 1 );     // 0, then the ambient-1 rule
		CHECK_NEAR( ShGridRealisedAmbient( 0 ), 32, 0 );
		CHECK_NEAR( ShGridRealisedLit( fit.ambientBytes[0], fit.directedBytes[0] ), 120, 2 ); // 118.4
	}
	// ambient ceiling and the per-channel clamp to 63
	{
		const float A = IrradianceForByte( 60 );    // target 240: (240 - 32) / 0.6 > 255
		const float AD = IrradianceForByte( 64 );
		const ShGridFit fit = ShFitGridPoint( ConstantSH( float( A / c_pi ) ) + DeltaSH( Vector3( 0, 0, 1 ), Vector3( AD - A ) ) );
		CHECK_EQ_INT( fit.ambientBytes[0], 63 );    // round(255 / 4) = 64, clamped
		const float realised = ShGridRealisedAmbient( 63 );
		CHECK_NEAR( realised, 4 * 63 * 0.6 + 32, 1e-4 ); // 183.2: the reachable maximum
		CHECK_EQ_INT( fit.directedBytes[0], std::lround( ( 4 * 64 - realised ) / 4 ) );
		// huge light everywhere still stays within 63
		const ShGridFit huge = ShFitGridPoint( ConstantSH( 1e6f ) + DeltaSH( Vector3( 0, 0, 1 ), Vector3( 1e6f ) ) );
		for ( int c = 0; c < 3; ++c )
			CHECK( huge.ambientBytes[c] <= 63 && huge.directedBytes[c] <= 63 );
	}
	// midtones: away from d gives about enc(A), facing d about enc(A + D)
	{
		const float A = IrradianceForByte( 50 ), AD = IrradianceForByte( 56 );
		const ShGridFit fit = ShFitGridPoint( ConstantSH( float( A / c_pi ) ) + DeltaSH( Vector3( 1, 0, 0 ), Vector3( AD - A ) ) );
		CHECK_NEAR( ShGridRealisedAmbient( fit.ambientBytes[1] ), 183.2, 1e-3 ); // 4 * 50 = 200 is above the ceiling
		const float A2 = IrradianceForByte( 30 ), AD2 = IrradianceForByte( 45 );
		const ShGridFit mid = ShFitGridPoint( ConstantSH( float( A2 / c_pi ) ) + DeltaSH( Vector3( 1, 0, 0 ), Vector3( AD2 - A2 ) ) );
		CHECK_NEAR( ShGridRealisedAmbient( mid.ambientBytes[1] ), 4 * 30, 1.3 );
		CHECK_NEAR( ShGridRealisedLit( mid.ambientBytes[1], mid.directedBytes[1] ), 4 * 45, 2.5 );
	}
	// the ambient-1 rule: an all-zero ambient triple becomes 1 1 1, per triple, with or without directed light,
	// since renderergl1 skips a cell whose ambient sums to zero (task 6.3)
	{
		const ShGridFit redOnly = ShFitGridPoint( DeltaSH( Vector3( 0, 0, 1 ), Vector3( 1, 0, 0 ) ) );
		CHECK( redOnly.directedBytes[0] > 0 && redOnly.directedBytes[1] == 0 );
		CHECK( redOnly.ambientBytes[0] == 1 && redOnly.ambientBytes[1] == 1 && redOnly.ambientBytes[2] == 1 );
		const ShGridFit dark = ShFitGridPoint( ZeroSH() );
		CHECK( dark.ambientBytes[0] == 1 && dark.ambientBytes[1] == 1 && dark.ambientBytes[2] == 1 );
		CHECK( dark.directedBytes[0] == 0 && dark.directedBytes[1] == 0 && dark.directedBytes[2] == 0 );
		// a dim point whose target lies under the floor: ambient 0 and directed 0 before the rule
		const ShGridFit dim = ShFitGridPoint( ConstantSH( float( IrradianceForByte( 3 ) / c_pi ) ) );
		CHECK( dim.ambientBytes[0] == 1 && dim.ambientBytes[1] == 1 && dim.ambientBytes[2] == 1 );
		CHECK( dim.directedBytes[0] == 0 && dim.directedBytes[1] == 0 && dim.directedBytes[2] == 0 );
		CHECK_NEAR( ShGridRealisedAmbient( 1 ), 34.4, 1e-4 ); // not skipped: 4 * 0.6 + 32
	}
	// the engine constants, from renderergl1 at default cvars in a window (task 6.3)
	{
		CHECK_EQ_INT( c_shGridShift, 2 );                // r_mapOverBrightBits 2 - tr.overbrightBits 0
		CHECK( c_shGridAmbientScale == 0.6f );          // r_ambientScale
		CHECK_NEAR( c_shGridDirectedScale, 1.0, 0 );     // r_directedScale
		CHECK_NEAR( c_shGridFloor, 32, 0 );              // tr.identityLight 1 * 32
		CHECK_EQ_INT( c_shGridMaxByte, 63 );             // 64 << 2 = 256 triggers the rescale
		CHECK_NEAR( ShGridRealisedLit( 63, 63 ), 255, 0 ); // RB_CalcDiffuseColor clamps each channel at 255
	}
}

void TestAlphaRewrite(){
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::GT0, 0 ), 0 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::GT0, 1 ), 255 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::LT128, 127 ), 255 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::LT128, 128 ), 0 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::GE128, 127 ), 0 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::GE128, 128 ), 255 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::None, 0 ), 255 );
	CHECK_EQ_INT( ShAlphaRewrite( ShAlphaFunc::None, 77 ), 255 );
	std::uint8_t pixels[8] = { 10, 20, 30, 127, 40, 50, 60, 128 };
	ShAlphaRewriteRGBA( ShAlphaFunc::GE128, pixels, 2 );
	CHECK( pixels[0] == 10 && pixels[1] == 20 && pixels[2] == 30 && pixels[3] == 0 );
	CHECK( pixels[4] == 40 && pixels[5] == 50 && pixels[6] == 60 && pixels[7] == 255 );
}

void TestSingleTileUV(){
	const std::vector<std::uint32_t> quad = { 0, 1, 2, 0, 2, 3 };
	// a quad covering the texture exactly
	{
		const ShUVTile t = ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 1, 1 ), Vector2( 0, 1 ) }, quad );
		CHECK( t.single && t.u == 0 && t.v == 0 );
	}
	// a quad in another tile, with float noise at the edges
	{
		const ShUVTile t = ShSingleTileUV( { Vector2( 2.00001f, -0.99999f ), Vector2( 3, -1 ), Vector2( 3, 0.00001f ), Vector2( 2, 0 ) }, quad );
		CHECK( t.single && t.u == 2 && t.v == -1 );
	}
	// tiled: spans two tiles
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 2, 0 ), Vector2( 2, 1 ), Vector2( 0, 1 ) }, quad ).single );
	CHECK( !ShSingleTileUV( { Vector2( 0.5f, 0 ), Vector2( 1.5f, 0 ), Vector2( 1.5f, 1 ), Vector2( 0.5f, 1 ) }, quad ).single );
	// overlapping: two quads on the same UVs
	{
		const std::vector<Vector2> uvs = { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 1, 1 ), Vector2( 0, 1 ),
		                                   Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 1, 1 ), Vector2( 0, 1 ) };
		CHECK( !ShSingleTileUV( uvs, { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 } ).single );
	}
	// overlapping partly, and mirrored onto each other along a shared edge
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 0.6f, 0 ), Vector2( 0, 0.6f ), Vector2( 0.2f, 0.2f ), Vector2( 0.8f, 0.2f ), Vector2( 0.2f, 0.8f ) }, { 0, 1, 2, 3, 4, 5 } ).single );
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 0, 1 ), Vector2( 0.5f, 0.5f ) }, { 0, 1, 2, 0, 1, 3 } ).single );
	// disjoint triangles whose bounding boxes overlap are fine
	CHECK( ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 0.6f, 0 ), Vector2( 0, 0.6f ), Vector2( 0.4f, 0.4f ), Vector2( 1, 0.4f ), Vector2( 0.4f, 1 ) }, { 0, 1, 2, 3, 4, 5 } ).single );
	// adjacent triangles sharing an edge or a vertex are fine
	CHECK( ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 0.5f, 0 ), Vector2( 0.5f, 0.5f ), Vector2( 1, 0.5f ), Vector2( 1, 1 ) }, { 0, 1, 2, 2, 3, 4 } ).single );
	// a triangle with no UV area is rejected; so are bad indices and non-finite UVs
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 0.5f, 0 ) }, { 0, 1, 2 } ).single );
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( 0, 1 ) }, { 0, 1, 7 } ).single );
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ), Vector2( 1, 0 ), Vector2( std::numeric_limits<float>::quiet_NaN(), 1 ) }, { 0, 1, 2 } ).single );
	CHECK( !ShSingleTileUV( { Vector2( 0, 0 ) }, {} ).single );
}

} // namespace

// --- Attached emitter lights (task 6.6, spec "Attached emitter lights skipped") ---

/// a key lookup over a key map; the map must outlive it
ShKeyLookup Keys( const std::map<std::string, std::string>& keys ){
	return [&keys]( const char* key ) -> const char* {
		const auto it = keys.find( key );
		return it == keys.end() ? "" : it->second.c_str();
	};
}

void TestAttachedLightSkip(){
	const std::map<std::string, std::string> none, proxy1 = { { "_proxy", "1" } }, proxy0 = { { "_proxy", "0" } },
	                                         proxy2 = { { "_proxy", "2" } }, proxy1sp = { { "_proxy", "1 " } };
	CHECK( ShIsAreaLightClassname( "light_area" ) );
	CHECK( ShIsAreaLightClassname( "LIGHT_AREA" ) );
	CHECK( !ShIsAreaLightClassname( "light" ) );
	CHECK( !ShIsAreaLightClassname( nullptr ) );
	// a light_area is always left out, whatever its keys
	CHECK( ShIsAttachedEmitterLight( "light_area", Keys( none ) ) );
	CHECK( ShIsAttachedEmitterLight( "Light_Area", Keys( proxy0 ) ) );
	// _proxy on light, light_spot and light_sun, read as q3map2 reads a boolean key: the first character is '1'
	CHECK( ShIsAttachedEmitterLight( "light", Keys( proxy1 ) ) );
	CHECK( ShIsAttachedEmitterLight( "light_spot", Keys( proxy1 ) ) );
	CHECK( ShIsAttachedEmitterLight( "light_sun", Keys( proxy1 ) ) );
	CHECK( ShIsAttachedEmitterLight( "light", Keys( proxy1sp ) ) ); // "1 " starts with '1'
	CHECK( !ShIsAttachedEmitterLight( "light", Keys( proxy2 ) ) );
	CHECK( !ShIsAttachedEmitterLight( "light", Keys( proxy0 ) ) );
	CHECK( !ShIsAttachedEmitterLight( "light", Keys( none ) ) );
	CHECK( !ShIsAttachedEmitterLight( "light_spot", Keys( none ) ) );
	CHECK( !ShIsAttachedEmitterLight( "light_sun", Keys( none ) ) );
	// other classes ignore _proxy
	CHECK( !ShIsAttachedEmitterLight( "info_null", Keys( proxy1 ) ) );
	CHECK( !ShIsAttachedEmitterLight( nullptr, Keys( proxy1 ) ) );
}

void TestAreaRect(){
	// default: a 64 x 64 rectangle facing +X (emissive-area-lights "Default area light")
	const std::map<std::string, std::string> none;
	const ShAreaRect d = ShAreaRectFromKeys( Vector3( 1, 2, 3 ), Keys( none ) );
	CHECK_VEC_NEAR( d.centre, 1, 2, 3, 0 );
	CHECK_VEC_NEAR( d.forward, 1, 0, 0, 1e-6 );
	CHECK_VEC_NEAR( d.right, 0, -1, 0, 1e-6 );
	CHECK_VEC_NEAR( d.up, 0, 0, 1, 1e-6 );
	CHECK_NEAR( d.width, 64, 0 );
	CHECK_NEAR( d.height, 64, 0 );

	// "Roll turns the rectangle": the normal stays -Y, the long side turns from X to Z
	const std::map<std::string, std::string> yaw = { { "width", "128" }, { "height", "16" }, { "angles", "0 270 0" } },
	                                         roll = { { "width", "128" }, { "height", "16" }, { "angles", "0 270 90" } };
	const ShAreaRect a = ShAreaRectFromKeys( Vector3( 0, 0, 0 ), Keys( yaw ) );
	const ShAreaRect b = ShAreaRectFromKeys( Vector3( 0, 0, 0 ), Keys( roll ) );
	CHECK_VEC_NEAR( a.forward, 0, -1, 0, 1e-6 );
	CHECK_VEC_NEAR( a.right, -1, 0, 0, 1e-6 );
	CHECK_VEC_NEAR( b.forward, 0, -1, 0, 1e-6 );
	CHECK_VEC_NEAR( b.right, 0, 0, -1, 1e-6 );
	CHECK_VEC_NEAR( b.up, -1, 0, 0, 1e-6 );
	CHECK_NEAR( a.width, 128, 0 );
	CHECK_NEAR( a.height, 16, 0 );

	// right = forward x up, for a general basis
	const std::map<std::string, std::string> general = { { "angles", "20 35 50" } };
	const ShAreaRect g = ShAreaRectFromKeys( Vector3( 0, 0, 0 ), Keys( general ) );
	const Vector3 fxu = vector3_cross( g.forward, g.up );
	CHECK_VEC_NEAR( fxu, g.right[0], g.right[1], g.right[2], 1e-6 );
	CHECK_VEC_NEAR( g.forward, std::cos( Deg( 20 ) ) * std::cos( Deg( 35 ) ), std::cos( Deg( 20 ) ) * std::sin( Deg( 35 ) ), -std::sin( Deg( 20 ) ), 1e-6 );

	// angle -2 points down; bad sizes fall back
	const std::map<std::string, std::string> down = { { "angle", "-2" }, { "width", "-5" }, { "height", "abc" } };
	const ShAreaRect dn = ShAreaRectFromKeys( Vector3( 0, 0, 0 ), Keys( down ) );
	CHECK_VEC_NEAR( dn.forward, 0, 0, -1, 1e-6 );
	CHECK_NEAR( dn.width, 64, 0 );
	CHECK_NEAR( dn.height, 64, 0 );
}

void TestAreaRectAttached(){
	// a 64 x 64 quad on x = 0 facing +X, centred on the origin
	const std::vector<Vector3> quad = { Vector3( 0, -32, -32 ), Vector3( 0, 32, -32 ), Vector3( 0, 32, 32 ), Vector3( 0, -32, 32 ) };
	const std::vector<std::uint32_t> tris = { 0, 1, 2, 0, 2, 3 };
	const auto rect = []( const Vector3& origin, const std::map<std::string, std::string>& keys ){
		return ShAreaRectFromKeys( origin, Keys( keys ) );
	};
	const Vector3 px( 1, 0, 0 );
	const auto NormalsLike = [px]( const std::vector<Vector3>& positions ){
		return std::vector<Vector3>( positions.size(), px );
	};
	const std::map<std::string, std::string> none, back = { { "angles", "0 180 0" } }, half = { { "angles", "0 0.5 0" } },
	                                         two = { { "angles", "0 2 0" } }, thin = { { "width", "8" }, { "height", "8" } };
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, NormalsLike( quad ), tris ) );
	CHECK( ShAreaRectAttached( rect( Vector3( 0.5, 0, 0 ), none ), quad, NormalsLike( quad ), tris ) );     // within 1 unit
	CHECK( ShAreaRectAttached( rect( Vector3( -1, 0, 0 ), none ), quad, NormalsLike( quad ), tris ) );      // exactly 1 unit
	CHECK( !ShAreaRectAttached( rect( Vector3( 1.5, 0, 0 ), none ), quad, NormalsLike( quad ), tris ) );    // too far off the plane
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), back ), quad, NormalsLike( quad ), tris ) );      // faces the other way
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), half ), quad, NormalsLike( quad ), tris ) );       // 0.5 degrees off
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), two ), quad, NormalsLike( quad ), tris ) );       // 2 degrees off
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 48, 0 ), none ), quad, NormalsLike( quad ), tris ) );      // overlaps 16 x 64
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 64, 0 ), none ), quad, NormalsLike( quad ), tris ) );     // shares an edge only
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 100, 0 ), none ), quad, NormalsLike( quad ), tris ) );    // beside it
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 10, -5 ), thin ), quad, NormalsLike( quad ), tris ) );     // a small rectangle inside
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), { quad[0], quad[1], quad[2] }, { px, px, px }, { 0, 1, 2 } ) ); // one triangle
	// winding doesn't matter (q3map2 winds clockwise), the vertex normals do
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, NormalsLike( quad ), { 0, 2, 1, 0, 3, 2 } ) );
	const std::vector<Vector3> mx( 4, Vector3( -1, 0, 0 ) );
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, mx, tris ) );
	CHECK( ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), back ), quad, mx, { 0, 2, 1, 0, 3, 2 } ) );
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, {}, tris ) );   // no normals, no side

	// a vertex 2 units off the plane: not coplanar
	std::vector<Vector3> bent = quad;
	bent[2][0] = 2;
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), bent, NormalsLike( bent ), tris ) );
	// no area, no normal
	const std::vector<Vector3> line = { Vector3( 0, 0, 0 ), Vector3( 0, 1, 0 ), Vector3( 0, 2, 0 ) };
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), line, NormalsLike( line ), { 0, 1, 2 } ) );
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, NormalsLike( quad ), {} ) );
	// an index out of range is not attached
	CHECK( !ShAreaRectAttached( rect( Vector3( 0, 0, 0 ), none ), quad, NormalsLike( quad ), { 0, 1, 7 } ) );
}

void TestProxyAttached(){
	const std::vector<Vector3> quad = { Vector3( 0, -32, -32 ), Vector3( 0, 32, -32 ), Vector3( 0, 32, 32 ), Vector3( 0, -32, 32 ) };
	CHECK( ShProxyAttached( Vector3( 8, 0, 0 ), quad ) );
	CHECK( ShProxyAttached( Vector3( 64, 0, 0 ), quad ) );       // on the grown bounds
	CHECK( ShProxyAttached( Vector3( -64, 96, -96 ), quad ) );   // a corner of the grown bounds
	CHECK( !ShProxyAttached( Vector3( 64.5, 0, 0 ), quad ) );
	CHECK( !ShProxyAttached( Vector3( 8, 0, -97 ), quad ) );
	CHECK( !ShProxyAttached( Vector3( 0, 0, 0 ), {} ) );
}

int main(){
	const struct
	{
		const char* name;
		void ( *run )();
	} tests[] = {
		{ "Placeholder", TestPlaceholder },
		{ "Parsers", TestParsers },
		{ "Defaults", TestDefaults },
		{ "Fallbacks", TestFallbacks },
		{ "ConeClamp", TestConeClamp },
		{ "Angles", TestAngles },
		{ "Angle", TestAngle },
		{ "Target", TestTarget },
		{ "Colour", TestColour },
		{ "Shadows", TestShadows },
		{ "PointIrradiance", TestPointIrradiance },
		{ "SpotAndSunIrradiance", TestSpotAndSunIrradiance },
		{ "CasterRule", TestCasterRule },
		{ "BlendFuncParsing", TestBlendFuncParsing },
		{ "StageParseLine", TestStageParseLine },
		{ "BaseColour", TestBaseColour },
		{ "AlphaTestedFence", TestAlphaTestedFence },
		{ "Emissive", TestEmissive },
		{ "ShBasis", TestShBasis },
		{ "Encoding", TestEncoding },
		{ "GridSingleSun", TestGridSingleSun },
		{ "GridConstantSky", TestGridConstantSky },
		{ "GridEngineModel", TestGridEngineModel },
		{ "NeutralBytes", TestNeutralBytes },
		{ "AlphaRewrite", TestAlphaRewrite },
		{ "SingleTileUV", TestSingleTileUV },
		{ "AttachedLightSkip", TestAttachedLightSkip },
		{ "AreaRect", TestAreaRect },
		{ "AreaRectAttached", TestAreaRectAttached },
		{ "ProxyAttached", TestProxyAttached },
	};
	for ( const auto& test : tests ) {
		g_test = test.name;
		const int before = g_failures;
		test.run();
		std::printf( "%s %s\n", g_failures == before ? "PASS" : "FAIL", test.name );
	}
	std::printf( "light_sh_model_test: %d checks, %d failed\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
