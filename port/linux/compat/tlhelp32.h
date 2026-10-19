// Linux build: process and thread lists from /proc. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
#define TH32CS_SNAPPROCESS 0x2u
#define TH32CS_SNAPTHREAD 0x4u
typedef struct tagPROCESSENTRY32W { DWORD dwSize, cntUsage, th32ProcessID; ULONG_PTR th32DefaultHeapID; DWORD th32ModuleID,
  cntThreads, th32ParentProcessID; LONG pcPriClassBase; DWORD dwFlags; WCHAR szExeFile[MAX_PATH]; } PROCESSENTRY32W;
typedef struct tagTHREADENTRY32 { DWORD dwSize, cntUsage, th32ThreadID, th32OwnerProcessID; LONG tpBasePri, tpDeltaPri;
  DWORD dwFlags; } THREADENTRY32;
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD process);
BOOL Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* entry);
BOOL Process32NextW(HANDLE snapshot, PROCESSENTRY32W* entry);
BOOL Thread32First(HANDLE snapshot, THREADENTRY32* entry);
BOOL Thread32Next(HANDLE snapshot, THREADENTRY32* entry);
