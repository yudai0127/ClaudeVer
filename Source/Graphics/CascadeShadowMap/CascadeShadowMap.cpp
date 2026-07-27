#include "CascadeShadowMap.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "misc.h"
#include "Graphics/Buffer.h"
#include <algorithm>

 
CascadeShadowMap::CascadeShadowMap(ID3D11Device* device)
{
    initialize(device);
}




 
bool CascadeShadowMap::initialize(ID3D11Device* device)
{
    HRESULT hr = S_OK;

   

    // 定数バッファの作成  
    co = std::make_unique<GPUConstantBuffer>(device, sizeof(CascadeConstants));
    

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = SHADOWMAP_SIZE;
    textureDesc.Height = SHADOWMAP_SIZE;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;

    for (int i = 0; i < CASCADE_COUNT; ++i)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthBuffer;
        hr = device->CreateTexture2D(&textureDesc, nullptr, depthBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        hr = device->CreateDepthStencilView(
            depthBuffer.Get(),
            &dsvDesc,
            shadowMapDSVs[i].GetAddressOf()
        );
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(
            depthBuffer.Get(),
            &srvDesc,
            shadowMapSRVs[i].GetAddressOf()
        );
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }


    
    cascadeConstants.cascade_shadow_bias = 0.005f;
    cascadeConstants.cascade_shadow_attenuation = 0.5f;
    cascadeConstants.display_cascade_area = FALSE;
    cascadeConstants.cascade_shadow_pcf_radius = 1.5f;
    return true;
}


