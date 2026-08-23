#include "GameScene.h"
#include "misc.h"
#include "high_resolution_timer.h"
#include "Graphics/DeviceManager/DeviceManager.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"
#include "Graphics/Debug/ImGuiRenderer.h"
#include "Graphics/Shader/Shader.h"
#include "Camera/Camera.h"



#include "Input/InputManager.h"
#include "Input/Mouse.h"
#include "Graphics/Texture/Texture.h"
#include <vector>
#include <algorithm>


using namespace DirectX;

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }


void GameScene::initialize()
{
	
	
	{
		buffer = std::make_unique<GPUConstantBuffer>(DeviceManager::instance()->getDevice(), sizeof(SceneConstants));
	}
	{
		lightBuffer = std::make_unique<GPUConstantBuffer>(DeviceManager::instance()->getDevice(), sizeof(GameScene::LIGHT_CONSTANT));
	}
	{
		SYSTEM_INFO sysInfo;
		FILETIME ftime, fsys, fuser;

		GetSystemInfo(&sysInfo);
		numProcessors = sysInfo.dwNumberOfProcessors;
		processHandle = GetCurrentProcess();
		GetSystemTimeAsFileTime(&ftime);
		memcpy(&lastSystemTime, &ftime, sizeof(FILETIME));

		GetProcessTimes(processHandle, &ftime, &ftime, &fsys, &fuser);
		memcpy(&lastProcessKernelTime, &fsys, sizeof(FILETIME));
		memcpy(&lastProcessUserTime, &fuser, sizeof(FILETIME));
	}
	D3D11_QUERY_DESC queryDesc = {};
	queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
	DeviceManager::instance()->getDevice()->CreateQuery(&queryDesc, queryDisjoint.GetAddressOf());

	queryDesc.Query = D3D11_QUERY_TIMESTAMP;
	DeviceManager::instance()->getDevice()->CreateQuery(&queryDesc, queryBeginFrame.GetAddressOf());
	DeviceManager::instance()->getDevice()->CreateQuery(&queryDesc, queryEndFrame.GetAddressOf());
	for (uint32_t passIndex = 0; passIndex < GPU_PASS_COUNT; ++passIndex)
	{
		DeviceManager::instance()->getDevice()->CreateQuery(
			&queryDesc, queryPassBegin[passIndex].GetAddressOf());
		DeviceManager::instance()->getDevice()->CreateQuery(
			&queryDesc, queryPassEnd[passIndex].GetAddressOf());
	}

	StageManager* stageMgr = StageManager::instance();

	stageBackground = std::make_unique<StageBackground>();
	stageMgr->regist(stageBackground.get());

	ObjectManager* objMgr = ObjectManager::instance();

	XMFLOAT3 shipPositions[3] = {
		{ 1000.0f, 50.0f, 15000.0f },
		{-35000.0f,50.0f,5000.0f},
		{1000.0f,50.0f,-25000.0f}
	};

	XMFLOAT3 shipRotations[3] = {
	{ 0.0f, XMConvertToRadians(170.0f),   0.0f },
	{ 0.0f, XMConvertToRadians(3000.0f),  0.0f },
	{ 0.0f, XMConvertToRadians(-90.0f), 0.0f }
	};

	for (int i = 0; i < 3; ++i)
	{
		auto newShip = std::make_unique<Ship>();

		newShip->position = shipPositions[i];
		
		newShip->scale = { 60.0f, 60.0f, 60.0f };

		newShip->rotation = shipRotations[i];

		ships.push_back(std::move(newShip));
	}




	DeviceManager* devicmgr = DeviceManager::instance();
	hdrSceneBuffer = std::make_unique<FrameBuffer>(
		devicmgr->getDevice(),
		devicmgr->getScreenWidth(),
		devicmgr->getScreenHeight()
	);
	Camera* camera = Camera::instance();
	cameraCtrl = std::make_unique<CameraController>();

	camera->setPerspectiveFov(
		DirectX::XMConvertToRadians(45),
		devicmgr->getScreenWidth() / devicmgr->getScreenHeight(),
		10.0f,
		200000.0f);


	DirectX::XMFLOAT3 startEye(-3122.40f, 867.93f, 11342.66f); // 初期カメラ位置
	DirectX::XMFLOAT3 startFocus(600.0f, 350.0f, 15000.0f);     // カメラが向くターゲット
	
	cameraCtrl->setMovementBounds(DirectX::XMFLOAT3(-70000.0f, -10.0f, -70000.0f), DirectX::XMFLOAT3(70000.0f, 80000.0f, 70000.0f));

	//カメラにセット
	camera->setLookAt(startEye, startFocus, DirectX::XMFLOAT3(0, 1, 0));

	//フリーカメラに上書きされないよう、変数を逆算して初期化
	freeCameraTarget = startFocus;

	DirectX::XMVECTOR vEye = DirectX::XMLoadFloat3(&startEye);
	DirectX::XMVECTOR vFocus = DirectX::XMLoadFloat3(&startFocus);
	DirectX::XMVECTOR vDir = DirectX::XMVectorSubtract(vFocus, vEye);

	// 距離の計算
	DirectX::XMVECTOR vLength = DirectX::XMVector3Length(vDir);
	DirectX::XMStoreFloat(&freeCameraRange, vLength);

	// 角度の計算
	DirectX::XMVECTOR vDirNorm = DirectX::XMVector3Normalize(vDir);
	DirectX::XMFLOAT3 dir;
	DirectX::XMStoreFloat3(&dir, vDirNorm);

	freeCameraAngle.x = asinf(-dir.y);               // Pitch
	freeCameraAngle.y = atan2f(-dir.x, -dir.z);

	
	
	elapsedTime = 0.0f;
	// Model lighting defaults. The BRDF contains the 1/PI diffuse
	// normalization, so the previous 1.0-class sun and 0.1 diffuse IBL left
	// ships and terrain several stops too dark.
	iblDiffuseIntensity = 0.50f;
	iblSpecularIntensity = 0.55f;

	skyMap = std::make_unique<SkyMap>();
	skyMap->initialize(DeviceManager::instance()->getDevice());
	skyMap->skyType = SkyMap::SkyType::Procedural;

	volumetricCloud = std::make_unique<VolumetricCloud>();
    volumetricCloud->initialize(DeviceManager::instance()->getDevice(), L".\\Resources\\Texture\\weather.01.dds");
	
	D3D11_TEXTURE2D_DESC texture2d_desc{};
	TextureManager::instance()->loadTextureFromFile(DeviceManager::instance()->getDevice(), L".\\Resources\\Sprite\\lut_ggx.dds", lut_ggx_srv.GetAddressOf(), &texture2d_desc);
	TextureManager::instance()->loadTextureFromFile(DeviceManager::instance()->getDevice(), L".\\Resources\\Sprite\\lut_charlie.dds", lut_charrlie_srv.GetAddressOf(), &texture2d_desc);
	ShaderManager::instance()->CreateCsFromCso(devicmgr->getDevice(), ".\\Shader\\Irradiance_CS.cso", irradiance_cs.GetAddressOf());
	ShaderManager::instance()->CreateCsFromCso(devicmgr->getDevice(), ".\\Shader\\SpecularFilter_CS.cso", specular_filter_cs.GetAddressOf());
	{
		HRESULT hr = S_OK;
		const UINT DIFFUSE_MAP_SIZE = 64;
		D3D11_TEXTURE2D_DESC diffuseDesc = {};
		diffuseDesc.Width = DIFFUSE_MAP_SIZE;
		diffuseDesc.Height = DIFFUSE_MAP_SIZE;
		diffuseDesc.MipLevels = 1;
		diffuseDesc.ArraySize = 6;
		diffuseDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		diffuseDesc.SampleDesc.Count = 1;
		diffuseDesc.Usage = D3D11_USAGE_DEFAULT;
		diffuseDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		diffuseDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		hr = devicmgr->getDevice()->CreateTexture2D(&diffuseDesc, nullptr, diffuse_iem_texture.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		hr = devicmgr->getDevice()->CreateShaderResourceView(diffuse_iem_texture.Get(), nullptr, diffuse_iem_srv.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		hr = devicmgr->getDevice()->CreateUnorderedAccessView(diffuse_iem_texture.Get(), nullptr, diffuse_iem_uav.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		const UINT SPECULAR_MAP_SIZE = 256;
		const UINT SPECULAR_MIP_LEVELS = 8;
		D3D11_TEXTURE2D_DESC specularDesc = diffuseDesc;
		specularDesc.Width = SPECULAR_MAP_SIZE;
		specularDesc.Height = SPECULAR_MAP_SIZE;
		specularDesc.MipLevels = SPECULAR_MIP_LEVELS;
		hr = devicmgr->getDevice()->CreateTexture2D(&specularDesc, nullptr, specular_pmrem_texture.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		hr = devicmgr->getDevice()->CreateShaderResourceView(specular_pmrem_texture.Get(), nullptr, specular_pmrem_srv.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		specular_pmrem_uav_mips.resize(SPECULAR_MIP_LEVELS);
		for (UINT mip = 0; mip < SPECULAR_MIP_LEVELS; ++mip)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = specularDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.MipSlice = mip;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = 6;
			hr = devicmgr->getDevice()->CreateUnorderedAccessView(specular_pmrem_texture.Get(), &uavDesc, specular_pmrem_uav_mips[mip].GetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
		}
		specular_cb = std::make_unique<GPUConstantBuffer>(devicmgr->getDevice(), sizeof(SpecularConstants));
		material_roughness_cb = std::make_unique<GPUConstantBuffer>(devicmgr->getDevice(), sizeof(MaterialRoughnessConstants));
	}
	cascadeShadowMap = std::make_unique<CascadeShadowMap>(DeviceManager::instance()->getDevice());

	water_simulation = std::make_unique<Water_Simulation>();
	// Fine surface detail comes from the normal maps. A 1280x1280 geometry grid
	// was spending most of its time evaluating waves on sub-pixel vertices in
	// three water passes per frame without a visible benefit.
	constexpr uint32_t WATER_GRID_RESOLUTION = 512;
	if (!water_simulation->Initialize(DeviceManager::instance()->getDevice(),
		WATER_GRID_RESOLUTION, WATER_GRID_RESOLUTION))
	{
		OutputDebugStringA("GameScene::initialize: Water_Simulation::Initialize failed\n");
		water_simulation.reset();
	}

	if (water_simulation)
	{
		if (!water_simulation->CreateRenderTargets(DeviceManager::instance()->getDevice(),
			devicmgr->getScreenWidth(), devicmgr->getScreenHeight()))
		{
			OutputDebugStringA("GameScene::initialize: Water_Simulation::CreateRenderTargets failed\n");
		}

		
	}

	// Caustics are broad and are bilinearly upsampled by the water shader, so
	// half resolution removes 75% of this off-screen pixel work.
	causticsBuffer = std::make_unique<FrameBuffer>(
		devicmgr->getDevice(),
		static_cast<uint32_t>(devicmgr->getScreenWidth()) / 2u,
		static_cast<uint32_t>(devicmgr->getScreenHeight()) / 2u);

	ShaderManager::instance()->CreatePsFromCso(
		devicmgr->getDevice(),
		".\\Shader\\Caustics_PS.cso",
		causticsPS.GetAddressOf());

	ShaderManager::instance()->CreateVsFromCso(
		devicmgr->getDevice(),
		".\\Shader\\Caustics_VS.cso",
		causticsVS.GetAddressOf(),nullptr,nullptr,0);
	causticsCB = std::make_unique<GPUConstantBuffer>(devicmgr->getDevice(), sizeof(CausticsCB));


	gbuffer = std::make_unique<Gbuffer>();
    gbuffer->initialize(devicmgr->getDevice(), devicmgr->getScreenWidth(), devicmgr->getScreenHeight());
	
	ssr = std::make_unique<ScreenSpaceReflection>();
	ssr->initialize(devicmgr->getDevice());
	ssr->resize(devicmgr->getDevice(), devicmgr->getScreenWidth(), devicmgr->getScreenHeight());
	

	QueryPerformanceFrequency(&qpcFreq);
	QueryPerformanceCounter(&prevQpc);

	rainSystem = std::make_unique<RainSystem>();
	rainSystem->initialize(devicmgr->getDevice());

	depthOfField = std::make_unique<DepthOfField>();
	depthOfField->initialize(devicmgr->getDevice(), devicmgr->getScreenWidth(), devicmgr->getScreenHeight());
	if (depthOfField)
	{
		depthOfFieldParams = depthOfField->GetParams();
	}
	
	bit_block_transfer = std::make_unique<Fullscreen_Quad>(devicmgr->getDevice());
	{
		HRESULT hr = ShaderManager::instance()->CreatePsFromCso(
			devicmgr->getDevice(),
			".\\Shader\\FinalPass_PS.cso",
			finalPassPS.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}
	{
		HRESULT hr;
		atmoLowResBuffer = std::make_unique<FrameBuffer>(
			devicmgr->getDevice(),
			atmoLowResWidth,
			atmoLowResHeight);

		atmoBlurTempBuffer = std::make_unique<FrameBuffer>(
			devicmgr->getDevice(),
			atmoLowResWidth,
			atmoLowResHeight);

		atmoBlurBuffer = std::make_unique<FrameBuffer>(
			devicmgr->getDevice(),
			atmoLowResWidth,
			atmoLowResHeight);

		atmoBlurCB = std::make_unique<GPUConstantBuffer>(devicmgr->getDevice(), sizeof(ATMOSPHERE_BLUR_CB));


		hr = ShaderManager::instance()->CreatePsFromCso(
			devicmgr->getDevice(),
			".\\Shader\\Dof_Blurh_PS.cso",
			atmoBlurHPS.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		hr = ShaderManager::instance()->CreatePsFromCso(
			devicmgr->getDevice(),
			".\\Shader\\Dof_Blurv_PS.cso",
			atmoBlurVPS.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		atmoBlurInvRes = DirectX::XMFLOAT2(
			1.0f / static_cast<float>(atmoLowResWidth),
			1.0f / static_cast<float>(atmoLowResHeight));
	}
}

void GameScene::finalize()
{
}

void GameScene::update(float elapsedTime)
{
	// FPS/CPU/GPU表示用の計測値を更新
	updatePerformanceMetrics(elapsedTime);

	this->elapsedTime += elapsedTime;
	
	for (const auto& ship : ships)
	{
		ship->update(elapsedTime);
	}

	// 雲シミュレーション時間と天候マップを更新
	static unsigned int weatherUpdateFrame = 0;
	if (enableVolumetricCloud && volumetricCloud)
	{
		volumetricCloud->volumetric_cloud_constant_data.time += elapsedTime;
		if ((weatherUpdateFrame++ % 4u) == 0u)
		{
			volumetricCloud->updateWeatherMap(DeviceManager::instance()->getDeviceContext(), volumetricCloud->getTargetWeather());
		}
	}

	const float cloudWeather = (enableVolumetricCloud && volumetricCloud)
		? volumetricCloud->getTargetWeather()
		: 0.0f;
	const float weatherOvercast = std::clamp((cloudWeather - 0.22f) / 0.78f, 0.0f, 1.0f);
	if (skyMap)
	{
		skyMap->atmosphere_constants_data._padding2.x = weatherOvercast;
	}
	const bool rainActive = enableVolumetricCloud && volumetricCloud && cloudWeather > 0.58f;
	// 雨粒システムを更新（天候テクスチャを参照）
	if (rainSystem && rainActive)
	{
		Camera* cam = Camera::instance();
		rainSystem->update(
			DeviceManager::instance()->getDeviceContext(),
			volumetricCloud->getWeatherTextureSRV(),
			realDt,
			*cam->getView(),
			*cam->getProjection(),
			*cam->getEye()
		);
	}

	// 雨の着弾位置を読み戻して水面波紋へ注入
	static unsigned int rainReadbackFrame = 0;
	if (rainSystem && rainActive && water_simulation && (rainReadbackFrame++ % 3u) == 0u)
	{
		auto* dc = DeviceManager::instance()->getDeviceContext();

		std::vector<DirectX::XMFLOAT4> hits;
		const UINT maxRead = 256;
		UINT hitCount = rainSystem->ReadbackHits(dc, hits, maxRead);

		auto* rippleSim = water_simulation->GetRippleSimulation();
		if (hitCount > 0 && rippleSim)
		{
			DirectX::XMFLOAT3 waterCenter = water_simulation->GetWorldCenter();
			DirectX::XMFLOAT2 waterSize = water_simulation->GetWorldSize();
			const float halfX = waterSize.x * 0.5f;
			const float halfZ = waterSize.y * 0.5f;

			const float rippleResX = (float)rippleSim->getWidth();
			const float rippleResY = (float)rippleSim->getHeight();

			float texelWorldX = waterSize.x / (rippleResX - 1.0f);
			float texelWorldZ = waterSize.y / (rippleResY - 1.0f);
			float texelWorld = 0.5f * (texelWorldX + texelWorldZ);

			const float radiusWorldBase = 0.12f;
			const float minRadiusPix = 2.0f;
			const float maxRadiusPix = 24.0f;
			const float strengthBase = 0.05f;
			const float strengthMultiplier = 0.2f;

			for (const auto& h : hits)
			{
				float u = (h.x - waterCenter.x + halfX) / waterSize.x;
				float v = (h.z - waterCenter.z + halfZ) / waterSize.y;

				if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
					continue;

				DirectX::XMFLOAT2 rippleCenter = { u * rippleResX, v * rippleResY };

				float impact = fabsf(h.w);
				float vel = -(strengthBase + impact * strengthMultiplier);
				if (vel > -0.02f) vel = -0.02f;

				float radiusPix = (radiusWorldBase / texelWorld);
				radiusPix = std::clamp(radiusPix, minRadiusPix, maxRadiusPix);

				rippleSim->InjectRipple(dc, rippleCenter, vel, radiusPix);
			}
		}
	}
	auto& cp = volumetricCloud->volumetric_cloud_constant_data;

	// 昼夜サイクルは太陽を動かすかどうかだけを制御する。
	// 照明色の評価は停止中も行い、手動で夜へ動かしたときに
	// 昼の環境光が残らないようにする。
	if (isDayNightCycleEnabled)
	{
		const float minCycleDuration = 0.1f;
		float cycleDurationSeconds = (dayNightCycleDurationSeconds > minCycleDuration) ? dayNightCycleDurationSeconds : minCycleDuration;
		const float angularSpeed = 2.0f * DirectX::XM_PI / cycleDurationSeconds;
		dayNightPhaseRadians = fmodf(
			dayNightPhaseRadians + elapsedTime * angularSpeed,
			2.0f * DirectX::XM_PI);

		DirectX::XMVECTOR sunDir = DirectX::XMVectorSet(
			sinf(dayNightPhaseRadians), cosf(dayNightPhaseRadians), 0.2f, 0.0f);
		sunDir = DirectX::XMVector3Normalize(sunDir);

		DirectX::XMStoreFloat3(&skyMap->atmosphere_constants_data.sunDirection, sunDir);
	}
	if (!isDayNightCycleEnabled)
	{
		// Capture once on the running -> paused transition, then explicitly keep
		// that direction. This prevents any delayed sky/IBL update from advancing
		// the visible sun while the checkbox says Paused.
		if (wasDayNightCycleEnabled)
			pausedSunDirection = skyMap->atmosphere_constants_data.sunDirection;
		else
			skyMap->atmosphere_constants_data.sunDirection = pausedSunDirection;

		dayNightPhaseRadians = atan2f(pausedSunDirection.x, pausedSunDirection.y);
		if (dayNightPhaseRadians < 0.0f)
			dayNightPhaseRadians += 2.0f * DirectX::XM_PI;
	}
	wasDayNightCycleEnabled = isDayNightCycleEnabled;

	// Keep ships, terrain, sky and water on one time-of-day palette even when
	// the animation is paused or the sun direction is edited manually.
	DirectX::XMVECTOR sunDir = DirectX::XMVector3Normalize(
		DirectX::XMLoadFloat3(&skyMap->atmosphere_constants_data.sunDirection));
	DirectX::XMStoreFloat3(&skyMap->atmosphere_constants_data.sunDirection, sunDir);

	DirectX::XMFLOAT3 sunDirection{};
	DirectX::XMStoreFloat3(&sunDirection, sunDir);
	const float sunY = sunDirection.y;
	const float dayVisibility = std::clamp((sunY + 0.06f) * 6.0f, 0.0f, 1.0f);
	const float nightVisibility = std::clamp((-sunY - 0.04f) * 4.0f, 0.0f, 1.0f);
	const float dayWhiteness = std::clamp((sunY - 0.04f) * 3.0f, 0.0f, 1.0f);
	const float horizonGlow = (1.0f - std::clamp(fabsf(sunY) / 0.34f, 0.0f, 1.0f)) * dayVisibility;

	if (sunY >= -0.08f)
	{
		// Sun direction is surface-to-light; the renderer stores the direction
		// in which the directional light travels, hence the negation.
		DirectX::XMVECTOR lightDir = DirectX::XMVectorNegate(sunDir);
		DirectX::XMStoreFloat4(&LightDirection, DirectX::XMVectorSetW(lightDir, 0.0f));

		const DirectX::XMFLOAT3 sunsetColor{ 1.0f, 0.30f, 0.075f };
		const DirectX::XMFLOAT3 daylightColor{ 1.0f, 0.98f, 0.90f };
		const float directIntensity = (0.06f + dayVisibility * 2.55f) * (1.0f + horizonGlow * 0.22f);
		LightColor = {
			lerp(sunsetColor.x, daylightColor.x, dayWhiteness) * directIntensity,
			lerp(sunsetColor.y, daylightColor.y, dayWhiteness) * directIntensity,
			lerp(sunsetColor.z, daylightColor.z, dayWhiteness) * directIntensity,
			1.0f };
	}
	else
	{
		// The moon is placed opposite the sun. Its cool, weak light keeps the
		// silhouette readable without making night look like a dim daytime scene.
		DirectX::XMVECTOR moonDir = DirectX::XMVectorNegate(sunDir);
		DirectX::XMVECTOR lightDir = DirectX::XMVectorNegate(moonDir);
		DirectX::XMStoreFloat4(&LightDirection, DirectX::XMVectorSetW(lightDir, 0.0f));

		const float moonIntensity = skyMap->atmosphere_constants_data.nightIntensity * nightVisibility;
		const DirectX::XMFLOAT3 moonColor{ 0.45f, 0.60f, 1.0f };
		LightColor = {
			moonColor.x * moonIntensity,
			moonColor.y * moonIntensity,
			moonColor.z * moonIntensity,
			1.0f };
	}

	const float directWeatherScale =
		lerp(1.0f, 0.58f, weatherOvercast * dayVisibility);
	LightColor.x *= directWeatherScale;
	LightColor.y *= directWeatherScale;
	LightColor.z *= directWeatherScale;

	const DirectX::XMFLOAT3 nightAmbient{ 0.018f, 0.028f, 0.065f };
	const DirectX::XMFLOAT3 dayAmbient{ 0.22f, 0.24f, 0.26f };
	DirectX::XMFLOAT3 ambient{
		lerp(nightAmbient.x, dayAmbient.x, dayVisibility),
		lerp(nightAmbient.y, dayAmbient.y, dayVisibility),
		lerp(nightAmbient.z, dayAmbient.z, dayVisibility) };
	ambient.x = lerp(ambient.x, 0.16f, horizonGlow * 0.60f);
	ambient.y = lerp(ambient.y, 0.060f, horizonGlow * 0.60f);
	ambient.z = lerp(ambient.z, 0.032f, horizonGlow * 0.60f);
	const DirectX::XMFLOAT3 overcastAmbient{ 0.10f, 0.12f, 0.15f };
	const float weatherAmbientBlend = weatherOvercast * dayVisibility * 0.82f;
	ambient.x = lerp(ambient.x, overcastAmbient.x, weatherAmbientBlend);
	ambient.y = lerp(ambient.y, overcastAmbient.y, weatherAmbientBlend);
	ambient.z = lerp(ambient.z, overcastAmbient.z, weatherAmbientBlend);
	AmbientColor = { ambient.x, ambient.y, ambient.z, 0.0f };

	// マウス入力で自由カメラ更新
	updateFreeCamera(elapsedTime);

	if (water_simulation)
	{
		// 波紋生成（クリック注入 + 自動注入）
		injectRippleFromCursor();
		injectAutoRipple(elapsedTime);
		injectShipInteractionRipples(elapsedTime);

		Camera* cam = Camera::instance();
		DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(cam->getView());
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(cam->getProjection());
		DirectX::XMMATRIX viewProj = view * proj;
		const DirectX::XMFLOAT3* camPos = cam->getEye();

		
		water_simulation->update(
			DeviceManager::instance()->getDeviceContext(),
			elapsedTime,
			world,
			view,
			proj,
			viewProj,
			*camPos);
	}
}


void GameScene::render()
{
	DeviceManager* mgr = DeviceManager::instance();
	GraphicsManager* graphics = GraphicsManager::instance();

	ID3D11DeviceContext* dc = mgr->getDeviceContext();
	ID3D11RenderTargetView* backbufferRTV = mgr->getRenderTargetView();
	ID3D11DepthStencilView* dsv = mgr->getDepthStencilView();

	// HDRバッファ有効時は一旦そちらへ描画
	ID3D11RenderTargetView* hdrRTV = hdrSceneBuffer ? hdrSceneBuffer->render_target_view.Get() : backbufferRTV;

	beginGpuQuery(dc);

	// シャドウマップ先行パス
	beginGpuPass(dc, GpuPass::Shadow);
	renderShadow(dc);
	endGpuPass(dc, GpuPass::Shadow);

	FLOAT color[] = { 0.0f, 0.0f, 0.5f, 1.0f };
	dc->ClearRenderTargetView(hdrRTV, color);
	dc->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	dc->OMSetRenderTargets(1, &hdrRTV, dsv);

	Camera* camera = Camera::instance();
	const DirectX::XMFLOAT4X4* view = camera->getView();
	const DirectX::XMFLOAT4X4* proj = camera->getProjection();
	const DirectX::XMFLOAT3* cameraWorldPosMetersPtr = camera->getPosition();

	// シーン共通定数（ViewProj/逆行列/カメラ位置）を更新
	SceneConstants sc;
	{
		DirectX::XMMATRIX View = DirectX::XMLoadFloat4x4(camera->getView());
		DirectX::XMMATRIX Projection = DirectX::XMLoadFloat4x4(camera->getProjection());

		DirectX::XMMATRIX ViewProjection = View * Projection;
		DirectX::XMStoreFloat4x4(&sc.view_projection, ViewProjection);

		DirectX::XMMATRIX InvViewProjection = DirectX::XMMatrixInverse(nullptr, ViewProjection);
		DirectX::XMStoreFloat4x4(&sc.inv_view_projection, InvViewProjection);

		const DirectX::XMFLOAT3* eye = camera->getEye();
		sc.camera_position = DirectX::XMFLOAT4(eye->x, eye->y, eye->z, 1.0f);
	}

	buffer->UploadData<SceneConstants>(dc, 1, sc, true, false, false, false, true, false);


	// IBL更新と大気描画
	beginGpuPass(dc, GpuPass::AtmosphereIBL);
	{
		DirectX::XMFLOAT4X4 viewProjection;
		DirectX::XMStoreFloat4x4(&viewProjection, DirectX::XMLoadFloat4x4(view) * DirectX::XMLoadFloat4x4(proj));

		updateIBLMaps(dc, *cameraWorldPosMetersPtr);
		renderAtmosphere(dc, hdrRTV, dsv, viewProjection);
	}
	endGpuPass(dc, GpuPass::AtmosphereIBL);

	beginGpuPass(dc, GpuPass::SceneGBuffer);
	buffer->UploadData<SceneConstants>(dc, 1, sc, true, false, false, false, true, false);


	// PBR参照テクスチャ群
	dc->PSSetShaderResources(7, 1, lut_charrlie_srv.GetAddressOf());
	dc->PSSetShaderResources(8, 1, diffuse_iem_srv.GetAddressOf());
	dc->PSSetShaderResources(9, 1, specular_pmrem_srv.GetAddressOf());
	dc->PSSetShaderResources(10, 1, lut_ggx_srv.GetAddressOf());

	graphics->SettingRenderContext([](ID3D11DeviceContext* dc, RenderContext* rc) {
		ID3D11SamplerState* samplers[8] = {
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_POINT)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_LINEAR)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_ANISOTROPIC)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::CLAMP_POINT)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::CLAMP_LINEAR)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::BORDER_WHITE)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::BORDER_BLACK)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::LINEAR_MIRROR)].Get(),
		};
		dc->PSSetSamplers(0, 8, samplers);
		dc->OMSetBlendState(rc->blendStates[static_cast<uint32_t>(BLEND_STATE::ALPHABLENDING)].Get(), nullptr, 0xFFFFFFFF);
		dc->OMSetDepthStencilState(rc->depthStencilStates[static_cast<uint32_t>(DEPTH_STENCIL_STATE::ON_ON)].Get(), 0);
		dc->RSSetState(rc->rasterizerStates[static_cast<uint32_t>(RASTERIZER_STATE::SOLID_CULLNONE)].Get());
		});

	LIGHT_CONSTANT lc;
	{
		lc.ambientColor = AmbientColor;
		lc.lightDirection = LightDirection;
		lc.lightColor = LightColor;
		lc.iblParams = DirectX::XMFLOAT4(iblDiffuseIntensity, iblSpecularIntensity, iblSheenIntensity, directSheenIntensity);
	}

	{
		// ライト定数とマテリアル粗さ係数を更新
		lightBuffer->UploadData<LIGHT_CONSTANT>(dc, 2, lc, true, false, false, false, true, false);


		
		roughnessCB.global_roughness_scale = globalRoughnessScale;
		material_roughness_cb->UploadData<MaterialRoughnessConstants>(dc, 5, roughnessCB, false, false, false, false, true, false);


		if (useCascadeShadowMap && cascadeShadowMap)
		{
			cascadeShadowMap->setShaderResources(dc, 12);
			cascadeShadowMap->setConstantBuffer(dc, 4);
		}

		ID3D11DepthStencilView* sceneDSV = dsv;
		if (gbuffer)
		{
			// GBufferをクリアしてMRTへ描画
			const float clear[4] = { 0, 0, 0, 0 };
			dc->ClearRenderTargetView(gbuffer->get_rtv(1), clear);
			dc->ClearRenderTargetView(gbuffer->get_rtv(2), clear);
			dc->ClearRenderTargetView(gbuffer->get_rtv(3), clear);
			dc->ClearDepthStencilView(gbuffer->get_dsv(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			ID3D11RenderTargetView* mrts[4] = {
				hdrRTV,
				gbuffer->get_rtv(1),
				gbuffer->get_rtv(2),
				gbuffer->get_rtv(3)
			};
			dc->OMSetRenderTargets(4, mrts, gbuffer->get_dsv());

			StageManager::instance()->render(dc);
			ObjectManager::instance()->render(dc);

			if (!ships.empty())
			{
				std::vector<DirectX::XMFLOAT4X4> shipWorlds;
				shipWorlds.reserve(ships.size());
				for (const auto& ship : ships)
				{
					shipWorlds.emplace_back(ship->buildWorld());
				}
				Ship::renderInstanced(dc, shipWorlds);
			}

			copySceneDepth(dc);

			// 水面のGBuffer前処理（法線/粗さなど）
			if (water_simulation)
			{
				GraphicsManager* graphicsLocal = GraphicsManager::instance();
				ID3D11RenderTargetView* waterRTV = gbuffer->get_rtv(1);
				dc->OMSetRenderTargets(1, &waterRTV, gbuffer->get_dsv());

				auto blendNone = graphicsLocal->getBlendStates(BLEND_STATE::NONE).Get();
				auto depthOnOn = graphicsLocal->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_ON).Get();
				
				dc->OMSetBlendState(blendNone, nullptr, 0xFFFFFFFF);
				dc->OMSetDepthStencilState(depthOnOn, 0);
				

				ID3D11ShaderResourceView* rippleSRV = water_simulation->GetRippleSimulation()->getDisplacementMap();
				water_simulation->renderGBufferPrepass(dc, rippleSRV);

				auto blendAlpha = graphicsLocal->getBlendStates(BLEND_STATE::ALPHABLENDING).Get();
				dc->OMSetBlendState(blendAlpha, nullptr, 0xFFFFFFFF);
			}

			sceneDSV = gbuffer->get_dsv();
			dc->OMSetRenderTargets(1, &hdrRTV, sceneDSV);
		}
		else
		{
			StageManager::instance()->render(dc);
			ObjectManager::instance()->render(dc);

			if (!ships.empty())
			{
				std::vector<DirectX::XMFLOAT4X4> shipWorlds;
				shipWorlds.reserve(ships.size());
				for (const auto& ship : ships)
				{
					shipWorlds.emplace_back(ship->buildWorld());
				}
				Ship::renderInstanced(dc, shipWorlds);
			}
		}

		// 後段ポストエフェクト用に現在のシーンカラーを退避
		copySceneColor(dc, hdrRTV);
	}
	endGpuPass(dc, GpuPass::SceneGBuffer);

	// SSR
	beginGpuPass(dc, GpuPass::SSR);
	if (useSSR && ssr && sceneColorCopySRV && gbuffer)
	{
		dc->OMSetRenderTargets(1, &hdrRTV, nullptr);

		Camera* cam = Camera::instance();
		const auto* view2 = cam->getView();
		const auto* proj2 = cam->getProjection();

		
		const float camFar = cam->getFar();

		// シーン規模に応じてSSRの探索距離を拡張
		ssrParams.screen_space_reflection_max_distance = min(camFar * 0.2f, 200000.0f);

		// 深度誤差対策で厚みも少し増やす
		ssrParams.screen_space_reflection_tickness = max(1.0f, camFar * 0.00002f);

		ssr->update(
			dc,
			*view2,
			*proj2,
			cam->getNear(),
			cam->getFar(),
			mgr->getScreenWidth(),
			mgr->getScreenHeight(),
			ssrParams);

		ID3D11ShaderResourceView* normalRoughnessSRV = gbuffer->get_srv(1);
		ID3D11ShaderResourceView* depthSRV = gbuffer->get_depth_srv();

		if (normalRoughnessSRV && depthSRV)
		{
			ssr->render(dc, sceneColorCopySRV.Get(), normalRoughnessSRV, depthSRV);
			ssrColorSRV = ssr->getResultSRV();
		}
		else
		{
			ssrColorSRV.Reset();
		}
	}
	else
	{
		ssrColorSRV.Reset();
	}
	endGpuPass(dc, GpuPass::SSR);

	
	DirectX::XMVECTOR lightDir = DirectX::XMLoadFloat4(&LightDirection);
	float sunDirY = -DirectX::XMVectorGetY(lightDir);
	float causticsVisibility = std::clamp((sunDirY - 0.02f) * 4.0f, 0.0f, 1.0f);

	beginGpuPass(dc, GpuPass::Caustics);
	if (causticsBuffer && causticsPS && water_simulation && causticsVisibility > 0.0f)
	{
		
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(camera->getView());
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(camera->getProjection());
		DirectX::XMMATRIX viewProj = view * proj;
		DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
		DirectX::XMStoreFloat4x4(&cb.gInvViewProjection, invViewProj);

		cb.params = DirectX::XMFLOAT4(causticsScale, causticsWobble, causticsPower, causticsIntensity * causticsVisibility);
		cb.invScreenSize = DirectX::XMFLOAT2(
			2.0f / static_cast<float>(mgr->getScreenWidth()),
			2.0f / static_cast<float>(mgr->getScreenHeight()));
		cb.time = elapsedTime * causticsSpeed;
		cb.waterPlaneY = water_simulation->worldOffsetY;
		cb.lightDirection = LightDirection;

		causticsCB->UploadData<CausticsCB>(dc, 8, cb, true, false, false, false, true, false);


		causticsBuffer->activate(dc);
		causticsBuffer->clear(dc, 0, 0, 0, 1);

		auto blendAdd = GraphicsManager::instance()->getBlendStates(BLEND_STATE::ADD).Get();
		auto depthOff = GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::OFF_OFF).Get();
		dc->OMSetBlendState(blendAdd, nullptr, 0xFFFFFFFF);
		dc->OMSetDepthStencilState(depthOff, 0);

		ID3D11ShaderResourceView* sceneNormalSRV = (gbuffer) ? gbuffer->get_srv(1) : nullptr;

		if (sceneDepthCopySRV)
		{
			water_simulation->renderCaustics(
				dc,
				causticsVS.Get(),
				causticsPS.Get(),
				sceneDepthCopySRV.Get()
			);
		}

		causticsBuffer->deactivate(dc);
	}
	endGpuPass(dc, GpuPass::Caustics);

	

	// 水面描画
	beginGpuPass(dc, GpuPass::Water);
	if (water_simulation)
	{
		ID3D11DepthStencilView* sceneDSV = (gbuffer) ? gbuffer->get_dsv_readonly() : dsv;
		dc->OMSetRenderTargets(1, &hdrRTV, sceneDSV);

		

		auto depthTestReadOnly = graphics->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_OFF).Get();
		dc->OMSetDepthStencilState(depthTestReadOnly, 0);

		ID3D11ShaderResourceView* sceneSRV = sceneColorCopySRV.Get();
		ID3D11ShaderResourceView* skySrv = (skyMap) ? skyMap->getSkyCubemapSRV() : nullptr;
		ID3D11ShaderResourceView* ssrSRV = ssrColorSRV ? ssrColorSRV.Get() : nullptr;
		ID3D11ShaderResourceView* rippleSRV = water_simulation->GetRippleSimulation()->getDisplacementMap();

		ID3D11ShaderResourceView* depthSRV =
			(gbuffer) ? gbuffer->get_depth_srv() : mgr->getDepthShaderResourceView();

		ID3D11ShaderResourceView* sceneNormalSRV =
			(gbuffer) ? gbuffer->get_srv(1) : nullptr;

		ID3D11ShaderResourceView* causticsSRV =
			(causticsBuffer) ? causticsBuffer->shader_resource_views[0].Get() : nullptr;

		water_simulation->render(
			dc,
			sceneSRV,
			sceneNormalSRV,
			causticsSRV,
			skySrv,
			ssrSRV,
			rippleSRV,
			sceneDepthCopySRV.Get());

		auto depthOn = graphics->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_ON).Get();
		dc->OMSetDepthStencilState(depthOn, 0);
	}
	endGpuPass(dc, GpuPass::Water);

	// DoF
	beginGpuPass(dc, GpuPass::DepthOfField);
	if (useDoF && depthOfField && sceneColorCopySRV)
	{
		copySceneColor(dc, hdrRTV);

		ID3D11ShaderResourceView* depthSRV =
			(gbuffer) ? gbuffer->get_depth_srv() : mgr->getDepthShaderResourceView();

		if (depthSRV)
		{
			depthOfFieldParams.gNear = camera->getNear();
			depthOfFieldParams.gFar = camera->getFar();
			depthOfField->SetParams(depthOfFieldParams);
			depthOfField->render(dc, sceneColorCopySRV.Get(), depthSRV, hdrRTV);
		}
	}
	endGpuPass(dc, GpuPass::DepthOfField);

	

	beginGpuPass(dc, GpuPass::DebugRain);
	graphics->getLineRenderer()->render(dc, *view, *proj);
	graphics->getDebugRenderer()->render(dc, *view, *proj);

	graphics->SettingRenderContext([](ID3D11DeviceContext* dc, RenderContext* rc) {
		ID3D11SamplerState* samplers[8] = {
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_POINT)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_LINEAR)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::WRAP_ANISOTROPIC)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::CLAMP_POINT)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::CLAMP_LINEAR)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::BORDER_WHITE)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::BORDER_BLACK)].Get(),
			rc->samplerStates[static_cast<uint32_t>(SAMPLER_STATE::LINEAR_MIRROR)].Get(),
		};
		dc->PSSetSamplers(0, 8, samplers);
		dc->OMSetBlendState(rc->blendStates[static_cast<uint32_t>(BLEND_STATE::ALPHABLENDING)].Get(), nullptr, 0xFFFFFFFF);
		dc->OMSetDepthStencilState(rc->depthStencilStates[static_cast<uint32_t>(DEPTH_STENCIL_STATE::ON_ON)].Get(), 0);
		dc->RSSetState(rc->rasterizerStates[static_cast<uint32_t>(RASTERIZER_STATE::SOLID_CULLNONE)].Get());
		});

	// 雨の描画
	if (rainSystem && enableVolumetricCloud && volumetricCloud && volumetricCloud->getTargetWeather() > 0.55f)
	{
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		dc->PSSetShaderResources(1, 1, nullSRV);

		ID3D11ShaderResourceView* depthSRV =
			(gbuffer) ? gbuffer->get_depth_srv() : mgr->getDepthShaderResourceView();

		rainSystem->render(dc, depthSRV, hdrRTV);
	}
	endGpuPass(dc, GpuPass::DebugRain);

	// HDR -> バックバッファへ最終合成
	beginGpuPass(dc, GpuPass::FinalComposite);
	if (bit_block_transfer && finalPassPS && hdrSceneBuffer)
	{
		ID3D11RenderTargetView* nullRTV[1] = { nullptr };
		dc->OMSetRenderTargets(1, nullRTV, nullptr);

		ID3D11DepthStencilView* nullDSV = nullptr;
		dc->OMSetRenderTargets(1, &backbufferRTV, nullDSV);

		auto blendNone = graphics->getBlendStates(BLEND_STATE::NONE).Get();
		auto depthOff = graphics->getDepthStencilStates(DEPTH_STENCIL_STATE::OFF_OFF).Get();
		dc->OMSetBlendState(blendNone, nullptr, 0xFFFFFFFF);
		dc->OMSetDepthStencilState(depthOff, 0);

		auto linearSampler = graphics->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR);
		ID3D11SamplerState* linearSamplerPtr = linearSampler.Get();
		if (linearSamplerPtr)
		{
			// FinalPass_PS indexes sampler_states[ClampLinear], so the sampler
			// must be bound at the enum's actual register (s4), not s1.
			const UINT samplerSlot = static_cast<UINT>(SAMPLER_STATE::CLAMP_LINEAR);
			dc->PSSetSamplers(samplerSlot, 1, &linearSamplerPtr);
		}

		ID3D11ShaderResourceView* src = hdrSceneBuffer->shader_resource_views[0].Get();
		bit_block_transfer->blit(dc, &src, 0, 1, finalPassPS.Get());

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		dc->PSSetShaderResources(0, 1, nullSRV);
	}
	endGpuPass(dc, GpuPass::FinalComposite);

	// GUI描画
	dc->OMSetRenderTargets(1, &backbufferRTV, dsv);
	debugGui();

	endGpuQuery(dc);
}

