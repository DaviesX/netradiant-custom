/// ============================================================================
/*
Full-screen resolve, vertex program. The quad is drawn in clip space.
*/
/// ============================================================================

varying vec2		var_texcoord;

void	main()
{
	gl_Position = gl_Vertex;
	var_texcoord = gl_Vertex.xy * 0.5 + 0.5;
}
