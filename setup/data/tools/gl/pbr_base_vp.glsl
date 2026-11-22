#version 120
/// ============================================================================
/*
PBR base pass, vertex program.
Drawn once per surface before the light passes; also fills the depth buffer.
Uses the fixed-function texcoord and normal arrays, so it works without the tangent frame.
The normal is turned back to world space for the sky term: gl_NormalMatrix holds the object transform
times the view, and u_eye_to_world is the transpose of the view's rotation.
gl_Position is invariant so that the extra normal work leaves its rounding, and so the light passes' GL_LEQUAL
depth test against this pass, exactly as without it.
*/
/// ============================================================================

invariant gl_Position;

uniform mat3		u_eye_to_world;

varying vec2		var_texcoord;
varying vec3		var_normal;    // world space, not normalised

void	main()
{
	gl_Position = ftransform();
	var_texcoord = (gl_TextureMatrix[0] * gl_MultiTexCoord0).st;
	var_normal = u_eye_to_world * ( gl_NormalMatrix * gl_Normal );
}
