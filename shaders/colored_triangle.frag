#version 460
//#extension GL_KHR_vulkan_glsl : enable

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;


void main()
{
	outFragColor = vec4(inColor,0.5);
}