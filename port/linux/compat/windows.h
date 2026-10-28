// Linux build of the shared host: the part of the Win32 API the portable host files use, backed by
// POSIX (port/linux/compat/compat.cpp). Never on the include path of a Windows build. The window,
// renderer, audio device and controller code have real Linux implementations of their own instead.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef _WIN32
#error "port/linux/compat is for the Linux build only"
#endif
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <cerrno>
#include <climits>
#include <unistd.h>
#include <x86intrin.h>

#define WINAPI
#define CALLBACK
#define APIENTRY
#define __stdcall
#define __cdecl
#define __declspec(x)
#define __forceinline inline __attribute__((always_inline))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int32_t BOOL;
typedef uint8_t BYTE;
typedef uint8_t BOOLEAN;
typedef uint16_t WORD;
typedef uint16_t USHORT;
typedef uint8_t UCHAR;
typedef UCHAR* PUCHAR;
typedef unsigned int UINT;
typedef int INT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t DWORD64;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t UINT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef size_t SIZE_T;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef DWORD* LPDWORD;
typedef long HRESULT;
typedef void* FARPROC;
typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct _RECT { LONG left, top, right, bottom; } RECT;
typedef struct _POINT { LONG x, y; } POINT;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_SHARING_VIOLATION 32L
#define STILL_ACTIVE 259u
#define CP_UTF8 65001u
#define CP_ACP 0u
#define MB_ERR_INVALID_CHARS 8u
#define MOVEFILE_REPLACE_EXISTING 1u
#define MOVEFILE_COPY_ALLOWED 2u
#define MOVEFILE_WRITE_THROUGH 8u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define FILE_SHARE_DELETE 4u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_RELEASE 0x8000u
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_LOWEST (-2)
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define MB_OK 0u
#define MB_ICONERROR 0x10u
#define MB_ICONWARNING 0x30u
#define MB_ICONINFORMATION 0x40u
#define MB_YESNO 4u
#define MB_OKCANCEL 1u
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define CREATE_NO_WINDOW 0x08000000u
#define DETACHED_PROCESS 0x00000008u
#define CREATE_NEW_PROCESS_GROUP 0x00000200u
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xFFFF))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xFFFF))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0L)
#define E_FAIL ((HRESULT)0x80004005L)

typedef struct _SYSTEM_INFO { DWORD dwPageSize; LPVOID lpMinimumApplicationAddress, lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask; DWORD dwNumberOfProcessors, dwProcessorType, dwAllocationGranularity;
  WORD wProcessorLevel, wProcessorRevision; } SYSTEM_INFO;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime,
  ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
typedef struct _STARTUPINFOW { DWORD cb; LPWSTR lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize,
  dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2; BYTE* lpReserved2;
  HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOW;
typedef STARTUPINFOW STARTUPINFOA;
typedef STARTUPINFOW STARTUPINFO;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;
#define STARTF_USESHOWWINDOW 1u
#define STARTF_USESTDHANDLES 0x100u
typedef struct _CRITICAL_SECTION { void* impl; } CRITICAL_SECTION;
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES;

// ---- time ----
void Sleep(DWORD ms);
DWORD GetTickCount();
ULONGLONG GetTickCount64();
BOOL QueryPerformanceCounter(LARGE_INTEGER* out);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* out);
inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : errno; }
inline int gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : errno; }

