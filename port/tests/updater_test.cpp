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
  // port_updater_test <zip> <dest> <release folder>: check and unpack one real release zip.
  if (argc == 4 && std::string(argv[1]).rfind("--", 0) != 0) {   // not the --hold-updater-test fixture
    const bool inside = host::updater::archive_inside(argv[1], argv[3]);
    const bool unpacked = inside && host::updater::unpack_archive(argv[1], argv[2]);
    std::printf("inside %d unpacked %d\n", inside, unpacked);
    return inside && unpacked ? 0 : 1;
  }
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
  // Unpacked in process, not by tar.exe (Wine and Proton have none).
  {
    { std::ofstream big(root / "MeleeUnlocked-9.9.9" / "Sys" / "big.bin", std::ios::binary); for (int i = 0; i < 200000; ++i) big << (char)(i * 7 % 251); }
    const fs::path packed = root / "packed.zip", out = root / "unpacked";
    check(make_zip(root, packed, L"MeleeUnlocked-9.9.9"), "compressed test archive made");
    check(host::updater::unpack_archive(packed.u8string(), out.u8string()), "the archive unpacks without tar");
    std::ifstream in(out / "MeleeUnlocked-9.9.9" / "Sys" / "big.bin", std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), {});
    bool same = got.size() == 200000;
    for (int i = 0; same && i < 200000; ++i) same = got[(size_t)i] == (char)(i * 7 % 251);
    check(same, "an unpacked file matches the original byte for byte");
    check(!host::updater::unpack_archive((root / "missing.zip").u8string(), out.u8string()), "a missing archive does not unpack");
    fs::remove(root / "MeleeUnlocked-9.9.9" / "Sys" / "big.bin", ec);
  }
  // Mod downloads (.zip and .tar.gz) unpack in process too: pax (Windows' tar) and GNU long names,
  // and nothing may land outside the folder.
  {
    const std::string deep(120, 'd');
    fs::create_directories(root / "Mod" / deep, ec);
    { std::ofstream(root / "Mod" / deep / "patch.xdelta", std::ios::binary) << "patch bytes"; }
    { std::ofstream big(root / "Mod" / "big.bin", std::ios::binary); for (int i = 0; i < 300000; ++i) big << (char)(i * 13 % 241); }
    const fs::path pax = root / "pax.tar.gz", gnu = root / "gnu.tar.gz", zip = root / "mod.zip";
    check(make_zip(root, pax, L"--format pax -z Mod") && make_zip(root, gnu, L"--format gnutar -z Mod") && make_zip(root, zip, L"Mod"),
          "mod test archives made");
    for (const fs::path& archive : {pax, gnu, zip}) {
      const fs::path out = root / ("mod-" + archive.stem().string());
      const bool unpacked = host::updater::unpack_download(archive.u8string(), out.u8string());
      std::ifstream patch(out / "Mod" / deep / "patch.xdelta", std::ios::binary), big(out / "Mod" / "big.bin", std::ios::binary);
      const std::string p((std::istreambuf_iterator<char>(patch)), {}), b((std::istreambuf_iterator<char>(big)), {});
      bool same = b.size() == 300000;
      for (int i = 0; same && i < 300000; ++i) same = b[(size_t)i] == (char)(i * 13 % 241);
      check(unpacked && p == "patch bytes" && same, ("a mod download unpacks without tar: " + archive.filename().string()).c_str());
    }
    { std::ofstream(root / "evil.txt") << "x"; }
    const fs::path evil = root / "evil.tar.gz";
    check(make_zip(root / "Mod", evil, L"-z -P ../evil.txt"), "an archive leaving its folder made");
    check(!host::updater::unpack_download(evil.u8string(), (root / "mod-evil" / "in").u8string()) &&
          !fs::exists(root / "mod-evil" / "evil.txt"), "an entry outside the folder is refused");
    check(!host::updater::unpack_download((root / "missing.tar.gz").u8string(), (root / "x").u8string()), "a missing download does not unpack");
  }
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
  // The install script itself, run hidden on two folders made here: a short one, and a deep one
  // where a long file name of the release lands past 260 characters (a shader pack's picture did,
  // and the copy stopped there with the folder half updated).
  {
    const std::string long_name = std::string(120, 'n') + ".png";
    const fs::path release = root / "MeleeUnlocked-9.9.8";
    fs::create_directories(release / "Shaders", ec);
    { std::ofstream(release / "melee_port.exe") << "new"; }
    { std::ofstream(release / "Shaders" / long_name) << "picture"; }
    { std::ofstream(release / "Shaders" / "zz-after.txt") << "after"; }
    const fs::path package = root / "package.zip";
    check(make_zip(root, package, L"MeleeUnlocked-9.9.8"), "release archive with a long file name made");
    const auto present = [](const fs::path& p) {
      return GetFileAttributesW((L"\\\\?\\" + p.wstring()).c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    const fs::path folders[2] = {root / "play", root / "deep" / std::string(100, 'd') / "game"};
    for (int i = 0; i < 2; ++i) {
      const fs::path& folder = folders[i];
      fs::create_directories(folder, ec);
      { std::ofstream(folder / "melee_port.exe") << "old"; }
      fs::copy_file(package, folder / "update.zip", fs::copy_options::overwrite_existing, ec);
      // 999996 is no process: the script's waits fall straight through. The "relaunch" writes a file.
      const std::string script = host::updater::install_script(folder.u8string(), 999996ul, {999992ul},
                                                               "/b cmd /c \"echo started> relaunched.txt\"", false);
      { std::ofstream b(folder / "update.bat", std::ios::binary); b << script; }
      std::wstring cmd = L"cmd /c \"\"" + (folder / "update.bat").wstring() + L"\"\"";
      STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
      const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                          folder.wstring().c_str(), &si, &pi);
      if (started) { WaitForSingleObject(pi.hProcess, 180000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
      for (int wait = 0; wait < 50 && !present(folder / "relaunched.txt"); ++wait) Sleep(100);
      std::string exe_text;
      { std::ifstream in(folder / "melee_port.exe"); in >> exe_text; }
      const char* where = i ? "deep folder" : "short folder";
      std::printf("%s: path of the long file is %zu characters\n", where, (folder / "Shaders" / long_name).wstring().size());
      check(started && exe_text == "new", "install script replaces the program");
      check(present(folder / "Shaders" / long_name), "install script copies the file with the long name");
      check(present(folder / "Shaders" / "zz-after.txt"), "install script copies the files after it");
      check(present(folder / "relaunched.txt"), "install script starts the program again");
      std::string log_text;
      { std::ifstream in(folder / "update.log", std::ios::binary); log_text.assign(std::istreambuf_iterator<char>(in), {}); }
      const bool second_way = log_text.find("trying robocopy") != std::string::npos;
      check(second_way == (i == 1), i ? "the deep folder needed the second way to copy" : "the short folder copied the first way");
      check(!present(folder / "update.zip") && !present(folder / "update_tmp"), "install script leaves no archive or unpack folder behind");
      // The long file is past what remove_all reaches.
      DeleteFileW((L"\\\\?\\" + (folder / "Shaders" / long_name).wstring()).c_str());
    }
  }
  fs::remove_all(root, ec);
  std::printf("%s\n", failures ? "updater checks FAILED" : "updater checks passed");
  return failures ? 1 : 0;
}
