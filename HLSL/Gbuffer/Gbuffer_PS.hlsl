#include "Gbuffer.hlsli"
#include "../Sampler.hlsli"

Texture2D base_color_texture : register(t0);
Texture2D normal_texture : register(t1);
Texture2D metallic_roughness_texture : register(t2);
Texture2D emissive_texture : register(t3);
Texture2D occlusion_texture : register(t4);



PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output = (PS_OUTPUT) 0;

    float4 base_color = base_color_texture.Sample(sampler_states[WrapAnisotropic], input.texcoord) * base_color_factor;

   
    if (alpha_mode == 1)
    {
        clip(base_color.a - alpha_cutoff);
    }

    float3 normal = input.world_normal;
    if (normal_texture_scale > 0.0f)
    {
        float3 tangent_normal = normal_texture.Sample(sampler_states[WrapLinear], input.texcoord).xyz * 2.0f - 1.0f;
        tangent_normal.y = -tangent_normal.y;
        float3 N = normalize(input.world_normal);
        float3 T = normalize(input.world_tangent);
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        normal = normalize(mul(tangent_normal, TBN));
    }

    float metallic = metallic_factor;
    float roughness = roughness_factor;

    if (metallic_roughness_texture_scale > 0.0f)
    {
        float2 metallic_roughness = metallic_roughness_texture.Sample(sampler_states[WrapLinear], input.texcoord).bg;
        metallic *= metallic_roughness.x;
        roughness *= metallic_roughness.y;
    }

    float3 emissive = emissive_factor.rgb;
    if (emissive_texture_scale > 0.0f)
    {
        emissive *= emissive_texture.Sample(sampler_states[WrapAnisotropic], input.texcoord).rgb * emissive_texture_scale;
    }

    output.rt0 = float4(base_color.rgb, metallic);
    output.rt1 = float4(normalize(normal), roughness); 
    output.rt2 = float4(emissive, 1.0f); 
    output.rt3 = float4(input.world_pos, 1.0f); 

    return output;
}