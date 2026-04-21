#include <Windows.h>

#include "WinApp.h"
#include "Logger.h"
#include "DXCommon.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	InitializeLogger();

	WinApp winApp;
	if (!winApp.Initialize(showCmd)) {
		FinalizeLogger();
		return -1;
	}

	DXCommon dxCommon;
	if (!dxCommon.Initialize()) {
		winApp.Finalize();
		FinalizeLogger();
		return -1;
	}

	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}
	}

	dxCommon.Finalize();
	winApp.Finalize();
	FinalizeLogger();

	return 0;
}