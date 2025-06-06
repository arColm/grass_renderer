#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	vec3 vertices[];
};
#include "../0_scene_data.glsl"
#include "../_pushConstantsDraw.glsl"

layout (location = 0) out vec3 outPosition;
//layout (location = 1) out vec3 outPlayerPos;



void main() {


	vec3 v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	vec4 position = vec4(v, 1.0f);

	gl_Position = sceneData.viewProj*(position);//+PushConstants.playerPosition);
	//gl_Position = sceneData.viewProj * position;

	outPosition = position.xyz;
	//outPlayerPos = PushConstants.playerPosition.xyz;
}