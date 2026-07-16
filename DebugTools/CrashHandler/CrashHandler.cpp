#include "CrashHandler.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <strsafe.h>

#pragma comment(lib, "Dbghelp.lib")

namespace Homura {

// 誰も捕捉しなかったSEH例外を捕捉して、Dumpを出力する
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	// Dumpsディレクトリ以下に出力する
	SYSTEMTIME time;
	GetLocalTime(&time);

	CreateDirectory(L"./Dumps", nullptr);

	wchar_t filePath[MAX_PATH] = { 0 };
	StringCchPrintfW(
		filePath,
		MAX_PATH,
		L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute
	);

	HANDLE dumpFileHandle = CreateFile(
		filePath,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (dumpFileHandle != INVALID_HANDLE_VALUE) {
		DWORD processId = GetCurrentProcessId();
		DWORD threadId = GetCurrentThreadId();

		MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{};
		minidumpInformation.ThreadId = threadId;
		minidumpInformation.ExceptionPointers = exception;
		minidumpInformation.ClientPointers = TRUE;

		MiniDumpWriteDump(
			GetCurrentProcess(),
			processId,
			dumpFileHandle,
			MiniDumpNormal,
			&minidumpInformation,
			nullptr,
			nullptr
		);

		CloseHandle(dumpFileHandle);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void InitializeCrashHandler() {
	// 誰も捕捉しなかった例外を捕捉する関数を登録
	SetUnhandledExceptionFilter(ExportDump);
}

} // namespace Homura
