#include <Windows.h>

#include <cassert>

#include "WinApp.h"
#include "Logger.h"
#include "DxCommon.h"
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"

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
		if (!winApp.Initialize(showCmd)) {
			result = -1;
		} else {
			DXCommon dxCommon;
			if (!dxCommon.Initialize(winApp.GetHwnd())) {
				dxCommon.Finalize();
				winApp.Finalize();
				result = -1;
			} else {
				while (true) {
					if (winApp.ProcessMessage()) {
						break;
					}

					// 毎フレームBackBufferを指定色でクリアして画面に表示する
					dxCommon.Draw();
				}

				dxCommon.Finalize();
				winApp.Finalize();
			}
		}
	}

	FinalizeLogger();
	CoUninitialize();

	return result;
}
