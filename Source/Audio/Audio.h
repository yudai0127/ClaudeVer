#pragma once

#include <memory>
#include <xaudio2.h>
#include "AudioResource.h"

// オーディオ
class Audio
{
public:
	Audio(IXAudio2* xaudio, std::shared_ptr<AudioResource>& resource);
	~Audio();

	/// <summary>
	/// オーディオの再生
	/// </summary>
	/// <param name="loop">true...ループ</param>
	void play(bool loop);

	/// <summary>
	/// オーディオの停止
	/// </summary>
	void stop();

private:
	IXAudio2SourceVoice*			sourceVoice = nullptr;
	std::shared_ptr<AudioResource>	resource;
};
