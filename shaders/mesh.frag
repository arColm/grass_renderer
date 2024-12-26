#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (set = 1, binding = 0) uniform sampler2D shadowMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inLightSpacePos;
layout (location = 4) in vec3 inPlayerPos;
layout (location = 5) in vec3 inFragPos;

layout (location = 0) out vec4 outFragColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords.xy = projCoords.xy *0.5 + 0.5;

	float currentDepth = projCoords.z;
	//TODO we need a low bias for the grass shadows, but a higher bias to prevent moire on mesh --> different fragment shader?
    float bias = 0.0001; 
	float shadow = 0;
	vec2 texelSize = 1.0/textureSize(shadowMap,0);
	for(int x=-1;x<=1;x++)
	{
		for(int y=-1;y<=1;y++)
		{
			float closestDepth = texture(shadowMap,projCoords.xy+vec2(x,y)*texelSize).r;
			shadow += currentDepth  - bias > closestDepth ? 0.8 : 0.0;
		}
	}
	shadow/=9.0;
    if(projCoords.z > 1.0 || projCoords.z < 0.0)
        shadow = 0.8;

	return shadow;
}

void main() {
	vec3 viewDir    = normalize(inPlayerPos-inFragPos);
	vec3 halfwayDir = normalize(-sceneData.sunlightDirection.xyz + viewDir);
	
	vec3 color = inColor;// * texture(colorTex,inUV).xyz;

	float diffuseLight = max(dot(inNormal, -sceneData.sunlightDirection.xyz),0.3f);
	vec3 ambientLight = vec3(0.1);//sceneData.ambientColor.xyz;
	vec3 specularLight = vec3(1)*pow(max(dot(inNormal, halfwayDir), 0.0), 16);
	
	float shadow = ShadowCalculation(inLightSpacePos);
	
	vec3 light = specularLight + ambientLight + (1.0-shadow) * (diffuseLight) * sceneData.sunlightDirection.w;
	//light = specularLight;

	outFragColor = vec4(color * light, 1.0f);
	//outFragColor = vec4(color,1.0f);
}