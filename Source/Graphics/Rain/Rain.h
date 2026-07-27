#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include "Graphics/GPUConstantBuffer.h"

class RainSystem
{
public:
    void initialize(ID3D11Device* device);

   
    void update(
        ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* weatherSRV,
        float dt,
        const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& proj,
        const DirectX::XMFLOAT3& cameraPos);

    void render(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* sceneDepthSRV, ID3D11RenderTargetView* rtvOverride = nullptr);

  

    // 雨が水面に当たったヒットをCPU側に読み戻す
    UINT ReadbackHits(ID3D11DeviceContext* dc,
        std::vector<DirectX::XMFLOAT4>& outHits,
        UINT maxRead = 256);

private:
    struct Particle
    {
        DirectX::XMFLOAT3 pos;
        float life;
        DirectX::XMFLOAT3 vel;
        float seed;
    };


     struct RainCB
    {
        DirectX::XMFLOAT4X4 gView;
        DirectX::XMFLOAT4X4 gProj;
        DirectX::XMFLOAT4X4 gViewProj;
        DirectX::XMFLOAT4X4 gInvViewProj;

        DirectX::XMFLOAT3 gCameraPos;
        float gDt;

        DirectX::XMFLOAT3 gWind;
        float gTime;

        DirectX::XMFLOAT3 gSpawnCenter;
        float gSpawnRadius;

        float gSpawnTopY;
        float gKillBottomY;
        float gGravity;
        float gWaterY;

        DirectX::XMFLOAT2 gNearFar;   
        float gSoftRange;             
        float gRainIntensity;         

        DirectX::XMFLOAT3 gCameraVel; 
        float gPad0;                  
    };

    static constexpr UINT kMaxParticles = 131072;
    static constexpr UINT kMaxHits = 65536;

    RainCB cb{};

    // シェーダー関連
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> rain_cs; // パーティクルの位置・速度更新用
    Microsoft::WRL::ComPtr<ID3D11VertexShader>  rain_vs; // 雨粒の描画用
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   rain_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>   input_layout;

    // パーティクル用メインバッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> particleBuffer;
   
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV; 
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  particleSRV; 

    // 衝突記録用バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> hitBuffer;
   
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> hitUAV;

    // 定数バッファと描画用メッシュ
    Microsoft::WRL::ComPtr<ID3D11Buffer> quadVB; 

    std::unique_ptr<GPUConstantBuffer> cbRain;
   

    float time = 0.0f;
    DirectX::XMFLOAT3 prevCameraPos = DirectX::XMFLOAT3(0, 0, 0); // 前フレームのカメラ座標
    bool hasPrevCameraPos = false;

    // CPUへのリードバック（読み戻し）用バッファ
     // GPUで計算した水面との衝突位置を取得し、Waterシミュレーションに波紋を発生させるために使用
    Microsoft::WRL::ComPtr<ID3D11Buffer> hitCountBuffer;   // GPU上でのヒット数保存用
    Microsoft::WRL::ComPtr<ID3D11Buffer> hitCountStaging;  // CPU読み取り用のヒット数バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> hitStaging;       // CPU読み取り用のヒット座標データバッファ
};
