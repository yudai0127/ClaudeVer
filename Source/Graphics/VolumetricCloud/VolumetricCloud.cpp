#include "VolumetricCloud.h"
#include "misc.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/Texture/Texture.h"

#include "Graphics/DeviceManager/DeviceManager.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"

#include "imgui.h"


inline float Lerp(float a, float b, float t)
{
	return a + t * (b - a);
}


void VolumetricCloud::initialize(ID3D11Device* device, const wchar_t* filename)
{
	HRESULT hr = S_OK;

	TextureManager::instance()->loadTextureFromFile(device, L".\\Resources\\Sprite\\low_freq_perlin_worley.dds", low_freq_perlin_worley_shader_resource_view.GetAddressOf(), nullptr);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


	TextureManager::instance()->loadTextureFromFile(device, L".\\Resources\\Sprite\\high_freq_worley.dds", high_freq_worley_shader_resource_view.GetAddressOf(), nullptr);
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));


	TextureManager::instance()->loadTextureFromFile(device, L".\\Resources\\Texture\\curl_noise.png", curl_noise_shader_resource_view.GetAddressOf(), nullptr);

	// シェーダーの読み込み
	ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\VolumetricCloud_PS.cso", pixel_shader.GetAddressOf());
	
	ShaderManager::instance()->CreateVsFromCso(device, ".\\Shader\\VolumetricCloud_VS.cso", vertex_shader.GetAddressOf(), nullptr, nullptr, 0);

	ShaderManager::instance()->CreatePsFromCso(device, ".\\Shader\\CloudShadowGen_PS.cso", cloud_shadow_ps.GetAddressOf());

	ShaderManager::instance()->CreateCsFromCso(device, ".\\Shader\\WeatherMap_CS.cso", weather_gen_cs.GetAddressOf());


	// 定数バッファの作成
	
	volumetric_cloud_cb = std::make_unique<GPUConstantBuffer>(device, sizeof(VOLUMETRIC_CLOUD_CONSTANT_BUFFER));
	atmosphere_cb = std::make_unique<GPUConstantBuffer>(device, sizeof(AtmosphereConstants));

	weather_gen_cb = std::make_unique<GPUConstantBuffer>(device, sizeof(WEATHER_GEN_CB));
	

	{
		D3D11_TEXTURE2D_DESC shadow_desc = {};
		shadow_desc.Width = 1024;
		shadow_desc.Height = 1024;
		shadow_desc.MipLevels = 1;
		shadow_desc.ArraySize = 1;
		shadow_desc.Format = DXGI_FORMAT_R16_FLOAT; // 深度(0.0-1.0)を表す
		shadow_desc.SampleDesc.Count = 1;
		shadow_desc.Usage = D3D11_USAGE_DEFAULT;
		shadow_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		hr = device->CreateTexture2D(&shadow_desc, nullptr, cloud_shadow_texture.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
		rtv_desc.Format = shadow_desc.Format;
		rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		hr = device->CreateRenderTargetView(cloud_shadow_texture.Get(), &rtv_desc, cloud_shadow_rtv.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = shadow_desc.Format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;
		hr = device->CreateShaderResourceView(cloud_shadow_texture.Get(), &srv_desc, cloud_shadow_srv.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}

	{
		const UINT W = 256;
		const UINT H = 256;

		D3D11_TEXTURE2D_DESC td = {};
		td.Width = W;
		td.Height = H;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		hr = device->CreateTexture2D(&td, nullptr, weather_texture2d.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = td.Format;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(weather_texture2d.Get(), &sd, weather_shader_resource_view.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = td.Format;
		ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		hr = device->CreateUnorderedAccessView(weather_texture2d.Get(), &ud, weather_uav.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}

	{
		D3D11_BLEND_DESC bd{};
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		device->CreateBlendState(&bd, rain_blend_state.GetAddressOf());
	}

	volumetric_cloud_constant_data = {};
	volumetric_cloud_constant_data.wind_direction = { 1.0f, 0.0f };
	volumetric_cloud_constant_data.cloud_altitudes_min_max = { 9000.0f, 13000.0f };
	volumetric_cloud_constant_data.wind_speed = 0.02f;
	volumetric_cloud_constant_data.density_scale = 0.82f;
	volumetric_cloud_constant_data.cloud_coverage_scale = 0.95f;
	volumetric_cloud_constant_data.rain_cloud_absorption_scale = 0.2f;
	volumetric_cloud_constant_data.cloud_type_scale = 1.0f;
	volumetric_cloud_constant_data.horizon_distance_scale = 1.0f;
	volumetric_cloud_constant_data.low_frequency_perlin_worley_sampling_scale = 0.00022f;
	volumetric_cloud_constant_data.high_frequency_worley_sampling_scale = 0.00170f;
	volumetric_cloud_constant_data.cloud_density_long_distance_scale = 22.0f;
	volumetric_cloud_constant_data.enable_powdered_sugar_efffect = 1;
	volumetric_cloud_constant_data.ray_marching_steps = 128;
	volumetric_cloud_constant_data.auto_ray_marching_steps = 1;
	
}
void VolumetricCloud::blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* sky_cubemap_srv, ID3D11ShaderResourceView* transmittance_srv, ID3D11ShaderResourceView* irradiance_srv, const AtmosphereConstants& atmosphere_data)
{
	
	this->atmosphere_constants_data = atmosphere_data;


	// ステートを保存
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> old_rasterizer_state;
	dc->RSGetState(old_rasterizer_state.GetAddressOf());
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> old_depth_stencil_state;
	UINT old_stencil_ref;
	dc->OMGetDepthStencilState(old_depth_stencil_state.GetAddressOf(), &old_stencil_ref);
	Microsoft::WRL::ComPtr<ID3D11BlendState> old_blend_state;
	FLOAT old_blend_factor[4];
	UINT old_sample_mask;
	dc->OMGetBlendState(old_blend_state.GetAddressOf(), old_blend_factor, &old_sample_mask);

	// ステートを設定
	dc->RSSetState(GraphicsManager::instance()->getRasterizerStates(RASTERIZER_STATE::SOLID_CULLNONE).Get());
	dc->OMSetDepthStencilState(GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::OFF_OFF).Get(), 0);
	dc->OMSetBlendState(GraphicsManager::instance()->getBlendStates(BLEND_STATE::NONE).Get(), nullptr, 0xffffffff);

	atmosphere_cb->UploadData<AtmosphereConstants>(dc, 3, atmosphere_constants_data, false, false, false, false, true, false);

	volumetric_cloud_cb->UploadData<VOLUMETRIC_CLOUD_CONSTANT_BUFFER>(dc, 8, volumetric_cloud_constant_data, false, false, false, false, true, false);

	ID3D11ShaderResourceView* shader_resource_views[] =
	{
		low_freq_perlin_worley_shader_resource_view.Get(),
		high_freq_worley_shader_resource_view.Get(),
		weather_shader_resource_view.Get(),
		curl_noise_shader_resource_view.Get(),
	};
	dc->PSSetShaderResources(0, 1, &sky_cubemap_srv);

	dc->PSSetShaderResources(1, _countof(shader_resource_views), shader_resource_views);

	dc->PSSetShaderResources(5, 1, &transmittance_srv);

	dc->PSSetShaderResources(6, 1, &irradiance_srv); 


	ID3D11SamplerState* samplers[8] = {};
	samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
	samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
	samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
	samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();   
	samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();  
	samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
	samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
	samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();

	
	dc->PSSetSamplers(0, _countof(samplers), samplers);
	

	dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(pixel_shader.Get(), nullptr, 0);

	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->IASetInputLayout(nullptr);

	dc->Draw(4, 0); // 雲を描画


	ID3D11ShaderResourceView* null_shader_resource_views[5] = {}; 
	dc->PSSetShaderResources(0, 5, null_shader_resource_views); 


	dc->VSSetShader(nullptr, nullptr, 0);
	dc->PSSetShader(nullptr, nullptr, 0);

	dc->RSSetState(old_rasterizer_state.Get());
	dc->OMSetDepthStencilState(old_depth_stencil_state.Get(), old_stencil_ref);
	dc->OMSetBlendState(old_blend_state.Get(), old_blend_factor, old_sample_mask);
}

void VolumetricCloud::generateCloudShadow(ID3D11DeviceContext* dc)
{
   
	// ステート保存
	D3D11_VIEWPORT old_vp;
	UINT num_vp = 1;
	dc->RSGetViewports(&num_vp, &old_vp);

	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> old_rtv;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> old_dsv;
	dc->OMGetRenderTargets(1, old_rtv.GetAddressOf(), old_dsv.GetAddressOf());

	// レンダーターゲット設定
	ID3D11RenderTargetView* rtv = cloud_shadow_rtv.Get();
	dc->OMSetRenderTargets(1, &rtv, nullptr);

	
	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	dc->ClearRenderTargetView(rtv, clearColor);

	// ビューポート設定
	D3D11_VIEWPORT vp = {};
	vp.Width = 1024.0f;
	vp.Height = 1024.0f;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	dc->RSSetViewports(1, &vp);

	// 定数バッファ更新
	volumetric_cloud_cb->UploadData<VOLUMETRIC_CLOUD_CONSTANT_BUFFER>(dc, 8, volumetric_cloud_constant_data, false, false, false, false, true, false);

	
	
	ID3D11ShaderResourceView* srvs[] = {
		low_freq_perlin_worley_shader_resource_view.Get(),
		high_freq_worley_shader_resource_view.Get(),
		weather_shader_resource_view.Get(),
		curl_noise_shader_resource_view.Get(),
	};
	
	dc->PSSetShaderResources(1, _countof(srvs), srvs);

	
	ID3D11SamplerState* samplers[8] = {};
	samplers[0] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_POINT).Get();
	samplers[1] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR).Get();
	samplers[2] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_ANISOTROPIC).Get();
	samplers[3] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_POINT).Get();
	samplers[4] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR).Get();
	samplers[5] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_WHITE).Get();
	samplers[6] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::BORDER_BLACK).Get();
	samplers[7] = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::LINEAR_MIRROR).Get();


	dc->PSSetSamplers(0, _countof(samplers), samplers);
	
	dc->VSSetShader(vertex_shader.Get(), nullptr, 0);
	dc->PSSetShader(cloud_shadow_ps.Get(), nullptr, 0);

	// 描画
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->Draw(4, 0);

	// 解除
	ID3D11ShaderResourceView* null_srvs[_countof(srvs)] = {};
	dc->PSSetShaderResources(1, _countof(srvs), null_srvs);
	dc->PSSetShader(nullptr, nullptr, 0);

	// ステート復元
	dc->RSSetViewports(1, &old_vp);
	dc->OMSetRenderTargets(1, old_rtv.GetAddressOf(), old_dsv.Get());
}

