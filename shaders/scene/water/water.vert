#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "../../0_scene_data.glsl"
#include "../../noise.glsl"

layout (set = 2, binding = 0) uniform sampler2D displacementTex;
layout (set = 2, binding = 1) uniform sampler2D derivativesTex;
layout (set = 2, binding = 2) uniform sampler2D turbulenceTex;
layout (set = 2, binding = 3) uniform sampler2D depthTex;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec4 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outCameraPos;
layout (location = 4) out vec3 outPos;

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

#include "../../_pushConstantsDraw.glsl"

void main() {
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	vec4 position = vec4(v.position, 1.0f);

	vec4 displacement = texture(displacementTex, vec2(v.uv_x,v.uv_y));
	vec4 derivatives = texture(derivativesTex, vec2(v.uv_x,v.uv_y));
	position.xyz += displacement.xyz;

	gl_Position = sceneData.viewProj * PushConstants.render_matrix * position;
	//gl_Position = sceneData.viewProj * position;

	//outNormal = (PushConstants.render_matrix * vec4(v.normal, 0.f)).xyz;
	vec2 slope = vec2(derivatives.x, derivatives.z);
	vec3 normal = normalize(vec3(-slope.x, 1, -slope.y));
	outNormal = normalize((PushConstants.render_matrix * vec4(normal, 1.f)).xyz);
	outColor = v.color;// * materialData.colorFactors.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
	
	outCameraPos = PushConstants.playerPosition.xyz;
	outPos = position.xyz;
}