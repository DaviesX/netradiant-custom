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
uniform int			u_light_type;             // 1 point, 2 spot, 3 sun

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
	vec4 lightPos = toLight * vec4( biased, 1.0 );
	vec3 proj = lightPos.xyz / lightPos.w;
	proj = proj * 0.5 + 0.5;
	if ( proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z < 0.0 || proj.z > 1.0 ) {
		return 1.0;   // outside this light's map: lit
	}

	float bias = clamp( 0.001 * ( sqrt( 1.0 - NdotL * NdotL ) / max( NdotL, 1e-4 ) ), 0.0, 0.003 );

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
	return sum / 9.0;
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
