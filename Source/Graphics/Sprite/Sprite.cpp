#include "Sprite.h"
#include "misc.h"
#include "Graphics/Texture/Texture.h"
#include "Graphics/Shader/Shader.h"


/// コンストラクタ
Sprite::Sprite(ID3D11Device* device, const wchar_t* filename)
{
    HRESULT hr{ S_OK };

    vertex vertices[] =
    {
        { {-0.5,+0.5, 0}, {1,1,1,1}, {0, 0} },
        { {+0.5,+0.5, 0}, {1,0,0,1}, {1, 0} },
        { {-0.5,-0.5, 0}, {0,1,0,1}, {0, 1} },
        { {+0.5,-0.5, 0}, {0,0,1,1}, {1, 1} },
    };

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(vertices);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;
    buffer_desc.StructureByteStride = 0;
    D3D11_SUBRESOURCE_DATA subresource_data{};
    subresource_data.pSysMem = vertices;
    subresource_data.SysMemPitch = 0;
    subresource_data.SysMemSlicePitch = 0;
    hr = device->CreateBuffer(&buffer_desc, &subresource_data, vertex_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

    D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
            D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
            D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Sprite_VS.cso", vertex_shader.GetAddressOf(), input_layout.GetAddressOf(), input_element_desc, ARRAYSIZE(input_element_desc));
    ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Sprite_PS.cso", pixel_shader.GetAddressOf());

    if (filename != nullptr)
    {
        hr = TextureManager::instance()->loadTextureFromFile(device, filename, shader_resource_view.GetAddressOf(), &texture2d_desc);
        if (FAILED(hr))
        {
            // エラーメッセージを表示
            std::wstring errorMessage = L"Failed to load texture from file: " + std::wstring(filename) + L" (HRESULT: " + std::to_wstring(hr) + L")\n";
            OutputDebugStringW(errorMessage.c_str());
        }
    }
    else
    {
        // ダミーテクスチャの作成
        const int width = 8;
        const int height = 8;
        UINT pixels[width * height];
        ::memset(pixels, 0xFF, sizeof(pixels));

        D3D11_TEXTURE2D_DESC desc = { 0 };
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels;
        data.SysMemPitch = width * sizeof(UINT); // 修正: 4バイト×幅

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        hr = device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        hr = device->CreateShaderResourceView(texture.Get(), nullptr, shader_resource_view.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

        texture2d_desc = desc;
    }
}


Sprite::~Sprite()
{
}

// 描画処理
void Sprite::render(ID3D11DeviceContext* dc,
	float dx, float dy, float dw, float dh,
	float r, float g, float b, float a,
	float angle,
	float sx, float sy, float sw, float sh)
{
	D3D11_VIEWPORT viewport{};
	UINT num_viewports{ 1 };
	dc->RSGetViewports(&num_viewports, &viewport);

	// 左上の頂点
	float x0(dx);
	float y0(dy);
	// 右上の頂点
	float x1(dx + dw);
	float y1(dy);
	// 左下の頂点
	float x2(dx);
	float y2(dy + dh);
	// 右下の頂点
	float x3(dx + dw);
	float y3(dy + dh);

	float cx = dx + dw * 0.5f;
	float cy = dy + dh * 0.5f;
	rotate(x0, y0, cx, cy, angle);
	rotate(x1, y1, cx, cy, angle);
	rotate(x2, y2, cx, cy, angle);
	rotate(x3, y3, cx, cy, angle);

	x0 = 2.0f * x0 / viewport.Width - 1.0f;
	y0 = 1.0f - 2.0f * y0 / viewport.Height;
	x1 = 2.0f * x1 / viewport.Width - 1.0f;
	y1 = 1.0f - 2.0f * y1 / viewport.Height;
	x2 = 2.0f * x2 / viewport.Width - 1.0f;
	y2 = 1.0f - 2.0f * y2 / viewport.Height;
	x3 = 2.0f * x3 / viewport.Width - 1.0f;
	y3 = 1.0f - 2.0f * y3 / viewport.Height;

	HRESULT hr{ S_OK };

	D3D11_MAPPED_SUBRESOURCE mapped_subresource{};
	hr = dc->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	vertex* vertices{ reinterpret_cast<vertex*>(mapped_subresource.pData) };
	if (vertices != nullptr)
	{
		vertices[0].position = { x0, y0, 0 };
		vertices[1].position = { x1, y1, 0 };
		vertices[2].position = { x2, y2, 0 };
		vertices[3].position = { x3, y3, 0 };
		vertices[0].color = vertices[1].color =
			vertices[2].color = vertices[3].color = { r,g,b,a };

		vertices[0].texcoord = { sx / texture2d_desc.Width, sy / texture2d_desc.Height };
		vertices[1].texcoord = { (sx + sw) / texture2d_desc.Width, sy / texture2d_desc.Height };
		vertices[2].texcoord = { sx / texture2d_desc.Width, (sy + sh) / texture2d_desc.Height };
		vertices[3].texcoord = { (sx + sw) / texture2d_desc.Width, (sy + sh) / texture2d_desc.Height };
	}
	dc->Unmap(vertex_buffer.Get(), 0);

	UINT stride{ sizeof(vertex) };
	UINT offset{ 0 };
	dc->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->IASetInputLayout(input_layout.Get());
	dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(pixel_shader.Get(), nullptr, 0);
	dc->PSSetShaderResources(0, 1, shader_resource_view.GetAddressOf());
	dc->Draw(4, 0);
}
