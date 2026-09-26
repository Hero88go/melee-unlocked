// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.h"
#include "host.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace host::updater {
namespace {
// The release list rather than /releases/latest: that endpoint skips pre-releases (betas).
const char* REPO_API = "https://api.github.com/repos/hero88go/melee-unlocked/releases?per_page=100";
std::atomic<State> g_state{State::Idle};
std::mutex g_mutex;
std::string g_current, g_latest, g_zip_url, g_message, g_zip_path;
size_t g_zip_size = 0;
std::thread g_thread;
struct Download { Release release; std::string root_name, legacy_url, experimental_url; size_t legacy_size = 0, experimental_size = 0; };
std::vector<Download> g_releases;
std::atomic<RollbackState> g_rollback_state{RollbackState::Idle};
std::string g_rollback_message, g_rollback_folder;
std::thread g_rollback_thread;

std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n ? n - 1 : 0, 0); if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
void set_message(const std::string& m) { std::lock_guard<std::mutex> lk(g_mutex); g_message = m; }

// GET with redirects (GitHub release assets redirect to a CDN). Returns the body.
bool http_get(const std::string& url, std::string* out, int* status) {
  std::wstring wurl = widen(url);
  URL_COMPONENTS uc{}; uc.dwStructSize = sizeof uc;
  wchar_t host[256]{}, path[4096]{}, extra[1024]{};
  uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
  uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
  HINTERNET session = WinHttpOpen(L"MeleeUnlocked updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, 8000, 8000, 30000, 120000);
  bool ok = false;
  HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
  if (conn) {
    const std::wstring request_path = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", request_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req) {
      DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
      WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
      if (WinHttpSendRequest(req, L"User-Agent: MeleeUnlocked\r\nAccept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        if (status) *status = (int)code;
        std::string body;
        DWORD expected = 0, expected_size = sizeof expected;
        bool have_length = WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &expected, &expected_size, WINHTTP_NO_HEADER_INDEX) != FALSE;
        bool complete = false;
        for (;;) {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(req, &avail)) break;
          if (!avail) { complete = true; break; }
          std::string chunk(avail, 0); DWORD got = 0;
          if (!WinHttpReadData(req, chunk.data(), avail, &got) || !got) break;
          body.append(chunk.data(), got);
        }
        ok = complete && (!have_length || body.size() == expected);
        if (out && ok) *out = std::move(body);
      }
      WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
  }
  WinHttpCloseHandle(session);
  return ok;
}

// "0.1.2-beta" -> (0,1,2); pre-release suffixes are ignored for ordering.
bool newer(const std::string& a, const std::string& b) {
  int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
  std::sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
  std::sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; ++i) if (x[i] != y[i]) return x[i] > y[i];
  return false;
}

