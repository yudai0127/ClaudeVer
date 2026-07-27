#include "Atmosphere.hlsli"

RWTexture2D<float4> g_TransmittanceLUT : register(u0);

static const int NUM_SAMPLES = 96;

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width, height;
    g_TransmittanceLUT.GetDimensions(width, height);

    if (dispatchID.x >= width || dispatchID.y >= height)
        return;

    // テクセル中心でサンプリング
    float u = (dispatchID.x + 0.5) / (float) width;
    float v = (dispatchID.y + 0.5) / (float) height;

    float r = planetRadius + (atmosphereRadius - planetRadius) * v;
    float mu = u * 2.0 - 1.0;

    float3 rayOrigin = float3(0.0, r, 0.0);
    float sinTheta = sqrt(saturate(1.0 - mu * mu));
    float3 rayDir = float3(sinTheta, mu, 0.0);

    // 大気外縁との交差
    float bAtm = 2.0 * r * mu;
    float cAtm = r * r - atmosphereRadius * atmosphereRadius;
    float dAtm = bAtm * bAtm - 4.0 * cAtm;

    if (dAtm < 0.0)
    {
        g_TransmittanceLUT[dispatchID.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float distanceToTop = (-bAtm + sqrt(dAtm)) * 0.5;
    distanceToTop = max(distanceToTop, 0.0);

    // 地面との交差（太陽方向が地面に当たるなら直達光は 0）
    float bGnd = 2.0 * r * mu;
    float cGnd = r * r - planetRadius * planetRadius;
    float dGnd = bGnd * bGnd - 4.0 * cGnd;

    if (dGnd >= 0.0)
    {
        float t0 = (-bGnd - sqrt(dGnd)) * 0.5;
        float t1 = (-bGnd + sqrt(dGnd)) * 0.5;

        float tGround = 1e20;
        if (t0 > 0.0)
            tGround = min(tGround, t0);
        if (t1 > 0.0)
            tGround = min(tGround, t1);

        if (tGround < distanceToTop)
        {
            g_TransmittanceLUT[dispatchID.xy] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    float stepSize = distanceToTop / (float) NUM_SAMPLES;
    float3 opticalDepth = 0.0;
    float3 currentPos = rayOrigin;

    [loop]
    for (int i = 0; i < NUM_SAMPLES; ++i)
    {
        float3 samplePos = currentPos + rayDir * (stepSize * 0.5);
        float h = max(0.0, length(samplePos) - planetRadius);

        float densityR = exp(-h / rayleighScaleHeight);
        float densityM = exp(-h / mieScaleHeight);

        opticalDepth.x += densityR * stepSize;
        opticalDepth.y += densityM * stepSize;

        currentPos += rayDir * stepSize;
    }

    float3 extinction = opticalDepth.x * rayleighScatteringCoefficient
                      + opticalDepth.y * mieScatteringCoefficient;
    float3 transmittance = exp(-extinction);

    g_TransmittanceLUT[dispatchID.xy] = float4(transmittance, 1.0);
}