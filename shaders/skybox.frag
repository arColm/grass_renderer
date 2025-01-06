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

#include "noise.glsl"
//uses voronoi noise from https://thebookofshaders.com/12/
vec4 starColor(float cosTheta, float cosPhi)
{
	vec2 i = vec2(floor(cosTheta),floor(cosPhi));
	vec2 f = vec2(fract(cosTheta),fract(cosPhi));
	
    float m_dist = 1.;  // minimum distance

	for (int y= -1; y <= 1; y++) 
	{
		for (int x= -1; x <= 1; x++) 
		{
			// Neighbor place in the grid
			vec2 neighbor = vec2(float(x),float(y));

			// Random position from current + neighbor place in the grid
			vec2 point = hash(i + neighbor);

			// Animate the point
			//point = 0.5 + 0.5*sin(u_time + 6.2831*point);
			point = 0.5 + 0.5*sin(6.2831*point);

			// Vector between the pixel and the point
			vec2 diff = neighbor + point - f;

			// Distance to the point
			float dist = length(diff);

			// Keep the closer distance
			m_dist = min(m_dist, dist);
        }
    }
	vec3 color = vec3(0);
    // Draw the min distance (distance field)
    //color += m_dist;

    // Draw cell center
    color += 1.-step(.02, m_dist);

	return vec4(color,1);
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

	float sunCosTheta = dot(normalizedPos,normalizedSunPos);
	//sky color
	vec4 color = vec4(mix(bottomColor,topColor,normalizedHeight), 1.0f);
	//sun color
	vec4 sunFragColor = mix(color,vec4(sunColor,1.0f),miePhase(max(0,sunCosTheta)));
	//horizon color
	vec4 horizonFragColor = mix(color, vec4(sunColor ,1.0f), mix(0,miePhase(1-max(0,normalizedPos.y)),0.7-abs(normalizedSunPos.y)));

	//MOON
	vec3 normalizedMoonPos = vec3(-normalizedSunPos.xy,normalizedSunPos.z);
	vec3 moonColor = mix(vec3(0.294,0.294,0.322),vec3(0.784,0.769,0.969),pow(abs(normalizedMoonPos.y),2)+0.3);
	float moonCosTheta = dot(normalizedPos,normalizedMoonPos);
	
	vec4 moonFragColor = mix(color,vec4(moonColor,1.0f),miePhase(max(0,moonCosTheta)));

	//STARS
	float cosTheta = normalizedPos.z;
	float cosPhi = sign(normalizedPos.y)*normalizedPos.x/length(normalizedPos.xy+0.4) + sceneData.time.x*0.03;//max(sign(normalizedPos.y),1);


	color = mix(sunFragColor,horizonFragColor,0.2);
	color += mix(vec4(0),starColor(cosTheta*40,cosPhi*40),clamp(normalizedMoonPos.y*2,0,1));
	color = mix(color,moonFragColor,max(normalizedMoonPos.y,0));
	outFragColor = color;
	outNormal = vec4(-normalizedPos,1);
	outPosition = sceneData.view * vec4(inPosition*300,1);
	outSpecularMap = vec4(0);
}