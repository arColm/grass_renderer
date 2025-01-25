
layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	vec4 playerPosition;
	vec4 data;
	VertexBuffer vertexBuffer;
} PushConstants;