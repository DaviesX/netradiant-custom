/// ============================================================================
/*
Full-screen resolve, fragment program:
  linear HDR radiance * exposure -> Uncharted 2 (Hable) filmic tonemap -> sRGB encode
  -> fog in display space (fog-preview)
Ported from sh-renderer glsl/tonemap.frag (constants A..F, white point W = 11.2,
exposure bias 1); the engine encodes sRGB through GL_FRAMEBUFFER_SRGB instead of
in the shader, which is the same transfer function.

Fog (fog-preview design decisions 2 and 8, the gamepack README's "Fog"): for a
pixel whose nearest depth-writing surface is not a sky, the world position is
rebuilt from the depth, the last fog volume containing it (within
eps = 1 + depth / 512) is found, and the encoded colour is blended towards the
volume's colour by f = sqrt( clamp( depth * fraction / d, 0, 1 ) ), as
opengl1 blends fog onto 8-bit pixels. With no volumes the fog code is skipped.

The pass also writes the scene's depth into the window target.
*/
/// ============================================================================

uniform sampler2D	u_hdr;
uniform sampler2D	u_depth;		// the scene's depth, written to the window target for the overlays
uniform sampler2D	u_sky_depth;	// the sky's own fragment depth, -1 where no sky was drawn
uniform float		u_exposure;

// fog volumes: one row per volume of FOG_TEX_WIDTH RGBA32F texels (libs/fogvolume.h PackVolumes):
// 0: colour, distance; 1: surface plane normal, dist; 2: has surface, plane count, t(eye); 3..: planes (normal, dist)
uniform sampler2D	u_volumes;
uniform float		u_volume_count;	// 0 skips the fog
uniform mat4		u_clip_to_world;
uniform vec3		u_eye;
uniform vec3		u_forward;

varying vec2		var_texcoord;

const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

const float FOG_TEX_WIDTH = 35.0;
const float FOG_TEX_HEIGHT = 8.0;
const int FOG_MAX_VOLUMES = 8;
const int FOG_MAX_PLANES = 32;
const float SKY_DEPTH_TOLERANCE = 1.0 / 8388608.0; // 2^-23, about two steps of a 24-bit depth buffer

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

vec4 fogTexel( int volume, int texel )
{
	return texture2D( u_volumes, vec2( ( float( texel ) + 0.5 ) / FOG_TEX_WIDTH, ( float( volume ) + 0.5 ) / FOG_TEX_HEIGHT ) );
}

// blends the encoded colour towards the fog of the last volume containing the pixel's surface
vec3 applyFog( vec3 encoded, float depth )
{
	vec4 clip = vec4( var_texcoord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0 );
	vec4 world = u_clip_to_world * clip;
	vec3 p = world.xyz / world.w;
	float viewDepth = dot( p - u_eye, u_forward );
	if ( viewDepth <= 0.0 ) {
		return encoded;
	}
	float eps = 1.0 + viewDepth / 512.0;

	int chosen = -1;
	for ( int v = 0; v < FOG_MAX_VOLUMES; ++v ) {
		if ( float( v ) >= u_volume_count ) {
			break;
		}
		float planes = fogTexel( v, 2 ).y;
		bool inside = true;
		for ( int j = 0; j < FOG_MAX_PLANES; ++j ) {
			if ( float( j ) >= planes ) {
				break;
			}
			vec4 plane = fogTexel( v, 3 + j );
			if ( dot( plane.xyz, p ) - plane.w > eps ) {
				inside = false;
				break;
			}
		}
		if ( inside ) {
			chosen = v; // the last containing volume wins
		}
	}
	if ( chosen < 0 ) {
		return encoded;
	}

	vec4 colourDistance = fogTexel( chosen, 0 );
	vec4 surface = fogTexel( chosen, 1 );
	vec4 info = fogTexel( chosen, 2 );
	float fraction = 1.0;
	if ( info.x > 0.5 ) {
		float tEye = info.z;
		float tPoint = surface.w - dot( surface.xyz, p );
		if ( tEye >= 0.0 ) {
			fraction = tPoint >= -eps ? 1.0 : 0.0;
		}
		else {
			fraction = tPoint < 1.0 ? 0.0 : tPoint / ( tPoint - tEye );
		}
	}
	float f = sqrt( clamp( viewDepth * fraction / colourDistance.w, 0.0, 1.0 ) );
	return mix( encoded, colourDistance.rgb, f );
}

void	main()
{
	vec3 hdr = texture2D( u_hdr, var_texcoord ).rgb * u_exposure;
	vec3 curr = uncharted2Tonemap( hdr );
	vec3 whiteScale = 1.0 / uncharted2Tonemap( vec3( W ) );
	vec3 ldr = clamp( curr * whiteScale, 0.0, 1.0 );
	vec3 encoded = linearToSrgb( ldr );
	float depth = texture2D( u_depth, var_texcoord ).r;

	if ( u_volume_count > 0.5 && depth < 1.0 && abs( depth - texture2D( u_sky_depth, var_texcoord ).r ) > SKY_DEPTH_TOLERANCE ) {
		encoded = applyFog( encoded, depth );
	}

	gl_FragColor = vec4( encoded, 1.0 );
	gl_FragDepth = depth;
}
