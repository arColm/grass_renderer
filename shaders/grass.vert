#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (std140,set = 1, binding = 0) readonly buffer GrassData {
	vec4 positions[];
} grassData;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	vec4 playerPosition;
	VertexBuffer vertexBuffer;

} PushConstants;


mat3 getGrassRotationMatrix(vec3 a,vec3 b) {
	mat3 matrix;
	a.y = 0;
	b.y =0 ;
	float c = dot(a,b);
	//float s = length(cross(a,b));
	float s =  a.z * b.x - a.x * b.z;
	matrix[0] = vec3(c,0,-s);
	matrix[1] = vec3(0,1,0);
	matrix[2] = vec3(s,0,c);
	return matrix;
}

void main() {
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	mat3 rotationTowardsPlayer = getGrassRotationMatrix(v.normal,normalize(PushConstants.playerPosition.xyz-grassData.positions[gl_InstanceIndex].xyz));
	vec3 relativePosition = rotationTowardsPlayer * v.position;
	vec3 position = relativePosition + grassData.positions[gl_InstanceIndex].xyz;

	//position = PushConstants.playerPosition.xyz+vec3(1,0,1) + v.position;

	gl_Position = sceneData.viewProj * PushConstants.render_matrix * vec4(position,1.0);
	//gl_Position = sceneData.viewProj * position;

	outNormal = (PushConstants.render_matrix * vec4(v.normal, 0.f)).xyz;
	outColor = v.color.xyz;// * materialData.colorFactors.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
}