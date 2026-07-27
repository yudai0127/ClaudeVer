#include "Water.hlsli"
#include "../Sampler.hlsli"
Texture2D NormalMap0 : register(t0);
Texture2D NormalMap1 : register(t1);
Texture2D NormalMap2 : register(t2);
Texture2D RippleDisplacementMap : register(t6);



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

    float3 n0 = NormalMap0.Sample(sampler_states[WrapAnisotropic], uv0).xyz * 2.0f - 1.0f;
    float3 n1 = NormalMap1.Sample(sampler_states[WrapAnisotropic], uv1).xyz * 2.0f - 1.0f;
    float3 n2 = NormalMap2.Sample(sampler_states[WrapAnisotropic], uv2).xyz * 2.0f - 1.0f;

    float3 nTS_Gerstner = normalize(n0 * normal0.w + n1 * normal1.w + n2 * normal2.w);

    float2 du = float2(1.0f / 200.0f, 0.0f);
    float2 dv = float2(0.0f, 1.0f / 200.0f);
    float h_c = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv, 0).x;
    float h_u = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv + du, 0).x;
    float h_v = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], uv + dv, 0).x;
    float3 rippleN_TS = normalize(float3((h_c - h_u) * rippleParams.w, (h_c - h_v) * rippleParams.w, 1.0f));

    float3 nTS = normalize(nTS_Gerstner + rippleN_TS);

    float3 T = IN.WorldTangent;
    float3 B = IN.WorldBitangent;
    float3 N_world = normalize(nTS.x * T + nTS.y * B + nTS.z * IN.WorldNorm);


    const float roughness = 0.02f;
    o.normalRoughness = float4(N_world, roughness);

    return o;
}