#include "SSR.hlsli"
#include "../Sampler.hlsli"

Texture2D ssr_scene_color : register(t20);
Texture2D ssr_uv_map : register(t21);
Texture2D ssr_dummy : register(t22);

float4 main(VS_OUT pin) : SV_TARGET
{
    // UVマップ: xy=参照先UV, z=距離など, w=有効度
    float4 uv = ssr_uv_map.Sample(sampler_states[ClampLinear], pin.texcoord);

    // 穴埋め有効時は近傍UVを平均して補完
    if (uv.a <= 0.01f && is_flag(screen_space_reflection_flags_consideration_uv_hole))
    {
        float2 texture_size;
        ssr_scene_color.GetDimensions(texture_size.x, texture_size.y);

        int size = 2;
        float separation = 1.0f;

        float4 accum_uv = (float4) 0;
        float count = 0.0f;

        [unroll]
        for (int i = -size; i <= size; ++i)
        {
            [unroll]
            for (int j = -size; j <= size; ++j)
            {
                if (i == 0 && j == 0)
                    continue;

                float2 delta = (float2(i, j) * separation) / texture_size;
                accum_uv += ssr_uv_map.Sample(sampler_states[ClampLinear], pin.texcoord + delta);
                count += 1.0f;
            }
        }

        uv = accum_uv / max(count, 1.0f);
    }

    // UV範囲外は無効
    if (saturate(uv.x) != uv.x || saturate(uv.y) != uv.y)
        return (float4) 0;

    // 反射先カラーを取得して alpha に有効度を掛ける
    float4 color = ssr_scene_color.Sample(sampler_states[ClampLinear], uv.xy);
    color.a *= saturate(uv.w);

    return color;
}

