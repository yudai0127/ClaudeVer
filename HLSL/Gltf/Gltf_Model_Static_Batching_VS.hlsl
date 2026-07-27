#include "gltf_model.hlsli"

VS_OUT main(BATCH_VS_IN vin)
{
    VS_OUT vout;

    // まずワールド座標を計算
    float4 w_pos = mul(float4(vin.position.xyz, 1.0f), world);
    
    // 水面へこみを適用し、変位の度合い(totalFactor)を受け取る
    float totalFactor = 0.0f;
    
    vout.position = mul(w_pos, view_projection);
    vout.w_position = w_pos;

    // --- 法線の更新 ---
    vin.normal.w = 0;
    float3 original_normal = normalize(mul(vin.normal, world).xyz);
    // へこみ度合いに応じて、法線を上向き(0, 1, 0)にブレンド
    float3 new_normal = normalize(lerp(original_normal, float3(0.0f, 1.0f, 0.0f), totalFactor));
    vout.w_normal = float4(new_normal, 0.0f);

    // --- 接線の更新 ---
    float sigma = vin.tangent.w;
    vin.tangent.w = 0;
    float3 original_tangent = normalize(mul(vin.tangent, world).xyz);
    // 変更した新しい法線に対して直交するように接線を再計算
    float3 new_tangent = normalize(original_tangent - dot(original_tangent, new_normal) * new_normal);
    vout.w_tangent = float4(new_tangent, sigma);

    vout.texcoord = vin.texcoord;

    // カスケードシャドウマップ用のテクスチャ座標を計算
    [unroll]
    for (int i = 0; i < ShadowBufferSize; i++)
    {
        float4 light_space_pos = mul(vout.w_position, cascade_light_view_projection[i]);
        light_space_pos /= light_space_pos.w;
        
        // NDC座標からUV座標に変換
        light_space_pos.y = -light_space_pos.y;
        light_space_pos.xy = 0.5f * light_space_pos.xy + 0.5f;
        vout.cascade_shadow_texcoord[i] = light_space_pos;
    }

    return vout;
}