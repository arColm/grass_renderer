#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "../../0_scene_data.glsl"

const int SHADOW_CASCADE_COUNT = 3;

layout (set = 1, binding = 0) uniform sampler2DArray shadowMaps;

layout (set = 2, binding = 0) uniform sampler2D displacementTex;
layout (set = 2, binding = 1) uniform sampler2D derivativesTex;
layout (set = 2, binding = 2) uniform sampler2D turbulenceTex;
layout (set = 2, binding = 3) uniform sampler2D depthTex;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPlayerPos;
layout (location = 4) in vec3 inPos;

#include "../../_fragOutput.glsl"

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

float linearDepth(float depth)
{
	float nearPlane = 0.1f;
	float farPlane = 300.0f;
	float z = depth * 2.0f - 1.0f; 
	return (2.0f * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));	
}
void main() {
	outPosition = sceneData.view * vec4(inPos,1);
	vec4 p = sceneData.proj * outPosition;
	float depth = texture(depthTex,(1+p.xy/p.w)/2).x;
	float depthDiff = 1000*(depth-p.z/p.w);

	float foamBias = 0.3;
    float turbulence = texture(turbulenceTex, inUV).x;
    vec4 derivatives = texture(derivativesTex, inUV);
    float Dydx = derivatives.x;
    float Dxdx = derivatives.y;
    float Dydz = derivatives.z;
    float Dzdz = derivatives.w;


	vec3 viewDir    = normalize(inPlayerPos-inPos);
	vec3 halfwayDir = normalize(-sceneData.sunlightDirection.xyz + viewDir);
	
	vec4 color = inColor;// * texture(colorTex,inUV).xyz;

    float Jxx = 1 + Dxdx;
    float Jzz = 1 + Dzdz;
    float Jxz = turbulence;
    float jacobian = Jxx * Jzz - Jxz * Jxz;
    float foam = clamp(-jacobian-foamBias,0,1);
	foam = max(foam,(1-clamp(depthDiff,0,1))*1);
	const vec4 FoamColor = vec4(1.0,1.0,1.0,1.0);
    if (foam > 0) color = mix(color, FoamColor, pow(foam, 5));

	float diffuseLight = max(dot(inNormal, normalize(-sceneData.sunlightDirection.xyz)),0.3f);
	vec3 ambientLight = vec3(0.1);//sceneData.ambientColor.xyz;
	vec3 specularLight = vec3(4)*pow(max(dot(inNormal, halfwayDir), 0.0), 256);
	if(foam>0) 
	{
		//diffuseLight = 1;//mix(diffuseLight, 1, foam);
		specularLight = mix(specularLight, vec3(0), foam);
	}
	
	float shadow = ShadowCalculation();
	
	vec3 light = ambientLight + (1.0-shadow) * (diffuseLight+specularLight) * sceneData.sunlightDirection.w;

	color.a = color.a+foam;
	outFragColor = vec4(color.xyz * light, color.a);
	//outFragColor = vec4(light,1.0f);

	outNormal = vec4(inNormal,1);
	//outNormal = vec4(0,1,0,1);
	outSpecularMap = vec4(1-foam);
	//outSpecularMap = vec4(0);
	//outFragColor = vec4(vec3(foam),1);
}