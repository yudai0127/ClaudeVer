#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

class Gbuffer
{
public:
    Gbuffer() = default;
    ~Gbuffer() = default;

    void initialize(ID3D11Device* device, uint32_t width, uint32_t height);

    void activate(ID3D11DeviceContext* dc);
    void deactivate(ID3D11DeviceContext* dc);
    void clear(ID3D11DeviceContext* dc);

    ID3D11ShaderResourceView* get_srv(size_t index) const;
    ID3D11RenderTargetView* get_rtv(size_t index) const;
    ID3D11DepthStencilView* get_dsv() const { return depth_stencil_view.Get(); }
    ID3D11ShaderResourceView* get_depth_srv() const { return depth_shader_resource_view.Get(); }
    ID3D11DepthStencilView* get_dsv_readonly() const { return depth_stencil_view_readonly.Get(); }
    ID3D11Texture2D* get_texture(size_t index) const;

private:
    // G-Bufferを構成するレンダーターゲットの数
    static constexpr size_t GBUFFER_RT_COUNT = 4;

    // シェーダー関連（G-Bufferへジオメトリ情報を書き込むパス用）
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;

    // マルチレンダーターゲット（MRT）用リソース
    // rt0: Albedo(RGB) + Metallic(A)
    // rt1: World Normal(RGB) + Roughness(A)
    // rt2: Emissive(RGB) + Unused(A)
    // rt3: World Position(RGB) + Unused(A)
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> render_target_textures;
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> render_target_views;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> shader_resource_views;

    // 深度バッファ関連リソース
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_stencil_texture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;                 // 書き込みありの標準DSV
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_shader_resource_view;       // テクスチャとしてサンプリングするためのSRV
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view_readonly;        // 読み込み専用DSV
   
    D3D11_VIEWPORT viewport{};

    UINT cached_viewport_count{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
    D3D11_VIEWPORT cached_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cached_render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> cached_depth_stencil_view;
};