void GameScene::renderShadow(ID3D11DeviceContext* dc)
{
	// 機能無効または未初期化時はスキップ
	if (!useCascadeShadowMap || !cascadeShadowMap)
		return;

	// シャドウテクスチャがPSに残っていると競合するため先に解除
	cascadeShadowMap->UnbindShaderResources(dc, 12);

	// 影パス後に戻すため現在のビューポートを退避
	D3D11_VIEWPORT originalViewport;
	UINT numViewports = 1;
	dc->RSGetViewports(&numViewports, &originalViewport);

	Camera* camera = Camera::instance();

	// カスケード分割と各ライト行列を計算
	cascadeShadowMap->CalculateCascades(
		dc,
		*camera->getView(),
		*camera->getProjection(),
		LightDirection,
		DirectX::XMConvertToRadians(45.0f),
		static_cast<float>(DeviceManager::instance()->getScreenWidth()) /
		static_cast<float>(DeviceManager::instance()->getScreenHeight()),
		*camera->getPosition());

	auto shouldRenderCascade = [this](int cascadeIndex)->bool {
		if (!staggerShadowUpdates) return true;
		// シンプルに 1 つだけ更新する場合
		for (int k = 0; k < shadowUpdatesPerFrame; ++k)
		{
			int idx = (shadowUpdateIndex + k) % CASCADE_COUNT;
			if (idx == cascadeIndex) return true;
		}
		return false;
		};

	for (int i = 0; i < CASCADE_COUNT; ++i)
	{
		if (!shouldRenderCascade(i))
			continue;

		// i番目カスケードの深度描画開始
		cascadeShadowMap->beginShadowRender(dc, i);

		// 影生成用の固定ステート
		dc->OMSetDepthStencilState(GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::ON_ON).Get(), 0);
		dc->RSSetState(GraphicsManager::instance()->getRasterizerStates(RASTERIZER_STATE::SOLID_CULLNONE).Get());
		dc->OMSetBlendState(GraphicsManager::instance()->getBlendStates(BLEND_STATE::NONE).Get(), nullptr, 0xFFFFFFFF);

		// このカスケード専用のViewProjectionをVSへ設定
		SceneConstants shadowPassConstants;
		DirectX::XMStoreFloat4x4(
			&shadowPassConstants.view_projection,
			DirectX::XMLoadFloat4x4(&cascadeShadowMap->getConstants().cascade_light_view_projection[i]));
		buffer->UploadData<SceneConstants>(dc, 1, shadowPassConstants, true, false, false, false, false, false);


		StageManager::instance()->renderShadow(dc);
		ObjectManager::instance()->renderShadow(dc);

		if (!ships.empty())
		{
			std::vector<DirectX::XMFLOAT4X4> shipWorlds;
			shipWorlds.reserve(ships.size());
			for (const auto& ship : ships)
			{
				shipWorlds.emplace_back(ship->buildWorld());
			}
			Ship::renderShadowInstanced(dc, shipWorlds);
		}

		cascadeShadowMap->endShadowRender(dc);
	}

	if (staggerShadowUpdates)
	{
		shadowUpdateIndex = (shadowUpdateIndex + shadowUpdatesPerFrame) % CASCADE_COUNT;
	}

	// 本来のビューポートとRTへ復帰
	dc->RSSetViewports(1, &originalViewport);

	ID3D11RenderTargetView* rtv = DeviceManager::instance()->getRenderTargetView();
	ID3D11DepthStencilView* dsv = DeviceManager::instance()->getDepthStencilView();
	dc->OMSetRenderTargets(1, &rtv, dsv);
}

