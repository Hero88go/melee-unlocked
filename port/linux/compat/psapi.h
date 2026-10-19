// Linux build: this process's memory use from /proc/self/status. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
typedef struct _PROCESS_MEMORY_COUNTERS { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize,
  QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage,
  PeakPagefileUsage; } PROCESS_MEMORY_COUNTERS;
typedef struct _PROCESS_MEMORY_COUNTERS_EX { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize,
  QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage,
  PeakPagefileUsage, PrivateUsage; } PROCESS_MEMORY_COUNTERS_EX;
BOOL K32GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size);
#define GetProcessMemoryInfo K32GetProcessMemoryInfo
