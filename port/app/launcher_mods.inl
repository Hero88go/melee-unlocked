// Mod manager: the launcher's Mods page (left rail > Mods). Lists the community mods, shows which are
// installed (the game's own scan, Mods/.cache/detected.json) with a green check, an On/Off switch
// each, and installs them:
//   Get      one click: the mod's official GitHub release is downloaded at the player's click,
//            checked against the pinned SHA-256 (and GitHub's published digest), unpacked, and set up.
//   Update   the same, replacing the installed disc once the new one is verified.
//   Remove   deletes the managed disc or save pack. Saves and the player's own files stay.
// Setting up = the pinned xdelta3 shipped in tools\ (same bytes as the one the mods ship) run with the
// official arguments on the player's own vanilla Melee 1.02 disc (MD5-checked), writing into
// Mods\Discs, then the result is checked against the official output hash. Nothing is hosted or
// mirrored by us and no game data is in the launcher. A save (.gci) goes to Mods\Saves; a finished
// .iso is copied into Mods\Discs, or stays at its original path when the player adds it to links.txt.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

void select_tab(int idx);

namespace mod_manager {

using Json = nlohmann::json;

using launcher::mod_catalog::CatalogMod;
using launcher::mod_catalog::Installed;
namespace catalog_core = launcher::mod_catalog;
namespace fs = std::filesystem;
using launcher::lang::tx;
// Whether this launcher can start an installed mod, and whether it offers a catalog mod at all. The
// launcher with one Game Build has no Static Recomp: a disc that needs it is listed as not supported,
// and a catalog mod whose note names it is not offered (the note is all a catalog entry says about
// engines, also in a catalog fetched later). Otherwise these are playable() and "every mod".
bool runs_here(const Installed& m) {
#ifdef MELEE_NO_SLIPPI
  if (m.needs_engine == "recomp") return false;
#endif
  return launcher::mod_catalog::playable(m);
}
bool offered_here(const CatalogMod& m) {
#ifdef MELEE_NO_SLIPPI
  if (m.note.find("Static Recomp") != std::string::npos) return false;
#endif
  (void)m;
  return true;
}
// Built in; a newer list at kCatalogUrl (our GitHub, metadata only) can update versions and pins
// without a launcher update. The same text is published as mods-catalog.json.
std::vector<CatalogMod> default_catalog() {
  std::vector<CatalogMod> mods; std::string error;
  catalog_core::parse(R"catalog({
  "version": 2,
  "mods": [
    {
      "id": "te",
      "name": "20XX Tournament Edition",
      "credits": "By Dan Salvato and contributors",
      "license": "MPL-2.0",
      "page": "https://github.com/dansalvato/20XXTE/releases/tag/v2d-rev4",
      "repo": "dansalvato/20XXTE",
      "tag": "v2d-rev4",
      "asset_pattern": "20XXTE-v2d-r4-USA.gci",
      "kind": "gci",
      "one_click": true,
      "version": "v2d r4",
      "url": "https://github.com/dansalvato/20XXTE/releases/download/v2d-rev4/20XXTE-v2d-r4-USA.gci",
      "sha256": "61834b65d921668ff11702c458a1ae6946172b0a4c8321a47e2c2ddff9aef51d",
      "policy": "licensed",
      "note": "Runs on the Source Port."
    },
    {
      "id": "tmce",
      "name": "Training Mode CE",
      "credits": "By UnclePunch, Aitch and contributors",
      "license": "Shared by its authors",
      "page": "https://github.com/AlexanderHarrison/TrainingMode-CommunityEdition/releases/tag/CE-v1.4-dev1",
      "repo": "AlexanderHarrison/TrainingMode-CommunityEdition",
      "tag": "CE-v1.4-dev1",
      "asset_pattern": "TM-CE.zip",
      "kind": "xdelta_zip",
      "one_click": true,
      "version": "v1.4 d1",
      "url": "https://github.com/AlexanderHarrison/TrainingMode-CommunityEdition/releases/download/CE-v1.4-dev1/TM-CE.zip",
      "sha256": "75091be9b402b1dea378a2ef3bc741baae3a11e0324b614cf5ee9da99d2e2d0f",
      "policy": "permission_granted",
      "tool_sha256": "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342",
      "patch_sha256": "6c21aac1e7c0d41b0f97f22c4cacd99f4dadae8b80507cfea6a7b47cead72e1f",
      "output_md5": "a5312a822ff958b967da45a43ce50d09",
      "output_sha256": "6e753b8f9c3178a9581f3c5d73ed4a75565160254f4f58c6e8c9568ea3e2a02e",
      "note": "Runs on the Source Port."
    },
    {
      "id": "akaneia",
      "name": "Akaneia",
      "credits": "By the Akaneia team",
      "license": "Shared by its authors",
      "page": "https://github.com/akaneia/akaneia-build/releases/tag/1.0.1",
      "repo": "akaneia/akaneia-build",
      "tag": "1.0.1",
      "asset_pattern": "Akaneia.Builder.1.0.1.7z",
      "kind": "xdelta_7z",
      "one_click": true,
      "version": "1.0.1",
      "url": "https://github.com/akaneia/akaneia-build/releases/download/1.0.1/Akaneia.Builder.1.0.1.7z",
      "sha256": "7d46ce47b27272a369150cb105f4cb3b046b2e7790d233595ff4537ef442919f",
      "policy": "permission_granted",
      "tool_sha256": "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342",
      "patch_sha256": "005d5667dffa345bfba5f08e6849a4ff2965633584e8a3da74ceba7556204608",
      "output_md5": "63ea8e113e451c9369f17f7a49012412",
      "output_sha256": "b1b60a188421c8d0e564fa276a4762fe67e8de271ac2e131c20f70ee715af6cd",
      "note": "Runs on the Static Recomp."
    },
    {
      "id": "ace",
      "name": "ACE Build",
      "credits": "By Team ACE",
      "license": "Shared by its authors",
      "page": "https://github.com/Chri222k/ACE-BUILD-PUBLIC-/releases/tag/v2.0.0",
      "repo": "Chri222k/ACE-BUILD-PUBLIC-",
      "tag": "v2.0.0",
      "asset_pattern": "ACE.2.0.patcher.zip",
      "kind": "xdelta_zip",
      "one_click": true,
      "version": "2.0.0",
      "url": "https://github.com/Chri222k/ACE-BUILD-PUBLIC-/releases/download/v2.0.0/ACE.2.0.patcher.zip",
      "sha256": "1ac7caa5c80b93dbcf6071b208045a3e855ad8938cea87918b84d5c9afd14f75",
      "policy": "permission_granted",
      "tool_sha256": "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342",
      "patch_sha256": "a451bf67acd1333836847acead02191fa2122a254b6295e949620c49e3b8b984",
      "output_sha256": "0d7ba36bef3505cdf6c0209d9ae34977e23cf6cae4d24366e8d405810f798993",
      "note": "Runs on the Static Recomp."
    },
    {
      "id": "hackpack",
      "name": "20XX Training Hack Pack",
      "credits": "By Achilles, DRGN and contributors",
      "license": "Shared by its authors",
      "page": "https://github.com/DRGN-DRC/20XX-HACK-PACK/releases/tag/v5.0.2",
      "repo": "DRGN-DRC/20XX-HACK-PACK",
      "tag": "v5.0.2",
      "asset_pattern": "20XXHP.5.0.2.Creator.zip",
      "kind": "xdelta_zip",
      "one_click": true,
      "version": "5.0.2",
      "url": "https://github.com/DRGN-DRC/20XX-HACK-PACK/releases/download/v5.0.2/20XXHP.5.0.2.Creator.zip",
      "sha256": "a0aa4958a2228de7cc9513c6014a8bb545bec7ee539cb67fb2468c10ad50e4e3",
      "policy": "permission_granted",
      "tool_sha256": "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342",
      "patch_sha256": "7f675995e000eda80cfdb340a1272b4370d8ef492cc7f576bf9bfa5e581048a5",
      "output_md5": "d926ba5b39551f5245fd655bc1dfeb3f",
      "output_sha256": "6a23c731a95a076e531ce38d60c699b4c73fc11a035e42afb390f6d4ab96acea",
      "note": "Plays on the Static Recomp, offline (no online play or replays)."
    }
  ]
})catalog", &mods, &error);
  return mods;
}
constexpr const wchar_t* kCatalogHost = L"raw.githubusercontent.com";
constexpr const wchar_t* kCatalogPath = L"/Hero88go/melee-unlocked/main/mods-catalog.json";
constexpr const char* kVanillaMd5 = "0e63d4223b01d9aba596259dc155a174";
// Tools shipped beside the launcher (tools\). xdelta3 3.0.11 (GPL) is byte-identical to the one the
// mods ship; 7zr.exe is the official 7-Zip standalone (LGPL) for .7z downloads, which Windows' tar
// cannot unpack (no LZMA).
constexpr const char* kXdeltaTool = "xdelta3.exe";
constexpr const char* kXdeltaSha256 = "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342";
constexpr const char* kSevenZipTool = "7zr.exe";
constexpr const char* k7zrSha256 = "ad4c82fadcbdf93c03b4fc440f300509c7d60c5c2f4d183e35d9d70d6957037d";
constexpr uint64_t kGiB = 1024ull * 1024 * 1024, kMiB = 1024ull * 1024;
constexpr uint64_t kDiscBytes = 1800 * kMiB;   // the largest built disc (ACE) is 1.72 GB

