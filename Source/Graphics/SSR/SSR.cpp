#include "SSR.h"
#include "Graphics/Buffer.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "misc.h"
#include "imgui.h"
using namespace DirectX;


void ScreenSpaceReflection::UnbindSRVs(ID3D11DeviceContext* dc, UINT startSlot, UINT count)
{
    ID3D11ShaderResourceView* nulls[16] = {};
    _ASSERT(count <= 16);
    dc->PSSetShaderResources(startSlot, count, nulls);
}

bool ScreenSpaceReflection::initialize(ID3D11Device* device)
{
    if (!device) return false;

    HRESULT hr;
  
    cbCamera = std::make_unique<GPUConstantBuffer>(device, sizeof(CameraCB));
    cbParam = std::make_unique<GPUConstantBuffer>(device, sizeof(ParamCB));


    hr = ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\SSR_VS.cso",
        ssrvs.GetAddressOf(), nullptr, nullptr, 0);
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

   
    hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\SSR_UV_PS.cso",
        ssrUV.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\SSR_Color_PS.cso",
        ssrColor.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    param.screen_space_reflection_max_distance = 1000.0f;

    param.screen_space_reflection_tickness = 1.0f;
    param.screen_space_reflection_ray_resolution = 0.5f;
    param.screen_space_reflection_flags = 0;

    return true;
}

static void CreateRT(
    ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT fmt,
    Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex,
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
{
    tex.Reset();
    rtv.Reset();
    srv.Reset();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, tex.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    hr = device->CreateRenderTargetView(tex.Get(), nullptr, rtv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    hr = device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
}

void ScreenSpaceReflection::resize(ID3D11Device* dev, UINT width, UINT height)
{
    if (!dev) return;
    if (width == this->width && height == this->height && colorTex) return;
    this->width = width; this->height = height;

    UINT halfW = max(1u, width / 2);
    UINT halfH = max(1u, height / 2);

    // Pass1 と Pass2 はハーフ解像度で生成
    CreateRT(dev, halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, uvTex, uvRTV, uvSRV);
    CreateRT(dev, halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, colorTex, colorRTV, colorSRV);

    // 水面は Pass2 の反射テクスチャを直接使うため、未使用だった
    // フル解像度 Composite RT は作成しない。
    compTex.Reset();
    compRTV.Reset();
    compSRV.Reset();
}

void ScreenSpaceReflection::update(ID3D11DeviceContext* dc,
    const XMFLOAT4X4& view, const XMFLOAT4X4& proj,
    float nearZ, float farZ,
    UINT screenW, UINT screenH,
    const ParamCB& params)
{
    XMMATRIX v = XMLoadFloat4x4(&view);
    XMMATRIX p = XMLoadFloat4x4(&proj);
    XMMATRIX iv = XMMatrixInverse(nullptr, v);
    XMMATRIX ip = XMMatrixInverse(nullptr, p);

    
    cam.ssr_view = view;
    cam.ssr_proj = proj;
    XMStoreFloat4x4(&cam.ssr_inv_view, iv);
    XMStoreFloat4x4(&cam.ssr_inv_proj, ip);
    cam.ssr_inv_screen_size = XMFLOAT2(1.0f / (float)screenW, 1.0f / (float)screenH);
    cam.ssr_near = nearZ;
    cam.ssr_far = farZ;

    this->param = params;

    
}

void ScreenSpaceReflection::render(ID3D11DeviceContext* dc,
    ID3D11ShaderResourceView* sceneColorSRV,
    ID3D11ShaderResourceView* normalRoughnessSRV,
    ID3D11ShaderResourceView* depthSRV)
{
    if (!dc || !uvRTV || !colorRTV) return;

    D3D11_VIEWPORT halfVp{};
    halfVp.Width = static_cast<float>(max(1u, width / 2));
    halfVp.Height = static_cast<float>(max(1u, height / 2));
    halfVp.MinDepth = 0.0f;
    halfVp.MaxDepth = 1.0f;

    D3D11_VIEWPORT fullVp{};
    fullVp.Width = static_cast<float>(width);
    fullVp.Height = static_cast<float>(height);
    fullVp.MinDepth = 0.0f;
    fullVp.MaxDepth = 1.0f;

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    cbCamera->UploadData<CameraCB>(dc, 6, cam, false, false, false, false, true, false);
    cbParam->UploadData<ParamCB>(dc, 7, param, false, false, false, false, true, false);

    dc->VSSetShader(ssrvs.Get(), nullptr, 0);
    
    

    ID3D11SamplerState* samplers[8] = {};
    samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
    samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
    samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
    samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();   // Slot 3
    samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();  // Slot 4
    samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
    samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
    samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

    dc->PSSetSamplers(0, _countof(samplers), samplers);
    

    float clear[4] = { 0,0,0,0 };


    // Pass1: UV 
    dc->RSSetViewports(1, &halfVp);
    dc->OMSetRenderTargets(1, uvRTV.GetAddressOf(), nullptr);
    dc->ClearRenderTargetView(uvRTV.Get(), clear);

    {
        ID3D11ShaderResourceView* srvs[3] = { sceneColorSRV, normalRoughnessSRV, depthSRV };
        dc->PSSetShaderResources(20, 3, srvs);

        dc->PSSetShader(ssrUV.Get(), nullptr, 0);
        dc->Draw(4, 0);

        UnbindSRVs(dc, 20, 3);
    }

    // Pass2: Color
    dc->RSSetViewports(1, &halfVp);
    dc->OMSetRenderTargets(1, colorRTV.GetAddressOf(), nullptr);
    dc->ClearRenderTargetView(colorRTV.Get(), clear);

    {
        ID3D11ShaderResourceView* srvs[3] = { sceneColorSRV, uvSRV.Get(), nullptr };
        dc->PSSetShaderResources(20, 3, srvs);

        dc->PSSetShader(ssrColor.Get(), nullptr, 0);
        dc->Draw(4, 0);

        UnbindSRVs(dc, 20, 3);
    }

    // 後続のコースティクス・水面描画へ半解像度 viewport を持ち越さない。
    dc->RSSetViewports(1, &fullVp);

    ID3D11SamplerState* nullSamplers[8] = { nullptr };
    dc->PSSetSamplers(0, 8, nullSamplers);
}


void ScreenSpaceReflection::debugGui()
{
    
    ImGui::Separator();
    ImGui::Text("Screen Space Reflection");

    ImGui::DragFloat("Max Distance", &params.screen_space_reflection_max_distance, 1.0f, 0.0f, 10000.0f);
    ImGui::DragFloat("Ray Resolution", &params.screen_space_reflection_ray_resolution, 0.01f, 0.01f, 2.0f);
    ImGui::DragFloat("Thickness", &params.screen_space_reflection_tickness, 0.01f, 0.0f, 10.0f);
    ImGui::InputInt("Flags (bitmask)", &params.screen_space_reflection_flags);

    ImGui::Checkbox("Show SSR Debug", &showDebug);

    // デバッグ画像表示
    if (showDebug)
    {
        ImGui::Separator();
        ImGui::Text("SSR Debug Textures");

        if (uvSRV)
        {
            ImGui::Text("UV (Pass1)");
            ImGui::Image(reinterpret_cast<ImTextureID>(uvSRV.Get()), ImVec2(256, 144));
        }
        if (colorSRV)
        {
            ImGui::Text("Color (Pass2)");
            ImGui::Image(reinterpret_cast<ImTextureID>(colorSRV.Get()), ImVec2(256, 144));
        }
    }
}
