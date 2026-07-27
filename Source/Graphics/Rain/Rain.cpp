
#include "Rain.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/DeviceManager/DeviceManager.h"
#include "Graphics/Buffer.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "misc.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float kDefaultNear = 0.1f;
    constexpr float kDefaultFar = 1000.0f;
    constexpr float kEpsilon = 1e-6f;

    constexpr float kParticleWindX = 2.0f;
    constexpr float kParticleWindY = 0.0f;
    constexpr float kParticleWindZ = 1.0f;

    constexpr float kSpawnRadius = 60.0f;
    constexpr float kSpawnTopOffsetY = 40.0f;
    constexpr float kKillBottomOffsetY = 20.0f;

    constexpr float kGravity = -30.0f;
    constexpr float kWaterY = 0.0f;
    constexpr float kSoftRange = 1.0f;
    constexpr float kRainIntensity = 0.35f;

    constexpr UINT kRainCsThreadGroupSize = 256;
}

struct QuadVtx
{
    float px, py;
    float u, v;
};

static XMFLOAT4X4 XMToF4x4(const XMMATRIX& m)
{
    XMFLOAT4X4 out;
    XMStoreFloat4x4(&out, m);
    return out;
}

static DirectX::XMFLOAT2 ExtractNearFarFromProj(const DirectX::XMFLOAT4X4& proj)
{
    const float m33 = proj._33;
    const float m43 = proj._43;

    float n = kDefaultNear;
    float f = kDefaultFar;

    if (fabsf(m33) > kEpsilon)
    {
        n = fabsf(-m43 / m33);

        const float denom = 1.0f - (1.0f / m33);
        if (fabsf(denom) > kEpsilon)
        {
            f = n / denom;
            f = fabsf(f);
        }
    }

    return DirectX::XMFLOAT2(n, f);
}

void RainSystem::initialize(ID3D11Device* device)
{

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\RainUpdate_CS.cso", rain_cs.GetAddressOf());
    ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\RainRender_VS.cso",
        rain_vs.GetAddressOf(), input_layout.GetAddressOf(), layout, (UINT)_countof(layout));
    ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\RainRender_PS.cso", rain_ps.GetAddressOf());


    std::vector<Particle> init(kMaxParticles);
    for (auto& p : init)
    {
        p.pos = XMFLOAT3(0, 0, 0);
        p.vel = XMFLOAT3(0, 0, 0);
        p.life = 0.0f;
        p.seed = 0.0f;
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(sizeof(Particle) * kMaxParticles);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Particle);

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = init.data();

    device->CreateBuffer(&bd, &sd, particleBuffer.GetAddressOf());

    // UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavd.Buffer.FirstElement = 0;
    uavd.Buffer.NumElements = kMaxParticles;
    uavd.Format = DXGI_FORMAT_UNKNOWN;
    device->CreateUnorderedAccessView(particleBuffer.Get(), &uavd, particleUAV.GetAddressOf());

    // SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvd.Buffer.FirstElement = 0;
    srvd.Buffer.NumElements = kMaxParticles;
    srvd.Format = DXGI_FORMAT_UNKNOWN;
    device->CreateShaderResourceView(particleBuffer.Get(), &srvd, particleSRV.GetAddressOf());


    D3D11_BUFFER_DESC hbd{};
    hbd.ByteWidth = UINT(sizeof(XMFLOAT4) * kMaxHits);
    hbd.Usage = D3D11_USAGE_DEFAULT;
    hbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    hbd.CPUAccessFlags = 0;
    hbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    hbd.StructureByteStride = sizeof(XMFLOAT4);
    device->CreateBuffer(&hbd, nullptr, hitBuffer.GetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC huavd{};
    huavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    huavd.Buffer.FirstElement = 0;
    huavd.Buffer.NumElements = kMaxHits;
    huavd.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    huavd.Format = DXGI_FORMAT_UNKNOWN;
    device->CreateUnorderedAccessView(hitBuffer.Get(), &huavd, hitUAV.GetAddressOf());



   
  

    cbRain = std::make_unique<GPUConstantBuffer>(device, sizeof(RainCB));


    QuadVtx quad[6] =
    {
        {-0.5f,-0.5f, 0,1},
        { 0.5f,-0.5f, 1,1},
        { 0.5f, 0.5f, 1,0},

        {-0.5f,-0.5f, 0,1},
        { 0.5f, 0.5f, 1,0},
        {-0.5f, 0.5f, 0,0},
    };

    D3D11_BUFFER_DESC qbd{};
    qbd.ByteWidth = sizeof(quad);
    qbd.Usage = D3D11_USAGE_IMMUTABLE;
    qbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA qsd{};
    qsd.pSysMem = quad;
    device->CreateBuffer(&qbd, &qsd, quadVB.GetAddressOf());


   

    {
        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = sizeof(UINT);
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        cbd.CPUAccessFlags = 0;
        cbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        device->CreateBuffer(&cbd, nullptr, hitCountBuffer.GetAddressOf());


        D3D11_BUFFER_DESC csbd{};
        csbd.ByteWidth = sizeof(UINT);
        csbd.Usage = D3D11_USAGE_STAGING;
        csbd.BindFlags = 0;
        csbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        csbd.MiscFlags = 0;
        device->CreateBuffer(&csbd, nullptr, hitCountStaging.GetAddressOf());


        D3D11_BUFFER_DESC hs{};
        hs.ByteWidth = UINT(sizeof(DirectX::XMFLOAT4) * kMaxHits);
        hs.Usage = D3D11_USAGE_STAGING;
        hs.BindFlags = 0;
        hs.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hs.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        hs.StructureByteStride = sizeof(DirectX::XMFLOAT4);
        device->CreateBuffer(&hs, nullptr, hitStaging.GetAddressOf());
    }
}

