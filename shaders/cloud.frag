#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
#include "noise.glsl"
float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;

layout (location = 0) out vec4 outFragColor;

void main() {
    vec2 uv = inPosition.xz;

    //float n = noise(uv*16 +vec2(sceneData.time));
    float n = cnoise(vec3(uv*0.16+vec2(sceneData.time),sceneData.time));
    
    float opacity = 4*(0.25-dot((uv-vec2(0.5,0.5)) * (uv-vec2(0.5,0.5)),vec2(1,1)));
    //opacity *= clamp(sunHeight*5,0.2,1.0);
	outFragColor = vec4(1) * vec4(1,1,1,n);
}