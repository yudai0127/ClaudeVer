#include "gltf_model.hlsli"




VS_OUT main(VS_IN vin)
{
    float sigma = vin.tangent.w;

	
    if (skin > -1)
    {
        row_major float4x4 skin_matrix =
            vin.weights[0].x * joint_matrices[vin.joints[0].x] +
            vin.weights[0].y * joint_matrices[vin.joints[0].y] +
            vin.weights[0].z * joint_matrices[vin.joints[0].z] +
            vin.weights[0].w * joint_matrices[vin.joints[0].w] +
            vin.weights[1].x * joint_matrices[vin.joints[1].x] +
            vin.weights[1].y * joint_matrices[vin.joints[1].y] +
            vin.weights[1].z * joint_matrices[vin.joints[1].z] +
            vin.weights[1].w * joint_matrices[vin.joints[1].w];
            
        vin.position = mul(float4(vin.position.xyz, 1), skin_matrix);
        vin.normal = normalize(mul(float4(vin.normal.xyz, 0), skin_matrix));
        vin.tangent = normalize(mul(float4(vin.tangent.xyz, 0), skin_matrix));
    }
    
    

    VS_OUT vout;
    // まずワールド座標を計算
    float4 w_pos = mul(float4(vin.position.xyz, 1.0f), world);
    
    
    vout.position = mul(w_pos, view_projection);

    vout.w_position = w_pos;
    
    

    
    // --- 法線の更新 ---
    vin.normal.w = 0;
    float3 original_normal = normalize(mul(vin.normal, world).xyz);
    float3 new_normal = original_normal;
    vout.w_normal = float4(new_normal, 0.0f);

    // --- 接線の更新 ---
    vin.tangent.w = 0;
    float3 original_tangent = normalize(mul(vin.tangent, world).xyz);
    // 元の法線に対して直交するように接線を再計算
    float3 new_tangent = normalize(original_tangent - dot(original_tangent, new_normal) * new_normal);
    vout.w_tangent = float4(new_tangent, sigma);

    vout.texcoord = vin.texcoord;
    
    
     [unroll]
    for (int i = 0; i < ShadowBufferSize; i++)
    {
        float4 wvpPos = mul(vout.w_position, cascade_light_view_projection[i]);
        wvpPos /= wvpPos.w;
        wvpPos.y = -wvpPos.y;
        wvpPos.xy = 0.5f * wvpPos.xy + 0.5f;
        vout.cascade_shadow_texcoord[i] = wvpPos;
    }
    
    return vout;
}