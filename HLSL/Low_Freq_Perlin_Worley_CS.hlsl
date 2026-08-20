#include "Perlin_Worley_Noise.hlsli"

RWTexture3D<float4> low_freq_perlin_worley : register(u0);

#define LOW_FREQ_PERLIN_WORLEY_DIMENSIONS 128
#define LOW_FREQ_PERLIN_WORLEY_NUMTHREADS 8

// Sum of the seven octave amplitudes used by perlin_fbm (gain = 2^-0.85).
// Normalize the signed gradient noise before storing it. The old abs() made
// every zero crossing into a hard ridge and gave clouds a carved-rock surface.
static const float PERLIN_FBM_AMPLITUDE_SUM = 2.20977146;

[numthreads(LOW_FREQ_PERLIN_WORLEY_NUMTHREADS, LOW_FREQ_PERLIN_WORLEY_NUMTHREADS, LOW_FREQ_PERLIN_WORLEY_NUMTHREADS)]
void main(uint3 dtid : SV_DISPATCHTHREADID)
{
    const float freq = 4.0;
    
    float3 uvw = (float3) (dtid) / LOW_FREQ_PERLIN_WORLEY_DIMENSIONS;
	
    float signed_perlin = perlin_fbm(uvw, freq, 7);
    float pfbm = saturate(0.5 + 0.5 * signed_perlin / PERLIN_FBM_AMPLITUDE_SUM);
    pfbm = smoothstep(0.34, 0.66, pfbm);
    
    float4 color = 0;
    color.g = saturate(worley_fbm(uvw, freq * 1.0));
    color.b = saturate(worley_fbm(uvw, freq * 2.0));
    color.a = saturate(worley_fbm(uvw, freq * 4.0));
    color.r = remap(pfbm, 0.0, 1.0, color.g, 1.0); 

    low_freq_perlin_worley[dtid] = color;
}
