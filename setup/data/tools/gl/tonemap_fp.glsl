/// ============================================================================
/*
Full-screen resolve, fragment program:
  linear HDR radiance * exposure -> Uncharted 2 (Hable) filmic tonemap -> sRGB encode
Ported from sh-renderer glsl/tonemap.frag (constants A..F, white point W = 11.2,
exposure bias 1); the engine encodes sRGB through GL_FRAMEBUFFER_SRGB instead of
in the shader, which is the same transfer function.
*/
/// ============================================================================

uniform sampler2D	u_hdr;
uniform float		u_exposure;

varying vec2		var_texcoord;

const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

vec3 uncharted2Tonemap( vec3 x )
{
	return ( ( x * ( A * x + C * B ) + D * E ) / ( x * ( A * x + B ) + D * F ) ) - E / F;
}

// exact sRGB opto-electronic transfer function
vec3 linearToSrgb( vec3 c )
{
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}

void	main()
{
	vec3 hdr = texture2D( u_hdr, var_texcoord ).rgb * u_exposure;
	vec3 curr = uncharted2Tonemap( hdr );
	vec3 whiteScale = 1.0 / uncharted2Tonemap( vec3( W ) );
	vec3 ldr = clamp( curr * whiteScale, 0.0, 1.0 );
	gl_FragColor = vec4( linearToSrgb( ldr ), 1.0 );
}
