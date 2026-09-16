// Melee Unlocked Launcher: an optional Win32 client with a Play page (ISO, Slippi account, version
// and self-update) and a Build tab (drop the ISO: verify it, run the source build when this is a
// checkout, precompile the pipeline library, remember the path). Starts melee_port.exe with the
// release settings. Plain Win32 so it has no dependencies beyond the OS.
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
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include "updater.h"

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
enum { ID_ISO_EDIT = 100, ID_BROWSE, ID_PLAY, ID_SLIPPI_GET, ID_UPDATE, ID_BUILD, ID_LOG, ID_TIMER = 1 };
const UINT WM_APP_LOG = WM_APP + 1;      // lParam: heap std::string* to append to the log
const UINT WM_APP_BUILD_DONE = WM_APP + 2;
const UINT WM_APP_GAME_DONE = WM_APP + 3;

// Client area in layout units; S() turns these into pixels for the current DPI.
const int WIN_W = 720, WIN_H = 400;
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
const COLORREF C_ACC_HI = RGB(0xEF, 0xA0, 0x54), C_ACC_LO = RGB(0xD9, 0x7B, 0x2C);
const COLORREF C_BTN = RGB(0x21, 0x2B, 0x42), C_BTN_BORDER = RGB(0x35, 0x41, 0x5F), C_BTN_DOWN = RGB(0x18, 0x21, 0x34);
const COLORREF C_NAV_ON = RGB(0x1F, 0x2A, 0x40), C_NAV_HOT = RGB(0x18, 0x21, 0x34);
const COLORREF C_LOG_BG = RGB(0x0A, 0x0F, 0x1A), C_LOG_TEXT = RGB(0x9F, 0xB4, 0xCE);
const COLORREF C_PLAY_TEXT = RGB(0x24, 0x16, 0x05);
const COLORREF C_OK = RGB(0x5A, 0xC8, 0x8A), C_WARN = RGB(0xE5, 0xA8, 0x4A), C_BAD = RGB(0xE0, 0x6B, 0x5B);
const COLORREF NO_FILL = CLR_INVALID;

HWND g_main;
HWND g_play[5], g_build[2];
HWND g_iso_edit, g_play_btn, g_slippi_btn, g_update_btn, g_log, g_build_btn;
HFONT g_font, g_font_big, g_font_mono, g_font_mark, g_font_nav, g_font_label, g_font_small;
HICON g_mark = nullptr;          // IDI_MELEE_MARK, the wordmark drawn at the top of the rail
HBRUSH g_br_field, g_br_log;
HBITMAP g_dog = nullptr;         // Bailey cut out of her white plate, 32bpp premultiplied
int g_dog_w = 0, g_dog_h = 0;
HBITMAP g_wordmark = nullptr;    // the MU mark cut off its navy plate, at full resolution
int g_wordmark_w = 0, g_wordmark_h = 0;
std::string g_dir, g_iso, g_game_exe;
std::string g_slippi_line, g_version_line;
COLORREF g_version_dot = C_FAINT;
std::atomic<bool> g_building{false}, g_playing{false};
bool g_slippi_missing = false;
int g_tab = 0, g_nav_hot = -1;
bool g_tracking = false;
int g_dpi = 96;
int S(int v) { return MulDiv(v, g_dpi, 96); }

