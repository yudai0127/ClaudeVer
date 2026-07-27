struct VS_IN
{
    float4 position : POSITION;
    float4 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    uint4 joints[2] : JOINTS;
    float4 weights[2] : WEIGHTS;
};


struct BATCH_VS_IN
{
    float4 position : POSITION;
    float4 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
};


struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 w_position : POSITION;
    float4 w_normal : NORMAL;
    float4 w_tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    float4 cascade_shadow_texcoord[4] : TEXCOORD1;
    
};

static const int MAX_BONES = 256;

// 定数
static const float GammaFactor = 2.2f;

cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    int material;
    int has_tangent;
    int skin;
    int pad;
};

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 camera_position;
};

cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    float4 ambient_color;
    float4 directional_light_direction;
    float4 directional_light_color;
    float4 ibl_params;
};

static const uint PRIMITIVE_MAX_JOINTS = 512;
cbuffer PRIMITIVE_JOINT_CONSTANTS : register(b3)
{
    row_major float4x4 joint_matrices[PRIMITIVE_MAX_JOINTS];
};

// カスケードシャドウマップ用定数バッファ
static const int ShadowBufferSize = 4;
cbuffer CASCADE_SHADOWMAP_CONSTANT_BUFFER : register(b4)
{
    row_major float4x4 cascade_light_view_projection[ShadowBufferSize];
    float cascade_shadow_bias;
    float cascade_shadow_attenuation;
    bool display_cascade_area;
    float cascade_shadow_pcf_radius;
    float4 cascade_shadow_padding;
};

cbuffer MATERIAL_ROUGHNESS_CONSTANT_BUFFER : register(b5)
{
    float global_roughness_scale;
    float3 _pad_material_roughness;
};

struct InstanceData
{
    row_major float4x4 World;
};

#include "../Shading_Functions.hlsli"