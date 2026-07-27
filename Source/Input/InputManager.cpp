#include "InputManager.h"

InputManager* InputManager::initialize(HWND hwnd)
{
	gamePad = std::make_unique<GamePad>();
	mouse = std::make_unique<Mouse>(hwnd);

	return this;
}

// XVˆ—
void InputManager::update()
{
	gamePad->update();
	mouse->update();
}
