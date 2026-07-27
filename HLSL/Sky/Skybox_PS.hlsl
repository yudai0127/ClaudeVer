#include "Skymap.hlsli" 
#include "../Sampler.hlsli"

TextureCube skybox : register(t0);


float4 main(VS_OUT pin) : SV_TARGET
{
    //Ž‹ü•ûŒü‚ÌŒvŽZ
    float4 R = mul(float4((pin.texcoord.x * 2.0) - 1.0, 1.0 - (pin.texcoord.y * 2.0), 1.0, 1.0), inverse_view_projection);
    R /= R.w;
    float3 viewDir = normalize(R.xyz);

    
    const float lod = 0;
    float3 skyColor = skybox.SampleLevel(sampler_states[ClampLinear], viewDir, lod).rgb;

    
    float3 sunColor = 0.0f;
    if (skyType != 2) 
    {
        // ’n•\ŽÕ•Á”»’è
        float3 oc = cameraPosition;
        float b = dot(oc, sunDirection);
        float c = dot(oc, oc) - planetRadius * planetRadius;
        float discriminant = b * b - c;
        bool sunOccluded = false;
        if (discriminant >= 0.0)
        {
            float tHit = -b - sqrt(discriminant);
            sunOccluded = (tHit > 0.0);
        }

        float sunDot = dot(viewDir, sunDirection);
        float sunHighlight = 0.0f;

        if (!sunOccluded)
        {
            const float SUN_RADIUS_DEG = 2.0f;
            const float SUN_SOFT_DEG = 1.0f;
            const float DEG2RAD = 0.017453292519943295f;
            float cosInner = cos(SUN_RADIUS_DEG * DEG2RAD);
            float cosOuter = cos((SUN_RADIUS_DEG + SUN_SOFT_DEG) * DEG2RAD);
            float mask = smoothstep(cosOuter, cosInner, sunDot);
            sunHighlight = pow(saturate(mask), 2.0f);
        }

        sunColor = sunHighlight * sunIntensity;
    }

    float3 finalColor = skyColor + sunColor;

    return float4(finalColor, 1.0);
}