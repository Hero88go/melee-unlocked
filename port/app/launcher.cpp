// Melee Unlocked Launcher: an optional Win32 client with a Play page (ISO, Slippi account, version
// and self-update) and a Build tab (drop the ISO: verify it, run the source build when this is a
// checkout, precompile the pipeline library, remember the path). Starts melee_port.exe, or
// melee_source.exe when the Source Port is picked, with the release settings. Plain Win32 so it has
// no dependencies beyond the OS.
//
// The chrome is drawn by hand: a left rail carrying the wordmark, the page nav and Bailey, and a
// content column on a dark gradient. Every label is painted by the parent in WM_PAINT and every
// button is BS_OWNERDRAW, so nothing stamps an opaque grey rectangle over the art. The whole client
// is composed in a memory DC and blitted once, which is also what keeps changing text from ghosting:
// a repaint always starts from the background, never from what was there before.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <intrin.h>
#include <nlohmann/json.hpp>
#include "launcher_mod_catalog.h"
#include "mod_scan.h"
#include <set>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <climits>
#include <cstring>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#include "updater.h"
#include "launch_process.h"
#include "launcher_engine.h"
#include "launcher_lobby.h"
#include "launcher_lobby_p2p.h"
#include "launcher_theme.h"
#include "launcher_replay_data.h"
#include "launcher_trace_view.h"
#include "launcher_lang.h"
#include "launcher_crash_text.h"   // with launcher_crash_zip.h; launcher_crash.inl sits inside the namespace below
#ifndef MELEE_NO_SLIPPI
#include "slippi_account.h"
#endif
// Every message box shows in the player's language (fixed English wording is looked up).
inline int mu_message_box(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
  return ::MessageBoxW(owner, launcher::lang::txw(text ? text : L"").c_str(),
                       launcher::lang::txw(caption ? caption : L"").c_str(), type);
}
#define MessageBoxW mu_message_box

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif

#define IDI_LAUNCHER 1
#define IDB_LAUNCHER_BG 2
#define IDI_MELEE_MARK 3
#define IDB_MELEE_MARK 4
#define IDB_BAILEY 5

namespace {
enum { ID_ISO_EDIT = 100, ID_BROWSE, ID_PLAY, ID_SLIPPI_GET, ID_UPDATE, ID_BUILD, ID_LOG, ID_WARM_CACHE, ID_VERSIONS, ID_THEME, ID_LANGUAGE, ID_MODS, ID_SEND_LOGS, ID_TIMER = 1, ID_TIMER_STANDBY = 2 };
HWND g_lang_btn = nullptr;
const UINT WM_APP_LOG = WM_APP + 1;      // lParam: heap std::string* to append to the log
const UINT WM_APP_BUILD_DONE = WM_APP + 2;
const UINT WM_APP_GAME_DONE = WM_APP + 3;
const UINT WM_APP_CACHE_WARM_DONE = WM_APP + 4;

// Client area in layout units; S() turns these into pixels for the current DPI.
const int WIN_W = 720, WIN_H = 480;
const int LOBBY_W = 980, LOBBY_H = 768;
const int REPLAY_W = 980, REPLAY_H = 700;
const int MODS_W = 980, MODS_H = 860;
const int RAIL_W = 190;                 // left rail: wordmark, page nav, Bailey
const int CX = 212, CW = 486;           // content column
// NAV_Y leaves room for the whole mark. At 104 the page nav started on top of it and cut the
// MELEE UNLOCKED line off the bottom of the logo.
const int NAV_Y = 152, NAV_H = 34, NAV_GAP = 38;

// Palette. Navy and silver come from the project's own MU mark; the amber accent picks up Bailey.
const COLORREF C_CONTENT_TOP = RGB(0x17, 0x1F, 0x30), C_CONTENT_BOT = RGB(0x11, 0x18, 0x25);
const COLORREF C_RAIL_TOP = RGB(0x0B, 0x11, 0x1E), C_RAIL_BOT = RGB(0x13, 0x1B, 0x2C);
const COLORREF C_DIVIDER = RGB(0x27, 0x31, 0x4A), C_SEP = RGB(0x22, 0x2B, 0x42);
const COLORREF C_TEXT = RGB(0xE7, 0xEC, 0xF5), C_DIM = RGB(0x8D, 0x9B, 0xB5), C_FAINT = RGB(0x66, 0x74, 0x8E);
const COLORREF C_FIELD = RGB(0x0E, 0x15, 0x22), C_FIELD_BORDER = RGB(0x2B, 0x36, 0x52);
// The accent (selected page bar, focus rings, the Build page's active border) is the same purple as
// the PLAY button rather than Bailey's amber: two different highlight colours on one page read as
// two different meanings, and PLAY is the one the eye is meant to land on.
#define C_ACC_HI launcher::theme::top()
#define C_ACC_LO launcher::theme::bottom()
const COLORREF C_BTN = RGB(0x21, 0x2B, 0x42), C_BTN_BORDER = RGB(0x35, 0x41, 0x5F), C_BTN_DOWN = RGB(0x18, 0x21, 0x34);
const COLORREF C_NAV_ON = RGB(0x1F, 0x2A, 0x40), C_NAV_HOT = RGB(0x18, 0x21, 0x34);
const COLORREF C_LOG_BG = RGB(0x0A, 0x0F, 0x1A), C_LOG_TEXT = RGB(0x9F, 0xB4, 0xCE);
#define C_PLAY_TEXT launcher::theme::on_accent()
// The PLAY button: a saturated purple gradient, as bold as the amber it replaced.
#define C_PLAY_HI launcher::theme::top()
#define C_PLAY_LO launcher::theme::bottom()
#define C_PLAY_DOWN launcher::theme::pressed()
const COLORREF C_OK = RGB(0x5A, 0xC8, 0x8A), C_WARN = RGB(0xE5, 0xA8, 0x4A), C_BAD = RGB(0xE0, 0x6B, 0x5B);
const COLORREF NO_FILL = CLR_INVALID;

HWND g_main;
HWND g_play[10], g_build[3];
HWND g_iso_edit, g_play_btn, g_slippi_btn, g_update_btn, g_versions_btn, g_log, g_build_btn, g_warm_cache_check;
HFONT g_font, g_font_big, g_font_mono, g_font_mark, g_font_nav, g_font_label, g_font_small;
HICON g_mark = nullptr;          // IDI_MELEE_MARK, the wordmark drawn at the top of the rail
HBRUSH g_br_field, g_br_log;
HBITMAP g_dog = nullptr;         // Bailey cut out of her white plate, 32bpp premultiplied
int g_dog_w = 0, g_dog_h = 0;
RECT g_dog_rect{};              // where she was last drawn: clicking her changes the picture
bool g_dog_hot = false;
HBITMAP g_wordmark = nullptr;    // the MU mark cut off its navy plate, at full resolution
int g_wordmark_w = 0, g_wordmark_h = 0;
std::string g_dir, g_iso, g_game_exe;
std::string g_language;   // launcher.ini language= (empty: follow Windows)
// A code mod (Akaneia, ACE, 20XX Hack Pack...) runs on the Static Recomp with its own disc, the
// player's vanilla disc for the reference code, and a memory card folder of its own.
std::string g_mod_launch_iso, g_mod_launch_key, g_mod_launch_engine, g_mod_launch_kind;
bool g_launcher_test = std::getenv("MELEE_LAUNCHER_TEST") != nullptr;
namespace mod_manager { std::string summary(); }
void start_game();
std::string static_recomp_exe();
std::string g_lobby_launch_args;
bool g_lobby_game_active = false;
bool g_lobby_game_p2p = false;   // that lobby game is a P2P Direct match (the launcher that also starts Slippi Direct)
launcher::lobby::Prefs g_lobby_prefs;   // launcher.ini lobbyopen / lobbyiso / lobbyisoname
std::string g_active_version; // folder name inside Versions; empty means the current install
bool g_rollback_consumed = true;
// The game is a single integrated build. Older CPUs can still use a compatibility executable
// when one is present; this is selected automatically unless launcher.ini overrides it.
using launcher::CPU_AUTO; using launcher::CPU_STANDARD; using launcher::CPU_COMPAT;
int g_cpu_build = CPU_AUTO;
// Which build PLAY starts, picked on the GAME BUILD row. Static Recomp (melee_port.exe) is the
// recompilation every release so far has shipped and stays the default; the Source Port
// (melee_source.exe + melee_game.dll) is the native build of the decompiled game. Both read the
// same port-settings.ini from the same working directory, so switching does not move anyone's
// settings. launcher.ini keeps engine=1 for the Source Port; no engine= line means Static Recomp.
using launcher::ENGINE_LEGACY; using launcher::ENGINE_SOURCE;
int g_engine = ENGINE_LEGACY;
bool g_install_requested = false;
bool g_warm_cache_on_play = false;
// Replay Viewer: "Show it as it was played" (launcher.ini replayasplayed=1). It applies to a replay
// that has a session trace file beside it; see launcher_replays.inl.
bool g_replay_as_played = false;
// Replay Viewer list: the sort order (launcher.ini replaysort=1..3; none or 0 is most recent first,
// an index into kReplaySorts) and "Hide short games" (replayhideshort=1).
int g_replay_sort = 0;
bool g_replay_hide_short = false;
std::string g_slippi_line, g_version_line;
COLORREF g_version_dot = C_FAINT;
std::atomic<bool> g_building{false}, g_playing{false};
bool g_slippi_missing = false;
int g_tab = 0, g_nav_hot = -1;
int g_seg_hot = -1;              // the GAME BUILD segment under the mouse, when a click would pick it
bool g_tracking = false;
int g_dpi = 96;
int S(int v) { return MulDiv(v, g_dpi, 96); }

std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n ? n - 1 : 0, 0); if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
std::string narrow(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n ? n - 1 : 0, 0); if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr); return s; }
bool file_exists(const std::string& p) { DWORD a = GetFileAttributesW(widen(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
void set_text(HWND h, const std::string& s) { SetWindowTextW(h, widen(launcher::lang::tx(s)).c_str()); }
bool safe_version_folder(const std::string& s) {
  if (s.empty() || s.size() > 70 || s.find("..") != std::string::npos) return false;
  for (char c : s) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
                    !(c >= '0' && c <= '9') && c != '.' && c != '-' && c != '_') return false;
  return true;
}
std::string active_dir() { return g_active_version.empty() ? g_dir : g_dir + "\\Versions\\" + g_active_version; }

// A layout rectangle in pixels.
RECT LR(int x, int y, int w, int h) { RECT r{S(x), S(y), S(x + w), S(y + h)}; return r; }
void invalidate(RECT r) { if (g_main) InvalidateRect(g_main, &r, FALSE); }

void log_line(const char* fmt, ...) {
  char buf[4096]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  PostMessageW(g_main, WM_APP_LOG, 0, (LPARAM) new std::string(std::string(buf) + "\r\n"));
}

std::string repo_root();   // defined below; load_ini looks for a disc beside a source checkout
std::string ini_path() { return g_dir + "\\launcher.ini"; }
// The game reads its settings from its working directory, which is where the launcher starts it.
std::string settings_ini_path();
// A second copy outside the game folder, so the ISO path survives an update, a re-extracted zip
// or a second copy of the game, and so the launcher knows the disc the game itself was last
// started with (melee_port records it there too).
std::string shared_ini_path() {
  char* local = nullptr; size_t n = 0;
  if (_dupenv_s(&local, &n, "LOCALAPPDATA") != 0 || !local) return "";
  std::string dir = std::string(local) + "\\MeleeUnlocked";
  free(local);
  CreateDirectoryW(widen(dir).c_str(), nullptr);
  return dir + "\\launcher.ini";
}
std::string read_iso_from(const std::string& path) {
  std::ifstream f(path); std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("iso=", 0) == 0) return line.substr(4);
  }
  return "";
}
// The launcher's language: launcher.ini's language= line, else Windows' display language. Runs
// before any control is made, so every label is created already translated.
void load_language() {
  std::string code;
  std::ifstream f(ini_path());
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("language=", 0) == 0) code = line.substr(9);
  }
  g_language = code;
  if (code.empty()) code = launcher::lang::system_default();
  launcher::lang::set_language(code, {widen(g_dir + "\\lang")});
}
void load_ini() {
  {
    std::ifstream f(ini_path());
    std::string line;
    while (std::getline(f, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("engine=", 0) == 0) { g_engine = launcher::parse_engine_line(line, g_engine); continue; }
      if (line.rfind("cpubuild=", 0) == 0) {
        const int v = std::atoi(line.c_str() + 9);
        if (v >= CPU_AUTO && v <= CPU_COMPAT) g_cpu_build = v;
      } else if (line.rfind("activeversion=", 0) == 0 && safe_version_folder(line.substr(14))) g_active_version = line.substr(14);
      else if (line == "warmcache=1") g_warm_cache_on_play = true;
      else if (line == "replayasplayed=1") g_replay_as_played = true;
      else if (line == "replayhideshort=1") g_replay_hide_short = true;
      else if (line.rfind("replaysort=", 0) == 0) g_replay_sort = std::clamp(std::atoi(line.c_str() + 11), 0, 3);
      else if (line.rfind("lobbyopen=", 0) == 0) g_lobby_prefs.open_to = line.substr(10);
      else if (line.rfind("lobbyiso=", 0) == 0) g_lobby_prefs.custom_iso = line.substr(9);
      else if (line.rfind("lobbyisoname=", 0) == 0) g_lobby_prefs.custom_name = line.substr(13);
      else if (line.rfind("accent=#",0)==0 && line.size()==14) {
        unsigned value=0; if(std::sscanf(line.c_str()+8,"%6x",&value)==1)
          launcher::theme::accent=RGB((value>>16)&255,(value>>8)&255,value&255);
      }
    }
  }
  if (!g_active_version.empty() &&
      (!file_exists(active_dir() + "\\Sys\\codehandler.bin") ||
       !file_exists(active_dir() + "\\melee_port.exe")))
    g_active_version.clear();
  g_iso = read_iso_from(ini_path());
  if (g_iso.empty() || !file_exists(g_iso)) {
    std::string remembered = read_iso_from(shared_ini_path());
    if (!remembered.empty() && file_exists(remembered)) g_iso = remembered;
  }
  if (g_iso.empty() && file_exists(g_dir + "\\melee.iso")) g_iso = g_dir + "\\melee.iso";
  std::string root = repo_root();
  if (g_iso.empty() && !root.empty() && file_exists(root + "\\melee.iso")) g_iso = root + "\\melee.iso";
  if (!g_iso.empty() && !file_exists(g_iso)) g_iso.clear();
}
void save_ini() {
  {
    std::ofstream f(ini_path());
    f << "iso=" << g_iso << "\n";
    if (!g_active_version.empty()) f << "activeversion=" << g_active_version << "\n";
    if (g_warm_cache_on_play) f << "warmcache=1\n";
    if (g_replay_as_played) f << "replayasplayed=1\n";
    if (g_replay_sort != 0) f << "replaysort=" << g_replay_sort << "\n";
    if (g_replay_hide_short) f << "replayhideshort=1\n";
    // Kept so that browsing for a disc does not silently undo a hand-set override.
    if (g_cpu_build != CPU_AUTO) f << "cpubuild=" << g_cpu_build << "\n";
    if (g_engine != ENGINE_LEGACY) f << "engine=" << g_engine << "\n";
    if (!g_language.empty()) f << "language=" << g_language << "\n";
    // The lobby: which versions this player takes match requests for, and their custom ISO.
    if (!g_lobby_prefs.open_to.empty() && g_lobby_prefs.open_to != "vanilla") f << "lobbyopen=" << g_lobby_prefs.open_to << "\n";
    if (!g_lobby_prefs.custom_iso.empty()) f << "lobbyiso=" << g_lobby_prefs.custom_iso << "\nlobbyisoname=" << g_lobby_prefs.custom_name << "\n";
    char color[16]; std::snprintf(color,sizeof color,"accent=#%02X%02X%02X",GetRValue(launcher::theme::accent),GetGValue(launcher::theme::accent),GetBValue(launcher::theme::accent));
    f << color << "\n";
  }
  const std::string shared = shared_ini_path();
  if (!shared.empty()) { std::ofstream f(shared); f << "iso=" << g_iso << "\n"; }
}

