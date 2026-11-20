/* -------------------------------------------------------------------------------

   light_sh_model: the pure rules of q3map2's -shbake stage; see light_sh_model.h.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "light_sh_model.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>

namespace
{

constexpr double c_pi = 3.14159265358979323846;
constexpr double c_deg2rad = c_pi / 180.0;

bool Empty( const char* s ){
	return s == nullptr || *s == '\0';
}

bool EqualNoCase( const char* a, const char* b ){
	for ( ; *a != '\0' && *b != '\0'; ++a, ++b )
	{
		if ( std::tolower( static_cast<unsigned char>( *a ) ) != std::tolower( static_cast<unsigned char>( *b ) ) ) {
			return false;
		}
	}
	return *a == *b;
}

/// a lookup that never returns nullptr
const char* Key( const ShKeyLookup& key, const char* name ){
	const char* value = key ? key( name ) : nullptr;
	return value == nullptr ? "" : value;
}

float Smoothstep( float edge0, float edge1, float x ){
	const float t = std::clamp( ( x - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

/// a light's "pitch yaw roll" from `angles`, else from `angle` (a yaw; -1 up, -2 down), else 0 0 0;
/// unparsable gives 0 0 0 (or yaw 0). The spec orders `angles` first.
Vector3 AnglesFromKeys( const ShKeyLookup& key ){
	Vector3 angles( 0, 0, 0 );
	const char* anglesKey = Key( key, "angles" );
	const char* angleKey = Key( key, "angle" );
	if ( !Empty( anglesKey ) ) {
		if ( !ShParseVector3( anglesKey, angles ) ) {
			angles = Vector3( 0, 0, 0 );
		}
	}
	else if ( !Empty( angleKey ) ) {
		float angle;
		if ( !ShParseFloat( angleKey, angle ) ) {
			angle = 0;
		}
		if ( angle == -1 ) {
			angles = Vector3( -90, 0, 0 );
		}
		else if ( angle == -2 ) {
			angles = Vector3( 90, 0, 0 );
		}
		else
		{
			angles = Vector3( 0, angle, 0 );
		}
	}
	return angles;
}

/// PBRLight::anglesDirection: "pitch yaw roll", positive pitch looks down
Vector3 AnglesDirection( const Vector3& angles ){
	const double pitch = angles[0] * c_deg2rad;
	const double yaw = angles[1] * c_deg2rad;
	return vector3_normalised( Vector3(
	    static_cast<float>( std::cos( yaw ) * std::cos( pitch ) ),
	    static_cast<float>( std::sin( yaw ) * std::cos( pitch ) ),
	    static_cast<float>( -std::sin( pitch ) ) ) );
}

} // namespace

// --- 1. Light entities ------------------------------------------------------

bool ShParseFloat( const char* string, float& f ){
	if ( Empty( string ) ) {
		return false;
	}
	char* end;
	f = static_cast<float>( std::strtod( string, &end ) );
	return *end == '\0';
}

bool ShParseInt( const char* string, int& i ){
	if ( Empty( string ) ) {
		return false;
	}
	char* end;
	i = static_cast<int>( std::strtol( string, &end, 10 ) );
	return *end == '\0';
}

bool ShParseVector3( const char* string, Vector3& v ){
	if ( Empty( string ) || *string == ' ' ) {
		return false;
	}
	char* end;
	for ( int i = 0; i < 3; ++i )
	{
		v[i] = static_cast<float>( std::strtod( string, &end ) );
		string = end;
		if ( i < 2 && *string++ != ' ' ) {
			return false;
		}
	}
	return *string == '\0';
}

std::optional<ShLightType> ShLightTypeFromClassname( const char* classname ){
	if ( classname == nullptr ) {
		return {};
	}
	if ( EqualNoCase( classname, "light" ) ) {
		return ShLightType::Point;
	}
	if ( EqualNoCase( classname, "light_spot" ) ) {
		return ShLightType::Spot;
	}
	if ( EqualNoCase( classname, "light_sun" ) ) {
		return ShLightType::Sun;
	}
	return {};
}

ShLight ShLightFromKeys( ShLightType type, const Vector3& origin, const ShKeyLookup& key, const Vector3* targetOrigin ){
	ShLight light;
	light.type = type;
	light.origin = origin;

	// intensity (PBRLight::intensityChanged, defaultIntensity)
	if ( !ShParseFloat( Key( key, "intensity" ), light.intensity ) ) {
		light.intensity = type == ShLightType::Sun ? 3.0f : 50000.0f;
	}

	// radius (PBRLight::radiusChanged)
	float radius;
	if ( !ShParseFloat( Key( key, "radius" ), radius ) || radius <= 0 ) {
		radius = 256;
	}

	// cones (PBRLight::coneChanged, coneInnerChanged, cosOuter, cosInner)
	float cone, coneInner;
	if ( !ShParseFloat( Key( key, "cone" ), cone ) || cone <= 0 ) {
		cone = 45;
	}
	if ( !ShParseFloat( Key( key, "cone_inner" ), coneInner ) || coneInner < 0 ) {
		coneInner = 30;
	}

	// colour (colour.h read_colour_normalized)
	Vector3 colour;
	if ( !ShParseVector3( Key( key, "_color" ), colour ) ) {
		colour = Vector3( 1, 1, 1 );
	}
	else
	{
		const float max = std::max( { colour[0], colour[1], colour[2] } );
		if ( max == 0 ) {
			colour = Vector3( 1, 1, 1 );
		}
		else
		{
			colour = Vector3( colour[0] / max, colour[1] / max, colour[2] / max );
		}
	}
	light.colour = colour;

	// direction (anglesChanged, angleChanged, pbrUpdateDirection); spec order: target, angles, angle
	light.direction = AnglesDirection( AnglesFromKeys( key ) );
	if ( targetOrigin != nullptr && type == ShLightType::Spot ) {
		const Vector3 dir = *targetOrigin - origin;
		if ( vector3_length_squared( dir ) > 0 ) {
			light.direction = vector3_normalised( dir );
		}
	}

	// shadows (shadowsChanged, castsShadows): only spot and sun read the key
	if ( type != ShLightType::Point ) {
		int shadows;
		light.castsShadows = !ShParseInt( Key( key, "_shadows" ), shadows ) || shadows != 0;
	}
	else
	{
		light.castsShadows = false;
	}

	switch ( type )
	{
	case ShLightType::Point:
		light.range = radius;
		light.radiantIntensity = static_cast<float>( light.intensity / ( 4.0 * c_pi ) );
		break;
	case ShLightType::Spot:
		light.range = radius;
		light.cosOuter = static_cast<float>( std::cos( std::min( cone, 89.9f ) * c_deg2rad ) );
		light.cosInner = static_cast<float>( std::cos( std::min( std::min( coneInner, cone ), 89.9f ) * c_deg2rad ) );
		light.radiantIntensity = static_cast<float>( light.intensity / ( 2.0 * c_pi * std::max( 1.0 - light.cosOuter, 1e-4 ) ) );
		break;
	case ShLightType::Sun:
		light.range = 0;
		light.radiantIntensity = 0;
		break;
	}
	return light;
}

Vector3 ShLightIrradiance( const ShLight& light, const Vector3& point, const Vector3& normal ){
	Vector3 L;
	double irradiance;
	if ( light.type == ShLightType::Sun ) {
		L = Vector3( -light.direction[0], -light.direction[1], -light.direction[2] );
		irradiance = light.intensity;
	}
	else
	{
		const Vector3 toLight = light.origin - point;
		const double length = vector3_length( toLight );
		const double dist = std::max( length, 5e-3 );
		L = length > 0 ? Vector3( toLight[0] / length, toLight[1] / length, toLight[2] / length ) : Vector3( 0, 0, 1 );
		double radiantIntensity = light.radiantIntensity;
		if ( light.type == ShLightType::Spot ) {
			const float cosAngle = -static_cast<float>( vector3_dot( L, light.direction ) );
			if ( light.cosInner <= light.cosOuter ) { // hard edge (design decision 1)
				radiantIntensity *= cosAngle >= light.cosOuter ? 1.0 : 0.0;
			}
			else
			{
				radiantIntensity *= Smoothstep( light.cosOuter, light.cosInner, cosAngle );
			}
		}
		irradiance = dist <= light.range ? radiantIntensity / ( dist * dist ) : 0.0;
	}
	const double nDotL = std::max( vector3_dot( normal, L ), 0.0 );
	const double e = irradiance * nDotL;
	return Vector3( static_cast<float>( e * light.colour[0] ), static_cast<float>( e * light.colour[1] ), static_cast<float>( e * light.colour[2] ) );
}

// --- 1b. Attached emitter lights --------------------------------------------

bool ShIsAreaLightClassname( const char* classname ){
	return classname != nullptr && EqualNoCase( classname, "light_area" );
}

bool ShIsAttachedEmitterLight( const char* classname, const ShKeyLookup& key ){
	if ( ShIsAreaLightClassname( classname ) ) {
		return true;
	}
	if ( !ShLightTypeFromClassname( classname ) ) {
		return false;
	}
	// q3map2's reading of a boolean key (entity_t::read_keyvalue_, bspfile_abstract.cpp:570), the one
	// the placeholder -light uses for the same skip (emissive-area-lights design.md:249)
	return Key( key, "_proxy" )[0] == '1';
}

ShAreaRect ShAreaRectFromKeys( const Vector3& origin, const ShKeyLookup& key ){
	ShAreaRect rect;
	rect.centre = origin;
	if ( !ShParseFloat( Key( key, "width" ), rect.width ) || !( rect.width > 0 ) ) {
		rect.width = 64;
	}
	if ( !ShParseFloat( Key( key, "height" ), rect.height ) || !( rect.height > 0 ) ) {
		rect.height = 64;
	}

	// Quake's AngleVectors (libs/mathlib/mathlib.c), as the emissive-area-lights spec writes it
	const Vector3 angles = AnglesFromKeys( key );
	const double p = angles[0] * c_deg2rad, y = angles[1] * c_deg2rad, r = angles[2] * c_deg2rad;
	const double sp = std::sin( p ), cp = std::cos( p ), sy = std::sin( y ), cy = std::cos( y ), sr = std::sin( r ), cr = std::cos( r );
	const auto vec = []( double a, double b, double c ){
		return Vector3( static_cast<float>( a ), static_cast<float>( b ), static_cast<float>( c ) );
	};
	rect.forward = vec( cp * cy, cp * sy, -sp );
	rect.right = vec( -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp );
	rect.up = vec( cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp );
	return rect;
}

namespace
{

/// the area of the part of triangle \p tri (in the rectangle's 2D frame) inside [-hw, hw] x [-hh, hh]
double ClippedTriangleArea( const std::array<Vector2, 3>& tri, double hw, double hh ){
	std::vector<std::array<double, 2>> poly = { { tri[0][0], tri[0][1] }, { tri[1][0], tri[1][1] }, { tri[2][0], tri[2][1] } };
	// clip against the four half-planes axis * sign <= limit (Sutherland-Hodgman)
	for ( int edge = 0; edge < 4 && !poly.empty(); ++edge )
	{
		const int axis = edge / 2;
		const double sign = edge % 2 == 0 ? 1.0 : -1.0;
		const double limit = axis == 0 ? hw : hh;
		std::vector<std::array<double, 2>> out;
		for ( size_t i = 0; i < poly.size(); ++i )
		{
			const std::array<double, 2>& a = poly[i];
			const std::array<double, 2>& b = poly[( i + 1 ) % poly.size()];
			const double da = sign * a[axis] - limit, db = sign * b[axis] - limit;
			if ( da <= 0 ) {
				out.push_back( a );
			}
			if ( ( da < 0 && db > 0 ) || ( da > 0 && db < 0 ) ) {
				const double t = da / ( da - db );
				out.push_back( { a[0] + t * ( b[0] - a[0] ), a[1] + t * ( b[1] - a[1] ) } );
			}
		}
		poly = std::move( out );
	}
	double area = 0;
	for ( size_t i = 0; i < poly.size(); ++i )
	{
		const std::array<double, 2>& a = poly[i];
		const std::array<double, 2>& b = poly[( i + 1 ) % poly.size()];
		area += a[0] * b[1] - b[0] * a[1];
	}
	return std::fabs( area ) * 0.5;
}

} // namespace

bool ShAreaRectAttached( const ShAreaRect& rect, const std::vector<Vector3>& positions, const std::vector<Vector3>& normals,
                         const std::vector<std::uint32_t>& indices ){
	// the surface's area-weighted normal; q3map2 winds triangles either way, so the vertex normals orient it
	double n[3] = { 0, 0, 0 };
	for ( size_t t = 0; t + 2 < indices.size(); t += 3 )
	{
		if ( indices[t] >= positions.size() || indices[t + 1] >= positions.size() || indices[t + 2] >= positions.size() ) {
			return false;
		}
		const Vector3& a = positions[indices[t]];
		const Vector3 e1 = positions[indices[t + 1]] - a, e2 = positions[indices[t + 2]] - a;
		n[0] += double( e1[1] ) * e2[2] - double( e1[2] ) * e2[1];
		n[1] += double( e1[2] ) * e2[0] - double( e1[0] ) * e2[2];
		n[2] += double( e1[0] ) * e2[1] - double( e1[1] ) * e2[0];
	}
	double side = 0;
	for ( const Vector3& vn : normals )
		side += n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
	if ( side < 0 ) {
		n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
	}
	const double length = std::sqrt( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
	if ( !( length > 0 ) || !( side != 0 ) ) {
		return false;
	}
	const double cosAngle = ( n[0] * rect.forward[0] + n[1] * rect.forward[1] + n[2] * rect.forward[2] ) / length;
	if ( cosAngle < std::cos( c_shAttachDegrees * c_deg2rad ) ) {
		return false;
	}

	// coplanar: every vertex near the rectangle's plane
	for ( const Vector3& v : positions )
	{
		if ( std::fabs( vector3_dot( v - rect.centre, rect.forward ) ) > c_shAttachDistance ) {
			return false;
		}
	}

	// overlap: some triangle, projected on the plane, covers part of the rectangle
	const double hw = rect.width * 0.5, hh = rect.height * 0.5;
	const auto project = [&rect]( const Vector3& v ){
		const Vector3 d = v - rect.centre;
		return Vector2( static_cast<float>( vector3_dot( d, rect.right ) ), static_cast<float>( vector3_dot( d, rect.up ) ) );
	};
	for ( size_t t = 0; t + 2 < indices.size(); t += 3 )
	{
		const std::array<Vector2, 3> tri = { project( positions[indices[t]] ), project( positions[indices[t + 1]] ), project( positions[indices[t + 2]] ) };
		if ( ClippedTriangleArea( tri, hw, hh ) > c_shAttachMinArea ) {
			return true;
		}
	}
	return false;
}

bool ShProxyAttached( const Vector3& origin, const std::vector<Vector3>& positions ){
	if ( positions.empty() ) {
		return false;
	}
	Vector3 mins = positions[0], maxs = positions[0];
	for ( const Vector3& v : positions )
	{
		for ( int i = 0; i < 3; ++i )
		{
			mins[i] = std::min( mins[i], v[i] );
			maxs[i] = std::max( maxs[i], v[i] );
		}
	}
	for ( int i = 0; i < 3; ++i )
	{
		if ( !( origin[i] >= mins[i] - c_shProxyMargin && origin[i] <= maxs[i] + c_shProxyMargin ) ) {
			return false;
		}
	}
	return true;
}

// --- 2. Casters and bake materials ------------------------------------------

bool ShStage::hasMap() const {
	return !map.empty() && !rend2;
}
bool ShStage::isLightmap() const {
	return EqualNoCase( map.c_str(), "$lightmap" );
}
bool ShStage::isWhiteImage() const {
	return EqualNoCase( map.c_str(), "$whiteimage" );
}
bool ShStage::multiplies() const {
	return blend && ( ( src == ShBlendFactor::DstColour && dst == ShBlendFactor::Zero )
	               || ( src == ShBlendFactor::Zero && dst == ShBlendFactor::SrcColour ) );
}
bool ShStage::adds() const {
	return blend && dst == ShBlendFactor::One && ( src == ShBlendFactor::One || src == ShBlendFactor::SrcAlpha );
}
bool ShStage::blends() const {
	return blend && src == ShBlendFactor::SrcAlpha && dst == ShBlendFactor::OneMinusSrcAlpha;
}

std::optional<ShBlendFactor> ShBlendFactorFromName( const char* name ){
	static const struct
	{
		const char* name;
		ShBlendFactor factor;
	} factors[] = {
		{ "gl_zero", ShBlendFactor::Zero },
		{ "gl_one", ShBlendFactor::One },
		{ "gl_src_color", ShBlendFactor::SrcColour },
		{ "gl_one_minus_src_color", ShBlendFactor::OneMinusSrcColour },
		{ "gl_src_alpha", ShBlendFactor::SrcAlpha },
		{ "gl_one_minus_src_alpha", ShBlendFactor::OneMinusSrcAlpha },
		{ "gl_dst_color", ShBlendFactor::DstColour },
		{ "gl_one_minus_dst_color", ShBlendFactor::OneMinusDstColour },
		{ "gl_dst_alpha", ShBlendFactor::DstAlpha },
		{ "gl_one_minus_dst_alpha", ShBlendFactor::OneMinusDstAlpha },
		{ "gl_src_alpha_saturate", ShBlendFactor::SrcAlphaSaturate },
	};
	if ( name != nullptr ) {
		for ( const auto& f : factors )
		{
			if ( EqualNoCase( name, f.name ) ) {
				return f.factor;
			}
		}
	}
	return {};
}

bool ShStageParseBlendFunc( ShStage& stage, const std::vector<std::string>& args ){
	// Quake3Stage_parseLine; evaluateBlendFactor maps an unknown name to GL_ZERO
	if ( args.size() == 1 ) {
		const char* blend = args[0].c_str();
		if ( EqualNoCase( blend, "add" ) ) {
			stage.blend = true, stage.src = ShBlendFactor::One, stage.dst = ShBlendFactor::One;
		}
		else if ( EqualNoCase( blend, "filter" ) ) {
			stage.blend = true, stage.src = ShBlendFactor::DstColour, stage.dst = ShBlendFactor::Zero;
		}
		else if ( EqualNoCase( blend, "blend" ) ) {
			stage.blend = true, stage.src = ShBlendFactor::SrcAlpha, stage.dst = ShBlendFactor::OneMinusSrcAlpha;
		}
		else
		{
			return false;
		}
		return true;
	}
	if ( args.size() >= 2 ) {
		stage.blend = true;
		stage.src = ShBlendFactorFromName( args[0].c_str() ).value_or( ShBlendFactor::Zero );
		stage.dst = ShBlendFactorFromName( args[1].c_str() ).value_or( ShBlendFactor::Zero );
		return true;
	}
	return false;
}

void ShStageParseAlphaFunc( ShStage& stage, const char* func ){
	if ( func == nullptr ) {
		return;
	}
	if ( EqualNoCase( func, "GT0" ) ) {
		stage.alphaFunc = ShAlphaFunc::GT0;
	}
	else if ( EqualNoCase( func, "LT128" ) ) {
		stage.alphaFunc = ShAlphaFunc::LT128;
	}
	else if ( EqualNoCase( func, "GE128" ) ) {
		stage.alphaFunc = ShAlphaFunc::GE128;
	}
}

bool ShStageIsRend2Keyword( const char* keyword ){
	// shaders.cpp c_rend2StageKeywords
	static const char* const rend2Keywords[] = {
		"stage", "normalMap", "bumpMap", "specularMap", "specularReflectance", "specularExponent",
		"gloss", "roughness", "normalScale", "specularScale", "parallaxDepth",
	};
	if ( keyword != nullptr ) {
		for ( const char* rend2 : rend2Keywords )
		{
			if ( EqualNoCase( keyword, rend2 ) ) {
				return true;
			}
		}
	}
	return false;
}

void ShStageParseLine( ShStage& stage, const char* keyword, const std::vector<std::string>& args ){
	// ShaderTemplate::parseQuake3 and Quake3Stage_parseLine
	if ( keyword == nullptr ) {
		return;
	}
	if ( ShStageIsRend2Keyword( keyword ) ) {
		stage.rend2 = true;
	}
	else if ( EqualNoCase( keyword, "map" ) || EqualNoCase( keyword, "clampMap" ) ) {
		if ( !args.empty() ) {
			stage.map = args[0];
		}
	}
	else if ( EqualNoCase( keyword, "animMap" ) ) { // animMap <frequency> <first frame> ...
		if ( args.size() > 1 ) {
			stage.map = args[1];
		}
	}
	else if ( EqualNoCase( keyword, "blendFunc" ) ) {
		ShStageParseBlendFunc( stage, args );
	}
	else if ( EqualNoCase( keyword, "rgbGen" ) ) {
		if ( !args.empty() && EqualNoCase( args[0].c_str(), "const" ) ) { // rgbGen const ( r g b )
			float c[3];
			std::size_t n = 0;
			for ( std::size_t i = 1; i < args.size() && n < 3; ++i )
			{
				if ( ShParseFloat( args[i].c_str(), c[n] ) ) {
					++n;
				}
			}
			if ( n == 3 ) {
				stage.constColour = true;
				stage.colour = Vector3( c[0], c[1], c[2] );
			}
		}
	}
	else if ( EqualNoCase( keyword, "tcGen" ) || EqualNoCase( keyword, "texGen" ) ) {
		stage.tcGenEnvironment = !args.empty() && EqualNoCase( args[0].c_str(), "environment" );
	}
	else if ( EqualNoCase( keyword, "alphaFunc" ) ) {
		ShStageParseAlphaFunc( stage, args.empty() ? "" : args[0].c_str() );
	}
}

bool ShShaderCasts( const ShSurfaceParms& p ){
	// renderstate.cpp Shadow_materialCasts
	if ( p.skyParms || p.sky || p.fog || p.water || p.slime || p.lava || p.playerClip || p.botClip || p.areaPortal
	  || p.noShadows || p.trigger || p.hint ) {
		return false;
	}
	if ( p.trans && !p.alphaShadow ) {
		return false;
	}
	return !p.nodraw || !p.nonsolid;
}

bool ShMaterialRules::emits() const {
	return emissiveStage >= 0 && emissiveStrength > 0
	    && std::max( { emissiveColour[0], emissiveColour[1], emissiveColour[2] } ) > 0;
}

ShMaterialRules ShDeriveMaterial( const ShShaderDesc& shader ){
	ShMaterialRules rules;
	rules.casts = ShShaderCasts( shader.parms );

	const std::vector<ShStage>& stages = shader.stages;
	const int count = static_cast<int>( stages.size() );

	if ( !shader.scripted ) {
		rules.baseImage = shader.name; // a bare texture is its own base colour
		return rules;
	}

	// base colour (deriveQuake3): the stage multiplied with the $lightmap stage, the earlier of the pair first
	int lightmap = -1;
	for ( int i = 0; i < count; ++i )
	{
		if ( stages[i].isLightmap() && !stages[i].rend2 ) {
			lightmap = i;
			break;
		}
	}
	int base = -1;
	if ( lightmap >= 0 ) {
		if ( lightmap > 0 && stages[lightmap].multiplies() && stages[lightmap - 1].hasMap() ) {
			base = lightmap - 1;
		}
		else if ( lightmap + 1 < count && stages[lightmap + 1].multiplies() && stages[lightmap + 1].hasMap() ) {
			base = lightmap + 1;
		}
	}
	if ( base < 0 ) { // otherwise the first stage map that isn't the lightmap
		for ( int i = 0; i < count; ++i )
		{
			if ( stages[i].hasMap() && !stages[i].isLightmap() ) {
				base = i;
				break;
			}
		}
	}
	if ( base >= 0 ) {
		rules.baseStage = base;
		rules.baseImage = stages[base].isWhiteImage() ? "_white" : stages[base].map;
		rules.alphaFunc = stages[base].alphaFunc;
	}

	// emissive: the first additive stage that isn't the lightmap, a white image or an environment map
	for ( int i = 0; i < count; ++i )
	{
		const ShStage& stage = stages[i];
		if ( stage.adds() && stage.hasMap() && !stage.isLightmap() && !stage.isWhiteImage() && !stage.tcGenEnvironment ) {
			rules.emissiveStage = i;
			rules.emissiveImage = stage.map;
			rules.emissiveColour = stage.constColour ? stage.colour : Vector3( 1, 1, 1 );
			rules.emissiveStrength = shader.emissiveStrength;
			break;
		}
	}

	// additive: every (non-rend2) stage adds
	int considered = 0;
	bool allAdd = true;
	for ( const ShStage& stage : stages )
	{
		if ( stage.rend2 ) {
			continue;
		}
		++considered;
		allAdd = allAdd && stage.adds();
	}
	rules.additive = considered > 0 && allAdd;
	return rules;
}

// --- 3. Alpha rewrite -------------------------------------------------------

std::uint8_t ShAlphaRewrite( ShAlphaFunc func, std::uint8_t alpha ){
	bool pass = true;
	switch ( func )
	{
	case ShAlphaFunc::None:
		pass = true;
		break;
	case ShAlphaFunc::GT0:
		pass = alpha > 0;
		break;
	case ShAlphaFunc::LT128:
		pass = alpha < 128;
		break;
	case ShAlphaFunc::GE128:
		pass = alpha >= 128;
		break;
	}
	return pass ? 255 : 0;
}

void ShAlphaRewriteRGBA( ShAlphaFunc func, std::uint8_t* rgba, std::size_t pixels ){
	for ( std::size_t i = 0; i < pixels; ++i )
	{
		rgba[i * 4 + 3] = ShAlphaRewrite( func, rgba[i * 4 + 3] );
	}
}

// --- 4. Single-tile UV test -------------------------------------------------

namespace
{

constexpr double c_uvTileTolerance = 1e-4;
constexpr double c_uvAreaEpsilon = 1e-12;   // twice the UV area
constexpr double c_uvOverlapEpsilon = 1e-6; // overlap along a separating-axis candidate, UV units

struct UVTri
{
	double u[3], v[3];
	double minU, maxU, minV, maxV;
};

/// true when the two triangles' interiors overlap by more than the epsilon (separating axis test)
bool TrianglesOverlap( const UVTri& a, const UVTri& b ){
	const UVTri* tris[2] = { &a, &b };
	for ( const UVTri* t : tris )
	{
		for ( int e = 0; e < 3; ++e )
		{
			const int f = ( e + 1 ) % 3;
			double ax = -( t->v[f] - t->v[e] );
			double ay = t->u[f] - t->u[e];
			const double len = std::sqrt( ax * ax + ay * ay );
			if ( len <= 0 ) {
				continue;
			}
			ax /= len, ay /= len;
			double minA = 1e300, maxA = -1e300, minB = 1e300, maxB = -1e300;
			for ( int k = 0; k < 3; ++k )
			{
				const double pa = a.u[k] * ax + a.v[k] * ay;
				const double pb = b.u[k] * ax + b.v[k] * ay;
				minA = std::min( minA, pa ), maxA = std::max( maxA, pa );
				minB = std::min( minB, pb ), maxB = std::max( maxB, pb );
			}
			if ( std::min( maxA, maxB ) - std::max( minA, minB ) <= c_uvOverlapEpsilon ) {
				return false;
			}
		}
	}
	return true;
}

} // namespace

ShUVTile ShSingleTileUV( const std::vector<Vector2>& uvs, const std::vector<std::uint32_t>& indices ){
	ShUVTile result;
	if ( indices.empty() || indices.size() % 3 != 0 ) {
		return result;
	}
	double minU = 1e300, minV = 1e300;
	for ( const std::uint32_t index : indices )
	{
		if ( index >= uvs.size() || !std::isfinite( uvs[index][0] ) || !std::isfinite( uvs[index][1] ) ) {
			return result;
		}
		minU = std::min( minU, double( uvs[index][0] ) );
		minV = std::min( minV, double( uvs[index][1] ) );
	}
	const double tileU = std::floor( minU + c_uvTileTolerance );
	const double tileV = std::floor( minV + c_uvTileTolerance );
	if ( std::fabs( tileU ) > 1e6 || std::fabs( tileV ) > 1e6 ) {
		return result;
	}
	for ( const std::uint32_t index : indices )
	{
		const double u = uvs[index][0], v = uvs[index][1];
		if ( u < tileU - c_uvTileTolerance || u > tileU + 1 + c_uvTileTolerance
		  || v < tileV - c_uvTileTolerance || v > tileV + 1 + c_uvTileTolerance ) {
			return result;
		}
	}

	std::vector<UVTri> tris( indices.size() / 3 );
	for ( std::size_t i = 0; i < tris.size(); ++i )
	{
		UVTri& t = tris[i];
		for ( int k = 0; k < 3; ++k )
		{
			t.u[k] = uvs[indices[i * 3 + k]][0];
			t.v[k] = uvs[indices[i * 3 + k]][1];
		}
		const double area2 = std::fabs( ( t.u[1] - t.u[0] ) * ( t.v[2] - t.v[0] ) - ( t.u[2] - t.u[0] ) * ( t.v[1] - t.v[0] ) );
		if ( area2 <= c_uvAreaEpsilon ) {
			return result; // no texels in sh-baker's rasteriser: its power would be lost
		}
		t.minU = std::min( { t.u[0], t.u[1], t.u[2] } ), t.maxU = std::max( { t.u[0], t.u[1], t.u[2] } );
		t.minV = std::min( { t.v[0], t.v[1], t.v[2] } ), t.maxV = std::max( { t.v[0], t.v[1], t.v[2] } );
	}
	std::sort( tris.begin(), tris.end(), []( const UVTri& a, const UVTri& b ){ return a.minU < b.minU; } );
	for ( std::size_t i = 0; i < tris.size(); ++i )
	{
		for ( std::size_t j = i + 1; j < tris.size() && tris[j].minU < tris[i].maxU - c_uvOverlapEpsilon; ++j )
		{
			if ( std::min( tris[i].maxV, tris[j].maxV ) - std::max( tris[i].minV, tris[j].minV ) <= c_uvOverlapEpsilon ) {
				continue;
			}
			if ( TrianglesOverlap( tris[i], tris[j] ) ) {
				return result;
			}
		}
	}
	result.single = true;
	result.u = static_cast<int>( tileU );
	result.v = static_cast<int>( tileV );
	return result;
}

// --- 5. SH basis -------------------------------------------------------------

std::array<float, 9> ShBasis( const Vector3& n ){
	const float x = n[0], y = n[1], z = n[2];
	return {
		c_shBasis0,
		c_shBasis1 * y,
		c_shBasis1 * z,
		c_shBasis1 * x,
		c_shBasis2_1 * x * y,
		c_shBasis2_1 * y * z,
		c_shBasis2_0 * ( 3.0f * z * z - 1.0f ),
		c_shBasis2_1 * x * z,
		c_shBasis2_2 * ( x * x - y * y ),
	};
}

// --- 6. Lightmap encoding ---------------------------------------------------

double ShHable( double x ){
	const double A = c_shHableA, B = c_shHableB, C = c_shHableC, D = c_shHableD, E = c_shHableE, F = c_shHableF;
	return ( x * ( A * x + C * B ) + D * E ) / ( x * ( A * x + B ) + D * F ) - E / F;
}

double ShSrgbEncode( double c ){
	return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow( c, 1.0 / 2.4 ) - 0.055;
}

double ShSrgbDecode( double s ){
	return s <= 0.04045 ? s / 12.92 : std::pow( ( s + 0.055 ) / 1.055, 2.4 );
}

int ShLightmapByte( float irradiance ){
	const double d = double( irradiance ) / c_pi;
	if ( !( d > 0 ) ) { // also NaN
		return 0;
	}
	double t;
	if ( d >= c_shWhitePoint ) {
		t = 1; // and keeps U(inf) = inf / inf out
	}
	else
	{
		t = std::clamp( ShHable( d ) / ShHable( c_shWhitePoint ), 0.0, 1.0 );
	}
	const long byte = std::lround( c_shByteScale * ShSrgbEncode( t ) );
	return static_cast<int>( std::clamp( byte, 0L, 64L ) );
}

ShBytes3 ShLightmapBytes( const Vector3& E ){
	return ShBytes3( static_cast<std::uint8_t>( ShLightmapByte( E[0] ) ),
	                 static_cast<std::uint8_t>( ShLightmapByte( E[1] ) ),
	                 static_cast<std::uint8_t>( ShLightmapByte( E[2] ) ) );
}

ShBytes3 ShNeutralLightmapBytes(){
	return ShBytes3( c_shNeutralLightmapByte, c_shNeutralLightmapByte, c_shNeutralLightmapByte );
}

// --- 7. Grid fit ------------------------------------------------------------

namespace
{

constexpr double c_luminance[3] = { 0.2126, 0.7152, 0.0722 }; // Rec. 709

/// the L1 vector (L11, L1m1, L10) = (x, y, z) of one channel
Vector3 L1( const ShCoeffs9& sh, int c ){
	return Vector3( sh[3][c], sh[1][c], sh[2][c] );
}

/// the luminance-weighted L1 vector
Vector3 WeightedL1( const ShCoeffs9& sh ){
	double v[3] = { 0, 0, 0 };
	for ( int c = 0; c < 3; ++c )
	{
		const Vector3 l = L1( sh, c );
		for ( int k = 0; k < 3; ++k )
		{
			v[k] += c_luminance[c] * l[k];
		}
	}
	return Vector3( static_cast<float>( v[0] ), static_cast<float>( v[1] ), static_cast<float>( v[2] ) );
}

bool UsableDirection( const Vector3& v ){
	const double l2 = vector3_length_squared( v );
	return std::isfinite( l2 ) && l2 > c_shGridMinL1Length2;
}

} // namespace

float ShGridRealisedAmbient( int ambientByte ){
	return std::min( 255.0f, float( ambientByte << c_shGridShift ) * c_shGridAmbientScale + c_shGridFloor );
}

float ShGridRealisedLit( int ambientByte, int directedByte ){
	return std::min( 255.0f, ShGridRealisedAmbient( ambientByte ) + float( directedByte << c_shGridShift ) * c_shGridDirectedScale );
}

ShGridFit ShFitGridPoint( const ShCoeffs9& sh ){
	ShGridFit fit;

	const Vector3 weightedL1 = WeightedL1( sh );
	fit.direction = UsableDirection( weightedL1 ) ? vector3_normalised( weightedL1 ) : Vector3( 0, 0, 1 );

	const float scale = 1 << c_shGridShift;
	int ambient[3], directed[3];
	float targetAD[3];
	for ( int c = 0; c < 3; ++c )
	{
		// a delta light of irradiance E-perp from d projects to E-perp Y(d): L1 . d = E-perp c_shBasis1, L0 = E-perp c_shBasis0
		const double D = std::max( 0.0, double( vector3_dot( L1( sh, c ), fit.direction ) ) ) / c_shBasis1;
		const double L0rest = std::max( 0.0, double( sh[0][c] ) - D * c_shBasis0 );
		const double A = c_pi * c_shBasis0 * L0rest;
		fit.directed[c] = static_cast<float>( D );
		fit.ambient[c] = static_cast<float>( A );

		const float targetA = scale * ShLightmapByte( fit.ambient[c] );
		targetAD[c] = scale * ShLightmapByte( fit.ambient[c] + fit.directed[c] );

		const float ambientPost = std::clamp( ( targetA - c_shGridFloor ) / c_shGridAmbientScale, 0.0f, 255.0f );
		ambient[c] = std::min<int>( c_shGridMaxByte, std::lround( ambientPost / scale ) );
	}

	// renderergl1 R_SetupEntityLightingGrid skips a grid cell whose ambient triple is zero as one in a wall,
	// and lights the model from the other cells; a baked point is never in a wall, so even a dark one gets
	// ambient 1 (q3map2 TraceGrid does this only when directed isn't zero; task 6.3)
	if ( ambient[0] + ambient[1] + ambient[2] == 0 ) {
		ambient[0] = ambient[1] = ambient[2] = 1;
	}

	// the lit face is measured from the ambient the engine realises from the final byte
	for ( int c = 0; c < 3; ++c )
	{
		const float realised = ShGridRealisedAmbient( ambient[c] );
		const float directedPost = std::clamp( targetAD[c] - realised, 0.0f, 255.0f ) / c_shGridDirectedScale;
		directed[c] = std::min<int>( c_shGridMaxByte, std::lround( directedPost / scale ) );
	}

	fit.ambientBytes = ShBytes3( std::uint8_t( ambient[0] ), std::uint8_t( ambient[1] ), std::uint8_t( ambient[2] ) );
	fit.directedBytes = ShBytes3( std::uint8_t( directed[0] ), std::uint8_t( directed[1] ), std::uint8_t( directed[2] ) );
	return fit;
}

ShGridFit ShGridNeutralFit(){
	ShGridFit fit;
	fit.direction = Vector3( 0, 0, 1 );
	fit.ambientBytes = ShBytes3( c_shGridNeutralAmbientByte, c_shGridNeutralAmbientByte, c_shGridNeutralAmbientByte );
	fit.directedBytes = ShBytes3( 0, 0, 0 );
	return fit;
}
