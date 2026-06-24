#include "Input.h"

#include <cassert>
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

	std::memset(key_, 0, sizeof(key_));
	std::memset(previousKey_, 0, sizeof(previousKey_));

	initialized_ = true;
	Log("Complete initialize Input!!!\n");
	return true;
}

void Input::Finalize() {
	if (keyboard_ != nullptr) {
		keyboard_->Unacquire();
	}

	keyboard_.Reset();
	directInput_.Reset();

	std::memset(key_, 0, sizeof(key_));
	std::memset(previousKey_, 0, sizeof(previousKey_));

	initialized_ = false;
}

void Input::Update() {
	if (!initialized_ || keyboard_ == nullptr) {
		return;
	}

	// 前フレームの入力状態を退避する
	std::memcpy(previousKey_, key_, sizeof(key_));

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

bool Input::IsKeyDown(const BYTE* keyState, uint8_t keyNumber) const {
	return (keyState[keyNumber] & 0x80) != 0;
}
