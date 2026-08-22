#pragma once
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "Graphics/ShaderConstants.h"
#include "Graphics/GPUConstantBuffer.h"

#define HIGH_FREQ_WORLEY_DIMENSIONS 32
#define HIGH_FREQ_WORLEY_NUMTHREADS 8
#define LOW_FREQ_PERLIN_WORLEY_DIMENSIONS 128
#define LOW_FREQ_PERLIN_WORLEY_NUMTHREADS 8

class VolumetricCloud
{
public:
    

    VolumetricCloud() = default;
     ~VolumetricCloud() = default;

    void initialize(ID3D11Device* device, const wchar_t* filename);


    ID3D11ShaderResourceView* getWeatherTextureSRV() const { return weather_shader_resource_view.Get(); }


   
    void blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* sky_cubemap_srv, ID3D11ShaderResourceView* transmittance_srv, ID3D11ShaderResourceView* irradiance_srv, const AtmosphereConstants& atmosphere_data);

   
    void generateCloudShadow(ID3D11DeviceContext* dc);
    ID3D11ShaderResourceView* getCloudShadowSRV() const { return cloud_shadow_srv.Get(); }

    void updateWeatherMap(ID3D11DeviceContext* dc, float weatherT);
    AtmosphereConstants atmosphere_constants_data;


    
    void debugGui();
    void setWeatherTarget(float t) { targetWeatherT = t; }
    float getTargetWeather() const { return targetWeatherT; }
   

    struct VOLUMETRIC_CLOUD_CONSTANT_BUFFER
    {
        DirectX::XMFLOAT2 wind_direction;
        DirectX::XMFLOAT2 cloud_altitudes_min_max;
        float wind_speed;

        float density_scale;

        float cloud_coverage_scale;
        float rain_cloud_absorption_scale;
        float cloud_type_scale;

        float horizon_distance_scale;

        float low_frequency_perlin_worley_sampling_scale;

        float high_frequency_worley_sampling_scale;

        float cloud_density_long_distance_scale;
        int enable_powdered_sugar_efffect;

        int ray_marching_steps;
        int auto_ray_marching_steps;
        float time;
        int debug_disable_self_shadow;
        int debug_disable_height_lighting;
        int debug_disable_horizon_fade;
        int debug_show_density;
    };

    VOLUMETRIC_CLOUD_CONSTANT_BUFFER volumetric_cloud_constant_data;

    struct WEATHER_GEN_CB
    {
        DirectX::XMFLOAT2 resolution;
        float time;
        float weatherT;

        DirectX::XMFLOAT2 windDir;
        float windSpeed;
        float pad0;

        float sunnyCoverage;
        float rainyCoverage;
        float sunnyRain;
        float rainyRain;

        float sunnyType;
        float rainyType;
        float noiseScale;
        float noiseAmp;
    };

private:
    // シェーダー関連
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> low_freq_perlin_worley_cs;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> high_freq_worley_cs;

    std::unique_ptr<GPUConstantBuffer> volumetric_cloud_cb;
    std::unique_ptr<GPUConstantBuffer> atmosphere_cb;

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> volumetric_cloud_constant_buffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> atmosphere_constant_buffer;
   
    // ノイズテクスチャ
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> low_freq_perlin_worley_shader_resource_view;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> high_freq_worley_shader_resource_view;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> curl_noise_shader_resource_view;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> weather_shader_resource_view;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_states[8];
 
    
    Microsoft::WRL::ComPtr<ID3D11PixelShader> cloud_shadow_ps;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cloud_shadow_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cloud_shadow_srv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> cloud_shadow_texture;

    WEATHER_GEN_CB cb = {};

    // 天候マップ生成用リソース
    Microsoft::WRL::ComPtr<ID3D11Texture2D> weather_texture2d;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> weather_uav;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> weather_gen_cs;

    
    std::unique_ptr<GPUConstantBuffer> weather_gen_cb;
    // 雨の描画用
    Microsoft::WRL::ComPtr<ID3D11PixelShader> rain_pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11BlendState> rain_blend_state;

 private:
        float currentWeatherT = 0.0f; 
        float targetWeatherT = 0.0f;  
        float weatherBlendSpeed = 0.05f;
};
