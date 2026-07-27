#pragma once

#include "GamePad.h"
#include "Mouse.h"
#include <memory>

// インプット
class InputManager
{
private:
	InputManager() {}
	~InputManager() {}

public:
	// インスタンス取得
	static InputManager* instance()
	{
		static InputManager inst;
		return &inst;
	}

	InputManager* initialize(HWND hwnd);

	// 更新処理
	void update();

	// ゲームパッド取得
	GamePad* getGamePad() { return gamePad.get(); }

	// マウス取得
	Mouse* getMouse() { return mouse.get(); }

private:
	static InputManager*	inst;

	std::unique_ptr<GamePad>	gamePad;
	std::unique_ptr<Mouse>		mouse;
};
