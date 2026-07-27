#pragma once

#include <windows.h>
#include "high_resolution_timer.h"
#include "Audio/AudioManager.h"
#include "Graphics/Debug/ImGuiRenderer.h"
#include "Input/InputManager.h"

#include "Graphics/DeviceManager/DeviceManager.h"



class Framework
{
public:
	Framework(HWND hwnd);
	~Framework();

private:
	void update(float elapsedTime/*Elapsed seconds from last frame*/);
	void render(float elapsedTime/*Elapsed seconds from last frame*/);

	void calculateFrameStats();

public:
	int run();
	LRESULT CALLBACK handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	
	void stylize_window(bool fullscreen);
private:
	const HWND				hwnd;
	
	bool        fullscreen_mode = false;
	RECT        windowed_rect = {};
	LONG_PTR    windowed_style = 0;

	high_resolution_timer	timer;
	unsigned int frames = 0 ;
	float elapsed_time = 0.0f;

	DeviceManager* deviceMgr;
	InputManager* inputMgr;
	AudioManager* audioMgr;

	std::unique_ptr<ImGuiRenderer>	imguiRenderer;

	const int						syncInterval = 1;		// êÇíºìØä˙ä‘äuê›íË
};