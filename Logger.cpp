#include "Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

	std::ofstream gLogStream;

	std::string MakeLogFilePath() {
		// logs ディレクトリを作る
		std::filesystem::create_directory("logs");

		// 現在時刻を取得
		auto now = std::chrono::system_clock::now();
		std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};
		localtime_s(&localTime, &nowTime);

		std::ostringstream oss;
		oss << "logs/" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".log";

		return oss.str();
	}

} // namespace

void InitializeLogger() {
	if (gLogStream.is_open()) {
		return;
	}

	const std::string logFilePath = MakeLogFilePath();
	gLogStream.open(logFilePath);

	OutputDebugStringA("Logger Initialize\n");

	if (gLogStream.is_open()) {
		gLogStream << "Logger Initialize" << std::endl;
	}
}

void FinalizeLogger() {
	OutputDebugStringA("Logger Finalize\n");

	if (gLogStream.is_open()) {
		gLogStream << "Logger Finalize" << std::endl;
		gLogStream.close();
	}
}

void Log(const std::string& message) {
	// ファイルへ出力
	if (gLogStream.is_open()) {
		gLogStream << message << std::endl;
	}

	// 出力ウィンドウへ出力
	std::string outputMessage = message;
	if (outputMessage.empty() || outputMessage.back() != '\n') {
		outputMessage += '\n';
	}
	OutputDebugStringA(outputMessage.c_str());
}