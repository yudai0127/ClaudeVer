#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <string>
#include <map>

class TextureManager
{
private:
	TextureManager() {}
	~TextureManager();

public:
	static TextureManager* instance()
	{
		static TextureManager inst;
		return &inst;
	}

	// ファイルからテクスチャを作成
	HRESULT loadTextureFromFile(
		ID3D11Device* device,
		const wchar_t* filename,
		ID3D11ShaderResourceView** shader_resource_view,
		D3D11_TEXTURE2D_DESC* texture2d_desc);

	// ダミーテクスチャの作成
	HRESULT makeDummyTexture(
		ID3D11Device* device,
		ID3D11ShaderResourceView** shader_resource_view,
		DWORD value,
		UINT dimension);

	HRESULT loadTextureFromMemory(ID3D11Device* device, const void* data, size_t size, ID3D11ShaderResourceView** shader_resource_view);

	// 作成した全てのテクスチャを開放
	void releaseAllTextures();

private:
	std::map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> resources;
};