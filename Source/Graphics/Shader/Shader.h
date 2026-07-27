#pragma once

#include <d3d11.h>

class ShaderManager
{
private:
	ShaderManager() {}
	~ShaderManager() {}
public:
	static ShaderManager* instance()
	{
		static ShaderManager inst;
		return &inst;
	}

	HRESULT CreateVsFromCso(ID3D11Device* device, const char* cso_name, ID3D11VertexShader** vertex_shader, ID3D11InputLayout** input_layout, D3D11_INPUT_ELEMENT_DESC* input_element_desc, UINT num_elements);
	HRESULT CreatePsFromCso(ID3D11Device* device, const char* cso_name, ID3D11PixelShader** pixel_shader);
	HRESULT CreateGsFromCso(ID3D11Device* device, const char* cso_name, ID3D11GeometryShader** geometry_shader);
	HRESULT CreateCsFromCso(ID3D11Device* device, const char* cso_name, ID3D11ComputeShader** compute_shader);
};