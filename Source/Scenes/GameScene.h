#pragma once

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl.h>
#include "high_resolution_timer.h"
#include "Scene.h"
#include "Camera/CameraController.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/GPUConstantBuffer.h"
#include "Graphics/Sky/Skymap.h"
#include "Graphics/framebuffer/framebuffer.h"
#include "Graphics/fullscreen_quad/fullscreen_quad.h"
#include "Graphics/CascadeShadowMap/CascadeShadowMap.h"
#include "Graphics/Water/Water.h"
#include "Graphics/SSR/SSR.h"
#include "Graphics/Gbuffer/Gbuffer.h"
#include "Graphics/VolumetricCloud/VolumetricCloud.h"
#include "Graphics/Dof/Dof.h"
#include "Graphics/Rain/Rain.h"
#include "Stage/StageManager.h"
#include "Stage/StageBackground.h"
#include "Object/Ship.h"
#include "Object/ObjectManager.h"

class GameScene : public Scene
{
public:
	GameScene() {}
	~GameScene() override {}

	void initialize() override;
	void finalize() override;
	void update(float elapsedTime) override;
	void render() override;

	void setWeatherTarget(float weatherT);

public:
	std::unique_ptr<FrameBuffer> framebuffers[8];
	std::unique_ptr<Fullscreen_Quad> bit_block_transfer;
	float critical_depth_value = 0.0f;

	std::unique_ptr<RainSystem> rainSystem = nullptr;

private:
	void renderShadow(ID3D11DeviceContext* dc);
	void renderAtmosphere(ID3D11DeviceContext* dc, ID3D11RenderTargetView* hdrRTV, ID3D11DepthStencilView* dsv, const DirectX::XMFLOAT4X4& viewProj);
	void debugGui();
	void updateFreeCamera(float elapsedTime);
	void updatePerformanceMetrics(float elapsedTime);
	void beginGpuQuery(ID3D11DeviceContext* dc);
	void endGpuQuery(ID3D11DeviceContext* dc);  
	void updateIBLMaps(ID3D11DeviceContext* dc, const DirectX::XMFLOAT3& cameraPos);
	void injectRippleFromCursor();
	void injectAutoRipple(float elapsedTime);
	void injectShipInteractionRipples(float elapsedTime);
	void copySceneColor(ID3D11DeviceContext* dc, ID3D11RenderTargetView* rtv);
	void copySceneDepth(ID3D11DeviceContext* dc);

private:
	static inline float Lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

private:
	struct LIGHT_CONSTANT
	{
		DirectX::XMFLOAT4 ambientColor;
		DirectX::XMFLOAT4 lightDirection;
		DirectX::XMFLOAT4 lightColor;
		DirectX::XMFLOAT4 iblParams;
	};

	struct SpecularConstants
	{
		float roughness;
		float padding[3];
		unsigned int numSamples;
		float padding2[3];
	};

	struct MaterialRoughnessConstants
	{
		float global_roughness_scale;
		float padding[3];
	};

	struct ATMOSPHERE_BLUR_CB
	{
		DirectX::XMFLOAT2 gInvHalfRes;
		DirectX::XMFLOAT2 _pad;
	};
	high_resolution_timer tictoc;

	struct CausticsCB
	{
		DirectX::XMFLOAT4X4 gInvViewProjection;
		DirectX::XMFLOAT4 params; // x=scale, y=wobble, z=power, w=intensity
		DirectX::XMFLOAT2 invScreenSize;
		DirectX::XMFLOAT2 _pad;
		float time;
		float waterPlaneY;
		DirectX::XMFLOAT2 _padTime;
		DirectX::XMFLOAT4 lightDirection; 
	};

	

private:
	std::unique_ptr<GPUConstantBuffer> buffer;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> finalPassPS;

	std::unique_ptr<GPUConstantBuffer> lightBuffer;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> diffuse_iem_srv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> specular_pmrem_srv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lut_ggx_srv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lut_charrlie_srv;

	Microsoft::WRL::ComPtr<ID3D11ComputeShader> irradiance_cs;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> specular_filter_cs;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> diffuse_iem_texture;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> diffuse_iem_uav;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> specular_pmrem_texture;
	std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> specular_pmrem_uav_mips;


	MaterialRoughnessConstants roughnessCB{};
	std::unique_ptr<GPUConstantBuffer> specular_cb;
	std::unique_ptr<GPUConstantBuffer> material_roughness_cb;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneColorCopyTex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sceneColorCopySRV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ssrColorSRV;

	Microsoft::WRL::ComPtr<ID3D11Query> queryDisjoint;
	Microsoft::WRL::ComPtr<ID3D11Query> queryBeginFrame;
	Microsoft::WRL::ComPtr<ID3D11Query> queryEndFrame;