std::vector<CatalogMod> g_catalog = default_catalog();
std::mutex g_mutex, g_install_mutex;
std::map<std::string, std::string> g_status;   // id -> status line
std::map<std::string, int> g_percent;          // id -> download percent while downloading
std::map<std::string, std::shared_ptr<std::atomic<bool>>> g_jobs;   // id -> cancel flag while busy
HWND g_window = nullptr;                       // the Mods page panel
const UINT WM_MODS_REFRESH = WM_APP + 40;

std::string mods_dir() { return work_dir() + "\\Mods"; }

// ---- small helpers ----
std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s; for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; } return s;
}
std::string hash_file(const std::wstring& path, LPCWSTR algorithm) {
  BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0) < 0) return {};
  std::string out;
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> buf(4 << 20);
    bool ok = (bool)in;
    while (ok && in) {
      in.read(buf.data(), (std::streamsize)buf.size());
      if (in.gcount() > 0 && BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)in.gcount(), 0) < 0) ok = false;
    }
    DWORD len = 0, got = 0; uint8_t digest[64];
    if (ok && BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof len, &got, 0) >= 0 && len <= 64 &&
        BCryptFinishHash(h, digest, len, 0) >= 0) out = hex(digest, len);
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}
std::string size_text(uint64_t bytes) {
  char buf[32];
  if (bytes >= kGiB) std::snprintf(buf, sizeof buf, "%.1f GB", bytes / (double)kGiB);
  else std::snprintf(buf, sizeof buf, "%.0f MB", bytes / (double)kMiB);
  return buf;
}
void post_refresh() { if (g_window) PostMessageW(g_window, WM_MODS_REFRESH, 0, 0); }
void set_status(const std::string& id, const std::string& text) {
  { std::lock_guard<std::mutex> lock(g_mutex); g_status[id] = text; }
  if (g_launcher_test) {   // the hidden test gate reads every status line from here
    static std::mutex log_mutex; std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream(fs::u8path(g_dir + "/mods-status.log"), std::ios::app) << id << '\t' << text << '\n';
  }
  post_refresh();
}
void set_percent(const std::string& id, int percent) {
  { std::lock_guard<std::mutex> lock(g_mutex); if (percent < 0) g_percent.erase(id); else g_percent[id] = percent; }
  post_refresh();
}
// One job per mod. Returns the cancel flag, or null when that mod is already busy.
std::shared_ptr<std::atomic<bool>> begin_job(const std::string& id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_jobs.count(id)) return nullptr;
  auto flag = std::make_shared<std::atomic<bool>>(false);
  g_jobs[id] = flag;
  return flag;
}
void end_job(const std::string& id) {
  { std::lock_guard<std::mutex> lock(g_mutex); g_jobs.erase(id); g_percent.erase(id); }
  post_refresh();
}
bool cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }
fs::path launcher_dir() { return fs::u8path(g_dir); }
// Runs a program hidden and waits. Returns its exit code, or -1 when it could not start or was cancelled.
int run_hidden(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd,
               const std::atomic<bool>* cancel = nullptr, DWORD timeout_ms = 30 * 60 * 1000) {
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) return -1;
  CloseHandle(pi.hThread);
  DWORD code = (DWORD)-1, wait = WAIT_TIMEOUT;
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (wait == WAIT_TIMEOUT && GetTickCount64() < deadline && !cancelled(cancel)) wait = WaitForSingleObject(pi.hProcess, 200);
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); }
  CloseHandle(pi.hProcess);
  return (int)code;
}
bool enough_space(uint64_t need, std::string* why) {
  ULARGE_INTEGER free_bytes{};
  const std::wstring dir = widen(work_dir());
  if (!GetDiskFreeSpaceExW(dir.c_str(), &free_bytes, nullptr, nullptr)) { *why = "Could not check free disk space. Nothing was installed."; return false; }
  if (free_bytes.QuadPart >= need) return true;
  *why = launcher::lang::fill(tx("Not enough free disk space: this needs {need} and the drive has {free}. Nothing was installed."),
                              launcher::lang::Args{{"need", size_text(need)}, {"free", size_text(free_bytes.QuadPart)}});
  return false;
}

// HTTPS GET. `to_file` empty: returns the body in *body. Reports progress for downloads.
bool https_get(const std::wstring& host, const std::wstring& path, std::string* body, const std::wstring& to_file,
               const std::string& id = "", uint64_t expected = 0, const std::atomic<bool>* cancel = nullptr) {
  HINTERNET session = WinHttpOpen(L"MeleeUnlocked-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
  bool ok = false;
  std::wstring h = host, p = path;
  for (int redirects = 0; redirects < 6 && !ok; ++redirects) {
    HINTERNET connect = WinHttpConnect(session, h.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) break;
    HINTERNET req = WinHttpOpenRequest(connect, L"GET", p.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
    DWORD no_redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (req) WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &no_redirect, sizeof no_redirect);
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nUser-Agent: MeleeUnlocked-Launcher\r\n";
    DWORD status = 0, len = sizeof status;
    if (req && WinHttpSendRequest(req, headers, (DWORD)-1, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr)) {
      if (status >= 300 && status < 400) {
        wchar_t location[4096]{}; DWORD size = sizeof location;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, nullptr, location, &size, nullptr)) {
          URL_COMPONENTS u{}; u.dwStructSize = sizeof u; wchar_t uh[256]{}, up[3800]{};
          u.lpszHostName = uh; u.dwHostNameLength = 256; u.lpszUrlPath = up; u.dwUrlPathLength = 3800;
          wchar_t extra[2048]{}; u.lpszExtraInfo = extra; u.dwExtraInfoLength = 2048;
          if (WinHttpCrackUrl(location, 0, 0, &u) && u.nScheme == INTERNET_SCHEME_HTTPS &&
              u.nPort == INTERNET_DEFAULT_HTTPS_PORT && !u.dwUserNameLength && !u.dwPasswordLength) { h = uh; p = std::wstring(up) + extra; }
          else { WinHttpCloseHandle(req); WinHttpCloseHandle(connect); break; }
        }
      } else if (status == 200) {
        std::ofstream out;
        if (!to_file.empty()) out.open(to_file, std::ios::binary);
        uint64_t got = 0; ULONGLONG last = 0;
        std::vector<char> buf(1 << 20);
        ok = to_file.empty() || (bool)out;
        while (ok) {
          if (cancelled(cancel)) { ok = false; break; }
          DWORD n = 0;
          if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &n)) { ok = false; break; }
          if (!n) break;
          got += n;
          if ((to_file.empty() && got > (1u << 20)) || (!to_file.empty() && got > expected)) { ok = false; break; }
          if (to_file.empty()) body->append(buf.data(), n);
          else { out.write(buf.data(), n); ok = (bool)out; }
          if (!id.empty() && expected && GetTickCount64() - last > 300) {
            last = GetTickCount64();
            const int percent = (int)(got * 100 / expected);
            set_percent(id, percent);
            set_status(id, tx("Downloading...") + " " + std::to_string(percent) + "% (" + size_text(got) + " / " + size_text(expected) + ")");
          }
        }
        if (!to_file.empty()) { out.close(); ok = ok && (bool)out && got == expected; }
        if (!ok) { WinHttpCloseHandle(req); WinHttpCloseHandle(connect); break; }
      } else {
        if (req) WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        break;
      }
    }
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return ok;
}

