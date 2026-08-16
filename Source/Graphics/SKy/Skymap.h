#include <d3d11.h>
#include <wrl.h>
#include <directxmath.h>
#include "Graphics/ShaderConstants.h"
#include "Graphics/GPUConstantBuffer.h"

class SkyMap
{
public:
    SkyMap() = default;
	 ~SkyMap() = default;
	

    void initialize(ID3D11Device* device);

	void update(ID3D11DeviceContext* dc, const DirectX::XMFLOAT3& camera_position);

	void render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& view_projection);

	bool debugGui(DirectX::XMFLOAT4* outLightDirection = nullptr);

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_view;

	ID3D11ShaderResourceView* getSkyCubemapSRV() { return sky_cubemap_srv.Get(); }


	enum SkyType { TextureCube, Texture2D, Procedural };
	SkyType skyType = SkyType::Procedural;


	AtmosphereConstants atmosphere_constants_data;

	// スカイマップ描画用の定数バッファ構造体
	struct SkyMapConstants
	{
		DirectX::XMFLOAT4X4 inverse_view_projection;
		int sky_type;
		DirectX::XMFLOAT3 padding{};
	};
	SkyMapConstants sky_map_constants_data;

	const AtmosphereConstants& getAtmosphereConstants() const { return atmosphere_constants_data; }
private:
	// 描画用シェーダー
	Microsoft::WRL::ComPtr<ID3D11VertexShader> sky_map_vs;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> sky_map_ps;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> sky_box_ps;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>  sky_depth_ps;


	Microsoft::WRL::ComPtr<ID3D11ComputeShader> procedural_sky_cs;

	// 生成された空の環境マップ（キューブマップ）リソース
	Microsoft::WRL::ComPtr<ID3D11Texture2D>           sky_cubemap_texture;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> sky_cubemap_uav; // CSでの書き込み用
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  sky_cubemap_srv; // 描画時・IBL時の読み込み用
	// 256 is sufficient for the blurred atmospheric background and lets the
	// visible sky update frequently enough for a smooth day/night cycle.
	UINT cubemap_resolution = 256;// キューブマップの解像度
	bool transmittanceDirty = true;

	// 定数バッファ
	
	std::unique_ptr<GPUConstantBuffer> atmosphere_constant_buffer;
	std::unique_ptr<GPUConstantBuffer> sky_map_constant_buffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
	// サンプラーステート
	Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_states[8];

public:
	ID3D11Buffer* getConstantBuffer() const { return constant_buffer.Get(); }

	// Transmittance LUT（透過率ルックアップテーブル）関連リソース
	// 大気圏を通る光の減衰を事前計算して格納しておくテクスチャ
	Microsoft::WRL::ComPtr<ID3D11Texture2D> transmittance_texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> transmittance_srv;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> transmittance_uav;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> transmittance_cs;

	// 透過率LUTの解像度
	UINT transmittance_width = 512;
	UINT transmittance_height = 128;

	void updateTransmittance(ID3D11DeviceContext* dc);
	ID3D11ShaderResourceView* getTransmittanceSRV() const { return transmittance_srv.Get(); }

};