void VolumetricCloud::updateWeatherMap(ID3D11DeviceContext* dc, float weatherT)
{
	targetWeatherT = weatherT;

	currentWeatherT = Lerp(currentWeatherT, targetWeatherT, weatherBlendSpeed);

	
	cb.resolution = { 256.0f, 256.0f };
	cb.time = volumetric_cloud_constant_data.time;
	cb.weatherT = currentWeatherT;

	cb.windDir = volumetric_cloud_constant_data.wind_direction;
	cb.windSpeed = volumetric_cloud_constant_data.wind_speed;

	cb.sunnyCoverage = 0.64f;
	cb.rainyCoverage = 0.88f;
	cb.sunnyRain = 0.0f;
	cb.rainyRain = 1.0f;

	cb.sunnyType = 0.70f;
	cb.rainyType = 0.88f;

	cb.noiseScale = 11.0f;
	cb.noiseAmp = 0.18f;

	weather_gen_cb->UploadData<WEATHER_GEN_CB>(dc, 0, cb, false, false, false, false, false, true);

	
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	dc->PSSetShaderResources(3, 1, nullSRV);

	dc->CSSetShader(weather_gen_cs.Get(), nullptr, 0);
	
	ID3D11UnorderedAccessView* uav = weather_uav.Get();
	UINT initialCounts = 0;
	dc->CSSetUnorderedAccessViews(0, 1, &uav, &initialCounts);

	const UINT groupX = (256 + 7) / 8;
	const UINT groupY = (256 + 7) / 8;
	dc->Dispatch(groupX, groupY, 1);

	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	dc->CSSetUnorderedAccessViews(0, 1, nullUAV, &initialCounts);
	dc->CSSetShader(nullptr, nullptr, 0);
}