// ---- process and threads ----
DWORD GetCurrentProcessId();
HANDLE GetCurrentProcess();
HANDLE GetCurrentThread();
BOOL SetThreadPriority(HANDLE thread, int priority);
DWORD GetLastError();
void SetLastError(DWORD error);
BOOL CloseHandle(HANDLE handle);
DWORD WaitForSingleObject(HANDLE handle, DWORD ms);
BOOL GetExitCodeProcess(HANDLE process, LPDWORD code);
BOOL TerminateProcess(HANDLE process, UINT code);
// A path argument given as a wide string, a narrow one (std::filesystem::path::c_str() is char here)
// or nullptr, so one signature serves every call.
struct LinuxPathArg {
  const wchar_t* wide = nullptr; const char* narrow = nullptr;
  LinuxPathArg(const wchar_t* w) : wide(w) {}
  LinuxPathArg(const char* n) : narrow(n) {}
  LinuxPathArg(std::nullptr_t) {}
  bool empty() const { return !wide && !narrow; }
};
BOOL CreateProcessW(LinuxPathArg application, LPWSTR command_line, SECURITY_ATTRIBUTES*, SECURITY_ATTRIBUTES*,
                    BOOL inherit, DWORD flags, LPVOID environment, LinuxPathArg directory, STARTUPINFOW*,
                    PROCESS_INFORMATION* info);
BOOL CreateProcessA(LPCSTR application, LPSTR command_line, SECURITY_ATTRIBUTES*, SECURITY_ATTRIBUTES*,
                    BOOL inherit, DWORD flags, LPVOID environment, LPCSTR directory, STARTUPINFOA*,
                    PROCESS_INFORMATION* info);
#define CreateProcess CreateProcessW
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
LPWSTR GetCommandLineW();
void InitializeCriticalSection(CRITICAL_SECTION* cs);
void DeleteCriticalSection(CRITICAL_SECTION* cs);
void EnterCriticalSection(CRITICAL_SECTION* cs);
void LeaveCriticalSection(CRITICAL_SECTION* cs);
inline LONG InterlockedIncrement(volatile LONG* v) { return __atomic_add_fetch(v, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedDecrement(volatile LONG* v) { return __atomic_sub_fetch(v, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedExchange(volatile LONG* v, LONG x) { return __atomic_exchange_n(v, x, __ATOMIC_SEQ_CST); }
inline LONG InterlockedCompareExchange(volatile LONG* v, LONG x, LONG cmp) {
  __atomic_compare_exchange_n(v, &cmp, x, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return cmp; }
inline void YieldProcessor() { _mm_pause(); }
inline void MemoryBarrier() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

// ---- modules ----
DWORD GetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size);
DWORD GetModuleFileNameA(HMODULE module, LPSTR out, DWORD size);
HMODULE GetModuleHandleW(LPCWSTR name);
HMODULE GetModuleHandleA(LPCSTR name);
HMODULE LoadLibraryA(LPCSTR name);
HMODULE LoadLibraryW(LPCWSTR name);
FARPROC GetProcAddress(HMODULE module, LPCSTR name);
BOOL FreeLibrary(HMODULE module);

// ---- memory ----
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect);
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD type);
BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, DWORD* old_protect);
BOOL FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size);
void GetSystemInfo(SYSTEM_INFO* info);
void GetCurrentThreadStackLimits(ULONG_PTR* low, ULONG_PTR* high);

// ---- text ----
int MultiByteToWideChar(UINT code_page, DWORD flags, LPCSTR in, int in_bytes, LPWSTR out, int out_chars);
int WideCharToMultiByte(UINT code_page, DWORD flags, LPCWSTR in, int in_chars, LPSTR out, int out_bytes,
                        LPCSTR default_char, BOOL* used_default);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
int _wremove(const wchar_t* path);
int _wrename(const wchar_t* from, const wchar_t* to);
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define sprintf_s snprintf
#define _getpid getpid
#define _fseeki64 fseeko
#define _ftelli64 ftello
#ifndef MU_BYTESWAP_DEFINED
#define MU_BYTESWAP_DEFINED
inline uint16_t _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t _byteswap_ulong(uint32_t v) { return __builtin_bswap32(v); }
inline uint64_t _byteswap_uint64(uint64_t v) { return __builtin_bswap64(v); }
#endif
#include <strings.h>
#include <cwctype>

// ---- files ----
BOOL DeleteFileW(LPCWSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists);
typedef DWORD (*LPPROGRESS_ROUTINE)(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
                                    HANDLE, HANDLE, LPVOID);
