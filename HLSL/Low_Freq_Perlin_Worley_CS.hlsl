#include "Perlin_Worley_Noise.hlsli"

RWTexture3D<float4> low_freq_perlin_worley : register(u0);

#define LOW_FREQ_PERLIN_WORLEY_DIMENSIONS 128
#define LOW_FREQ_PERLIN_WORLEY_NUMTHREADS 8
[numthreads(LOW_FREQ_PERLIN_WORLEY_NUMTHREADS, LOW_FREQ_PERLIN_WORLEY_NUMTHREADS, LOW_FREQ_PERLIN_WORLEY_NUMTHREADS)]
void main(uint3 dtid : SV_DISPATCHTHREADID)
{
    const float freq = 4.0;
    
    float3 uvw = (float3) (dtid) / LOW_FREQ_PERLIN_WORLEY_DIMENSIONS;
	
    float pfbm = lerp(1.0, perlin_fbm(uvw, freq , 7), 0.5);
    pfbm = abs(pfbm * 2.0 - 1.0); 
    
    float4 color = 0;
    color.g = worley_fbm(uvw, freq * 1.0);
    color.b = worley_fbm(uvw, freq * 2.0);
    color.a = worley_fbm(uvw, freq * 4.0);
    color.r = remap(pfbm, 0.0, 1.0, color.g, 1.0); 

    low_freq_perlin_worley[dtid] = color;
}