// Header check: game id GALE01 at offset 0, revision byte 2 at offset 7 (NTSC 1.02).
bool verify_iso(const std::string& path, std::string* why) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { *why = "cannot open the file"; return false; }
  char hdr[8]{}; f.read(hdr, 8);
  if (std::string(hdr, 6) != "GALE01") { *why = "not a Melee NTSC disc (game id " + std::string(hdr, 6) + ")"; return false; }
  if (hdr[7] != 2) { *why = "Melee NTSC 1.0" + std::to_string((int)hdr[7]) + "; version 1.02 is required"; return false; }
  f.seekg(0, std::ios::end);
  auto size = (unsigned long long)f.tellg();
  if (size < 1000000000ull) { *why = "file is too small to be a full disc image (" + std::to_string(size / 1000000) + " MB)"; return false; }
  return true;
}

// Use the game's account discovery and credential checks so this line reflects a usable login.
#ifndef MELEE_NO_SLIPPI
std::string slippi_account_line() {
  if (auto profile = slippi::account::resolve(std::filesystem::path(g_dir) / "User" / "Slippi")) {
    auto name = slippi::account::text(profile.data, "displayName");
    if (name.empty()) name = "?";
    const auto code = slippi::account::text(profile.data, "connectCode");
    launcher::lobby::set_account(name, code);
    return "Slippi account: " + name + " (" + code + ")";
  }
  launcher::lobby::set_account("", "");
  switch (slippi::account::why_missing(std::filesystem::path(g_dir) / "User" / "Slippi")) {
    case slippi::account::Missing::SignIn:
      return "Slippi Launcher found, but no sign-in: open it, log in, and start Slippi Dolphin once.";
    case slippi::account::Missing::ConnectCode:
      return "Your Slippi sign-in has no connect code yet: finish the account in the Slippi Launcher.";
    default:
      return "Slippi online needs an account: install the Slippi Launcher and log in once.";
  }
}
#endif
// The line under PLAY and whether online play is missing something. The build without the Slippi
// layer has no account to look for: it shows the lobby name and the code made from it, and a match
// needs only the disc and the game files.
void refresh_account_line() {
#ifdef MELEE_NO_SLIPPI
  std::string name, code;
  launcher::lobby::p2p_player(name, code);
  g_slippi_line = name.empty() ? launcher::lang::tr("lobby.p2p.name_missing")
                               : launcher::lang::tr("lobby.p2p.name", {{"name", name}, {"code", code}});
  g_slippi_missing = false;
#else
  g_slippi_line = slippi_account_line();
  g_slippi_missing = g_slippi_line.rfind("Slippi account:", 0) != 0;
#endif
}

// Runs a command line with stdout/stderr piped into the log. Returns the exit code.
DWORD run_logged(std::string cmd, const std::string& cwd) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return 1;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  auto wcmd = widen(cmd), wcwd = widen(cwd);
  if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, wcwd.c_str(), &si, &pi)) { DWORD error = GetLastError(); CloseHandle(rd); CloseHandle(wr); log_line("cannot start (Windows error %lu): %s", error, cmd.c_str()); return error ? error : 1; }
  CloseHandle(wr);
  std::string acc; char buf[4096]; DWORD got = 0;
  while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) {
    acc.append(buf, got);
    size_t nl;
    while ((nl = acc.find('\n')) != std::string::npos) { std::string line = acc.substr(0, nl); if (!line.empty() && line.back() == '\r') line.pop_back(); log_line("%s", line.c_str()); acc.erase(0, nl + 1); }
  }
  if (!acc.empty()) log_line("%s", acc.c_str());
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return code;
}

// A source checkout has build.bat at the repo root; the launcher then lives in build-review\port\Release.
std::string repo_root() {
  std::string d = g_dir;
  for (int i = 0; i < 4; ++i) { if (file_exists(d + "\\build.bat") && file_exists(d + "\\tools\\extract_dol.py")) return d; auto p = d.find_last_of("\\/"); if (p == std::string::npos) break; d.resize(p); }
  return "";
}
// The game is built for AVX2, which a processor older than Intel Haswell (2013) or AMD Ryzen does
// not have, and on such a machine it cannot start at all: it died during startup with no message
// until 0.3.3 and with one after it. A second build for those processors ships beside the ordinary
// one. Choosing between them here, rather than asking the player to work out which they need, is
// what lets the ordinary build keep its instruction set so nobody else gives anything up.
bool cpu_has_avx2() {
  int r[4];
  __cpuid(r, 0); if (r[0] < 7) return false;
  __cpuid(r, 1); const bool osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
  if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
  __cpuidex(r, 7, 0); return (r[1] >> 5) & 1;
}

// The Source Port is two files: the host and the game library beside it. A release that predates
// it, or a source checkout that has not built it, has neither, so it can only be picked when both
// are actually there: in the game folder (active_dir(), the one melee_port.exe comes from), or in a
// source checkout's build-sourceport output. That search is launcher_engine.h, so a test can run it
// without this window.
std::string source_exe_dir() {
  launcher::EngineInputs in;
  in.dir = active_dir();
  // An older version from Versions runs only what is in its own folder, as in game_exe() below.
  in.repo_root = g_active_version.empty() ? repo_root() : std::string();
  in.exists = file_exists;
  return launcher::source_exe_dir(in);
}
bool source_available() { return !source_exe_dir().empty(); }

std::string game_exe() {
  // The Source Port when it is picked and installed. Picked but gone since (an update that dropped
  // it, or an older version without it) falls back to Static Recomp rather than failing to start.
  if (g_engine == ENGINE_SOURCE) {
    const std::string d = source_exe_dir();
    if (!d.empty()) return d + "\\melee_source.exe";
  }
#ifdef MELEE_NO_SLIPPI
  // There is no Static Recomp to fall back to here: the Source Port, or nothing to start.
  return active_dir() + "\\melee_source.exe";
#endif
  // Automatic only hands over the compatibility build to a processor that cannot run the other one.
  // A machine that can, keeps it. The two explicit choices exist so that a player who knows their
  // machine is not stuck arguing with a detector.
  const bool want_compat = g_cpu_build == CPU_COMPAT ||
                           (g_cpu_build == CPU_AUTO && !cpu_has_avx2());
  const std::string stem = "melee_port";
  const std::string dir = active_dir();
  if (want_compat && file_exists(dir + "\\" + stem + "_compat.exe"))
    return dir + "\\" + stem + "_compat.exe";
  if (file_exists(dir + "\\" + stem + ".exe")) return dir + "\\" + stem + ".exe";
  if (!g_active_version.empty()) return dir + "\\" + stem + ".exe";
  std::string root = repo_root();
  if (!root.empty()) {
    const std::string integrated = root + "\\build-integration\\port\\Release\\melee_port.exe";
    if (file_exists(integrated)) return integrated;
    return root + "\\build-review\\port\\Release\\melee_port.exe";
  }
  return g_dir + "\\" + stem + ".exe";
}
// Working directory for the game: the release folder (Sys next to the launcher) or the repo root
// of a source checkout (its defaults, port/slippi_sys and shadercache, are relative paths).
std::string work_dir() {
  if (file_exists(active_dir() + "\\Sys\\codehandler.bin")) return active_dir();
  std::string root = repo_root();
  return root.empty() ? g_dir : root;
}
std::string settings_ini_path() { return (g_active_version.empty() ? work_dir() : g_dir) + "\\port-settings.ini"; }
std::string game_args() {
  std::string base = active_dir();
  std::string a = " --iso \"" + g_iso + "\" --threaded-renderer --settings-path \"" + settings_ini_path() + "\"";
#ifdef MELEE_NO_SLIPPI
  // That game has no Slippi user folder and refuses the option.
  if (file_exists(base + "\\Sys\\codehandler.bin"))
    a += " --sys-dir \"" + base + "\\Sys\" --replay-dir \"" + g_dir + "\\Replays\" --card-dir \"" + g_dir + "\\User\\GC\\CardA\"";
#else
  if (file_exists(base + "\\Sys\\codehandler.bin"))
    a += " --sys-dir \"" + base + "\\Sys\" --user-dir \"" + g_dir + "\\User\\Slippi\" --replay-dir \"" + g_dir + "\\Replays\" --card-dir \"" + g_dir + "\\User\\GC\\CardA\"";
#endif
  return a;
}

// ---------------------------------------------------------------------------- painting helpers

COLORREF lerp(COLORREF a, COLORREF b, int num, int den) {
  if (den <= 0) return a;
  if (num < 0) num = 0;
  if (num > den) num = den;
  return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * num / den,
             GetGValue(a) + (GetGValue(b) - GetGValue(a)) * num / den,
             GetBValue(a) + (GetBValue(b) - GetBValue(a)) * num / den);
}

void fill(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }

// Vertical gradient, one FillRect per row. Small window, so the simple version is fast enough.
void vgrad(HDC dc, RECT r, COLORREF top, COLORREF bot) {
  int h = r.bottom - r.top;
  for (int y = 0; y < h; ++y) {
    RECT row{r.left, r.top + y, r.right, r.top + y + 1};
    fill(dc, row, lerp(top, bot, y, h - 1));
  }
}

// The content column's background colour at a given client y. Owner-drawn buttons use this to
// reproduce the parent's gradient inside their own rect, so their rounded corners blend instead of
// showing a box.
COLORREF content_bg_at(int y) { return lerp(C_CONTENT_TOP, C_CONTENT_BOT, y, S(WIN_H) - 1); }

// A rounded rectangle with an optional vertical gradient fill and a 1px border, supersampled 3x so
// the corners come out smooth. The destination is stretched into the scratch bitmap first, which is
// what lets the corners keep whatever is already painted underneath them.
void round_rect(HDC dc, RECT r, int radius, COLORREF top, COLORREF bot, COLORREF border) {
  const int SS = 3;
  int w = r.right - r.left, h = r.bottom - r.top;
  if (w <= 0 || h <= 0) return;
  HDC md = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, w * SS, h * SS);
  HGDIOBJ oldb = SelectObject(md, bmp);
  SetStretchBltMode(md, COLORONCOLOR);
  StretchBlt(md, 0, 0, w * SS, h * SS, dc, r.left, r.top, w, h, SRCCOPY);
  HRGN rgn = CreateRoundRectRgn(0, 0, w * SS + 1, h * SS + 1, radius * 2 * SS, radius * 2 * SS);
  if (top != NO_FILL) {
    SelectClipRgn(md, rgn);
    RECT all{0, 0, w * SS, h * SS};
    if (top == bot) fill(md, all, top); else vgrad(md, all, top, bot);
    SelectClipRgn(md, nullptr);
  }
  if (border != NO_FILL) {
    HBRUSH b = CreateSolidBrush(border);
    FrameRgn(md, rgn, b, SS, SS);
    DeleteObject(b);
  }
  DeleteObject(rgn);
  SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, nullptr);
  StretchBlt(dc, r.left, r.top, w, h, md, 0, 0, w * SS, h * SS, SRCCOPY);
  SelectObject(md, oldb);
  DeleteObject(bmp);
  DeleteDC(md);
}

