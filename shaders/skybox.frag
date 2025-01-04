#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;

#include "_fragOutput.glsl"

float EaseInOutSine(float x)
{ 
    return -(cos(PI * x) - 1.0) / 2.0;
}

//mie scattering algorithm
// from TooMuchVoltage
// https://www.reddit.com/r/GraphicsProgramming/comments/r9wofc/volumetrics_galore_now_with_100_more_mie/
// also from research paper
// https://research.nvidia.com/labs/rtr/approximate-mie/publications/approximate-mie-supplemental.pdf

float Inv4Pi = 1/(4*PI);
float PHG (float g, float cosTheta)
{
    float gSq = g * g;

    float denomPreMul = 1 + gSq - (2.0 * g * cosTheta);

    return (1 - gSq) * Inv4Pi * inversesqrt(denomPreMul * denomPreMul * denomPreMul);
}

float DrainePHG (float g, float a, float cosTheta)
{
    float gSq = g * g;

    float denomPreMul = 1 + gSq - (2.0 * g * cosTheta);

    return (1 - gSq) * Inv4Pi * inversesqrt(denomPreMul * denomPreMul * denomPreMul) * (1+a*cosTheta*cosTheta) * (1.0/(1+a*(1+2*gSq)/3));
}

float miePhase (float cosTheta)
{
    return mix (PHG (0.8, cosTheta), PHG (-0.5, cosTheta), 0.5);
}

float draineMiePhase(float cosTheta)
{
    return mix (DrainePHG (0.8, 2, cosTheta), DrainePHG (-0.5, 2, cosTheta), 0.5);
}
void main() {
	//vec3 topColor = vec3(0.906,0.984,0.988);
	//vec3 bottomColor = vec3(0.988,0.78,0.408);
	vec3 normalizedSunPos = normalize(-sceneData.sunlightDirection.xyz);

	vec3 topColorDay = vec3(0.694, 0.922, 0.914);
	vec3 bottomColorDay = vec3(1);
	//vec3 topColorNight = vec3( 0.1, 0.2, 0.4 );
	//vec3 bottomColorNight = vec3( 0.01, 0.02, 0.05 );
	vec3 topColorNight = vec3( 0.01, 0.02, 0.05 );
	vec3 bottomColorNight = vec3( 0 );

	vec3 topColor = mix(topColorNight,topColorDay,max(0,normalizedSunPos.y));
	vec3 bottomColor = mix(bottomColorNight,bottomColorDay,max(0,normalizedSunPos.y));

	//vec3 sunColor = vec3(0.961, 0.8, 0.635);
	vec3 sunColor = mix(vec3(1, 0.592, 0),vec3(0.961, 0.8, 0.635),pow(abs(normalizedSunPos.y),2)+0.3);

	vec3 normalizedPos = normalize(inPosition);
	float normalizedHeight = (normalizedPos.y+1)*0.5;

	float cosTheta = dot(normalizedPos,normalizedSunPos);
	//sky color
	vec4 color = vec4(mix(bottomColor,topColor,normalizedHeight), 1.0f);
	//sun color
	vec4 sunFragColor = mix(color,vec4(sunColor,1.0f),miePhase(max(0,cosTheta)));
	//horizon color
	vec4 horizonFragColor = mix(color, vec4(sunColor ,1.0f), mix(0,miePhase(1-max(0,normalizedPos.y)),0.7-abs(normalizedSunPos.y)));

	color = mix(sunFragColor,horizonFragColor,0.2);
	outFragColor = color;
	outNormal = vec4(-normalizedPos,1);
	outPosition = sceneData.view * vec4(inPosition*300,1);
	outSpecularMap = vec4(0);
}