#pragma once
#include <Windows.h>

class WinApp {
public:
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	bool Initialize(int nCmdShow);
	void Finalize();
	bool ProcessMessage();

	HWND GetHwnd() const { return hwnd_; }

private:
	bool isClassRegistered_ = false;
	WNDCLASS wc_{};
	RECT wrc_{};
	HWND hwnd_ = nullptr;
};