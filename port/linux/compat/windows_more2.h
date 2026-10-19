// Linux build: process queries, waitable timers and string helpers (included by windows_more.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

typedef uint64_t ULONG64;
typedef int64_t LONG64;
typedef intptr_t INT_PTR;
#define COPY_FILE_FAIL_IF_EXISTS 0x00000001u
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100u
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000u
#define PROCESS_TERMINATE 0x0001u
#define SYNCHRONIZE 0x00100000u
#define TIMER_ALL_ACCESS 0x1F0003u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002u

// The game's own processes by id (the updater waits for them to close): a handle whose image path is
// read from /proc and which can be waited on until the process is gone.
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR name, LPDWORD size);

// A one-shot high-resolution timer: SetWaitableTimer arms it (negative = relative, in 100 ns units),
// WaitForSingleObject sleeps until it is due with clock_nanosleep.
HANDLE CreateWaitableTimerExW(SECURITY_ATTRIBUTES* sa, LPCWSTR name, DWORD flags, DWORD access);
typedef void (*PTIMERAPCROUTINE)(LPVOID, DWORD, DWORD);
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG period, PTIMERAPCROUTINE routine, LPVOID arg, BOOL resume);
BOOL CancelWaitableTimer(HANDLE timer);

// Case-insensitive compares for both string widths: std::filesystem::path::c_str() is char here.
#undef _wcsicmp
#undef _wcsnicmp
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wcsicmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline int _wcsnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

// The thread's stack and its CPU time, for the host's diagnostics (stack peak, slow frames).
typedef struct _NT_TIB { void* ExceptionList; void* StackBase; void* StackLimit; } NT_TIB;
void* NtCurrentTeb();   // points at this thread's NT_TIB, refreshed on each call
USHORT RtlCaptureStackBackTrace(ULONG skip, ULONG count, void** frames, ULONG* hash);
// Cycles this thread has run, in TSC units like Windows (CPU time scaled by the measured TSC rate).
BOOL QueryThreadCycleTime(HANDLE thread, ULONG64* cycles);

// MSVC's CPU feature intrinsics.
#include <cpuid.h>
#undef __cpuid
inline void __cpuid(int r[4], int leaf) { __cpuid_count((unsigned)leaf, 0u, r[0], r[1], r[2], r[3]); }
inline unsigned long long linux_xgetbv(unsigned index) {
  unsigned lo, hi;
  __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(index));
  return ((unsigned long long)hi << 32) | lo;
}
#define _xgetbv linux_xgetbv
inline int _putenv_s(const char* name, const char* value) { return setenv(name, value, 1); }
#define ERROR_FILE_EXISTS 80L
#define MEM_WRITE_WATCH 0x00200000u
#define CaptureStackBackTrace RtlCaptureStackBackTrace

// Timer resolution (Linux timers are already fine-grained), keeping the display awake (SDL's
// screensaver inhibit), power throttling (not a Linux scheduler notion), environment and paths.
#define TIMERR_NOERROR 0u
inline UINT timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
inline UINT timeEndPeriod(UINT) { return TIMERR_NOERROR; }
#define ES_SYSTEM_REQUIRED 0x00000001u
#define ES_DISPLAY_REQUIRED 0x00000002u
#define ES_CONTINUOUS 0x80000000u
DWORD SetThreadExecutionState(DWORD flags);
typedef struct _PROCESS_POWER_THROTTLING_STATE { ULONG Version, ControlMask, StateMask; } PROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
enum { ProcessPowerThrottling = 4 };
inline BOOL SetProcessInformation(HANDLE, int, LPVOID, DWORD) { return TRUE; }
inline int _dupenv_s(char** out, size_t* size, const char* name) {
  const char* v = std::getenv(name);
  *out = v ? strdup(v) : nullptr;
  if (size) *size = v ? std::strlen(v) + 1 : 0;
  return 0;
}
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR out, LPSTR* file_part);

typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
inline void linux_fill_systemtime(SYSTEMTIME* t, bool local) {
  timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm{}; const time_t s = ts.tv_sec;
  if (local) localtime_r(&s, &tm); else gmtime_r(&s, &tm);
  t->wYear = (WORD)(tm.tm_year + 1900); t->wMonth = (WORD)(tm.tm_mon + 1); t->wDayOfWeek = (WORD)tm.tm_wday;
  t->wDay = (WORD)tm.tm_mday; t->wHour = (WORD)tm.tm_hour; t->wMinute = (WORD)tm.tm_min; t->wSecond = (WORD)tm.tm_sec;
  t->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
}
inline void GetLocalTime(SYSTEMTIME* t) { linux_fill_systemtime(t, true); }
inline void GetSystemTime(SYSTEMTIME* t) { linux_fill_systemtime(t, false); }