void RainSystem::update(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* weatherSRV, float dt, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& cameraPos)
{
    time += dt;

    DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&view);
    DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&proj);
    DirectX::XMMATRIX VP = V * P;
    DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, VP);

   
    cb.gView = XMToF4x4(DirectX::XMMatrixTranspose(V));
    cb.gProj = XMToF4x4(DirectX::XMMatrixTranspose(P));
    cb.gViewProj = XMToF4x4(DirectX::XMMatrixTranspose(VP));
    cb.gInvViewProj = XMToF4x4(DirectX::XMMatrixTranspose(invVP));
    cb.gCameraPos = cameraPos;
    cb.gDt = dt;

    cb.gWind = DirectX::XMFLOAT3(kParticleWindX, kParticleWindY, kParticleWindZ);
    cb.gTime = time;

    cb.gSpawnCenter = cameraPos;
    cb.gSpawnRadius = kSpawnRadius;
    cb.gSpawnTopY = cameraPos.y + kSpawnTopOffsetY;
    cb.gKillBottomY = cameraPos.y - kKillBottomOffsetY;

    cb.gGravity = kGravity;
    cb.gWaterY = kWaterY;

    cb.gNearFar = ExtractNearFarFromProj(proj);

    cb.gSoftRange = kSoftRange;
    cb.gRainIntensity = kRainIntensity;

    if (!hasPrevCameraPos || dt <= kEpsilon)
    {
        cb.gCameraVel = DirectX::XMFLOAT3(0, 0, 0);
        prevCameraPos = cameraPos;
        hasPrevCameraPos = true;
    }
    else
    {
        cb.gCameraVel = DirectX::XMFLOAT3(
            (cameraPos.x - prevCameraPos.x) / dt,
            (cameraPos.y - prevCameraPos.y) / dt,
            (cameraPos.z - prevCameraPos.z) / dt
        );
        prevCameraPos = cameraPos;
    }

    cbRain->UploadData<RainCB>(dc, 0, cb, /*VS*/ false, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ false, /*CS*/ true);


    dc->CSSetShader(rain_cs.Get(), nullptr, 0);

    ID3D11UnorderedAccessView* uavs[] = { particleUAV.Get(), hitUAV.Get() };

    UINT initialCounts[2] = { (UINT)-1, 0 };
    dc->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    dc->CSSetShaderResources(0, 1, &weatherSRV);

    ID3D11SamplerState* samplers[8] = {};
    samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
    samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
    samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
    samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();
    samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();
    samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
    samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
    samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

    dc->CSSetSamplers(0, _countof(samplers), samplers);

    dc->CSSetShaderResources(0, 1, &weatherSRV);

    const UINT groups = (kMaxParticles + (kRainCsThreadGroupSize - 1)) / kRainCsThreadGroupSize;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[2] = { nullptr, nullptr };
    dc->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);

    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    dc->CSSetShaderResources(0, 1, nullSRV);

    dc->CSSetShader(nullptr, nullptr, 0);
}

