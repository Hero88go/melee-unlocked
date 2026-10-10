// Updater download checks: the release digest and the archive entry check, on small zips made here.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>
#include <algorithm>

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

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--hold-updater-test") {
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]);
    HANDLE quit = OpenEventA(SYNCHRONIZE, FALSE, argv[3]);
    if (!ready || !quit) return 2;
    SetEvent(ready);
    const DWORD result = WaitForSingleObject(quit, 30000);
    CloseHandle(ready); CloseHandle(quit);
    return result == WAIT_OBJECT_0 ? 0 : 3;
  }
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
  // A Source game also keeps its DLL locked. Only this installation's processes
  // should hold up an update; a second installation may stay running.
  const fs::path install = root / L"install \u00e9", other = root / "second install";
  fs::create_directories(install, ec); fs::create_directories(other, ec);
  wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, 32768);
  PROCESS_INFORMATION children[2]{};
  HANDLE ready[2]{}, quit[2]{};
  for (int i = 0; i < 2; ++i) {
    const fs::path exe = (i ? other : install) / "melee_source.exe";
    fs::copy_file(self, exe, fs::copy_options::overwrite_existing, ec);
    const std::string prefix = "Local\\mu-updater-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i);
    const std::string ready_name = prefix + "-ready", quit_name = prefix + "-quit";
    ready[i] = CreateEventA(nullptr, TRUE, FALSE, ready_name.c_str());
    quit[i] = CreateEventA(nullptr, TRUE, FALSE, quit_name.c_str());
    std::wstring command = L"\"" + exe.wstring() + L"\" --hold-updater-test " +
      std::wstring(ready_name.begin(), ready_name.end()) + L" " + std::wstring(quit_name.begin(), quit_name.end());
    STARTUPINFOW si{}; si.cb = sizeof si;
    const bool started = !ec && CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS, nullptr, nullptr, &si, &children[i]);
    // The fixture runs at idle priority: on a PC busy with other work it can take well over 5 s to start.
    check(started && WaitForSingleObject(ready[i], 60000) == WAIT_OBJECT_0, "own hidden Source fixture started");
  }
  const auto pids = host::updater::install_processes(install.u8string());
  check(children[0].dwProcessId && std::find(pids.begin(), pids.end(), children[0].dwProcessId) != pids.end(),
        "updater waits for Source from the installation being updated");
  check(children[1].dwProcessId && std::find(pids.begin(), pids.end(), children[1].dwProcessId) == pids.end(),
        "another installation does not block the updater");
  for (int i = 0; i < 2; ++i) {
    if (quit[i]) SetEvent(quit[i]);
    if (children[i].hProcess) { WaitForSingleObject(children[i].hProcess, 5000); CloseHandle(children[i].hProcess); }
    if (children[i].hThread) CloseHandle(children[i].hThread);
    if (ready[i]) CloseHandle(ready[i]);
    if (quit[i]) CloseHandle(quit[i]);
  }
  fs::remove_all(root, ec);
  std::printf("%s\n", failures ? "updater checks FAILED" : "updater checks passed");
  return failures ? 1 : 0;
}
