
/*
* SET 0 - GLOBAL SCENE DATA
*/
layout (set = 0, binding = 0) uniform SceneData {
	mat4 view;
	mat4 proj;
	mat4 viewProj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sunlight strength
	vec4 sunlightColor;
    mat4 sunViewProj[3];
	vec4 time; //.x = time, .y = time/2
} sceneData;