void draw_text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF col, UINT fmt, int tracking = 0) {
  HGDIOBJ old = SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, col);
  if (tracking) SetTextCharacterExtra(dc, tracking);
  // Fixed English text shows in the player's language, line by line; anything else (paths,
  // names) as is.
  std::wstring shown;
  for (size_t at = 0; at <= s.size();) {
    size_t end = s.find(L'\n', at);
    if (end == std::wstring::npos) end = s.size();
    if (at) shown += L'\n';
    shown += launcher::lang::txw(s.substr(at, end - at).c_str());
    at = end + 1;
  }
  DrawTextW(dc, shown.c_str(), -1, &r, fmt);
  if (tracking) SetTextCharacterExtra(dc, 0);
  SelectObject(dc, old);
}

void dot(HDC dc, int x, int y, COLORREF c) {
  RECT r{S(x), S(y), S(x + 8), S(y + 8)};
  round_rect(dc, r, 4, c, c, NO_FILL);
}

// Rail order: Play, Settings, Lobby, Replays, Mods, Build.
#ifdef MELEE_NO_SLIPPI
// That build records no replays, so the Replay Viewer has no rail entry and the rest move up.
bool nav_shown(int i) { return i != 4; }
int nav_slot(int i) { static const int slot[6] = {0, 1, 4, 2, 5, 3}; return i >= 0 && i < 6 ? slot[i] : i; }
#else
bool nav_shown(int) { return true; }
int nav_slot(int i) { static const int slot[6] = {0, 1, 5, 2, 3, 4}; return i >= 0 && i < 6 ? slot[i] : i; }
#endif
RECT nav_rect(int i) { return LR(12, NAV_Y + nav_slot(i) * NAV_GAP, RAIL_W - 24, NAV_H); }
// The two build segments, side by side on the GAME BUILD row: 0 Source Port, 1 Static Recomp (Legacy).
RECT build_seg_rect(int i) {
  const int left = CX + 96, total = CW - 96, gap = 8, w = (total - gap) / 2;
  return LR(left + i * (w + gap), 98, w, 28);
}
// The segment a click at p would pick: -1 off the row, off the Play page, or on a Source Port
// segment with nothing installed behind it. The file check only runs over that segment.
int build_seg_at(POINT p) {
  if (g_tab != 0) return -1;
#ifdef MELEE_NO_SLIPPI
  return -1;   // one Game Build: nothing to pick
#endif
  for (int i = 0; i < 2; ++i) {
    RECT r = build_seg_rect(i);
    if (PtInRect(&r, p)) return i == 0 && !source_available() ? -1 : i;
  }
  return -1;
}
RECT slippi_text_rect() { return LR(CX + 15, 212, 319, 34); }
RECT version_text_rect() { return LR(CX + 15, 236, 319, 24); }
RECT drop_sub_rect() { return LR(CX, 86, CW, 20); }

const wchar_t* HINT_TEXT =
    L"F1 opens settings in game. GameCube adapters work automatically.\n"
    L"Keyboard: arrows, IJKL, Z X C V, Enter, Q W E.";

std::string iso_name() {
  auto p = g_iso.find_last_of("\\/");
  return p == std::string::npos ? g_iso : g_iso.substr(p + 1);
}

// The rail art. Both images are already 32bpp premultiplied cutouts (see launcher.rc), so this
// only has to hand GDI the bits. Keying the plate out at runtime by colour was the wrong idea:
// "delete the white" also deletes white fur, which punched holes through Bailey. The cut is made
// before the build by flooding the background in from the borders, so only pixels actually
// connected to the plate are removed.
// Area average into a bitmap of exactly the size it will be drawn at. AlphaBlend scales with a
// cheap filter, so letting it shrink the art every paint is what made Bailey look pixelated; the
// resample happens once here and the result is blitted 1:1. Premultiplied throughout, so averaging
// the channels is correct across the transparent edge instead of dragging colour out of it.
HBITMAP resample(const uint8_t* src, int sw, int sh, int dw, int dh) {
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return nullptr;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = dw; bi.bmiHeader.biHeight = -dh;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dst = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dst || !bits) { if (dst) DeleteObject(dst); return nullptr; }
  auto* out = (uint8_t*)bits;
  for (int y = 0; y < dh; ++y) {
    const int sy0 = y * sh / dh, sy1 = (y + 1) * sh / dh > sy0 ? (y + 1) * sh / dh : sy0 + 1;
    for (int x = 0; x < dw; ++x) {
      const int sx0 = x * sw / dw, sx1 = (x + 1) * sw / dw > sx0 ? (x + 1) * sw / dw : sx0 + 1;
      unsigned acc[4] = {0, 0, 0, 0}; unsigned n = 0;
      for (int sy = sy0; sy < sy1 && sy < sh; ++sy)
        for (int sx = sx0; sx < sx1 && sx < sw; ++sx) {
          const uint8_t* p = src + ((size_t)sy * sw + sx) * 4;
          acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; ++n;
        }
      uint8_t* q = out + ((size_t)y * dw + x) * 4;
      if (!n) { q[0] = q[1] = q[2] = q[3] = 0; continue; }
      q[0] = (uint8_t)(acc[0] / n); q[1] = (uint8_t)(acc[1] / n);
      q[2] = (uint8_t)(acc[2] / n); q[3] = (uint8_t)(acc[3] / n);
    }
  }
  return dst;
}

// Loads a 32bpp premultiplied cutout and scales it to fit inside max_w x max_h, keeping its aspect.
// Fitting to both is what stops the art being clipped when the window height changes: sizing by
// width alone left Bailey taller than the space under the nav and her paws ran off the bottom.
HBITMAP load_art(int resource, int max_w, int max_h, int& w, int& h) {
  HBITMAP bmp = (HBITMAP)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
  if (!bmp) return nullptr;
  DIBSECTION ds{};
  if (GetObjectW(bmp, sizeof ds, &ds) != sizeof ds || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits) { DeleteObject(bmp); return nullptr; }
  const int sw = ds.dsBm.bmWidth, sh = ds.dsBm.bmHeight < 0 ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
  int dw = max_w > 0 ? max_w : sw, dh = sw ? dw * sh / sw : sh;
  if (max_h > 0 && dh > max_h) { dh = max_h; dw = sh ? dh * sw / sh : dw; }
  HBITMAP scaled = resample((const uint8_t*)ds.dsBm.bmBits, sw, sh, dw, dh);
  DeleteObject(bmp);
  if (!scaled) return nullptr;
  w = dw; h = dh;
  return scaled;
}
// The player's own picture in Bailey's place: any PNG, JPEG, BMP or GIF, decoded by Windows, kept as
// premultiplied BGRA so it blends onto the rail exactly as she does.
HBITMAP load_art_file(const std::wstring& path, int max_w, int max_h, int& w, int& h) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IWICImagingFactory* factory = nullptr; IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* conv = nullptr;
  HBITMAP result = nullptr;
  UINT sw = 0, sh = 0;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
      SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(conv->GetSize(&sw, &sh)) && sw > 0 && sh > 0 && sw <= 8192 && sh <= 8192) {
    std::vector<uint8_t> bits((size_t)sw * sh * 4);
    if (SUCCEEDED(conv->CopyPixels(nullptr, sw * 4, (UINT)bits.size(), bits.data()))) {
      int dw = max_w, dh = (int)((long long)dw * sh / sw);
      if (dh > max_h) { dh = max_h; dw = (int)((long long)dh * sw / sh); }
      result = resample(bits.data(), (int)sw, (int)sh, std::max(dw, 1), std::max(dh, 1));
      if (result) { w = std::max(dw, 1); h = std::max(dh, 1); }
    }
  }
  if (conv) conv->Release(); if (frame) frame->Release(); if (decoder) decoder->Release(); if (factory) factory->Release();
  return result;
}
std::string rail_image_path() {
  for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".gif"})
    if (file_exists(g_dir + "\\rail-image" + ext)) return g_dir + "\\rail-image" + ext;
  return {};
}
void load_rail_art() {
  // One box for Bailey and for a player's picture: it fits under the page list in the shortest
  // window, so no picture can ever reach the nav text.
  const int box_w = S(RAIL_W - 46), box_h = S(480 - (NAV_Y + 5 * NAV_GAP + NAV_H + 10) - 12);
  if (g_dog) { DeleteObject(g_dog); g_dog = nullptr; }
  const std::string custom = rail_image_path();
  if (!custom.empty()) g_dog = load_art_file(widen(custom), box_w, box_h, g_dog_w, g_dog_h);
  if (!g_dog) g_dog = load_art(IDB_BAILEY, box_w, box_h, g_dog_w, g_dog_h);
  g_wordmark = load_art(IDB_MELEE_MARK, S(RAIL_W - 44), S(NAV_Y - 22), g_wordmark_w, g_wordmark_h);
}

