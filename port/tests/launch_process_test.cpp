#define NOMINMAX
#include "../app/launch_process.h"
#include <cstdio>

int wmain(int argc, wchar_t**) {
  if (argc > 1) return 42;
  wchar_t temp[MAX_PATH]{}, own[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, temp) || !GetModuleFileNameW(nullptr, own, MAX_PATH)) return 1;
  const std::wstring dir = std::wstring(temp) + L"launch test \u6e2c\u8a66 " +
      std::to_wstring(GetCurrentProcessId());
  if (!CreateDirectoryW(dir.c_str(), nullptr)) return 2;
  const std::wstring exe = dir + L"\\child test.exe";
  if (!CopyFileW(own, exe.c_str(), TRUE)) { RemoveDirectoryW(dir.c_str()); return 3; }
  PROCESS_INFORMATION process{};
  const std::wstring command = L"\"" + exe + L"\" --child";
  DWORD result = launcher::start_process(exe, command, dir, CREATE_NO_WINDOW, process);
  int failed = result != ERROR_SUCCESS;
  if (!failed) {
    if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) {
      // Only this test's own child may be terminated.
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 10000);
      failed = 1;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process.hProcess, &code) || code != 42) failed = 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  const DWORD missing = launcher::start_process(dir + L"\\missing.exe", command,
                                                dir, CREATE_NO_WINDOW, process);
  if (missing != ERROR_FILE_NOT_FOUND) failed = 1;
  const DWORD bad_dir = launcher::start_process(exe, command, dir + L"\\missing",
                                                CREATE_NO_WINDOW, process);
  if (bad_dir != ERROR_DIRECTORY && bad_dir != ERROR_PATH_NOT_FOUND) failed = 1;
  if (!DeleteFileW(exe.c_str()) || !RemoveDirectoryW(dir.c_str())) failed = 1;
  std::printf("Unicode and spaces launch=%lu; missing executable=%lu; missing directory=%lu\n",
              result, missing, bad_dir);
  return failed;
}
