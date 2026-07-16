#pragma once

#include <Windows.h>

#include <cstdint>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl.h>

namespace Homura {

struct MouseMove {
	LONG x;
	LONG y;
	LONG wheel;
};

class Input {
public:
	~Input();

	bool Initialize(HINSTANCE hInstance, HWND hwnd);
	void Finalize();
	void Update();

	bool IsPressKey(uint8_t keyNumber) const;
	bool IsReleaseKey(uint8_t keyNumber) const;
	bool IsTriggerKey(uint8_t keyNumber) const;
	bool IsReleaseTriggerKey(uint8_t keyNumber) const;
	bool IsPressMouse(uint8_t buttonNumber) const;
	bool IsTriggerMouse(uint8_t buttonNumber) const;
	MouseMove GetMouseMove() const;

private:
	bool IsKeyDown(const BYTE* keyState, uint8_t keyNumber) const;
	bool IsMouseButtonDown(const DIMOUSESTATE2& mouseState, uint8_t buttonNumber) const;

private:
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;
	BYTE key_[256]{};
	BYTE previousKey_[256]{};
	DIMOUSESTATE2 mouseState_{};
	DIMOUSESTATE2 previousMouseState_{};
	bool initialized_ = false;
};

} // namespace Homura
