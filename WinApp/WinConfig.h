#pragma once
#include <Windows.h>
#include <cstdint>

namespace Homura {
namespace WinConfig {

	constexpr int32_t kClientWidth = 1280;
	constexpr int32_t kClientHeight = 720;

	constexpr wchar_t kWindowClassName[] = L"WindowClass";
	constexpr wchar_t kWindowTitle[] = L"CG2";
	

	constexpr DWORD kWindowStyle =
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

} // namespace WinConfig
} // namespace Homura
