#include "perlin_worley_noise.hlsli"

RWTexture3D<float4> high_freq_worley : register(u0);

#define HIGH_FREQ_WORLEY_DIMENSIONS 32
#define HIGH_FREQ_WORLEY_NUMTHREADS 8
[numthreads(HIGH_FREQ_WORLEY_NUMTHREADS, HIGH_FREQ_WORLEY_NUMTHREADS, HIGH_FREQ_WORLEY_NUMTHREADS)]
void main(uint3 dtid : SV_DISPATCHTHREADID)
{
    const float freq = 4.0;
    
    float3 uvw = (float3) (dtid) / HIGH_FREQ_WORLEY_DIMENSIONS;
	
    float4 color = 0;
    color.r = worley_fbm(uvw, freq * 1.0);
    color.g = worley_fbm(uvw, freq * 2.0);
    color.b = worley_fbm(uvw, freq * 4.0);
    color.a = 1.0;

    high_freq_worley[dtid] = color;
}

