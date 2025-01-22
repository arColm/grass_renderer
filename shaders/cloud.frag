#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
#include "noise.glsl"

//layout(rgba16f, set = 1, binding = 0) readonly uniform image3D cloudMap;
layout(set = 1, binding = 0) uniform sampler3D cloudMap;

float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inPlayerPos;

#include "_fragOutput.glsl"

//inspired from https://github.com/SebLague/Clouds
vec2 rayBoxIntersect(vec3 boundsMin, vec3 boundsMax, vec3 rayOrigin, vec3 rayDir)
{
    vec3 t0 = (boundsMin - rayOrigin) / rayDir;
    vec3 t1 = (boundsMax - rayOrigin) / rayDir;

    vec3 tmin = min(t0,t1);
    vec3 tmax = max(t0,t1);

    float dstA = max(max(tmin.x,tmin.y),tmin.z);
    float dstB = min(min(tmax.x,tmax.y),tmax.z);

    float dstToBox = max(0,dstA);
    float dstInsideBox = max(0,dstB - dstToBox);
    return vec2(dstToBox, dstInsideBox);
}

//TODO : currently hard coded
const vec3 BOX_BOUNDS_MIN = vec3(-1200,50.9,-1200);
const vec3 BOX_BOUNDS_MAX = vec3(1200,80.9,1200);

float sampleDensity(vec3 pos)
{
    //const vec3 size = imageSize(cloudMap);
    //return imageLoad(cloudMap,ivec3(abs(pos) - abs(pos/size))).r;

    const ivec3 size = textureSize(cloudMap, 0);
    vec3 uv = vec3(
        (pos.x-BOX_BOUNDS_MIN.x) / (BOX_BOUNDS_MAX.x-BOX_BOUNDS_MIN.x),
        (pos.y-BOX_BOUNDS_MIN.y) / (BOX_BOUNDS_MAX.y-BOX_BOUNDS_MIN.y),
        (pos.z-BOX_BOUNDS_MIN.z) / (BOX_BOUNDS_MAX.z-BOX_BOUNDS_MIN.z)
    );
    uv = fract(uv);
    return texture(cloudMap,uv).r;
}

float getCloudDensity(vec3 raySrc, vec3 rayHit, int resolution)
{
    float density = 0;
    vec3 rayDir = normalize(rayHit-raySrc);

    vec2 boxIntersectInfo = rayBoxIntersect(BOX_BOUNDS_MIN,BOX_BOUNDS_MAX,raySrc,rayDir);
    float dstToBox = boxIntersectInfo.x;
    float dstInsideBox = boxIntersectInfo.y;

    float distanceTravelled = 0;
    float stepSize = dstInsideBox / resolution;
    float distanceLimit = min(distance(rayHit,raySrc),dstInsideBox);

    //if(dstToBox==0) return 0;

    //for(int i = 0; i<resolution;i++)
    while(distanceTravelled < distanceLimit)
    {
        vec3 pos = raySrc + rayDir * (dstToBox + distanceTravelled);
        density += sampleDensity(pos +100*vec3(sceneData.time.x,0,sceneData.time.x))*stepSize;
        distanceTravelled += stepSize;
    }

    density = exp(-density);

    return density;
}

float fbmClouds()
{
    vec2 uv = inPosition.xz * 0.02 +vec2(sceneData.time.x)*0.2;

    float n = fbm(uv+sceneData.time.y,5,1.0,0.4,1.0,0.6);
    float m= fbm(uv,6,1.0,0.4,1.0,0.6);
    float p = fbm(uv*0.6+sceneData.time.y*0.2,4,1.0,0.6,1.0,0.6);
    float q = fbm(uv*4,7,1.0,0.4,1.0,0.4);
    n += abs(p);
    m+= abs(q);


    float opacity = mix(0,n,clamp(n+m,0,1));
    
    opacity = mix(0,opacity,3*(gl_FragCoord.z*0.1));
    return opacity;
}
// inspired by https://www.shadertoy.com/view/4tdSWr
// using a similar concept of 4 layers of noise and adding to obtain opacity
void main() {
    
    float cloud = getCloudDensity(inPlayerPos,inPosition,50);

	outFragColor = vec4(1,1,1,cloud);
    outNormal = vec4(0,-1,0,1);
	outPosition = sceneData.view * vec4(inPosition,1);
	outSpecularMap = vec4(0);
}