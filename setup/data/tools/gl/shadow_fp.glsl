/// ============================================================================
/*
Shadow caster pass, fragment program.

Colour writes are off, so this only has to decide whether the fragment occludes.
Alpha-masked materials (glTF "alphamode mask") discard below their cutoff, so a
fence casts its cut-out pattern instead of a solid rectangle. u_alpha_cutoff is
zero for every other material, which skips the texture fetch.

GLSL 1.20 to match the QOpenGLFunctions_2_0 binding used by the editor.
*/
/// ============================================================================

uniform sampler2D	u_basecolormap;
uniform float		u_alpha_cutoff;   // 0 disables the mask

varying vec2		var_texcoord;

void	main()
{
	if ( u_alpha_cutoff > 0.0 && texture2D( u_basecolormap, var_texcoord ).a < u_alpha_cutoff ) {
		discard;
	}

	gl_FragColor = vec4( 1.0 );
}
