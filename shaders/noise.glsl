
// 2D Random
float random (in vec2 st) {
    return fract(sin(dot(st.xy,
                         vec2(12.9898,78.233)))
                 * 43758.5453123);
}

#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uvec2(UI0, UI1)
#define UI3 uvec3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

vec3 hash33(vec3 p)
{
	uvec3 q = uvec3(ivec3(p)) * UI3;
	q = (q.x ^ q.y ^ q.z)*UI3;
	return -1. + 2. * vec3(q) * UIF;
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
    //vec2 u = f*f*f*(10.0-f*(15.0-6.0*f));
    // u = smoothstep(0.,1.,f);
    // Interpolate along the x-axis for both rows of corners
    float x1 = mix(a, b, u.x);
    float x2 = mix(c, d, u.x);

    // Then, interpolate along the y-axis
    return mix(x1, x2, u.y);
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

vec2 hash( vec2 p ) {
	p = vec2(dot(p,vec2(127.1,311.7)), dot(p,vec2(269.5,183.3)));
	return -1.0 + 2.0*fract(sin(p)*43758.5453123);
}

float rnoise( in vec2 p ) {
    const float K1 = 0.366025404; // (sqrt(3)-1)/2;
    const float K2 = 0.211324865; // (3-sqrt(3))/6;
	vec2 i = floor(p + (p.x+p.y)*K1);	
    vec2 a = p - i + (i.x+i.y)*K2;
    vec2 o = (a.x>a.y) ? vec2(1.0,0.0) : vec2(0.0,1.0); //vec2 of = 0.5 + 0.5*vec2(sign(a.x-a.y), sign(a.y-a.x));
    vec2 b = a - o + K2;
	vec2 c = a - 1.0 + 2.0*K2;
    vec3 h = max(0.5-vec3(dot(a,a), dot(b,b), dot(c,c) ), 0.0 );
	vec3 n = h*h*h*h*vec3( dot(a,hash(i+0.0)), dot(b,hash(i+o)), dot(c,hash(i+1.0)));
    return dot(n, vec3(70.0));	
}
float layeredNoise(vec2 p, int octaves, float initialPersistence) {
    float n = 0;
    float amplitude = 1.0;
    float persistence = initialPersistence; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= rnoise(p*persistence) * amplitude;
        //amplitude *= 0.3;
        amplitude *= 0.3;
        persistence *= 1.5;
    }
    return n /maxAmplitude;
}


float fbm(vec2 n,int octaves) {
    const mat2 m = mat2( 1.6,  1.2, -1.2,  1.6 );
	float total = 0.0, amplitude = 0.1;
	for (int i = 0; i < octaves; i++) {
		total += rnoise(n) * amplitude;
		n = m * n;
		amplitude *= 0.4;
	}
	return total;
}


float layeredNoise(vec2 p, int octaves, float initialFrequency, float lacunarity, float persistence) {
    float n = 0;
    float amplitude = 1.0;
    float frequency = initialFrequency; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= rnoise(p*frequency) * amplitude;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}

float layeredNoise(vec2 p, int octaves, float initialFrequency, float initialAmplitude, float lacunarity, float persistence) {
    float n = 0;
    float amplitude = initialAmplitude;
    float frequency = initialFrequency; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= rnoise(p*frequency) * amplitude;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}

float fbm(vec2 p, int octaves, float initialFrequency, float initialAmplitude, float lacunarity, float persistence) {
    const mat2 m = mat2( 1.6,  1.2, -1.2,  1.6 );
    float n = 0;
    float amplitude = initialAmplitude;
    float frequency = initialFrequency; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= rnoise(p*frequency) * amplitude;
        p = m*p;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}

