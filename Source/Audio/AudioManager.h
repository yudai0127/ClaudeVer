#pragma once

#include <xaudio2.h>
#include "Audio.h"

// オーディオ管理
class AudioManager
{
private:
	AudioManager() {}
	~AudioManager();

public:
	static AudioManager* instance()
	{
		static AudioManager inst;
		return &inst;
	}

	AudioManager* initialize();

	/// <summary>
	/// 読み込み
	/// </summary>
	/// <param name="filename">読み込むファイル名</param>
	/// <returns>作成したオーディオ</returns>
	std::unique_ptr<Audio> loadAudioSource(const char* filename);

private:
	IXAudio2*				xaudio = nullptr;
	IXAudio2MasteringVoice* masteringVoice = nullptr;
};
