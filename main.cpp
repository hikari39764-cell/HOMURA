#include <Windows.h>
#include "WinApp.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int showCmd) {

	WinApp winApp;
	if (!winApp.Initialize(showCmd)) {
		return -1;
	}

	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}

	}

	winApp.Finalize();

	return 0;
}