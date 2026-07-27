#include "Gbuffer.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output = (PS_INPUT) 0;

    matrix world_view_projection = mul(world, view_projection);

    output.position = mul(input.position, world_view_projection);
    output.world_pos = mul(input.position, world).xyz;
    output.world_normal = normalize(mul(input.normal, (float3x3) world));
    output.world_tangent = normalize(mul(input.tangent.xyz, (float3x3) world));
    output.texcoord = input.texcoord;

    return output;
}
