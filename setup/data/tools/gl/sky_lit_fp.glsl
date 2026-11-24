/// ============================================================================
/*
Lighting-mode sky, fragment program (fog-preview design decision 3).

The sky shows its own bytes: c = the 8-bit texel x the sky stage's rgbGen const
(white for a farbox), as stock modulates them. Into the HDR target it writes
the value the resolve (tonemap_fp.glsl) maps back to c: decode sRGB, scale by
the tonemap's white U(W), invert the Hable curve and divide by exposure. It also
writes its fragment depth to the sky attachment, so that the resolve leaves sky
pixels unfogged unless an opaque surface was drawn in front.

Without the HDR target (u_hdr = 0, the fallback) it writes c to the window.
*/
/// ============================================================================

#ifdef FARBOX
uniform samplerCube	u_image;
#else
uniform sampler2D	u_image;
#endif
uniform vec3		u_colour;	// the sky stage's rgbGen const, white for a farbox
uniform float		u_exposure;
uniform float		u_hdr;		// 1 when the HDR target is bound

varying vec3		var_direction;
varying vec2		var_texcoord;

// the resolve's Uncharted 2 (Hable) constants and white point
const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

float uncharted2Tonemap( float x )
{
	return ( ( x * ( A * x + C * B ) + D * E ) / ( x * ( A * x + B ) + D * F ) ) - E / F;
}

// exact sRGB electro-optical transfer function
vec3 srgbToLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}

void	main()
{
#ifdef FARBOX
	// the cubemap's orientation fix of skybox_fp.glsl
	vec3 texel = textureCube( u_image, vec3( -var_direction.y, var_direction.z, var_direction.x ) ).rgb;
#else
	vec3 texel = texture2D( u_image, var_texcoord ).rgb;
#endif
	vec3 c = clamp( texel * u_colour, 0.0, 1.0 );
	if ( u_hdr < 0.5 ) {
		gl_FragData[0] = vec4( c, 1.0 );
		return;
	}

	// U(x) = y solves A (1 - z) x^2 + B (C - z) x + D (E - z F) = 0 with z = y + E / F; c <= 0, so take the non-negative root
	vec3 y = srgbToLinear( c ) * uncharted2Tonemap( W );
	vec3 z = y + E / F;
	vec3 qa = A * ( 1.0 - z );
	vec3 qb = B * ( C - z );
	vec3 qc = D * ( E - z * F );
	vec3 x = ( -qb + sqrt( max( qb * qb - 4.0 * qa * qc, 0.0 ) ) ) / ( 2.0 * qa );

	gl_FragData[0] = vec4( x / u_exposure, 1.0 );
	gl_FragData[1] = vec4( gl_FragCoord.z, 0.0, 0.0, 1.0 );
}
