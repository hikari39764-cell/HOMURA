#include <Windows.h>
#include "WinApp.h"
#include "Logger.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {
	InitializeLogger();
	Log("Program Start");

	WinApp winApp;
	if (!winApp.Initialize(showCmd)) {
		Log("WinApp Initialize Failed");
		FinalizeLogger();
		return -1;
	}

	Log("WinApp Initialize Success");

	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}

	}

	Log("Program End");

	winApp.Finalize();
	FinalizeLogger();

	return 0;
}