	ATMOSPHERE_BLUR_CB blurCB{};
	Microsoft::WRL::ComPtr<ID3D11PixelShader> atmoBlurHPS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> atmoBlurVPS;
	std::unique_ptr<GPUConstantBuffer> atmoBlurCB;

private:
	std::unique_ptr<CameraController> cameraCtrl = nullptr;
	std::unique_ptr<SkyMap> skyMap = nullptr;
	std::unique_ptr<VolumetricCloud> volumetricCloud = nullptr;
	std::unique_ptr<StageBackground> stageBackground = nullptr;
	std::vector<std::unique_ptr<Ship>> ships;
	


	std::unique_ptr<CascadeShadowMap> cascadeShadowMap;
	std::unique_ptr<Water_Simulation> water_simulation;
	std::unique_ptr<Gbuffer> gbuffer;
	std::unique_ptr<ScreenSpaceReflection> ssr;
	std::unique_ptr<DepthOfField> depthOfField;

	std::unique_ptr<FrameBuffer> atmoLowResBuffer;
	std::unique_ptr<FrameBuffer> atmoBlurTempBuffer;
	std::unique_ptr<FrameBuffer> atmoBlurBuffer;
	std::unique_ptr<FrameBuffer> hdrSceneBuffer = nullptr;

private:
	DirectX::XMFLOAT4 AmbientColor = { 0.1f, 0.1f, 0.09f, 0.0f };
	DirectX::XMFLOAT4 LightDirection = { 0.3f, -0.7f, 0.3f, 0.0f };
	DirectX::XMFLOAT4 LightColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	DirectX::XMFLOAT3 freeCameraTarget = { 0.0f, 10.0f, 0.0f };
	DirectX::XMFLOAT2 freeCameraAngle = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 atmoBlurInvRes = { 0.001302f, 0.001340f };

private:
	float iblDiffuseIntensity = 0.1f;
	float iblSpecularIntensity = 0.4f;
	float iblSheenIntensity = 0.0f;
	float directSheenIntensity = 0.0f;
	float globalRoughnessScale = 1.0f;

	float elapsedTime = 0.0f;
	float dayNightCycleDurationSeconds = 50.0f;

	float freeCameraRange = 20.0f;

	float realDt = 0.016f;
	float fpsAccum = 0.0f;
	float fps = 0.0f;
	float cpuUsage = 0.0f;
	float gpuUsage = 0.0f;
	float performanceUpdateTimer = 0.0f;

	float rippleTimer = 0.0f;
	float autoRippleInterval = 1.5f;
	float shipRippleTimer = 0.0f;
	float shipRippleInterval = 0.65f;
	unsigned int shipRipplePhase = 0;

	float targetWeatherT = 0.0f;
	float weatherBlendSpeed = 0.05f;

private:
	int fpsFrames = 0;
	int numProcessors = 0;

	UINT atmoLowResWidth = 640;
	UINT atmoLowResHeight = 360;

private:
	bool isDayNightCycleEnabled = true;
	bool useSpaceDivision = true;
	bool isFreeCameraMode = false;

	bool useCascadeShadowMap = true;
	bool useSSR = true;
	bool enableAutoRipple = false;
	bool enableShipInteractionRipples = true;
	bool queryStarted = false;

	bool enableVolumetricCloud = true;                                                                                                                                                                                  
	bool showSsrDebug = false;
	bool showSsrUvDebug = false;

	bool useDoF = true;

	bool enableLowResAtmosphere = true;

private:
	LARGE_INTEGER qpcFreq{};
	LARGE_INTEGER prevQpc{};

	HANDLE processHandle = nullptr;
	ULARGE_INTEGER lastProcessKernelTime{};
	ULARGE_INTEGER lastProcessUserTime{};
	ULARGE_INTEGER lastSystemTime{};

private:
	ScreenSpaceReflection::ParamCB ssrParams{};
	DepthOfField::Params depthOfFieldParams{};

private:
	static const int GRAPH_HISTORY_COUNT = 120;
	float fpsHistory[GRAPH_HISTORY_COUNT] = {};
	float cpuHistory[GRAPH_HISTORY_COUNT] = {};
	float gpuHistory[GRAPH_HISTORY_COUNT] = {};
private:
	int   shadowUpdateIndex = 0;           // 次に更新するカスケードのインデックス
	int   shadowUpdatesPerFrame = 4;       // 1フレームあたり更新するカスケード数
	bool  staggerShadowUpdates = true;

private:
	float causticsScale = 28.0f;
	float causticsPower = 0.85f;
	float causticsIntensity = 0.16f;
	float causticsSpeed = 0.22f;
	float causticsWobble = 0.55f;

	std::unique_ptr<FrameBuffer> causticsBuffer;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> causticsPS;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> causticsVS;
	
	CausticsCB cb{};
	std::unique_ptr<GPUConstantBuffer>causticsCB;
	bool showCausticsDebug = false;


	Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneDepthCopyTex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sceneDepthCopySRV;
};
