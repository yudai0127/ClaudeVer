#pragma once

using GamePadButton = unsigned int;

// ゲームパッド
class GamePad
{
public:
	static const GamePadButton BTN_UP				= (1 <<  0);
	static const GamePadButton BTN_RIGHT			= (1 <<  1);
	static const GamePadButton BTN_DOWN				= (1 <<  2);
	static const GamePadButton BTN_LEFT				= (1 <<  3);
	static const GamePadButton BTN_A				= (1 <<  4);
	static const GamePadButton BTN_B				= (1 <<  5);
	static const GamePadButton BTN_X				= (1 <<  6);
	static const GamePadButton BTN_Y				= (1 <<  7);
	static const GamePadButton BTN_START			= (1 <<  8);
	static const GamePadButton BTN_BACK				= (1 <<  9);
	static const GamePadButton BTN_LEFT_THUMB		= (1 << 10);
	static const GamePadButton BTN_RIGHT_THUMB		= (1 << 11);
	static const GamePadButton BTN_LEFT_SHOULDER	= (1 << 12);
	static const GamePadButton BTN_RIGHT_SHOULDER	= (1 << 13);
	static const GamePadButton BTN_LEFT_TRIGGER		= (1 << 14);
	static const GamePadButton BTN_RIGHT_TRIGGER	= (1 << 15);

public:
	GamePad() {}
	~GamePad() {}

	// 更新
	void update();

	// スロット設定
	void setSlot(int slot) { this->slot = slot; }

	// ボタン入力状態の取得
	GamePadButton getButton() const { return buttonState[0]; }

	// ボタン押下状態の取得
	GamePadButton getButtonDown() const { return buttonDown; }

	// ボタン押上状態の取得
	GamePadButton getButtonUp() const { return buttonUp; }

	// 左スティックX軸入力状態の取得
	float getAxisLX() const { return axisLx; }

	// 左スティックY軸入力状態の取得
	float getAxisLY() const { return axisLy; }

	// 右スティックX軸入力状態の取得
	float getAxisRX() const { return axisRx; }

	// 右スティックY軸入力状態の取得
	float getAxisRY() const { return axisRy; }

	// 左トリガー入力状態の取得
	float getTriggerL() const { return triggerL; }

	// 右トリガー入力状態の取得
	float getTriggerR() const { return triggerR; }

private:
	GamePadButton		buttonState[2] = { 0 };
	GamePadButton		buttonDown = 0;
	GamePadButton		buttonUp = 0;
	float				axisLx = 0.0f;
	float				axisLy = 0.0f;
	float				axisRx = 0.0f;
	float				axisRy = 0.0f;
	float				triggerL = 0.0f;
	float				triggerR = 0.0f;
	int					slot = 0;
};
