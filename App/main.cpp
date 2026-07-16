#include <Windows.h>

#include <cassert>

#include "WinApp/WinApp.h"
#include "DebugTools/Logger/Logger.h"
#include "Renderer/FrameRenderer.h"
#include "DebugTools/CrashHandler/CrashHandler.h"
#include "DebugTools/CrashHandler/D3DResourceLeakChecker.h"
#include "Audio/Audio.h"
#include "Input/Input.h"

#pragma comment(lib, "ole32.lib")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int showCmd) {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return -1;
	}

	using namespace Homura;

	InitializeCrashHandler();
	InitializeLogger();

	int result = 0;

	{
		// DirectX関連のComPtrが解放された後にリークチェックを走らせるため、先に作って最後に破棄させる
		D3DResourceLeakChecker leakChecker;

		WinApp winApp;
		FrameRenderer frameRenderer;
		Audio audio;
		Input input;

		if (!winApp.Initialize(showCmd)) {
			result = -1;
		} else if (!frameRenderer.Initialize(winApp.GetHwnd())) {
			result = -1;
		} else if (!audio.Initialize()) {
			result = -1;
		} else if (!input.Initialize(hInstance, winApp.GetHwnd())) {
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

					// キーボード入力を毎フレーム更新する
					input.Update();

					// 入力確認用。0キーを押した瞬間だけ出力する
					if (input.IsTriggerKey(DIK_0)) {
						OutputDebugStringA("Ohhhhhhhhh\n");
					}

					// デバッグ用にEscapeキーでアプリを終了できるようにする
					if (input.IsTriggerKey(DIK_ESCAPE)) {
						break;
					}

					// 入力をもとにデバッグカメラなどの更新を行う
					frameRenderer.Update(input);

					// 再生が終わったSourceVoiceを毎フレーム片付ける
					audio.Update();

					// 毎フレームBackBufferを指定色でクリアして画面に表示する
					frameRenderer.Draw();
				}
			}
		}

		input.Finalize();
		audio.Finalize();
		frameRenderer.Finalize();
		winApp.Finalize();
	}

	FinalizeLogger();
	CoUninitialize();

	return result;
}