void join() { if (g_thread.joinable()) g_thread.join(); }
bool safe_version(const std::string& s) {
  if (s.empty() || s.size() > 60 || s.find("..") != std::string::npos) return false;
  for (char c : s) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
                    !(c >= '0' && c <= '9') && c != '.' && c != '-' && c != '_') return false;
  return true;
}
bool release_url(const std::string& url) {
  return url.rfind("https://github.com/Hero88go/melee-unlocked/releases/download/", 0) == 0 ||
         url.rfind("https://github.com/hero88go/melee-unlocked/releases/download/", 0) == 0;
}
void rollback_message(const std::string& s) { std::lock_guard<std::mutex> lk(g_mutex); g_rollback_message = s; }
bool valid_install(const std::filesystem::path& p, bool experimental) {
  std::error_code ec;
  return std::filesystem::is_regular_file(p / "melee_port.exe", ec) &&
         std::filesystem::is_regular_file(p / "Sys" / "codehandler.bin", ec) &&
         (!experimental || std::filesystem::is_regular_file(p / "melee_port_dlss5.exe", ec));
}
bool archive_paths_safe(const std::filesystem::path& zip, const std::string& root) {
  std::wstring cmd = L"tar.exe -tf \"" + zip.wstring() + L"\"";
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                       nullptr, nullptr, &si, &pi);
  CloseHandle(wr);
  if (!started) { CloseHandle(rd); return false; }
  std::string listing; char buf[4096]; DWORD got = 0;
  while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) {
    listing.append(buf, got);
    if (listing.size() > 4 * 1024 * 1024) break;
  }
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  if (code != 0 || listing.empty() || listing.size() > 4 * 1024 * 1024) return false;
  size_t pos = 0;
  while (pos < listing.size()) {
    const size_t end = listing.find('\n', pos);
    std::string path = listing.substr(pos, end == std::string::npos ? end : end - pos);
    if (!path.empty() && path.back() == '\r') path.pop_back();
    if (path != root && path != root + "/" && path.rfind(root + "/", 0) != 0) return false;
    if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
      const size_t slash = path.find('/', start);
      const std::string part = path.substr(start, slash == std::string::npos ? slash : slash - start);
      if (part == ".." || part == ".") return false;
      if (slash == std::string::npos) break;
      start = slash + 1;
    }
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return true;
}
bool unpack_zip(const std::filesystem::path& zip, const std::filesystem::path& dest) {
  std::wstring cmd = L"tar.exe -xf \"" + zip.wstring() + L"\" -C \"" + dest.wstring() + L"\"";
  STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      dest.wstring().c_str(), &si, &pi)) return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return code == 0;
}
}  // namespace

void check(const std::string& current_version, bool install_experimental) {
  join();
  g_current = current_version;
  g_state = State::Checking;
  g_thread = std::thread([install_experimental] {
    std::string body; int status = 0;
    if (!http_get(REPO_API, &body, &status) || status != 200) { set_message(status ? "Update check failed (HTTP " + std::to_string(status) + ")" : "Update check failed (no connection)"); host::log("updater: GET %s failed, HTTP %d, error %lu", REPO_API, status, (unsigned long)GetLastError()); g_state = State::Failed; return; }
    auto list = nlohmann::json::parse(body, nullptr, false);
    nlohmann::json j;
    std::vector<Download> catalog;
    if (list.is_array()) for (auto& r : list) {
      if (!r.is_object() || r.value("draft", false) || !r.count("tag_name") || !r["tag_name"].is_string()) continue;
      std::string tag = r["tag_name"].get<std::string>();
      if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
      if (!safe_version(tag)) continue;
      Download d; d.release.version = tag; d.root_name = "MeleeUnlocked-" + tag;
      if (r.count("assets") && r["assets"].is_array()) for (const auto& a : r["assets"]) {
        if (!a.is_object()) continue;
        const std::string name = a.value("name", std::string());
        const std::string url = a.value("browser_download_url", std::string());
        if (!release_url(url)) continue;
        if (name == "MeleeUnlocked-" + tag + "-win64.zip") { d.legacy_url = url; d.legacy_size = a.value("size", size_t(0)); d.release.legacy = true; }
        if (name == "MeleePort-" + tag + "-win64.zip") { d.root_name = "MeleePort-" + tag; d.legacy_url = url; d.legacy_size = a.value("size", size_t(0)); d.release.legacy = true; }
        if (name == "MeleeUnlocked-" + tag + "-DLSS5-Experimental.zip") { d.experimental_url = url; d.experimental_size = a.value("size", size_t(0)); d.release.experimental = true; }
      }
      if (d.release.legacy || d.release.experimental) catalog.push_back(std::move(d));
      if (!j.is_object()) j = r;
    }
    if (!j.is_object()) { set_message("Update check failed (bad response)"); g_state = State::Failed; return; }
    std::string tag = j["tag_name"].get<std::string>();
    if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
    std::string zip;
    size_t zip_size = 0;
    char module[MAX_PATH]{}; GetModuleFileNameA(nullptr, module, MAX_PATH);
    std::string dir(module); auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir.resize(slash);
    // Since 0.7.0 there is one game build with DLSS 5 inside it, shipped as -win64.zip. A folder
    // that still holds an old melee_port_dlss5.exe must not keep asking for a separate
    // DLSS5-Experimental download, or it would never be offered an update again. The old name is
    // only a fallback for a release that has no -win64.zip.
    (void)dir;
    const std::string wanted = "MeleeUnlocked-" + tag + "-win64.zip";
    const std::string fallback = "MeleeUnlocked-" + tag + "-DLSS5-Experimental.zip";
    if (j.count("assets") && j["assets"].is_array()) {
      for (auto& a : j["assets"]) if (a.is_object() && a.value("name", std::string()) == wanted) { zip = a.value("browser_download_url", std::string()); zip_size = a.value("size", size_t(0)); break; }
      if (zip.empty())
        for (auto& a : j["assets"]) if (a.is_object() && a.value("name", std::string()) == fallback) { zip = a.value("browser_download_url", std::string()); zip_size = a.value("size", size_t(0)); break; }
    }
    { std::lock_guard<std::mutex> lk(g_mutex); g_latest = tag; g_zip_url = zip; g_zip_size = zip_size; g_releases = std::move(catalog); }
    if ((newer(tag, g_current) || install_experimental) && !zip.empty()) { set_message(install_experimental ? "Installing experimental build: " + tag : "Update available: " + tag); g_state = State::UpdateAvailable; host::log("updater: version %s available (running %s)", tag.c_str(), g_current.c_str()); }
    else if (zip.empty() && install_experimental) { set_message("Experimental download missing from the latest release"); g_state = State::Failed; }
    else { set_message("Up to date (" + g_current + ")"); g_state = State::UpToDate; }
  });
}

