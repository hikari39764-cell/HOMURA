#include "Audio.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>

#include "DebugTools/Logger/Logger.h"

#pragma comment(lib, "xaudio2.lib")

namespace {

	struct ChunkHeader {
		char id[4];
		uint32_t size;
	};

	struct RiffHeader {
		ChunkHeader chunk;
		char type[4];
	};

	bool IsChunk(const char* id, const char* expected) {
		return std::strncmp(id, expected, 4) == 0;
	}

	bool ReadChunkHeader(std::ifstream& file, ChunkHeader& chunk) {
		file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
		return file.good();
	}

	void SkipBytes(std::ifstream& file, uint32_t size) {
		file.seekg(static_cast<std::streamoff>(size), std::ios_base::cur);
	}

	void SkipChunk(std::ifstream& file, uint32_t size) {
		// RIFFのChunkは2Byte境界に揃えられるので、奇数サイズならPaddingも飛ばす
		const std::streamoff skipSize = static_cast<std::streamoff>(size + (size & 1));
		file.seekg(skipSize, std::ios_base::cur);
	}

} // namespace

Audio::~Audio() {
	Finalize();
}

bool Audio::Initialize() {
	if (initialized_) {
		return true;
	}

	// XAudio2エンジンを生成する
	HRESULT hr = XAudio2Create(xAudio2_.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log("Failed to create XAudio2.\n");
		return false;
	}

	// 全ての音声が最後に通るMasterVoiceを生成する
	hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		xAudio2_.Reset();
		Log("Failed to create MasteringVoice.\n");
		return false;
	}

	initialized_ = true;
	Log("Complete initialize Audio!!!\n");
	return true;
}

void Audio::Finalize() {
	DestroyAllVoices();

	if (masterVoice_ != nullptr) {
		// MasterVoiceはReleaseではなくDestroyVoiceで破棄する
		masterVoice_->DestroyVoice();
		masterVoice_ = nullptr;
	}

	xAudio2_.Reset();
	initialized_ = false;
}

void Audio::Update() {
	DestroyFinishedVoices();
}

Audio::SoundData Audio::LoadWave(const std::string& filePath) const {
	SoundData soundData{};

	// .wavファイルをバイナリモードで開く
	std::ifstream file(filePath, std::ios_base::binary);
	assert(file.is_open());

	if (!file.is_open()) {
		Log(std::format("Failed to open sound file, path:{}\n", filePath));
		return soundData;
	}

	// RIFFヘッダを読み込んでWAVEファイルか確認する
	RiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

	if (!file.good() || !IsChunk(riff.chunk.id, "RIFF") || !IsChunk(riff.type, "WAVE")) {
		assert(false);
		Log(std::format("Invalid wave file, path:{}\n", filePath));
		return soundData;
	}

	bool foundFormat = false;
	bool foundData = false;

	while (file.good() && (!foundFormat || !foundData)) {
		ChunkHeader chunk{};
		if (!ReadChunkHeader(file, chunk)) {
			break;
		}

		if (IsChunk(chunk.id, "fmt ")) {
			// fmt Chunkから波形フォーマットを読み込む
			const uint32_t readSize =
				std::min<uint32_t>(chunk.size, static_cast<uint32_t>(sizeof(soundData.wfex)));
			file.read(reinterpret_cast<char*>(&soundData.wfex), readSize);

			if (!file.good()) {
				assert(false);
				Log(std::format("Failed to read sound format, path:{}\n", filePath));
				return {};
			}

			if (chunk.size > readSize) {
				SkipBytes(file, chunk.size - readSize);
			}
			if ((chunk.size & 1) != 0) {
				SkipBytes(file, 1);
			}

			foundFormat = true;
		} else if (IsChunk(chunk.id, "data")) {
			if (!foundFormat) {
				assert(false);
				Log(std::format("Wave data appeared before format, path:{}\n", filePath));
				return {};
			}

			// data Chunkの波形データを読み込む
			soundData.buffer.resize(chunk.size);
			file.read(
				reinterpret_cast<char*>(soundData.buffer.data()),
				static_cast<std::streamsize>(soundData.buffer.size())
			);

			if (!file.good()) {
				assert(false);
				Log(std::format("Failed to read sound data, path:{}\n", filePath));
				return {};
			}

			foundData = true;
		} else {
			// JUNKなど今回利用しないChunkは読み飛ばす
			SkipChunk(file, chunk.size);
		}
	}

	assert(foundFormat && foundData);

	if (!foundFormat || !foundData) {
		Log(std::format("Required wave chunk was not found, path:{}\n", filePath));
		return {};
	}

	Log(std::format("Complete load sound, path:{}!!!\n", filePath));
	return soundData;
}

bool Audio::PlayWave(const SoundData& soundData) {
	assert(initialized_);
	assert(xAudio2_.Get() != nullptr);
	assert(!soundData.buffer.empty());

	if (!initialized_ || xAudio2_.Get() == nullptr || soundData.buffer.empty()) {
		return false;
	}

	if (soundData.buffer.size() > UINT32_MAX) {
		assert(false);
		Log("Sound data is too large.\n");
		return false;
	}

	IXAudio2SourceVoice* sourceVoice = nullptr;

	// SoundDataのフォーマットに合わせたSourceVoiceを生成する
	HRESULT hr = xAudio2_->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log("Failed to create SourceVoice.\n");
		return false;
	}

	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = soundData.buffer.data();
	buffer.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	// SourceVoiceへ波形データを渡して再生を開始する
	hr = sourceVoice->SubmitSourceBuffer(&buffer);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		sourceVoice->DestroyVoice();
		Log("Failed to submit source buffer.\n");
		return false;
	}

	hr = sourceVoice->Start();
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		sourceVoice->DestroyVoice();
		Log("Failed to start SourceVoice.\n");
		return false;
	}

	sourceVoices_.push_back(sourceVoice);
	Log("Start sound playback!!!\n");
	return true;
}

void Audio::DestroyFinishedVoices() {
	for (auto it = sourceVoices_.begin(); it != sourceVoices_.end();) {
		IXAudio2SourceVoice* sourceVoice = *it;
		XAUDIO2_VOICE_STATE state{};
		sourceVoice->GetState(&state);

		if (state.BuffersQueued == 0) {
			// 再生が終わったSourceVoiceだけを破棄する
			sourceVoice->DestroyVoice();
			it = sourceVoices_.erase(it);
		} else {
			++it;
		}
	}
}

void Audio::DestroyAllVoices() {
	for (IXAudio2SourceVoice* sourceVoice : sourceVoices_) {
		if (sourceVoice != nullptr) {
			sourceVoice->DestroyVoice();
		}
	}

	sourceVoices_.clear();
}
