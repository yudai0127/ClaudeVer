#include "../Gltf/Gltf_Model.hlsli"

StructuredBuffer<InstanceData> InstanceWorlds : register(t17);

struct SHADOW_VS_OUT
{
    float4 position : SV_POSITION;
};

cbuffer SHADOW_PASS_CB : register(b10)
{
    row_major float4x4 light_view_projection;
};

SHADOW_VS_OUT main(VS_IN vin, uint instanceId : SV_InstanceID)
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
    }

    row_major float4x4 instance_world = InstanceWorlds[instanceId].World;
    row_major float4x4 world_inst = mul(world, instance_world);

    SHADOW_VS_OUT vout;

    float4 w_pos = mul(float4(vin.position.xyz, 1.0f), world_inst);
    float totalFactor = 0.0f;
   
    vout.position = mul(w_pos, view_projection);

    return vout;
}