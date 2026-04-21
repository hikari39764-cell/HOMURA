#pragma once
#include <string>

void InitializeLogger();
void FinalizeLogger();
void Log(const std::string& message);