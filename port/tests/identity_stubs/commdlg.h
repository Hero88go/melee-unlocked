#pragma once
#include "windows.h"
struct OPENFILENAMEW {
  DWORD lStructSize = 0, nMaxFile = 0, Flags = 0;
  HWND hwndOwner = nullptr;
  const wchar_t* lpstrFilter = nullptr;
  wchar_t* lpstrFile = nullptr;
};
constexpr DWORD OFN_FILEMUSTEXIST = 1, OFN_PATHMUSTEXIST = 2, OFN_NOCHANGEDIR = 4;
inline bool GetOpenFileNameW(OPENFILENAMEW*) { return false; }
