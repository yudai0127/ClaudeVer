#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include "Camera/Camera.h"
#include <DirectXMath.h>
#include "Graphics/GPUConstantBuffer.h"


class FrameBuffer;
class Fullscreen_Quad;

class DepthOfField
{
public:
    DepthOfField() = default;
    ~DepthOfField() = default;

    bool initialize(ID3D11Device* device, UINT width, UINT height);
    void resize(ID3D11Device* device, UINT width, UINT height);
   

    void render(ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* sceneColorSRV,
        ID3D11ShaderResourceView* depthSRV,
        ID3D11RenderTargetView* outputRTV);



    ID3D11ShaderResourceView* getHalfCoCSRV() const;
    ID3D11ShaderResourceView* getHalfBlurSRV() const;

    struct Params
    {
        float gNear;
        float gFar;
        float gFocusDist;
        float gFocusRange;
        float gMaxCoC;
        float gReversedZ;
        float padding[2];
    };
    void debugGui(Params* params, bool* pEnable, ID3D11ShaderResourceView* depthSRV);

    void SetParams(const Params& param);
    const Params& GetParams() const { return params; }
private:
    struct BlurParams
    {
        DirectX::XMFLOAT2 gInvHalfRes;
        DirectX::XMFLOAT2 padding;
    };

    void UnbindSRVs(ID3D11DeviceContext* dc, UINT startSlot, UINT count);

    // 定数バッファ
    
    std::unique_ptr<GPUConstantBuffer> cbParams;
    std::unique_ptr<GPUConstantBuffer> cbBlur;

    // ピクセルシェーダー（DoFの各パス用）
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psDownsample; // 解像度半減 ＋ 錯乱円(CoC)の計算用
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psBlurH;      // 水平方向のガウスぼかし用
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psBlurV;      // 垂直方向のガウスぼかし用
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psComposite;  // 元画像とボケ画像の最終合成用

    // 中間レンダリング用フレームバッファ
    std::unique_ptr<FrameBuffer> halfColorCoC;  // ダウンサンプルされたカラーとCoCを格納
    std::unique_ptr<FrameBuffer> halfBlurTemp;  // 水平ブラー適用後のテクスチャ
    std::unique_ptr<FrameBuffer> halfBlur;      // 垂直ブラー適用後のテクスチャ

    // フルスクリーン描画用ヘルパー
    std::unique_ptr<Fullscreen_Quad> blit;

    
    Params params{};

    // 画面解像度と内部バッファ（ハーフ）の解像度管理
    UINT width = 0;
    UINT height = 0;
    UINT halfWidth = 0;
    UINT halfHeight = 0;
   
};