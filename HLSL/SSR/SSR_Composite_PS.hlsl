#include "SSR.hlsli"
#include "../Sampler.hlsli"

Texture2D ssr_scene_color : register(t20);
Texture2D ssr_reflection : register(t21);
Texture2D ssr_param : register(t22);

// 粗さぼかし用のポアソンディスク（単位円内にほぼ均等に分布）
#define POISSON_SAMPLE_COUNT 16
static const float BLUR_MAX_RADIUS_PIXELS = 24.0f;
static const float2 poisson_disk[POISSON_SAMPLE_COUNT] =
{
    float2(-0.94201624f, -0.39906216f),
    float2( 0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f),
    float2( 0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f),
    float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f),
    float2( 0.97484398f,  0.75648379f),
    float2( 0.44323325f, -0.97511554f),
    float2( 0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f),
    float2( 0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f),
    float2(-0.81409955f,  0.91437590f),
    float2( 0.19984126f,  0.78641367f),
    float2( 0.14383161f, -0.14100790f)
};

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

        // 以前は 13x13 = 169 タップの全探索で、画面全体に掛けるには非常に重かった。
        // ポアソンディスクの 16 タップに置き換え、半径を粗さでスケールさせることで
        // 見た目をほぼ保ったままタップ数を 1/10 以下にする
        float2 blur_radius = (BLUR_MAX_RADIUS_PIXELS * roughness) / texture_size;

        float4 accum_color = (float4) 0;

        [unroll]
        for (int i = 0; i < POISSON_SAMPLE_COUNT; ++i)
        {
            float2 delta = poisson_disk[i] * blur_radius;
            accum_color += ssr_reflection.Sample(sampler_states[ClampLinear], pin.texcoord.xy + delta);
        }

        reflection_color = lerp(reflection_color, accum_color / (float) POISSON_SAMPLE_COUNT, roughness);
    }

    // シーン色と反射色を alpha で合成
    float3 color = lerp(base_color.rgb, reflection_color.rgb, reflection_color.a);

    return float4(color, 1.0f);
}
