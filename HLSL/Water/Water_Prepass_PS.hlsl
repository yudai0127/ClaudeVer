#include "Water.hlsli"
#include "../Sampler.hlsli"
Texture2D NormalMap0 : register(t0);
Texture2D NormalMap1 : register(t1);
Texture2D NormalMap2 : register(t2);
Texture2D RippleDisplacementMap : register(t6);



// Water_PS.hlsl と同じ距離減衰のフォールバック値
static const float WaterPrepassDetailFadeDistance = 20000.0f;
static const float WaterPrepassRoughnessNear = 0.02f;
static const float WaterPrepassRoughnessFar = 0.20f;

struct PS_OUTPUT_PREPASS
{
    float4 normalRoughness : SV_Target0;
};

PS_OUTPUT_PREPASS main(PSIn IN)
{
    PS_OUTPUT_PREPASS o;

    float2 uv = IN.UV;
    float2 uv0 = uv * normal0.z + normal0.xy * gTime;
    float2 uv1 = uv * normal1.z + normal1.xy * gTime;
    float2 uv2 = uv * normal2.z + normal2.xy * gTime;

    // 本パス(Water_PS)と同じ距離減衰を適用する。
    // ここで作った法線はSSRとカスティクスに使われるため、
    // 減衰が食い違うと水面と反射で波の形が一致しなくなる。
    float viewDistance = length(gCameraPos - IN.WorldPos);
    float detailFadeDistance = (shadingParams.z > 1.0f) ? shadingParams.z : WaterPrepassDetailFadeDistance;
    float distFade = saturate(viewDistance / detailFadeDistance);

    float3 n0 = NormalMap0.Sample(sampler_states[WrapAnisotropic], uv0).xyz * 2.0f - 1.0f;
    float3 n1 = NormalMap1.Sample(sampler_states[WrapAnisotropic], uv1).xyz * 2.0f - 1.0f;
    float3 n2 = NormalMap2.Sample(sampler_states[WrapAnisotropic], uv2).xyz * 2.0f - 1.0f;

    // SSR のレイには低周波のうねりを主体にした滑らかな法線を使う。
    // 細波を同じ強さで入れると、船の像が細かく分断されて白い破片に見える。
    float weight0 = normal0.w * 0.55f;
    float weight1 = normal1.w * 0.18f * (1.0f - distFade * 0.80f);
    float weight2 = normal2.w * 0.06f * (1.0f - distFade * 0.95f);

    float3 nTS_Gerstner = normalize(float3(0.0f, 0.0f, 0.85f)
                                  + n0 * weight0 + n1 * weight1 + n2 * weight2);

    // 差分オフセットは波紋テクスチャの実テクセルサイズ(rippleParams.yz)を使う。
    // 固定値 1/200 では本パスと法線が食い違っていた。
    float2 du = float2(rippleParams.y, 0.0f);
    float2 dv = float2(0.0f, rippleParams.z);
    float h_c = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv, 0).x;
    float h_u = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv + du, 0).x;
    float h_v = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv + dv, 0).x;

    float rippleNormalStrength = rippleParams.w * 0.35f * (1.0f - distFade * 0.90f);
    float3 rippleN_TS = normalize(float3((h_c - h_u) * rippleNormalStrength,
                                         (h_c - h_v) * rippleNormalStrength,
                                         1.0f));

    float3 nTS = normalize(nTS_Gerstner + rippleN_TS);

    float3 T = IN.WorldTangent;
    float3 B = IN.WorldBitangent;
    float3 N_world = normalize(nTS.x * T + nTS.y * B + nTS.z * IN.WorldNorm);


    // 遠景では落とした細部を粗さとして戻す。
    // これによりSSRが遠方でぼけ、細かいノイズの反射が揺れにくくなる。
    float roughness = lerp(WaterPrepassRoughnessNear, WaterPrepassRoughnessFar, distFade);
    o.normalRoughness = float4(N_world, roughness);

    return o;
}
