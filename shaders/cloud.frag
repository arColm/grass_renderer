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

#include "_fragOutput.glsl"
layout(buffer_reference, std430) readonly buffer VertexBuffer {
	vec3 vertices[];
};

#include "_pushConstantsDraw.glsl"
//layout( push_constant ) uniform constants
//{
//	float coverage;
//} PushConstants;

const float LIGHT_ABSORPTION = 9.1; //greater = darker - consider using this in weather map in 1 channel
const float DARKNESS_THRESHOLD = 0.5;
//TODO : currently hard coded
const vec3 BOX_BOUNDS_MIN = vec3(-600,80.0,-600);
const vec3 BOX_BOUNDS_MAX = vec3(600,200.0,600);

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

float GetDensityHeightGradientForPoint(float heightFraction)
{
    //return -pow(2*heightFraction-1,2)+1;
    return 2.0*exp(-heightFraction) * (1.0-exp(-heightFraction*2));
}

// Fractional value for sample position in the cloud layer.
float GetHeightFractionForPoint(vec3 pos, vec2 cloudMinMax)
{
    float heightFraction = (pos.y - cloudMinMax.x) / (cloudMinMax.y - cloudMinMax.x);
    return clamp(heightFraction,0,1);
}

//from Real-Time Volumetric Cloudscapes GPU Chapter 4
float HenyeyGreenstein(vec3 dirToLight, vec3 viewDir, float G)
{
    //G = [0,1] - typically 0.2-0.4 is fine
    float cosAngle = dot(dirToLight,viewDir);
    return ( (1.0-G*G)/pow((1.0+G*G-2.0*G*cosAngle),3.0/2.0) )/4.0*3.1415;
}

float sampleDensity(vec3 pos,vec3 offset)
{
    const float FREQUENCY = 8.0;
    //const vec3 size = imageSize(cloudMap);
    //return imageLoad(cloudMap,ivec3(abs(pos) - abs(pos/size))).r;

    float heightFraction = GetHeightFractionForPoint(pos,vec2(BOX_BOUNDS_MIN.y,BOX_BOUNDS_MAX.y));
    float densityHeightGradient = GetDensityHeightGradientForPoint(heightFraction);

    pos += vec3(10,0,10) * heightFraction;
    const ivec3 size = textureSize(baseNoise, 0);
    vec3 uv = vec3(
        (pos.x+offset.x-BOX_BOUNDS_MIN.x) / (BOX_BOUNDS_MAX.x-BOX_BOUNDS_MIN.x),
        (pos.y+offset.y-BOX_BOUNDS_MIN.y) / (BOX_BOUNDS_MAX.y-BOX_BOUNDS_MIN.y),
        (pos.z+offset.z-BOX_BOUNDS_MIN.z) / (BOX_BOUNDS_MAX.z-BOX_BOUNDS_MIN.z)
    );
    vec4 weather = texture(weatherMap,uv.xz*0.67,0);
    uv *= vec3(FREQUENCY,1,FREQUENCY);
    //uv = fract(uv);
    vec4 detailNoise = texture(detailNoise,uv * 0.1,0);
    vec4 fluidNoise = texture(fluidNoise,uv.xz*1.14,0);

    vec4 lowFrequencyNoises = texture(baseNoise,uv,0);
    float lowFrequencyFbm = (lowFrequencyNoises.g * 0.625) + (lowFrequencyNoises.b * 0.25) + (lowFrequencyNoises.a * 0.125);
    float baseCloud = remap(lowFrequencyNoises.r,-(1.0-lowFrequencyFbm),1.0, 0.0, 1.0);
    


    baseCloud *= densityHeightGradient;
    //coverage
    float coverage = weather.r;
    coverage = remap(coverage,PushConstants.data.x,1,0,1);
    coverage = clamp(coverage,0,1);
    float baseCloudWithCoverage = remap(baseCloud,coverage,1.0,0.0,1.0);
    baseCloudWithCoverage *= coverage;
    // adding noise
    pos.xz += fluidNoise.xy * (1.0-heightFraction);
    float highFrequencyFbm = (detailNoise.r * 0.625) + (detailNoise.g * 0.25) + (detailNoise.b * 0.125);

    heightFraction = GetHeightFractionForPoint(pos,vec2(BOX_BOUNDS_MIN.y,BOX_BOUNDS_MAX.y));
    
    float highFrequencyNoiseModifier = mix(highFrequencyFbm, 1.0-highFrequencyFbm,clamp(heightFraction * 10.0,0,1));

    float finalCloud = remap(baseCloudWithCoverage,highFrequencyNoiseModifier*0.3,1.0,0.0,1.0);

    return finalCloud;
}

