#include "SSR.hlsli"
#include "../Sampler.hlsli"

Texture2D ssr_scene_color : register(t20);
Texture2D ssr_reflection : register(t21);
Texture2D ssr_param : register(t22);

float4 main(VS_OUT pin) : SV_TARGET
{
    // 元のシーンカラー
    float3 base_color = ssr_scene_color.Sample(sampler_states[ClampLinear], pin.texcoord).rgb;

    // SSRで得た反射色
    float4 reflection_color = ssr_reflection.Sample(sampler_states[ClampLinear], pin.texcoord);

    // roughness は GBuffer 相当テクスチャの alpha を使用
    float roughness = ssr_param.Sample(sampler_states[ClampLinear], pin.texcoord).a;

    // 粗さ考慮が有効な場合は反射をぼかして拡散寄りにする
    if (reflection_color.a >= 0.01f && roughness >= 0.01f && is_flag(screen_space_reflection_flags_consideration_rougness))
    {
        float2 texture_size;
        ssr_reflection.GetDimensions(texture_size.x, texture_size.y);

        static const int Size = 6;
        static const float separation = 4.0f;

        float4 accum_color = (float4) 0;
        float count = 0.0f;

        [unroll]
        for (int i = -Size; i <= Size; ++i)
        {
            [unroll]
            for (int j = -Size; j <= Size; ++j)
            {
                float2 delta = (float2(i, j) * separation) / texture_size;
                float4 sampling_color = ssr_reflection.Sample(sampler_states[ClampLinear], pin.texcoord.xy + delta);
                accum_color += sampling_color;
                count += 1.0f;
            }
        }

        reflection_color = lerp(reflection_color, accum_color / count, roughness);
    }

    // シーン色と反射色を alpha で合成
    float3 color = lerp(base_color.rgb, reflection_color.rgb, reflection_color.a);

    return float4(color, 1.0f);
}
