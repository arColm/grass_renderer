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

    float n = fbm(uv*0.05 +vec2(sceneData.time),5,1.0,0.1,1.0,0.4);
    //float n = fbm(uv*0.16 +vec2(sceneData.time),5,);
    //float n = cnoise(vec3(uv*0.16+vec2(sceneData.time),1));
    
	outFragColor = vec4(1) * vec4(1,1,1,n);
}