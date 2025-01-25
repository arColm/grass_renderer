#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "0_scene_data.glsl"
#include "noise.glsl"

//layout(rgba16f, set = 1, binding = 0) readonly uniform image3D cloudMap;
layout(set = 1, binding = 0) uniform sampler3D baseNoise;
layout(set = 1, binding = 1) uniform sampler3D detailNoise;
layout(set = 1, binding = 2) uniform sampler2D fluidNoise;
layout(set = 1, binding = 3) uniform sampler2D weatherMap;

float PI = 3.1415926;

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inPlayerPos;

#include "_fragOutput.glsl"
layout(buffer_reference, std430) readonly buffer VertexBuffer {
	vec3 vertices[];
};

#include "_pushConstantsDraw.glsl"
//layout( push_constant ) uniform constants
//{
//	float coverage;
//} PushConstants;

const float LIGHT_ABSORPTION = 0.3; //less = darker - consider using this in weather map in 1 channel
const float DARKNESS_THRESHOLD = 0.1;
//TODO : currently hard coded
const vec3 BOX_BOUNDS_MIN = vec3(-600,0.0,-600);
const vec3 BOX_BOUNDS_MAX = vec3(600,50.0,600);

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


// Fractional value for sample position in the cloud layer.
float GetHeightFractionForPoint(vec3 pos, vec2 cloudMinMax)
{
    float heightFraction = (pos.y - cloudMinMax.x) / (cloudMinMax.y - cloudMinMax.x);
    return clamp(heightFraction,0,1);
}

float sampleDensity(vec3 pos,vec3 offset)
{
    const float FREQUENCY = 8.0;
    //const vec3 size = imageSize(cloudMap);
    //return imageLoad(cloudMap,ivec3(abs(pos) - abs(pos/size))).r;

    const ivec3 size = textureSize(baseNoise, 0);
    vec3 uv = vec3(
        (pos.x+offset.x-BOX_BOUNDS_MIN.x) / (BOX_BOUNDS_MAX.x-BOX_BOUNDS_MIN.x),
        (pos.y+offset.y-BOX_BOUNDS_MIN.y) / (BOX_BOUNDS_MAX.y-BOX_BOUNDS_MIN.y),
        (pos.z+offset.z-BOX_BOUNDS_MIN.z) / (BOX_BOUNDS_MAX.z-BOX_BOUNDS_MIN.z)
    );
    vec4 weather = texture(weatherMap,uv.xz*0.87,0);
    uv *= vec3(FREQUENCY,1,FREQUENCY);
    //uv = fract(uv);
    vec4 detailNoise = texture(detailNoise,uv * 0.1,0);
    vec4 fluidNoise = texture(fluidNoise,uv.xz*1.14,0);

    vec4 lowFrequencyNoises = texture(baseNoise,uv,0);
    float lowFrequencyFbm = (lowFrequencyNoises.g * 0.625) + (lowFrequencyNoises.b * 0.25) + (lowFrequencyNoises.a * 0.125);
    float baseCloud = remap(lowFrequencyNoises.r,-(1.0-lowFrequencyFbm),1.0, 0.0, 1.0);
    
    float heightFraction = GetHeightFractionForPoint(pos,vec2(BOX_BOUNDS_MIN.y,BOX_BOUNDS_MAX.y));


    baseCloud *= heightFraction;

    //coverage
    float coverage = weather.r;
    coverage = remap(coverage,PushConstants.data.x,1,0,1);
    //coverage = clamp(coverage,0,1);
    float baseCloudWithCoverage = remap(baseCloud,coverage,1.0,0.0,1.0);
    baseCloudWithCoverage *= coverage;
    
    // adding noise
    pos.xz += fluidNoise.xy * (1.0-heightFraction);
    float highFrequencyFbm = (detailNoise.r * 0.625) + (detailNoise.g * 0.25) + (detailNoise.b * 0.125);

    heightFraction = GetHeightFractionForPoint(pos,vec2(BOX_BOUNDS_MIN.y,BOX_BOUNDS_MAX.y));
    
    float highFrequencyNoiseModifier = mix(highFrequencyFbm, 1.0-highFrequencyFbm,clamp(heightFraction * 10.0,0,1));

    float finalCloud = remap(baseCloudWithCoverage,highFrequencyNoiseModifier*0.5,1.0,0.0,1.0);

    return finalCloud;
}

float getLightStrength(vec3 raySrc, int resolution)
{
    vec3 dirToLight = -sceneData.sunlightDirection.xyz;
    float dstInsideBox = rayBoxIntersect(BOX_BOUNDS_MIN,BOX_BOUNDS_MAX,raySrc,1.0/dirToLight).y;

    float stepSize = dstInsideBox / resolution;
    float density = 0;

    vec3 pos = raySrc;

    for(int i=0;i<resolution;i++)
    {
        pos += dirToLight * stepSize;
        density += max(0,sampleDensity(pos,vec3(0))*stepSize);
    }

    float transmittance = exp(-density * LIGHT_ABSORPTION);
    return DARKNESS_THRESHOLD + transmittance * (1-DARKNESS_THRESHOLD);

    
}

vec4 getCloudColor(vec3 raySrc, vec3 rayHit, int resolution)
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
    float transmittance = 1;
    float light = 0;
    while(distanceTravelled < distanceLimit)
    {
        vec3 pos = raySrc + rayDir * (dstToBox + distanceTravelled);
        float nextDensity = sampleDensity(pos,10*vec3(sceneData.time.x,sceneData.time.x,sceneData.time.x))*stepSize;

        if(nextDensity>0)
        {
            float lightTransmittance = getLightStrength(pos,10);
            light += nextDensity * transmittance * lightTransmittance *20/resolution;
            transmittance *= exp(-nextDensity * stepSize * LIGHT_ABSORPTION);
        }
        if(transmittance < 0.01) break;

        distanceTravelled += stepSize;
    }
    light = clamp(light,0,1);
    transmittance = 1-clamp(transmittance,0,1);
    //light = 1;
    vec4 color = vec4(0,0,0,1.0) * transmittance + (vec4(sceneData.sunlightColor.xyz,0) * light);

    return color;
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
    
    //float cloud = getCloudDensity(inPlayerPos,inPosition,50);
	//outFragColor = vec4(1,1,1,cloud);

    vec4 cloudColor = getCloudColor(inPlayerPos,inPosition,50);
    outFragColor = cloudColor;

    outNormal = vec4(0,-1,0,1);
	outPosition = sceneData.view * vec4(inPosition,1);
	outSpecularMap = vec4(0);
}