// Gradient noise by iq (modified to be tileable)
float gradientNoise3D(vec3 x, float freq)
{
    // grid
    vec3 p = floor(x);
    vec3 w = fract(x);
    
    // quintic interpolant
    vec3 u = w * w * w * (w * (w * 6. - 15.) + 10.);

    
    // gradients
    vec3 ga = hash33(mod(p + vec3(0., 0., 0.), freq));
    vec3 gb = hash33(mod(p + vec3(1., 0., 0.), freq));
    vec3 gc = hash33(mod(p + vec3(0., 1., 0.), freq));
    vec3 gd = hash33(mod(p + vec3(1., 1., 0.), freq));
    vec3 ge = hash33(mod(p + vec3(0., 0., 1.), freq));
    vec3 gf = hash33(mod(p + vec3(1., 0., 1.), freq));
    vec3 gg = hash33(mod(p + vec3(0., 1., 1.), freq));
    vec3 gh = hash33(mod(p + vec3(1., 1., 1.), freq));
    
    // projections
    float va = dot(ga, w - vec3(0., 0., 0.));
    float vb = dot(gb, w - vec3(1., 0., 0.));
    float vc = dot(gc, w - vec3(0., 1., 0.));
    float vd = dot(gd, w - vec3(1., 1., 0.));
    float ve = dot(ge, w - vec3(0., 0., 1.));
    float vf = dot(gf, w - vec3(1., 0., 1.));
    float vg = dot(gg, w - vec3(0., 1., 1.));
    float vh = dot(gh, w - vec3(1., 1., 1.));
	
    // interpolation
    return va + 
           u.x * (vb - va) + 
           u.y * (vc - va) + 
           u.z * (ve - va) + 
           u.x * u.y * (va - vb - vc + vd) + 
           u.y * u.z * (va - vc - ve + vg) + 
           u.z * u.x * (va - vb - ve + vf) + 
           u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
}

float fbm3D(vec3 p, int octaves, float initialFrequency, float initialAmplitude, float lacunarity, float persistence) {

    const mat3 m = mat3(
        15.27, 47.63, 21.94,
        39.41, 49.98, 23.12,
        67.31, 57.24, 76.45); 
    float n = 0;
    float amplitude = initialAmplitude;
    float frequency = initialFrequency; //1.0
    //float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        //maxAmplitude += amplitude;
        n+= gradientNoise3D(p*frequency,frequency) * amplitude;
        //p = m*p;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n;// /maxAmplitude;
}

//	Classic Perlin 3D Noise 
//	by Stefan Gustavson (https://github.com/stegu/webgl-noise)
//
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
vec3 fade(vec3 t) {return t*t*t*(t*(t*6.0-15.0)+10.0);}

