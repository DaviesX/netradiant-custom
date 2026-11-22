#version 120
/// ============================================================================
/*
PBR base pass, fragment program:
  emissive * emissivefactor * emissivestrength + ambient * basecolor * occlusion
  + (1 - metallic) * basecolor * occlusion * E_sky(n) / pi
Output is linear radiance, resolved later with exposure and tonemapping.
E_sky(n) / pi = max( sum c_i Y_i(n), 0 ) with c_i = A_l L_lm / pi, the sky's L0-L2 SH in sh-baker's order
(sky-environment-light); n is the interpolated vertex normal in world space. Zero coefficients give no sky term.
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
uniform float		u_metallic_factor;
uniform vec3		u_sky_sh[9];      // A_l L_lm / pi

varying vec2		var_texcoord;
varying vec3		var_normal;

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

	// sky light at the vertex normal, unshadowed
	float len = length( var_normal );
	vec3 n = len > 0.0 ? var_normal / len : vec3( 0.0, 0.0, 1.0 );
	vec3 skyOverPi = u_sky_sh[0] * 0.282095
	               + u_sky_sh[1] * ( 0.488603 * n.y )
	               + u_sky_sh[2] * ( 0.488603 * n.z )
	               + u_sky_sh[3] * ( 0.488603 * n.x )
	               + u_sky_sh[4] * ( 1.092548 * n.x * n.y )
	               + u_sky_sh[5] * ( 1.092548 * n.y * n.z )
	               + u_sky_sh[6] * ( 0.315392 * ( 3.0 * n.z * n.z - 1.0 ) )
	               + u_sky_sh[7] * ( 1.092548 * n.x * n.z )
	               + u_sky_sh[8] * ( 0.546274 * ( n.x * n.x - n.y * n.y ) );
	float metallic = clamp( texture2D( u_metallicroughnessmap, var_texcoord ).b * u_metallic_factor, 0.0, 1.0 );
	radiance += ( 1.0 - metallic ) * baseColor * occlusion * max( skyOverPi, vec3( 0.0 ) );

	gl_FragColor = vec4( radiance, alpha );
}