BOOL CopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, BOOL* cancel, DWORD flags);
BOOL CreateDirectoryA(LPCSTR path, SECURITY_ATTRIBUTES*);
BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES*);
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out);
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out);
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetFileAttributesA(LPCSTR path);
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES*, DWORD disposition, DWORD flags,
                   HANDLE);
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES*, DWORD disposition, DWORD flags,
                   HANDLE);
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD read, LPVOID overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD bytes, LPDWORD written, LPVOID overlapped);
BOOL FlushFileBuffers(HANDLE file);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size);
UINT GetSystemDirectoryW(LPWSTR out, UINT size);
DWORD GetTempPathW(DWORD size, LPWSTR out);

// ---- desktop ----
// The desktop's handlers: xdg-open for a folder or link, zenity/kdialog for a message box.
HINSTANCE ShellExecuteW(HWND, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR directory, int show);
HINSTANCE ShellExecuteA(HWND, LPCSTR verb, LPCSTR file, LPCSTR params, LPCSTR directory, int show);
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type);
int MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type);
HWND GetActiveWindow();
HWND GetForegroundWindow();
DWORD GetWindowThreadProcessId(HWND window, LPDWORD process);
short GetAsyncKeyState(int key);
void OutputDebugStringA(LPCSTR text);
void OutputDebugStringW(LPCWSTR text);

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_NUMPAD0 0x60
#define VK_INSERT 0x2D
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_SNAPSHOT 0x2C
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_MULTIPLY 0x6A
#define VK_DIVIDE 0x6F
#define VK_DECIMAL 0x6E

// The keyboard state the SDL window keeps (port/linux/window_sdl.cpp), in Windows virtual-key codes, so
// GetAsyncKeyState answers for the game window as on Windows.
namespace linux_compat { void set_key_state(int vk, bool down); void set_window_focused(bool focused); bool window_focused(); void set_game_window(HWND window); }

// ---- memory status, process lists (from /proc) ----
typedef struct _MEMORYSTATUSEX { DWORD dwLength, dwMemoryLoad; ULONGLONG ullTotalPhys, ullAvailPhys, ullTotalPageFile,
  ullAvailPageFile, ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual; } MEMORYSTATUSEX;
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* status);
BOOL SwitchToThread();
#define _TRUNCATE ((size_t)-1)
// The bounded printf family: always terminated, truncated to the buffer.
#define _snwprintf_s(buffer, count, ...) swprintf((buffer), sizeof(buffer) / sizeof((buffer)[0]), __VA_ARGS__)
#define _snprintf_s(buffer, count, ...) snprintf((buffer), sizeof(buffer), __VA_ARGS__)
#define swprintf_s(buffer, ...) swprintf((buffer), sizeof(buffer) / sizeof((buffer)[0]), __VA_ARGS__)
#define WC_ERR_INVALID_CHARS 0x80u
inline int strncpy_s(char* dst, size_t size, const char* src, size_t count) {
  if (!dst || !size) return 22; size_t n = std::strlen(src); if (count != _TRUNCATE && count < n) n = count;
  if (n >= size) n = size - 1; std::memcpy(dst, src, n); dst[n] = 0; return 0; }
inline int wcsncpy_s(wchar_t* dst, size_t size, const wchar_t* src, size_t count) {
  if (!dst || !size) return 22; size_t n = std::wcslen(src); if (count != _TRUNCATE && count < n) n = count;
  if (n >= size) n = size - 1; std::wmemcpy(dst, src, n); dst[n] = 0; return 0; }
inline int strcpy_s(char* dst, size_t size, const char* src) { return strncpy_s(dst, size, src, _TRUNCATE); }
inline int wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src) { return wcsncpy_s(dst, size, src, _TRUNCATE); }
template <size_t N> inline int strcpy_s(char (&dst)[N], const char* src) { return strncpy_s(dst, N, src, _TRUNCATE); }
template <size_t N> inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcsncpy_s(dst, N, src, _TRUNCATE); }

#include "windows_more.h"
