#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
#include "noise.glsl"
float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inPlayerPos;

#include "_fragOutput.glsl"

// inspired by https://www.shadertoy.com/view/4tdSWr
// using a similar concept of 4 layers of noise and adding to obtain opacity
void main() {
    vec2 uv = inPosition.xz * 0.02 +vec2(sceneData.time.x)*0.2;

    float n = fbm(uv+sceneData.time.y,5,1.0,0.4,1.0,0.6);
    float m= fbm(uv,6,1.0,0.4,1.0,0.6);
    float p = fbm(uv*0.6+sceneData.time.y*0.2,4,1.0,0.6,1.0,0.6);
    float q = fbm(uv*4,7,1.0,0.4,1.0,0.4);
    n += abs(p);
    m+= abs(q);


    float opacity = mix(0,n,clamp(n+m,0,1));
    
    opacity = mix(0,opacity,3*(gl_FragCoord.z*0.1));




	outFragColor = vec4(1,1,1,opacity);
    outNormal = vec4(0,-1,0,1);
	outPosition = sceneData.view * vec4(inPosition,1);
	outSpecularMap = vec4(0);
}