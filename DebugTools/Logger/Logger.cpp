#include "Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <format>

namespace Homura {

namespace {

	std::ofstream gLogStream;

	std::string ConvertWideToUtf8(const std::wstring& str) {
		if (str.empty()) {
			return std::string();
		}

		int sizeNeeded = WideCharToMultiByte(
			CP_UTF8,
			0,
			str.c_str(),
			-1,
			nullptr,
			0,
			nullptr,
			nullptr
		);

		if (sizeNeeded == 0) {
			return std::string();
		}

		std::string result(sizeNeeded, 0);

		WideCharToMultiByte(
			CP_UTF8,
			0,
			str.c_str(),
			-1,
			result.data(),
			sizeNeeded,
			nullptr,
			nullptr
		);

		if (!result.empty() && result.back() == '\0') {
			result.pop_back();
		}

		return result;
	}

	std::string MakeLogFilePath() {
		// 現在時刻を取得
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};
		localtime_s(&localTime, &nowTime);

		const std::string fileName = std::format(
			"{:04}{:02}{:02}_{:02}{:02}{:02}.log",
			localTime.tm_year + 1900,
			localTime.tm_mon + 1,
			localTime.tm_mday,
			localTime.tm_hour,
			localTime.tm_min,
			localTime.tm_sec
		);

		return std::format("logs/{}", fileName);
	}

} // namespace

void InitializeLogger() {
	if (gLogStream.is_open()) {
		return;
	}
	// 初期化時に一度だけ作る
	std::filesystem::create_directories("logs");

	const std::string logFilePath = MakeLogFilePath();
	gLogStream.open(logFilePath, std::ios::out);

	OutputDebugStringA("Logger Initialize\n");

	if (gLogStream.is_open()) {
		gLogStream << "Logger Initialize\n";
	}
}

void FinalizeLogger() {
	OutputDebugStringA("Logger Finalize\n");

	if (gLogStream.is_open()) {
		gLogStream << "Logger Finalize\n";
		gLogStream.close();
	}
}

void Log(const std::string& message) {
	// ファイルへ出力
	std::string outputMessage = message;
	if (outputMessage.empty() || outputMessage.back() != '\n') {
		outputMessage += '\n';
	}

	// 出力ウィンドウへ出力
	if (gLogStream.is_open()) {
		gLogStream << outputMessage;
	}

	OutputDebugStringA(outputMessage.c_str());
}

void Log(const std::wstring& message) {
	// ファイルへ出力
	std::wstring outputMessage = message;
	if (outputMessage.empty() || outputMessage.back() != L'\n') {
		outputMessage += L'\n';
	}

	// 出力ウィンドウへ出力
	if (gLogStream.is_open()) {
		gLogStream << ConvertWideToUtf8(outputMessage);
	}

	OutputDebugStringW(outputMessage.c_str());
}

} // namespace Homura

