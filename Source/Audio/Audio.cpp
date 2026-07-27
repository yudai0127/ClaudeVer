#include "../misc.h"
#include "Audio.h"

// コンストラクタ
Audio::Audio(IXAudio2* xaudio, std::shared_ptr<AudioResource>& resource)
	: resource(resource)
{
	HRESULT hr;

	// ソースボイスを生成
	hr = xaudio->CreateSourceVoice(&sourceVoice, &resource->getWaveFormat());
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
}

// デストラクタ
Audio::~Audio()
{
	if (sourceVoice != nullptr)
	{
		sourceVoice->DestroyVoice();
		sourceVoice = nullptr;
	}
}

// 再生
void Audio::play(bool loop)
{
	stop();

	// ソースボイスにデータを送信
	XAUDIO2_BUFFER buffer = { 0 };
	buffer.AudioBytes = resource->getAudioBytes();
	buffer.pAudioData = resource->getAudioData();
	buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	
	sourceVoice->SubmitSourceBuffer(&buffer);

	HRESULT hr = sourceVoice->Start();
	_ASSERT_EXPR(SUCCEEDED(hr), hrTrace(hr));
	sourceVoice->SetVolume(1.0f);
}

// 停止
void Audio::stop()
{
	sourceVoice->FlushSourceBuffers();
	sourceVoice->Stop();
}
