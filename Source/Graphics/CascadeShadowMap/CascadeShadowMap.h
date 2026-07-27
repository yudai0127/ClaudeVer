#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>
#include "Graphics/ShaderConstants.h"
#include "Graphics/GPUConstantBuffer.h"

class CascadeShadowMap
{
public:
    // シャドウマップのサイズ
    static constexpr UINT SHADOWMAP_SIZE = 2048;

    // 分割エリアテーブル
    static constexpr float SPLIT_AREA_TABLE[CASCADE_COUNT + 1] = {
        0.1f,   // Near
        25.0f,
        100.0f,
        250.0f,
        500.0f  // Far
    };

public:
    CascadeShadowMap(ID3D11Device* device);
    ~CascadeShadowMap() = default;

    // 初期化
    bool initialize(ID3D11Device* device);

    void CalculateCascades(
        ID3D11DeviceContext* dc,
        const DirectX::XMFLOAT4X4& cameraView,
        const DirectX::XMFLOAT4X4& cameraProjection,
        const DirectX::XMFLOAT4& lightDirection,
        float fovY,
        float aspectRatio,
        const DirectX::XMFLOAT3& cameraPosition
    );

    // シャドウマップ描画開始
    void beginShadowRender(ID3D11DeviceContext* dc, int cascadeIndex);

    // シャドウマップ描画終了
    void endShadowRender(ID3D11DeviceContext* dc);

    // シェーダーリソースの設定
    void setShaderResources(ID3D11DeviceContext* dc, UINT startSlot = 10);

    // 定数バッファの設定
    void setConstantBuffer(ID3D11DeviceContext* dc, UINT slot = 4);

    

    // シャドウマップのクリア
    void clearShadowMaps(ID3D11DeviceContext* dc);

    ID3D11ShaderResourceView* getShadowMapSRV(int index) const;
    const CascadeConstants& getConstants() const { return cascadeConstants; }
    CascadeConstants& getConstants() { return cascadeConstants; }

    void UnbindShaderResources(ID3D11DeviceContext* dc, UINT startSlot);

private:
  
    void calculateFrustumVertices(
        const DirectX::XMFLOAT3& cameraPos,
        const DirectX::XMFLOAT3& cameraFront,
        const DirectX::XMFLOAT3& cameraUp,
        const DirectX::XMFLOAT3& cameraRight,
        float nearDepth,
        float farDepth,
        float fovY,
        float aspectRatio,
        DirectX::XMVECTOR* vertices
    );

    // ライトビュープロジェクション行列の計算
    DirectX::XMMATRIX calculateLightViewProjection(
        const DirectX::XMVECTOR* frustumVertices,
        const DirectX::XMMATRIX& lightView
    );

private:
    // シャドウマップリソース
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> shadowMapDSVs[CASCADE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadowMapSRVs[CASCADE_COUNT];

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
    std::unique_ptr<GPUConstantBuffer> co;
    CascadeConstants cascadeConstants;

};