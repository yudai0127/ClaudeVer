#include "shader.h"
#include "misc.h"
#include <sstream>

HRESULT ShaderManager::CreateVsFromCso(ID3D11Device* device, const char* cso_name, ID3D11VertexShader** vertex_shader, ID3D11InputLayout** input_layout, D3D11_INPUT_ELEMENT_DESC* input_element_desc, UINT num_elements)
{
	FILE* fp{};
	fopen_s(&fp, cso_name, "rb");
	_ASSERT_EXPR_A(fp, "CSO File not found");

	// ファイルのサイズを調べて
	fseek(fp, 0, SEEK_END);
	long cso_sz{ ftell(fp) };
	fseek(fp, 0, SEEK_SET);

	// ファイル内の文字を cso_data に文字列として取り出し、ファイルを閉じる
	std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
	fread(cso_data.get(), cso_sz, 1, fp);
	fclose(fp);

	HRESULT hr{ S_OK };
	hr = device->CreateVertexShader(cso_data.get(), cso_sz, nullptr, vertex_shader);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	if (input_layout)
	{
		// 入力レイアウトオブジェクトを生成
		hr = device->CreateInputLayout(
			input_element_desc,             // 入力レイアウトの設定を行うための構造体 
			num_elements,					// 構造体の配列の数
			cso_data.get(),                 // コンパイルされた頂点シェーダー文字列
			cso_sz,                         // コンパイルされた頂点シェーダーのサイズ
			input_layout                   // 成功時の入力レイアウトオブジェクト
		);
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}

	return hr;
}

HRESULT ShaderManager::CreatePsFromCso(ID3D11Device* device, const char* cso_name, ID3D11PixelShader** pixel_shader)
{
	// ファイルを開く
	FILE* fp{};
	fopen_s(&fp, cso_name, "rb");
	_ASSERT_EXPR_A(fp, "CSO File not found");

	// ファイルのサイズを調べて
	fseek(fp, 0, SEEK_END);
	long cso_sz{ ftell(fp) };
	fseek(fp, 0, SEEK_SET);

	// ファイル内の文字を cso_data に文字列として取り出し、ファイルを閉じる
	std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
	fread(cso_data.get(), cso_sz, 1, fp);
	fclose(fp);

	HRESULT hr{ S_OK };
	hr = device->CreatePixelShader(cso_data.get(), cso_sz, nullptr, pixel_shader);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	return hr;
}

HRESULT ShaderManager::CreateGsFromCso(ID3D11Device* device, const char* cso_name, ID3D11GeometryShader** geometry_shader)
{
	FILE* fp{ nullptr };
	fopen_s(&fp, cso_name, "rb");
	_ASSERT_EXPR_A(fp, "CSO File not found");

	fseek(fp, 0, SEEK_END);
	long cso_sz{ ftell(fp) };
	fseek(fp, 0, SEEK_SET);

	std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
	fread(cso_data.get(), cso_sz, 1, fp);
	fclose(fp);

	HRESULT hr{ S_OK };
	hr = device->CreateGeometryShader(cso_data.get(), cso_sz, nullptr, geometry_shader);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	return hr;
}

HRESULT ShaderManager::CreateCsFromCso(ID3D11Device* device, const char* cso_name, ID3D11ComputeShader** compute_shader)
{
	// ファイルを開く
	FILE* fp{};
	fopen_s(&fp, cso_name, "rb");
	_ASSERT_EXPR_A(fp, "CSO File not found");

	// ファイルのサイズを調べて
	fseek(fp, 0, SEEK_END);
	long cso_sz{ ftell(fp) };
	fseek(fp, 0, SEEK_SET);

	// ファイル内の文字を cso_data に文字列として取り出し、ファイルを閉じる
	std::unique_ptr<unsigned char[]> cso_data{ std::make_unique<unsigned char[]>(cso_sz) };
	fread(cso_data.get(), cso_sz, 1, fp);
	fclose(fp);

	HRESULT hr{ S_OK };
	hr = device->CreateComputeShader(cso_data.get(), cso_sz, nullptr, compute_shader);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	return hr;
}