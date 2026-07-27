#ifndef GBUFFER_HLSLI
#define GBUFFER_HLSLI

#include "../Gltf/Gltf_Model.hlsli"

struct VS_INPUT
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    float3 world_normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 world_tangent : TANGENT;
};

struct PS_OUTPUT
{
    float4 rt0 : SV_Target0; 
    float4 rt1 : SV_Target1; 
    float4 rt2 : SV_Target2; 
    float4 rt3 : SV_Target3;
};


cbuffer CbMaterial : register(b2)
{
    float4 base_color_factor;
    float4 emissive_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_texture_scale;
    float occlusion_texture_strength;
    float emissive_texture_scale;
    float metallic_roughness_texture_scale;
    uint alpha_mode;
    float alpha_cutoff;
};



#endif // GBUFFER_HLSLI