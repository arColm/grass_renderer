#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
#include "noise.glsl"

layout(rgba16f,set = 2, binding = 0) readonly uniform image2D WindMap;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec4 outColor;
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

layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	vec4 playerPosition;
	VertexBuffer vertexBuffer;
} PushConstants;

vec3 getWindDirection(vec3 pos) {
	//return imageLoad(WindMap,imageSize(WindMap)/2+ivec2(pos.x,pos.z)).xyz;
    vec3 i = floor(pos);
    vec3 f = fract(pos);
	ivec2 mapCenter = imageSize(WindMap)/2;
    // Four corners in 2D of a tile
    vec3 a = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)).xyz;
    vec3 b = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(1,0)).xyz;
    vec3 c = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(0,1)).xyz;
    vec3 d = imageLoad(WindMap,mapCenter+ivec2(i.x,i.z)+ivec2(1,1)).xyz;

    // Smooth Interpolation

    // Cubic Hermine Curve.  Same as SmoothStep()
    vec3 u = f*f*(3.0-2.0*f);
    //vec3 u = mix(vec3(0.),vec3(1.),f);

    // Mix 4 coorners percentages
    return mix(a, b, u.x) +
            (c - a)* u.z * (1.0 - u.x) +
            (d - b) * u.x * u.z;
}

void main() {
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	vec4 position = vec4(v.position, 1.0f);

    //float wave =-layeredNoise(position.xz +vec2(sceneData.time.x),5,1.0);
    //float wave =-getWindDirection(position.xyz).y;
	//position.y += wave;
    vec3 wave =-getWindDirection(position.xyz);
	position.xyz += wave; //idk but i think this looks better than just changing height some how ???

	gl_Position = sceneData.viewProj * PushConstants.render_matrix * position;
	//gl_Position = sceneData.viewProj * position;

	//outNormal = (PushConstants.render_matrix * vec4(v.normal, 0.f)).xyz;
	outNormal = normalize((PushConstants.render_matrix * vec4(wave.y,1-2*wave.y,-wave.y, 1.f)).xyz);
	outColor = v.color;// * materialData.colorFactors.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
	
	outCameraPos = PushConstants.playerPosition.xyz;
	outPos = position.xyz;
}