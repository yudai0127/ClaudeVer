#include "Gbuffer.h"
#include "Graphics/Shader/Shader.h"
#include "misc.h"



void Gbuffer::initialize(ID3D11Device* device, uint32_t width, uint32_t height)
{
    D3D11_INPUT_ELEMENT_DESC input_layout_desc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Gbuffer_VS.cso", vertex_shader.GetAddressOf(), input_layout.GetAddressOf(), input_layout_desc, ARRAYSIZE(input_layout_desc));
    ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Gbuffer_PS.cso", pixel_shader.GetAddressOf());

    HRESULT hr{ S_OK };

    DXGI_FORMAT formats[GBUFFER_RT_COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,         // rt0: Albedo(RGB) + Metallic(A)
        DXGI_FORMAT_R16G16B16A16_FLOAT,     // rt1: World Normal(RGB) + Roughness(A)
        DXGI_FORMAT_R16G16B16A16_FLOAT,     // rt2: Emissive(RGB) + Unused(A)
        DXGI_FORMAT_R32G32B32A32_FLOAT      // rt3: World Position(RGB) + Unused(A)
    };

    render_target_textures.resize(GBUFFER_RT_COUNT);
    render_target_views.resize(GBUFFER_RT_COUNT);
    shader_resource_views.resize(GBUFFER_RT_COUNT);

    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    for (size_t i = 0; i < GBUFFER_RT_COUNT; ++i)
    {
        tex_desc.Format = formats[i];
        hr = device->CreateTexture2D(&tex_desc, nullptr, render_target_textures[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        hr = device->CreateRenderTargetView(render_target_textures[i].Get(), nullptr, render_target_views[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        hr = device->CreateShaderResourceView(render_target_textures[i].Get(), nullptr, shader_resource_views[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


#ifdef _DEBUG
        if (render_target_textures[i]) {
            char resourceName[256];
            sprintf_s(resourceName, "GBuffer_RT%zu", i);
            render_target_textures[i]->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen(resourceName)), resourceName);
        }
#endif
    }

    // 深度バッファの作成
    D3D11_TEXTURE2D_DESC depth_tex_desc{};
    depth_tex_desc.Width = width;
    depth_tex_desc.Height = height;
    depth_tex_desc.MipLevels = 1;
    depth_tex_desc.ArraySize = 1;
    depth_tex_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depth_tex_desc.SampleDesc.Count = 1;
    depth_tex_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&depth_tex_desc, nullptr, depth_stencil_texture.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(depth_stencil_texture.Get(), &dsv_desc, depth_stencil_view.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    D3D11_SHADER_RESOURCE_VIEW_DESC ds_srv_desc{};
    ds_srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    ds_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ds_srv_desc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(depth_stencil_texture.Get(), &ds_srv_desc, depth_shader_resource_view.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_ro_desc = dsv_desc;


    dsv_ro_desc.Flags = D3D11_DSV_READ_ONLY_DEPTH;

    hr = device->CreateDepthStencilView(depth_stencil_texture.Get(), &dsv_ro_desc,
        depth_stencil_view_readonly.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));



#ifdef _DEBUG
    if (depth_stencil_texture) {
        depth_stencil_texture->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("GBuffer_Depth"), "GBuffer_Depth");
    }
#endif

    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
}

void Gbuffer::activate(ID3D11DeviceContext* dc)
{
    cached_viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    dc->RSGetViewports(&cached_viewport_count, cached_viewports);
    dc->OMGetRenderTargets(1, cached_render_target_view.ReleaseAndGetAddressOf(), cached_depth_stencil_view.ReleaseAndGetAddressOf());

    std::vector<ID3D11RenderTargetView*> rtvs(GBUFFER_RT_COUNT);
    for (size_t i = 0; i < GBUFFER_RT_COUNT; ++i)
    {
        rtvs[i] = render_target_views[i].Get();
    }
    dc->OMSetRenderTargets(static_cast<UINT>(GBUFFER_RT_COUNT), rtvs.data(), depth_stencil_view.Get());
    dc->RSSetViewports(1, &viewport);

    dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
    dc->PSSetShader(pixel_shader.Get(), nullptr, 0);
    dc->IASetInputLayout(input_layout.Get());
}

void Gbuffer::deactivate(ID3D11DeviceContext* dc)
{
    dc->RSSetViewports(cached_viewport_count, cached_viewports);
    dc->OMSetRenderTargets(1, cached_render_target_view.GetAddressOf(), cached_depth_stencil_view.Get());
}

void Gbuffer::clear(ID3D11DeviceContext* dc)
{
    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < GBUFFER_RT_COUNT; ++i)
    {
        dc->ClearRenderTargetView(render_target_views[i].Get(), clear_color);
    }
    dc->ClearDepthStencilView(depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

ID3D11ShaderResourceView* Gbuffer::get_srv(size_t index) const
{
    return (index < shader_resource_views.size()) ? shader_resource_views[index].Get() : nullptr;
}

ID3D11RenderTargetView* Gbuffer::get_rtv(size_t index) const
{
    return (index < render_target_views.size()) ? render_target_views[index].Get() : nullptr;
}

ID3D11Texture2D* Gbuffer::get_texture(size_t index) const
{
    return (index < render_target_textures.size()) ? render_target_textures[index].Get() : nullptr;
}