void GameScene::renderAtmosphere(ID3D11DeviceContext* dc, ID3D11RenderTargetView* hdrRTV, ID3D11DepthStencilView* dsv, const DirectX::XMFLOAT4X4& viewProjection)
{
	const AtmosphereConstants& atmosphereData = skyMap->getAtmosphereConstants();

	// 低解像度描画 + 2passブラー経由
	if (enableLowResAtmosphere &&
		bit_block_transfer &&
		atmoLowResBuffer &&
		atmoBlurTempBuffer &&
		atmoBlurBuffer &&
		atmoBlurHPS &&
		atmoBlurVPS)
	{
		//低解像度バッファに空/雲を描く
		atmoLowResBuffer->activate(dc);
		atmoLowResBuffer->clear(dc, 0, 0, 0, 1);

		skyMap->render(dc, viewProjection);

		if (enableVolumetricCloud && volumetricCloud && skyMap)
		{
			ID3D11ShaderResourceView* skySrv = skyMap->getSkyCubemapSRV();
			if (skySrv)
			{
				volumetricCloud->blit(dc, skySrv, skyMap->getTransmittanceSRV(), diffuse_iem_srv.Get(), atmosphereData);
			}
		}

		atmoLowResBuffer->deactivate(dc);

		//ブラー定数を設定
		
		blurCB.gInvHalfRes = atmoBlurInvRes;
		atmoBlurCB->UploadData<ATMOSPHERE_BLUR_CB>(dc, 1, blurCB, false, false, false, false, true, false);


		auto linearSampler = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::CLAMP_LINEAR);
		ID3D11SamplerState* linearSamplerPtr = linearSampler.Get();
		if (linearSamplerPtr)
		{
			dc->PSSetSamplers(0, 1, &linearSamplerPtr);
			dc->PSSetSamplers(1, 1, &linearSamplerPtr);
		}

		//横ブラー
		atmoBlurTempBuffer->activate(dc);
		atmoBlurTempBuffer->clear(dc, 0, 0, 0, 1);
		{
			ID3D11ShaderResourceView* src[1] = { atmoLowResBuffer->shader_resource_views[0].Get() };
			bit_block_transfer->blit(dc, src, 0, 1, atmoBlurHPS.Get());
			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
			dc->PSSetShaderResources(0, 1, nullSRV);
		}
		atmoBlurTempBuffer->deactivate(dc);

		//縦ブラー
		atmoBlurBuffer->activate(dc);
		atmoBlurBuffer->clear(dc, 0, 0, 0, 1);
		{
			ID3D11ShaderResourceView* src[1] = { atmoBlurTempBuffer->shader_resource_views[0].Get() };
			bit_block_transfer->blit(dc, src, 0, 1, atmoBlurVPS.Get());
			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
			dc->PSSetShaderResources(0, 1, nullSRV);
		}
		atmoBlurBuffer->deactivate(dc);

		//HDRターゲットへ合成
		{
			ID3D11DepthStencilView* nullDSV = nullptr;
			dc->OMSetRenderTargets(1, &hdrRTV, nullDSV);

			auto blendNone = GraphicsManager::instance()->getBlendStates(BLEND_STATE::NONE).Get();
			auto depthOff = GraphicsManager::instance()->getDepthStencilStates(DEPTH_STENCIL_STATE::OFF_OFF).Get();
			dc->OMSetBlendState(blendNone, nullptr, 0xFFFFFFFF);
			dc->OMSetDepthStencilState(depthOff, 0);

			ID3D11ShaderResourceView* src[1] = { atmoBlurBuffer->shader_resource_views[0].Get() };
			bit_block_transfer->blit(dc, src, 0, 1, nullptr);

			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
			dc->PSSetShaderResources(0, 1, nullSRV);

			dc->OMSetRenderTargets(1, &hdrRTV, dsv);
		}
	}
	else
	{
		// 通常解像度で直接描画
		skyMap->render(dc, viewProjection);

		if (enableVolumetricCloud && volumetricCloud && skyMap)
		{
			ID3D11ShaderResourceView* skySrv = skyMap->getSkyCubemapSRV();
			if (skySrv)
			{
				volumetricCloud->blit(dc, skySrv, skyMap->getTransmittanceSRV(), diffuse_iem_srv.Get(), atmosphereData);
			}
		}
	}
}