void paint_rail(HDC dc) {
  RECT client{}; GetClientRect(g_main,&client);
  RECT r{0,0,S(RAIL_W),client.bottom};
  // The rail is always the dark gradient. Stretching the photo over it filled the rail with the
  // white plate the dog was composited on and cropped her at the same time, because the plate is
  // 620x416 and the rail is tall and narrow. The dog is cut out of that plate at load (see
  // load_rail_art) and drawn over the gradient at her own aspect, whole.
  vgrad(dc, r, C_RAIL_TOP, C_RAIL_BOT);
  if (g_dog && g_dog_w > 0 && g_dog_h > 0) {
    const int dw = g_dog_w, dh = g_dog_h;
    // Centred in what is left of the rail under the nav, so the gap above her matches the gap below.
    // Bottom-anchored left a dead band between the page list and her head.
    const int top = S(NAV_Y + 5 * NAV_GAP + NAV_H + 10);
    const int dx = (S(RAIL_W) - dw) / 2, dy = top + (client.bottom - top - dh) / 2;
    HDC md = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(md, g_dog);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    AlphaBlend(dc, dx, dy, dw, dh, md, 0, 0, g_dog_w, g_dog_h, bf);
    g_dog_rect = RECT{dx, dy, dx + dw, dy + dh};
    SelectObject(md, old);
    DeleteDC(md);
  }
  RECT line{r.right - 1, 0, r.right, client.bottom};
  fill(dc, line, C_DIVIDER);

  // The game's own mark, drawn as artwork. Setting the two words in a system font next to the
  // real logo never matched it: the wordmark is part of the mark, so the image is the wordmark.
  if (g_wordmark && g_wordmark_w > 0) {
    const int mw = g_wordmark_w, mh = g_wordmark_h;
    HDC md = CreateCompatibleDC(dc);
    HGDIOBJ oldm = SelectObject(md, g_wordmark);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    AlphaBlend(dc, (S(RAIL_W) - mw) / 2, S(14), mw, mh, md, 0, 0, g_wordmark_w, g_wordmark_h, bf);
    SelectObject(md, oldm); DeleteDC(md);
  }

  const wchar_t* names[6] = {L"Play", L"Settings", L"Build", L"Multiplayer Lobby", L"Replay Viewer", L"Mods"};
  for (int i = 0; i < 6; ++i) {
    if (!nav_shown(i)) continue;
    RECT nr = nav_rect(i);
    const int page = i == 0 ? 0 : (i == 2 ? 1 : (i == 3 ? 2 : (i == 4 ? 3 : (i == 5 ? 4 : -1))));
    if (page >= 0 && g_tab == page) {
      round_rect(dc, nr, 8, C_NAV_ON, C_NAV_ON, NO_FILL);
      RECT bar = LR(12, NAV_Y + nav_slot(i) * NAV_GAP + 8, 4, 18);
      round_rect(dc, bar, 2, C_ACC_HI, C_ACC_LO, NO_FILL);
    } else if (g_nav_hot == i) {
      round_rect(dc, nr, 8, C_NAV_HOT, C_NAV_HOT, NO_FILL);
    }
    RECT tr{nr.left + S(18), nr.top, nr.right, nr.bottom};
    draw_text(dc, names[i], tr, g_font_nav, (page >= 0 && g_tab == page) ? C_TEXT : C_DIM, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
  }
}

void paint_play(HDC dc) {
  draw_text(dc, L"MELEE NTSC 1.02 DISC IMAGE", LR(CX, 32, CW, 18), g_font_label, C_FAINT,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER, S(1));
  round_rect(dc, LR(CX, 58, 380, 34), 7, C_FIELD, C_FIELD, C_FIELD_BORDER);
  // GAME BUILD row, drawn like 0.6.61's two segments (same label, rects, radius and colours): the
  // build PLAY starts is filled in PLAY's purple and the other is an outline that a click selects.
  // With melee_source.exe or melee_game.dll missing from the game folder, the Source Port segment
  // says so in the disabled-button colours, cannot be picked, and PLAY starts Static Recomp.
  draw_text(dc, L"GAME BUILD", LR(CX, 102, 130, 19), g_font_label, C_FAINT, DT_LEFT | DT_SINGLELINE | DT_VCENTER, S(1));
#ifdef MELEE_NO_SLIPPI
  // One Game Build here, so the row names it in plain text where the two segments start: a segment
  // reads as a button, and there is nothing to pick.
  draw_text(dc, source_available() ? L"Source Port" : L"Source Port (not installed)", LR(CX + 96, 98, CW - 96, 28), g_font,
            source_available() ? C_TEXT : C_WARN, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
#else
  {
    const bool installed = source_available();
    const int on = g_engine == ENGINE_SOURCE && installed ? 0 : 1;
    const wchar_t* names[2] = {installed ? L"Source Port (Beta)" : L"Source Port (Beta, not installed)", L"Static Recomp (Legacy)"};
    const int segments = 2;
    for (int i = 0; i < segments; ++i) {
      const RECT r = build_seg_rect(i);
      COLORREF top = C_BTN, bot = C_BTN, border = C_BTN_BORDER, text = C_DIM;
      if (i == on) { top = C_ACC_HI; bot = C_ACC_LO; border = NO_FILL; text = C_PLAY_TEXT; }
      else if (i == 0 && !installed) { top = bot = RGB(0x1A, 0x21, 0x32); border = RGB(0x28, 0x31, 0x47); text = RGB(0x5C, 0x68, 0x7E); }
      round_rect(dc, r, 7, top, bot, border);
      draw_text(dc, names[i], r, g_font_small, text, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
  }
#endif

  dot(dc, CX, 215, g_slippi_missing ? C_WARN : C_OK);   // centred on the first line of slippi_text_rect
  draw_text(dc, widen(g_slippi_line), slippi_text_rect(), g_font, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);

  dot(dc, CX, 239, g_version_dot);   // centred on the first line of version_text_rect
  draw_text(dc, widen(g_version_line), version_text_rect(), g_font, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);

  // Same shape as the account and version lines: a dot, then dim text. Green dot while a mod is on.
  const std::string mods_line = mod_manager::summary();
  dot(dc, CX, 263, mods_line == launcher::lang::tx("Mods: none active") ? C_FAINT : C_OK);
  draw_text(dc, widen(mods_line + " | " + launcher::lang::tx("Open Mods folder")), LR(CX + 15, 260, CW - 15, 17), g_font, C_DIM, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  RECT sep = LR(CX, 282, CW, 1);
  fill(dc, sep, C_SEP);
  draw_text(dc, HINT_TEXT, LR(CX, 294, CW, 44), g_font_small, C_FAINT, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
  std::string version_status = g_active_version.empty() ? "" : launcher::lang::fill(launcher::lang::tx("Playing {version}"), launcher::lang::Args{{"version", g_active_version}});
  if (host::updater::rollback_state() == host::updater::RollbackState::Downloading)
    version_status = host::updater::rollback_message();
  else if (host::updater::rollback_state() == host::updater::RollbackState::Failed)
    version_status = host::updater::rollback_message();
  draw_text(dc, widen(version_status), LR(398, 348, 188, 32), g_font_small,
            host::updater::rollback_state() == host::updater::RollbackState::Failed ? C_BAD : C_DIM,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}




void paint_build(HDC dc) {
  round_rect(dc, LR(CX, 36, CW, 84), 10, RGB(0x14, 0x1C, 0x2C), RGB(0x14, 0x1C, 0x2C),
             g_building ? C_ACC_LO : RGB(0x33, 0x40, 0x60));
  draw_text(dc, L"Drop your Melee NTSC 1.02 ISO here", LR(CX, 54, CW, 30), g_font_big, C_TEXT,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  std::wstring sub = g_iso.empty() ? L"or press Browse on the Play page" : widen(iso_name());
  draw_text(dc, sub, drop_sub_rect(), g_font_small, C_FAINT, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

  draw_text(dc, L"Verifies the disc and precompiles shaders for your GPU. The ISO is never copied. Its location is remembered for later launches.",
            LR(CX, 132, CW, 44), g_font_small, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
  round_rect(dc, LR(CX, 226, CW, 106), 8, C_LOG_BG, C_LOG_BG, RGB(0x24, 0x2E, 0x46));
}

namespace crash_report { void note_launch(); }   // launcher_crash.inl, below: the replay viewer counts as a launch too
#include "launcher_replays.inl"
#include "launcher_match_character.inl"
#include "launcher_theme_picker.inl"
#include "launcher_crash.inl"
// Standard Win32 printing also renders the controls when the test window is off the desktop.
// Bottom of the z-order first, so a child over another (the lobby banner over the Lobby page)
// prints on top, as it shows on screen.
void print_client_children(HWND window, HDC dc) {
  std::vector<HWND> children;
  for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) children.push_back(child);
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    const HWND child = *it;
    if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
    RECT r; GetWindowRect(child, &r); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
    const int saved = SaveDC(dc);
    POINT origin{}; GetViewportOrgEx(dc, &origin);
    SetViewportOrgEx(dc, origin.x + r.left, origin.y + r.top, nullptr);
    IntersectClipRect(dc, 0, 0, r.right - r.left, r.bottom - r.top);
    SendMessageW(child, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    RestoreDC(dc, saved);
  }
}
void capture_test_client(HWND window, const char* name) {
  if (!g_launcher_test) return;
  RECT r; GetClientRect(window, &r);
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = r.right; info.bmiHeader.biHeight = -r.bottom;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
  HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen); void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (bitmap && pixels) {
    const auto old = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
    header.bfOffBits = sizeof header + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + r.right * r.bottom * 4;
    std::ofstream out(std::filesystem::u8path(g_dir + "/" + name), std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof header);
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char*>(pixels), r.right * r.bottom * 4);
    SelectObject(dc, old);
  }
  if (bitmap) DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
}
void open_settings(const std::string& tab);   // below; the Mods page's "Choose skins" opens the Mods tab
#include "launcher_mods.inl"

// The Language button: a menu of the languages, each named in itself. Picking one saves it and
// restarts the launcher, so every page is built again in that language.
void pick_language() {
  HMENU menu = CreatePopupMenu();
  const auto& list = launcher::lang::languages();
  for (size_t i = 0; i < list.size(); ++i)
    AppendMenuW(menu, MF_STRING | (list[i].code == launcher::lang::current() ? MF_CHECKED : 0), 1 + i, list[i].native);
  RECT r{}; GetWindowRect(g_lang_btn, &r);
  const int pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, r.left, r.bottom, 0, g_main, nullptr);
  DestroyMenu(menu);
  if (pick <= 0 || (size_t)pick > list.size() || list[pick - 1].code == launcher::lang::current()) return;
  g_language = list[pick - 1].code;
  save_ini();
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  ShellExecuteW(nullptr, L"open", exe, nullptr, widen(g_dir).c_str(), SW_SHOWNORMAL);
  DestroyWindow(g_main);
}

void paint(HWND hwnd, HDC target, RECT dirty) {
  RECT cr; GetClientRect(hwnd, &cr);
  HDC dc = CreateCompatibleDC(target);
  HBITMAP bmp = CreateCompatibleBitmap(target, cr.right, cr.bottom);
  HGDIOBJ oldb = SelectObject(dc, bmp);

  RECT content{S(RAIL_W), 0, cr.right, cr.bottom};
  vgrad(dc, content, C_CONTENT_TOP, C_CONTENT_BOT);
  paint_rail(dc);
  if (g_tab == 0) paint_play(dc); else if (g_tab == 1) paint_build(dc); else if(g_tab==3) paint_replays(dc);

  BitBlt(target, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top, dc, dirty.left, dirty.top, SRCCOPY);
  SelectObject(dc, oldb);
  DeleteObject(bmp);
  DeleteDC(dc);
}

// An owner-drawn control is drawn off screen and copied to the window in one step. Drawn straight
// into the control, every repaint showed the background fill, then the shape, then the text, so
// buttons flickered whenever they were redrawn.
template <typename Draw> void draw_buffered(DRAWITEMSTRUCT* di, Draw draw) {
  const RECT r = di->rcItem;
  const int w = r.right - r.left, h = r.bottom - r.top;
  HDC mem = w > 0 && h > 0 ? CreateCompatibleDC(di->hDC) : nullptr;
  HBITMAP bmp = mem ? CreateCompatibleBitmap(di->hDC, w, h) : nullptr;
  if (!bmp) { if (mem) DeleteDC(mem); draw(di); return; }
  HGDIOBJ old = SelectObject(mem, bmp);
  SetViewportOrgEx(mem, -r.left, -r.top, nullptr);   // the drawing code keeps the control's own coordinates
  DRAWITEMSTRUCT copy = *di; copy.hDC = mem;
  draw(&copy);
  SetViewportOrgEx(mem, 0, 0, nullptr);
  BitBlt(di->hDC, r.left, r.top, w, h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
}

void draw_button(DRAWITEMSTRUCT* di) {
  RECT wr; GetWindowRect(di->hwndItem, &wr);
  MapWindowPoints(nullptr, g_main, (POINT*)&wr, 2);
  RECT r = di->rcItem;
  bool disabled = (di->itemState & ODS_DISABLED) != 0;
  bool down = (di->itemState & ODS_SELECTED) != 0;
  bool primary = di->hwndItem == g_play_btn;
  const bool replay_check = di->CtlID == ID_REPLAY_ASPLAYED;
  bool checkbox = di->hwndItem == g_warm_cache_check || replay_check;
  if(di->CtlID==ID_THEME) {
    vgrad(di->hDC,di->rcItem,content_bg_at(wr.top),content_bg_at(wr.bottom));
    theme_wheel(di->hDC,16,16,13,true);
    return;
  }

  // Reproduce the parent's gradient behind the button so the rounded corners have the right colour.
  vgrad(di->hDC, r, content_bg_at(wr.top), content_bg_at(wr.bottom));

  if (checkbox) {
    RECT box{r.left + S(1), r.top + S(5), r.left + S(17), r.top + S(21)};
    round_rect(di->hDC, box, 3, C_FIELD, C_FIELD, C_BTN_BORDER);
    if (replay_check ? g_replay_as_played : g_warm_cache_on_play) {
      HPEN pen = CreatePen(PS_SOLID, S(2), disabled ? C_FAINT : C_ACC_HI);
      HGDIOBJ old_pen = SelectObject(di->hDC, pen);
      MoveToEx(di->hDC, box.left + S(3), box.top + S(8), nullptr);
      LineTo(di->hDC, box.left + S(7), box.bottom - S(4));
      LineTo(di->hDC, box.right - S(3), box.top + S(3));
      SelectObject(di->hDC, old_pen); DeleteObject(pen);
    }
    RECT label = r; label.left += S(24);
    const std::wstring check_label = replay_check ? launcher::lang::txw(L"Show it as it was played (uses the .trace file)")
                                                  : std::wstring(L"Warm cache from remembered ISO before Play");
    draw_text(di->hDC, check_label, label, g_font_small,
              disabled ? C_FAINT : C_DIM, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    return;
  }

  COLORREF top, bot, border = NO_FILL, text;
  if (primary) {
    top = C_PLAY_HI; bot = C_PLAY_LO; text = C_PLAY_TEXT;
    if (down) top = bot = C_PLAY_DOWN;
    if (disabled) { top = RGB(0x36, 0x33, 0x40); bot = RGB(0x30, 0x2D, 0x3A); text = RGB(0x78, 0x74, 0x84); }
  } else {
    top = bot = down ? C_BTN_DOWN : C_BTN; border = C_BTN_BORDER; text = C_TEXT;
    if (disabled) { top = bot = RGB(0x1A, 0x21, 0x32); border = RGB(0x28, 0x31, 0x47); text = RGB(0x5C, 0x68, 0x7E); }
  }
  round_rect(di->hDC, r, primary ? 10 : 7, top, bot, border);
  if (di->itemState & ODS_FOCUS) {
    RECT f{r.left + S(3), r.top + S(3), r.right - S(3), r.bottom - S(3)};
    round_rect(di->hDC, f, primary ? 8 : 5, NO_FILL, NO_FILL, primary ? C_PLAY_TEXT : C_ACC_LO);   // focus ring
  }
  wchar_t cap[128]{}; GetWindowTextW(di->hwndItem, cap, 128);
  draw_text(di->hDC, cap, r, primary ? g_font_big : g_font, text,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER, primary ? S(2) : 0);
}



// ---------------------------------------------------------------------------- behaviour

void refresh_updater();
void open_settings(const std::string& tab = std::string());
void settings_standby_start();
void settings_standby_stop();
void choose_rail_image();

void select_tab(int idx) {
  const int previous = g_tab;
  g_tab = idx;
  for (HWND h : g_play) if (h) ShowWindow(h, idx == 0 ? SW_SHOW : SW_HIDE);
  for (HWND h : g_build) if (h) ShowWindow(h, idx == 1 ? SW_SHOW : SW_HIDE);
  replay_layout();
  if (idx != 2) launcher::lobby::hide();
  if (g_main && previous != idx) {
    RECT size{0,0,S(idx==2?LOBBY_W:(idx==3?REPLAY_W:(idx==4?MODS_W:WIN_W))),S(idx==2?LOBBY_H:(idx==3?REPLAY_H:(idx==4?MODS_H:WIN_H)))};
    AdjustWindowRect(&size,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    int width=size.right-size.left, height=size.bottom-size.top;
    // The hidden test launcher (MELEE_LAUNCHER_TEST) stays off the desktop at its full size.
    RECT work{}; if(!g_launcher_test) SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    if(work.right>work.left && work.bottom>work.top) {
      width=std::min(width,int(work.right-work.left));
      height=std::min(height,int(work.bottom-work.top));
    }
    RECT current{}; GetWindowRect(g_main,&current);
    int x=current.left,y=current.top;
    if(work.right>work.left && work.bottom>work.top) {
      x=std::clamp(x,int(work.left),int(work.right-width));
      y=std::clamp(y,int(work.top),int(work.bottom-height));
    }
    SetWindowPos(g_main,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
  }
  mod_manager::show_page(idx == 4);
  if (idx == 0 && !g_slippi_missing) ShowWindow(g_slippi_btn, SW_HIDE);
  if (idx != 0) ShowWindow(g_update_btn, SW_HIDE);
  if (g_main) InvalidateRect(g_main, nullptr, FALSE);
}

void build_thread() {
  std::string why;
  log_line("Checking %s", g_iso.c_str());
  if (!verify_iso(g_iso, &why)) { log_line("Rejected: %s.", why.c_str()); PostMessageW(g_main, WM_APP_BUILD_DONE, 1, 0); return; }
  log_line("Melee NTSC 1.02 disc image: OK");
  save_ini();
  g_game_exe = game_exe();
  if (!file_exists(g_game_exe)) {
    std::string root = repo_root();
#ifdef MELEE_NO_SLIPPI   // the one game this launcher starts
    if (root.empty()) { log_line("melee_source.exe or melee_game.dll is missing next to this launcher and this is not a source checkout."); PostMessageW(g_main, WM_APP_BUILD_DONE, 1, 0); return; }
#else
    if (root.empty()) { log_line("melee_port.exe is missing next to this launcher and this is not a source checkout."); PostMessageW(g_main, WM_APP_BUILD_DONE, 1, 0); return; }
#endif
    log_line("Source checkout at %s: running build.bat (20 to 40 minutes the first time)", root.c_str());
    DWORD code = run_logged("cmd /c \"\"" + root + "\\build.bat\" \"" + g_iso + "\"\" <nul", root);
    if (code != 0 || !file_exists(g_game_exe)) { log_line("Build failed (exit code %lu).", code); PostMessageW(g_main, WM_APP_BUILD_DONE, 1, 0); return; }
  }
  log_line("Precompiling the graphics pipelines for this GPU (this runs once, about 15 to 30 seconds)");
  std::string cwd = work_dir();
  DWORD code = run_logged("\"" + g_game_exe + "\"" + game_args() + " --hidden --frames 30 --volume 0 --log-file launcher_build.log", cwd);
  if (code != 0) log_line("The game exited with code %lu during the pipeline precompile; see launcher_build.log.", code);
  log_line("Done. Press Play.");
  PostMessageW(g_main, WM_APP_BUILD_DONE, code, 0);
}

void start_build() {
  if (g_building || g_iso.empty()) return;
  g_building = true;
  EnableWindow(g_build_btn, FALSE); EnableWindow(g_play_btn, FALSE);
  SetWindowTextW(g_log, L"");
  InvalidateRect(g_main, nullptr, FALSE);
  std::thread(build_thread).detach();
}

void launch_game_now();

void warm_cache_thread() {
  log_line("Warming the graphics cache before launch (the first run may take 15 to 30 seconds)");
  const std::string cwd = work_dir();
  const DWORD code = run_logged("\"" + g_game_exe + "\"" + game_args() +
                                " --hidden --frames 30 --volume 0 --log-file launcher_warm.log", cwd);
  if (code != 0) log_line("Cache warmup exited with code %lu; starting the game anyway.", code);
  PostMessageW(g_main, WM_APP_CACHE_WARM_DONE, code, 0);
}

// Opens the game straight into its own PC settings panel. Same binary, same panel, same file: what
// is changed here is what the next launch uses, because the game reads port-settings.ini from this
// working directory before it opens a window.
void report_launch_error(DWORD error, const std::string& exe, const std::string& cwd) {
  wchar_t detail[2048]{};
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, error, 0, detail, 2048, nullptr);
  std::wstring message = L"Could not start:\n" + widen(exe) +
      L"\n\nWindows error " + std::to_wstring(error) + L": " + detail +
      L"\nWorking directory:\n" + widen(cwd) +
      L"\n\nPlease include this message when reporting the problem.";
  log_line("Launch failed: Windows error %lu; executable=%s; directory=%s",
           error, exe.c_str(), cwd.c_str());
  MessageBoxW(g_main, message.c_str(), L"Melee Unlocked Launcher", MB_ICONERROR);
}

// The settings window waits hidden in the background, fully started, so the Settings button only
// has to show it: starting a process and a graphics device on the click took about a second behind
// a black window. A fresh one is started after each use.
HANDLE g_settings_standby = nullptr, g_settings_show = nullptr, g_settings_quit = nullptr;
std::string g_settings_standby_exe;
void settings_standby_stop() {
  if (!g_settings_standby) return;
  SetEvent(g_settings_quit);
  if (WaitForSingleObject(g_settings_standby, 2000) != WAIT_OBJECT_0) TerminateProcess(g_settings_standby, 0);
  ResetEvent(g_settings_quit);
  CloseHandle(g_settings_standby); g_settings_standby = nullptr;
}
void settings_standby_start() {
  const std::string exe = game_exe();
  if (g_settings_standby && WaitForSingleObject(g_settings_standby, 0) == WAIT_TIMEOUT && g_settings_standby_exe == exe) return;
  settings_standby_stop();
  if (!file_exists(exe)) return;
  const DWORD pid = GetCurrentProcessId();
  if (!g_settings_show) g_settings_show = CreateEventW(nullptr, FALSE, FALSE, (L"Local\\MeleeUnlockedSettingsShow-" + std::to_wstring(pid)).c_str());
  if (!g_settings_quit) g_settings_quit = CreateEventW(nullptr, FALSE, FALSE, (L"Local\\MeleeUnlockedSettingsQuit-" + std::to_wstring(pid)).c_str());
  if (!g_settings_show || !g_settings_quit) return;
  ResetEvent(g_settings_show);
  const std::string cmd = "\"" + exe + "\" --settings-window --settings-standby " + std::to_string(pid) + " --settings-path \"" + settings_ini_path() + "\"";
  PROCESS_INFORMATION pi{};
  if (launcher::start_process(widen(exe), widen(cmd), widen(work_dir()), 0, pi) != ERROR_SUCCESS) return;
  CloseHandle(pi.hThread);
  g_settings_standby = pi.hProcess; g_settings_standby_exe = exe;
}
void choose_rail_image() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, launcher::lang::txw(L"Choose a picture...").c_str());
  if (!rail_image_path().empty()) AppendMenuW(menu, MF_STRING, 2, launcher::lang::txw(L"Bring back Bailey").c_str());
  POINT at; GetCursorPos(&at);
  const UINT picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, g_main, nullptr);
  DestroyMenu(menu);
  if (picked == 2) {
    for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".gif"}) DeleteFileW(widen(g_dir + "\\rail-image" + ext).c_str());
  } else if (picked == 1) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW dialog{sizeof dialog}; dialog.hwndOwner = g_main;
    dialog.lpstrFilter = L"Pictures (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0";
    const std::wstring dialog_title = launcher::lang::txw(L"Choose a picture for the launcher");
    dialog.lpstrTitle = dialog_title.c_str(); dialog.lpstrFile = file; dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    std::wstring ext = std::filesystem::path(file).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    int w = 0, h = 0;
    HBITMAP test = load_art_file(file, 64, 64, w, h);
    if (!test) { MessageBoxW(g_main, L"That picture could not be opened.", L"Melee Unlocked Launcher", MB_OK | MB_ICONWARNING); return; }
    DeleteObject(test);
    // A copy beside the launcher, so the picture stays when the original is moved or deleted.
    for (const char* old : {".png", ".jpg", ".jpeg", ".bmp", ".gif"}) DeleteFileW(widen(g_dir + "\\rail-image" + old).c_str());
    CopyFileW(file, (widen(g_dir + "\\rail-image") + ext).c_str(), FALSE);
  } else return;
  if (g_dog) { DeleteObject(g_dog); g_dog = nullptr; }
  g_dog_rect = RECT{};
  load_rail_art();
  InvalidateRect(g_main, nullptr, FALSE);
}
// `tab` names the tab the window opens on ("mods" from the Mods page's "Choose skins"); empty keeps
// the panel's own start. A window already waiting in standby cannot change its tab, so a tab request
// starts a fresh one.
void open_settings(const std::string& tab) {
  if (g_playing) return;
  g_game_exe = game_exe();
  if (!file_exists(g_game_exe)) {
    select_tab(1); refresh_updater(); start_build(); return;
  }
  if (!tab.empty()) settings_standby_stop();
  if (g_settings_standby && WaitForSingleObject(g_settings_standby, 0) == WAIT_TIMEOUT && g_settings_standby_exe == g_game_exe) {
    AllowSetForegroundWindow(GetProcessId(g_settings_standby));
    SetEvent(g_settings_show);
    CloseHandle(g_settings_standby); g_settings_standby = nullptr;
    SetTimer(g_main, ID_TIMER_STANDBY, 3000, nullptr);
    return;
  }
  SetTimer(g_main, ID_TIMER_STANDBY, 3000, nullptr);
  std::string cwd = work_dir();
  // --settings-window: the panel alone, no disc, no match engine, no prewarm.
  std::string cmd = "\"" + g_game_exe + "\" --settings-window --settings-path \"" + settings_ini_path() + "\"";
  if (!tab.empty()) cmd += " --settings-tab " + tab;
  PROCESS_INFORMATION pi{};
  const DWORD error = launcher::start_process(widen(g_game_exe), widen(cmd), widen(cwd), 0, pi);
  if (error != ERROR_SUCCESS) {
    report_launch_error(error, g_game_exe, cwd);
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
}

// A GAME BUILD segment was clicked: remember the choice for the next PLAY and repaint the row.
// build_seg_at() has already refused a Source Port segment with nothing installed behind it.
void select_engine(int engine) {
  if (g_engine == engine) return;
  g_engine = engine;
  save_ini();
  SetTimer(g_main, ID_TIMER_STANDBY, 800, nullptr);   // the panel of the build now picked
  invalidate(build_seg_rect(0)); invalidate(build_seg_rect(1));
}

bool lobby_game_ready() {
  if (g_iso.empty() || !file_exists(game_exe()) || g_building || g_playing ||
      !g_active_version.empty()) return false;
#ifdef MELEE_NO_SLIPPI
  return true;   // a peer-to-peer match is told its characters and stage: no menu, no account, no save needed
#endif
  // The Source Port needs no save, and its P2P Direct match no account either: ready without a
  // Slippi sign-in. The lobby knows whether one is there (set_account) and refuses a Slippi Direct
  // request without it, in both directions, with that reason.
  if (p2p_test_shown() && g_engine == ENGINE_SOURCE && source_available()) return true;
  if (g_slippi_missing) return false;
  if (g_engine == ENGINE_SOURCE && source_available()) return true;   // with the sign-in, as before
  // Slippi's normal boot loads/creates the Melee save. First-run card prompts need the player's
  // choice before we can promise that accepting a lobby request starts a match without input.
  for (const auto& base : {g_dir, work_dir()}) {
    std::error_code ec;
    const auto card = std::filesystem::u8path(base + "/User/GC/CardA");
    for (std::filesystem::directory_iterator it(card, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->path().extension() != ".gci") continue;
      std::ifstream f(it->path(), std::ios::binary); char id[6]{};
      if (f.read(id, sizeof id) && std::string(id, sizeof id) == "GALE01") return true;
    }
  }
  return false;
}

// Why the chosen disc cannot be played, or empty. Both engines need the retail NTSC 1.02 disc as the
// base; a compressed image or another game used to reach the engine and end in a crash report.
static std::wstring base_disc_problem(const std::string& path) {
  std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
  if (!f) return L"The disc image could not be opened. Choose your Melee NTSC 1.02 ISO again.";
  unsigned char hdr[8]{}; f.read((char*)hdr, 8);
  if (!std::memcmp(hdr, "RVZ\x01", 4) || !std::memcmp(hdr, "WIA\x01", 4) || !std::memcmp(hdr, "CISO", 4) ||
      (hdr[0] == 0x01 && hdr[1] == 0xC0 && hdr[2] == 0x0B && hdr[3] == 0xB1))
    return L"This disc image is compressed (RVZ, GCZ, WIA or CISO). In Dolphin, right-click the game, choose Convert File and pick ISO, then choose that ISO here.";
  if (std::memcmp(hdr, "GALE01", 6) != 0)
    return L"This file is not a Melee NTSC disc image. Choose your own Melee NTSC 1.02 ISO.";
  if (hdr[7] != 2)
    return L"This is Melee NTSC 1.0" + std::to_wstring((int)hdr[7]) + L". Version 1.02 is required.";
  // A mod disc keeps the game id. Its game code (main.dol) differs from the original, which both
  // engines check at startup (host.cpp disc_has_vanilla_dol: the same size and SHA-1).
  unsigned char at[4]{};
  f.seekg(0x420); f.read((char*)at, 4);
  const uint32_t dol = ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) | ((uint32_t)at[2] << 8) | at[3];
  std::vector<unsigned char> image(0x4385E0u);
  f.seekg(dol); f.read((char*)image.data(), (std::streamsize)image.size());
  if (!f) return L"This file is not a full Melee disc image. Choose a clean, uncompressed Melee NTSC 1.02 ISO.";
  const unsigned char expected[20] = {0x08,0xe0,0xbf,0x20,0x13,0x4d,0xfc,0xb2,0x60,0x69,0x96,0x71,0x00,0x45,0x27,0xb2,0xd6,0xbb,0x1a,0x45};
  unsigned char digest[20]{};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) return {};
  const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, image.data(), (ULONG)image.size(), digest, sizeof digest);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status >= 0 && std::memcmp(digest, expected, sizeof digest) != 0)
    return L"This is a modded Melee disc. The disc chosen here has to be your original Melee NTSC 1.02 ISO. Add the modded disc under Mods instead.";
  return {};
}

