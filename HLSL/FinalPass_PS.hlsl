#include "Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "Sampler.hlsli"



Texture2D scene_texture : register(t0);


float3 ACESFitted(float3 color)
{
    
    static const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );

    
    static const float3x3 ACESOutputMat = float3x3(
        1.60475, -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602
    );

    color = mul(ACESInputMat, color);

   
    float3 a = color * (color + 0.0245786f) - 0.000090537f;
    float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;

    color = mul(ACESOutputMat, color);

    return saturate(color);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 hdr = scene_texture.Sample(sampler_states[ClampPoint], pin.texcoord).rgb;
    hdr = max(hdr, 0.0f);

  

    
    const float exposure = 2.0f;
    hdr *= exposure;

    // トーンマップ
    float3 ldr = ACESFitted(hdr);

    // ガンマ補正
    const float invGamma = 1.0f / 2.2f;
    float3 outColor = pow(ldr, invGamma);

    return float4(outColor, 1.0f);
}