void VolumetricCloud::debugGui()
{
	if (!ImGui::TreeNode("Volumetric Cloud"))
		return;

	auto& params = volumetric_cloud_constant_data;

	ImGui::DragFloat2("Wind Direction", &params.wind_direction.x, 0.01f, -1.0f, 1.0f);
	ImGui::DragFloatRange2("Cloud Altitudes (world units)", &params.cloud_altitudes_min_max.x, &params.cloud_altitudes_min_max.y, 100.0f, 1000.0f, 80000.0f);
	ImGui::DragFloat("Wind Speed", &params.wind_speed, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Density Scale", &params.density_scale, 0.01f, 0.2f, 10.0f);
	ImGui::DragFloat("Cloud Coverage Scale", &params.cloud_coverage_scale, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Rain Cloud Absorption", &params.rain_cloud_absorption_scale, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Cloud Type Scale", &params.cloud_type_scale, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Horizon Distance Scale", &params.horizon_distance_scale, 0.1f, 0.1f, 1000.0f);

	ImGui::DragFloat("Low Freq Sampling Scale", &params.low_frequency_perlin_worley_sampling_scale, 0.000001f, 0.000005f, 0.001f, "%.8f");
	ImGui::DragFloat("High Freq Sampling Scale", &params.high_frequency_worley_sampling_scale, 0.00001f, 0.00002f, 0.01f, "%.8f");
	ImGui::DragFloat("Long Distance Scale", &params.cloud_density_long_distance_scale, 1.0f, 1.0f, 100.0f);
	ImGui::Checkbox("Powdered Sugar Effect", reinterpret_cast<bool*>(&params.enable_powdered_sugar_efffect));
	ImGui::SliderInt("Ray Marching Steps", &params.ray_marching_steps, 32, 256);
	ImGui::Checkbox("Auto Ray Marching Steps", reinterpret_cast<bool*>(&params.auto_ray_marching_steps));

	int weatherMode = (targetWeatherT < 0.25f) ? 0 : (targetWeatherT < 0.75f ? 1 : 2);
	const char* weatherItems[] = { "Sunny", "Cloudy", "Rainy" };
	if (ImGui::Combo("Cloud Preset", &weatherMode, weatherItems, IM_ARRAYSIZE(weatherItems)))
	{
		float t = (weatherMode == 0) ? 0.0f : (weatherMode == 1 ? 0.5f : 1.0f);
		setWeatherTarget(t);
	}

	if (ImGui::SliderFloat("Weather Transition (manual)", &targetWeatherT, 0.0f, 1.0f))
	{

	}

	ImGui::TreePop();
}