void start_game() {
  if (g_playing || g_iso.empty()) return;
  if (const std::wstring problem = base_disc_problem(g_iso); !problem.empty()) {
    MessageBoxW(g_main, problem.c_str(), L"Melee Unlocked Launcher", MB_OK | MB_ICONWARNING);
    return;
  }
  g_game_exe = !g_mod_launch_iso.empty() ? (g_mod_launch_engine == "source" ? source_exe_dir() + "\\melee_source.exe" : static_recomp_exe()) : game_exe();
  if (!file_exists(g_game_exe) && !g_mod_launch_iso.empty()) {
#ifdef MELEE_NO_SLIPPI   // one game here: it is that game which is missing
    MessageBoxW(g_main,L"The game is not installed beside this launcher. Install it before playing this mod.",L"Mods",MB_ICONINFORMATION);
#else
    MessageBoxW(g_main,L"The required game engine is not installed. Install it before playing this mod.",L"Mods",MB_ICONINFORMATION);
#endif
    g_mod_launch_iso.clear(); g_mod_launch_engine.clear(); g_mod_launch_kind.clear(); return;
  }
  if (!file_exists(g_game_exe)) {
    select_tab(1); refresh_updater(); start_build(); return;
  }
  if (g_warm_cache_on_play) {
    g_building = true;
    EnableWindow(g_play_btn, FALSE);
    EnableWindow(g_build_btn, FALSE);
    std::thread(warm_cache_thread).detach();
    return;
  }
  launch_game_now();
}

