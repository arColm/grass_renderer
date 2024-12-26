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

void main() {
	//vec3 topColor = vec3(0.906,0.984,0.988);
	//vec3 bottomColor = vec3(0.988,0.78,0.408);
	vec3 topColor = vec3(0.694, 0.922, 0.914);
	vec3 bottomColor = vec3(1);

	//outFragColor = vec4(mix(bottomColor,topColor,EaseInOutSine((normalize(inPosition).y+1)/2)), 1.0f);
	outFragColor = vec4(mix(bottomColor,topColor,(normalize(inPosition).y+1)/2), 1.0f);
}