void GameScene::updatePerformanceMetrics(float elapsedTime)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	// 実時間ベースのdeltaを計算（フレーム依存を避ける）
	double dt = double(now.QuadPart - prevQpc.QuadPart) / double(qpcFreq.QuadPart);
	prevQpc = now;

	// 異常値ガード
	if (dt < 0.000001) dt = 0.000001;
	if (dt > 0.25)     dt = 0.25;

	realDt = static_cast<float>(dt);

	// FPSは0.5秒ごとに更新して表示の揺れを抑える
	fpsAccum += realDt;
	fpsFrames++;

	if (fpsAccum >= 0.5f)
	{
		fps = fpsFrames / fpsAccum;
		fpsFrames = 0;
		fpsAccum = 0.0f;
	}

	// CPU使用率は1秒周期で更新
	performanceUpdateTimer += elapsedTime;
	if (performanceUpdateTimer >= 1.0f)
	{
		performanceUpdateTimer -= 1.0f;

		FILETIME ftime, fsys, fuser;
		ULARGE_INTEGER now_ul, sys, user;
		GetSystemTimeAsFileTime(&ftime);
		memcpy(&now_ul, &ftime, sizeof(FILETIME));

		GetProcessTimes(processHandle, &ftime, &ftime, &fsys, &fuser);
		memcpy(&sys, &fsys, sizeof(FILETIME));
		memcpy(&user, &fuser, sizeof(FILETIME));

		double percent = static_cast<double>((sys.QuadPart - lastProcessKernelTime.QuadPart) +
			(user.QuadPart - lastProcessUserTime.QuadPart));
		percent /= static_cast<double>(now_ul.QuadPart - lastSystemTime.QuadPart);
		percent /= numProcessors;

		lastSystemTime = now_ul;
		lastProcessUserTime = user;
		lastProcessKernelTime = sys;
		cpuUsage = static_cast<float>(percent * 100);
	}

	// グラフ用リングバッファ
	for (int i = 0; i < GRAPH_HISTORY_COUNT - 1; ++i)
	{
		fpsHistory[i] = fpsHistory[i + 1];
		cpuHistory[i] = cpuHistory[i + 1];
	}
	fpsHistory[GRAPH_HISTORY_COUNT - 1] = fps;
	cpuHistory[GRAPH_HISTORY_COUNT - 1] = cpuUsage;
}