std::string static_recomp_exe() {
  const bool want_compat = g_cpu_build == CPU_COMPAT || (g_cpu_build == CPU_AUTO && !cpu_has_avx2());
  const std::string dir = active_dir();
  if (want_compat && file_exists(dir + "\\melee_port_compat.exe")) return dir + "\\melee_port_compat.exe";
  return dir + "\\melee_port.exe";
}
void launch_game_now() {
  std::string cwd = work_dir();
  std::string exe = g_game_exe, args = game_args();
  if (!g_mod_launch_iso.empty()) {
    exe = g_mod_launch_engine == "source" ? source_exe_dir() + "\\melee_source.exe" : static_recomp_exe();
    const std::string base = active_dir();
    args = g_mod_launch_engine == "source"
        ? " --iso \"" + g_iso + "\" " + (g_mod_launch_kind == "te" ? "--mod-gci" : "--mod-iso") + " \"" + g_mod_launch_iso + "\""
        : " --iso \"" + g_mod_launch_iso + "\" --mod-base-iso \"" + g_iso + "\"";
    args += " --threaded-renderer --settings-path \"" + settings_ini_path() + "\"";
    // Explicit Source layers take precedence over automatic layers. Keep enabled TE alongside a disc.
    if (g_mod_launch_engine == "source" && g_mod_launch_kind != "te")
      for (const auto& entry : mod_manager::installed())
        if (entry.second.kind == "te" && entry.second.enabled && launcher::mod_catalog::playable(entry.second))
          args += " --mod-gci \"" + entry.second.path + "\"";

#ifdef MELEE_NO_SLIPPI
    // As in game_args(): that game has no user folder option and refuses it.
    if (file_exists(base + "\\Sys\\codehandler.bin"))
      args += " --sys-dir \"" + base + "\\Sys\" --replay-dir \"" + g_dir +
              "\\Replays\" --card-dir \"" + g_dir + "\\User\\GC\\Mods\\" + g_mod_launch_key + "\"";
#else
    if (file_exists(base + "\\Sys\\codehandler.bin"))
      args += " --sys-dir \"" + base + "\\Sys\" --user-dir \"" + g_dir + "\\User\\Slippi\" --replay-dir \"" + g_dir +
              "\\Replays\" --card-dir \"" + g_dir + "\\User\\GC\\Mods\\" + g_mod_launch_key + "\"";
#endif
    g_mod_launch_iso.clear(); g_mod_launch_engine.clear(); g_mod_launch_kind.clear();
  }
  std::string cmd = "\"" + exe + "\"" + args + g_lobby_launch_args +
      " --lobby-status-file \"" + g_dir + "\\lobby-game-status.json\"";
  g_lobby_game_active = !g_lobby_launch_args.empty();
  g_lobby_launch_args.clear();
  // Test runs only (MELEE_LAUNCHER_TEST): extra game arguments (hidden, local peering instead of
  // Slippi's servers) and the exact command line written to a file for the lobby gate to check.
  if (g_launcher_test) {
    if (const char* extra = std::getenv("MELEE_LAUNCHER_TEST_GAME_ARGS")) cmd += std::string(" ") + extra;
    if (const char* log_path = std::getenv("MELEE_LAUNCHER_TEST_LAUNCH_LOG"))
      if (FILE* f = std::fopen(log_path, "ab")) { std::fprintf(f, "%s\n", cmd.c_str()); std::fclose(f); }
  }
  PROCESS_INFORMATION pi{};
  const DWORD error = launcher::start_process(widen(exe), widen(cmd), widen(cwd), 0, pi);
  if (error != ERROR_SUCCESS) {
    launcher::lobby::game_running(false);
#ifdef MELEE_NO_SLIPPI
    launcher::lobby::restart_after_match();   // no game took the lobby's port after all
#endif
    report_launch_error(error, exe, cwd);
    return;
  }
  CloseHandle(pi.hThread);
  crash_report::note_launch();
  g_playing = true;
  launcher::lobby::game_running(true);
  EnableWindow(g_play_btn, FALSE);
  set_text(g_play_btn, "Running");
  ShowWindow(g_main, SW_MINIMIZE);
  std::thread([h = pi.hProcess] { WaitForSingleObject(h, INFINITE); DWORD code=0; GetExitCodeProcess(h,&code); CloseHandle(h); PostMessageW(g_main, WM_APP_GAME_DONE, code, 0); }).detach();
}

void set_iso(const std::string& path) {
  g_iso = path; set_text(g_iso_edit, g_iso);
  EnableWindow(g_play_btn, !g_iso.empty() && !g_playing && !g_building);
  EnableWindow(g_build_btn, !g_iso.empty() && !g_building);
  invalidate(drop_sub_rect());
}

// An NKit image is the retail disc trimmed and repacked: not the 1.02 ISO and not a mod. Said
// plainly instead of filing it under Mods, where Play then needed a retail disc the player lacks.
bool nkit_image(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  char tag[4] = {};
  if (!in.seekg(0x200) || !in.read(tag, 4)) return false;
  return std::memcmp(tag, "NKIT", 4) == 0;
}
void explain_nkit() {
  MessageBoxW(g_main, L"This is an NKit image, not a full disc image. Melee Unlocked needs the full Melee NTSC 1.02 ISO.\n\n"
              L"Restore it with NKit's \"Recover to ISO\" and select the restored .iso.", L"NKit image", MB_ICONINFORMATION);
}

void browse() {
  wchar_t file[MAX_PATH]{};
  OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = g_main; ofn.lpstrFilter = L"GameCube disc image (*.iso;*.gcm)\0*.iso;*.gcm\0All files\0*.*\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&ofn)) {
    if (nkit_image(file)) { explain_nkit(); return; }
    if (mod_manager::hash_file(file, BCRYPT_MD5_ALGORITHM) != mod_manager::kVanillaMd5) {
      mod_manager::open(); std::thread(mod_manager::install_file, std::wstring(file), std::string(), g_iso).detach();
    } else { set_iso(narrow(file)); save_ini(); }
  }
}

