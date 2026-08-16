#pragma once
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "Graphics/GPUConstantBuffer.h"

class ScreenSpaceReflection
{
public:
    ScreenSpaceReflection() = default;
    ~ScreenSpaceReflection() = default;
    // カメラ関連の定数バッファ用構造体
    struct CameraCB {
        DirectX::XMFLOAT4X4 ssr_view;
        DirectX::XMFLOAT4X4 ssr_proj;
        DirectX::XMFLOAT4X4 ssr_inv_view;
        DirectX::XMFLOAT4X4 ssr_inv_proj;
        DirectX::XMFLOAT2 ssr_inv_screen_size;
        float ssr_near;
        float ssr_far;
    };
    // SSRの制御パラメータ用定数バッファ構造体
    struct ParamCB 
    {
        float	screen_space_reflection_max_distance = 1000.0f;
        float	screen_space_reflection_ray_resolution = 0.65f;
        float	screen_space_reflection_tickness = 0.5f;
        // Direction correction, UV-hole fill, roughness and sub-step search.
        int		screen_space_reflection_flags = 30;
    };
    ParamCB params;

    bool initialize(ID3D11Device* device);
    void resize(ID3D11Device* device, UINT width, UINT height);
    void update(ID3D11DeviceContext* dc,
        const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& proj,
        float nearZ, float farZ,
        UINT screenW, UINT screenH,
        const ParamCB& params);

     void render(ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* sceneColorSRV,
        ID3D11ShaderResourceView* normalRoughnessSRV,
        ID3D11ShaderResourceView* depthSRV);

     void debugGui();

   
    ID3D11ShaderResourceView* getResultSRV() const { return colorSRV.Get(); }

    ID3D11ShaderResourceView* getCompositeSRV() const { return compSRV.Get(); }

    ID3D11ShaderResourceView* getUvSRV() const { return uvSRV.Get(); }

    void setResolutionScale(float s) { if (s < 0.1f) s = 0.1f; if (s > 1.0f) s = 1.0f; resolutionScale = s; }
    float getResolutionScale() const { return resolutionScale; }

private:
    CameraCB cam{};

    ParamCB param{};
    // 定数バッファ
   /* Microsoft::WRL::ComPtr<ID3D11Buffer> cbCamera;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbParam;*/
    std::unique_ptr<GPUConstantBuffer> cbCamera;
    std::unique_ptr<GPUConstantBuffer> cbParam;

    // シェーダーリソース
    Microsoft::WRL::ComPtr<ID3D11VertexShader> ssrvs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  ssrUV;          // UV/衝突判定用
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  ssrColor;       // 反射カラー取得用
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  ssrComposite;   // 最終合成用

    // Pass1: レイマーチングによる衝突判定結果（UV座標など）を保存するテクスチャ
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uvTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> uvRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvSRV;

    // Pass2: 衝突したUVをもとにシーンから色を取得・ぼかし等を適用したテクスチャ
    Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV;

    // Pass3: 元のシーン画像と反射結果を合成した最終出力テクスチャ
    Microsoft::WRL::ComPtr<ID3D11Texture2D> compTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> compRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> compSRV;

    // 画面解像度と内部バッファの解像度管理
    UINT fullW = 0;
    UINT fullH = 0;
    UINT width = 0;
    UINT height = 0;

private:
    void UnbindSRVs(ID3D11DeviceContext* dc, UINT startSlot, UINT count);
    bool    showDebug = false;
    float resolutionScale = 0.5f;
};
