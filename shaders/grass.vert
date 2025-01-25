#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"

layout (std140,set = 2, binding = 0) readonly buffer GrassData {
	vec4 positions[];
} grassData;

layout(rgba16f,set = 2, binding = 1) readonly uniform image2D WindMap;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outCameraPos;
layout (location = 4) out vec3 outPos;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

#include "_pushConstantsDraw.glsl"

#include "noise.glsl"

mat3 getGrassRotationMatrix(vec3 a,vec3 b) {
	mat3 matrix;
	a.y = 0;
	b.y =0 ;
	float c = dot(a,b);
	float s =  (a.z * b.x - a.x * b.z);
	matrix[0] = vec3(c,0,-s);
	matrix[1] = vec3(0,1,0);
	matrix[2] = vec3(s,0,c);
	return matrix;
}

float getWindStrength(float height) {
	return clamp(height*height,0,2);
}

vec3 getWindDirection(vec3 grassBladePosition) {
    vec3 i = floor(grassBladePosition);
    vec3 f = fract(grassBladePosition);
	ivec2 mapCenter = imageSize(WindMap)/2;
    // Four corners in 2D of a tile
    vec3 a = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)).xyz;
    vec3 b = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(1,0)).xyz;
    vec3 c = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(0,1)).xyz;
    vec3 d = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(1,1)).xyz;

    // Smooth Interpolation

    // Cubic Hermine Curve.  Same as SmoothStep()
    vec3 u = f*f*(3.0-2.0*f);

    // Mix 4 coorners percentages
    return mix(a, b, u.x) +
            (c - a)* u.z * (1.0 - u.x) +
            (d - b) * u.x * u.z;
}

mat3 getGrassRotationMatrix(vec3 vNormal,vec3 playerPosition, vec3 grassPosition) {
	vec3 a = vNormal;
	vec3 b = normalize((playerPosition + 10*(random(grassPosition.xz)-0.5))-grassPosition);
	mat3 matrix;
	a.y = 0;
	b.y =0 ;
	float c = dot(a,b);
	float s =  (a.z * b.x - a.x * b.z);
	matrix[0] = vec3(c,0,-s);
	matrix[1] = vec3(0,1,0);
	matrix[2] = vec3(s,0,c);
	return matrix;
}

void main() {
	vec3 grassBladePosition = grassData.positions[gl_InstanceIndex].xyz;
	vec3 windDirection = getWindDirection(grassBladePosition);

	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	mat3 rotationTowardsPlayer = getGrassRotationMatrix(v.normal,PushConstants.playerPosition.xyz,grassData.positions[gl_InstanceIndex].xyz);
	vec3 relativePosition = rotationTowardsPlayer * v.position;
	vec3 position = relativePosition + grassBladePosition;

	vec3 windOffset = (windDirection * getWindStrength(v.position.y));
	windOffset.y += -length(windOffset)*0.5;
	position+= windOffset;

	gl_Position = sceneData.viewProj * PushConstants.render_matrix * vec4(position,1.0);

	outNormal = normalize((PushConstants.render_matrix * -vec4(
		sceneData.sunlightDirection.x-(v.position.x+windOffset.x*3),
		0.3*abs(random(grassBladePosition.xz)),
		sceneData.sunlightDirection.z-(v.position.x+windOffset.z*3),
		0)).xyz);

	outColor = mix(v.color.xyz*0.8,v.color.xyz*1.2,(clamp(rnoise(position.xz*0.02),0,1)));
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	outCameraPos = PushConstants.playerPosition.xyz;
	outPos = position;
}