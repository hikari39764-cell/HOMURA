#pragma once
#include <string>

void InitializeLogger();
void FinalizeLogger();
void Log(const std::string& message);
void Log(const std::wstring& message);