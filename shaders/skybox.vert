#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (location = 0) out vec3 outPosition;

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	vec3 vertices[];
};

layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	vec4 playerPosition;
	VertexBuffer vertexBuffer;
} PushConstants;


void main() {


	vec3 v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	vec4 position = vec4(v, 1.0f);

	gl_Position = sceneData.viewProj*(PushConstants.playerPosition+ position);
	//gl_Position = sceneData.viewProj * position;

	outPosition = position.xyz;
}