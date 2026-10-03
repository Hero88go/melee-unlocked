// Updater download checks: the release digest and the archive entry check, on small zips made here.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>

namespace host {
void log(const char*, ...) {}
void request_exit(int) {}
}

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
// A zip of the given folder made by Windows' own tar, the tool the updater unpacks with.
bool make_zip(const std::filesystem::path& folder, const std::filesystem::path& zip, const std::wstring& members) {
  std::wstring cmd = L"tar.exe -a -c -f \"" + zip.wstring() + L"\" " + members;
  STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      folder.wstring().c_str(), &si, &pi)) return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return code == 0;
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  using host::updater::archive_inside;
  using host::updater::download_matches;
  // SHA-256 of "abc" (FIPS 180-2 example).
  const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  check(download_matches("abc", abc), "a download that matches the listed digest passes");
  check(!download_matches("abd", abc), "a damaged download of the same size is refused");
  check(download_matches("anything", ""), "a release without a listed digest keeps the size check only");

  std::error_code ec;
  const fs::path root = fs::temp_directory_path() / ("mu-updater-test-" + std::to_string(GetCurrentProcessId()));
  fs::remove_all(root, ec);
  fs::create_directories(root / "MeleeUnlocked-9.9.9" / "Sys", ec);
  fs::create_directories(root / "Other", ec);
  { std::ofstream(root / "MeleeUnlocked-9.9.9" / "melee_port.exe") << "x"; }
  { std::ofstream(root / "MeleeUnlocked-9.9.9" / "Sys" / "codehandler.bin") << "x"; }
  { std::ofstream(root / "Other" / "stray.txt") << "x"; }
  const fs::path good = root / "good.zip", stray = root / "stray.zip";
  check(make_zip(root, good, L"MeleeUnlocked-9.9.9") && make_zip(root, stray, L"MeleeUnlocked-9.9.9 Other"),
        "test archives made");
  check(archive_inside(good.u8string(), "MeleeUnlocked-9.9.9"), "an archive holding only its release folder passes");
  check(!archive_inside(good.u8string(), "MeleeUnlocked-9.9.8"), "the wrong release folder is refused");
  check(!archive_inside(stray.u8string(), "MeleeUnlocked-9.9.9"), "an entry outside the release folder is refused");
  check(!archive_inside((root / "missing.zip").u8string(), "MeleeUnlocked-9.9.9"), "a missing archive is refused");
  fs::remove_all(root, ec);
  std::printf("%s\n", failures ? "updater checks FAILED" : "updater checks passed");
  return failures ? 1 : 0;
}