void GameScene::beginGpuQuery(ID3D11DeviceContext* dc)
{
	gpuQueryRecording = false;
	if (queryStarted)
	// 前フレーム分のGPUタイムスタンプ結果を回収
	if (queryStarted)
	{
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
		UINT64 startTime = 0;
		UINT64 endTime = 0;

		const bool frameDataReady =
			dc->GetData(queryDisjoint.Get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
			dc->GetData(queryBeginFrame.Get(), &startTime, sizeof(startTime), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
			dc->GetData(queryEndFrame.Get(), &endTime, sizeof(endTime), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;

		if (frameDataReady)
		{
			if (!disjointData.Disjoint)
			{
				UINT64 delta = endTime - startTime;
				double frequency = static_cast<double>(disjointData.Frequency);
				double gpuTimeSeconds = static_cast<double>(delta) / frequency;
				for (uint32_t passIndex = 0; passIndex < GPU_PASS_COUNT; ++passIndex)
				{
					UINT64 passBeginTime = 0;
					UINT64 passEndTime = 0;
					const bool passReady =
						dc->GetData(queryPassBegin[passIndex].Get(), &passBeginTime,
							sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
						dc->GetData(queryPassEnd[passIndex].Get(), &passEndTime,
							sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
					if (!passReady || passEndTime < passBeginTime)
						continue;

					const UINT64 passDelta = passEndTime - passBeginTime;
					const float currentPassMs = static_cast<float>(
						(static_cast<double>(passDelta) / frequency) * 1000.0);
					if (gpuPassTimeMs[passIndex] <= 0.0f)
						gpuPassTimeMs[passIndex] = currentPassMs;
					else
						gpuPassTimeMs[passIndex] = lerp(gpuPassTimeMs[passIndex], currentPassMs, 0.10f);
				}
				const float currentGpuFrameTimeMs = static_cast<float>(gpuTimeSeconds * 1000.0);
				if (gpuFrameTimeMs <= 0.0f)
					gpuFrameTimeMs = currentGpuFrameTimeMs;
				else
					gpuFrameTimeMs = lerp(gpuFrameTimeMs, currentGpuFrameTimeMs, 0.10f);

				// This is an occupancy estimate for the measured scene frame, not the
				// system-wide GPU utilization reported by Task Manager. Use the CPU
				// interval captured when this exact timestamp query was started.
				float frameTime = gpuQueryFrameIntervalSeconds;
				if (frameTime <= 0.0f) frameTime = 0.016f;
				float currentUsage = static_cast<float>(gpuTimeSeconds / frameTime) * 100.0f;

				gpuUsage = lerp(gpuUsage, currentUsage, 0.05f);
				if (gpuUsage > 100.0f) gpuUsage = 100.0f;
				if (gpuUsage < 0.0f) gpuUsage = 0.0f;

				for (int i = 0; i < GRAPH_HISTORY_COUNT - 1; ++i)
				{
					gpuHistory[i] = gpuHistory[i + 1];
				}
				gpuHistory[GRAPH_HISTORY_COUNT - 1] = gpuFrameTimeMs;
			}
			queryStarted = false;
		}
	}

	// 今フレーム計測を開始
	if (!queryStarted)
	{
		gpuQueryFrameIntervalSeconds = (realDt > 0.0f) ? realDt : 0.016f;
		dc->Begin(queryDisjoint.Get());
		dc->End(queryBeginFrame.Get());
		gpuQueryRecording = true;
	}
}

void GameScene::endGpuQuery(ID3D11DeviceContext* dc)
{
	// begin側で開始した計測を閉じる
	if (gpuQueryRecording)
	{
		dc->End(queryEndFrame.Get());
		dc->End(queryDisjoint.Get());
		queryStarted = true;
		gpuQueryRecording = false;
	}
}

void GameScene::beginGpuPass(ID3D11DeviceContext* dc, GpuPass pass)
{
	if (!gpuQueryRecording)
		return;

	const uint32_t passIndex = static_cast<uint32_t>(pass);
	if (passIndex < GPU_PASS_COUNT && queryPassBegin[passIndex])
		dc->End(queryPassBegin[passIndex].Get());
}

void GameScene::endGpuPass(ID3D11DeviceContext* dc, GpuPass pass)
{
	if (!gpuQueryRecording)
		return;

	const uint32_t passIndex = static_cast<uint32_t>(pass);
	if (passIndex < GPU_PASS_COUNT && queryPassEnd[passIndex])
		dc->End(queryPassEnd[passIndex].Get());
}

void GameScene::injectRippleFromCursor()
{
	if (!water_simulation)
		return;

	// UI操作中はワールド側の入力を無効化
	if (ImGui::GetIO().WantCaptureMouse)
		return;

	Mouse* mouse = InputManager::instance()->getMouse();
	if (!(mouse->getButton() & Mouse::BTN_LEFT))
		return;

	DeviceManager* devicmgr = DeviceManager::instance();
	const float screenW = static_cast<float>(devicmgr->getScreenWidth());
	const float screenH = static_cast<float>(devicmgr->getScreenHeight());

	// 画面座標 -> NDC
	float ndcX = (static_cast<float>(mouse->getPositionX()) / screenW) * 2.0f - 1.0f;
	float ndcY = (static_cast<float>(mouse->getPositionY()) / screenH) * 2.0f - 1.0f;
	ndcY = -ndcY;

	Camera* cam = Camera::instance();
	DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(cam->getView());
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(cam->getProjection());
	DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, view * proj);

	// NDCのnear/farをワールドへ戻してレイを作る
	DirectX::XMVECTOR nearPoint = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
	DirectX::XMVECTOR farPoint = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

	nearPoint = DirectX::XMVector4Transform(nearPoint, invViewProj);
	farPoint = DirectX::XMVector4Transform(farPoint, invViewProj);

	nearPoint = DirectX::XMVectorScale(nearPoint, 1.0f / DirectX::XMVectorGetW(nearPoint));
	farPoint = DirectX::XMVectorScale(farPoint, 1.0f / DirectX::XMVectorGetW(farPoint));

	DirectX::XMVECTOR dir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farPoint, nearPoint));
	DirectX::XMVECTOR origin = nearPoint;

	// 水面平面との交点を計算
	const float dirY = DirectX::XMVectorGetY(dir);
	if (fabsf(dirY) < 1e-5f)
		return;

	const float waterY = water_simulation->worldOffsetY;
	const float t = (waterY - DirectX::XMVectorGetY(origin)) / dirY;
	if (t < 0.0f)
		return;

	DirectX::XMVECTOR hit = DirectX::XMVectorAdd(origin, DirectX::XMVectorScale(dir, t));
	DirectX::XMFLOAT3 hitPos;
	DirectX::XMStoreFloat3(&hitPos, hit);

	const auto* rippleSim = water_simulation->GetRippleSimulation();
	if (!rippleSim)
		return;

	DirectX::XMFLOAT3 waterCenter = water_simulation->GetWorldCenter();
	DirectX::XMFLOAT2 waterSize = water_simulation->GetWorldSize();
	const float halfX = waterSize.x * 0.5f;
	const float halfZ = waterSize.y * 0.5f;

	// ワールド座標 -> 水面UV
	const float u = (hitPos.x - waterCenter.x + halfX) / waterSize.x;
	const float v = (hitPos.z - waterCenter.z + halfZ) / waterSize.y;

	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		return;

	const float rippleResX = static_cast<float>(rippleSim->getWidth());
	const float rippleResY = static_cast<float>(rippleSim->getHeight());
	DirectX::XMFLOAT2 rippleCenter = { u * rippleResX, v * rippleResY };

	// クリック位置へ波紋を注入
	water_simulation->GetRippleSimulation()->InjectRipple(
		DeviceManager::instance()->getDeviceContext(),
		rippleCenter,
		-1.0f,
		8.0f
	);
}

void GameScene::injectAutoRipple(float elapsedTime)
{
	if (!water_simulation || !enableAutoRipple)
		return;

	// 指定間隔ごとに中心へ波紋を注入
	rippleTimer += elapsedTime;
	if (rippleTimer < autoRippleInterval)
		return;

	rippleTimer = 0.0f;

	const auto* rippleSim = water_simulation->GetRippleSimulation();
	if (!rippleSim)
		return;

	DirectX::XMFLOAT2 center = {
		static_cast<float>(rippleSim->getWidth()) * 0.5f,
		static_cast<float>(rippleSim->getHeight()) * 0.5f };

	water_simulation->GetRippleSimulation()->InjectRipple(
		DeviceManager::instance()->getDeviceContext(),
		center,
		-0.2f,
		5.0f
	);
}

void GameScene::injectShipInteractionRipples(float elapsedTime)
{
	if (!water_simulation || !enableShipInteractionRipples || ships.empty())
		return;

	shipRippleTimer += elapsedTime;
	if (shipRippleTimer < shipRippleInterval)
		return;

	shipRippleTimer = 0.0f;
	++shipRipplePhase;
	auto* dc = DeviceManager::instance()->getDeviceContext();
	const float waterY = water_simulation->worldOffsetY;

	for (const auto& ship : ships)
	{
		const float yaw = ship->rotation.y;
		const DirectX::XMFLOAT3 forward{ sinf(yaw), 0.0f, cosf(yaw) };
		const DirectX::XMFLOAT3 right{ forward.z, 0.0f, -forward.x };

		// Emit just outside the hull instead of at its centre. The resulting
		// rings split around the silhouette and make hull contact readable.
		DirectX::XMFLOAT3 bow{
			ship->position.x + forward.x * 850.0f,
			waterY,
			ship->position.z + forward.z * 850.0f };
		DirectX::XMFLOAT3 stern{
			ship->position.x - forward.x * 720.0f,
			waterY,
			ship->position.z - forward.z * 720.0f };

		const float sideSign = (shipRipplePhase & 1u) ? 1.0f : -1.0f;
		DirectX::XMFLOAT3 side{
			ship->position.x + right.x * 330.0f * sideSign,
			waterY,
			ship->position.z + right.z * 330.0f * sideSign };

		water_simulation->InjectRippleWorld(dc, bow, 0.34f, 260.0f);
		water_simulation->InjectRippleWorld(dc, stern, 0.22f, 210.0f);
		water_simulation->InjectRippleWorld(dc, side, 0.16f, 160.0f);
	}
}




void GameScene::updateFreeCamera(float elapsedTime)
{
	Mouse* mouse = InputManager::instance()->getMouse();
	Camera* camera = Camera::instance();

	float moveX = static_cast<float>(mouse->getDeltaX());
	float moveY = static_cast<float>(mouse->getDeltaY());

	// 右ドラッグ: オービット回転（Yaw/Pitch）
	if (mouse->getButton() & Mouse::BTN_RIGHT)
	{
		freeCameraAngle.y += moveX * 0.005f;
		if (freeCameraAngle.y > DirectX::XM_PI)
			freeCameraAngle.y -= DirectX::XM_2PI;
		else if (freeCameraAngle.y < -DirectX::XM_PI)
			freeCameraAngle.y += DirectX::XM_2PI;

		freeCameraAngle.x += moveY * 0.005f;
		const float maxPitch = DirectX::XMConvertToRadians(89.9f);
		constexpr float minPitch = -DirectX::XMConvertToRadians(89.9f);
		if (freeCameraAngle.x > maxPitch)
			freeCameraAngle.x = maxPitch;
		else if (freeCameraAngle.x < minPitch)
			freeCameraAngle.x = minPitch;
	}
	// 中ドラッグ: ターゲット平行移動
	else if (mouse->getButton() & Mouse::BTN_MIDDLE)
	{
		DirectX::XMMATRIX viewMatrix = DirectX::XMLoadFloat4x4(camera->getView());
		DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixInverse(nullptr, viewMatrix);
		DirectX::XMFLOAT4X4 W;
		DirectX::XMStoreFloat4x4(&W, worldMatrix);

		float scale = freeCameraRange * 0.001f;
		float panX = moveX * scale;
		float panY = moveY * scale;

		freeCameraTarget.x -= W._11 * panX;
		freeCameraTarget.y -= W._12 * panX;
		freeCameraTarget.z -= W._13 * panX;

		freeCameraTarget.x += W._21 * panY;
		freeCameraTarget.y += W._22 * panY;
		freeCameraTarget.z += W._23 * panY;
	}

	// ホイール: 距離ズーム
	int wheel = mouse->getWheel();
	if (wheel != 0)
	{
		freeCameraRange -= static_cast<float>(wheel) * freeCameraRange * 0.001f;
		if (freeCameraRange < 0.1f) freeCameraRange = 0.1f;
	}

	// 球面座標からカメラ位置を再計算
	float sx = ::sinf(freeCameraAngle.x);
	float cx = ::cosf(freeCameraAngle.x);
	float sy = ::sinf(freeCameraAngle.y);
	float cy = ::cosf(freeCameraAngle.y);

	DirectX::XMVECTOR focusVec = DirectX::XMLoadFloat3(&freeCameraTarget);
	DirectX::XMVECTOR frontVec = DirectX::XMVectorSet(-cx * sy, -sx, -cx * cy, 0.0f);
	DirectX::XMVECTOR distanceVec = DirectX::XMVectorReplicate(freeCameraRange);

	frontVec = DirectX::XMVectorMultiply(frontVec, distanceVec);
	DirectX::XMVECTOR eyeVec = DirectX::XMVectorSubtract(focusVec, frontVec);

	DirectX::XMFLOAT3 eyePos;
	DirectX::XMStoreFloat3(&eyePos, eyeVec);

	if (cameraCtrl && cameraCtrl->isMovementBoundsEnabled())
	{
		auto clampf = [](float v, float lo, float hi) {
			if (v < lo) return lo;
			if (v > hi) return hi;
			return v;
			};

		auto clampFloat3 = [&](const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& minV, const DirectX::XMFLOAT3& maxV) {
			return DirectX::XMFLOAT3(
				clampf(v.x, minV.x, maxV.x),
				clampf(v.y, minV.y, maxV.y),
				clampf(v.z, minV.z, maxV.z));
			};

		const DirectX::XMFLOAT3 minB = cameraCtrl->getMovementBoundsMin();
		const DirectX::XMFLOAT3 maxB = cameraCtrl->getMovementBoundsMax();

		auto computeAxis = [](float eye, float target, float minV, float maxV, float& outMin, float& outMax) {
			outMin = max(minV - eye, minV - target);
			outMax = min(maxV - eye, maxV - target);
			return outMin <= outMax;
			};

		float tMinX = 0.0f, tMaxX = 0.0f;
		float tMinY = 0.0f, tMaxY = 0.0f;
		float tMinZ = 0.0f, tMaxZ = 0.0f;

		const bool canFit =
			computeAxis(eyePos.x, freeCameraTarget.x, minB.x, maxB.x, tMinX, tMaxX) &&
			computeAxis(eyePos.y, freeCameraTarget.y, minB.y, maxB.y, tMinY, tMaxY) &&
			computeAxis(eyePos.z, freeCameraTarget.z, minB.z, maxB.z, tMinZ, tMaxZ);

		if (canFit)
		{
			const float tx = std::clamp(0.0f, tMinX, tMaxX);
			const float ty = std::clamp(0.0f, tMinY, tMaxY);
			const float tz = std::clamp(0.0f, tMinZ, tMaxZ);

			freeCameraTarget.x += tx;
			freeCameraTarget.y += ty;
			freeCameraTarget.z += tz;

			eyePos.x += tx;
			eyePos.y += ty;
			eyePos.z += tz;
		}
		else
		{
			const DirectX::XMFLOAT3 before = freeCameraTarget;
			freeCameraTarget = clampFloat3(freeCameraTarget, minB, maxB);

			const DirectX::XMFLOAT3 delta = {
				freeCameraTarget.x - before.x,
				freeCameraTarget.y - before.y,
				freeCameraTarget.z - before.z
			};

			eyePos.x += delta.x;
			eyePos.y += delta.y;
			eyePos.z += delta.z;

			eyePos = clampFloat3(eyePos, minB, maxB);
		}
	}

	camera->setLookAt(eyePos, freeCameraTarget, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
}

void GameScene::updateIBLMaps(ID3D11DeviceContext* dc, const DirectX::XMFLOAT3& cameraPos)
{
	static int s_skyVisualUpdateCounter = 0;
	static int s_iblUpdateCounter = 0;
	// The visible sky needs a much finer cadence than the filtered reflection
	// maps. Updating them together every 12 frames made the setting sun jump.
	constexpr int SKY_VISUAL_UPDATE_INTERVAL = 2;
	constexpr int IBL_UPDATE_INTERVAL = 12;
	static DirectX::XMFLOAT3 s_lastCameraPos = cameraPos;
	auto cameraAltitudeChanged = [](const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float thresholdMeters) {
		return fabsf(a.y - b.y) > thresholdMeters;
		};

	// 毎フレーム更新は重いので、間引き＋カメラ移動時のみ再生成
	const bool altitudeChanged = cameraAltitudeChanged(cameraPos, s_lastCameraPos, 100.0f);
	const bool needSkyVisualUpdate =
		(s_skyVisualUpdateCounter++ % SKY_VISUAL_UPDATE_INTERVAL) == 0 || altitudeChanged;
	if (needSkyVisualUpdate)
	{
		skyMap->update(dc, cameraPos);
		s_lastCameraPos = cameraPos;
	}

	// Irradiance and specular PMREM change slowly and remain expensive, so keep
	// those on the lower-frequency schedule.
	const bool needIBLUpdate =
		(s_iblUpdateCounter++ % IBL_UPDATE_INTERVAL) == 0 || altitudeChanged;

	ID3D11ShaderResourceView* sourceSkySRV = skyMap->getSkyCubemapSRV();
	ID3D11UnorderedAccessView* pNullUAV[1] = { nullptr };
	ID3D11ShaderResourceView* pNullSRV[1] = { nullptr };

	// SRV/UAV競合を避けるため先に使用スロットを解除
	{
		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		dc->PSSetShaderResources(8, 2, nullSRVs);
	}

	Microsoft::WRL::ComPtr<ID3D11SamplerState> csSamplerCP = GraphicsManager::instance()->getSamplerState(SAMPLER_STATE::WRAP_LINEAR);
	ID3D11SamplerState* csSamplerPtr = csSamplerCP.Get();
	if (csSamplerPtr)
	{
		dc->CSSetSamplers(0, 1, &csSamplerPtr);
	}

	if (needIBLUpdate)
	{
		// Diffuse IBL（Irradiance）
		dc->CSSetShader(irradiance_cs.Get(), nullptr, 0);
		dc->CSSetShaderResources(0, 1, &sourceSkySRV);

		ID3D11UnorderedAccessView* diffuseUAVptr = diffuse_iem_uav.Get();
		dc->CSSetUnorderedAccessViews(0, 1, &diffuseUAVptr, nullptr);

		dc->Dispatch(8, 8, 6);

		dc->CSSetUnorderedAccessViews(0, 1, pNullUAV, nullptr);

		// Specular IBL（PMREM）をmipごとに生成
		dc->CSSetShader(specular_filter_cs.Get(), nullptr, 0);
		dc->CSSetShaderResources(0, 1, &sourceSkySRV);
		
		const UINT SPECULAR_MIP_LEVELS = static_cast<UINT>(specular_pmrem_uav_mips.size());
		constexpr UINT THREAD_GROUP = 8;
		const UINT MAX_SPECULAR_SAMPLES = 128;
		const UINT MIN_SPECULAR_SAMPLES = 8;

		for (UINT mip = 0; mip < SPECULAR_MIP_LEVELS; ++mip)
		{
			SpecularConstants scSpec{};
			scSpec.roughness = static_cast<float>(mip) / static_cast<float>(std::max<UINT>(1, SPECULAR_MIP_LEVELS - 1));

			UINT samples = static_cast<UINT>(MAX_SPECULAR_SAMPLES * (1.0f - scSpec.roughness) + 0.5f);
			samples = std::max<UINT>(MIN_SPECULAR_SAMPLES, samples);
			scSpec.numSamples = samples;

			specular_cb->UploadData<SpecularConstants>(dc, 0, scSpec, false, false, false, false, false, true);


			ID3D11UnorderedAccessView* pmremUavPtr = specular_pmrem_uav_mips[mip].Get();
			dc->CSSetUnorderedAccessViews(0, 1, &pmremUavPtr, nullptr);

			UINT mipSize = 1;
			if (specular_pmrem_texture)
			{
				D3D11_TEXTURE2D_DESC specDesc = {};
				specular_pmrem_texture->GetDesc(&specDesc);
				mipSize = std::max<UINT>(1, specDesc.Width >> mip);
			}

			UINT dispatchSize = (mipSize + THREAD_GROUP - 1) / THREAD_GROUP;
			dc->Dispatch(dispatchSize, dispatchSize, 6);

			dc->CSSetUnorderedAccessViews(0, 1, pNullUAV, nullptr);
		}

		dc->CSSetShaderResources(0, 1, pNullSRV);
		dc->CSSetShader(nullptr, nullptr, 0);
	}
}

void GameScene::setWeatherTarget(float weatherT)
{
	targetWeatherT = weatherT;
}

void GameScene::copySceneColor(ID3D11DeviceContext* dc, ID3D11RenderTargetView* rtv)
{
	// 現在の描画先RTVが指す実体リソースを取得
	Microsoft::WRL::ComPtr<ID3D11Resource> rtvRes;
	rtv->GetResource(rtvRes.GetAddressOf());

	// RTVの実体を2Dテクスチャとして扱う
	Microsoft::WRL::ComPtr<ID3D11Texture2D> backbufferTex;
	rtvRes.As(&backbufferTex);

	D3D11_TEXTURE2D_DESC bbDesc{};
	backbufferTex->GetDesc(&bbDesc);

	// コピー先テクスチャが未作成、またはサイズ/フォーマット不一致なら作り直す
	const bool needCreateSceneColor =
		!sceneColorCopyTex ||
		[this, &bbDesc]() {
		D3D11_TEXTURE2D_DESC cur{};
		sceneColorCopyTex->GetDesc(&cur);
		return cur.Width != bbDesc.Width ||
			cur.Height != bbDesc.Height ||
			cur.Format != bbDesc.Format;
		}();

	if (needCreateSceneColor)
	{
		D3D11_TEXTURE2D_DESC copyDesc{};
		copyDesc.Width = bbDesc.Width;
		copyDesc.Height = bbDesc.Height;
		copyDesc.MipLevels = 1;
		copyDesc.ArraySize = 1;
		copyDesc.Format = bbDesc.Format;
		copyDesc.SampleDesc.Count = 1;
		copyDesc.SampleDesc.Quality = 0;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		sceneColorCopyTex.Reset();
		sceneColorCopySRV.Reset();

		HRESULT hr = DeviceManager::instance()->getDevice()->CreateTexture2D(&copyDesc, nullptr, sceneColorCopyTex.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		// 後段のポストエフェクトで参照できるようにSRVを作成
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = copyDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		hr = DeviceManager::instance()->getDevice()->CreateShaderResourceView(sceneColorCopyTex.Get(), &srvDesc, sceneColorCopySRV.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}

	// MSAA時はResolve、非MSAA時はそのままCopy
	if (bbDesc.SampleDesc.Count > 1)
	{
		dc->ResolveSubresource(sceneColorCopyTex.Get(), 0, backbufferTex.Get(), 0, bbDesc.Format);
	}
	else
	{
		dc->CopyResource(sceneColorCopyTex.Get(), backbufferTex.Get());
	}
}



void GameScene::copySceneDepth(ID3D11DeviceContext* dc)
{
	// GBufferから現在の深度バッファ(SRV)の実体リソースを取得
	ID3D11ShaderResourceView* depthSRV = gbuffer->get_depth_srv();
	if (!depthSRV) return;

	Microsoft::WRL::ComPtr<ID3D11Resource> res;
	depthSRV->GetResource(res.GetAddressOf());

	Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTex;
	res.As(&depthTex);

	D3D11_TEXTURE2D_DESC desc{};
	depthTex->GetDesc(&desc);

	// コピー用テクスチャの作成・サイズ変更チェック
	const bool needCreate = !sceneDepthCopyTex || [&]() {
		D3D11_TEXTURE2D_DESC cur{};
		sceneDepthCopyTex->GetDesc(&cur);
		return cur.Width != desc.Width || cur.Height != desc.Height;
		}();

	if (needCreate)
	{
		D3D11_TEXTURE2D_DESC copyDesc = desc;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		// 深度バッファが特殊なフォーマット(Typeless)の場合、SRVで読める形式に合わせる
		// 一般的な D32_FLOAT や D24_S8 の場合、R32_FLOAT や R24_UNORM_X8 として読み出す必要がある
		DXGI_FORMAT srvFormat = desc.Format;
		if (desc.Format == DXGI_FORMAT_R32_TYPELESS || desc.Format == DXGI_FORMAT_D32_FLOAT) {
			copyDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvFormat = DXGI_FORMAT_R32_FLOAT;
		}
		else if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
			copyDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
			srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		}

		sceneDepthCopyTex.Reset();
		sceneDepthCopySRV.Reset();

		HRESULT hr = DeviceManager::instance()->getDevice()->CreateTexture2D(&copyDesc, nullptr, sceneDepthCopyTex.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = srvFormat;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		hr = DeviceManager::instance()->getDevice()->CreateShaderResourceView(sceneDepthCopyTex.Get(), &srvDesc, sceneDepthCopySRV.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	}

	// マルチサンプリング(MSAA)なら Resolve、そうでなければ Copy
	if (desc.SampleDesc.Count > 1)
	{
		dc->ResolveSubresource(sceneDepthCopyTex.Get(), 0, depthTex.Get(), 0, desc.Format);
	}
	else
	{
		dc->CopyResource(sceneDepthCopyTex.Get(), depthTex.Get());
	}
}

void GameScene::debugGui()
{

	ImGui::Begin("Imgui");

	// 現在のカメラ位置を表示
	{
		Camera* cam = Camera::instance();
		if (cam)
		{
			const DirectX::XMFLOAT3* eye = cam->getEye();
			if (eye)
			{
				ImGui::Text("Camera Position: X=%.2f Y=%.2f Z=%.2f", eye->x, eye->y, eye->z);
			}
			else
			{
				ImGui::Text("Camera Position: (null)");
			}
		}
		else
		{
			ImGui::Text("Camera: (null)");
		}
	}

	

	if (ImGui::TreeNode("Performance"))
	{
		ImGui::Text("FPS: %.1f", fps);
		ImGui::PlotLines("##FPS", fpsHistory, GRAPH_HISTORY_COUNT, 0, nullptr, 0.0f, 144.0f, ImVec2(0, 40));

		ImGui::Text("Process CPU: %.1f %%", cpuUsage);
		ImGui::PlotLines("##CPU", cpuHistory, GRAPH_HISTORY_COUNT, 0, nullptr, 0.0f, 100.0f, ImVec2(0, 40));

		ImGui::Text("GPU scene time: %.2f ms", gpuFrameTimeMs);
		ImGui::SameLine();
		ImGui::TextDisabled("(60 FPS budget: 16.67 ms)");
		const float gpuGraphMax = (gpuFrameTimeMs > 33.33f) ? gpuFrameTimeMs * 1.25f : 33.33f;
		ImGui::PlotLines("##GPUTime", gpuHistory, GRAPH_HISTORY_COUNT, 0, nullptr, 0.0f, gpuGraphMax, ImVec2(0, 40));
		ImGui::Text("GPU frame occupancy: %.1f %% (estimate)", gpuUsage);
		ImGui::TextDisabled("Timestamp ratio for this scene; not Windows GPU utilization");

		ImGui::Separator();
		ImGui::TextUnformatted("GPU pass breakdown");
		static const char* GPU_PASS_NAMES[GPU_PASS_COUNT] = {
			"Shadows",
			"Atmosphere + IBL",
			"Scene + GBuffer",
			"SSR",
			"Caustics",
			"Water",
			"Depth of Field",
			"Debug + Rain",
			"Final Composite"
		};

		float measuredPassSumMs = 0.0f;
		for (uint32_t passIndex = 0; passIndex < GPU_PASS_COUNT; ++passIndex)
		{
			const float passMs = gpuPassTimeMs[passIndex];
			measuredPassSumMs += passMs;
			const float share = (gpuFrameTimeMs > 0.001f)
				? std::clamp(passMs / gpuFrameTimeMs, 0.0f, 1.0f)
				: 0.0f;

			char overlay[96] = {};
			sprintf_s(overlay, "%s  %.2f ms  (%.0f%%)",
				GPU_PASS_NAMES[passIndex], passMs, share * 100.0f);
			ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f), overlay);
		}

		const float unaccountedMs = (gpuFrameTimeMs > measuredPassSumMs)
			? gpuFrameTimeMs - measuredPassSumMs
			: 0.0f;
		ImGui::TextDisabled("Pass sum: %.2f ms, setup/other: %.2f ms",
			measuredPassSumMs, unaccountedMs);

		ImGui::TreePop();
	}





	if (ImGui::TreeNode("IBL"))
	{
		ImGui::DragFloat("Diffuse IBL", &iblDiffuseIntensity, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Specular IBL", &iblSpecularIntensity, 0.01f, 0.0f, 2.0f);

		ImGui::DragFloat("Global Roughness", &globalRoughnessScale, 0.01f, 0.0f, 3.0f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Atmospheric Scattering"))
	{

		ImGui::Checkbox("Day/Night Cycle", &isDayNightCycleEnabled);
		ImGui::SameLine();
		ImGui::TextDisabled(isDayNightCycleEnabled ? "Running" : "Paused");
		ImGui::DragFloat("Cycle Duration (sec)", &dayNightCycleDurationSeconds, 0.1f, 1.0f, 60.0f);


		ImGui::ColorEdit3("AmbientColor", &AmbientColor.x);


		ImGui::Separator();
		ImGui::Text("Atmosphere Blur");
		ImGui::Checkbox("Low Resolution Atmosphere", &enableLowResAtmosphere);
		ImGui::TextDisabled("ON = 640x360 atmosphere pass (recommended for performance)");
		ImGui::TextDisabled("Blur texel size: %.6f, %.6f",
			atmoBlurInvRes.x,
			atmoBlurInvRes.y);


		if (skyMap)
		{
			// The sky owns the sun direction. LightDirection may represent the sun
			// by day or the opposite moon direction by night, so copying it back to
			// the sky made a paused night alternate between sun and moon each frame.
			// GameScene::update derives the correct primary light on the next frame.
			const bool skyChanged = skyMap->debugGui(nullptr);
			if (skyChanged && !isDayNightCycleEnabled)
			{
				// Manual edits while paused become the new fixed position.
				pausedSunDirection = skyMap->atmosphere_constants_data.sunDirection;
				dayNightPhaseRadians = atan2f(pausedSunDirection.x, pausedSunDirection.y);
				if (dayNightPhaseRadians < 0.0f)
					dayNightPhaseRadians += 2.0f * DirectX::XM_PI;
			}
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Volumetric Cloud"))
	{
		if (volumetricCloud)
		{
			ImGui::Checkbox("Enable Volumetric Cloud", &enableVolumetricCloud);



			volumetricCloud->debugGui();

		}
		ImGui::TreePop();
	}


	if (ImGui::TreeNode("Water"))
	{
		if (water_simulation)
		{	
			ImGui::DragFloat("Caustics Tiling", &causticsScale, 0.1f, 1.0f, 120.0f);
			ImGui::DragFloat("Caustics Intensity", &causticsIntensity, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Caustics Speed", &causticsSpeed, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Caustics Wobble", &causticsWobble, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Caustics Softness", &causticsPower, 0.01f, 0.35f, 2.0f);


			ImGui::Checkbox("Auto Ripple", &enableAutoRipple);
			ImGui::DragFloat("Auto Ripple Interval (sec)", &autoRippleInterval, 0.1f, 0.1f, 10.0f);
			ImGui::Checkbox("Ship Interaction Ripples", &enableShipInteractionRipples);
			ImGui::DragFloat("Ship Ripple Interval (sec)", &shipRippleInterval, 0.05f, 0.1f, 3.0f);
			water_simulation->debugGui();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Screen Space Reflection"))
	{

		ImGui::Checkbox("Use SSR", &useSSR);

		if (ssr)
		{
			ssr->debugGui();
		}

		ImGui::TreePop();
	}

	if (depthOfField)
	{
		ID3D11ShaderResourceView* depthSRV = (gbuffer) ? gbuffer->get_depth_srv() : DeviceManager::instance()->getDepthShaderResourceView();
		depthOfField->debugGui(&depthOfFieldParams, &useDoF, depthSRV);
	}

	if (ImGui::TreeNode("Cascade Shadow Map Debug"))
	{

		if (cascadeShadowMap)
		{
			ImGui::Text("Light Direction: (%.3f, %.3f, %.3f)",
				LightDirection.x, LightDirection.y, LightDirection.z);

			auto& constants = cascadeShadowMap->getConstants();
			ImGui::DragFloat("Shadow Bias", &constants.cascade_shadow_bias, 0.00001f, 0.0f, 0.01f, "%.6f");
			ImGui::DragFloat("Shadow Attenuation", &constants.cascade_shadow_attenuation, 0.01f, 0.0f, 1.0f);

			bool displayCascadeArea = static_cast<bool>(constants.display_cascade_area);
			if (ImGui::Checkbox("Display Cascade Area", &displayCascadeArea))
			{
				constants.display_cascade_area = static_cast<BOOL>(displayCascadeArea);
			}
		}
		ImGui::TreePop();
	}



	ImGui::End();
}
