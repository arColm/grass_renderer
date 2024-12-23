#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (set = 1, binding = 0) uniform sampler2D shadowMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inLightSpacePos;

layout (location = 0) out vec4 outFragColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
	
	//vec4 shadowCoord = fragPosLightSpace / fragPosLightSpace.w;
	//float shadow = 1.0;
	//if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 ) 
	//{
	//	float dist = texture( shadowMap, shadowCoord.st ).r;
	//	if ( shadowCoord.w > 0.0 && dist < shadowCoord.z ) 
	//	{
	//		shadow = 0.0;
	//	}
	//}
	//return shadow;


	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords.xy = projCoords.xy *0.5 + 0.5;
	//projCoords.y = 1 - projCoords.y;
	//return projCoords.z;

	//note we are just flooring here, could do better
	float closestDepth = texture(shadowMap,projCoords.xy).r;
	float currentDepth = projCoords.z;
	//return currentDepth;
    float bias = 0.001;

	float shadow = currentDepth  - bias > closestDepth ? 1.0 : 0.0;
    if(projCoords.z > 1.0 || projCoords.z < 0.0)
        shadow = 1.0;

	return shadow;
}

void main() {
	
	vec3 color = inColor;// * texture(colorTex,inUV).xyz;

	float diffuseLight = max(dot(inNormal, -sceneData.sunlightDirection.xyz),0.0f);
	vec3 ambientLight = vec3(0);//sceneData.ambientColor.xyz;
	
	float shadow = ShadowCalculation(inLightSpacePos);
	
	//outFragColor = vec4(shadow,shadow,shadow, 1.0f);
	//return;
	vec3 light = ambientLight + (1.0-shadow) * (diffuseLight) * sceneData.sunlightDirection.w;

	outFragColor = vec4(color * light, 1.0f);
	//outFragColor = vec4(shadow,shadow,shadow,1.0f);
	//outFragColor = vec4(light, 1.0f);
}