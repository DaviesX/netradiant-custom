/// ============================================================================
/*
Lighting-mode sky, vertex program (fog-preview design decision 3). The farbox
variant (FARBOX defined) looks up the cubemap along the view direction, as
skybox_vp.glsl does; the cloud-layer variant samples the sky stage's image at
the face's texture coordinates.
*/
/// ============================================================================

uniform vec3		u_view_origin;

varying vec3		var_direction;
varying vec2		var_texcoord;

void	main()
{
	var_direction = gl_Vertex.xyz - u_view_origin;
	var_texcoord = gl_MultiTexCoord0.st;
	gl_Position = ftransform();
}
