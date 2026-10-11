#pragma once

#include <Windows.h>

#include <cstdint>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl.h>

namespace Homura {

class WinApp;

struct MouseMove {
	LONG x;
	LONG y;
	LONG wheel;
};

/// <summary>
/// キーボードとマウスの入力管理
/// </summary>
class Input {
public:
	~Input();

	// 入力デバイスを初期化する
	bool Initialize(WinApp* winApp);
	// 入力デバイスを解放する
	void Finalize();
	// 前フレームの状態を保存して入力を更新する
	void Update();

	// キーが押されているかを返す
	bool IsPressKey(uint8_t keyNumber) const;
	// キーが離されているかを返す
	bool IsReleaseKey(uint8_t keyNumber) const;
	// キーが押された瞬間かを返す
	bool IsTriggerKey(uint8_t keyNumber) const;
	// キーが離された瞬間かを返す
	bool IsReleaseTriggerKey(uint8_t keyNumber) const;
	// マウスボタンが押されているかを返す
	bool IsPressMouse(uint8_t buttonNumber) const;
	// マウスボタンが押された瞬間かを返す
	bool IsTriggerMouse(uint8_t buttonNumber) const;
	// マウスの相対移動量とホイールの移動量を返す
	MouseMove GetMouseMove() const;

private:
	bool IsKeyDown(const BYTE* keyState, uint8_t keyNumber) const;
	bool IsMouseButtonDown(const DIMOUSESTATE2& mouseState, uint8_t buttonNumber) const;

private:
	// 入力先のウィンドウを借用する
	WinApp* winApp_ = nullptr;
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;
	BYTE key_[256]{};
	// トリガー判定に使用する前フレームのキー状態
	BYTE previousKey_[256]{};
	DIMOUSESTATE2 mouseState_{};
	// トリガー判定に使用する前フレームのマウス状態
	DIMOUSESTATE2 previousMouseState_{};
	bool initialized_ = false;
};

} // namespace Homura
