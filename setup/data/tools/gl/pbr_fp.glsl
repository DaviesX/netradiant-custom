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

varying vec3		var_vertex;
varying vec2		var_texcoord;
varying mat3		var_mat_ts2os;

const float PI = 3.14159265358979;

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

	vec3 radiance = ( diffuse + specular ) * u_light_color * irradiance * NdotL;

	gl_FragColor = vec4( radiance * alpha, alpha );
}