bool g_update_prompted = false;
void refresh_updater() {
  using host::updater::State;
  auto st = host::updater::state();
  if (!g_rollback_consumed && host::updater::rollback_state() == host::updater::RollbackState::Ready) {
    g_rollback_consumed = true;
    const std::string folder = host::updater::rollback_folder();
    const auto slash = folder.find_last_of("\\/");
    if (slash != std::string::npos && safe_version_folder(folder.substr(slash + 1))) {
      g_active_version = folder.substr(slash + 1);
      save_ini();
    }
    InvalidateRect(g_main, nullptr, FALSE);
  }
  if (!g_rollback_consumed && host::updater::rollback_state() == host::updater::RollbackState::Failed) {
    g_rollback_consumed = true;
    InvalidateRect(g_main, nullptr, FALSE);
  }
  // One yes/no prompt per launch when a newer release exists. Nothing installs without a Yes.
  if (st == State::UpdateAvailable && g_install_requested) {
    g_install_requested = false;
    g_update_prompted = true;
    settings_standby_stop();
    host::updater::download_and_install();
    st = host::updater::state();
  } else if (st == State::UpdateAvailable && !g_update_prompted) {
    g_update_prompted = true;
    std::string text = "Melee Unlocked " + host::updater::latest_version() + " is available (you have " MELEE_PORT_VERSION ").\n\nUpdate now? The game folder is updated in place; settings, saves and replays are kept.\n\nNo keeps this version; the Update button stays on the Play page.";
    if (MessageBoxW(g_main, widen(text).c_str(), L"Melee Unlocked Launcher", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) { settings_standby_stop(); host::updater::download_and_install(); }
    st = host::updater::state();
  }
  std::string line = "Version " MELEE_PORT_VERSION ". " + host::updater::message();
  if (st == State::Checking) line = "Version " MELEE_PORT_VERSION ". Checking for updates...";
  COLORREF d = st == State::Checking ? C_FAINT : st == State::UpdateAvailable ? C_WARN : st == State::Failed ? C_BAD : C_OK;
  // Only repaint when something actually changed: this runs on a 500 ms timer, and repainting the
  // label's rect from the background every tick is what stops old text showing through the new.
  if (line != g_version_line || d != g_version_dot) {
    g_version_line = line; g_version_dot = d;
    if (g_tab == 0) { invalidate(version_text_rect()); invalidate(LR(CX, 239, 8, 8)); }
  }
  ShowWindow(g_update_btn, (st == State::UpdateAvailable || st == State::Failed) && g_tab == 0 ? SW_SHOW : SW_HIDE);
  set_text(g_update_btn, st == State::Failed ? "Retry" : "Update and restart");
  const bool choosing = host::updater::rollback_state() == host::updater::RollbackState::Downloading;
  EnableWindow(g_versions_btn, !choosing);
  EnableWindow(g_update_btn, !choosing);
}

void choose_version() {
  if (host::updater::rollback_state() == host::updater::RollbackState::Downloading) return;
  const auto catalog = host::updater::releases();
  std::vector<std::string> installed;
  std::error_code ec;
  const std::filesystem::path versions = std::filesystem::path(g_dir) / "Versions";
  for (std::filesystem::directory_iterator it(versions, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec) || ec) { ec.clear(); continue; }
    const std::string id = it->path().filename().string();
    if (safe_version_folder(id) && file_exists((it->path() / "melee_port.exe").string()) &&
        file_exists((it->path() / "Sys" / "codehandler.bin").string())) installed.push_back(id);
  }
  if (catalog.empty() && installed.empty()) {
    MessageBoxW(g_main, L"Version history is still loading or could not be reached. Try again shortly.", L"Melee Unlocked Launcher", MB_OK | MB_ICONINFORMATION);
    return;
  }
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | (g_active_version.empty() ? MF_CHECKED : 0), 1900, launcher::lang::txw(L"Current install").c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  if (!installed.empty()) {
    HMENU local = CreatePopupMenu();
    for (size_t i = 0; i < installed.size(); ++i)
      AppendMenuW(local, MF_STRING | (g_active_version == installed[i] ? MF_CHECKED : 0), 3000 + (UINT)i, widen(installed[i]).c_str());
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)local, launcher::lang::txw(L"Installed versions").c_str());
  }
  for (size_t first = 0; first < catalog.size(); first += 10) {
    HMENU group = CreatePopupMenu();
    for (size_t i = first; i < catalog.size() && i < first + 10; ++i) {
      const bool available = catalog[i].legacy;
      std::wstring name = L"v" + widen(catalog[i].version);
      int y = 0, m = 0, d = 0;
      if (std::sscanf(catalog[i].published.c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3 && m >= 1 && m <= 12) {
        static const wchar_t* months[12] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
        wchar_t date[32]; swprintf_s(date, L"\t%ls %d, %d", months[m - 1], d, y); name += date;
      }
      if (!available) name += L" (" + launcher::lang::txw(L"no standard build") + L")";
      AppendMenuW(group, MF_STRING | (available ? 0 : MF_GRAYED), 2000 + (UINT)i, name.c_str());
    }
    std::wstring title = first == 0 ? launcher::lang::txw(L"Recent releases") : launcher::lang::txw(L"Older releases") + L" " + std::to_wstring(first + 1) + L"-" + std::to_wstring(std::min(first + 10, catalog.size()));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)group, title.c_str());
  }
  RECT r{}; GetWindowRect(g_versions_btn, &r);
  const UINT picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top, 0, g_main, nullptr);
  DestroyMenu(menu);
  SetTimer(g_main, ID_TIMER_STANDBY, 800, nullptr);
  if (picked == 1900) { g_active_version.clear(); save_ini(); InvalidateRect(g_main, nullptr, FALSE); return; }
  if (picked >= 3000 && picked - 3000 < installed.size()) {
    g_active_version = installed[picked - 3000]; save_ini(); InvalidateRect(g_main, nullptr, FALSE); return;
  }
  if (picked < 2000 || picked - 2000 >= catalog.size()) return;
  const std::string version = catalog[picked - 2000].version;
  const std::string label = "Install and select Melee Unlocked " + version + "?\n\nIt will be kept in Versions beside the current build. Your ISO, settings, saves, and replays stay shared.";
  if (MessageBoxW(g_main, widen(label).c_str(), L"Choose game version", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
  if (host::updater::install_release(version, false, g_dir)) {
    g_rollback_consumed = false;
    InvalidateRect(g_main, nullptr, FALSE);
  }
}

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font, DWORD ex) {
  const std::wstring shown = launcher::lang::txw(text ? text : L"");
  HWND hw = CreateWindowExW(ex, cls, shown.c_str(), WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_main, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(hw, WM_SETFONT, (WPARAM)(font ? font : g_font), TRUE);
  return hw;
}
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_main = hwnd;
      load_language();
      load_rail_art();
      // Play page
      int i = 0;
      g_play[i++] = g_iso_edit = make(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, CX + 10, 66, 360, 18, ID_ISO_EDIT);
      g_play[i++] = make(L"BUTTON", L"Browse...", BS_OWNERDRAW, 602, 58, 96, 34, ID_BROWSE);
      g_play[i++] = g_play_btn = make(L"BUTTON", L"PLAY", BS_OWNERDRAW, CX, 134, CW, 62, ID_PLAY, g_font_big);
#ifdef MELEE_NO_SLIPPI
      // No account button and no version picker in this build (the older versions it lists are the
      // other Game Build). Their g_play slots stay, empty, so the indices below keep their meaning,
      // and Mods and Language start at the left edge where the version picker was.
      g_play[i++] = g_slippi_btn = nullptr;
      g_play[i++] = g_update_btn = make(L"BUTTON", L"Update and restart", BS_OWNERDRAW, 554, 246, 144, 30, ID_UPDATE);
      g_play[i++] = g_versions_btn = nullptr;
      const int send_x = CX, lang_x = CX + 156, lang_w = 176;
#else
      g_play[i++] = g_slippi_btn = make(L"BUTTON", L"Get Slippi Launcher", BS_OWNERDRAW, 554, 214, 144, 30, ID_SLIPPI_GET);
      g_play[i++] = g_update_btn = make(L"BUTTON", L"Update and restart", BS_OWNERDRAW, 554, 246, 144, 30, ID_UPDATE);
      g_play[i++] = g_versions_btn = make(L"BUTTON", L"Choose version...", BS_OWNERDRAW, CX, 348, 174, 32, ID_VERSIONS);
      const int send_x = 392, lang_x = 548, lang_w = 112;
#endif
      g_play[i++] = make(L"BUTTON",L"Launcher color",BS_OWNERDRAW,666,348,32,32,ID_THEME);
      {
        const auto* current = launcher::lang::find(launcher::lang::current());
        g_lang_btn = CreateWindowExW(0, L"BUTTON", current ? current->native : L"English", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     S(lang_x), S(348), S(lang_w), S(32), hwnd, (HMENU)(INT_PTR)ID_LANGUAGE, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_lang_btn, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_play[i++] = g_lang_btn;
      }
      // In the bottom row where a second Mods button was (the rail and the mods line open Mods):
      // logs, the marked match traces and a note go to the report relay (launcher_crash.inl).
      g_play[i++] = make(L"BUTTON", L"Send recent game logs", BS_OWNERDRAW, send_x, 348, 150, 32, ID_SEND_LOGS);
      g_play[i++] = nullptr;   // keeps the slot count
      {
        HWND tips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,
                                  0,0,0,0,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        TOOLINFOW tip{sizeof tip};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=hwnd;
        std::wstring tip_text=launcher::lang::txw(L"Launcher color");   // the tooltip keeps its own copy
        tip.uId=(UINT_PTR)g_play[6];tip.lpszText=tip_text.data();
        SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tip);
      }
      // Build page
      g_build[0] = g_build_btn = make(L"BUTTON", L"Build", BS_OWNERDRAW, CX, 182, 120, 32, ID_BUILD);
      g_build[1] = g_warm_cache_check = make(L"BUTTON", L"", BS_OWNERDRAW, CX + 136, 188, 350, 24, ID_WARM_CACHE);
      g_build[2] = g_log = make(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, CX + 2, 228, CW - 4, 102, ID_LOG, g_font_mono);
      SetWindowTheme(g_log, L"DarkMode_Explorer", nullptr);   // a dark scrollbar where the OS has one
      create_replay_controls();
      select_tab(0);
      load_ini();
#ifdef MELEE_NO_SLIPPI
      // An older version kept in Versions is the other Game Build, and nothing here could switch
      // back from one: the current install always.
      g_active_version.clear();
#endif
      set_iso(g_iso);
      if (!g_iso.empty()) save_ini();   // remember wherever it came from
#ifdef MELEE_NO_SLIPPI
      g_engine = ENGINE_SOURCE;   // the only Game Build here, whatever launcher.ini last held
#endif
      refresh_account_line();
      ShowWindow(g_slippi_btn, g_slippi_missing ? SW_SHOW : SW_HIDE);
#ifndef MELEE_NO_SLIPPI   // the public releases are the other build: this one must never install them over itself
      if (!g_launcher_test) { host::updater::check(MELEE_PORT_VERSION); refresh_updater(); }
#endif
      SetTimer(hwnd, ID_TIMER, 500, nullptr);
      if (!g_launcher_test) SetTimer(hwnd, ID_TIMER_STANDBY, 1500, nullptr);
      DragAcceptFiles(hwnd, TRUE);
      return 0;
    }
    case WM_ERASEBKGND: return 1;        // WM_PAINT paints every pixel from a memory DC
    case WM_APP + 46: {
      // An isolated fixture opened by the hidden launcher capture harness.
      if(!g_launcher_test) return 0;
      const char* file=std::getenv("MELEE_LAUNCHER_TEST_REPLAY_STATS");
      if(!file||!*file) return 0;
      ++g_replay_gen;
      const auto path=std::filesystem::u8path(file);
      g_replay_files={path};g_replay_info={launcher::replay::inspect(path,true)};
      g_replay_traces={launcher::trace::load(path)};
      g_replay_view=0;g_stats_scroll=0;g_trace_hover=-1;
      select_tab(3);replay_layout();stats_changed();return 0;
    }
    case WM_PRINTCLIENT: {
      RECT r; GetClientRect(hwnd, &r); paint(hwnd, (HDC)wp, r); print_client_children(hwnd, (HDC)wp); return 0;
    }
    case WM_APP + 41: capture_test_client(hwnd, "launcher-capture.bmp"); return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
      paint(hwnd, dc, ps.rcPaint);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DRAWITEM: if(((DRAWITEMSTRUCT*)lp)->CtlID==ID_REPLAY_LIST) draw_replay((DRAWITEMSTRUCT*)lp); else draw_buffered((DRAWITEMSTRUCT*)lp, draw_button); return TRUE;
    case WM_MEASUREITEM: if(((MEASUREITEMSTRUCT*)lp)->CtlID==ID_REPLAY_LIST) { ((MEASUREITEMSTRUCT*)lp)->itemHeight=S(84); return TRUE; } break;
    case WM_MOUSEMOVE: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      int hot = -1;
      for (int i = 0; i < 6; ++i) { RECT r = nav_rect(i); if (nav_shown(i) && PtInRect(&r, p)) hot = i; }
      if (hot != g_nav_hot) {
        int was = g_nav_hot; g_nav_hot = hot;
        if (was >= 0) invalidate(nav_rect(was));
        if (hot >= 0) invalidate(nav_rect(hot));
      }
      g_seg_hot = build_seg_at(p);   // only the cursor changes over a segment, so nothing to repaint
      g_dog_hot = PtInRect(&g_dog_rect, p) != 0;
      if (!g_tracking) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_tracking = true; }
      return 0;
    }
    case WM_MOUSELEAVE:
      g_tracking = false;
      g_seg_hot = -1;
      if (g_nav_hot >= 0) { int was = g_nav_hot; g_nav_hot = -1; invalidate(nav_rect(was)); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT && (g_nav_hot >= 0 || g_seg_hot >= 0 || g_dog_hot)) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
      break;
    case WM_LBUTTONDOWN: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      RECT mods_line = LR(CX, 260, CW, 17);
      if (g_tab == 0 && PtInRect(&mods_line, p)) {
        std::error_code ec; std::filesystem::create_directories(widen(mod_manager::mods_dir()), ec);
        ShellExecuteW(nullptr, L"open", widen(mod_manager::mods_dir()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
      }
      for (int i = 0; i < 6; ++i) {
        RECT r = nav_rect(i);
        if (!nav_shown(i) || !PtInRect(&r, p)) continue;
        // Settings is not a page here. The launcher used to draw its own copy of the options, which
        // is a second settings UI to keep in step with the real one; this opens the game's own F1
        // panel instead, so it is the same UI by construction and cannot drift from it.
        if (i == 1) { open_settings(); return 0; }
        if (i == 4) { select_tab(3); refresh_replays(); return 0; }
        if (i == 5) { mod_manager::open(); return 0; }
        if (i == 3) {
          refresh_account_line();
          select_tab(2);
          launcher::lobby::open(std::string(MELEE_PORT_VERSION) + (g_engine == ENGINE_SOURCE ? ":source" : ":recomp"),
                                lobby_game_ready());
          return 0;
        }
        const int tab = i == 0 ? 0 : 1;
        if (g_tab != tab) { select_tab(tab); refresh_updater(); }
        return 0;
      }
      if (const int seg = build_seg_at(p); seg >= 0) select_engine(seg == 0 ? ENGINE_SOURCE : ENGINE_LEGACY);
      if (PtInRect(&g_dog_rect, p)) choose_rail_image();
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_WARM_CACHE:
          if (HIWORD(wp) == BN_CLICKED) {
            g_warm_cache_on_play = !g_warm_cache_on_play;
            InvalidateRect(g_warm_cache_check, nullptr, FALSE);
            save_ini();
          }
          break;
        case ID_BROWSE: browse(); break;
        case ID_PLAY: start_game(); break;
        case ID_BUILD: start_build(); break;
        case ID_UPDATE:
#ifdef MELEE_NO_SLIPPI
          break;   // no update source for this build (see the check at start)
#endif
          if (host::updater::rollback_state() == host::updater::RollbackState::Downloading) break;
          if (host::updater::state() == host::updater::State::Failed) host::updater::check(MELEE_PORT_VERSION);
          else { settings_standby_stop(); host::updater::download_and_install(); }
          refresh_updater();
          break;
        case ID_VERSIONS: choose_version(); break;
        case ID_THEME: open_theme_picker(); break;
        case ID_LANGUAGE: pick_language(); break;
        case ID_MODS: mod_manager::open(); break;
        case ID_SEND_LOGS:
          crash_report::send_logs(hwnd, work_dir(), g_engine == ENGINE_SOURCE ? "Source Port" : "Static Recomp",
                                  {g_dir + "\\Replays", work_dir() + "\\Replays"});
          break;
        case ID_REPLAY_BROWSE: browse_replay(); break;
        case ID_REPLAY_WATCH: watch_replay(); break;
        case ID_REPLAY_REFRESH: refresh_replays(); break;
        case ID_REPLAY_LIST: if(HIWORD(wp)==LBN_SELCHANGE) replay_selection_changed(); break;
        case ID_REPLAY_BACK: replay_back(); break;
        case ID_REPLAY_PREV: replay_step(-1); break;
        case ID_REPLAY_NEXT: replay_step(1); break;
        case ID_REPLAY_SEARCH: if(HIWORD(wp)==EN_CHANGE) replay_filter_changed(); break;
        case ID_REPLAY_SORT: pick_replay_sort(); break;
        case ID_REPLAY_HIDESHORT: replay_hide_short_toggle(); break;
        case ID_REPLAY_QUEUE: watch_replay_queue(); break;
        case ID_REPLAY_QUEUE_CLEAR: replay_queue_clear(); break;
        case ID_REPLAY_ASPLAYED:
          if (HIWORD(wp) == BN_CLICKED) {
            g_replay_as_played = !g_replay_as_played;
            InvalidateRect(g_replays[7], nullptr, FALSE);
            save_ini();
          }
          break;
#ifndef MELEE_NO_SLIPPI   // that button does not exist in the build without an account
        case ID_SLIPPI_GET: ShellExecuteW(hwnd, L"open", L"https://slippi.gg/downloads", nullptr, nullptr, SW_SHOWNORMAL); break;
#endif
      }
      return 0;
    case WM_DROPFILES: {
      const HDROP drop = (HDROP)wp;
      const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      std::wstring game_disc;
      const std::string base_iso = g_iso;
      for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring file(length + 1, L'\0');
        if (!length || !DragQueryFileW(drop, index, file.data(), length + 1)) continue;
        file.resize(length);
        // Mod downloads go to the mod manager. A disc is the game disc when it is vanilla 1.02, and a
        // mod otherwise: a modded disc is never an error, it is added to the Mods folder.
        std::wstring lower = file; for (auto& c : lower) c = (wchar_t)towlower(c);
        const bool disc = lower.size() > 4 && (lower.compare(lower.size() - 4, 4, L".iso") == 0 || lower.compare(lower.size() - 4, 4, L".gcm") == 0);
        bool vanilla = false;
        if (disc) {
          HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
          vanilla = mod_manager::hash_file(file, BCRYPT_MD5_ALGORITHM) == mod_manager::kVanillaMd5;
          SetCursor(old);
        }
        if (nkit_image(file)) { explain_nkit(); continue; }
        if (mod_manager::is_mod_file(file) || (disc && !vanilla)) {
          mod_manager::open();
          std::thread(mod_manager::install_file, file, std::string(), base_iso).detach();
        } else if (game_disc.empty()) game_disc = file;
      }
      DragFinish(drop);
      if (!game_disc.empty()) {
        set_iso(narrow(game_disc)); select_tab(1); refresh_updater(); start_build();
      }
      return 0;
    }
    case WM_APP_LOG: {
      auto* s = (std::string*)lp;
      int len = GetWindowTextLengthW(g_log);
      SendMessageW(g_log, EM_SETSEL, len, len);
      SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)widen(*s).c_str());
      delete s;
      return 0;
    }
    case WM_APP_REPLAYS_LISTED: replays_listed((ReplayScan*)lp); return 0;
    case WM_APP_REPLAY_LOADED: replay_loaded((ReplayLoaded*)lp); return 0;
    case WM_APP_BUILD_DONE:
      g_building = false;
      set_iso(g_iso);
      if (wp == 0) { select_tab(0); refresh_updater(); } else InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_APP_GAME_DONE:
      g_playing = false;
      // A replay queue that went on to its next replay: that game is running now, so nothing below
      // applies yet and the launcher stays minimized.
      if(replay_game_done((DWORD)wp)) return 0;
      launcher::lobby::game_running(false);
      set_text(g_play_btn, "PLAY");
