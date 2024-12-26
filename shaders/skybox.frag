#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;

layout (location = 0) out vec4 outFragColor;

float EaseInOutSine(float x)
{ 
    return -(cos(PI * x) - 1.0) / 2.0;
}

//mie scattering algorithm
// from TooMuchVoltage
// https://www.reddit.com/r/GraphicsProgramming/comments/r9wofc/volumetrics_galore_now_with_100_more_mie/

float Inv4Pi = 1/(4*PI);
float PHG (float g, float cosTheta)
{
    float gSq = g * g;

    float denomPreMul = 1 + gSq - (2.0 * g * cosTheta);

    return (1 - gSq) * Inv4Pi * inversesqrt(denomPreMul * denomPreMul * denomPreMul);
}

float miePhase (float cosTheta)
{
    return mix (PHG (0.8, cosTheta), PHG (-0.5, cosTheta), 0.5);
}
void main() {
	//vec3 topColor = vec3(0.906,0.984,0.988);
	//vec3 bottomColor = vec3(0.988,0.78,0.408);
	vec3 topColor = vec3(0.694, 0.922, 0.914);
	vec3 bottomColor = vec3(1);
	vec3 sunColor = vec3(0.961, 0.8, 0.635);

	vec3 normalizedPos = normalize(inPosition);
	float normalizedHeight = (normalizedPos.y+1)*0.5;

	//sky color
	vec4 color = vec4(mix(bottomColor,topColor,normalizedHeight), 1.0f);

	//sun color
	color = mix(color,vec4(sunColor,1.0f),miePhase(dot(normalizedPos,normalize(-sceneData.sunlightDirection.xyz))));
	outFragColor = color;
}