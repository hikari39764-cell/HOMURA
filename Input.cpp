#include "Input.h"

#include <cassert>
#include <cstddef>
#include <cstring>

#include "Logger.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

Input::~Input() {
	Finalize();
}

bool Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
	if (initialized_) {
		return true;
	}

	// DirectInputの本体を生成する
	HRESULT hr = DirectInput8Create(
		hInstance,
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(directInput_.GetAddressOf()),
		nullptr
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Log("Failed to create DirectInput.\n");
		return false;
	}

	// キーボードデバイスを生成する
	hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		directInput_.Reset();
		Log("Failed to create keyboard device.\n");
		return false;
	}

	// キーボード入力の標準形式を設定する
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Finalize();
		Log("Failed to set keyboard data format.\n");
		return false;
	}

	// 画面が手前にある時だけ、他のアプリと共有しながら入力を受け取る
	hr = keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Finalize();
		Log("Failed to set keyboard cooperative level.\n");
		return false;
	}

	// マウスデバイスを生成する
	hr = directInput_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Finalize();
		Log("Failed to create mouse device.\n");
		return false;
	}

	// マウス入力の標準形式を設定する
	hr = mouse_->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Finalize();
		Log("Failed to set mouse data format.\n");
		return false;
	}

	// 画面が手前にある時だけ、他のアプリと共有しながらマウス入力を受け取る
	hr = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		Finalize();
		Log("Failed to set mouse cooperative level.\n");
		return false;
	}

	std::memset(key_, 0, sizeof(key_));
	std::memset(previousKey_, 0, sizeof(previousKey_));
	std::memset(&mouseState_, 0, sizeof(mouseState_));
	std::memset(&previousMouseState_, 0, sizeof(previousMouseState_));

	initialized_ = true;
	Log("Complete initialize Input!!!\n");
	return true;
}

void Input::Finalize() {
	if (keyboard_ != nullptr) {
		keyboard_->Unacquire();
	}

	if (mouse_ != nullptr) {
		mouse_->Unacquire();
	}

	keyboard_.Reset();
	mouse_.Reset();
	directInput_.Reset();

	std::memset(key_, 0, sizeof(key_));
	std::memset(previousKey_, 0, sizeof(previousKey_));
	std::memset(&mouseState_, 0, sizeof(mouseState_));
	std::memset(&previousMouseState_, 0, sizeof(previousMouseState_));

	initialized_ = false;
}

void Input::Update() {
	if (!initialized_ || keyboard_ == nullptr || mouse_ == nullptr) {
		return;
	}

	// 前フレームの入力状態を退避する
	std::memcpy(previousKey_, key_, sizeof(key_));
	previousMouseState_ = mouseState_;

	HRESULT hr = keyboard_->Acquire();

	if (FAILED(hr)) {
		// フォーカスが外れている間は入力なしとして扱う
		std::memset(key_, 0, sizeof(key_));
		return;
	}

	// キーの入力状態を取得する
	hr = keyboard_->GetDeviceState(static_cast<DWORD>(sizeof(key_)), key_);

	if (FAILED(hr)) {
		std::memset(key_, 0, sizeof(key_));
	}

	hr = mouse_->Acquire();

	if (FAILED(hr)) {
		// フォーカスが外れている間は入力なしとして扱う
		std::memset(&mouseState_, 0, sizeof(mouseState_));
		return;
	}

	// マウスの相対移動量とボタン状態を取得する
	hr = mouse_->GetDeviceState(static_cast<DWORD>(sizeof(mouseState_)), &mouseState_);

	if (FAILED(hr)) {
		std::memset(&mouseState_, 0, sizeof(mouseState_));
	}
}

bool Input::IsPressKey(uint8_t keyNumber) const {
	return IsKeyDown(key_, keyNumber);
}

bool Input::IsReleaseKey(uint8_t keyNumber) const {
	return !IsKeyDown(key_, keyNumber);
}

bool Input::IsTriggerKey(uint8_t keyNumber) const {
	return IsKeyDown(key_, keyNumber) && !IsKeyDown(previousKey_, keyNumber);
}

bool Input::IsReleaseTriggerKey(uint8_t keyNumber) const {
	return !IsKeyDown(key_, keyNumber) && IsKeyDown(previousKey_, keyNumber);
}

bool Input::IsPressMouse(uint8_t buttonNumber) const {
	return IsMouseButtonDown(mouseState_, buttonNumber);
}

bool Input::IsTriggerMouse(uint8_t buttonNumber) const {
	return IsMouseButtonDown(mouseState_, buttonNumber) &&
		!IsMouseButtonDown(previousMouseState_, buttonNumber);
}

MouseMove Input::GetMouseMove() const {
	return {
		mouseState_.lX,
		mouseState_.lY,
		mouseState_.lZ
	};
}

bool Input::IsKeyDown(const BYTE* keyState, uint8_t keyNumber) const {
	return (keyState[keyNumber] & 0x80) != 0;
}

bool Input::IsMouseButtonDown(const DIMOUSESTATE2& mouseState, uint8_t buttonNumber) const {
	size_t index = buttonNumber;

	if (index >= _countof(mouseState.rgbButtons)) {
		return false;
	}

	return (mouseState.rgbButtons[index] & 0x80) != 0;
}
