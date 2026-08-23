#include "Water.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/DeviceManager/DeviceManager.h"
#include "Graphics/Texture/Texture.h"
#include "Graphics/Buffer.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "misc.h"
#include "Graphics/Debug/ImGuiRenderer.h"

#include <vector>
#include <cstring>
#include <cassert>

using namespace DirectX;



bool RippleSimulation::Initialize(ID3D11Device* device, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;

    
    HRESULT hr = ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\RippleSim_CS.cso", simCS.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    hr = ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\RippleInject_CS.cso", injectCS.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

   
   
    simcb = std::make_unique<GPUConstantBuffer>(device, sizeof(SimCB));
    injectcb = std::make_unique<GPUConstantBuffer>(device, sizeof(InjectCB));

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // height, velocity
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    for (int i = 0; i < 2; ++i)
    {
        hr = device->CreateTexture2D(&texDesc, nullptr, textures[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
        hr = device->CreateShaderResourceView(textures[i].Get(), nullptr, srvs[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
        hr = device->CreateUnorderedAccessView(textures[i].Get(), nullptr, uavs[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    
    simParams.c = 15.0f;      // 波の伝播速度 (texels/sec)
    simParams.damping = 1.5f; // 減衰係数

    return true;
}

void RippleSimulation::update(ID3D11DeviceContext* dc, float dt)
{
    dc->CSSetShader(simCS.Get(), nullptr, 0);

    simParams.texel = { 1.0f / width, 1.0f / height };
    simParams.dt = dt;
  
    
    simcb->UploadData<SimCB>(dc, 0, simParams, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ false, /*CS*/ true);

    ID3D11ShaderResourceView* srv = srvs[currentBufferIdx].Get();
    ID3D11UnorderedAccessView* uav = uavs[1 - currentBufferIdx].Get();
    dc->CSSetShaderResources(0, 1, &srv);
    dc->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

    
    dc->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetShaderResources(0, 1, nullSRV);
    dc->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

    // Ping-pong
    currentBufferIdx = 1 - currentBufferIdx;
}

void RippleSimulation::InjectRipple(ID3D11DeviceContext* dc, const DirectX::XMFLOAT2& center, float strength, float radius)
{
    dc->CSSetShader(injectCS.Get(), nullptr, 0);

    
    cb.center = { (uint32_t)center.x, (uint32_t)center.y };
    cb.velocity = strength;
    cb.radius = radius;
    cb.falloff = 1.0f;
    injectcb->UploadData<InjectCB>(dc, 0, cb, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ false, /*CS*/ true);

  
    ID3D11UnorderedAccessView* uav = uavs[currentBufferIdx].Get();
    dc->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

   dc->Dispatch(1, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}






bool Water_Simulation::CreateGridMesh(ID3D11Device* device, uint32_t gridWidth, uint32_t gridHeight)
{
    if (!device) return false;
    if (gridWidth < 2 || gridHeight < 2) return false;

    const float sizeX = worldSizeX;
    const float sizeZ = worldSizeZ;
    const float dx = sizeX / float(gridWidth - 1);
    const float dz = sizeZ / float(gridHeight - 1);
    const float x0 = -sizeX * 0.5f;
    const float z0 = -sizeZ * 0.5f;

    std::vector<Vertex> verts;
    verts.reserve(size_t(gridWidth) * size_t(gridHeight));

    for (uint32_t z = 0; z < gridHeight; ++z)
    {
        for (uint32_t x = 0; x < gridWidth; ++x)
        {
            Vertex v{};
            v.pos = XMFLOAT3(x0 + dx * float(x), 0.0f, z0 + dz * float(z));
            v.nrm = XMFLOAT3(0.0f, 1.0f, 0.0f);
            v.uv = XMFLOAT2(float(x) / float(gridWidth - 1), float(z) / float(gridHeight - 1));
            verts.push_back(v);
        }
    }

    std::vector<uint32_t> idx;
    idx.reserve((gridWidth - 1) * (gridHeight - 1) * 6u);

    for (uint32_t z = 0; z < gridHeight - 1; ++z)
    {
        for (uint32_t x = 0; x < gridWidth - 1; ++x)
        {
            uint32_t i0 = z * gridWidth + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + gridWidth;
            uint32_t i3 = i2 + 1;


            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);

            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }
    }


    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.ByteWidth = static_cast<UINT>(verts.size() * sizeof(Vertex));
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA vbInit{ verts.data(), 0, 0 };
    HRESULT hr = device->CreateBuffer(&vbDesc, &vbInit, vertexBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.ByteWidth = static_cast<UINT>(idx.size() * sizeof(uint32_t));
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA ibInit{ idx.data(), 0, 0 };
    hr = device->CreateBuffer(&ibDesc, &ibInit, indexBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    indexCount = static_cast<uint32_t>(idx.size());
    return true;
}

// 初期化
bool Water_Simulation::Initialize(ID3D11Device* device, uint32_t gridWidth, uint32_t gridHeight)
{
    if (!device) return false;

    // シェーダ読み込み 
    D3D11_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, nrm), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Water_VS.cso",
        watervs.GetAddressOf(), waterinputLayout.GetAddressOf(),
        inputLayout, _countof(inputLayout));
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Water_PS.cso", waterps.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


    hr = ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Water_Prepass_PS.cso", prepassPixelShader.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    // グリッドメッシュ作成
    if (!CreateGridMesh(device, gridWidth, gridHeight))
        return false;


  
    cbwater = std::make_unique<GPUConstantBuffer>(device, sizeof(CB_Water));
    pspara = std::make_unique<GPUConstantBuffer>(device, sizeof(PSParams));

    D3D11_TEXTURE2D_DESC texDesc{};
    TextureManager::instance()->loadTextureFromFile(device, L".\\Resources\\Texture\\Water\\waterNormals1.png", normalMapSRV[0].GetAddressOf(), &texDesc);
    normalMapSRV[1] = normalMapSRV[0];
    normalMapSRV[2] = normalMapSRV[0];


    psParams.normal0 = { 0.02f, 0.01f,  3.0f, 1.0f };
    psParams.normal1 = { -0.015f, 0.02f, 8.0f, 0.72f };
    psParams.normal2 = { 0.01f, -0.008f, 18.0f, 0.48f };
    psParams.misc = XMFLOAT4(0.02f, 0.12f, 1.35f, 0.0f);
   
    psParams.iblParams = XMFLOAT4(3.50f, 1.0f, 1.0f, 1.80f);
    psParams.waterTint = XMFLOAT4(0.018f, 0.18f, 0.24f, 0.075f);
    
    psParams.alphaParam = XMFLOAT4(0.16f, 0.80f, 0.10f, 0.85f);
    psParams.rippleParams = XMFLOAT4(1.2f, 0.0f, 0.0f, 70.0f);
    
    psParams.shadingParams = XMFLOAT4(0.75f, 0.016f, 26000.0f, 260.0f);

    
    waves[0] = { {0.7f, 0.7f}, 180.0f, 3000.0f, 0.5f, 0.35f };
    waves[1] = { {-0.3f, 1.0f},100.0f, 2000.0f, 0.7f, 0.3f };
    waves[2] = { {1.0f, 0.2f}, 45.0f, 700.0f, 0.9f, 0.5f };
    waves[3] = { {-0.8f, 0.4f}, 20.0f, 300.0f, 1.2f, 0.55f };
    waves[4] = { {0.25f, -1.0f}, 11.0f, 150.0f, 1.65f, 0.42f };
    waves[5] = { {-1.0f, -0.15f}, 6.0f, 72.0f, 2.10f, 0.36f };
    waves[6] = { {0.82f, -0.58f}, 3.2f, 34.0f, 2.75f, 0.30f };
    waves[7] = { {-0.45f, -0.89f}, 1.6f, 16.0f, 3.40f, 0.24f };


    // 波紋シミュレーションの初期化
    if (!rippleSim.Initialize(device, 1024, 1024))
    {
        OutputDebugStringA("FATAL ERROR: Failed to initialize RippleSimulation.\n");
        return false;
    }

    const float rippleTexelX = 1.0f / float(rippleSim.getWidth());
    const float rippleTexelY = 1.0f / float(rippleSim.getHeight());
    psParams.rippleParams.y = rippleTexelX;
    psParams.rippleParams.z = rippleTexelY;

    return true;
}


bool Water_Simulation::CreateRenderTargets(ID3D11Device* device, uint32_t width, uint32_t height)
{
    if (!device) return false;
    if (width == 0 || height == 0) return false;

    screenSize = { static_cast<float>(width), static_cast<float>(height) };

    HRESULT hr = S_OK;

    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        normalRTTex.Reset();
        normalRTV.Reset();
        normalSRV.Reset();

        hr = device->CreateTexture2D(&desc, nullptr, normalRTTex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
        hr = device->CreateRenderTargetView(normalRTTex.Get(), nullptr, normalRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(normalRTTex.Get(), &srvDesc, normalSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        depthRTTex.Reset();
        depthRTV.Reset();
        depthSRV.Reset();

        hr = device->CreateTexture2D(&desc, nullptr, depthRTTex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
        hr = device->CreateRenderTargetView(depthRTTex.Get(), nullptr, depthRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(depthRTTex.Get(), &srvDesc, depthSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    return true;
}


void Water_Simulation::update(ID3D11DeviceContext* dc, float elapsedTime,
    const XMMATRIX& world,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMMATRIX& viewProjection,
    const XMFLOAT3& cameraPos)
{
    if (!pauseds) {
        time += elapsedTime;

        
        constexpr float RIPPLE_FIXED_STEP = 1.0f / 30.0f;
        rippleUpdateAccumulator += elapsedTime;
        if (rippleUpdateAccumulator >= RIPPLE_FIXED_STEP)
        {
            rippleSim.update(dc, RIPPLE_FIXED_STEP);
            rippleUpdateAccumulator -= RIPPLE_FIXED_STEP;
            rippleUpdateAccumulator = min(rippleUpdateAccumulator, RIPPLE_FIXED_STEP);
        }
    }

    

    const XMMATRIX liftT = XMMatrixTranslation(0.0f, worldOffsetY, 0.0f);
    const XMMATRIX worldLifted = XMMatrixMultiply(world, liftT);

    XMStoreFloat4x4(&cb.gWorld, worldLifted);
    XMStoreFloat4x4(&cb.gViewProjection, viewProjection);

    cb.gCameraPos = cameraPos;
    cb._pad_cam = 0.0f;

    cb.gTime = time;
    cb.gGravity = 9.81f;
    cb.gWaveCount = 8u;
    cb._pad0 = 0.0f;

    const float waveAmpScale = this->waveAmpScale;
    const float waveSteepScale = this->waveSteepScale;

    for (int i = 0; i < 8; ++i)
    {
        cb.gWaves[i] = waves[i];
        cb.gWaves[i].amplitude *= waveAmpScale;
        cb.gWaves[i].steepness *= waveSteepScale;
    }

    cbwater->UploadData<CB_Water>(dc, 6, cb, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
    
    const XMMATRIX invWLifted = XMMatrixInverse(nullptr, worldLifted);
    XMStoreFloat4x4(&lastWorld, worldLifted);
    XMStoreFloat4x4(&lastInvWorld, invWLifted);

    const XMMATRIX invView = XMMatrixInverse(nullptr, view);
    const XMMATRIX invProjection = XMMatrixInverse(nullptr, projection);

    XMStoreFloat4x4(&psParams.gInvView, invView);
    XMStoreFloat4x4(&psParams.gInvProjection, invProjection);
    psParams.gScreenSize = screenSize;

    worldCenter = { 0.0f, worldOffsetY, 0.0f };

    pspara->UploadData<PSParams>(dc, 7, psParams, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
}



void Water_Simulation::render(ID3D11DeviceContext* dc,
    ID3D11ShaderResourceView* pSceneSRV,
    ID3D11ShaderResourceView* pSceneNormalSRV,
    ID3D11ShaderResourceView* pCausticsSRV,
    ID3D11ShaderResourceView* pSkyCubeSRV,
    ID3D11ShaderResourceView* pSSRReflectionSRV,
    ID3D11ShaderResourceView* pRippleDisplacementSRV,
    ID3D11ShaderResourceView* pDepthSRV)
{
    if (!dc || !watervs || !indexBuffer) return;

    cbwater->UploadData<CB_Water>(dc, 6, cb, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
    pspara->UploadData<PSParams>(dc, 7, psParams, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);


    

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vb = vertexBuffer.Get();
    dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    dc->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetInputLayout(waterinputLayout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->VSSetShader(watervs.Get(), nullptr, 0);
    dc->PSSetShader(waterps.Get(), nullptr, 0);

 

    ID3D11ShaderResourceView* nm0 = normalMapSRV[0] ? normalMapSRV[0].Get() : nullptr;
    ID3D11ShaderResourceView* nm1 = normalMapSRV[1] ? normalMapSRV[1].Get() : nm0;
    ID3D11ShaderResourceView* nm2 = normalMapSRV[2] ? normalMapSRV[2].Get() : nm0;

    ID3D11ShaderResourceView* srvs[10] =
    {
        nm0, nm1, nm2,
        pSceneSRV,
        pSkyCubeSRV,
        pSSRReflectionSRV,
        pRippleDisplacementSRV,
        pDepthSRV,
        pSceneNormalSRV,
        pCausticsSRV
    };
    dc->PSSetShaderResources(0, 10, srvs);
    dc->VSSetShaderResources(6, 1, &pRippleDisplacementSRV);

    ID3D11SamplerState* samplers[8] = {};
    samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
    samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
    samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
    samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();
    samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();
    samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
    samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
    samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

    dc->PSSetSamplers(0, _countof(samplers), samplers);
    dc->VSSetSamplers(0, _countof(samplers), samplers);

    dc->DrawIndexed(indexCount, 0, 0);

    ID3D11ShaderResourceView* nullSRVs[10] = { nullptr };
    dc->PSSetShaderResources(0, 10, nullSRVs);
    ID3D11ShaderResourceView* nullVsSRVs[1] = { nullptr };
    dc->VSSetShaderResources(6, 1, nullVsSRVs);

    ID3D11SamplerState* nullSmp[8] = { nullptr };
    dc->PSSetSamplers(0, 8, nullSmp);
    dc->VSSetSamplers(0, 8, nullSmp);
}

void Water_Simulation::renderGBufferPrepass(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* pRippleDisplacementSRV)
{
    if (!dc || !vertexBuffer || !indexBuffer) return;

    cbwater->UploadData<CB_Water>(dc, 6, cb, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
    pspara->UploadData<PSParams>(dc, 7, psParams, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);


    

    dc->VSSetShader(watervs.Get(), nullptr, 0);
    dc->PSSetShader(prepassPixelShader.Get(), nullptr, 0); 

    

    ID3D11ShaderResourceView* nm0 = normalMapSRV[0] ? normalMapSRV[0].Get() : nullptr;
    ID3D11ShaderResourceView* nm1 = normalMapSRV[1] ? normalMapSRV[1].Get() : nm0;
    ID3D11ShaderResourceView* nm2 = normalMapSRV[2] ? normalMapSRV[2].Get() : nm0;

    ID3D11ShaderResourceView* srvs[7] = { nm0, nm1, nm2, nullptr, nullptr, nullptr, pRippleDisplacementSRV };
    dc->PSSetShaderResources(0, 7, srvs);
    dc->VSSetShaderResources(6, 1, &pRippleDisplacementSRV);


    ID3D11SamplerState* samplers[8] = {};
    samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
    samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
    samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
    samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();
    samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();
    samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
    samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
    samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();


    dc->PSSetSamplers(0, _countof(samplers), samplers);
    dc->VSSetSamplers(0, _countof(samplers), samplers);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vb = vertexBuffer.Get();
    dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    dc->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetInputLayout(waterinputLayout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->DrawIndexed(indexCount, 0, 0);

    ID3D11ShaderResourceView* nullSRVs[9] = {}; 
    dc->PSSetShaderResources(0, 9, nullSRVs);

    ID3D11ShaderResourceView* nullVsSRVs[1] = { nullptr };
    dc->VSSetShaderResources(6, 1, nullVsSRVs);

    ID3D11SamplerState* nullSmp[8] = { nullptr };
    dc->PSSetSamplers(0, 8, nullSmp);
    dc->VSSetSamplers(0, 8, nullSmp);
}

void Water_Simulation::renderCaustics(
    ID3D11DeviceContext* dc,
    ID3D11VertexShader* pCausticsVS,
    ID3D11PixelShader* pCausticsPS,
    ID3D11ShaderResourceView* pDepthSRV)
{
    if (!dc || !pCausticsVS || !pCausticsPS) return;

    cbwater->UploadData<CB_Water>(dc, 6, cb, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
   
    pspara->UploadData<PSParams>(dc, 7, psParams, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);

    //////定数バッファの更新
    

    //サンプラーの設定
    ID3D11SamplerState* samplers[2] = {
        GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get(),
        GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get()
    };
    dc->VSSetSamplers(0, 2, samplers);

    //Zバッファに加えてノーマルマップも頂点シェーダーに渡す
    ID3D11ShaderResourceView* nm0 = normalMapSRV[0] ? normalMapSRV[0].Get() : nullptr;
    ID3D11ShaderResourceView* nm1 = normalMapSRV[1] ? normalMapSRV[1].Get() : nm0;
    ID3D11ShaderResourceView* srvs[3] = { pDepthSRV, nm0, nm1 };
    dc->VSSetShaderResources(0, 3, srvs);

    dc->IASetInputLayout(waterinputLayout.Get());
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->VSSetShader(pCausticsVS, nullptr, 0);
    dc->PSSetShader(pCausticsPS, nullptr, 0);

    // 描画実行
    dc->DrawIndexed(indexCount, 0, 0);

    // リソース解除
    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    dc->VSSetShaderResources(0, 3, nullSRVs);
}

void Water_Simulation::InjectRippleWorld(ID3D11DeviceContext* dc,
    const DirectX::XMFLOAT3& hitWorldPos,
    float strength,
    float radiusWorld)
{
    if (!dc) return;

    DirectX::XMMATRIX invW = DirectX::XMLoadFloat4x4(&lastInvWorld);
    DirectX::XMVECTOR pW = DirectX::XMLoadFloat3(&hitWorldPos);
    DirectX::XMVECTOR pL = DirectX::XMVector3TransformCoord(pW, invW);

    DirectX::XMFLOAT3 local{};
    DirectX::XMStoreFloat3(&local, pL);

    const float sizeX = worldSizeX;
    const float sizeZ = worldSizeZ;

    float u = (local.x / sizeX) + 0.5f;
    float v = (local.z / sizeZ) + 0.5f;

    // 水面外なら捨てる
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return;

    const uint32_t rw = rippleSim.getWidth(); 
    const uint32_t rh = rippleSim.getHeight();

    uint32_t cx = (uint32_t)min(max(u * (rw - 1), 0.0f), float(rw - 1));
    uint32_t cy = (uint32_t)min(max(v * (rh - 1), 0.0f), float(rh - 1));

    float texelWorldX = sizeX / float(rw - 1);
    float texelWorldZ = sizeZ / float(rh - 1);
    float texelWorld = 0.5f * (texelWorldX + texelWorldZ);

    float radiusPix = max(radiusWorld / texelWorld, 0.0f);

    float vel = -strength;

    rippleSim.InjectRipple(dc, DirectX::XMFLOAT2((float)cx, (float)cy), vel, radiusPix);
}


void Water_Simulation::debugGui()
{
    ImGui::TextUnformatted("Water appearance presets");
    if (ImGui::Button("Clear Ocean")) ApplyPreset(Preset::ClearOcean);
    ImGui::SameLine();
    if (ImGui::Button("Tropical")) ApplyPreset(Preset::Tropical);
    ImGui::SameLine();
    if (ImGui::Button("Murky Harbor")) ApplyPreset(Preset::MurkyHarbor);
    ImGui::SameLine();
    if (ImGui::Button("Storm")) ApplyPreset(Preset::Storm);

    if (ImGui::TreeNode("Gerstner Waves"))
    {
        ImGui::DragFloat("World Offset Y", &worldOffsetY, 0.01f);
        ImGui::DragFloat("Global Amplitude", &waveAmpScale, 0.005f, 0.0f, 1.0f);
        ImGui::DragFloat("Global Steepness", &waveSteepScale, 0.01f, 0.0f, 2.0f);

        for (int i = 0; i < 8; ++i)
        {
            char label[32];
            sprintf_s(label, "Wave %d", i);
            if (ImGui::TreeNode(label))
            {
                ImGui::DragFloat2("Direction", &waves[i].direction.x, 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat("Amplitude", &waves[i].amplitude, 0.001f, 0.0f, 5000.0f);
                ImGui::DragFloat("Wavelength", &waves[i].wavelength, 0.1f, 0.1f, 10000.0f);
                ImGui::DragFloat("Speed", &waves[i].speed, 0.01f, 0.0f, 100.0f);
                ImGui::DragFloat("Steepness", &waves[i].steepness, 0.01f, 0.0f, 5.0f);
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
  
    if (ImGui::TreeNode("Shading"))
    {

       
        if (psParams.misc.x < 0.005f) psParams.misc.x = 0.005f;
        if (psParams.misc.x > 0.08f) psParams.misc.x = 0.08f;
        ImGui::ColorEdit3("Tint", &psParams.waterTint.x);
        ImGui::DragFloat("Absorption", &psParams.waterTint.w, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Fresnel (F0)", &psParams.misc.x, 0.001f, 0.005f, 0.08f, "%.3f");
        ImGui::TextDisabled("Water F0 is normally about 0.02; use Readability for visibility");
        ImGui::DragFloat("Refraction", &psParams.misc.y, 0.005f, 0.0f, 1.0f);
        ImGui::DragFloat("Specular", &psParams.misc.z, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Reflection Scale", &psParams.iblParams.y, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Reflection Readability", &psParams.iblParams.x, 0.02f, 1.0f, 5.0f);
        ImGui::TextDisabled("1.0 = physical Fresnel, 2.0-4.0 = clearer angular reflection");
        ImGui::DragFloat("SSR Hit Confidence", &psParams.iblParams.w, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("Subsurface Scattering", &psParams.alphaParam.x, 0.005f, 0.0f, 2.0f);
        ImGui::DragFloat("Caustics Visibility", &psParams.alphaParam.y, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Turbidity", &psParams.alphaParam.z, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Foam Intensity", &psParams.alphaParam.w, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Thickness Scale", &psParams.shadingParams.x, 0.01f, 0.0f, 5.0f);
        // 水底までの実際の深さから光路長を求めるためのスケール。
        // 上げるほど浅瀬と深場の色差がはっきりする
        ImGui::DragFloat("Depth Absorption", &psParams.shadingParams.y, 0.001f, 0.0f, 1.0f, "%.4f");
        // この距離で高周波ノイズの寄与が0になり、遠景のちらつきが収まる
        ImGui::DragFloat("Detail Fade Distance", &psParams.shadingParams.z, 100.0f, 100.0f, 200000.0f);
        ImGui::DragFloat("Shore Foam Depth", &psParams.shadingParams.w, 1.0f, 1.0f, 3000.0f);

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Normal Maps"))
    {
        ImGui::Text("Normal 0 (Scroll, Tile, Strength)");
        ImGui::DragFloat2("Scroll##N0", &psParams.normal0.x, 0.001f);
        ImGui::DragFloat("Tile##N0", &psParams.normal0.z, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Strength##N0", &psParams.normal0.w, 0.01f, 0.0f, 2.0f);

        ImGui::Text("Normal 1 (Scroll, Tile, Strength)");
        ImGui::DragFloat2("Scroll##N1", &psParams.normal1.x, 0.001f);
        ImGui::DragFloat("Tile##N1", &psParams.normal1.z, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Strength##N1", &psParams.normal1.w, 0.01f, 0.0f, 2.0f);

        ImGui::Text("Normal 2 (Scroll, Tile, Strength)");
        ImGui::DragFloat2("Scroll##N2", &psParams.normal2.x, 0.001f);
        ImGui::DragFloat("Tile##N2", &psParams.normal2.z, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Strength##N2", &psParams.normal2.w, 0.01f, 0.0f, 2.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ripple Simulation"))
    {
        ImGui::DragFloat("Ripple Scale", &psParams.rippleParams.x, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Ripple Normal Strength", &psParams.rippleParams.w, 0.5f, 1.0f, 200.0f);
        ImGui::DragFloat("Wave Speed", &rippleSim.getWaveSpeed(), 0.1f, 1.0f, 100.0f);
        ImGui::DragFloat("Damping", &rippleSim.getDamping(), 0.01f, 0.0f, 5.0f);
        ImGui::TreePop();
    }
}

void Water_Simulation::ApplyPreset(Preset preset)
{
    
    psParams.misc.x = 0.02f;

    switch (preset)
    {
    case Preset::ClearOcean:
        psParams.waterTint = XMFLOAT4(0.018f, 0.18f, 0.24f, 0.065f);
        psParams.alphaParam = XMFLOAT4(0.14f, 0.90f, 0.05f, 0.75f);
        psParams.misc.y = 0.14f;
        psParams.misc.z = 1.40f;
        psParams.iblParams.x = 3.50f;
        psParams.iblParams.y = 1.00f;
        psParams.iblParams.w = 1.80f;
        psParams.normal0.w = 0.85f;
        psParams.normal1.w = 0.55f;
        psParams.normal2.w = 0.28f;
        waveAmpScale = 0.02f;
        waveSteepScale = 0.55f;
        break;
    case Preset::Tropical:
        psParams.waterTint = XMFLOAT4(0.015f, 0.42f, 0.36f, 0.055f);
        psParams.alphaParam = XMFLOAT4(0.22f, 1.15f, 0.08f, 1.05f);
        psParams.misc.y = 0.16f;
        psParams.misc.z = 1.55f;
        psParams.iblParams.x = 3.25f;
        psParams.iblParams.y = 1.00f;
        psParams.iblParams.w = 1.75f;
        psParams.normal0.w = 0.55f;
        psParams.normal1.w = 0.30f;
        psParams.normal2.w = 0.14f;
        waveAmpScale = 0.016f;
        waveSteepScale = 0.45f;
        break;
    case Preset::MurkyHarbor:
        psParams.waterTint = XMFLOAT4(0.16f, 0.20f, 0.10f, 0.12f);
        psParams.alphaParam = XMFLOAT4(0.34f, 0.35f, 0.78f, 0.65f);
        psParams.misc.y = 0.055f;
        psParams.misc.z = 0.85f;
        psParams.iblParams.x = 2.75f;
        psParams.iblParams.y = 0.85f;
        psParams.iblParams.w = 1.50f;
        psParams.normal0.w = 0.70f;
        psParams.normal1.w = 0.45f;
        psParams.normal2.w = 0.22f;
        waveAmpScale = 0.012f;
        waveSteepScale = 0.35f;
        break;
    case Preset::Storm:
        psParams.waterTint = XMFLOAT4(0.018f, 0.07f, 0.09f, 0.15f);
        psParams.alphaParam = XMFLOAT4(0.20f, 0.20f, 0.38f, 1.45f);
        psParams.misc.y = 0.10f;
        psParams.misc.z = 1.80f;
        psParams.iblParams.x = 2.50f;
        psParams.iblParams.y = 1.05f;
        psParams.iblParams.w = 1.55f;
        psParams.normal0.w = 1.25f;
        psParams.normal1.w = 1.00f;
        psParams.normal2.w = 0.80f;
        waveAmpScale = 0.042f;
        waveSteepScale = 0.90f;
        break;
    }
}
