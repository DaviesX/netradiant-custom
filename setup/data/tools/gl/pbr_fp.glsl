/// ============================================================================
/*
PBR lighting pass, fragment program: one light per pass, additively blended.

Material: glTF metallic-roughness.
  base colour and emissive are sRGB encoded, everything else is linear.
BRDF: ported from sh-renderer glsl/radiance.frag: GGX normal distribution
  (roughness clamped to >= 0.05), Smith-Schlick geometry, Schlick Fresnel with
  F0 = mix(0.04, basecolor, metallic), Disney diffuse, kD = (1 - F)(1 - metallic).
Lights (see the pbr gamepack README):
  point: radiant intensity = flux / (4 pi)
  spot:  radiant intensity = flux / (2 pi (1 - cosOuter))
         irradiance = I / d^2 with d in map units (the engine shades in raw
         map units, no metre conversion), cut hard at the cutoff radius (the
         engine culls lights per tile by that radius and has no window term),
         cone falloff smoothstep(cosOuter, cosInner, cosAngle)
  sun:   irradiance = intensity, no falloff
  area:  radiance L over a one-sided rectangle, linearly transformed cosines
         (Heitz, Dupuy, Hill, Neubelt 2016; github.com/selfshadow/ltc_code,
         webgl/shaders/ltc/ltc_quad.fs): Lambert form factor for diffuse with
         no 1/pi and no (1 - F), LTC GGX for specular weighted by the table 2
         Fresnel, zero behind the plane and beyond the radius from the centre
The output is linear radiance; exposure and tonemapping happen in the resolve.
*/
/// ============================================================================

uniform sampler2D	u_basecolormap;
uniform sampler2D	u_normalmap;
uniform sampler2D	u_metallicroughnessmap;   // G = roughness, B = metallic
uniform sampler2D	u_occlusionmap;           // R = occlusion (unused for direct light)
uniform sampler2D	u_emissivemap;

uniform vec4		u_basecolor_factor;
uniform float		u_metallic_factor;
uniform float		u_roughness_factor;

uniform vec3		u_view_origin;            // object space
uniform vec3		u_light_origin;           // object space
uniform vec3		u_light_direction;        // object space, direction the light travels
uniform vec3		u_light_color;            // linear RGB
uniform float		u_light_intensity;        // flux (point, spot) or irradiance (sun)
uniform float		u_light_radius;           // map units
uniform float		u_light_cos_inner;
uniform float		u_light_cos_outer;
uniform int			u_light_type;             // 1 point, 2 spot, 3 sun, 4 area
uniform vec3		u_light_half_right;       // object space, area: right * width / 2, not normalised
uniform vec3		u_light_half_up;          // object space, area: up * height / 2, not normalised

// LTC tables (gl/ltc_ggx.bin): table 1 (M^-1) in rows 0-63, table 2 (magnitude, Fresnel, 0, sphere) in rows 64-127
uniform sampler2D	u_ltc_tables;
uniform int			u_ltc_available;          // 0: the tables failed to load, area lights shade diffuse only

// Shadows, ported from sh-renderer glsl/radiance.frag. Cascades and spot tiles share one atlas each and are
// addressed by a uv scale and offset, because GLSL 1.20 forbids indexing a sampler array with a non-constant.
uniform sampler2DShadow	u_sun_shadow_atlas;
uniform sampler2DShadow	u_spot_shadow_atlas;
uniform int			u_shadow_mode;            // 0 unshadowed, 1 spot, 2 sun
uniform mat4		u_local_to_shadow0;       // object space -> light clip space; cascade 0, or the spot
uniform mat4		u_local_to_shadow1;
uniform mat4		u_local_to_shadow2;
uniform vec4		u_shadow_uv0;             // xy atlas uv scale, zw atlas uv offset
uniform vec4		u_shadow_uv1;
uniform vec4		u_shadow_uv2;
uniform vec3		u_sun_cascade_splits;     // view space distances at which cascades 0 and 1 end
uniform vec4		u_local_to_view_z;        // third row of object space -> camera view space
uniform float		u_shadow_texel_size;      // 1 / atlas size

varying vec3		var_vertex;
varying vec2		var_texcoord;
varying mat3		var_mat_ts2os;

const float PI = 3.14159265358979;