void RainSystem::render(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* sceneDepthSRV, ID3D11RenderTargetView* rtvOverride)
{
    ID3D11RenderTargetView* nullRTV[1] = { nullptr };
    dc->OMSetRenderTargets(1, nullRTV, nullptr);

    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    dc->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    dc->PSSetShaderResources(1, 1, nullSRV);


    ID3D11RenderTargetView* targetRTV = rtvOverride;
    if (!targetRTV)
    {
        targetRTV = DeviceManager::instance()->getRenderTargetView();
    }
    dc->OMSetRenderTargets(1, &targetRTV, nullptr);

    // IA
    UINT stride = sizeof(QuadVtx);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = { quadVB.Get() };
    dc->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    dc->IASetInputLayout(input_layout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    float blendFactor[4] = { 0,0,0,0 };
    dc->OMSetBlendState(GraphicsManager::instance()->getBlendStates(BLEND_STATE::ALPHABLENDING).Get(), blendFactor, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_OFF).Get(), 0);

    dc->VSSetShader(rain_vs.Get(), nullptr, 0);
    ID3D11ShaderResourceView* vsSrvs[] = { particleSRV.Get() };
    dc->VSSetShaderResources(0, 1, vsSrvs);

    
    cbRain->UploadData<RainCB>(dc, 0, cb, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
 

    dc->PSSetShader(rain_ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* psSrvs[] = { sceneDepthSRV };
    dc->PSSetShaderResources(1, 1, psSrvs);
    ID3D11SamplerState* samplers[8] = {};
    samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
    samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
    samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
    samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();
    samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();
    samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
    samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
    samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();


    dc->PSSetSamplers(0, _countof(samplers), samplers);;

    dc->DrawInstanced(6, kMaxParticles, 0, 0);

    dc->VSSetShaderResources(0, 1, nullSRV);
    dc->PSSetShaderResources(1, 1, nullSRV);
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);

    dc->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(nullptr, 0);
}

UINT RainSystem::ReadbackHits(ID3D11DeviceContext* dc,
    std::vector<DirectX::XMFLOAT4>& outHits,
    UINT maxRead)
{
    outHits.clear();
    if (!dc || !hitUAV || !hitBuffer) return 0;


    dc->CopyStructureCount(hitCountBuffer.Get(), 0, hitUAV.Get());
    dc->CopyResource(hitCountStaging.Get(), hitCountBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(dc->Map(hitCountStaging.Get(), 0, D3D11_MAP_READ, 0, &ms)))
        return 0;

    UINT hitCount = *(UINT*)ms.pData;
    dc->Unmap(hitCountStaging.Get(), 0);

    if (hitCount == 0) return 0;

    hitCount = (std::min)(hitCount, (UINT)kMaxHits);
    hitCount = (std::min)(hitCount, maxRead);

    // ƒqƒbƒg”z—ñ‚ð“Ç‚Ý–ß‚µ
    dc->CopyResource(hitStaging.Get(), hitBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mh{};
    if (FAILED(dc->Map(hitStaging.Get(), 0, D3D11_MAP_READ, 0, &mh)))
        return 0;

    outHits.resize(hitCount);
    std::memcpy(outHits.data(), mh.pData, sizeof(DirectX::XMFLOAT4) * hitCount);

    dc->Unmap(hitStaging.Get(), 0);

    return hitCount;
}


