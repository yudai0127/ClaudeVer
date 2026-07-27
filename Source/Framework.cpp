#include <memory>
#include <sstream>
#include <d3d11.h>

#include "Framework.h"
#include "Scenes/SceneManager.h"
#include "Scenes/GameScene.h"
#include "Scenes/LoadingScene.h"
#include "Graphics/GraphicsManager/GraphicsManager.h"

#include "Input/Mouse.h"

// コンストラクタ
Framework::Framework(HWND hwnd) : hwnd(hwnd)
{
	windowed_style = GetWindowLongPtrA(hwnd, GWL_STYLE);
	GetWindowRect(hwnd, &windowed_rect);

	// デバイス管理の初期化
	deviceMgr = DeviceManager::instance()->initialize(hwnd);

	// 入力管理の初期化
	inputMgr = InputManager::instance()->initialize(hwnd);

	// オーディオ管理の初期化
	audioMgr = AudioManager::instance()->initialize();

	// グラフィックス管理の初期化
	GraphicsManager::instance()->initialize(deviceMgr->getDevice(), deviceMgr->getDeviceContext());

	// imgui の作成
	imguiRenderer = std::make_unique<ImGuiRenderer>(hwnd, deviceMgr->getDevice());


	stylize_window(true);

	// シーンの生成と初期化
	SceneManager::instance()->changeScene(new LoadingScene(new GameScene()));
}

void Framework::stylize_window(bool fullscreen)
{
	fullscreen_mode = fullscreen;

	if (fullscreen)
	{
		GetWindowRect(hwnd, &windowed_rect);

		SetWindowLongPtrA(
			hwnd,
			GWL_STYLE,
			WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

		RECT fullscreen_window_rect{};
		HRESULT hr{ E_FAIL };

		if (auto* swap_chain = deviceMgr->getSwapChain())
		{
			Microsoft::WRL::ComPtr<IDXGIOutput> out;
			hr = swap_chain->GetContainingOutput(&out);
			if (hr == S_OK)
			{
				DXGI_OUTPUT_DESC desc{};
				if (out->GetDesc(&desc) == S_OK)
					fullscreen_window_rect = desc.DesktopCoordinates;
			}
		}
		if (hr != S_OK)
		{
			DEVMODE dev{};
			dev.dmSize = sizeof(DEVMODE);
			EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dev);
			fullscreen_window_rect = {
				dev.dmPosition.x,
				dev.dmPosition.y,
				dev.dmPosition.x + static_cast<LONG>(dev.dmPelsWidth),
				dev.dmPosition.y + static_cast<LONG>(dev.dmPelsHeight)
			};
		}

		const int winW = fullscreen_window_rect.right - fullscreen_window_rect.left;
		const int winH = fullscreen_window_rect.bottom - fullscreen_window_rect.top;

		SetWindowPos(
			hwnd,
			NULL,
			fullscreen_window_rect.left,
			fullscreen_window_rect.top,
			winW,
			winH,
			SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);

		ShowWindow(hwnd, SW_MAXIMIZE);

		// クライアントサイズ取得してリサイズ
		RECT rc{};
		GetClientRect(hwnd, &rc);
		deviceMgr->resize(rc.right - rc.left, rc.bottom - rc.top);
		if (inputMgr && inputMgr->getMouse())
		{
			inputMgr->getMouse()->setScreenWidth(rc.right - rc.left);
			inputMgr->getMouse()->setScreenHeight(rc.bottom - rc.top);
		}
	}
	else
	{
		SetWindowLongPtrA(hwnd, GWL_STYLE, windowed_style);

		const int winW = windowed_rect.right - windowed_rect.left;
		const int winH = windowed_rect.bottom - windowed_rect.top;

		SetWindowPos(
			hwnd,
			HWND_NOTOPMOST,
			windowed_rect.left,
			windowed_rect.top,
			winW,
			winH,
			SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);

		ShowWindow(hwnd, SW_NORMAL);

		// 復帰後のクライアントサイズでリサイズ
		RECT rc{};
		GetClientRect(hwnd, &rc);
		if (rc.right > 0 && rc.bottom > 0)
		{
			deviceMgr->resize(rc.right - rc.left, rc.bottom - rc.top);
			if (inputMgr && inputMgr->getMouse())
			{
				inputMgr->getMouse()->setScreenWidth(rc.right - rc.left);
				inputMgr->getMouse()->setScreenHeight(rc.bottom - rc.top);
			}
		}
	}
}