float getLightStrength(vec3 raySrc, int resolution)
{
    vec3 lightDir = -sceneData.sunlightDirection.xyz;
    lightDir *= sign(lightDir.y);
    float dstInsideBox = rayBoxIntersect(BOX_BOUNDS_MIN,BOX_BOUNDS_MAX,raySrc,lightDir).y;

    float stepSize = dstInsideBox / resolution;
    float density = 0;

    vec3 pos = raySrc;

    for(int i=0;i<resolution;i++)
    {
        pos += lightDir * stepSize;
        density += max(0,sampleDensity(pos,vec3(0))*stepSize);
    }

    float beer = exp(-density * LIGHT_ABSORPTION);
    float powder = 1.0 - exp2(-density);
    float transmittance = 1.0*beer*powder;

    return DARKNESS_THRESHOLD + transmittance * (1-DARKNESS_THRESHOLD);

    
}

vec4 getCloudColor(vec3 raySrc, vec3 rayHit, int resolution)
{
    float density = 0;
    vec3 rayDir = normalize(rayHit-raySrc);
    vec3 lightDir = normalize(-sceneData.sunlightDirection.xyz);
    lightDir *= sign(lightDir.y);

    vec2 boxIntersectInfo = rayBoxIntersect(BOX_BOUNDS_MIN,BOX_BOUNDS_MAX,raySrc,rayDir);
    float dstToBox = boxIntersectInfo.x;
    float dstInsideBox = boxIntersectInfo.y;

    float distanceTravelled = 0;
    float stepSize = dstInsideBox / resolution;
    float distanceLimit = dstInsideBox;//min(distance(rayHit,raySrc),dstInsideBox);

    //if(dstToBox==0) return 0;

    //for(int i = 0; i<resolution;i++)
    float transmittance = 1;
    float light = 0;
    while(distanceTravelled < distanceLimit)
    {
        vec3 pos = raySrc + rayDir * (dstToBox + distanceTravelled);
        float nextDensity = sampleDensity(pos,10*vec3(sceneData.time.x,-sceneData.time.x,sceneData.time.x))*stepSize;

        if(nextDensity>0)
        {
            float hg = HenyeyGreenstein(lightDir,rayDir,PushConstants.data.y);
            float lightTransmittance = getLightStrength(pos,10) * hg;
            light += nextDensity * transmittance * lightTransmittance *stepSize;
            transmittance *= exp(-nextDensity * stepSize);
        }
        if(transmittance < 0.01) break;

        distanceTravelled += stepSize;
    }
    light = clamp(light,0,1);
    transmittance = 1.0-clamp(transmittance,0,1);
    //light = 1;
    float lightParam = clamp(sceneData.sunlightDirection.y,-1,1);
    lightParam = pow(lightParam,3);
    float lightStrength = mix(1.0,0.2,lightParam);
    vec3 lightColor = 
        mix(
        mix(
            vec3(0.961,0.792,0.502),
            sceneData.sunlightColor.xyz,
            clamp(-lightParam,0,1)
        ),
        vec3(0.855,0.91,0.91),
        (lightParam+1)*0.5);
    vec4 color = vec4(0,0,0,1.0) * transmittance + (vec4(lightColor,0) * light * lightStrength);

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
    vec3 inPlayerPos = PushConstants.playerPosition.xyz;
    vec4 cloudColor = getCloudColor(inPlayerPos,inPosition,100);
    //cloudColor += vec4(0.1,0.1,0.1,0.5);
    outFragColor = cloudColor;

    outNormal = vec4(0,-1,0,1);
	outPosition = sceneData.view * vec4(inPosition,1);
	outSpecularMap = vec4(0);
}