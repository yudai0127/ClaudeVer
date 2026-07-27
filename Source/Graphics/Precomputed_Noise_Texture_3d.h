#include <d3d11.h>
#include <wrl.h>

#include <vector>

#include "misc.h"

// https://github.com/Auburn/FastNoiseLite
#include "../FastNoiseLite-master/Cpp/FastNoiseLite.h"


static HRESULT precompute_noise_texture_3d(_In_ ID3D11Device* device, _In_ UINT dimension, _Out_ ID3D11ShaderResourceView** shader_resource_view)
{
	HRESULT hr = S_OK;

	const UINT width = dimension;
	const UINT height = dimension;
	const UINT depth = dimension;
	const UINT bytes_per_pixel = 4;
	const UINT slice_size = width * height * bytes_per_pixel;

	FastNoiseLite noise;
	noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	noise.SetRotationType3D(FastNoiseLite::RotationType3D::RotationType3D_None);
	noise.SetSeed(1337);
	noise.SetFrequency(0.1f);

	noise.SetFractalType(FastNoiseLite::FractalType::FractalType_FBm);
	noise.SetFractalOctaves(5);
	noise.SetFractalLacunarity(2.00f);
	noise.SetFractalGain(0.50f);

	std::vector<float> noise_data(width * height * depth);
	size_t index = 0;
	for (UINT z = 0; z < depth; z++)
	{
		for (UINT y = 0; y < height; y++)
		{
			for (UINT x = 0; x < width; x++)
			{
				noise_data.at(index++) = noise.GetNoise(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
			}
		}
	}

	std::vector<D3D11_SUBRESOURCE_DATA> initial_data(depth);
	byte* p = reinterpret_cast<byte*>(noise_data.data());
	for (UINT z = 0; z < depth; z++)
	{
		initial_data.at(z).pSysMem = static_cast<const void*>(p);
		initial_data.at(z).SysMemPitch = width * bytes_per_pixel;
		initial_data.at(z).SysMemSlicePitch = width * height * bytes_per_pixel;
		p += slice_size;
	}
	D3D11_TEXTURE3D_DESC texture3d_desc = {};
	texture3d_desc.Width = width;
	texture3d_desc.Height = height;
	texture3d_desc.Depth = depth;
	texture3d_desc.MipLevels = 1;
	texture3d_desc.Format = DXGI_FORMAT_R32_FLOAT;
	texture3d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture3d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texture3d_desc.CPUAccessFlags = 0;
	texture3d_desc.MiscFlags = 0;

	Microsoft::WRL::ComPtr<ID3D11Texture3D> texture_3d;
	hr = device->CreateTexture3D(&texture3d_desc, initial_data.data(), texture_3d.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc{};
	shader_resource_view_desc.Format = texture3d_desc.Format;
	shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
	shader_resource_view_desc.Texture3D.MipLevels = 1;

	hr = device->CreateShaderResourceView(texture_3d.Get(), &shader_resource_view_desc, shader_resource_view);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));



	return hr;
}

