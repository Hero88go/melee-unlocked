// Linux build: open-file dialogs through the desktop's zenity or kdialog, filled into the same
// OPENFILENAMEW result Windows returns (one path, or folder + names for a multi-selection).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
#include <string>

#define OFN_READONLY 0x1u
#define OFN_OVERWRITEPROMPT 0x2u
#define OFN_HIDEREADONLY 0x4u
#define OFN_NOCHANGEDIR 0x8u
#define OFN_ALLOWMULTISELECT 0x200u
#define OFN_PATHMUSTEXIST 0x800u
#define OFN_FILEMUSTEXIST 0x1000u
#define OFN_EXPLORER 0x80000u
#define OFN_NOREADONLYRETURN 0x8000u
typedef struct tagOFNW {
  DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter;
  DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle;
  DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension;
  LPCWSTR lpstrDefExt; LPARAM lCustData; void* lpfnHook; LPCWSTR lpTemplateName; void* pvReserved;
  DWORD dwReserved, FlagsEx;
} OPENFILENAMEW;
BOOL GetOpenFileNameW(OPENFILENAMEW* dialog);
BOOL GetSaveFileNameW(OPENFILENAMEW* dialog);
// A folder picker, for the launcher's "choose folder" buttons. Empty when cancelled.
namespace linux_compat { std::wstring choose_folder(const wchar_t* title); }
