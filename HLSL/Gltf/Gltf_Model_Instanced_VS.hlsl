#include "gltf_model.hlsli"

StructuredBuffer<InstanceData> InstanceWorlds : register(t17);

VS_OUT main(VS_IN vin, uint instanceId : SV_InstanceID)
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

    row_major float4x4 instance_world = InstanceWorlds[instanceId].World;
    row_major float4x4 world_inst = mul(world, instance_world);

    VS_OUT vout;

    float4 w_pos = mul(float4(vin.position.xyz, 1.0f), world_inst);

    float totalFactor = 0.0f;
   
    vout.position = mul(w_pos, view_projection);

    vout.w_position = w_pos;

    vin.normal.w = 0;
    float3 original_normal = normalize(mul(vin.normal, world_inst).xyz);
    float3 new_normal = normalize(lerp(original_normal, float3(0.0f, 1.0f, 0.0f), totalFactor));
    vout.w_normal = float4(new_normal, 0.0f);

    vin.tangent.w = 0;
    float3 original_tangent = normalize(mul(vin.tangent, world_inst).xyz);
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