#pragma once

#include <memory>
#include <d3d11.h>
#include <wrl.h>
#include <mutex>

// デバイス管理
class DeviceManager
{
private:
	DeviceManager() : screenWidth(0), screenHeight(0){}
	~DeviceManager() {}

public:
	static DeviceManager* instance()
	{
		static DeviceManager inst;
		return &inst;
	}

	// 初期化処理
	DeviceManager* initialize(HWND hwnd);

	// デバイス取得
	ID3D11Device* getDevice() const { return device.Get(); }

	// デバイスコンテキスト取得
	ID3D11DeviceContext* getDeviceContext() const { return immediateContext.Get(); }

	// スワップチェーン取得
	IDXGISwapChain* getSwapChain() const { return swapchain.Get(); }

	// レンダーターゲットビュー取得
	ID3D11RenderTargetView* getRenderTargetView() const { return renderTargetView.Get(); }

	// デプスステンシルビュー取得
	ID3D11DepthStencilView* getDepthStencilView() const { return depthStencilView.Get(); }

	ID3D11ShaderResourceView* getDepthShaderResourceView() const { return depthStencilSRV.Get(); }


	

	// スクリーン幅取得
	float getScreenWidth() const { return screenWidth; }

	// スクリーン高さ取得
	float getScreenHeight() const { return screenHeight; }

	HRESULT resize(UINT width, UINT height);
	void ensureWindowed();

	// 排他処理
	std::mutex& getMutex() { return mutex; }

private:
	float	screenWidth;
	float	screenHeight;

	Microsoft::WRL::ComPtr<ID3D11Device>			device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext>		immediateContext;
	Microsoft::WRL::ComPtr<IDXGISwapChain>			swapchain;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>	renderTargetView;
	Microsoft::WRL::ComPtr<ID3D11Texture2D>			depthStencilBuffer;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView>	depthStencilView;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthStencilSRV; 

	std::mutex mutex;
};