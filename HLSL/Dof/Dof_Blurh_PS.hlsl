#include "../Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "../Sampler.hlsli"

Texture2D gHalfColorCoC : register(t0);

cbuffer BlurParams : register(b1)
{
    // 1 / 半解像度サイズ
    float2 gInvHalfRes;
    float2 _pad;
};

// 9タップのガウス重み（中心 + 片側4タップ）
#define DOF_BLUR_TAP_COUNT 4
static const float DofBlurWeights[DOF_BLUR_TAP_COUNT + 1] =
{
    0.2270270f, 0.1945946f, 0.1216216f, 0.0540541f, 0.0162162f
};

// CoCが0の画素の重みが完全に0にならないようにする下限
static const float DofCocWeightBias = 0.02f;

float4 main(VS_OUT i) : SV_Target
{
    float2 tapStep = float2(gInvHalfRes.x, 0.0f);

    float4 center = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord, 0);

    // ピントの合った画素（CoCが小さい）の色がボケ側へにじみ出すと
    // 輪郭にハローが出る。CoCの大きさでタップを重み付けして防ぐ
    float centerWeight = DofBlurWeights[0] * (saturate(abs(center.a)) + DofCocWeightBias);
    float3 colorSum = center.rgb * centerWeight;
    float cocSum = center.a * centerWeight;
    float weightSum = centerWeight;

    [unroll]
    for (int t = 1; t <= DOF_BLUR_TAP_COUNT; ++t)
    {
        float2 offset = tapStep * (float) t;

        float4 s0 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord - offset, 0);
        float4 s1 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord + offset, 0);

        float w0 = DofBlurWeights[t] * (saturate(abs(s0.a)) + DofCocWeightBias);
        float w1 = DofBlurWeights[t] * (saturate(abs(s1.a)) + DofCocWeightBias);

        colorSum += s0.rgb * w0 + s1.rgb * w1;
        cocSum += s0.a * w0 + s1.a * w1;
        weightSum += w0 + w1;
    }

    float invWeight = 1.0f / max(weightSum, 1e-5f);
    return float4(colorSum * invWeight, cocSum * invWeight);
}
