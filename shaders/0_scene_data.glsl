
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
} sceneData;