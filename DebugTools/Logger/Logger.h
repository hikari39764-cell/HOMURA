#pragma once
#include <string>

namespace Homura {

void InitializeLogger();
void FinalizeLogger();
void Log(const std::string& message);
void Log(const std::wstring& message);

} // namespace Homura
