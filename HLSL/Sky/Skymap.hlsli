#include "../Atmosphere.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION; // 頂点の位置
    float2 texcoord : TEXCOORD; // テクスチャ座標
};


struct VS_OUT_CUBOID
{
    float4 position : SV_POSITION; // 頂点の位置
    float2 texcoord : TEXCOORD; // テクスチャ座標
    float3 bearing : BEARING; // ベアリング
    uint instance_id : SV_INSTANCEID; // インスタンスID
};

struct GS_OUT_CUBOID
{
    float4 position : SV_POSITION; // 頂点の位置
    float2 texcoord : TEXCOORD; // テクスチャ座標
    float3 bearing : BEARING; // ベアリング
    uint sv_render_target_array_index : SV_RENDERTARGETARRAYINDEX; // レンダーターゲット配列インデックス
};

cbuffer SKY_MAP_CONSTANT_BUFFER : register(b4)
{
    // クリップ空間 → 惑星中心（km）空間 への変換行列
    row_major float4x4 inverse_view_projection;
    int skyType;
    float3 _paddingSkyMap;
}

