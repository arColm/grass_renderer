#version 460
#extension GL_EXT_buffer_reference : require
//#extension GL_KHR_vulkan_glsl : enable

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 outUV;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

//declare the VertexBuffer as a readonly buffer
//	containing an array of Vertex
//		buffer_reference tells the shader that the object is used from buffer address - a uint64 handle
//		std430 is an alignment rule for the structure
layout(buffer_reference, std430) readonly buffer VertexBuffer {
	Vertex vertices[];
};

//push constants
layout ( push_constant) uniform constants 
{
	mat4 render_matrix;
	VertexBuffer vertexBuffer;
} PushConstants;

void main()
{
	//load vertex data from device address
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	//output data
	gl_Position = PushConstants.render_matrix * vec4(v.position,1.0f);
	outColor = v.color.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
}