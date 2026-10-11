#include <memory>

#include "WinApp/WinApp.h"
#include "DebugTools/Logger/Logger.h"
#include "Renderer/FrameRenderer.h"
#include "DebugTools/CrashHandler/CrashHandler.h"
#include "DebugTools/CrashHandler/D3DResourceLeakChecker.h"
#include "Audio/Audio.h"
#include "Input/Input.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	using namespace Homura;

	InitializeCrashHandler();
	InitializeLogger();

	int result = 0;

	{
		// DirectX関連のComPtrが解放された後にリークチェックを走らせるため、先に作って最後に破棄させる
		D3DResourceLeakChecker leakChecker;

		// ウィンドウは入力や描画から借りて使い、最後に解放する
		auto winApp = std::make_unique<WinApp>();
		FrameRenderer frameRenderer;
		Audio audio;
		// 入力クラスを動的に生成し、スコープ終了時に解放する
		auto input = std::make_unique<Input>();

		if (!winApp->Initialize(showCmd)) {
			result = -1;
		} else if (!frameRenderer.Initialize(winApp->GetHwnd())) {
			result = -1;
		} else if (!audio.Initialize()) {
			result = -1;
		} else if (!input->Initialize(winApp.get())) {
			result = -1;
		} else {
			// 再生テスト用のwavファイルを読み込んで、アプリ起動時に一度だけ鳴らす
			Audio::SoundData fanfare = audio.LoadWave("Resources/fanfare.wav");

			if (fanfare.buffer.empty() || !audio.PlayWave(fanfare)) {
				result = -1;
			} else {
				while (true) {
					if (winApp->ProcessMessage()) {
						break;
					}

					// キーボード入力を毎フレーム更新する
					input->Update();

					// 入力確認用。0キーを押した瞬間だけ出力する
					if (input->IsTriggerKey(DIK_0)) {
						OutputDebugStringA("Ohhhhhhhhh\n");
					}

					// デバッグ用にEscapeキーでアプリを終了できるようにする
					if (input->IsTriggerKey(DIK_ESCAPE)) {
						break;
					}

					// 入力をもとにデバッグカメラなどの更新を行う
					frameRenderer.Update(*input);

					// 再生が終わったSourceVoiceを毎フレーム片付ける
					audio.Update();

					// 毎フレームBackBufferを指定色でクリアして画面に表示する
					frameRenderer.Draw();
				}
			}
		}

		input->Finalize();
		audio.Finalize();
		frameRenderer.Finalize();
		winApp->Finalize();
	}

	FinalizeLogger();

	return result;
}
