#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;



void main() {
	
	vec3 color = inColor;// * texture(colorTex,inUV).xyz;

	float diffuseLight = max(dot(inNormal, -sceneData.sunlightDirection.xyz),0.0f);
	vec3 ambientLight = sceneData.ambientColor.xyz;
	
	vec3 light = ambientLight + (diffuseLight) * sceneData.sunlightDirection.w;

	outFragColor = vec4(color * light, 1.0f);
	//outFragColor = vec4(ambientLight, 1.0f);
}