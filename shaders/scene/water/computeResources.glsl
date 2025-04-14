struct SpectrumParameters {
    float scale;
    float angle;
    float spreadBlend;
    float swell;
    float alpha;
    float peakOmega;
    float gamma;
    float shortWavesFade;
};

layout(rgba16f,set = 0, binding = 0) uniform image2D butterflyTex;
layout(rgba16f,set = 0, binding = 1) uniform image2D posSpectrumTex;
layout(rgba16f,set = 0, binding = 2) uniform image2D negSpectrumTex;
layout(rgba16f,set = 0, binding = 3) uniform image2D noiseTex;
layout(set = 0, binding = 4) uniform SpectrumParams {
	SpectrumParameters pars[1];
} spectrumParams;