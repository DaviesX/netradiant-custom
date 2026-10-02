/// ============================================================================
/*
Shadow caster pass, fragment program.

Colour writes are off, so this only has to decide whether the fragment occludes.
Alpha-tested materials discard the texels that fail their preview alpha test, so
a fence casts its cut-out pattern instead of a solid rectangle. The test is the
stage alphaFunc of a Quake 3 shader (GT0, LT128, GE128) or a .mtr material's
"alphamode mask" cutoff. u_alpha_func is 0 for every other material, which
skips the texture fetch.

GLSL 1.20 to match the QOpenGLFunctions_2_0 binding used by the editor.
*/
/// ============================================================================

uniform sampler2D	u_basecolormap;
uniform int			u_alpha_func;     // 0: no test, 1: greater, 2: less, 3: greater or equal
uniform float		u_alpha_ref;

varying vec2		var_texcoord;

void	main()
{
	if ( u_alpha_func != 0 ) {
		float alpha = texture2D( u_basecolormap, var_texcoord ).a;
		bool passes = ( u_alpha_func == 1 ) ? alpha > u_alpha_ref
		            : ( u_alpha_func == 2 ) ? alpha < u_alpha_ref
		            : alpha >= u_alpha_ref;
		if ( !passes ) {
			discard;
		}
	}

	gl_FragColor = vec4( 1.0 );
}
