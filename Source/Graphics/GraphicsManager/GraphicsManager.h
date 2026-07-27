#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <functional>
#include <memory>

#include "Graphics/Debug/DebugRenderer.h"
#include "Graphics/Line/LineRenderer.h"
#include "Graphics/Buffer.h"

enum class SAMPLER_STATE
{
	WRAP_POINT,
	WRAP_LINEAR,
	WRAP_ANISOTROPIC,
	CLAMP_POINT,
	CLAMP_LINEAR,
	BORDER_WHITE,
	BORDER_BLACK,
	LINEAR_MIRROR,
};

enum class DEPTH_STENCIL_STATE
{
	ON_ON,		// 深度テストを ON 深度書き込みを ON
	ON_OFF,		// 深度テストを ON 深度書き込みを OFF
	OFF_ON,		// 深度テストを OFF 深度書き込みを ON
	OFF_OFF,	// 深度テストを OFF 深度書き込みを OFF
};

enum class BLEND_STATE
{
	NONE,
	ALPHABLENDING,
	ADD,
	MULTIPLE
};

enum class RASTERIZER_STATE
{
	SOLID_CULLBACK,
	WIRE_CULLBACK,
	SOLID_CULLNONE,
	WIRE_CULLNONE
};

// レンダーコンテキスト
struct RenderContext
{
	// サンプラーステート
	Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerStates[8];
	// 深度ステンシルステート
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStates[4];
	// ブレンドステート
	Microsoft::WRL::ComPtr<ID3D11BlendState> blendStates[4];
	// ラスタライザステート
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerStates[4];
};

class GraphicsManager
{
private:
	GraphicsManager() {}
	~GraphicsManager() {}

public:
	static GraphicsManager* instance()
	{
		static GraphicsManager inst;
		return &inst;
	}

	bool initialize(ID3D11Device* device, ID3D11DeviceContext* dc);

	Microsoft::WRL::ComPtr<ID3D11SamplerState> getSamplerState(SAMPLER_STATE state) const { return renderContext.samplerStates[static_cast<int>(state)]; }

	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> getDepthStencilStates(DEPTH_STENCIL_STATE state) const { return renderContext.depthStencilStates[static_cast<int>(state)]; }

	Microsoft::WRL::ComPtr<ID3D11BlendState> getBlendStates(BLEND_STATE state) const { return renderContext.blendStates[static_cast<int>(state)]; }

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> getRasterizerStates(RASTERIZER_STATE state) const { return renderContext.rasterizerStates[static_cast<int>(state)]; }

	void SettingRenderContext(void(*settingCallback)(ID3D11DeviceContext*, RenderContext*));

	DebugRenderer* getDebugRenderer() const { return debugRenderer.get(); }

	LineRenderer* getLineRenderer() const { return lineRenderer.get(); }

private:
	ID3D11DeviceContext* deviceContext = nullptr;
	RenderContext renderContext;
	
	std::unique_ptr<DebugRenderer> debugRenderer;
	std::unique_ptr<LineRenderer> lineRenderer;

};