#include "../Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "../Sampler.hlsli"

Texture2D gHalfColorCoC : register(t0);

cbuffer BlurParams : register(b1)
{
    // 1 / 半解像度サイズ
    float2 gInvHalfRes;
    float2 _pad;
};

float4 main(VS_OUT i) : SV_Target
{
    // 横方向1ピクセル分のUVオフセット
    float2 du = float2(gInvHalfRes.x, 0);

    // 5tap の重み付きブラー（1,2,3,2,1）
    float4 c0 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord - 2 * du, 0);
    float4 c1 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord - 1 * du, 0);
    float4 c2 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord, 0);
    float4 c3 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord + 1 * du, 0);
    float4 c4 = gHalfColorCoC.SampleLevel(sampler_states[ClampLinear], i.texcoord + 2 * du, 0);

    float4 sum = (c0 + 2 * c1 + 3 * c2 + 2 * c3 + c4) / 9.0;
    return sum;
}