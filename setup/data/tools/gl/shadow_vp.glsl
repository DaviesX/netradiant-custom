/// ============================================================================
/*
Shadow caster pass, vertex program: position only, plus the texture coordinate
the fragment program needs for alpha-masked materials.

GLSL 1.20 to match the QOpenGLFunctions_2_0 binding used by the editor.
*/
/// ============================================================================

attribute vec4		attr_TexCoord0;

varying vec2		var_texcoord;

void	main()
{
	gl_Position = ftransform();

	var_texcoord = ( gl_TextureMatrix[0] * attr_TexCoord0 ).st;
}