// Detection is shared with the game's content scanner, including externally linked discs. A disc
// whose whole-file hash is a catalog build's official result is that catalog mod, also when its
// header cannot tell it apart from another build (ACE's disc names itself Akaneia).
std::map<std::string, Installed> installed() {
  std::ifstream in(fs::u8path(mods_dir() + "/.cache/detected.json"), std::ios::binary);
  const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<CatalogMod> catalog;
  { std::lock_guard<std::mutex> lock(g_mutex); catalog = g_catalog; }
  std::map<std::string, Installed> out;
  for (auto m : catalog_core::detected(contents)) {
    const fs::path path = fs::u8path(m.path);
    m.path = (path.is_absolute() ? path : fs::u8path(work_dir()) / path).u8string();
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(m.path), ec)) continue;
    for (const auto& c : catalog)
      if (!c.output_sha256.empty() && _stricmp(c.output_sha256.c_str(), m.hash.c_str()) == 0) {
        m.id = c.id;
        if (m.version.empty() || m.name.rfind("m-ex mod", 0) == 0) { m.name = c.name; m.version = c.version; }
      }
    if (out.count(m.id)) {
      if (out[m.id].hash == m.hash) continue;
      m.id = m.hash.substr(0,16);
      if (!catalog_core::valid_id(m.id) || out.count(m.id)) continue;
    }
    out[m.id] = std::move(m);
  }
  return out;
}
// A scan is running. Until it ends, what the page shows comes from the last scan, which after an
// update is the previous version's verdict: a disc that version could not run would read "Not
// supported yet" for as long as the new scan takes to read the disc again.
std::atomic<int> g_scans_running{0};
bool scanning() { return g_scans_running.load() > 0; }
void scan_installed() {
  struct Running { Running() { ++g_scans_running; post_refresh(); } ~Running() { --g_scans_running; post_refresh(); } } running;
  source_port::mods::ScanOptions options;
  options.mods_dir = fs::u8path(mods_dir()); options.base_iso = fs::u8path(g_iso);
  options.source_port = g_engine == ENGINE_SOURCE;
  std::map<std::string, int> choices;
  std::ifstream in(fs::u8path(settings_ini_path())); std::string line;
  while (std::getline(in, line)) {
    std::istringstream row(line); std::string key, value; row >> key >> std::ws; std::getline(row, value);
    source_port::mods::parse_choice(key, value, &choices);
  }
  in.close();  // Windows cannot atomically replace settings while this read handle is open.
  const auto previous_choices = choices;
  const auto found = source_port::mods::scan(options, &choices);
  for (const auto& choice : choices) {
    const auto previous = previous_choices.find(choice.first);
    if (previous != previous_choices.end() && previous->second == choice.second) continue;
    std::string error;
    if (!catalog_core::enable_pack(fs::u8path(settings_ini_path()), choice.first, choice.second != 0, &error))
      set_status("drop", error);
  }
  source_port::mods::write_detected_json(options.mods_dir / ".cache/detected.json", found, options.source_port);
  post_refresh();
  if (g_main) InvalidateRect(g_main, nullptr, FALSE);
}
void rescan() { std::lock_guard<std::mutex> lock(g_install_mutex); scan_installed(); }
std::string summary() {
  std::string packs;
  for (const auto& entry : installed()) {
    const auto& m = entry.second;
    if (m.enabled && runs_here(m)) packs += " " + m.name + " \xE2\x9C\x93";
  }
  return packs.empty() ? tx("Mods: none detected") : tx("Mods:") + packs;
}
bool vanilla_iso_ok(const std::string& iso, std::string* why) {
  if (iso.empty() || !file_exists(iso)) { *why = "Choose your Melee disc image on the Play page first."; return false; }
  if (hash_file(widen(iso), BCRYPT_MD5_ALGORITHM) != kVanillaMd5) {
    *why = "Choose an unmodified Melee NTSC 1.02 disc. Mods are built from your vanilla disc."; return false;
  }
  return true;
}
std::wstring find_file(const std::wstring& dir, const std::wstring& suffix) {
  std::error_code ec;
  fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
    if (!it->is_regular_file(ec)) continue;
    std::wstring name = it->path().filename().wstring();
    for (auto& c : name) c = (wchar_t)towlower(c);
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
      return it->path().wstring();
  }
  return {};
}
// Turns an installed catalog mod on (a fresh Get or Update is always on, also after an earlier Off).
void enable_installed(const std::string& id) {
  const auto found = installed();
  const auto it = found.find(id);
  if (it == found.end() || it->second.enabled || !runs_here(it->second)) return;
  std::string error;
  if (catalog_core::enable_pack(fs::u8path(settings_ini_path()), it->second.key, true, &error)) scan_installed();
}
// Moves a verified staged file into place. Same volume (both under Mods), so no second 1.7 GB copy.
bool place(const fs::path& staged, const fs::path& to, std::string* error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  if (MoveFileExW(staged.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
  if (catalog_core::copy_pack(staged, to, true, error)) { fs::remove(staged, ec); return true; }
  return false;
}
void install_file_job(std::wstring file, std::string id, const std::string& iso, const std::atomic<bool>* cancel) {
  std::lock_guard<std::mutex> operation(g_install_mutex);
  if (cancelled(cancel)) { set_status(id.empty() ? "drop" : id, "Cancelled. Nothing was installed."); return; }
  std::wstring lower = file; for (auto& c : lower) c = (wchar_t)towlower(c);
  auto ends = [&](const wchar_t* s) { const size_t n = wcslen(s); return lower.size() >= n && lower.compare(lower.size() - n, n, s) == 0; };
  const std::string key = id.empty() ? "drop" : id;
  const fs::path mods = fs::u8path(mods_dir());
  std::string error; std::error_code ec;
  const std::string previous = [&] { const auto f = installed(); const auto it = f.find(id); return it == f.end() ? std::string() : it->second.path; }();
  if (ends(L".gci") || ends(L".iso") || ends(L".gcm")) {
    set_status(key, "Copying into Mods...");
    if (cancelled(cancel)) { set_status(key, "Cancelled. Nothing was installed."); return; }
    fs::path out = mods / (ends(L".gci") ? "Saves" : "Discs") / fs::path(file).filename();
    if (id.empty() && fs::exists(out, ec) && !fs::equivalent(file, out, ec) &&
        catalog_core::sha256_file(file) != catalog_core::sha256_file(out)) {
      const fs::path original = out;
      for (unsigned suffix = 2; fs::exists(out, ec); ++suffix)
        out = original.parent_path() / fs::u8path(original.stem().u8string() + " (" + std::to_string(suffix) + ")" + original.extension().u8string());
    }
    if (!catalog_core::copy_pack(file, out, true, &error, cancel)) { set_status(key, error); return; }
    scan_installed();
    if (!id.empty()) enable_installed(id);
    set_status(key, id.empty() ? "Added to Mods. Online compatibility is unverified; different builds can desync." : "Installed and turned on.");
    return;
  }
  std::vector<CatalogMod> catalog;
  { std::lock_guard<std::mutex> lock(g_mutex); catalog = g_catalog; }
  set_status(key, "Checking the file...");
  const std::string hash = catalog_core::sha256_file(file);
  if (cancelled(cancel)) { set_status(key, "Cancelled. Nothing was installed."); return; }
  const CatalogMod* match = catalog_core::matching_archive(catalog, hash);
  const bool bare_patch = !match && ends(L".xdelta");
  if (bare_patch)   // an official patch dropped on its own, recognized by its pinned hash
    for (const auto& m : catalog) if (!m.patch_sha256.empty() && _stricmp(m.patch_sha256.c_str(), hash.c_str()) == 0) match = &m;
  if (!match || (!id.empty() && match->id != id)) {
    set_status(key, "This archive is not in the verified catalog. Drop a finished .iso or .gci instead."); return;
  }
  const CatalogMod mod = *match;
  if (!catalog_core::valid_sha256(mod.tool_sha256) || !catalog_core::valid_sha256(mod.patch_sha256)) {
    set_status(key, "This archive's patcher has not been verified yet. Build it yourself and drop the finished .iso here."); return;
  }
  if (!bare_patch && !ends(L".zip") && !ends(L".7z")) { set_status(key, "Drop the complete verified archive or a finished disc."); return; }
  set_status(key, "Checking your Melee disc...");
  if (!vanilla_iso_ok(iso, &error)) { set_status(key, error); return; }
  const uint64_t file_size = fs::file_size(fs::path(file), ec);
  if (!enough_space((bare_patch ? 0 : file_size + file_size / 8) + kDiscBytes + 256 * kMiB, &error)) { set_status(key, error); return; }
  const fs::path work = mods / ".downloads" / (mod.id + "-extract-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
  fs::create_directories(work, ec);
  if (ec) { set_status(key, "Could not create the mod build folder."); return; }
  struct Cleanup { fs::path dir; ~Cleanup() { std::error_code e; fs::remove_all(dir, e); } } cleanup{work};
  fs::path patch = bare_patch ? fs::path(file) : fs::path();
  if (!bare_patch) {
    set_status(key, "Unpacking the verified archive...");
    int code = -1;
    if (ends(L".7z")) {
      const fs::path seven = catalog_core::shipped_tool(launcher_dir(), kSevenZipTool, k7zrSha256);
      if (seven.empty()) { set_status(key, "The 7-Zip tool is missing from the launcher's tools folder (tools\\7zr.exe). Reinstall Melee Unlocked to get it."); return; }
      code = run_hidden(seven.wstring(), L"x -y -bd \"-o" + work.wstring() + L"\" \"" + file + L"\"", work.wstring(), cancel);
    } else {
      wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
      code = run_hidden(std::wstring(sys) + L"\\tar.exe", L"-xf \"" + file + L"\" -C \"" + work.wstring() + L"\"", work.wstring(), cancel);
    }
    if (cancelled(cancel)) { set_status(key, "Cancelled. Nothing was installed."); return; }
    if (code != 0) { set_status(key, "The archive could not be unpacked. Nothing was installed."); return; }
    patch = find_file(work.wstring(), L".xdelta");
  }
  // The shipped tool first; the one inside the download only when its bytes are the pinned ones.
  fs::path tool = catalog_core::shipped_tool(launcher_dir(), kXdeltaTool, mod.tool_sha256);
  if (tool.empty() && !bare_patch) tool = catalog_core::pinned_xdelta(work, mod.tool_sha256);
  if (tool.empty()) { set_status(key, "The xdelta3 tool is missing from the launcher's tools folder (tools\\xdelta3.exe). Reinstall Melee Unlocked to get it."); return; }
  if (patch.empty()) { set_status(key, "The verified patch was not found. Nothing was installed."); return; }
  const fs::path staged = work / "result.iso";
  set_status(key, "Building the modded disc from your Melee disc...");
  if (!catalog_core::apply_delta(tool, mod.tool_sha256, patch, mod.patch_sha256, fs::u8path(iso), staged, &error, cancel)) {
    set_status(key, error); return;
  }
  set_status(key, "Checking the finished disc...");
  if ((!mod.output_md5.empty() && hash_file(staged.wstring(), BCRYPT_MD5_ALGORITHM) != mod.output_md5) ||
      (!mod.output_sha256.empty() && catalog_core::sha256_file(staged) != mod.output_sha256)) {
    set_status(key, "The built disc did not match the official result. Your installed disc was kept."); return;
  }
  if (cancelled(cancel)) { set_status(key, "Cancelled. Nothing was installed."); return; }
  const fs::path target = mods / "Discs" / (mod.id + ".iso");
  if (!place(staged, target, &error)) { set_status(key, error); return; }
  // An Update replaces the old disc; a different managed file of the same mod is removed (a linked
  // original only loses its link).
  if (!previous.empty() && fs::weakly_canonical(fs::u8path(previous), ec) != fs::weakly_canonical(target, ec))
    catalog_core::remove_pack(fs::u8path(work_dir()), fs::u8path(previous), &error);
  scan_installed();
  enable_installed(mod.id);
  const auto detected = installed(); const auto current = detected.find(mod.id);
  set_status(key, current != detected.end() && current->second.enabled ? "Installed and turned on. Saves were kept." : "Installed. This version is not supported yet. Saves were kept.");
}
void install_file(std::wstring file, std::string id, std::string iso) {
  // Each dropped file is a job; a Get uses its own cancel flag through download_and_install.
  static std::atomic<unsigned> drop_serial{0};
  const std::string job = id.empty() ? "drop-" + std::to_string(drop_serial.fetch_add(1)) : id;
  auto flag = begin_job(job);
  if (!flag) { set_status(id.empty() ? "drop" : id, "Already working on this mod."); return; }
  install_file_job(std::move(file), id, iso, flag.get());
  end_job(job);
}
void download_and_install(CatalogMod mod, std::string iso) {
  auto flag = begin_job(mod.id);
  if (!flag) return;
  struct Done { std::string id; ~Done() { end_job(id); } } done{mod.id};
  const std::atomic<bool>* cancel = flag.get();
  if (!mod.one_click) { set_status(mod.id, "Use the official download page for this mod."); return; }
  if (!mod.patch_sha256.empty() && catalog_core::shipped_tool(launcher_dir(), kXdeltaTool, mod.tool_sha256).empty()) {
    set_status(mod.id, "The xdelta3 tool is missing from the launcher's tools folder (tools\\xdelta3.exe). Reinstall Melee Unlocked to get it."); return;
  }
  if (mod.kind == "xdelta_7z" && catalog_core::shipped_tool(launcher_dir(), kSevenZipTool, k7zrSha256).empty()) {
    set_status(mod.id, "The 7-Zip tool is missing from the launcher's tools folder (tools\\7zr.exe). Reinstall Melee Unlocked to get it."); return;
  }
  std::string error;
  if (!mod.patch_sha256.empty() && !vanilla_iso_ok(iso, &error)) { set_status(mod.id, error); return; }
  set_status(mod.id, "Checking the official release...");
  std::string body;
  const std::wstring api = L"/repos/" + widen(mod.repo) + (mod.tag.empty() ? L"/releases/latest" : L"/releases/tags/" + widen(mod.tag));
  if (!https_get(L"api.github.com", api, &body, L"", "", 0, cancel)) {
    set_status(mod.id, cancelled(cancel) ? "Cancelled. Nothing was installed." : "Could not reach the official release."); return;
  }
  const Json release = Json::parse(body, nullptr, false);
  if (!release.is_object() || !release.count("assets") || !release["assets"].is_array()) { set_status(mod.id, "The release could not be read."); return; }
  Json asset;
  for (const auto& a : release["assets"]) if (a.is_object() && a.value("name", std::string()) == mod.asset_pattern) { asset = a; break; }
  if (asset.is_null()) { set_status(mod.id, "The verified release file was not found."); return; }
  const std::string url = asset.value("browser_download_url", std::string()), name = asset.value("name", std::string());
  const uint64_t size = asset.value("size", (uint64_t)0);
  std::string digest;
  const std::string official = asset.count("digest") && asset["digest"].is_string() ? asset["digest"].get<std::string>() : "";
  if (!catalog_core::download_digest(official, mod.sha256, &digest, &error)) { set_status(mod.id, error); return; }
  if (name.empty() || name.find_first_of("/\\:\"\r\n") != std::string::npos || name == "." || name == ".." ||
      !size || size > 2ull * kGiB || (!mod.url.empty() && url != mod.url)) {
    set_status(mod.id, "The release metadata changed. A catalog update is needed."); return;
  }
  // Archive + its unpacked patch + the built disc, with some room to spare.
  const uint64_t need = mod.kind == "gci" ? size + 64 * kMiB : size + size + size / 8 + kDiscBytes + 256 * kMiB;
  if (!enough_space(need, &error)) { set_status(mod.id, error); return; }
  std::error_code ec;
  const fs::path folder = fs::u8path(mods_dir()) / ".downloads" / (mod.id + "-" + std::to_string(GetTickCount64()));
  fs::create_directories(folder, ec); if (ec) { set_status(mod.id, "Could not create the download folder."); return; }
  struct Cleanup { fs::path dir; ~Cleanup() { std::error_code e; fs::remove_all(dir, e); } } cleanup{folder};
  const fs::path file = folder / name;
  URL_COMPONENTS u{}; u.dwStructSize = sizeof u; wchar_t host[256]{}, path[3800]{};
  u.lpszHostName = host; u.dwHostNameLength = 256; u.lpszUrlPath = path; u.dwUrlPathLength = 3800;
  const std::wstring wurl = widen(url);
  set_percent(mod.id, 0);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS ||
      u.nPort != INTERNET_DEFAULT_HTTPS_PORT || u.dwUserNameLength || u.dwPasswordLength ||
      !https_get(host, path, nullptr, file.wstring(), mod.id, size, cancel)) {
    set_percent(mod.id, -1);
    set_status(mod.id, cancelled(cancel) ? "Cancelled. Nothing was installed." : "The download failed. Nothing was installed."); return;
  }
  set_percent(mod.id, -1);
  set_status(mod.id, "Checking the download...");
  if (catalog_core::sha256_file(file) != digest) {
    set_status(mod.id, "The download did not match the verified release. Nothing was installed."); return;
  }
  if (mod.sha256.empty()) {   // keep the API's verified digest for dropped-archive matching
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& m : g_catalog) if (m.id == mod.id) m.sha256 = digest;
  }
  if (cancelled(cancel)) { set_status(mod.id, "Cancelled. Nothing was installed."); return; }
  install_file_job(file.wstring(), mod.id, iso, cancel);
}
void refresh_catalog() {
  std::string body, error;
  if (!https_get(kCatalogHost, kCatalogPath, &body, L"")) return;
  std::vector<CatalogMod> newer;
  if (!catalog_core::parse(body, &newer, &error)) return;
  { std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& m : newer) for (const auto& old : g_catalog) if (m.id == old.id) {
      if (m.credits.empty()) m.credits = old.credits;
      if (m.note.empty()) m.note = old.note;
    }
    g_catalog = std::move(newer);
  }
  post_refresh();
}
bool tools_present() {
  std::error_code ec;
  return fs::is_regular_file(launcher_dir() / "tools" / kXdeltaTool, ec) && fs::is_regular_file(launcher_dir() / "tools" / kSevenZipTool, ec);
}

