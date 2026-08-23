#include "Skymap.h"
#include "Graphics/Buffer.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "misc.h"

#include <cstdio>

#include "imgui.h" 
#include <DirectXMath.h>

void SkyMap::initialize(ID3D11Device* device)
{
	HRESULT hr;

	// シェーダーの読み込み
	ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\Skymap_VS.cso", sky_map_vs.GetAddressOf(), NULL, NULL, 0);
	ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\ProceduralSky_CS.cso", procedural_sky_cs.GetAddressOf());
	ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\Skymap_PS.cso", sky_map_ps.GetAddressOf());
	ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\Transmittance_CS.cso", transmittance_cs.GetAddressOf());

	
	atmosphere_constant_buffer = std::make_unique<GPUConstantBuffer>(device, sizeof(AtmosphereConstants));
	sky_map_constant_buffer = std::make_unique<GPUConstantBuffer>(device, sizeof(SkyMapConstants));


	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = cubemap_resolution;
	texDesc.Height = cubemap_resolution;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 6; // キューブマップの面数
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	hr = device->CreateTexture2D(&texDesc, nullptr, sky_cubemap_texture.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.MipSlice = 0;
	uavDesc.Texture2DArray.FirstArraySlice = 0;
	uavDesc.Texture2DArray.ArraySize = 6;
	hr = device->CreateUnorderedAccessView(sky_cubemap_texture.Get(), &uavDesc, sky_cubemap_uav.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = 1;
	hr = device->CreateShaderResourceView(sky_cubemap_texture.Get(), &srvDesc, sky_cubemap_srv.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	
	

	
	D3D11_TEXTURE2D_DESC lutDesc = {};
	lutDesc.Width = transmittance_width;
	lutDesc.Height = transmittance_height;
	lutDesc.MipLevels = 1;
	lutDesc.ArraySize = 1;
	lutDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	lutDesc.SampleDesc.Count = 1;
	lutDesc.Usage = D3D11_USAGE_DEFAULT;
	lutDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	hr = device->CreateTexture2D(&lutDesc, nullptr, transmittance_texture.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	D3D11_UNORDERED_ACCESS_VIEW_DESC transUavDesc = {};
	transUavDesc.Format = lutDesc.Format;
	transUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	transUavDesc.Texture2D.MipSlice = 0;
	hr = device->CreateUnorderedAccessView(transmittance_texture.Get(), &transUavDesc, transmittance_uav.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC transSrvDesc = {};
	transSrvDesc.Format = lutDesc.Format;
	transSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	transSrvDesc.Texture2D.MostDetailedMip = 0;
	transSrvDesc.Texture2D.MipLevels = 1;
	hr = device->CreateShaderResourceView(transmittance_texture.Get(), &transSrvDesc, transmittance_srv.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

	// 定数バッファの初期値設定
	atmosphere_constants_data = {};
	const float DEG2RAD = 3.14159265358979323846f / 180.0f;
	float sunElevationDeg = 80.0f;
	float sunAzimuthDeg = 0.0f;
	float elev = sunElevationDeg * DEG2RAD;
	float azim = sunAzimuthDeg * DEG2RAD;
	DirectX::XMFLOAT3 sunDir;
	sunDir.x = cosf(azim) * cosf(elev);
	sunDir.y = sinf(elev);
	sunDir.z = sinf(azim) * cosf(elev);
	DirectX::XMStoreFloat3(&atmosphere_constants_data.sunDirection, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&sunDir)));
	atmosphere_constants_data.sunIntensity = 10.0f;
	atmosphere_constants_data.planetRadius = 6371.0f;
	atmosphere_constants_data.atmosphereRadius = 6471.0f;
	atmosphere_constants_data.rayleighScaleHeight = 8.0f;
	atmosphere_constants_data.rayleighScatteringCoefficient = { 0.005802f * 0.9f, 0.013558f * 0.95f, 0.033100f * 1.8f };
	atmosphere_constants_data.mieScaleHeight = 1.2f;
	atmosphere_constants_data.mieScatteringCoefficient = 0.0003f;
	atmosphere_constants_data.mieEccentricity = 0.99f;
	atmosphere_constants_data.nightIntensity = 0.35f;
}

void SkyMap::update(ID3D11DeviceContext* dc, const DirectX::XMFLOAT3& camera_position)
{
	DirectX::XMVECTOR camMeters = DirectX::XMLoadFloat3(&camera_position);
	DirectX::XMVECTOR camKm = DirectX::XMVectorScale(camMeters, 0.001f); // m -> km
	DirectX::XMVECTOR camPlanetCenteredKm = DirectX::XMVectorAdd(
		camKm,
		DirectX::XMVectorSet(0.0f, atmosphere_constants_data.planetRadius, 0.0f, 0.0f));

	// 惑星内部に入ると散乱積分がほぼゼロになり黒化するため、地表より少し上に固定
	const float minAltitudeKm = 0.001f; // 1m
	const float minRadiusKm = atmosphere_constants_data.planetRadius + minAltitudeKm;

	const float len = DirectX::XMVectorGetX(DirectX::XMVector3Length(camPlanetCenteredKm));
	if (len < minRadiusKm)
	{
		DirectX::XMVECTOR dir = (len > 1e-6f)
			? DirectX::XMVectorScale(camPlanetCenteredKm, 1.0f / len)
			: DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

		camPlanetCenteredKm = DirectX::XMVectorScale(dir, minRadiusKm);
	}

	DirectX::XMStoreFloat3(&this->atmosphere_constants_data.cameraPosition, camPlanetCenteredKm);

	
	if (transmittanceDirty)
	{
		updateTransmittance(dc);
		transmittanceDirty = false;
	}

	atmosphere_constant_buffer->UploadData<AtmosphereConstants>(dc, 3, atmosphere_constants_data, false, false, false, false, false, true);

	ID3D11ShaderResourceView* csSRVs[] = { transmittance_srv.Get() };
	dc->CSSetShaderResources(0, 1, csSRVs);

	dc->CSSetShader(procedural_sky_cs.Get(), 0, 0);
	dc->CSSetUnorderedAccessViews(0, 1, sky_cubemap_uav.GetAddressOf(), nullptr);

	ID3D11SamplerState* samplers[8] = {};
	samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
	samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
	samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
	samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();   // Slot 3
	samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();  // Slot 4
	samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
	samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
	samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

	dc->CSSetSamplers(0, _countof(samplers), samplers);

	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	dc->ClearUnorderedAccessViewFloat(sky_cubemap_uav.Get(), clearColor);

	const UINT THREAD_GROUP_SIZE = 8;
	UINT dispatchX = (cubemap_resolution + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
	UINT dispatchY = dispatchX;
	dc->Dispatch(dispatchX, dispatchY, 6);

	// アンバインド
	ID3D11UnorderedAccessView* pNullUAV = nullptr;
	dc->CSSetUnorderedAccessViews(0, 1, &pNullUAV, nullptr);
	dc->CSSetShader(NULL, 0, 0);

	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	dc->CSSetShaderResources(0, 1, nullSRV);

	ID3D11SamplerState* nullSamplers[8] = { nullptr };
	dc->PSSetSamplers(0, 8, nullSamplers);
}

void SkyMap::render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& view_projection)
{

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> original_rasterizer_state;
	dc->RSGetState(original_rasterizer_state.GetAddressOf());
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> original_depth_stencil_state;
	UINT original_stencil_ref;
	dc->OMGetDepthStencilState(original_depth_stencil_state.GetAddressOf(), &original_stencil_ref);
	Microsoft::WRL::ComPtr<ID3D11BlendState> original_blend_state;
	FLOAT original_blend_factor[4];
	UINT original_sample_mask;
	dc->OMGetBlendState(original_blend_state.GetAddressOf(), original_blend_factor, &original_sample_mask);


	dc->RSSetState(GraphicsManager::instance()->getRasterizerStates(RASTERIZER_STATE::SOLID_CULLNONE).Get());
	dc->OMSetDepthStencilState(GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_OFF).Get(), 0);
	dc->OMSetBlendState(GraphicsManager::instance()->getBlendStates(BLEND_STATE::NONE).Get(), nullptr, 0xFFFFFFFF);

	dc->IASetVertexBuffers(0, 0, NULL, NULL, NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->IASetInputLayout(NULL);


	dc->VSSetShader(sky_map_vs.Get(), 0, 0);
	// initialize()で生成しているプロシージャル空用PSを使用する。
	// 未初期化のsky_box_psを設定すると、雲OFF時は空が描画されずクリア色だけが残る。
	dc->PSSetShader(sky_map_ps.Get(), 0, 0);

	ID3D11SamplerState* samplers[8] = {};
	samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
	samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
	samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
	samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();   // Slot 3
	samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();  // Slot 4
	samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
	samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
	samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();



	dc->PSSetSamplers(0, _countof(samplers), samplers);

	DirectX::XMMATRIX VP = DirectX::XMLoadFloat4x4(&view_projection);
	DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, VP);
	DirectX::XMStoreFloat4x4(&sky_map_constants_data.inverse_view_projection, invVP);

	sky_map_constants_data.sky_type = static_cast<int>(skyType);

	atmosphere_constant_buffer->UploadData<AtmosphereConstants>(dc, 3, atmosphere_constants_data, false, false, false, false, true, false);
	sky_map_constant_buffer->UploadData<SkyMapConstants>(dc, 4, sky_map_constants_data, false, false, false, false, true, false);

	dc->PSSetShaderResources(0, 1, sky_cubemap_srv.GetAddressOf()); // t0



	dc->Draw(4, 0);

	dc->VSSetShader(NULL, 0, 0);
	dc->PSSetShader(NULL, 0, 0);


	ID3D11ShaderResourceView* pNullSRV = nullptr;
	dc->PSSetShaderResources(0, 1, &pNullSRV);

	ID3D11SamplerState* nullSamplers[8] = { nullptr };
	dc->PSSetSamplers(0, 8, nullSamplers);

	dc->RSSetState(original_rasterizer_state.Get());
	dc->OMSetDepthStencilState(original_depth_stencil_state.Get(), original_stencil_ref);
	dc->OMSetBlendState(original_blend_state.Get(), original_blend_factor, original_sample_mask);
}

void SkyMap::updateTransmittance(ID3D11DeviceContext* dc)
{
	atmosphere_constant_buffer->UploadData<AtmosphereConstants>(dc, 3, atmosphere_constants_data, false, false, false, false, false, true);

	dc->CSSetShader(transmittance_cs.Get(), nullptr, 0);
	dc->CSSetUnorderedAccessViews(0, 1, transmittance_uav.GetAddressOf(), nullptr);


	const UINT THREAD_GROUP_SIZE = 8;
	UINT dispatchX = (transmittance_width + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
	UINT dispatchY = (transmittance_height + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
	dc->Dispatch(dispatchX, dispatchY, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	dc->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	dc->CSSetShader(nullptr, nullptr, 0);
}

bool SkyMap::debugGui(DirectX::XMFLOAT4* outLightDirection)
{
	if (skyType != SkyType::Procedural)
		return false;

	bool anyChanged = false;
	auto& params = atmosphere_constants_data;

	ImGui::Separator();
	ImGui::Text("Procedural Sky");

	// 太陽方向
	DirectX::XMFLOAT3 oldSunDir = params.sunDirection;
	if (ImGui::DragFloat3("sunDirection", &params.sunDirection.x, 0.01f, -1.0f, 1.0f))
	{
		DirectX::XMVECTOR sunDirV = DirectX::XMLoadFloat3(&params.sunDirection);
		DirectX::XMVECTOR lenVec = DirectX::XMVector3Length(sunDirV);
		float len = DirectX::XMVectorGetX(lenVec);
		if (len > 1e-6f)
		{
			sunDirV = DirectX::XMVector3Normalize(sunDirV);
			DirectX::XMStoreFloat3(&params.sunDirection, sunDirV);
		}
		else
		{
			params.sunDirection = oldSunDir; 
		}

		
		if (outLightDirection)
		{
			DirectX::XMVECTOR neg = DirectX::XMVectorNegate(DirectX::XMLoadFloat3(&params.sunDirection));
			DirectX::XMFLOAT4 ld;
			DirectX::XMStoreFloat4(&ld, DirectX::XMVectorSetW(neg, 0.0f));
			*outLightDirection = ld;
		}

		anyChanged = true;
	}

	if (ImGui::DragFloat("Sun Intensity", &params.sunIntensity, 0.1f, 0.0f, 20.0f))
		anyChanged = true;
	if (ImGui::DragFloat("Moon Intensity", &params.nightIntensity, 0.01f, 0.0f, 1.0f))
		anyChanged = true;

	ImGui::Separator();
	ImGui::Text("Sky Colors");

	if (ImGui::ColorEdit3("Rayleigh Coefficient", &params.rayleighScatteringCoefficient.x))
	{
		anyChanged = true;
		transmittanceDirty = true;
	}

	if (ImGui::DragFloat("Scale Height##Rayleigh", &params.rayleighScaleHeight, 0.01f, 0.1f, 20.0f))
	{
		anyChanged = true;
		transmittanceDirty = true;
	}

	ImGui::Separator();
	ImGui::Text("Mie (Haze, Sun Corona)");
	if (ImGui::DragFloat("Scattering Coeff##Mie", &params.mieScatteringCoefficient, 0.000001f, 0.0f, 0.0001f, "%.6f"))
	{
		anyChanged = true;
		transmittanceDirty = true;
	}
	if (ImGui::DragFloat("Scale Height##Mie", &params.mieScaleHeight, 0.01f, 0.1f, 5.0f))
	{
		anyChanged = true;
		transmittanceDirty = true;
	}
	if (ImGui::DragFloat("Eccentricity", &params.mieEccentricity, 0.001f, 0.0f, 0.999f))
		anyChanged = true;

	return anyChanged;
}
