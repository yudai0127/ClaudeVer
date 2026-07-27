#include "../Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "../Sampler.hlsli"

Texture2D gSceneColor : register(t0);
Texture2D gSceneDepth : register(t1);

cbuffer DoFParams : register(b0)
{
    float gNear;
    float gFar;
    float gFocusDist; // ピント距離
    float gFocusRange; // ピント幅
    float gMaxCoC; // CoC最大値
    float3 _pad;
};

float LinearizeDepth01(float z01)
{
    // 投影深度(0..1) -> view空間距離
    float n = gNear;
    float f = gFar;
    return (n * f) / max(1e-6, (f - z01 * (f - n)));
}

float4 main(VS_OUT i) : SV_Target
{
    // シーン色と深度を取得
    float3 col = gSceneColor.SampleLevel(sampler_states[ClampLinear], i.texcoord, 0).rgb;
    float z01 = gSceneDepth.SampleLevel(sampler_states[ClampPoint], i.texcoord, 0).r;
    float dist = LinearizeDepth01(z01);

    // 焦点からのズレを CoC として計算
    float coc = (dist - gFocusDist) / max(1e-3, gFocusRange);
    coc = clamp(coc, -1.0, 1.0);
    coc *= gMaxCoC;

    // rgb に色、alpha に CoC を格納
    return float4(col, coc);
}