float cnoise(vec3 P){
  vec3 Pi0 = floor(P); // Integer part for indexing
  vec3 Pi1 = Pi0 + vec3(1.0); // Integer part + 1
  Pi0 = mod(Pi0, 289.0);
  Pi1 = mod(Pi1, 289.0);
  vec3 Pf0 = fract(P); // Fractional part for interpolation
  vec3 Pf1 = Pf0 - vec3(1.0); // Fractional part - 1.0
  vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
  vec4 iy = vec4(Pi0.yy, Pi1.yy);
  vec4 iz0 = Pi0.zzzz;
  vec4 iz1 = Pi1.zzzz;

  vec4 ixy = permute(permute(ix) + iy);
  vec4 ixy0 = permute(ixy + iz0);
  vec4 ixy1 = permute(ixy + iz1);

  vec4 gx0 = ixy0 / 7.0;
  vec4 gy0 = fract(floor(gx0) / 7.0) - 0.5;
  gx0 = fract(gx0);
  vec4 gz0 = vec4(0.5) - abs(gx0) - abs(gy0);
  vec4 sz0 = step(gz0, vec4(0.0));
  gx0 -= sz0 * (step(0.0, gx0) - 0.5);
  gy0 -= sz0 * (step(0.0, gy0) - 0.5);

  vec4 gx1 = ixy1 / 7.0;
  vec4 gy1 = fract(floor(gx1) / 7.0) - 0.5;
  gx1 = fract(gx1);
  vec4 gz1 = vec4(0.5) - abs(gx1) - abs(gy1);
  vec4 sz1 = step(gz1, vec4(0.0));
  gx1 -= sz1 * (step(0.0, gx1) - 0.5);
  gy1 -= sz1 * (step(0.0, gy1) - 0.5);

  vec3 g000 = vec3(gx0.x,gy0.x,gz0.x);
  vec3 g100 = vec3(gx0.y,gy0.y,gz0.y);
  vec3 g010 = vec3(gx0.z,gy0.z,gz0.z);
  vec3 g110 = vec3(gx0.w,gy0.w,gz0.w);
  vec3 g001 = vec3(gx1.x,gy1.x,gz1.x);
  vec3 g101 = vec3(gx1.y,gy1.y,gz1.y);
  vec3 g011 = vec3(gx1.z,gy1.z,gz1.z);
  vec3 g111 = vec3(gx1.w,gy1.w,gz1.w);

  vec4 norm0 = taylorInvSqrt(vec4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
  g000 *= norm0.x;
  g010 *= norm0.y;
  g100 *= norm0.z;
  g110 *= norm0.w;
  vec4 norm1 = taylorInvSqrt(vec4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
  g001 *= norm1.x;
  g011 *= norm1.y;
  g101 *= norm1.z;
  g111 *= norm1.w;

  float n000 = dot(g000, Pf0);
  float n100 = dot(g100, vec3(Pf1.x, Pf0.yz));
  float n010 = dot(g010, vec3(Pf0.x, Pf1.y, Pf0.z));
  float n110 = dot(g110, vec3(Pf1.xy, Pf0.z));
  float n001 = dot(g001, vec3(Pf0.xy, Pf1.z));
  float n101 = dot(g101, vec3(Pf1.x, Pf0.y, Pf1.z));
  float n011 = dot(g011, vec3(Pf0.x, Pf1.yz));
  float n111 = dot(g111, Pf1);

  vec3 fade_xyz = fade(Pf0);
  vec4 n_z = mix(vec4(n000, n100, n010, n110), vec4(n001, n101, n011, n111), fade_xyz.z);
  vec2 n_yz = mix(n_z.xy, n_z.zw, fade_xyz.y);
  float n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x); 
  return 2.2 * n_xyz;
}
vec3 voronoiNoiseRandomVector(vec3 uv, float offset)
{
    const mat3 m = mat3(
        15.27, 47.63, 21.94,
        99.41, 89.98, 53.12,
        67.31, 37.24, 76.45); 
    
    uv = fract(sin(uv*m)*46839.32);

    return vec3(
        sin(uv.x + offset) * 0.5 + 0.5,
        cos(uv.y + offset) * 0.5 + 0.5,
        sin(uv.z + offset) * 0.5 + 0.5
    );
}

float voronoiNoise(vec3 p, float cellDensity, float angleOffset,vec3 wavelength)
{
    vec3 g = floor(p * cellDensity);
    vec3 f = fract(p * cellDensity);
    float minDistanceToCell = 100.0;

    for(int x=-1;x<=1;x++)
    {
        for(int y=-1;y<=1;y++)
        {
            for(int z=-1;z<=1;z++)
            {
                vec3 offset = vec3(x,y,z);
                vec3 targetCell = offset+g;
                //targetCell -= floor(targetCell/wavelength)*wavelength;
                //targetCell = fract(targetCell/wavelength) * wavelength;
                targetCell = mod(targetCell,wavelength);
                targetCell += step(targetCell, vec3(0.0)) * wavelength;
                vec3 cellOffset = voronoiNoiseRandomVector(targetCell,angleOffset);
                vec3 r = offset +cellOffset - f;
                //float d = distance(cell + cellOffset,f);
                float d = dot(r,r);
                minDistanceToCell = min(minDistanceToCell,d);
            }
        }
    }
    return sqrt(minDistanceToCell);
}

float layeredVoronoiNoise(vec3 p, float cellDensity, float angleOffset, float persistence,float lacunarity, int octaves,vec3 wavelength)
{
    const mat3 m = mat3(
        15.27, 47.63, 21.94,
        39.41, 49.98, 23.12,
        67.31, 57.24, 76.45); 
    float n = 0;
    float amplitude = 1.0;
    float frequency = 1.0; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= voronoiNoise(p,cellDensity*frequency,angleOffset, wavelength) * amplitude;
        //p = m*p;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}


//adapted from https://iquilezles.org/articles/smoothvoronoi/
float voronoiNoiseSmooth(vec3 p, float cellDensity, float angleOffset, vec3 wavelength)
{
    p+=wavelength;
    vec3 g = floor(p * cellDensity);
    vec3 f = fract(p * cellDensity);
    float res = 0.0;

    for(int x=-1;x<=1;x++)
    {
        for(int y=-1;y<=1;y++)
        {
            for(int z=-1;z<=1;z++)
            {
                vec3 offset = vec3(x,y,z);
                vec3 targetCell = offset+g;
                targetCell = mod(targetCell,wavelength);
                targetCell += step(targetCell, vec3(0.0)) * wavelength;

                vec3 cellOffset = voronoiNoiseRandomVector(targetCell,angleOffset);
                vec3 r = offset +cellOffset - f;
                //float d = distance(cell + cellOffset,f);
                float d = length(r);
                res += exp2( -32.0 * d);
            }
        }
    }
    return -(1.0/32.0) * log2(res);
}


float layeredVoronoiNoiseSmooth(vec3 p, float cellDensity, float angleOffset, float persistence,float lacunarity, int octaves, vec3 wavelength)
{
    const mat3 m = mat3(
        15.27, 47.63, 21.94,
        39.41, 49.98, 23.12,
        67.31, 57.24, 76.45); 
    float n = 0;
    float amplitude = 1.0;
    float frequency = 1.0; //1.0
    float maxAmplitude = 0.0;

    for(int i=0;i<octaves;i++) {
        maxAmplitude += amplitude;
        n+= voronoiNoiseSmooth(p,cellDensity*frequency,angleOffset,wavelength*frequency) * amplitude;
        //p = m*p;
        //amplitude *= 0.3;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return n /maxAmplitude;
}

float remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}


// Tileable 3D worley noise https://www.shadertoy.com/view/3dVXDc
float worleyNoise(vec3 uv, float freq)
{    
    vec3 id = floor(uv);
    vec3 p = fract(uv);
    
    float minDist = 10000.;
    for (float x = -1.; x <= 1.; ++x)
    {
        for(float y = -1.; y <= 1.; ++y)
        {
            for(float z = -1.; z <= 1.; ++z)
            {
                vec3 offset = vec3(x, y, z);
            	vec3 h = hash33(mod(id + offset, vec3(freq))) * .5 + .5;
    			h += offset;
            	vec3 d = p - h;
           		minDist = min(minDist, dot(d, d));
            }
        }
    }
    
    // inverted worley noise
    return 1. - minDist;
}
// Tileable Worley fbm inspired by Andrew Schneider's Real-Time Volumetric Cloudscapes
// chapter in GPU Pro 7.
float worleyFbm(vec3 p, float freq)
{
    return worleyNoise(p*freq, freq) * .625 +
        	 worleyNoise(p*freq*2., freq*2.) * .25 +
        	 worleyNoise(p*freq*4., freq*4.) * .125;
}

//curl noise https://www.shadertoy.com/view/cl23Wt
float gyroid (vec3 p) { return dot(sin(p),cos(p.yzx)); }
float curlNoise (vec3 p)
{
    float result = 0.;
    float a = .5;
    for (float i = 0.; i < 6; i++, a/=2.)
    {
        //p.z += iTime*.1;//+result*.5;
        result += abs(gyroid(p/a))*a;
    }
    return result;
}