#pragma once

#include <Windows.h>

using MouseButton = unsigned int;

// マウス
class Mouse
{
public:
	static const MouseButton BTN_LEFT = (1 << 0);
	static const MouseButton BTN_MIDDLE = (1 << 1);
	static const MouseButton BTN_RIGHT = (1 << 2);

public:
	Mouse(HWND hWnd);
	~Mouse() {}

	// 更新
	void update();

	// ボタン入力状態の取得
	MouseButton getButton() const { return buttonState[0]; }

	// ボタン押下状態の取得
	MouseButton getButtonDown() const { return buttonDown; }

	// ボタン押上状態の取得
	MouseButton getButtonUp() const { return buttonUp; }

	// ホイール値の設定
	void setWheel(int wheel) { this->wheel[0] += wheel; }

	// ホイール値の取得
	int getWheel() const { return wheel[1]; }

	// マウスカーソルX座標取得
	int getPositionX() const { return positionX[0]; }

	// マウスカーソルY座標取得
	int getPositionY() const { return positionY[0]; }

	// 前回のマウスカーソルX座標取得
	int getPrevPositionX() const { return positionX[1]; }

	// 前回のマウスカーソルY座標取得
	int getPrevPositionY() const { return positionY[1]; }

	int getDeltaX() const { return positionX[0] - positionX[1]; }
	int getDeltaY() const { return positionY[0] - positionY[1]; }


	// スクリーン幅設定
	void setScreenWidth(int width) { screenWidth = width; }

	// スクリーン高さ設定
	void setScreenHeight(int height) { screenHeight = height; }

	// スクリーン幅取得
	int getScreenWidth() const { return screenWidth; }

	// スクリーン高さ取得
	int getScreenHeight() const { return screenHeight; }

private:
	MouseButton		buttonState[2] = { 0 };
	MouseButton		buttonDown = 0;
	MouseButton		buttonUp = 0;
	int				positionX[2];
	int				positionY[2];
	int				wheel[2];
	int				screenWidth = 0;
	int				screenHeight = 0;
	HWND			hwnd = nullptr;
};