std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n ? n - 1 : 0, 0); if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
std::string narrow(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n ? n - 1 : 0, 0); if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr); return s; }
bool file_exists(const std::string& p) { DWORD a = GetFileAttributesW(widen(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
void set_text(HWND h, const std::string& s) { SetWindowTextW(h, widen(s).c_str()); }

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
void load_ini() {
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
  { std::ofstream f(ini_path()); f << "iso=" << g_iso << "\n"; }
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

// Slippi account: this folder's User\Slippi\user.json, else the Slippi Launcher's login.
std::string slippi_account_line() {
  std::string paths[2] = {g_dir + "\\User\\Slippi\\user.json", ""};
  char* appdata = nullptr; size_t n = 0;
  if (_dupenv_s(&appdata, &n, "APPDATA") == 0 && appdata) { paths[1] = std::string(appdata) + "\\Slippi Launcher\\netplay\\User\\Slippi\\user.json"; free(appdata); }
  for (auto& p : paths) {
    if (p.empty() || !file_exists(p)) continue;
    std::ifstream f(p); auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_object() && j.count("connectCode")) return "Slippi account: " + j.value("displayName", std::string("?")) + " (" + j["connectCode"].get<std::string>() + ")";
  }
  return "Slippi online needs an account: install the Slippi Launcher and log in once.";
}

// Runs a command line with stdout/stderr piped into the log. Returns the exit code.
DWORD run_logged(std::string cmd, const std::string& cwd) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return 1;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi)) { CloseHandle(rd); CloseHandle(wr); log_line("cannot start: %s", cmd.c_str()); return 1; }
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
std::string game_exe() {
  if (file_exists(g_dir + "\\melee_port.exe")) return g_dir + "\\melee_port.exe";
  std::string root = repo_root();
  if (!root.empty()) return root + "\\build-review\\port\\Release\\melee_port.exe";
  return g_dir + "\\melee_port.exe";
}
// Working directory for the game: the release folder (Sys next to the launcher) or the repo root
// of a source checkout (its defaults, port/slippi_sys and shadercache, are relative paths).
std::string work_dir() {
  if (file_exists(g_dir + "\\Sys\\codehandler.bin")) return g_dir;
  std::string root = repo_root();
  return root.empty() ? g_dir : root;
}
std::string settings_ini_path() { return work_dir() + "\port-settings.ini"; }
std::string game_args() {
  std::string base = g_dir;
  std::string a = " --iso \"" + g_iso + "\" --threaded-renderer";
  if (file_exists(g_dir + "\\Sys\\codehandler.bin"))
    a += " --sys-dir \"" + base + "\\Sys\" --user-dir \"" + base + "\\User\\Slippi\" --replay-dir \"" + base + "\\Replays\" --card-dir \"" + base + "\\User\\GC\\CardA\"";
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
  DrawTextW(dc, s.c_str(), -1, &r, fmt);
  if (tracking) SetTextCharacterExtra(dc, 0);
  SelectObject(dc, old);
}

void dot(HDC dc, int x, int y, COLORREF c) {
  RECT r{S(x), S(y), S(x + 8), S(y + 8)};
  round_rect(dc, r, 4, c, c, NO_FILL);
}

RECT nav_rect(int i) { return LR(12, NAV_Y + i * NAV_GAP, RAIL_W - 24, NAV_H); }
RECT slippi_text_rect() { return LR(CX + 15, 212, 319, 34); }
RECT version_text_rect() { return LR(CX + 15, 244, 319, 34); }
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
void load_rail_art() {
  g_dog = load_art(IDB_BAILEY, S(RAIL_W - 46), S(WIN_H - (NAV_Y + 2 * NAV_GAP + NAV_H + 10) - 12), g_dog_w, g_dog_h);
  g_wordmark = load_art(IDB_MELEE_MARK, S(RAIL_W - 44), S(NAV_Y - 22), g_wordmark_w, g_wordmark_h);
}

void paint_rail(HDC dc) {
  RECT r = LR(0, 0, RAIL_W, WIN_H);
  // The rail is always the dark gradient. Stretching the photo over it filled the rail with the
  // white plate the dog was composited on and cropped her at the same time, because the plate is
  // 620x416 and the rail is tall and narrow. The dog is cut out of that plate at load (see
  // load_rail_art) and drawn over the gradient at her own aspect, whole.
  vgrad(dc, r, C_RAIL_TOP, C_RAIL_BOT);
  if (g_dog && g_dog_w > 0 && g_dog_h > 0) {
    const int dw = g_dog_w, dh = g_dog_h;
    // Centred in what is left of the rail under the nav, so the gap above her matches the gap below.
    // Bottom-anchored left a dead band between the page list and her head.
    const int top = S(NAV_Y + 2 * NAV_GAP + NAV_H + 10);
    const int dx = (S(RAIL_W) - dw) / 2, dy = top + (S(WIN_H) - top - dh) / 2;
    HDC md = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(md, g_dog);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    AlphaBlend(dc, dx, dy, dw, dh, md, 0, 0, g_dog_w, g_dog_h, bf);
    SelectObject(md, old);
    DeleteDC(md);
  }
  RECT line{r.right - 1, 0, r.right, S(WIN_H)};
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

  const wchar_t* names[3] = {L"Play", L"Settings", L"Build"};
  for (int i = 0; i < 3; ++i) {
    RECT nr = nav_rect(i);
    const int page = i == 0 ? 0 : (i == 2 ? 1 : -1);
    if (page >= 0 && g_tab == page) {
      round_rect(dc, nr, 8, C_NAV_ON, C_NAV_ON, NO_FILL);
      RECT bar = LR(12, NAV_Y + i * NAV_GAP + 8, 4, 18);
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

  dot(dc, CX, 218, g_slippi_missing ? C_WARN : C_OK);
  draw_text(dc, widen(g_slippi_line), slippi_text_rect(), g_font, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);

  dot(dc, CX, 250, g_version_dot);
  draw_text(dc, widen(g_version_line), version_text_rect(), g_font, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);

  RECT sep = LR(CX, 282, CW, 1);
  fill(dc, sep, C_SEP);
  draw_text(dc, HINT_TEXT, LR(CX, 294, CW, 44), g_font_small, C_FAINT, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
}




void paint_build(HDC dc) {
  round_rect(dc, LR(CX, 36, CW, 84), 10, RGB(0x14, 0x1C, 0x2C), RGB(0x14, 0x1C, 0x2C),
             g_building ? C_ACC_LO : RGB(0x33, 0x40, 0x60));
  draw_text(dc, L"Drop your Melee NTSC 1.02 ISO here", LR(CX, 54, CW, 30), g_font_big, C_TEXT,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  std::wstring sub = g_iso.empty() ? L"or press Browse on the Play page" : widen(iso_name());
  draw_text(dc, sub, drop_sub_rect(), g_font_small, C_FAINT, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

  draw_text(dc, L"Verifies the disc and precompiles shaders for your GPU. The ISO is never copied.",
            LR(CX, 132, CW, 44), g_font_small, C_DIM, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
  round_rect(dc, LR(CX, 226, CW, 106), 8, C_LOG_BG, C_LOG_BG, RGB(0x24, 0x2E, 0x46));
}

void paint(HWND hwnd, HDC target, RECT dirty) {
  RECT cr; GetClientRect(hwnd, &cr);
  HDC dc = CreateCompatibleDC(target);
  HBITMAP bmp = CreateCompatibleBitmap(target, cr.right, cr.bottom);
  HGDIOBJ oldb = SelectObject(dc, bmp);

  RECT content{S(RAIL_W), 0, cr.right, cr.bottom};
  vgrad(dc, content, C_CONTENT_TOP, C_CONTENT_BOT);
  paint_rail(dc);
  if (g_tab == 0) paint_play(dc); else paint_build(dc);

  BitBlt(target, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top, dc, dirty.left, dirty.top, SRCCOPY);
  SelectObject(dc, oldb);
  DeleteObject(bmp);
  DeleteDC(dc);
}

void draw_button(DRAWITEMSTRUCT* di) {
  RECT wr; GetWindowRect(di->hwndItem, &wr);
  MapWindowPoints(nullptr, g_main, (POINT*)&wr, 2);
  RECT r = di->rcItem;
  bool disabled = (di->itemState & ODS_DISABLED) != 0;
  bool down = (di->itemState & ODS_SELECTED) != 0;
  bool primary = di->hwndItem == g_play_btn;

  // Reproduce the parent's gradient behind the button so the rounded corners have the right colour.
  vgrad(di->hDC, r, content_bg_at(wr.top), content_bg_at(wr.bottom));

  COLORREF top, bot, border = NO_FILL, text;
  if (primary) {
    top = C_ACC_HI; bot = C_ACC_LO; text = C_PLAY_TEXT;
    if (down) { top = C_ACC_LO; bot = C_ACC_LO; }
    if (disabled) { top = RGB(0x3A, 0x35, 0x30); bot = RGB(0x33, 0x2E, 0x2A); text = RGB(0x7C, 0x74, 0x6B); }
  } else {
    top = bot = down ? C_BTN_DOWN : C_BTN; border = C_BTN_BORDER; text = C_TEXT;
    if (disabled) { top = bot = RGB(0x1A, 0x21, 0x32); border = RGB(0x28, 0x31, 0x47); text = RGB(0x5C, 0x68, 0x7E); }
  }
  round_rect(di->hDC, r, primary ? 10 : 7, top, bot, border);
  if (di->itemState & ODS_FOCUS) {
    RECT f{r.left + S(3), r.top + S(3), r.right - S(3), r.bottom - S(3)};
    round_rect(di->hDC, f, primary ? 8 : 5, NO_FILL, NO_FILL, primary ? C_PLAY_TEXT : C_ACC_LO);
  }
  wchar_t cap[128]{}; GetWindowTextW(di->hwndItem, cap, 128);
  draw_text(di->hDC, cap, r, primary ? g_font_big : g_font, text,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER, primary ? S(2) : 0);
}



// ---------------------------------------------------------------------------- behaviour

void refresh_updater();
void open_settings();

void select_tab(int idx) {
  g_tab = idx;
  for (HWND h : g_play) if (h) ShowWindow(h, idx == 0 ? SW_SHOW : SW_HIDE);
  for (HWND h : g_build) if (h) ShowWindow(h, idx == 1 ? SW_SHOW : SW_HIDE);
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
    if (root.empty()) { log_line("melee_port.exe is missing next to this launcher and this is not a source checkout."); PostMessageW(g_main, WM_APP_BUILD_DONE, 1, 0); return; }
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

// Opens the game straight into its own PC settings panel. Same binary, same panel, same file: what
// is changed here is what the next launch uses, because the game reads port-settings.ini from this
// working directory before it opens a window.
void open_settings() {
  if (g_playing || g_iso.empty()) return;
  g_game_exe = game_exe();
  if (!file_exists(g_game_exe)) { select_tab(1); refresh_updater(); start_build(); return; }
  std::string cwd = work_dir();
  std::string cmd = "\"" + g_game_exe + "\"" + game_args() + " --pc-settings-open";
  STARTUPINFOA si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
    MessageBoxW(g_main, L"Could not start melee_port.exe", L"Melee Unlocked Launcher", MB_ICONERROR);
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
}

void start_game() {
  if (g_playing || g_iso.empty()) return;
  g_game_exe = game_exe();
  if (!file_exists(g_game_exe)) { select_tab(1); refresh_updater(); start_build(); return; }
  std::string cwd = work_dir();
  std::string cmd = "\"" + g_game_exe + "\"" + game_args();
  STARTUPINFOA si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) { MessageBoxW(g_main, L"Could not start melee_port.exe", L"Melee Unlocked Launcher", MB_ICONERROR); return; }
  CloseHandle(pi.hThread);
  g_playing = true;
  EnableWindow(g_play_btn, FALSE);
  set_text(g_play_btn, "Running");
  ShowWindow(g_main, SW_MINIMIZE);
  std::thread([h = pi.hProcess] { WaitForSingleObject(h, INFINITE); CloseHandle(h); PostMessageW(g_main, WM_APP_GAME_DONE, 0, 0); }).detach();
}

void set_iso(const std::string& path) {
  g_iso = path; set_text(g_iso_edit, g_iso);
  EnableWindow(g_play_btn, !g_iso.empty() && !g_playing && !g_building);
  EnableWindow(g_build_btn, !g_iso.empty() && !g_building);
  invalidate(drop_sub_rect());
}

void browse() {
  wchar_t file[MAX_PATH]{};
  OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = g_main; ofn.lpstrFilter = L"GameCube disc image (*.iso;*.gcm)\0*.iso;*.gcm\0All files\0*.*\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&ofn)) { set_iso(narrow(file)); save_ini(); }
}

bool g_update_prompted = false;
void refresh_updater() {
  using host::updater::State;
  auto st = host::updater::state();
  // One yes/no prompt per launch when a newer release exists. Nothing installs without a Yes.
  if (st == State::UpdateAvailable && !g_update_prompted) {
    g_update_prompted = true;
    std::string text = "Melee Unlocked " + host::updater::latest_version() + " is available (you have " MELEE_PORT_VERSION ").\n\nUpdate now? The game folder is updated in place; settings, saves and replays are kept.\n\nNo keeps this version; the Update button stays on the Play page.";
    if (MessageBoxW(g_main, widen(text).c_str(), L"Melee Unlocked Launcher", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) host::updater::download_and_install();
    st = host::updater::state();
  }
  std::string line = "Version " MELEE_PORT_VERSION ". " + host::updater::message();
  if (st == State::Checking) line = "Version " MELEE_PORT_VERSION ". Checking for updates...";
  COLORREF d = st == State::Checking ? C_FAINT : st == State::UpdateAvailable ? C_WARN : st == State::Failed ? C_BAD : C_OK;
  // Only repaint when something actually changed: this runs on a 500 ms timer, and repainting the
  // label's rect from the background every tick is what stops old text showing through the new.
  if (line != g_version_line || d != g_version_dot) {
    g_version_line = line; g_version_dot = d;
    if (g_tab == 0) { invalidate(version_text_rect()); invalidate(LR(CX, 264, 8, 8)); }
  }
  ShowWindow(g_update_btn, (st == State::UpdateAvailable || st == State::Failed) && g_tab == 0 ? SW_SHOW : SW_HIDE);
  set_text(g_update_btn, st == State::Failed ? "Retry" : "Update and restart");
}

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font = nullptr, DWORD ex = 0) {
  HWND hw = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_main, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(hw, WM_SETFONT, (WPARAM)(font ? font : g_font), TRUE);
  return hw;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_main = hwnd;
      load_rail_art();
      // Play page
      int i = 0;
      g_play[i++] = g_iso_edit = make(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, CX + 10, 66, 360, 18, ID_ISO_EDIT);
      g_play[i++] = make(L"BUTTON", L"Browse...", BS_OWNERDRAW, 602, 58, 96, 34, ID_BROWSE);
      g_play[i++] = g_play_btn = make(L"BUTTON", L"PLAY", BS_OWNERDRAW, CX, 118, CW, 76, ID_PLAY, g_font_big);
      g_play[i++] = g_slippi_btn = make(L"BUTTON", L"Get Slippi Launcher", BS_OWNERDRAW, 554, 214, 144, 30, ID_SLIPPI_GET);
      g_play[i++] = g_update_btn = make(L"BUTTON", L"Update and restart", BS_OWNERDRAW, 554, 246, 144, 30, ID_UPDATE);
      // Build page
      g_build[0] = g_build_btn = make(L"BUTTON", L"Build", BS_OWNERDRAW, CX, 182, 120, 32, ID_BUILD);
      g_build[1] = g_log = make(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, CX + 2, 228, CW - 4, 102, ID_LOG, g_font_mono);
      SetWindowTheme(g_log, L"DarkMode_Explorer", nullptr);   // a dark scrollbar where the OS has one
      select_tab(0);
      load_ini();
      set_iso(g_iso);
      if (!g_iso.empty()) save_ini();   // remember wherever it came from
      g_slippi_line = slippi_account_line();
      g_slippi_missing = g_slippi_line.rfind("Slippi account:", 0) != 0;
      ShowWindow(g_slippi_btn, g_slippi_missing ? SW_SHOW : SW_HIDE);
      host::updater::check(MELEE_PORT_VERSION);
      refresh_updater();
      SetTimer(hwnd, ID_TIMER, 500, nullptr);
      DragAcceptFiles(hwnd, TRUE);
      return 0;
    }
    case WM_ERASEBKGND: return 1;        // WM_PAINT paints every pixel from a memory DC
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
      paint(hwnd, dc, ps.rcPaint);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DRAWITEM: draw_button((DRAWITEMSTRUCT*)lp); return TRUE;
    case WM_MOUSEMOVE: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      int hot = -1;
      for (int i = 0; i < 3; ++i) { RECT r = nav_rect(i); if (PtInRect(&r, p)) hot = i; }
      if (hot != g_nav_hot) {
        int was = g_nav_hot; g_nav_hot = hot;
        if (was >= 0) invalidate(nav_rect(was));
        if (hot >= 0) invalidate(nav_rect(hot));
      }
      if (!g_tracking) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_tracking = true; }
      return 0;
    }
    case WM_MOUSELEAVE:
      g_tracking = false;
      if (g_nav_hot >= 0) { int was = g_nav_hot; g_nav_hot = -1; invalidate(nav_rect(was)); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT && g_nav_hot >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
      break;
    case WM_LBUTTONDOWN: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      for (int i = 0; i < 3; ++i) {
        RECT r = nav_rect(i);
        if (!PtInRect(&r, p)) continue;
        // Settings is not a page here. The launcher used to draw its own copy of the options, which
        // is a second settings UI to keep in step with the real one; this opens the game's own F1
        // panel instead, so it is the same UI by construction and cannot drift from it.
        if (i == 1) { open_settings(); return 0; }
        const int tab = i == 0 ? 0 : 1;
        if (g_tab != tab) { select_tab(tab); refresh_updater(); }
        return 0;
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_BROWSE: browse(); break;
        case ID_PLAY: start_game(); break;
        case ID_BUILD: start_build(); break;
        case ID_UPDATE:
          if (host::updater::state() == host::updater::State::Failed) host::updater::check(MELEE_PORT_VERSION);
          else host::updater::download_and_install();
          refresh_updater();
          break;
        case ID_SLIPPI_GET: ShellExecuteW(hwnd, L"open", L"https://slippi.gg/downloads", nullptr, nullptr, SW_SHOWNORMAL); break;
      }
      return 0;
    case WM_DROPFILES: {
      wchar_t file[MAX_PATH]{};
      if (DragQueryFileW((HDROP)wp, 0, file, MAX_PATH)) { set_iso(narrow(file)); select_tab(1); refresh_updater(); start_build(); }
      DragFinish((HDROP)wp);
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
    case WM_APP_BUILD_DONE:
      g_building = false;
      set_iso(g_iso);
      if (wp == 0) { select_tab(0); refresh_updater(); } else InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_APP_GAME_DONE:
      g_playing = false;
      set_text(g_play_btn, "PLAY");
      set_iso(g_iso);
      ShowWindow(hwnd, SW_RESTORE);
      return 0;
    case WM_TIMER: refresh_updater(); return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wp;
      if ((HWND)lp == g_log) { SetTextColor(dc, C_LOG_TEXT); SetBkColor(dc, C_LOG_BG); return (LRESULT)g_br_log; }
      SetTextColor(dc, C_TEXT); SetBkColor(dc, C_FIELD); return (LRESULT)g_br_field;
    }
    case WM_DESTROY:
      KillTimer(hwnd, ID_TIMER); host::updater::shutdown();
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
  ShowWindow(hwnd, show);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
  return 0;
}
