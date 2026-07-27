#include "misc.h"
#include "DeviceManager.h"

HRESULT DeviceManager::resize(UINT width, UINT height)
{
    if (!swapchain || !device || !immediateContext) return E_FAIL;
    if (width == 0 || height == 0) return S_OK;

    ID3D11RenderTargetView* nullRTV = nullptr;
    immediateContext->OMSetRenderTargets(1, &nullRTV, nullptr);
    renderTargetView.Reset();
    depthStencilView.Reset();
    depthStencilBuffer.Reset();
    depthStencilSRV.Reset(); 

    HRESULT hr = swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    // [“x (Typeless + SRV/DSV)
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    depthDesc.CPUAccessFlags = 0;
    depthDesc.MiscFlags = 0;

    hr = device->CreateTexture2D(&depthDesc, nullptr, depthStencilBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    hr = device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, depthStencilView.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(depthStencilBuffer.Get(), &srvDesc, depthStencilSRV.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    if (FAILED(hr)) return hr;

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    immediateContext->RSSetViewports(1, &vp);

    screenWidth = (float)width;
    screenHeight = (float)height;
    return S_OK;
}

DeviceManager* DeviceManager::initialize(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    UINT screenWidth = rc.right - rc.left;
    UINT screenHeight = rc.bottom - rc.top;
    this->screenWidth = (float)screenWidth;
    this->screenHeight = (float)screenHeight;

    HRESULT hr = S_OK;

    {
        UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1,
        };
        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        swapchainDesc.BufferDesc.Width = screenWidth;
        swapchainDesc.BufferDesc.Height = screenHeight;
        swapchainDesc.BufferDesc.RefreshRate.Numerator = 60;
        swapchainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.BufferCount = 2;
        swapchainDesc.OutputWindow = hwnd;
        swapchainDesc.Windowed = TRUE;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapchainDesc.Flags = 0;

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &swapchainDesc,
            swapchain.GetAddressOf(),
            device.GetAddressOf(),
            &featureLevel,
            immediateContext.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
        hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    // [“x typeless + SRV
    {
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = screenWidth;
        depthDesc.Height = screenHeight;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        depthDesc.CPUAccessFlags = 0;
        depthDesc.MiscFlags = 0;
        hr = device->CreateTexture2D(&depthDesc, nullptr, depthStencilBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        hr = device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, depthStencilView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(depthStencilBuffer.Get(), &srvDesc, depthStencilSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
    }

    {
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = (float)screenWidth;
        viewport.Height = (float)screenHeight;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        immediateContext->RSSetViewports(1, &viewport);
    }
    return this;
}

void DeviceManager::ensureWindowed()
{
    if (swapchain)
    {
        swapchain->SetFullscreenState(FALSE, nullptr);
    }
}