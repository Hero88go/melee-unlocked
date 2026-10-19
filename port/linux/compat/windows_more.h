// Linux build: the rest of the Win32 surface the shared files use (included by windows.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

typedef int16_t SHORT;
typedef SHORT* PSHORT;
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_NO_DATA 232L
#define ERROR_IO_PENDING 997L
#define THREAD_MODE_BACKGROUND_BEGIN 0x00010000
#define THREAD_MODE_BACKGROUND_END 0x00020000
#define HANDLE_FLAG_INHERIT 1u
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
inline time_t _mkgmtime64(struct tm* t) { return timegm(t); }
inline time_t _mkgmtime(struct tm* t) { return timegm(t); }
BOOL CreatePipe(HANDLE* read_end, HANDLE* write_end, SECURITY_ATTRIBUTES* sa, DWORD size);
BOOL SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags);
HANDLE GetStdHandle(DWORD which);
BOOL PeekNamedPipe(HANDLE pipe, LPVOID buffer, DWORD size, LPDWORD read, LPDWORD available, LPDWORD left);
BOOL CancelIoEx(HANDLE file, LPVOID overlapped);
BOOL CancelIo(HANDLE file);

// The clipboard, through SDL (text, as the shared code uses it).
typedef void* HGLOBAL;
#define GMEM_MOVEABLE 0x0002u
#define GMEM_ZEROINIT 0x0040u
#define CF_TEXT 1u
#define CF_UNICODETEXT 13u
BOOL OpenClipboard(HWND owner);
BOOL EmptyClipboard();
BOOL CloseClipboard();
HANDLE SetClipboardData(UINT format, HANDLE data);
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL mem);
BOOL GlobalUnlock(HGLOBAL mem);
HGLOBAL GlobalFree(HGLOBAL mem);

// std::filesystem::path::c_str() is char on Linux: the wide file functions also take narrow paths.
BOOL DeleteFileW(const char* path);
BOOL MoveFileExW(const char* from, const char* to, DWORD flags);
BOOL CopyFileW(const char* from, const char* to, BOOL fail_if_exists);
BOOL CopyFileExW(const char* from, const char* to, LPPROGRESS_ROUTINE progress, LPVOID data, BOOL* cancel, DWORD flags);
BOOL CreateDirectoryW(const char* path, SECURITY_ATTRIBUTES* sa);
BOOL GetFileAttributesExW(const char* path, GET_FILEEX_INFO_LEVELS level, LPVOID out);
DWORD GetFileAttributesW(const char* path);
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* sa, DWORD disposition, DWORD flags, HANDLE t);
FILE* _wfopen(const char* path, const wchar_t* mode);
int _wremove(const char* path);
int _wrename(const char* from, const char* to);
HINSTANCE ShellExecuteW(HWND window, LPCWSTR verb, const char* file, LPCWSTR params, LPCWSTR directory, int show);

#include "windows_more2.h"