float interleavedGradientNoise( vec2 positionScreen )
{
	vec3 magic = vec3( 0.06711056, 0.00583715, 52.9829189 );
	return fract( magic.z * fract( dot( positionScreen, magic.xy ) ) );
}

// One hardware depth comparison, clamped into this light's tile: the one texel guard band around every tile is
// left at the cleared depth, so a tap that lands in it reads as lit rather than picking up a neighbour.
float shadowTap( sampler2DShadow map, vec3 coord, vec2 disc, mat2 rotation, float radius, vec4 uvScaleOffset )
{
	vec2 uv = coord.xy + rotation * disc * radius;
	vec2 lo = uvScaleOffset.zw - u_shadow_texel_size;
	vec2 hi = uvScaleOffset.zw + uvScaleOffset.xy + u_shadow_texel_size;
	return shadow2D( map, vec3( clamp( uv, lo, hi ), coord.z ) ).r;
}

// normal offset + slope scaled depth bias + nine tap Poisson disc rotated per pixel, as in the reference renderer.
// The disc is unrolled rather than held in an array so nothing depends on GLSL 1.20 dynamic indexing rules.
float computeShadow( sampler2DShadow map, mat4 toLight, vec4 uvScaleOffset, float penumbra,
                     vec3 position, vec3 normal, float NdotL )
{
	vec3 biased = position + normal * ( 1.0 - NdotL ) * 0.005;
	float slope = sqrt( max( 1.0 - NdotL * NdotL, 0.0 ) ) / max( NdotL, 1e-4 );
	if ( u_light_type == 4 ) {
		// an area light's map is a wide perspective frustum seen at grazing angles from far away, where a bias in
		// normalised depth grows with the square of the distance (0.003 is about 77 units at 320) and detaches the
		// shadow from its caster. Pay the bias in map units instead, scaled to one shadow texel at the receiver.
		vec3 fromLight = position - u_light_origin;
		float dist = length( fromLight );
		float cosHalf = clamp( u_light_cos_outer, 1e-4, 1.0 );
		float tanHalf = sqrt( 1.0 - cosHalf * cosHalf ) / cosHalf;
		float texelWorld = 2.0 * dist * tanHalf * u_shadow_texel_size / max( uvScaleOffset.x, 1e-6 );
		biased = position + normal * texelWorld * ( 1.0 - NdotL )
		       - fromLight / max( dist, 1e-4 ) * texelWorld * clamp( slope, 1.0, 3.0 );
	}
	vec4 lightPos = toLight * vec4( biased, 1.0 );
	vec3 proj = lightPos.xyz / lightPos.w;
	proj = proj * 0.5 + 0.5;
	if ( proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 ) {
		// beside this light's map. A spot gives no light there anyway; an area light lights its whole front
		// hemisphere, so treating it as lit would leak past occluders the map can't see (a recessed emitter's
		// surround), and a casting area light's _frustum bounds its light instead
		return u_light_type == 4 ? 0.0 : 1.0;
	}
	if ( proj.z < 0.0 || proj.z > 1.0 ) {
		return 1.0;   // nearer than the near plane or past the radius cutoff: lit
	}

	// an area light fades out over the last 10 degrees before _frustum on each axis of its square frustum, so
	// its light doesn't stop at a hard edge
	float edgeFade = 1.0;
	if ( u_light_type == 4 ) {
		float cosHalf = clamp( u_light_cos_outer, 1e-4, 1.0 );
		float halfAngle = acos( cosHalf );
		float tanHalf = sqrt( 1.0 - cosHalf * cosHalf ) / cosHalf;
		vec2 offAxis = atan( abs( proj.xy * 2.0 - 1.0 ) * tanHalf );
		vec2 fade = 1.0 - smoothstep( vec2( max( halfAngle - 0.17453293, 0.0 ) ), vec2( halfAngle ), offAxis );
		edgeFade = fade.x * fade.y;
	}

	float bias = u_light_type == 4 ? 0.0 : clamp( 0.001 * slope, 0.0, 0.003 );
	if ( u_shadow_mode == 2 ) {
		// a cascade's depth range reaches back to the world extent so that every caster is in it, which makes a
		// bias in normalised depth hundreds of map units; pay it in map units instead. The projection is
		// orthographic, so the third row of the matrix is the depth one unit along the light covers.
		float depthPerUnit = 0.5 * length( vec3( toLight[0][2], toLight[1][2], toLight[2][2] ) );
		bias = clamp( 0.5 * slope, 0.25, 2.0 ) * depthPerUnit;
	}

	vec3 coord = vec3( uvScaleOffset.xy * proj.xy + uvScaleOffset.zw, proj.z - bias );

	float angle = interleavedGradientNoise( gl_FragCoord.xy ) * 6.28318530718;
	float s = sin( angle );
	float c = cos( angle );
	mat2 rotation = mat2( c, -s, s, c );
	float radius = u_shadow_texel_size * penumbra;

	float sum = 0.0;
	sum += shadowTap( map, coord, vec2( -0.7674601,  0.5097495 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2( -0.0652758,  0.9238806 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2(  0.6559792,  0.7077699 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2( -0.9333916, -0.2797686 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2( -0.3013854, -0.4005081 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2(  0.4485547, -0.1982736 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2(  0.9008985,  0.1802927 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2( -0.5298812, -0.8258384 ), rotation, radius, uvScaleOffset );
	sum += shadowTap( map, coord, vec2(  0.3533838, -0.8351508 ), rotation, radius, uvScaleOffset );
	return sum / 9.0 * edgeFade;
}

// Cascade selection is unrolled for the same reason: no dynamic index into a sampler or a uniform array.
float shadowTerm( vec3 position, vec3 normal, float NdotL )
{
	if ( u_shadow_mode == 0 ) {
		return 1.0;
	}
	if ( u_shadow_mode == 1 ) {
		return computeShadow( u_spot_shadow_atlas, u_local_to_shadow0, u_shadow_uv0, 1.0, position, normal, NdotL );
	}

	float viewDepth = abs( dot( u_local_to_view_z, vec4( position, 1.0 ) ) );
	if ( viewDepth < u_sun_cascade_splits.x ) {
		return computeShadow( u_sun_shadow_atlas, u_local_to_shadow0, u_shadow_uv0, 2.0, position, normal, NdotL );
	}
	if ( viewDepth < u_sun_cascade_splits.y ) {
		return computeShadow( u_sun_shadow_atlas, u_local_to_shadow1, u_shadow_uv1, 1.0, position, normal, NdotL );
	}
	// past the last split there is no cascade covering this fragment. Its light space position can still land
	// inside the last cascade's bounds, because that box is an AABB fitted around a frustum slice and for an
	// oblique sun it reaches well beyond the slice, so sampling anyway would shadow some of what lies past the
	// shadow distance and not the rest, depending on the sun angle. Render it lit instead.
	if ( viewDepth >= u_sun_cascade_splits.z ) {
		return 1.0;
	}
	return computeShadow( u_sun_shadow_atlas, u_local_to_shadow2, u_shadow_uv2, 2.0 / 3.0, position, normal, NdotL );
}

// exact sRGB electro-optical transfer function
vec3 srgbToLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}

float distributionGGX( float NdotH, float alpha )
{
	float a2 = alpha * alpha;
	float d = NdotH * NdotH * ( a2 - 1.0 ) + 1.0;
	return a2 / ( PI * d * d );
}

float geometrySchlickGGX( float NdotX, float k )
{
	return NdotX / ( NdotX * ( 1.0 - k ) + k );
}

// Smith geometry term with the Schlick-GGX approximation, k = (roughness + 1)^2 / 8 for analytic lights
float geometrySmith( float NdotV, float NdotL, float roughness )
{
	float r = roughness + 1.0;
	float k = ( r * r ) / 8.0;
	return geometrySchlickGGX( NdotV, k ) * geometrySchlickGGX( NdotL, k );
}

vec3 fresnelSchlick( float VdotH, vec3 F0 )
{
	float f = pow( 1.0 - VdotH, 5.0 );
	return F0 + ( 1.0 - F0 ) * f;
}

// Burley / Disney diffuse
float diffuseDisney( float NdotV, float NdotL, float VdotH, float roughness )
{
	float fd90 = 0.5 + 2.0 * roughness * VdotH * VdotH;
	float lightScatter = 1.0 + ( fd90 - 1.0 ) * pow( 1.0 - NdotL, 5.0 );
	float viewScatter = 1.0 + ( fd90 - 1.0 ) * pow( 1.0 - NdotV, 5.0 );
	return lightScatter * viewScatter;
}

// ---------------------------------------------------------------------------
// Area lights: linearly transformed cosines, after upstream's ltc_quad.fs. Arrays and matrix transposes are avoided
// so the file stays GLSL 1.10: the polygon is five named vertices and the basis is applied with dot products.
// ---------------------------------------------------------------------------

const float LTC_LUT_SIZE = 64.0;
const float LTC_LUT_SCALE = ( LTC_LUT_SIZE - 1.0 ) / LTC_LUT_SIZE;
const float LTC_LUT_BIAS = 0.5 / LTC_LUT_SIZE;

// upstream's IntegrateEdge: the edge's contribution to the cosine-weighted solid angle, with the 1/(2 pi) folded
// into the rational fit of theta / sin(theta)
float ltcIntegrateEdge( vec3 v1, vec3 v2 )
{
	float x = dot( v1, v2 );
	float y = abs( x );

	float a = 0.8543985 + ( 0.4965155 + 0.0145206 * y ) * y;
	float b = 3.4175940 + ( 4.1616724 + y ) * y;
	float v = a / b;

	float theta_sintheta = ( x > 0.0 ) ? v : 0.5 * inversesqrt( max( 1.0 - x * x, 1e-7 ) ) - v;

	return cross( v1, v2 ).z * theta_sintheta;
}

// upstream's ClipQuadToHorizon on five named vertices; n is the clipped vertex count
void ltcClipQuadToHorizon( inout vec3 L0, inout vec3 L1, inout vec3 L2, inout vec3 L3, inout vec3 L4, out int n )
{
	int config = 0;
	if ( L0.z > 0.0 ) config += 1;
	if ( L1.z > 0.0 ) config += 2;
	if ( L2.z > 0.0 ) config += 4;
	if ( L3.z > 0.0 ) config += 8;

	n = 0;

	if ( config == 1 ) { // V1 clip V2 V3 V4
		n = 3;
		L1 = -L1.z * L0 + L0.z * L1;
		L2 = -L3.z * L0 + L0.z * L3;
	}
	else if ( config == 2 ) { // V2 clip V1 V3 V4
		n = 3;
		L0 = -L0.z * L1 + L1.z * L0;
		L2 = -L2.z * L1 + L1.z * L2;
	}
	else if ( config == 3 ) { // V1 V2 clip V3 V4
		n = 4;
		L2 = -L2.z * L1 + L1.z * L2;
		L3 = -L3.z * L0 + L0.z * L3;
	}
	else if ( config == 4 ) { // V3 clip V1 V2 V4
		n = 3;
		L0 = -L3.z * L2 + L2.z * L3;
		L1 = -L1.z * L2 + L2.z * L1;
	}
	else if ( config == 6 ) { // V2 V3 clip V1 V4
		n = 4;
		L0 = -L0.z * L1 + L1.z * L0;
		L3 = -L3.z * L2 + L2.z * L3;
	}
	else if ( config == 7 ) { // V1 V2 V3 clip V4
		n = 5;
		L4 = -L3.z * L0 + L0.z * L3;
		L3 = -L3.z * L2 + L2.z * L3;
	}
	else if ( config == 8 ) { // V4 clip V1 V2 V3
		n = 3;
		L0 = -L0.z * L3 + L3.z * L0;
		L1 = -L2.z * L3 + L3.z * L2;
		L2 = L3;
	}
	else if ( config == 9 ) { // V1 V4 clip V2 V3
		n = 4;
		L1 = -L1.z * L0 + L0.z * L1;
		L2 = -L2.z * L3 + L3.z * L2;
	}
	else if ( config == 11 ) { // V1 V2 V4 clip V3
		n = 5;
		L4 = L3;
		L3 = -L2.z * L3 + L3.z * L2;
		L2 = -L2.z * L1 + L1.z * L2;
	}
	else if ( config == 12 ) { // V3 V4 clip V1 V2
		n = 4;
		L1 = -L1.z * L2 + L2.z * L1;
		L0 = -L0.z * L3 + L3.z * L0;
	}
	else if ( config == 13 ) { // V1 V3 V4 clip V2
		n = 5;
		L4 = L3;
		L3 = L2;
		L2 = -L1.z * L2 + L2.z * L1;
		L1 = -L1.z * L0 + L0.z * L1;
	}
	else if ( config == 14 ) { // V2 V3 V4 clip V1
		n = 5;
		L4 = -L0.z * L3 + L3.z * L0;
		L0 = -L0.z * L1 + L1.z * L0;
	}
	else if ( config == 15 ) { // V1 V2 V3 V4
		n = 4;
	}
	// configs 0, 5 and 10 clip everything (5 and 10 cannot occur for a planar convex quad)

	if ( n == 3 )
		L3 = L0;
	if ( n == 4 )
		L4 = L0;
}

// upstream's LTC_Evaluate, one-sided and with clipping: the corners q0..q3 relative to the shaded point, already in
// the (T1, T2, N) frame, are transformed by Minv, clipped to the horizon, projected onto the sphere and integrated
float ltcEvaluate( mat3 Minv, vec3 q0, vec3 q1, vec3 q2, vec3 q3 )
{
	vec3 L0 = Minv * q0;
	vec3 L1 = Minv * q1;
	vec3 L2 = Minv * q2;
	vec3 L3 = Minv * q3;
	vec3 L4 = L0;

	int n;
	ltcClipQuadToHorizon( L0, L1, L2, L3, L4, n );
	if ( n == 0 ) {
		return 0.0;
	}

	L0 = normalize( L0 );
	L1 = normalize( L1 );
	L2 = normalize( L2 );
	L3 = normalize( L3 );
	L4 = normalize( L4 );

	float sum = 0.0;
	sum += ltcIntegrateEdge( L0, L1 );
	sum += ltcIntegrateEdge( L1, L2 );
	sum += ltcIntegrateEdge( L2, L3 );
	if ( n >= 4 )
		sum += ltcIntegrateEdge( L3, L4 );
	if ( n == 5 )
		sum += ltcIntegrateEdge( L4, L0 );

	return max( 0.0, sum );
}

// outgoing radiance from the area light, before the light colour, intensity and shadow
vec3 areaLightBRDF( vec3 position, vec3 N, vec3 V, vec3 baseColor, float metallic, float roughness )
{
	// the frame of upstream's LTC_Evaluate; at normal incidence any tangent will do, the lobe is isotropic there
	vec3 T1 = V - N * dot( V, N );
	if ( dot( T1, T1 ) < 1e-12 ) {
		T1 = abs( N.z ) < 0.999 ? cross( N, vec3( 0.0, 0.0, 1.0 ) ) : cross( N, vec3( 1.0, 0.0, 0.0 ) );
	}
	T1 = normalize( T1 );
	vec3 T2 = cross( N, T1 );

	// corners in upstream's order (InitRectPoints with dirx = right, diry = up), which emits along forward
	vec3 c = u_light_origin - position;
	vec3 p0 = c - u_light_half_right - u_light_half_up;
	vec3 p1 = c + u_light_half_right - u_light_half_up;
	vec3 p2 = c + u_light_half_right + u_light_half_up;
	vec3 p3 = c - u_light_half_right + u_light_half_up;
	vec3 q0 = vec3( dot( p0, T1 ), dot( p0, T2 ), dot( p0, N ) );
	vec3 q1 = vec3( dot( p1, T1 ), dot( p1, T2 ), dot( p1, N ) );
	vec3 q2 = vec3( dot( p2, T1 ), dot( p2, T2 ), dot( p2, N ) );
	vec3 q3 = vec3( dot( p3, T1 ), dot( p3, T2 ), dot( p3, N ) );

	// normalised form factor: cosine-weighted solid angle above the horizon / pi, so E = pi L FF
	float formFactor = ltcEvaluate( mat3( 1.0 ), q0, q1, q2, q3 );
	vec3 radiance = ( 1.0 - metallic ) * baseColor * formFactor;

	if ( u_ltc_available != 0 ) {
		float NdotV = clamp( dot( N, V ), 0.0, 1.0 );
		vec2 uv = vec2( roughness, sqrt( 1.0 - NdotV ) ) * LTC_LUT_SCALE + LTC_LUT_BIAS;
		vec4 t1 = texture2D( u_ltc_tables, vec2( uv.x, 0.5 * uv.y ) );
		vec4 t2 = texture2D( u_ltc_tables, vec2( uv.x, 0.5 + 0.5 * uv.y ) );
		mat3 Minv = mat3( vec3( t1.x, 0.0, t1.y ),
		                  vec3( 0.0, 1.0, 0.0 ),
		                  vec3( t1.z, 0.0, t1.w ) );
		float spec = ltcEvaluate( Minv, q0, q1, q2, q3 );

		vec3 F0 = mix( vec3( 0.04 ), baseColor, metallic );
		radiance += spec * ( F0 * t2.x + ( 1.0 - F0 ) * t2.y );
	}

	return radiance;
}

void	main()
{
	vec4 baseSample = texture2D( u_basecolormap, var_texcoord );
	vec3 baseColor = srgbToLinear( baseSample.rgb ) * u_basecolor_factor.rgb;
	float alpha = baseSample.a * u_basecolor_factor.a;

	vec3 mr = texture2D( u_metallicroughnessmap, var_texcoord ).rgb;
	float roughness = clamp( mr.g * u_roughness_factor, 0.05, 1.0 );
	float metallic = clamp( mr.b * u_metallic_factor, 0.0, 1.0 );

	vec3 nTS = texture2D( u_normalmap, var_texcoord ).xyz * 2.0 - 1.0;
	vec3 N = normalize( var_mat_ts2os * nTS );

	vec3 V = normalize( u_view_origin - var_vertex );

	if ( u_light_type == 4 ) {
		// one-sided, with the cutoff measured from the centre
		vec3 fromLight = var_vertex - u_light_origin;
		if ( dot( fromLight, u_light_direction ) <= 0.0 || length( fromLight ) > u_light_radius ) {
			gl_FragColor = vec4( 0.0, 0.0, 0.0, alpha );
			return;
		}
		// the shadow map is a perspective frustum from the centre, so its NdotL is toward the centre
		float NdotC = max( dot( N, normalize( -fromLight ) ), 0.0 );
		float areaShadow = NdotC > 0.0 ? shadowTerm( var_vertex, N, NdotC ) : 1.0;
		vec3 areaRadiance = areaLightBRDF( var_vertex, N, V, baseColor, metallic, roughness ) * u_light_color * u_light_intensity * areaShadow;
		gl_FragColor = vec4( areaRadiance * alpha, alpha );
		return;
	}

	// light vector and irradiance for this light
	vec3 L;
	float irradiance;
	if ( u_light_type == 3 ) {
		L = -u_light_direction;
		irradiance = u_light_intensity;
	}
	else {
		vec3 toLight = u_light_origin - var_vertex;
		float dist = max( length( toLight ), 5e-3 );
		L = toLight / dist;

		float radiantIntensity;
		if ( u_light_type == 2 ) {
			radiantIntensity = u_light_intensity / ( 2.0 * PI * max( 1.0 - u_light_cos_outer, 1e-4 ) );
			float cosAngle = dot( -L, u_light_direction );
			radiantIntensity *= smoothstep( u_light_cos_outer, u_light_cos_inner, cosAngle );
		}
		else {
			radiantIntensity = u_light_intensity / ( 4.0 * PI );
		}

		irradiance = dist <= u_light_radius ? radiantIntensity / ( dist * dist ) : 0.0;
	}

	float NdotL = max( dot( N, L ), 0.0 );
	float NdotV = max( dot( N, V ), 0.0 );
	vec3 H = normalize( L + V );
	float NdotH = max( dot( N, H ), 0.0 );
	float VdotH = max( dot( V, H ), 0.0 );

	float alphaRough = roughness * roughness;
	vec3 F0 = mix( vec3( 0.04 ), baseColor, metallic );

	float D = distributionGGX( NdotH, alphaRough );
	float G = geometrySmith( NdotV, NdotL, roughness );
	vec3 F = fresnelSchlick( VdotH, F0 );

	vec3 specular = ( D * G * F ) / ( 4.0 * NdotL * NdotV + 0.001 );
	vec3 kD = ( 1.0 - F ) * ( 1.0 - metallic );
	vec3 diffuse = kD * baseColor / PI * diffuseDisney( NdotV, NdotL, VdotH, roughness );

	float shadow = NdotL > 0.0 ? shadowTerm( var_vertex, N, NdotL ) : 1.0;

	vec3 radiance = ( diffuse + specular ) * u_light_color * irradiance * NdotL * shadow;

	gl_FragColor = vec4( radiance * alpha, alpha );
}