// デストラクタ
Framework::~Framework()
{
	// シーン終了処理
	SceneManager::instance()->clear();
}

// 更新処理
void Framework::update(float elapsedTime/*Elapsed seconds from last frame*/)
{
	// 入力更新処理
	inputMgr->update();

	// シーン更新処理
	SceneManager::instance()->update(elapsedTime);
}

// 描画処理
void Framework::render(float elapsedTime/*Elapsed seconds from last frame*/)
{
	// 排他処理
	std::lock_guard<std::mutex> lock(DeviceManager::instance()->getMutex());

	ID3D11DeviceContext* dc = deviceMgr->getDeviceContext();

	// IMGUIフレーム開始処理
	imguiRenderer->newFrame();

	// シーン描画処理
	SceneManager::instance()->render();

	
	// IMGUI描画
	imguiRenderer->render(dc);

	// バックバッファに描画した画を画面に表示する。
	deviceMgr->getSwapChain()->Present(syncInterval, 0);
}

// フレームレート計算
void Framework::calculateFrameStats()
{
	if (++frames, (timer.timeStamp() - elapsed_time) >= 1.0f)
	{
		float fps = static_cast<float>(frames);
		std::ostringstream outs;
		outs.precision(6);
		outs << " : FPS : " << fps << " / " << "Frame Time : " << 1000.0f / fps << " (ms)";
		SetWindowTextA(hwnd, outs.str().c_str());

		frames = 0;
		elapsed_time += 1.0f;
	}
}


// アプリケーションループ
int Framework::run()
{


	MSG msg = {};
	const float targetFrameTime = 1.0f / 60.0f; // 60fps = 約16.67ms
	float accumulatedTime = 0.0f;

	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			timer.tick(); // タイマー更新
			accumulatedTime += timer.timeInterval();

			// フレーム間待機
			while (accumulatedTime < targetFrameTime)
			{
				Sleep(1); // 待機して CPU 負荷を下げる
				timer.tick();
				accumulatedTime += timer.timeInterval();
			}

			// フレームごとの更新・描画処理
			calculateFrameStats();
			update(targetFrameTime);
			render(targetFrameTime);

			accumulatedTime -= targetFrameTime; // 次のフレームに向けて残り時間を計算
		}
	}
	return static_cast<int>(msg.wParam);
}

// メッセージハンドラ
LRESULT CALLBACK Framework::handleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (imguiRenderer->handleMessage(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_MOUSEWHEEL:
	{
		
		int wheel = GET_WHEEL_DELTA_WPARAM(wParam);
		
		if (inputMgr)
		{
			inputMgr->getMouse()->setWheel(wheel);
		}
		return 0;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc;
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_SIZE:
	{
		// 最小化はスキップ
		if (wParam != SIZE_MINIMIZED)
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);
			if (w > 0 && h > 0)
			{
				DeviceManager::instance()->resize(w, h);
				if (inputMgr && inputMgr->getMouse())
				{
					inputMgr->getMouse()->setScreenWidth(static_cast<int>(w));
					inputMgr->getMouse()->setScreenHeight(static_cast<int>(h));
				}
			}
		}
		return 0;
	}

	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
		{
			stylize_window(!fullscreen_mode);
			return 0;
		}
		break;

	case WM_KEYDOWN:
		if (wParam == VK_F11)
		{
			stylize_window(!fullscreen_mode);
			return 0;
		}
		if (wParam == VK_ESCAPE) PostMessage(hWnd, WM_CLOSE, 0, 0);
		break;

	case WM_DESTROY:
		// 終了前に確実にウィンドウ化
		DeviceManager::instance()->ensureWindowed();
		PostQuitMessage(0);
		break;

	case WM_ENTERSIZEMOVE:
		timer.stop();
		break;
	case WM_EXITSIZEMOVE:
		timer.start();
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
