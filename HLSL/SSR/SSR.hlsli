#ifndef __SCREEN_SPACE_REFLECTION_EX_HLSLI__
#define __SCREEN_SPACE_REFLECTION_EX_HLSLI__

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer SCREEN_SPACE_REFLECTION_CAMERA_BUFFER : register(b6)
{
    row_major float4x4 ssr_view;
    row_major float4x4 ssr_proj;
    row_major float4x4 ssr_inv_view;
    row_major float4x4 ssr_inv_proj;

    float2 ssr_inv_screen_size;
    float ssr_near;
    float ssr_far;
};

cbuffer SCREEN_SPACE_REFLECTION_CONSTANT_BUFFER : register(b7)
{
    float screen_space_reflection_max_distance; // 最大距離
    float screen_space_reflection_ray_resolution; // レイのステップ解像度
    float screen_space_reflection_tickness; // 厚み判定を行う際の丸め
    int screen_space_reflection_flags; // 処理の設定フラグ
};

static const int screen_space_reflection_flags_consideration_metalness = 0;
static const int screen_space_reflection_flags_reflection_dir_adjust = 1;
static const int screen_space_reflection_flags_consideration_uv_hole = 2;
static const int screen_space_reflection_flags_consideration_rougness = 3;
static const int screen_space_reflection_flags_z_check_subdivision = 4;

bool is_flag(int bitflag)
{
    return screen_space_reflection_flags & (1 << bitflag);
}

#endif  //  __SCREEN_SPACE_REFLECTION_EX_HLSLI__
