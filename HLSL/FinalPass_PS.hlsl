#include "Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "Sampler.hlsli"



Texture2D scene_texture : register(t0);

float3 BrightPass(float3 color)
{
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float contribution = smoothstep(0.85f, 2.5f, luminance);
    return color * contribution;
}


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
    float3 hdr = scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord).rgb;
    hdr = max(hdr, 0.0f);

    // Lightweight multi-radius bloom keeps sun glints, foam and bright cloud
    // edges readable on an exhibition display without adding another pass.
    uint textureWidth, textureHeight;
    scene_texture.GetDimensions(textureWidth, textureHeight);
    float2 texel = 1.0f / float2(max(textureWidth, 1u), max(textureHeight, 1u));

    // 先に各サンプルを明部判定すると、孤立した1画素が周囲に複製され、
    // 雲の光点のように見える。周辺色を平均してから明部を抽出する。
    float3 bloomNeighborhood = 0.0f;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2( 2.0f,  0.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2(-2.0f,  0.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2( 0.0f,  2.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2( 0.0f, -2.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2( 5.0f,  5.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2(-5.0f,  5.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2( 5.0f, -5.0f)).rgb;
    bloomNeighborhood += scene_texture.Sample(sampler_states[ClampLinear], pin.texcoord + texel * float2(-5.0f, -5.0f)).rgb;
    bloomNeighborhood *= 0.125f;
    float3 bloom = BrightPass(max(bloomNeighborhood, 0.0f));
    hdr += bloom * 0.28f;

    // Lower exposure preserves reflection and cloud-shadow detail that was
    // previously clipped to white; contrast and saturation restore punch.
    const float exposure = 1.55f;
    hdr *= exposure;

    // トーンマップ
    float3 ldr = ACESFitted(hdr);

    float luma = dot(ldr, float3(0.2126f, 0.7152f, 0.0722f));
    ldr = lerp(luma.xxx, ldr, 1.12f);
    // Preserve exhibition readability in dark hull/terrain materials without
    // raising the HDR exposure or clipping bright water and clouds.
    ldr = saturate((ldr - 0.5f) * 1.04f + 0.5f);

    float2 centeredUv = pin.texcoord * 2.0f - 1.0f;
    float vignette = 1.0f - smoothstep(0.55f, 1.35f, dot(centeredUv, centeredUv)) * 0.12f;
    ldr *= vignette;

    // ガンマ補正
    const float invGamma = 1.0f / 2.2f;
    float3 outColor = pow(ldr, invGamma);

    return float4(outColor, 1.0f);
}
