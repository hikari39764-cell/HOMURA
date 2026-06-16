#include <Windows.h>

#include <cassert>

#include "WinApp.h"
#include "Logger.h"
#include "DxCommon.h"
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"
#include "Audio.h"

#pragma comment(lib, "ole32.lib")

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return -1;
	}

	InitializeCrashHandler();
	InitializeLogger();

	int result = 0;

	{
		// DirectX関連のComPtrが解放された後にリークチェックを走らせるため、先に作って最後に破棄させる
		D3DResourceLeakChecker leakChecker;

		WinApp winApp;
		DXCommon dxCommon;
		Audio audio;

		if (!winApp.Initialize(showCmd)) {
			result = -1;
		} else if (!dxCommon.Initialize(winApp.GetHwnd())) {
			result = -1;
		} else if (!audio.Initialize()) {
			result = -1;
		} else {
			// 再生テスト用のwavファイルを読み込んで、アプリ起動時に一度だけ鳴らす
			Audio::SoundData fanfare = audio.LoadWave("Resources/fanfare.wav");

			if (fanfare.buffer.empty() || !audio.PlayWave(fanfare)) {
				result = -1;
			} else {
				while (true) {
					if (winApp.ProcessMessage()) {
						break;
					}

					// 再生が終わったSourceVoiceを毎フレーム片付ける
					audio.Update();

					// 毎フレームBackBufferを指定色でクリアして画面に表示する
					dxCommon.Draw();
				}
			}
		}

		audio.Finalize();
		dxCommon.Finalize();
		winApp.Finalize();
	}

	FinalizeLogger();
	CoUninitialize();

	return result;
}
