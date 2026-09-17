/// ============================================================================
/*
PBR lighting pass, vertex program.
One additive pass per light; lighting is evaluated in object space with the
light and viewer transformed into it by the renderer (see renderstate.cpp).

GLSL 1.20 to match the QOpenGLFunctions_2_0 binding used by the editor.
*/
/// ============================================================================

attribute vec4		attr_TexCoord0;
attribute vec3		attr_Tangent;
attribute vec3		attr_Binormal;

varying vec3		var_vertex;       // object space position
varying vec2		var_texcoord;
varying mat3		var_mat_ts2os;    // tangent space -> object space

void	main()
{
	gl_Position = ftransform();

	var_vertex = gl_Vertex.xyz;

	var_texcoord = (gl_TextureMatrix[0] * attr_TexCoord0).st;

	// columns are the tangent, bitangent and normal in object space
	var_mat_ts2os = mat3( attr_Tangent, attr_Binormal, gl_Normal );
}
