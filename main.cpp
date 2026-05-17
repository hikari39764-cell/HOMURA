#include <Windows.h>

#include <cassert>

#include "WinApp.h"
#include "Logger.h"
#include "DxCommon.h"
#include "CrashHandler.h"

#pragma comment(lib, "ole32.lib")

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return -1;
	}

	InitializeCrashHandler();
	InitializeLogger();

	WinApp winApp;
	if (!winApp.Initialize(showCmd)) {
		FinalizeLogger();
		CoUninitialize();
		return -1;
	}

	DXCommon dxCommon;
	if (!dxCommon.Initialize(winApp.GetHwnd())) {
		winApp.Finalize();
		FinalizeLogger();
		CoUninitialize();
		return -1;
	}

	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}

		// 毎フレームBackBufferを指定色でクリアして画面に表示する
		dxCommon.Draw();
	}

	dxCommon.Finalize();
	winApp.Finalize();
	FinalizeLogger();
	CoUninitialize();

	return 0;
}
