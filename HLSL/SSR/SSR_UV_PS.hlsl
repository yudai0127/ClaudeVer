#include "SSR.hlsli"
#include "../Sampler.hlsli"

Texture2D ssr_scene_color : register(t20);
Texture2D ssr_normal_roughness : register(t21); 
Texture2D ssr_depth : register(t22); 

float4 convert_ndcspaceposition_to_uvspaceposition(float4 ndc)
{
    return float4(ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f), ndc.z, ndc.w);
}

float4 convert_uvspaceposition_to_ndcspaceposition(float2 uv, float projection_depth01)
{
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), projection_depth01, 1.0f);
}

// depth から view-space 座標を再構築
float4 convert_uvspaceposition_to_viewspaceposition(float2 uv)
{
    float projection_depth01 = ssr_depth.SampleLevel(sampler_states[ClampPoint], uv, 0).r;
    float4 position = mul(convert_uvspaceposition_to_ndcspaceposition(uv, projection_depth01), ssr_inv_proj);
    return position / max(position.w, 1e-6f);
}

float3 FetchWorldNormal(float2 uv)
{
    float4 nr = ssr_normal_roughness.SampleLevel(sampler_states[ClampPoint], uv, 0);
    return normalize(nr.xyz);
}

float FetchRoughness(float2 uv)
{
    float4 nr = ssr_normal_roughness.SampleLevel(sampler_states[ClampPoint], uv, 0);
    return saturate(nr.a);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    // 出力: (hitUV.x, hitUV.y, reflectionLength, alpha)
    float4 outColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    bool out_of_bounds = false;

    float2 uv = pin.texcoord.xy;

    // 背景ピクセルは対象外
    float depth01 = ssr_depth.SampleLevel(sampler_states[ClampPoint], uv, 0).r;
    if (depth01 >= 1.0f - 1e-6f)
    {
        return outColor;
    }

    // 現在ピクセルの view-space 情報
    float4 viewspace_position = convert_uvspaceposition_to_viewspaceposition(uv);
    float3 world_n = FetchWorldNormal(uv);
    float4 viewspace_normal = normalize(mul(float4(world_n, 0.0f), ssr_view));

    // 反射レイ（view-space）
    float3 viewspace_ray = normalize(viewspace_position.xyz);
    float3 viewspace_reflect_ray = normalize(reflect(viewspace_ray, viewspace_normal.xyz));
    float4 viewspace_ray_origin = float4(viewspace_position.xyz, 1.0f);

   
    float view_depth = abs(viewspace_position.z);
    float depth_scaled_max_distance = max(screen_space_reflection_max_distance, view_depth * 0.5f);
    float depth_scaled_tickness = screen_space_reflection_tickness * clamp(view_depth * 0.002f, 1.0f, 8.0f);

    float ray_length = depth_scaled_max_distance;
    if ((viewspace_ray_origin.z + viewspace_reflect_ray.z * depth_scaled_max_distance) < ssr_near)
    {
        float denom = (abs(viewspace_reflect_ray.z) > 1e-5f) ? viewspace_reflect_ray.z : -1e-5f;
        ray_length = (ssr_near - viewspace_ray_origin.z) / denom;
    }
    
    ray_length = max(ray_length, 0.0f);

    float4 viewspace_ray_end = float4(viewspace_position.xyz + viewspace_reflect_ray * ray_length, 1.0f);

    float tickness = depth_scaled_tickness;
    float ray_resolution = screen_space_reflection_ray_resolution;

    // 斜め方向の反射ほど判定を少し緩める
    float ray_adjust_rate = 1.0f - max(0.0f, dot(viewspace_normal.xyz, viewspace_reflect_ray.xyz));
    if (is_flag(screen_space_reflection_flags_reflection_dir_adjust))
    {
        tickness *= (1.0f + ray_adjust_rate);
        ray_resolution *= (1.0f + ray_adjust_rate);
    }

    // レイ端点をスクリーン空間へ変換
    float4 clipspace_ray_origin = mul(viewspace_ray_origin, ssr_proj);
    float4 clipspace_ray_end = mul(viewspace_ray_end, ssr_proj);

    float2 uvspace_ray_origin = convert_ndcspaceposition_to_uvspaceposition(clipspace_ray_origin / clipspace_ray_origin.w).xy;
    float2 uvspace_ray_end = convert_ndcspaceposition_to_uvspaceposition(clipspace_ray_end / clipspace_ray_end.w).xy;

    // UV基準とテクスチャ座標基準の両方を持つ
    float2 viewportWH = 1.0f / max(ssr_inv_screen_size, float2(1e-6f, 1e-6f));
    float4 viewport_size = float4(viewportWH.xy, ssr_inv_screen_size.xy);

    float2 texturespace_ray_origin = uvspace_ray_origin * viewport_size.xy;
    float2 texturespace_ray_end = uvspace_ray_end * viewport_size.xy;
    float2 texturespace_ray_delta = texturespace_ray_end - texturespace_ray_origin;

    // 支配軸（x or y）を選んでステップ数を決める
    float use_axis_x = abs(texturespace_ray_delta.x) >= abs(texturespace_ray_delta.y) ? 1.0f : 0.0f;
    float axis_delta = lerp(abs(texturespace_ray_delta.y), abs(texturespace_ray_delta.x), use_axis_x) * saturate(ray_resolution);

    if (axis_delta <= 0.0f)
    {
        return outColor;
    }

    // 長い反射レイが画面サイズに比例して数百～数千回走査されないようにする。
    // ハーフ解像度 SSR では 96 ステップで十分な連続性を維持できる。
    static const float max_primary_step_count = 96.0f;
    axis_delta = clamp(ceil(axis_delta), 1.0f, max_primary_step_count);
    float2 texturespace_ray_step = texturespace_ray_delta.xy / axis_delta;

    int increment_count = 0;
    float viewspace_ray_current_distance = viewspace_ray_origin.z;
    float viewspace_diff_depth = screen_space_reflection_tickness;
    float4 viewspace_current_ray_position = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 texturespace_current_ray_position = texturespace_ray_origin.xy;
    float2 hit_uv = texturespace_current_ray_position * viewport_size.zw;

    // 必要なら補間探索の分割数を増やす
    static const int subdivision_iteration = 4;
    int subdivision_step_count = 1;
    if (is_flag(screen_space_reflection_flags_z_check_subdivision))
    {
        subdivision_step_count = max(1, (int) (ray_adjust_rate * ray_adjust_rate * (float) subdivision_iteration));
    }

    int hit_flag0 = 0, hit_flag1 = 0;
    float search_0 = 0.0f, search_1 = 0.0f;

    // 1次探索: レイマーチでヒット区間を見つける
    [loop]
    for (increment_count = 0; increment_count < (int) axis_delta; ++increment_count)
    {
        texturespace_current_ray_position += texturespace_ray_step;
        hit_uv.xy = texturespace_current_ray_position * viewport_size.zw;

        if (saturate(hit_uv.x) != hit_uv.x || saturate(hit_uv.y) != hit_uv.y)
        {
            out_of_bounds = true;
            break;
        }

        viewspace_current_ray_position = convert_uvspaceposition_to_viewspaceposition(hit_uv.xy);

        search_1 = lerp(
            (texturespace_current_ray_position.y - texturespace_ray_origin.y) / texturespace_ray_delta.y,
            (texturespace_current_ray_position.x - texturespace_ray_origin.x) / texturespace_ray_delta.x,
            use_axis_x);
        search_1 = saturate(search_1);

        float search_delta = (search_1 - search_0) / (float) subdivision_step_count;

        [loop]
        for (int i = 0; i <= subdivision_step_count; ++i)
        {
            viewspace_ray_current_distance =
                (viewspace_ray_origin.z * viewspace_ray_end.z) /
                lerp(viewspace_ray_end.z, viewspace_ray_origin.z, search_0 + search_delta * (float) i);

            viewspace_diff_depth = viewspace_ray_current_distance - viewspace_current_ray_position.z;

            if (viewspace_diff_depth > 0 && viewspace_diff_depth < tickness)
            {
                hit_flag0 = 1;
                break;
            }
        }

        if (hit_flag0 || out_of_bounds)
            break;

        search_0 = search_1;
    }

    if (out_of_bounds)
    {
        return outColor;
    }

    // 2次探索: ヒット区間を二分探索で詰める
    search_1 = search_0 + ((search_1 - search_0) / 2.0f);

    static const int secondary_step_count = 6;
    int secondary_steps = secondary_step_count * hit_flag0;

    [unroll(secondary_step_count)]
    for (increment_count = 0; increment_count < secondary_steps; ++increment_count)
    {
        texturespace_current_ray_position = lerp(texturespace_ray_origin.xy, texturespace_ray_end.xy, search_1);
        hit_uv.xy = texturespace_current_ray_position * viewport_size.zw;

        if (saturate(hit_uv.x) != hit_uv.x || saturate(hit_uv.y) != hit_uv.y)
        {
            out_of_bounds = true;
            break;
        }

        viewspace_current_ray_position = convert_uvspaceposition_to_viewspaceposition(hit_uv.xy);

        viewspace_ray_current_distance =
            (viewspace_ray_origin.z * viewspace_ray_end.z) /
            lerp(viewspace_ray_end.z, viewspace_ray_origin.z, search_1);

        viewspace_diff_depth = viewspace_ray_current_distance - viewspace_current_ray_position.z;

        if (viewspace_diff_depth > 0 && viewspace_diff_depth < tickness)
        {
            hit_flag1 = 1;
            search_1 = search_0 + ((search_1 - search_0) / 2.0f);
        }
        else
        {
            float temp = search_1;
            search_1 = search_1 + ((search_1 - search_0) / 2.0f);
            search_0 = temp;
        }
    }

    if (out_of_bounds)
    {
        return outColor;
    }

    // 反射寄与率
    float alpha = (float) hit_flag1;

    // 反射角で減衰
    alpha *= (1.0f - max(0.0f, dot(-viewspace_ray, viewspace_reflect_ray)));
    // 深度差で減衰
    alpha *= (1.0f - saturate(viewspace_diff_depth / tickness));

    float reflection_length = length(viewspace_current_ray_position - viewspace_ray_origin);
    // 長距離ほど減衰
    alpha *= (1.0f - saturate(reflection_length / depth_scaled_max_distance));

    
    

    
    // 画面端の破綻を緩和
    static const float screen_space_reflection_border = 0.05f;
    alpha *= smoothstep(1.0f, 1.0f - screen_space_reflection_border, saturate(2.0f * abs(hit_uv.x - 0.5f)));
    alpha *= smoothstep(1.0f, 1.0f - screen_space_reflection_border, saturate(2.0f * abs(hit_uv.y - 0.5f)));

    // 粗さ考慮（粗いほどSSRを弱める）
    float roughness = FetchRoughness(uv);
    if (is_flag(screen_space_reflection_flags_consideration_rougness))
        alpha *= (1.0f - roughness);

    outColor = float4(hit_uv.xy, reflection_length, saturate(alpha));
    return outColor;
}
