#include "WinApp.h"
#include <objbase.h>
#include <mmsystem.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
);
#endif

namespace Homura {

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

bool WinApp::Initialize(int nCmdShow) {
	if (hwnd_ != nullptr) {
		return true;
	}

	// COMを初期化
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		MessageBox(
			nullptr,
			L"COMの初期化に失敗しました。",
			L"Error",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	isComInitialized_ = true;

	// ウィンドウクラスの設定
	wc_ = {};
	wc_.lpfnWndProc = WindowProc;
	wc_.lpszClassName = WinConfig::kWindowClassName;
	wc_.hInstance = GetModuleHandle(nullptr);
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録
	if (RegisterClass(&wc_) == 0) {
		MessageBox(
			nullptr,
			L"ウィンドウクラスの登録に失敗しました。",
			L"Error",
			MB_OK | MB_ICONERROR
		);
		Finalize();
		return false;
	}
	isClassRegistered_ = true;

	// クライアント領域のサイズを元に実際のウィンドウサイズを計算
	RECT windowRect = { 0, 0, kClientWidth, kClientHeight };
	if (AdjustWindowRect(&windowRect, WinConfig::kWindowStyle, false) == FALSE) {
		MessageBox(
			nullptr,
			L"ウィンドウサイズの調整に失敗しました。",
			L"Error",
			MB_OK | MB_ICONERROR
		);
		Finalize();
		return false;
	}

	// ウィンドウを生成
	hwnd_ = CreateWindow(
		wc_.lpszClassName,
		WinConfig::kWindowTitle,
		WinConfig::kWindowStyle,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		wc_.hInstance,
		nullptr
	);

	if (hwnd_ == nullptr) {
		MessageBox(
			nullptr,
			L"ウィンドウの生成に失敗しました。",
			L"Error",
			MB_OK | MB_ICONERROR
		);
		Finalize();
		return false;
	}

	// ウィンドウを表示
	ShowWindow(hwnd_, nCmdShow);
	UpdateWindow(hwnd_);

	// タイマーの精度を1ミリ秒に設定
	isTimerResolutionSet_ = timeBeginPeriod(1) == TIMERR_NOERROR;

	return true;
}

void WinApp::Finalize() {
	// タイマーの精度を元に戻す
	if (isTimerResolutionSet_) {
		timeEndPeriod(1);
		isTimerResolutionSet_ = false;
	}

	if (hwnd_ != nullptr) {
		if (IsWindow(hwnd_)) {
			DestroyWindow(hwnd_);
		}
		hwnd_ = nullptr;
	}

	if (isClassRegistered_) {
		UnregisterClass(wc_.lpszClassName, wc_.hInstance);
		isClassRegistered_ = false;
	}

	if (isComInitialized_) {
		CoUninitialize();
		isComInitialized_ = false;
	}
}

bool WinApp::ProcessMessage() {
	MSG msg{};

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		if (msg.message == WM_QUIT) {
			return true;
		}
	}

	return false;
}

} // namespace Homura