#ifdef MELEE_NO_SLIPPI
      launcher::lobby::restart_after_match();   // before any dialog: the lobby is back while it is read
      if (g_lobby_game_active && wp != 0 && !g_launcher_test)
        MessageBoxW(hwnd, L"The match could not connect or the game exited with an error. A direct connection needs one of you to be reachable: the same network, or a forwarded port. Check melee_port.log, then request another match.", L"Lobby match ended", MB_ICONWARNING);
#else
      // After a P2P Direct match the lobby opens again on its port (it does nothing after any other
      // match), before any dialog: the lobby is back while it is read.
      launcher::lobby::restart_after_match();
      if (g_lobby_game_active && wp != 0 && g_lobby_game_p2p) {
        if (!g_launcher_test)
          MessageBoxW(hwnd, L"The P2P Direct match could not connect or the game exited with an error. A direct connection needs one of you to be reachable: the same network, or a forwarded port. Check melee_port.log, then request another match.", L"Lobby match ended", MB_ICONWARNING);
      } else if (g_lobby_game_active && wp != 0 && !g_launcher_test)
        MessageBoxW(hwnd, L"The lobby match could not complete its connection or the game exited with an error. Check your Slippi login/code and melee_port.log, then request another match.", L"Lobby match ended", MB_ICONWARNING);
      g_lobby_game_p2p = false;
#endif
      g_lobby_game_active = false;
      set_iso(g_iso);
      // Also after a zero exit code: offer() only acts on a crash file newer than this launch.
      crash_report::offer(hwnd, work_dir(), g_engine == ENGINE_SOURCE ? "Source Port" : "Static Recomp");
      // Restore, but never take the foreground. The game restarting itself looks exactly like the
      // game exiting from here, so activating would put the launcher in front of the new instance
      // the moment it started.
      ShowWindow(hwnd, SW_SHOWNOACTIVATE);
      return 0;
    case WM_APP_CACHE_WARM_DONE:
      g_building = false;
      EnableWindow(g_play_btn, !g_iso.empty() && !g_playing);
      EnableWindow(g_build_btn, !g_iso.empty());
      launch_game_now();
      return 0;
    case WM_TIMER: {
      if (wp == ID_TIMER_STANDBY) { KillTimer(hwnd, ID_TIMER_STANDBY); settings_standby_start(); return 0; }
      // The lobby answers requests from here too (a banner), also before its page was opened, and
      // hears about a changed Game Build, setup or Mods folder within two seconds.
      launcher::lobby::tick_ui();
      {
        static ULONGLONG last_state = 0;
        if (GetTickCount64() - last_state > 2000) {
          last_state = GetTickCount64();
          std::string hint;
          if (g_iso.empty()) hint = "no disc";
          else if (!file_exists(game_exe())) hint = "game not built";
#ifndef MELEE_NO_SLIPPI   // the build without that layer has nothing to sign in to
          else if (g_slippi_missing) hint = "not signed in to Slippi";
#endif
          launcher::lobby::set_game_state(std::string(MELEE_PORT_VERSION) + (g_engine == ENGINE_SOURCE ? ":source" : ":recomp"),
                                          lobby_game_ready(), hint);
#ifdef MELEE_NO_SLIPPI
          // Lobby mods run on the Static Recomp, which this launcher does not start: none are offered.
          launcher::lobby::refresh_mods({mod_manager::mods_dir() + "\\.cache\\detected.json"}, false);
          // The name is typed on the Lobby page: the Play page follows it.
          const std::string line_before = g_slippi_line;
          refresh_account_line();
          if (line_before != g_slippi_line) invalidate(slippi_text_rect());
#else
          launcher::lobby::refresh_mods({mod_manager::mods_dir() + "\\.cache\\detected.json"}, file_exists(static_recomp_exe()));
#endif
        }
      }
      launcher::lobby::Match match;
#ifdef MELEE_NO_SLIPPI
      if (launcher::lobby::take_match(match)) {
        // A peer-to-peer match: both launchers agreed on ports, characters, stage and seed over the
        // lobby's encrypted channel, and the lobby hands over the game's --p2p-* arguments already
        // checked field by field (launcher_lobby_p2p.cpp, p2p_arguments). The game connects to the
        // other player by itself; no matchmaking server and no account are involved.
        if (!match.p2p_args.empty() && match.mode == "vanilla" && !g_playing && !g_building && !g_iso.empty() &&
            g_active_version.empty() && file_exists(game_exe())) {
          std::error_code ec;
          std::filesystem::create_directories(std::filesystem::u8path(g_dir + "\\p2p-results"), ec);
          // The two games start seconds apart and each takes several more to open its port, so
          // each keeps dialing (and keeps its router mapping alive) far longer than the bare default.
          // --p2p-games 0: the two keep playing over the one connection until one of them quits a
          // game or closes the window (a game started by hand plays one game unless told otherwise).
          g_lobby_launch_args = match.p2p_args + " --p2p-connect-seconds 45 --p2p-games 0";
          // The game binds the lobby's own UDP port, the one the other player's router already lets
          // through: the lobby closes its socket first and comes back when the game exits.
          launcher::lobby::stop_for_match(match);
          g_game_exe = game_exe(); launch_game_now();
        } else {
          launcher::lobby::game_running(false);
          if (!g_launcher_test)
            MessageBoxW(hwnd, L"The match could not start. Select an installed current build and disc, then request another match.",
                        L"Lobby", MB_ICONWARNING);
        }
      }
#else
      // Chosen per match: one whose request was P2P Direct arrives with the game's --p2p-* arguments
      // (launcher_lobby.cpp fills them only for such a request); any other is Slippi Direct.
      const bool have_match = launcher::lobby::take_match(match);
      if (have_match && !match.p2p_args.empty()) {
        // As in the build without Slippi: both launchers agreed on ports, characters, stage and seed
        // over the lobby's encrypted channel, and the arguments were checked field by field
        // (launcher_lobby_p2p.cpp, p2p_arguments). Here it also needs the Source Port to be the Game
        // Build that is picked and installed (the Static Recomp has no such session), vanilla, and
        // no mod profile: nothing of a mod launch is passed on.
        const bool source_ready = g_engine == ENGINE_SOURCE && source_available();
        if (source_ready && match.mode == "vanilla" && !g_playing && !g_building && !g_iso.empty() &&
            g_active_version.empty() && file_exists(game_exe())) {
          std::error_code ec;
          std::filesystem::create_directories(std::filesystem::u8path(g_dir + "\\p2p-results"), ec);
          g_mod_launch_iso.clear(); g_mod_launch_key.clear(); g_mod_launch_engine.clear(); g_mod_launch_kind.clear();
          // The two games start seconds apart, so each keeps dialing well past the bare default.
          // --p2p-games 1: one game per session in this phase.
          g_lobby_launch_args = match.p2p_args + " --p2p-connect-seconds 45 --p2p-games 1";
          g_lobby_game_p2p = true;
          // The game binds the lobby's own UDP port, the one the other player's router already lets
          // through: the lobby closes its socket first and comes back when the game exits.
          launcher::lobby::stop_for_match(match);
          g_game_exe = game_exe(); launch_game_now();
        } else {
          launcher::lobby::game_running(false);
          if (!g_launcher_test)
            MessageBoxW(hwnd, launcher::lang::trw(source_ready ? "lobby.p2p.setup_failed" : "lobby.p2p.error.launch_engine").c_str(),
                        L"Lobby", MB_ICONWARNING);
        }
      } else if (have_match) {
        // Codes arrive from another user. Never concatenate unchecked text into a command line.
        bool valid = !match.code.empty() && match.code.size() <= 18 && match.code.find('#') != std::string::npos;
        for (char c : match.code) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#')) valid = false;
        const auto selected_build = std::string(MELEE_PORT_VERSION) + (g_engine == ENGINE_SOURCE ? ":source" : ":recomp");
        // A mod match runs on Static Recomp whichever Game Build is picked: the version must match,
        // and the mod's disc must still be where the Mods scan found it.
        const bool mod_match = match.mode != "vanilla";
        const bool build_ok = mod_match ? launcher::lobby::build_version(match.build) == MELEE_PORT_VERSION
                                        : match.build == selected_build;
        const bool mod_ok = !mod_match || (!match.mod_path.empty() && file_exists(match.mod_path) &&
                                           file_exists(static_recomp_exe()));
        if (valid && build_ok && mod_ok && match.character >= 0 && match.character < 26 && !g_playing && !g_building && !g_iso.empty() && g_active_version.empty() && file_exists(game_exe())) {
          match.character=choose_match_character(match);
          g_lobby_launch_args = " --lobby-direct " + match.code + " --lobby-character " + std::to_string(match.character);
          if (mod_match) {
            // Each mod keeps its own memory card folder (User\GC\Mods\<key>), as the Mods page does.
            g_mod_launch_iso = match.mod_path; g_mod_launch_engine = "recomp"; g_mod_launch_kind = "mex";
            g_mod_launch_key = match.mode.rfind("custom:", 0) == 0 ? "custom-" + match.mode.substr(7) : match.mode;
          }
          g_game_exe = game_exe(); launch_game_now();
        } else {
          launcher::lobby::game_running(false);
          MessageBoxW(hwnd, mod_match && !mod_ok
                                ? L"The match could not start: the mod's disc is not where the Mods folder scan found it. Open the Mods page, then request another match."
                                : L"The match could not start. Select an installed current build and disc, then request another match.",
                      L"Lobby", MB_ICONWARNING);
        }
      }
#endif
      refresh_updater();
      if (g_tab == 0 && host::updater::rollback_state() == host::updater::RollbackState::Downloading) invalidate(LR(398, 348, 300, 32));
      return 0;
    }
    case WM_CTLCOLORLISTBOX: SetTextColor((HDC)wp,C_TEXT); SetBkColor((HDC)wp,C_FIELD); return (LRESULT)g_br_field;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wp;
      if ((HWND)lp == g_log) { SetTextColor(dc, C_LOG_TEXT); SetBkColor(dc, C_LOG_BG); return (LRESULT)g_br_log; }
      SetTextColor(dc, C_TEXT); SetBkColor(dc, C_FIELD); return (LRESULT)g_br_field;
    }
    case WM_DESTROY:
      launcher::lobby::shutdown();
      KillTimer(hwnd, ID_TIMER); settings_standby_stop(); host::updater::shutdown();
      if (g_dog) DeleteObject(g_dog);
      if (g_wordmark) DeleteObject(g_wordmark);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

// The updater expects these from the host runtime.
namespace host {
void log(const char* fmt, ...) { char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap); OutputDebugStringA(buf); OutputDebugStringA("\n"); }
void request_exit(int) { PostMessageW(g_main, WM_CLOSE, 0, 0); }
}  // namespace host

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  g_dir = narrow(exe); g_dir.resize(g_dir.find_last_of("\\/"));
  SetCurrentDirectoryW(widen(g_dir).c_str());
  g_dpi = (int)GetDpiForSystem();
  NONCLIENTMETRICSW ncm{sizeof ncm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  LOGFONTW lf = ncm.lfMessageFont;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfHeight = -S(12); g_font = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(11); g_font_small = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(14); lf.lfWeight = FW_SEMIBOLD; g_font_nav = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(10); lf.lfWeight = FW_BOLD; g_font_label = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(21); g_font_big = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(19); g_font_mark = CreateFontIndirectW(&lf);
  // Loaded at the size it is drawn at, so Windows picks the right image out of the .ico rather than
  // scaling a mismatched one.
  g_mark = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_MELEE_MARK), IMAGE_ICON, S(84), S(84), LR_DEFAULTCOLOR);
  g_font_mono = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
  g_br_field = CreateSolidBrush(C_FIELD);
  g_br_log = CreateSolidBrush(C_LOG_BG);
  WNDCLASSW wc{}; wc.lpfnWndProc = wnd_proc; wc.hInstance = inst; wc.lpszClassName = L"MeleeUnlockedLauncher";
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.hbrBackground = nullptr; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_LAUNCHER));
  RegisterClassW(&wc);
  RECT r{0, 0, S(WIN_W), S(WIN_H)}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  std::wstring title = widen(std::string("Melee Unlocked Launcher ") + MELEE_PORT_VERSION);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  launcher::lobby::init(hwnd, g_dir);
  launcher::lobby::set_prefs(g_lobby_prefs);
  launcher::lobby::on_prefs_changed([] { g_lobby_prefs = launcher::lobby::prefs(); save_ini(); });
  if (g_launcher_test) SetWindowPos(hwnd, nullptr, -10000, -10000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
  ShowWindow(hwnd, g_launcher_test ? SW_SHOWNOACTIVATE : show);
  std::thread(mod_manager::rescan).detach();
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
  return 0;
}
