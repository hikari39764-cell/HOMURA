#pragma once
#include <Windows.h>
#include "WinConfig.h"

namespace Homura {

/// <summary>
/// ウィンドウの生成とメッセージ処理
/// </summary>
class WinApp {
public:
	static constexpr int32_t kClientWidth = WinConfig::kClientWidth;
	static constexpr int32_t kClientHeight = WinConfig::kClientHeight;

	// ウィンドウメッセージを処理する
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// COMとウィンドウを初期化する
	bool Initialize(int nCmdShow);
	// ウィンドウとCOMを終了する
	void Finalize();
	// メッセージを処理し、終了要求があればtrueを返す
	bool ProcessMessage();

	// ウィンドウハンドルを返す
	HWND GetHwnd() const { return hwnd_; }
	// インスタンスハンドルを返す
	HINSTANCE GetHInstance() const { return wc_.hInstance; }

private:
	bool isComInitialized_ = false;
	bool isClassRegistered_ = false;
	WNDCLASS wc_{};
	HWND hwnd_ = nullptr;
};

} // namespace Homura