// ---- the page ----
enum { ID_MOD_BUTTON = 3000, ID_MOD_UPDATE = 4000, ID_MOD_REMOVE = 5000, ID_MOD_TOGGLE = 6000,
       ID_MOD_FOLDER = 7000, ID_MOD_LINK, ID_MOD_SKINS = 8000 };
struct Row { CatalogMod catalog; Installed installed; bool found = false; };
int disc_skins(const std::string& disc_path, std::string* sets);
std::vector<Row> g_rows;
std::vector<HWND> g_buttons, g_updates, g_removes, g_toggles, g_skins;
HWND g_folder_btn = nullptr, g_link_btn = nullptr;
int g_top = 0;
bool g_tools_ok = true;
// Layout units (S() turns them into pixels), relative to the panel's client area.
constexpr int kPad = 20, kHeader = 78, kCardH = 126, kCardGap = 8, kFooter = 84;
const COLORREF C_CARD = RGB(0x14, 0x1C, 0x2C), C_CARD_BORDER = RGB(0x27, 0x31, 0x4A);
enum { STYLE_ACCENT = 1, STYLE_ON = 2 };
int units(int px) { return MulDiv(px, 96, g_dpi); }
int visible_rows() {
  RECT r{}; if (g_window) GetClientRect(g_window, &r);
  return std::max(1, (units(r.bottom) - kHeader - kFooter + kCardGap) / (kCardH + kCardGap));
}
RECT card_rect(int slot) {
  RECT r{}; GetClientRect(g_window, &r);
  return LR(kPad, kHeader + slot * (kCardH + kCardGap), units(r.right) - 2 * kPad, kCardH);
}
bool is_busy(const std::string& id) { std::lock_guard<std::mutex> lock(g_mutex); return g_jobs.count(id) != 0; }
bool same_version(std::string a, std::string b) {
  auto norm = [](std::string s) { std::string o; for (char c : s) if (c != ' ' && c != 'v' && c != 'V') o += (char)tolower((unsigned char)c); return o; };
  return norm(a) == norm(b);
}
void refresh_window() {
  if (!g_window) return;
  const auto found = installed();
  std::vector<CatalogMod> catalog;
  { std::lock_guard<std::mutex> lock(g_mutex); catalog = g_catalog; }
  g_rows.clear(); std::set<std::string> listed;
  for (const auto& m : catalog) {
    Row row; row.catalog = m;
    const auto f = found.find(m.id);
    if (f != found.end()) { row.installed = f->second; row.found = true; listed.insert(f->first); }
    else if (!offered_here(m)) continue;   // nothing to Get for a mod this launcher cannot start
    g_rows.push_back(std::move(row));
  }
  for (const auto& f : found) if (!listed.count(f.first)) {
    Row row; row.found = true; row.installed = f.second; row.catalog.id = f.first; row.catalog.name = f.second.name;
    g_rows.push_back(std::move(row));
  }
  while (g_buttons.size() < g_rows.size()) {
    const int i = (int)g_buttons.size();
    auto make = [&](int id) {
      HWND h = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, 0, 0, S(10), S(10), g_window,
                               (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); return h;
    };
    g_buttons.push_back(make(ID_MOD_BUTTON + i)); g_updates.push_back(make(ID_MOD_UPDATE + i));
    g_removes.push_back(make(ID_MOD_REMOVE + i)); g_toggles.push_back(make(ID_MOD_TOGGLE + i));
    g_skins.push_back(make(ID_MOD_SKINS + i));
  }
  const int shown = visible_rows();
  g_top = std::max(0, std::min(g_top, std::max(0, (int)g_rows.size() - shown)));
  SCROLLINFO scroll{sizeof scroll, SIF_RANGE | SIF_PAGE | SIF_POS}; scroll.nMax = (int)g_rows.size() - 1; scroll.nPage = shown; scroll.nPos = g_top;
  SetScrollInfo(g_window, SB_VERT, &scroll, TRUE);
  for (size_t i = 0; i < g_buttons.size(); ++i) {
    const bool visible = i < g_rows.size() && (int)i >= g_top && (int)i < g_top + shown;
    HWND controls[] = {g_buttons[i], g_updates[i], g_removes[i], g_toggles[i], g_skins[i]};
    if (!visible) { for (HWND h : controls) ShowWindow(h, SW_HIDE); continue; }
    const RECT card = card_rect((int)i - g_top);
    const int right = units(card.right) - 16, top = units(card.top);
    MoveWindow(g_toggles[i], S(right - 84), S(top + 12), S(84), S(28), TRUE);
    MoveWindow(g_removes[i], S(right - 84), S(top + 92), S(84), S(28), TRUE);
    MoveWindow(g_updates[i], S(right - 176), S(top + 92), S(84), S(28), TRUE);
    MoveWindow(g_buttons[i], S(right - 308), S(top + 92), S(124), S(28), TRUE);
    MoveWindow(g_skins[i], S(right - 428), S(top + 92), S(112), S(28), TRUE);
    const auto& row = g_rows[i]; const auto& m = row.catalog; const auto& f = row.installed;
    // "Choose skins" opens the Settings window on the Mods tab, where the disc's costumes are picked.
    // Only once the game has listed them (disc-skins.txt), so the button never leads to an empty list.
    const bool has_skins = row.found && disc_skins(f.path, nullptr) > 0;
    SetWindowTextW(g_skins[i], launcher::lang::txw(L"Choose skins").c_str());
    EnableWindow(g_skins[i], has_skins && !g_playing);
    const bool busy = is_busy(m.id), playable = row.found && runs_here(f);
    std::string primary; bool enabled = true; LONG_PTR style = 0;
    if (busy) primary = "Cancel";
    else if (row.found) { primary = playable ? "Play" : scanning() ? "Checking..." : "Not supported yet"; enabled = playable && !g_playing; style = STYLE_ACCENT; }
    else if (m.one_click) { primary = "Get"; style = STYLE_ACCENT; enabled = !m.url.empty() || !m.repo.empty(); }
    else { primary = "Open download page"; enabled = !m.page.empty(); }
    SetWindowTextW(g_buttons[i], widen(tx(primary)).c_str());
    SetWindowLongPtrW(g_buttons[i], GWLP_USERDATA, style);
    EnableWindow(g_buttons[i], enabled);
    const bool current = row.found && !m.version.empty() && same_version(m.version, f.version);
    SetWindowTextW(g_updates[i], launcher::lang::txw(current ? L"Up to date" : L"Update").c_str());
    EnableWindow(g_updates[i], row.found && !busy && m.one_click && !m.version.empty() && !current && !g_playing);
    SetWindowTextW(g_removes[i], launcher::lang::txw(L"Remove").c_str());
    EnableWindow(g_removes[i], row.found && !busy && !g_playing);
    SetWindowTextW(g_toggles[i], launcher::lang::txw(f.enabled && row.found ? L"On" : L"Off").c_str());
    SetWindowLongPtrW(g_toggles[i], GWLP_USERDATA, row.found && f.enabled ? STYLE_ON : 0);
    EnableWindow(g_toggles[i], playable && !busy && !g_playing);
    for (HWND h : controls) { ShowWindow(h, h == g_skins[i] && !has_skins ? SW_HIDE : SW_SHOW); InvalidateRect(h, nullptr, FALSE); }
  }
  InvalidateRect(g_window, nullptr, FALSE);
}
void draw_check(HDC dc, int x, int y, bool on) {
  RECT box = LR(x, y, 18, 18);
  if (!on) { round_rect(dc, box, 9, NO_FILL, NO_FILL, C_FAINT); return; }
  round_rect(dc, box, 9, C_OK, C_OK, NO_FILL);
  HPEN pen = CreatePen(PS_SOLID, S(2), RGB(0x0E, 0x1A, 0x14));
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, S(x + 5), S(y + 9), nullptr); LineTo(dc, S(x + 8), S(y + 12)); LineTo(dc, S(x + 13), S(y + 6));
  SelectObject(dc, old); DeleteObject(pen);
}
// How many of a mod disc's costumes the game listed as skins (it writes Mods/.cache/disc-skins.txt
// at boot: path, tab, count, tab, the alternate sets by name such as "alt L,alt R"). 0 until the
// game has run once with the disc in the Mods folder. *sets gets the set names, "" when it has none.
int disc_skins(const std::string& disc_path, std::string* sets) {
  std::ifstream in(fs::u8path(mods_dir() + "/.cache/disc-skins.txt"), std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t first = line.find('\t');
    if (first == std::string::npos) continue;
    const size_t second = line.find('\t', first + 1);
    std::error_code ec;
    if (!fs::equivalent(fs::u8path(line.substr(0, first)), fs::u8path(disc_path), ec) || ec) continue;
    if (sets) *sets = second == std::string::npos ? std::string() : line.substr(second + 1);
    return std::atoi(line.c_str() + first + 1);
  }
  return 0;
}
// "alt L and alt R sets" from "alt L,alt R"; empty when the disc has no alternate sets.
std::string sets_text(const std::string& sets) {
  if (sets.empty()) return {};
  std::string text;
  size_t at = 0;
  while (at <= sets.size()) {
    const size_t comma = sets.find(',', at);
    const std::string one = sets.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!text.empty()) text += comma == std::string::npos ? " and " : ", ";
    text += one;
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return text + (sets.find(',') == std::string::npos ? " set" : " sets");
}
void paint_page(HDC dc) {
  RECT client{}; GetClientRect(g_window, &client);
  vgrad(dc, client, C_CONTENT_TOP, C_CONTENT_BOT);
  const int width = units(client.right), height = units(client.bottom);
  draw_text(dc, L"MODS", LR(kPad, 26, width - 2 * kPad, 18), g_font_label, C_FAINT, DT_LEFT | DT_SINGLELINE | DT_VCENTER, S(1));
  draw_text(dc, L"Get downloads each mod from its official release and builds it from your own Melee disc.",
            LR(kPad, 46, width - 2 * kPad, 18), g_font_small, C_DIM, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  std::map<std::string, std::string> status; std::map<std::string, int> percent;
  { std::lock_guard<std::mutex> lock(g_mutex); status = g_status; percent = g_percent; }
  const int shown = visible_rows();
  for (int slot = 0; slot < shown && g_top + slot < (int)g_rows.size(); ++slot) {
    const Row& row = g_rows[g_top + slot];
    const auto& m = row.catalog; const auto& f = row.installed;
    const RECT card = card_rect(slot);
    round_rect(dc, card, 10, C_CARD, C_CARD, C_CARD_BORDER);
    const int x = units(card.left), y = units(card.top), text_w = units(card.right) - 16 - 100 - (x + 44);
    draw_check(dc, x + 16, y + 14, row.found);
    // Name, then the catalog's latest version beside it.
    RECT name = LR(x + 44, y + 10, text_w, 22);
    const std::wstring title = widen(m.name);
    HGDIOBJ old = SelectObject(dc, g_font_nav); RECT measure = name;
    DrawTextW(dc, title.c_str(), -1, &measure, DT_CALCRECT | DT_SINGLELINE); SelectObject(dc, old);
    draw_text(dc, title, name, g_font_nav, C_TEXT, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!m.version.empty() && measure.right + S(8) < name.right)
      draw_text(dc, widen(launcher::lang::fill(tx("Latest {version}"), launcher::lang::Args{{"version", m.version}})),
                RECT{measure.right + S(8), name.top, name.right, name.bottom}, g_font_small, C_FAINT, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (!m.credits.empty())
      draw_text(dc, widen(m.credits), LR(x + 44, y + 32, text_w, 16), g_font_small, C_FAINT, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    else if (row.found)
      draw_text(dc, widen(tx("Custom mod")), LR(x + 44, y + 32, text_w, 16), g_font_small, C_FAINT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    std::string state; COLORREF state_color = C_DIM;
    if (row.found) {
      state = "\xE2\x9C\x93 " + (f.version.empty() ? tx("Installed") : launcher::lang::fill(tx("Installed {version}"), launcher::lang::Args{{"version", f.version}}));
#ifndef MELEE_NO_SLIPPI   // with one Game Build there is no engine to name
      state += " | " + std::string(f.needs_engine == "source" ? "Source Port" : f.needs_engine == "recomp" ? "Static Recomp" : tx("Both engines"));
#endif
      if (runs_here(f)) state += " | " + tx(f.enabled ? "On" : "Off");
      if (f.status == "untested") state += " | " + tx("Untested");
      state_color = f.status == "supported" ? C_OK : C_WARN;
    } else state = tx(m.one_click ? "Not installed" : "Not installed. Put the file in the Mods folder.");
    draw_text(dc, widen(state), LR(x + 44, y + 50, text_w, 18), g_font, state_color, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    // Last line: what is happening now, else what runs this mod.
    std::string line; COLORREF line_color = C_DIM;
    if (status.count(m.id) && (is_busy(m.id) || status[m.id].rfind("Installed", 0) != 0)) { line = tx(status[m.id]); line_color = C_TEXT; }
    else if (row.found && !runs_here(f) && scanning()) { line = tx("Checking this disc..."); line_color = C_DIM; }
#ifdef MELEE_NO_SLIPPI   // the note of such a mod names an engine this launcher does not have
    else if (row.found && !runs_here(f)) { line = tx("This version is not supported yet."); line_color = C_WARN; }
#else
    else if (row.found && !runs_here(f)) { line = m.note.empty() ? tx("This version is not supported yet.") : tx(m.note); line_color = C_WARN; }
#endif
    else if (row.found && f.kind != "te" && f.kind != "tmce") {
      // Once the game has listed the disc's costumes as skins, say how many and where to pick them.
      std::string sets;
      const int skins = disc_skins(f.path, &sets);
      if (skins > 0) {
        const std::string named = sets_text(sets);
        line = launcher::lang::fill(tx("Skins: {count} in your skin list{sets}. Pick them in Settings, Mods."),
                                    launcher::lang::Args{{"count", std::to_string(skins)}, {"sets", named.empty() ? "" : " (" + named + ")"}});
        line_color = C_DIM;
      }
#ifndef MELEE_NO_SLIPPI   // both lines name an engine and online modes the build without that layer does not have: no line there
      else if (f.kind == "hack_pack") {
        // This pack has no online play at all, so the Direct line would be wrong for it.
        line = tx(m.note.empty() ? "Plays on the Static Recomp, offline (no online play or replays)." : m.note);
        line_color = C_DIM;
      } else {
        line = tx(f.needs_engine == "recomp" ? "Direct only. Same mod required; different builds can desync. Launch vanilla for Unranked."
                                           : "Direct needs matching mods; different builds can desync. Source Port uses vanilla for Unranked.");
        line_color = C_WARN;
      }
#endif
    }
    else if (!m.note.empty()) { line = tx(m.note); line_color = m.note.find("nsupported") != std::string::npos || m.note.find("ntested") != std::string::npos || m.note.find("not supported") != std::string::npos ? C_WARN : C_DIM; }
    RECT last = LR(x + 44, y + 70, units(card.right) - 16 - (x + 44), 16);
    if (percent.count(m.id)) {
      RECT track = LR(x + 44, y + 86, text_w, 4), done = track;
      round_rect(dc, track, 2, C_FIELD, C_FIELD, NO_FILL);
      done.right = done.left + (track.right - track.left) * std::clamp(percent[m.id], 0, 100) / 100;
      if (done.right > done.left + S(4)) round_rect(dc, done, 2, C_ACC_HI, C_ACC_LO, NO_FILL);
      last.right = S(x + 44 + text_w);
    }
    draw_text(dc, widen(line), last, g_font_small, line_color, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  // Footer: drops, and the tools check.
  std::string note = tx("Drop a .zip, .7z, .xdelta, .iso or .gci anywhere on the launcher. Saves stay when you remove a pack.");
  COLORREF note_color = C_FAINT;
  if (status.count("drop")) { note = tx(status["drop"]); note_color = C_TEXT; }
  else if (!g_tools_ok) { note = tx("The mod tools are missing (tools\\xdelta3.exe, tools\\7zr.exe). Reinstall Melee Unlocked to use Get."); note_color = C_WARN; }
  draw_text(dc, widen(note), LR(kPad, height - kFooter + 10, width - 2 * kPad, 30), g_font_small, note_color, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
}
void draw_item(DRAWITEMSTRUCT* di) {
  RECT wr; GetWindowRect(di->hwndItem, &wr); MapWindowPoints(nullptr, g_window, (POINT*)&wr, 2);
  RECT client{}; GetClientRect(g_window, &client);
  const int base = (int)(di->CtlID / 1000) * 1000;
  const bool footer = di->CtlID == ID_MOD_FOLDER || di->CtlID == ID_MOD_LINK;
  const RECT r = di->rcItem;
  if (footer) vgrad(di->hDC, r, lerp(C_CONTENT_TOP, C_CONTENT_BOT, wr.top, client.bottom - 1), lerp(C_CONTENT_TOP, C_CONTENT_BOT, wr.bottom, client.bottom - 1));
  else fill(di->hDC, r, C_CARD);
  const bool disabled = (di->itemState & ODS_DISABLED) != 0, down = (di->itemState & ODS_SELECTED) != 0;
  const LONG_PTR style = GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
  wchar_t cap[128]{}; GetWindowTextW(di->hwndItem, cap, 128);
  if (base == ID_MOD_TOGGLE && !footer) {
    const bool on = (style & STYLE_ON) != 0;
    const int side = S(20), y = r.top + (r.bottom - r.top - side) / 2;
    RECT box{r.right - side, y, r.right, y + side};
    round_rect(di->hDC, box, 4, on && !disabled ? C_OK : C_FIELD, on && !disabled ? C_OK : C_FIELD, disabled ? C_FAINT : C_BTN_BORDER);
    if (on) {
      HPEN pen = CreatePen(PS_SOLID, S(2), disabled ? C_FAINT : RGB(0x0E, 0x1A, 0x14));
      HGDIOBJ old = SelectObject(di->hDC, pen);
      MoveToEx(di->hDC, box.left + S(4), box.top + S(10), nullptr);
      LineTo(di->hDC, box.left + S(8), box.top + S(14));
      LineTo(di->hDC, box.left + S(16), box.top + S(5));
      SelectObject(di->hDC, old); DeleteObject(pen);
    }
    RECT label{r.left, r.top, box.left - S(8), r.bottom};
    draw_text(di->hDC, cap, label, g_font_small, disabled ? RGB(0x5C, 0x68, 0x7E) : on ? C_OK : C_DIM, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    return;
  }
  COLORREF top = down ? C_BTN_DOWN : C_BTN, bot = top, border = C_BTN_BORDER, text = C_TEXT;
  if ((style & STYLE_ACCENT) && !disabled) { top = C_ACC_HI; bot = C_ACC_LO; border = NO_FILL; text = C_PLAY_TEXT; if (down) top = bot = C_PLAY_DOWN; }
  if (disabled) { top = bot = RGB(0x1A, 0x21, 0x32); border = RGB(0x28, 0x31, 0x47); text = RGB(0x5C, 0x68, 0x7E); }
  round_rect(di->hDC, r, 7, top, bot, border);
  draw_text(di->hDC, cap, r, g_font, text, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}
void layout_footer() {
  if (!g_window) return;
  RECT client{}; GetClientRect(g_window, &client);
  const int h = units(client.bottom);
  MoveWindow(g_folder_btn, S(kPad), S(h - 42), S(150), S(30), TRUE);
  MoveWindow(g_link_btn, S(kPad + 158), S(h - 42), S(150), S(30), TRUE);
}
void choose_link() {
  wchar_t file[32768]{}; OPENFILENAMEW ofn{sizeof ofn}; ofn.hwndOwner = g_main;
  ofn.lpstrFilter = L"Mod disc (*.iso;*.gcm)\0*.iso;*.gcm\0"; ofn.lpstrFile = file; ofn.nMaxFile = (DWORD)std::size(file); ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return;
  std::string error;
  std::thread([path = fs::path(file)] {
    std::lock_guard<std::mutex> lock(g_install_mutex); std::string error;
    if (!catalog_core::link_pack(fs::u8path(work_dir()), path, &error)) { set_status("drop", error); return; }
    scan_installed(); set_status("drop", "Linked. Your disc stays where it is.");
  }).detach();
}
void cancel_job(const std::string& id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_jobs.count(id)) g_jobs[id]->store(true);
}
LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_APP + 41: capture_test_client(w, "mods-capture.bmp"); return 0;
    case WM_PRINTCLIENT: paint_page((HDC)wp); print_client_children(w, (HDC)wp); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps);
      RECT cr; GetClientRect(w, &cr);
      HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
      HGDIOBJ old = SelectObject(mem, bmp);
      paint_page(mem);
      BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
      EndPaint(w, &ps); return 0;
    }
    case WM_DRAWITEM: draw_item((DRAWITEMSTRUCT*)lp); return TRUE;
    case WM_SIZE: layout_footer(); refresh_window(); return 0;
    case WM_MODS_REFRESH: refresh_window(); return 0;
    case WM_MOUSEWHEEL: g_top -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; refresh_window(); return 0;
    case WM_VSCROLL: {
      const int action = LOWORD(wp), page = visible_rows();
      if (action == SB_LINEUP) --g_top; else if (action == SB_LINEDOWN) ++g_top; else if (action == SB_PAGEUP) g_top -= page; else if (action == SB_PAGEDOWN) g_top += page;
      else if (action == SB_THUMBTRACK) { SCROLLINFO s{sizeof s, SIF_TRACKPOS}; GetScrollInfo(w, SB_VERT, &s); g_top = s.nTrackPos; }
      refresh_window(); return 0;
    }
    case WM_DROPFILES: {
      HDROP drop = (HDROP)wp; wchar_t path[32768]; const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < n; ++i) if (DragQueryFileW(drop, i, path, (UINT)std::size(path))) std::thread(install_file, std::wstring(path), std::string(), g_iso).detach();
      DragFinish(drop); return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      if (id == ID_MOD_FOLDER) { std::error_code ec; fs::create_directories(fs::u8path(mods_dir()), ec); ShellExecuteW(nullptr, L"open", widen(mods_dir()).c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
      if (id == ID_MOD_LINK) { choose_link(); return 0; }
      const int base = (id / 1000) * 1000, index = id - base;
      if (index < 0 || index >= (int)g_rows.size()) return 0;
      const Row row = g_rows[index];
      if (base == ID_MOD_SKINS) { if (!g_playing) open_settings("mods"); return 0; }
      if (base == ID_MOD_BUTTON && is_busy(row.catalog.id)) { cancel_job(row.catalog.id); set_status(row.catalog.id, "Cancelling..."); return 0; }
      if (base == ID_MOD_BUTTON && row.found) {
        if (g_playing || !runs_here(row.installed)) return 0;
        std::string error;
        if (!catalog_core::enable_pack(fs::u8path(settings_ini_path()), row.installed.key, true, &error)) { set_status(row.catalog.id, error); return 0; }
        g_mod_launch_iso = row.installed.path; g_mod_launch_key = row.installed.key; g_mod_launch_kind = row.installed.kind;
        g_mod_launch_engine = catalog_core::play_engine(row.installed, g_engine == ENGINE_SOURCE ? "source" : "recomp");
        select_tab(0); start_game(); return 0;
      }
      if (base == ID_MOD_BUTTON || base == ID_MOD_UPDATE) {
        if (base == ID_MOD_UPDATE && row.found && same_version(row.catalog.version, row.installed.version)) return 0;
        if (row.catalog.one_click) {
          set_status(row.catalog.id, "Starting...");
          std::thread(download_and_install, row.catalog, g_iso).detach();
          PostMessageW(w, WM_MODS_REFRESH, 0, 0);
        } else if (!row.catalog.page.empty()) ShellExecuteW(nullptr, L"open", widen(row.catalog.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      } else if (base == ID_MOD_REMOVE && row.found && !g_playing) {
        std::thread([row] {
          std::lock_guard<std::mutex> lock(g_install_mutex); std::string error;
          if (catalog_core::remove_pack(fs::u8path(work_dir()), fs::u8path(row.installed.path), &error)) { scan_installed(); set_status(row.catalog.id, "Removed. Saves and original linked files were kept."); }
          else set_status(row.catalog.id, error);
        }).detach();
      } else if (base == ID_MOD_TOGGLE && row.found && !g_playing) {
        std::string error;
        if (catalog_core::enable_pack(fs::u8path(settings_ini_path()), row.installed.key, !row.installed.enabled, &error)) std::thread(rescan).detach();
        else set_status(row.catalog.id, error);
      }
      return 0;
    }
    case WM_TIMER: refresh_window(); return 0;
  }
  return DefWindowProcW(w, msg, wp, lp);
}
// The page is a child of the launcher window over the content column, shown while Mods is selected.
void show_page(bool show) {
  if (!show) { if (g_window) { ShowWindow(g_window, SW_HIDE); KillTimer(g_window, 1); } return; }
  if (!g_window) {
    WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"MeleeUnlockedMods"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_ACCEPTFILES | WS_EX_CONTROLPARENT, L"MeleeUnlockedMods", L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
                               0, 0, S(10), S(10), g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(g_window, L"DarkMode_Explorer", nullptr);
    auto button = [&](const wchar_t* label, int id) {
      HWND h = CreateWindowExW(0, L"BUTTON", launcher::lang::txw(label).c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, S(10), S(10), g_window, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); return h;
    };
    g_folder_btn = button(L"Open Mods folder", ID_MOD_FOLDER);
    g_link_btn = button(L"Use it where it is", ID_MOD_LINK);
    g_tools_ok = tools_present();
    if (!g_launcher_test) std::thread(refresh_catalog).detach();
  }
  RECT client{}; GetClientRect(g_main, &client);
  SetWindowPos(g_window, HWND_TOP, S(RAIL_W), 0, client.right - S(RAIL_W), client.bottom, SWP_SHOWWINDOW | SWP_NOACTIVATE);
  layout_footer(); refresh_window();
  SetTimer(g_window, 1, 2000, nullptr);
  std::thread(rescan).detach();
}
void open() { select_tab(4); }
bool is_mod_file(const std::wstring& path) {
  std::wstring l = path; for (auto& c : l) c = (wchar_t)towlower(c);
  for (const wchar_t* s : {L".zip", L".7z", L".xdelta", L".gci"}) if (l.size() >= wcslen(s) && l.compare(l.size() - wcslen(s), wcslen(s), s) == 0) return true;
  return false;
}
}  // namespace mod_manager
