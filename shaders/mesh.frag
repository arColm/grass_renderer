#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

const int SHADOW_CASCADE_COUNT = 3;

layout (set = 1, binding = 0) uniform sampler2DArray shadowMaps;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPlayerPos;
layout (location = 4) in vec3 inPos;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outSpecularMap;

float ShadowCalculation() 
{
	vec3 projCoords = vec3(2);
	int cascade = 4;
	for(int i=0;i<SHADOW_CASCADE_COUNT;i++)
	{
		if(projCoords.x>1.0 || projCoords.x<0.0 || projCoords.y > 1.0 || projCoords.y < 0.0)
		{
			vec4 fragPosLightSpace = sceneData.sunViewProj[i] * vec4(inPos,1.0);
			projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
			projCoords.xy = projCoords.xy *0.5 + 0.5;
			cascade = i;
		}
	}

	float currentDepth = projCoords.z;
	//TODO we need a low bias for the grass shadows, but a higher bias to prevent moire on mesh --> different fragment shader?
    float bias = 0.0001; 
	float shadow = 0;
	vec2 texelSize = 1.0/textureSize(shadowMaps,0).xy;
	for(int x=-1;x<=1;x++)
	{
		for(int y=-1;y<=1;y++)
		{
			float closestDepth = texture(shadowMaps,vec3(projCoords.xy+vec2(x,y)*texelSize,cascade)).r; //z is the array indictaor
			shadow += currentDepth  - bias > closestDepth ? 0.4 : 0.0;
		}
	}
	shadow/=9.0;
	//shadow = float(cascade)/SHADOW_CASCADE_COUNT;
	//shadow = texture(shadowMaps,vec3(projCoords.xy*texelSize,2)).r;
	//shadow = currentDepth;
    //if(projCoords.z > 1.0 || projCoords.z < 0.0)
    //    shadow = 0.0;

	return shadow;
}

void main() {
	vec3 viewDir    = normalize(inPlayerPos-inPos);
	vec3 halfwayDir = normalize(-sceneData.sunlightDirection.xyz + viewDir);
	
	vec3 color = inColor;// * texture(colorTex,inUV).xyz;

	float diffuseLight = max(dot(inNormal, normalize(-sceneData.sunlightDirection.xyz)),0.3f);
	vec3 ambientLight = vec3(0.1);//sceneData.ambientColor.xyz;
	vec3 specularLight = vec3(1)*pow(max(dot(inNormal, halfwayDir), 0.0), 16);
	
	float shadow = ShadowCalculation();
	
	vec3 light = ambientLight + (1.0-shadow) * (diffuseLight+specularLight) * sceneData.sunlightDirection.w;
	//light = vec3(1.0-shadow);

	outFragColor = vec4(color * light, 1.0f);
	//outFragColor = vec4(color,1.0f);
	//outFragColor = vec4(light,1.0f);

	outNormal = vec4(inNormal,1);
	outSpecularMap = vec4(0);
}