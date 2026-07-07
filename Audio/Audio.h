#pragma once

#include <Windows.h>
#include <xaudio2.h>
#include <wrl.h>

#include <string>
#include <vector>

class Audio {
public:
	struct SoundData {
		WAVEFORMATEX wfex{};
		std::vector<BYTE> buffer;
	};

public:
	~Audio();

	bool Initialize();
	void Finalize();
	void Update();

	SoundData LoadWave(const std::string& filePath) const;
	bool PlayWave(const SoundData& soundData);

private:
	void DestroyFinishedVoices();
	void DestroyAllVoices();

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masterVoice_ = nullptr;
	std::vector<IXAudio2SourceVoice*> sourceVoices_;
	bool initialized_ = false;
};
