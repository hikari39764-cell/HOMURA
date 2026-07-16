#include "WinApp.h"
#include "WinConfig.h"

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
		return false;
	}
	isClassRegistered_ = true;

	// クライアント領域のサイズを元に実際のウィンドウサイズを計算
	wrc_ = { 0, 0, WinConfig::kClientWidth, WinConfig::kClientHeight };
	if (AdjustWindowRect(&wrc_, WinConfig::kWindowStyle, false) == FALSE) {
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
		wrc_.right - wrc_.left,
		wrc_.bottom - wrc_.top,
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

	return true;
}

void WinApp::Finalize() {
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
