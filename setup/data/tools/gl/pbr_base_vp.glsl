/// ============================================================================
/*
PBR base pass, vertex program.
Drawn once per surface before the light passes; also fills the depth buffer.
Uses the fixed-function texcoord array, so it works without the tangent frame.
*/
/// ============================================================================

varying vec2		var_texcoord;

void	main()
{
	gl_Position = ftransform();
	var_texcoord = (gl_TextureMatrix[0] * gl_MultiTexCoord0).st;
}
