#pragma once
#include <DirectXMath.h>

struct SceneConstants
{
    DirectX::XMFLOAT4X4		view_projection;
    DirectX::XMFLOAT4   camera_position;
    DirectX::XMFLOAT4X4 inv_view_projection;
};


struct AtmosphereConstants
{
    DirectX::XMFLOAT3 sunDirection;
    float  sunIntensity;

    DirectX::XMFLOAT3 cameraPosition;
    float  nightIntensity;

    float planetRadius;
    float atmosphereRadius;
    float rayleighScaleHeight;
    float mieScaleHeight;

    DirectX::XMFLOAT3 rayleighScatteringCoefficient;
    float mieScatteringCoefficient;

    float mieEccentricity;
    DirectX::XMFLOAT3 _padding2;
};


static constexpr int CASCADE_COUNT = 4;
// カスケードシャドウマップ用
struct CascadeConstants
{
    DirectX::XMFLOAT4X4 cascade_light_view_projection[CASCADE_COUNT];
    float               cascade_shadow_bias;
    float               cascade_shadow_attenuation;
    BOOL                display_cascade_area;
    float               cascade_shadow_pcf_radius;
    DirectX::XMFLOAT4   cascade_shadow_padding{}; 
};