void shutdown() { join(); if (g_rollback_thread.joinable()) g_rollback_thread.join(); }
State state() { return g_state.load(); }
std::string latest_version() { std::lock_guard<std::mutex> lk(g_mutex); return g_latest; }
std::string message() { std::lock_guard<std::mutex> lk(g_mutex); return g_message; }
std::vector<Release> releases() { std::lock_guard<std::mutex> lk(g_mutex); std::vector<Release> out; for (const auto& d : g_releases) out.push_back(d.release); return out; }
RollbackState rollback_state() { return g_rollback_state.load(); }
std::string rollback_message() { std::lock_guard<std::mutex> lk(g_mutex); return g_rollback_message; }
std::string rollback_folder() { std::lock_guard<std::mutex> lk(g_mutex); return g_rollback_folder; }

bool install_release(const std::string& version, bool experimental, const std::string& install_root) {
  if (!safe_version(version) || g_rollback_state == RollbackState::Downloading) return false;
  Download chosen; bool found = false;
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (const auto& d : g_releases) if (d.release.version == version &&
        (experimental ? d.release.experimental : d.release.legacy)) { chosen = d; found = true; break; }
    g_rollback_folder.clear();
  }
  if (!found) return false;
  if (g_rollback_thread.joinable()) g_rollback_thread.join();
  g_rollback_state = RollbackState::Downloading;
  rollback_message("Installing " + version + " beside the current version...");
  g_rollback_thread = std::thread([chosen, experimental, install_root] {
    namespace fs = std::filesystem;
    const std::string id = chosen.release.version + (experimental ? "-dlss5" : "-legacy");
    const fs::path versions = fs::path(install_root) / "Versions";
    const fs::path target = versions / id;
    std::error_code ec;
    if (valid_install(target, experimental)) {
      { std::lock_guard<std::mutex> lk(g_mutex); g_rollback_folder = target.string(); }
      rollback_message("Version " + chosen.release.version + " is ready"); g_rollback_state = RollbackState::Ready; return;
    }
    if (fs::exists(target, ec)) { rollback_message("Version folder exists but is incomplete; remove it before retrying"); g_rollback_state = RollbackState::Failed; return; }
    fs::create_directories(versions, ec);
    if (ec) { rollback_message("Cannot create Versions folder"); g_rollback_state = RollbackState::Failed; return; }
    const fs::path stage = versions / ("." + id + ".stage-" + std::to_string(GetCurrentProcessId()));
    if (fs::exists(stage, ec)) { rollback_message("A previous version install is still staged"); g_rollback_state = RollbackState::Failed; return; }
    fs::create_directory(stage, ec);
    if (ec) { rollback_message("Cannot create download folder"); g_rollback_state = RollbackState::Failed; return; }
    const auto fail = [&](const std::string& why) {
      fs::remove_all(stage, ec); rollback_message(why); g_rollback_state = RollbackState::Failed;
    };
    const std::string url = experimental ? chosen.experimental_url : chosen.legacy_url;
    const size_t expected = experimental ? chosen.experimental_size : chosen.legacy_size;
    std::string body; int status = 0;
    if (!http_get(url, &body, &status) || status != 200 || body.size() < 1000000 ||
        (expected && body.size() != expected)) { fail("Version download failed or was incomplete"); return; }
    const fs::path zip = stage / "release.zip";
    { std::ofstream f(zip, std::ios::binary); f.write(body.data(), (std::streamsize)body.size()); if (!f) { fail("Cannot save version archive"); return; } }
    const fs::path unpack = stage / "unpack";
    fs::create_directory(unpack, ec);
    if (ec || !archive_paths_safe(zip, chosen.root_name) || !unpack_zip(zip, unpack)) {
      fail("Version archive is invalid or could not be unpacked"); return;
    }
    const fs::path payload = unpack / chosen.root_name;
    if (!valid_install(payload, experimental)) { fail("Archive does not contain the expected game files"); return; }
    fs::rename(payload, target, ec);
    if (ec) { fail("Could not install version beside current build"); return; }
    fs::remove_all(stage, ec);
    { std::lock_guard<std::mutex> lk(g_mutex); g_rollback_folder = target.string(); }
    rollback_message("Version " + chosen.release.version + " is ready"); g_rollback_state = RollbackState::Ready;
  });
  return true;
}