void CascadeShadowMap::CalculateCascades(
    ID3D11DeviceContext* dc,
    const DirectX::XMFLOAT4X4& cameraView,
    const DirectX::XMFLOAT4X4& cameraProjection,
    const DirectX::XMFLOAT4& lightDirection,
    float fovY,
    float aspectRatio,
    const DirectX::XMFLOAT3& cameraPosition)
{
    DirectX::XMVECTOR lightDirVec = DirectX::XMLoadFloat4(&lightDirection);
    lightDirVec = DirectX::XMVector3Normalize(lightDirVec);

    DirectX::XMMATRIX viewMatrix = DirectX::XMLoadFloat4x4(&cameraView);
    DirectX::XMMATRIX invViewMatrix = DirectX::XMMatrixInverse(nullptr, viewMatrix);

    DirectX::XMVECTOR cameraRight = DirectX::XMVector3Normalize(invViewMatrix.r[0]);
    DirectX::XMVECTOR cameraUp = DirectX::XMVector3Normalize(invViewMatrix.r[1]);
    DirectX::XMVECTOR cameraFront = DirectX::XMVector3Normalize(invViewMatrix.r[2]);

    // カメラ投影行列(XMMatrixPerspectiveFovLH)から near/far を復元
    float cameraNear = SPLIT_AREA_TABLE[0];
    float cameraFar = SPLIT_AREA_TABLE[CASCADE_COUNT];
    {
        const float m33 = cameraProjection._33;
        const float m43 = cameraProjection._43;
        if (fabsf(m33) > 1e-6f && fabsf(m33 - 1.0f) > 1e-6f)
        {
            const float nearZ = -m43 / m33;
            const float farZ = -m43 / (m33 - 1.0f);
            if (nearZ > 0.0f && farZ > nearZ)
            {
                cameraNear = nearZ;
                cameraFar = farZ;
            }
        }
    }

    const float tableNear = SPLIT_AREA_TABLE[0];
    const float tableFar = SPLIT_AREA_TABLE[CASCADE_COUNT];
    const float tableRange = (tableFar - tableNear);

    // ライト方向とUpベクトルがほぼ平行な場合の退避
    DirectX::XMVECTOR defaultUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMVECTOR altUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    float dotWithUp = fabsf(DirectX::XMVectorGetX(DirectX::XMVector3Dot(lightDirVec, defaultUp)));
    DirectX::XMVECTOR upVec = (dotWithUp > 0.99f) ? altUp : defaultUp;

    for (int i = 0; i < CASCADE_COUNT; ++i)
    {
        // 既存の分割比率を維持しつつ、near/far をカメラに合わせてスケール
        float tNear = (SPLIT_AREA_TABLE[i] - tableNear) / tableRange;
        float tFar = (SPLIT_AREA_TABLE[i + 1] - tableNear) / tableRange;

        float nearDepth = cameraNear + (cameraFar - cameraNear) * tNear;
        float farDepth = cameraNear + (cameraFar - cameraNear) * tFar;

        DirectX::XMVECTOR vertices[8];
        calculateFrustumVertices(
            cameraPosition,
            DirectX::XMFLOAT3(DirectX::XMVectorGetX(cameraFront), DirectX::XMVectorGetY(cameraFront), DirectX::XMVectorGetZ(cameraFront)),
            DirectX::XMFLOAT3(DirectX::XMVectorGetX(cameraUp), DirectX::XMVectorGetY(cameraUp), DirectX::XMVectorGetZ(cameraUp)),
            DirectX::XMFLOAT3(DirectX::XMVectorGetX(cameraRight), DirectX::XMVectorGetY(cameraRight), DirectX::XMVectorGetZ(cameraRight)),
            nearDepth,
            farDepth,
            fovY,
            aspectRatio,
            vertices
        );

        DirectX::XMVECTOR frustumCenter = DirectX::XMVectorZero();
        for (int j = 0; j < 8; ++j)
            frustumCenter = DirectX::XMVectorAdd(frustumCenter, vertices[j]);
        frustumCenter = DirectX::XMVectorScale(frustumCenter, 1.0f / 8.0f);

        float sphereRadius = 0.0f;
        for (int j = 0; j < 8; ++j)
        {
            float dist = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                DirectX::XMVectorSubtract(vertices[j], frustumCenter)));
            if (dist > sphereRadius)
                sphereRadius = dist;
        }
        sphereRadius = ceilf(sphereRadius);

        const float lightDistanceLocal = sphereRadius * 2.0f + 50.0f;
        DirectX::XMVECTOR lightPos = DirectX::XMVectorAdd(
            frustumCenter,
            DirectX::XMVectorScale(lightDirVec, -lightDistanceLocal)
        );

        DirectX::XMMATRIX lightView = DirectX::XMMatrixLookAtLH(
            lightPos,
            frustumCenter,
            upVec
        );

        float zMin = FLT_MAX;
        float zMax = -FLT_MAX;
        for (int j = 0; j < 8; ++j)
        {
            DirectX::XMVECTOR lv = DirectX::XMVector3TransformCoord(vertices[j], lightView);
            float z = DirectX::XMVectorGetZ(lv);
            if (z < zMin) zMin = z;
            if (z > zMax) zMax = z;
        }

        float texelSize = (sphereRadius * 2.0f) / static_cast<float>(SHADOWMAP_SIZE);
        DirectX::XMVECTOR centerLV = DirectX::XMVector3TransformCoord(frustumCenter, lightView);
        float cx = DirectX::XMVectorGetX(centerLV);
        float cy = DirectX::XMVectorGetY(centerLV);
        cx = floorf(cx / texelSize) * texelSize;
        cy = floorf(cy / texelSize) * texelSize;

        float left = cx - sphereRadius;
        float right = cx + sphereRadius;
        float bottom = cy - sphereRadius;
        float top = cy + sphereRadius;

        const float zPadding = 150.0f;
        float zn = zMin - zPadding;
        float zf = zMax + zPadding;

        DirectX::XMMATRIX lightProj = DirectX::XMMatrixOrthographicOffCenterLH(
            left, right, bottom, top, zn, zf);

        DirectX::XMMATRIX lightViewProjection = lightView * lightProj;
        DirectX::XMStoreFloat4x4(&cascadeConstants.cascade_light_view_projection[i], lightViewProjection);
    }
}


  
void CascadeShadowMap::calculateFrustumVertices(
    const DirectX::XMFLOAT3& cameraPos,
    const DirectX::XMFLOAT3& cameraFront,
    const DirectX::XMFLOAT3& cameraUp,
    const DirectX::XMFLOAT3& cameraRight,
    float nearDepth,
    float farDepth,
    float fovY,
    float aspectRatio,
    DirectX::XMVECTOR* vertices)
{
   
    DirectX::XMVECTOR cameraPosVec = DirectX::XMLoadFloat3(&cameraPos);
    DirectX::XMVECTOR cameraFrontVec = DirectX::XMLoadFloat3(&cameraFront);
    DirectX::XMVECTOR cameraUpVec = DirectX::XMLoadFloat3(&cameraUp);
    DirectX::XMVECTOR cameraRightVec = DirectX::XMLoadFloat3(&cameraRight);

    // ニアプレーンでの視錐台の半分の幅と高さを計算
    float nearY = tanf(fovY * 0.5f) * nearDepth;    // ニアプレーンの半分の高さ
    float nearX = nearY * aspectRatio;               // ニアプレーンの半分の幅

    // ファープレーンでの視錐台の半分の幅と高さを計算
    float farY = tanf(fovY * 0.5f) * farDepth;       // ファープレーンの半分の高さ
    float farX = farY * aspectRatio;                 // ファープレーンの半分の幅

    // ニアプレーンの中心位置を計算
    DirectX::XMVECTOR nearPosition = DirectX::XMVectorAdd(cameraPosVec,
        DirectX::XMVectorScale(cameraFrontVec, nearDepth));

    // ファープレーンの中心位置を計算
    DirectX::XMVECTOR farPosition = DirectX::XMVectorAdd(cameraPosVec,
        DirectX::XMVectorScale(cameraFrontVec, farDepth));

    // 視錐台の8頂点を計算
    // ニアプレーンの右上
    vertices[0] = DirectX::XMVectorAdd(nearPosition,
        DirectX::XMVectorAdd(
            DirectX::XMVectorScale(cameraUpVec, nearY),      
            DirectX::XMVectorScale(cameraRightVec, nearX))); 

    // ニアプレーンの左上
    vertices[1] = DirectX::XMVectorAdd(nearPosition,
        DirectX::XMVectorAdd(
            DirectX::XMVectorScale(cameraUpVec, nearY),      
            DirectX::XMVectorScale(cameraRightVec, -nearX)));

    // ニアプレーンの右下
    vertices[2] = DirectX::XMVectorAdd(nearPosition,
        DirectX::XMVectorAdd(
            DirectX::XMVectorScale(cameraUpVec, -nearY),     
            DirectX::XMVectorScale(cameraRightVec, nearX))); 

    // ニアプレーンの左下
    vertices[3] = DirectX::XMVectorAdd(nearPosition,
        DirectX::XMVectorAdd(
            DirectX::XMVectorScale(cameraUpVec, -nearY),      
            DirectX::XMVectorScale(cameraRightVec, -nearX))); 

    // ファープレーンの右上
    vertices[4] = DirectX::XMVectorAdd(farPosition,
        DirectX::XMVectorAdd(DirectX::XMVectorScale(cameraUpVec, farY),
            DirectX::XMVectorScale(cameraRightVec, farX)));

    // ファープレーンの左上
    vertices[5] = DirectX::XMVectorAdd(farPosition,
        DirectX::XMVectorAdd(DirectX::XMVectorScale(cameraUpVec, farY),
            DirectX::XMVectorScale(cameraRightVec, -farX)));

    // ファープレーンの右下
    vertices[6] = DirectX::XMVectorAdd(farPosition,
        DirectX::XMVectorAdd(DirectX::XMVectorScale(cameraUpVec, -farY),
            DirectX::XMVectorScale(cameraRightVec, farX)));

    // ファープレーンの左下
    vertices[7] = DirectX::XMVectorAdd(farPosition,
        DirectX::XMVectorAdd(DirectX::XMVectorScale(cameraUpVec, -farY),
            DirectX::XMVectorScale(cameraRightVec, -farX)));
}


  
DirectX::XMMATRIX CascadeShadowMap::calculateLightViewProjection(
    const DirectX::XMVECTOR* frustumVertices,
    const DirectX::XMMATRIX& lightView)
{
    // ライトビュー空間へ
    DirectX::XMVECTOR lv[8];
    for (int i = 0; i < 8; ++i)
    {
        lv[i] = DirectX::XMVector3TransformCoord(frustumVertices[i], lightView);
    }

    //AABB（x,y,z の最小最大）
    DirectX::XMVECTOR vmin = lv[0];
    DirectX::XMVECTOR vmax = lv[0];
    for (int i = 1; i < 8; ++i)
    {
        vmin = DirectX::XMVectorMin(vmin, lv[i]);
        vmax = DirectX::XMVectorMax(vmax, lv[i]);
    }

    DirectX::XMFLOAT3 minL, maxL;
    DirectX::XMStoreFloat3(&minL, vmin);
    DirectX::XMStoreFloat3(&maxL, vmax);

    
    const float xyPadding = 2.0f;   
    const float zPadding = 50.0f;

    float left = minL.x - xyPadding;
    float right = maxL.x + xyPadding;
    float bottom = minL.y - xyPadding;
    float top = maxL.y + xyPadding;
    float zn = minL.z - zPadding;
    float zf = maxL.z + zPadding;


    DirectX::XMMATRIX lightProj = DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, zn, zf);

   
    return lightView * lightProj;
}


 
void CascadeShadowMap::beginShadowRender(ID3D11DeviceContext* dc, int cascadeIndex)
{
    if (cascadeIndex < 0 || cascadeIndex >= CASCADE_COUNT) return;

    
    dc->ClearDepthStencilView(
        shadowMapDSVs[cascadeIndex].Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
        1.0f, 0);

    dc->OMSetRenderTargets(0, nullptr, shadowMapDSVs[cascadeIndex].Get());

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(SHADOWMAP_SIZE);
    viewport.Height = static_cast<float>(SHADOWMAP_SIZE);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &viewport);
}

