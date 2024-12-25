
// 2D Random
highp float random (in vec2 st) {
    return fract(sin(dot(st.xy,
                         vec2(12.9898,78.233)))
                 * 43758.5453123);
}

// 2D Noise based on Morgan McGuire @morgan3d
// https://www.shadertoy.com/view/4dS3Wd
float noise (in vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);

    // Four corners in 2D of a tile
    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));

    // Smooth Interpolation

    // Cubic Hermine Curve.  Same as SmoothStep()
    vec2 u = f*f*(3.0-2.0*f);
    // u = smoothstep(0.,1.,f);

    // Mix 4 coorners percentages
    return mix(a, b, u.x) +
            (c - a)* u.y * (1.0 - u.x) +
            (d - b) * u.x * u.y;
}
	
float noise2(vec2 p){
	vec2 ip = floor(p);
	vec2 u = fract(p);
	u = u*u*(3.0-2.0*u);
	
	float res = mix(
		mix(random(ip),random(ip+vec2(1.0,0.0)),u.x),
		mix(random(ip+vec2(0.0,1.0)),random(ip+vec2(1.0,1.0)),u.x),u.y);
	return res*res;
}
float layeredNoise(vec2 p, int octaves, float initialPersistence) {
    float n = 0;
    float amplitude = 1.0;
    float persistence = initialPersistence; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= noise(p*persistence) * amplitude;
        //amplitude *= 0.3;
        amplitude *= 0.2;
        persistence *= 2;
    }
    return n /maxAmplitude;
}


float layeredNoise(vec2 p, int octaves, float initialFrequency, float lacunarity, float persistence) {
    float n = 0;
    float amplitude = 1.0;
    float frequency = initialFrequency; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= noise(p*frequency) * amplitude;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}