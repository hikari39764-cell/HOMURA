#include <Windows.h>

#include "WinApp.h"
#include "Logger.h"
#include "DxCommon.h"
#include "CrashHandler.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	InitializeCrashHandler();
	InitializeLogger();

	WinApp winApp;
	if (!winApp.Initialize(showCmd)) {
		FinalizeLogger();
		return -1;
	}

	DXCommon dxCommon;
	if (!dxCommon.Initialize(winApp.GetHwnd())) {
		winApp.Finalize();
		FinalizeLogger();
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

	return 0;
}