void download_and_install() {
  if (g_state != State::UpdateAvailable) return;
  join();
  g_state = State::Downloading;
  set_message("Downloading update...");
  g_thread = std::thread([] {
    std::string url; size_t expected;
    { std::lock_guard<std::mutex> lk(g_mutex); url = g_zip_url; expected = g_zip_size; }
    std::string body; int status = 0;
    if (!http_get(url, &body, &status) || status != 200 || body.size() < 1000000 || (expected && body.size() != expected)) { set_message("Download failed or incomplete"); g_state = State::Failed; return; }
    char exe[MAX_PATH]{}; DWORD length = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) { set_message("Cannot resolve application path"); g_state = State::Failed; return; }
    std::string dir(exe); auto separator = dir.find_last_of("\\/");
    if (separator == std::string::npos) { set_message("Cannot resolve application directory"); g_state = State::Failed; return; }
    dir.resize(separator);
    std::string zip = dir + "\\update.zip", bat = dir + "\\update.bat";
    { std::ofstream f(zip, std::ios::binary); f.write(body.data(), (std::streamsize)body.size()); if (!f) { set_message("Cannot write update.zip"); g_state = State::Failed; return; } }
    // Relaunch exactly what was started, so this works the same from the release batch file, the
    // launcher, or a development shortcut with its own arguments. Percent signs would be eaten by
    // the batch interpreter.
    std::string relaunch = GetCommandLineA();
    for (size_t i = relaunch.find('%'); i != std::string::npos; i = relaunch.find('%', i + 2)) relaunch.insert(i, 1, '%');

    // The script waits for this process to exit, unpacks the zip (Windows 10+ ships tar for zips),
    // copies the release folder over this one (keeping User\, settings, saves, replays) and starts
    // the game again. Written in binary: text mode would turn every \r\n into \r\r\n, and the
    // stray carriage return becomes part of the last argument on each line, which is what stopped
    // the relaunch from working.
    std::ofstream b(bat, std::ios::binary);
    b << "@echo off\r\ncd /d \"" << dir << "\"\r\n"
      << "set LOG=\"" << dir << "\\update.log\"\r\n"
      << "echo update started %DATE% %TIME%> %LOG%\r\n"
      << ":wait\r\ntasklist /FI \"PID eq " << GetCurrentProcessId() << "\" 2>nul | find \"" << GetCurrentProcessId() << "\" >nul && (timeout /t 1 /nobreak >nul & goto wait)\r\n"
      // The game holds its own exe open. Updating from the launcher while a match is running used to
      // fail the copy with a locked file, so wait for it too rather than fighting it.
      << ":waitgame\r\n"
      << "tasklist /FI \"IMAGENAME eq melee_port.exe\" 2>nul | find /i \"melee_port.exe\" >nul && (timeout /t 1 /nobreak >nul & goto waitgame)\r\n"
      << "tasklist /FI \"IMAGENAME eq melee_port_compat.exe\" 2>nul | find /i \"melee_port_compat.exe\" >nul && (timeout /t 1 /nobreak >nul & goto waitgame)\r\n"
      << "tasklist /FI \"IMAGENAME eq melee_port_dlss5.exe\" 2>nul | find /i \"melee_port_dlss5.exe\" >nul && (timeout /t 1 /nobreak >nul & goto waitgame)\r\n"
      << "tasklist /FI \"IMAGENAME eq melee_port_dlss5_compat.exe\" 2>nul | find /i \"melee_port_dlss5_compat.exe\" >nul && (timeout /t 1 /nobreak >nul & goto waitgame)\r\n"
      << "rmdir /s /q update_tmp 2>nul\r\nmkdir update_tmp\r\n"
      << "tar -xf update.zip -C update_tmp\r\n"
      << "if errorlevel 1 (echo could not unpack update.zip>> %LOG% & echo Could not unpack the update. & pause & exit /b 1)\r\n"
      << "set SRC=\r\n"
      << "for /d %%d in (update_tmp\\MeleeUnlocked-* update_tmp\\MeleePort-*) do set SRC=%%d\r\n"
      << "if not defined SRC (echo no release folder inside update.zip>> %LOG% & echo The update did not contain a release folder. & pause & exit /b 1)\r\n"
      << "echo copying from %SRC%>> %LOG%\r\n"
      // /r overwrites read-only files: Windows marks files unpacked from a downloaded zip read-only
      // often enough that the copy failed outright with "Could not copy the update into place".
      // A file can also still be held for a moment by the process that just exited, so retry.
      << "set TRIES=0\r\n"
      << ":copy\r\n"
      << "set /a TRIES+=1\r\n"
      << "attrib -r \"*.*\" /s >nul 2>&1\r\n"
      << "xcopy /e /y /q /r \"%SRC%\\*\" \".\\\" >> %LOG% 2>&1\r\n"
      << "if not errorlevel 1 goto copied\r\n"
      << "if %TRIES% lss 5 (echo copy attempt %TRIES% failed, retrying>> %LOG% & timeout /t 2 /nobreak >nul & goto copy)\r\n"
      << "echo copy failed after %TRIES% attempts>> %LOG%\r\n"
      << "echo Could not copy the update into place.\r\n"
      << "echo Close the game and any open Explorer window on this folder, then try again.\r\n"
      << "echo Details: %LOG%\r\n"
      << "pause & exit /b 1\r\n"
      << ":copied\r\n"
      << "rmdir /s /q update_tmp\r\ndel update.zip\r\n"
      << "echo restarting: " << relaunch << ">> %LOG%\r\n"
      << "start \"\" " << relaunch << "\r\n"
      << "echo done>> %LOG%\r\n"
      << "del \"%~f0\"\r\n";
    b.close();
    if (!b) { set_message("Cannot write update installer"); g_state = State::Failed; return; }
    set_message("Update downloaded; restarting to install");
    g_state = State::ReadyToInstall;
    host::log("updater: %zu bytes downloaded, installing via update.bat", body.size());
    STARTUPINFOA si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    // Doubled quotes: cmd strips one layer, and the path contains spaces.
    std::string cmd = "cmd /c \"\"" + bat + "\"\"";
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
      set_message("Cannot start update installer"); g_state = State::Failed; return;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    host::request_exit(0);
  });
}
}  // namespace host::updater