void CascadeShadowMap::endShadowRender(ID3D11DeviceContext* dc)
{
    ID3D11RenderTargetView* nullRTV = nullptr;
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);
}

//シャドウマップをシェーダーリソースとして設定
void CascadeShadowMap::setShaderResources(ID3D11DeviceContext* dc, UINT startSlot)
{
    for (int i = 0; i < CASCADE_COUNT; ++i)
    {
        dc->PSSetShaderResources(startSlot + i, 1, shadowMapSRVs[i].GetAddressOf());
    }
}


 //カスケードシャドウマップ用定数バッファの設定
void CascadeShadowMap::setConstantBuffer(ID3D11DeviceContext* dc, UINT slot)
{
     co->UploadData<CascadeConstants>(dc, slot, cascadeConstants, /*VS*/ true, /*HS*/ false, /*DS*/ false, /*GS*/ false, /*PS*/ true, /*CS*/ false);
}




 //全カスケードシャドウマップのクリア
void CascadeShadowMap::clearShadowMaps(ID3D11DeviceContext* dc)
{
    for (int i = 0; i < CASCADE_COUNT; ++i)
    {
        dc->ClearDepthStencilView(shadowMapDSVs[i].Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}


 
ID3D11ShaderResourceView* CascadeShadowMap::getShadowMapSRV(int index) const
{
    if (index < 0 || index >= CASCADE_COUNT) return nullptr;
    return shadowMapSRVs[index].Get();
}





void CascadeShadowMap::UnbindShaderResources(ID3D11DeviceContext* dc, UINT startSlot)
{
    // NULLのシェーダーリソースビューを4つ準備
    ID3D11ShaderResourceView* nullSRVs[CASCADE_COUNT] = { nullptr };

    // 4つのスロットに一括でNULLを設定してアンバインド
    dc->PSSetShaderResources(startSlot, CASCADE_COUNT, nullSRVs);
}