// Minimal filesystem-only Win32 surface for scanner tests on non-Windows hosts.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <unistd.h>
using DWORD = uint32_t;
using ULONG = uint32_t;
using PUCHAR = unsigned char*;
using LPCWSTR = const wchar_t*;
using INT_PTR = intptr_t;
using HWND = void*;
struct FILETIME { DWORD dwLowDateTime = 0, dwHighDateTime = 0; };
struct WIN32_FILE_ATTRIBUTE_DATA { FILETIME ftCreationTime; };
constexpr int GetFileExInfoStandard = 0, SW_SHOWNORMAL = 1;
constexpr DWORD MOVEFILE_COPY_ALLOWED = 2, MOVEFILE_WRITE_THROUGH = 8, COPY_FILE_FAIL_IF_EXISTS = 1;
inline DWORD GetCurrentProcessId() { return getpid(); }
inline DWORD GetLastError() { return 1; }
inline bool GetFileAttributesExW(const char* p, int, WIN32_FILE_ATTRIBUTE_DATA* out) {
  std::error_code ec; const auto t = std::filesystem::last_write_time(p, ec).time_since_epoch().count();
  out->ftCreationTime = {DWORD(t), DWORD(uint64_t(t) >> 32)}; return !ec;
}
inline bool MoveFileExW(const char* a, const char* b, DWORD) {
  std::error_code ec; std::filesystem::rename(a, b, ec); return !ec;
}
inline bool CopyFileExW(const char* a, const char* b, void*, void*, void*, DWORD) {
  std::error_code ec; return std::filesystem::copy_file(a, b, ec);
}
inline void* ShellExecuteW(void*, const wchar_t*, const char*, void*, void*, int) { return nullptr; }
inline void* ShellExecuteW(void*, const wchar_t*, const wchar_t*, void*, void*, int) { return nullptr; }
inline HWND GetActiveWindow() { return nullptr; }
constexpr int CP_UTF8 = 65001;
inline int WideCharToMultiByte(int, int, const wchar_t*, int, char*, int, void*, void*) { return 0; }
