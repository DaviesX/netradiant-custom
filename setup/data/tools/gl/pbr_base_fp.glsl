/// ============================================================================
/*
PBR base pass, fragment program:
  emissive * emissivefactor * emissivestrength + ambient * basecolor * occlusion
Output is linear radiance, resolved later with exposure and tonemapping.
*/
/// ============================================================================

uniform sampler2D	u_basecolormap;
uniform sampler2D	u_normalmap;
uniform sampler2D	u_metallicroughnessmap;
uniform sampler2D	u_occlusionmap;   // R = occlusion
uniform sampler2D	u_emissivemap;

uniform vec4		u_basecolor_factor;
uniform vec3		u_emissive_factor;
uniform float		u_emissive_strength;
uniform float		u_ambient;        // constant ambient irradiance (W/m^2 equivalent)

varying vec2		var_texcoord;

vec3 srgbToLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}

void	main()
{
	vec4 baseSample = texture2D( u_basecolormap, var_texcoord );
	vec3 baseColor = srgbToLinear( baseSample.rgb ) * u_basecolor_factor.rgb;
	float alpha = baseSample.a * u_basecolor_factor.a;

	vec3 emissive = srgbToLinear( texture2D( u_emissivemap, var_texcoord ).rgb ) * u_emissive_factor * u_emissive_strength;
	float occlusion = texture2D( u_occlusionmap, var_texcoord ).r;

	vec3 radiance = emissive + u_ambient * baseColor * occlusion;

	gl_FragColor = vec4( radiance, alpha );
}
