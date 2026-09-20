#pragma once
#include <windows.h>
#include <string>

namespace launcher {
// Windows mutates the command buffer. Capture its error before doing any logging.
inline DWORD start_process(const std::wstring& exe, std::wstring command,
                           const std::wstring& cwd, DWORD flags,
                           PROCESS_INFORMATION& process) {
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  process = {};
  if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                     flags, nullptr, cwd.c_str(), &startup, &process)) return ERROR_SUCCESS;
  return GetLastError();
}
}
