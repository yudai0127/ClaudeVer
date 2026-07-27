#include "../Fullscreen_Quad/Fullscreen_Quad.hlsli"
#include "../Sampler.hlsli"
Texture2D gFullColor : register(t0);
Texture2D gFullDepth : register(t1);
Texture2D gHalfCoC : register(t2); 
Texture2D gHalfBlur : register(t3); 


cbuffer DoFParams : register(b0)
{
    float gNear;
    float gFar;
    float gFocusDist;
    float gFocusRange;
    float gMaxCoC;
    float3 _pad;
};

float LinearizeDepth01(float z01)
{
    float n = gNear;
    float f = gFar;
    return (n * f) / max(1e-6, (f - z01 * (f - n)));
}

float4 main(VS_OUT i) : SV_Target
{
   
    float3 sharp = gFullColor.SampleLevel(sampler_states[ClampLinear], i.texcoord, 0).rgb;


   
    float coc = gHalfCoC.SampleLevel(sampler_states[ClampPoint], i.texcoord, 0).a;
    
    coc = saturate(coc);

    // 半解像度でブラーした色
    float3 blur = gHalfBlur.SampleLevel(sampler_states[ClampLinear], i.texcoord, 0).rgb;


    float3 outCol = lerp(sharp, blur, coc);
   
    return float4(outCol, 1);
}