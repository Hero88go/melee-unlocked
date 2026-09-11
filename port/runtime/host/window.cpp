// Win32 window, message pump, keyboard + XInput controller mapping to GameCube pads.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include "host.h"
#include "window.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "xinput9_1_0.lib")

namespace host {

namespace {
HWND g_hwnd = nullptr;
bool g_keys[256];
std::mutex g_keys_mutex;
bool g_closed = false;
int g_client_w = 1280, g_client_h = 960;
ResizeCallback g_on_resize;

LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CLOSE: g_closed = true; request_exit(0); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN: { std::lock_guard<std::mutex> lock(g_keys_mutex); if (w < 256) g_keys[w] = true; return 0; }
    case WM_KEYUP: { std::lock_guard<std::mutex> lock(g_keys_mutex); if (w < 256) g_keys[w] = false; return 0; }
    case WM_SIZE:
      if (w != SIZE_MINIMIZED) {
        g_client_w = LOWORD(l); g_client_h = HIWORD(l);
        if (g_on_resize && g_client_w > 0 && g_client_h > 0) g_on_resize(g_client_w, g_client_h);
      }
      return 0;
    case WM_KILLFOCUS: { std::lock_guard<std::mutex> lock(g_keys_mutex); std::memset(g_keys, 0, sizeof g_keys); return 0; }
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

void* window_create(int w, int h, const wchar_t* title, bool visible) {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = inst; wc.lpfnWndProc = wnd_proc; wc.lpszClassName = L"MeleePortWindow"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  RECT r{0, 0, w, h};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  if (!g_hwnd) die("cannot create native window");
  g_client_w = w; g_client_h = h;
  if (visible) ShowWindow(g_hwnd, SW_SHOW);
  return g_hwnd;
}

void window_set_resize_callback(ResizeCallback cb) { g_on_resize = std::move(cb); }

void window_destroy() { if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; } }

void window_pump() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void window_set_title(const wchar_t* title) { if (g_hwnd) SetWindowTextW(g_hwnd, title); }
bool window_closed() { return g_closed; }
void window_client_size(int* w, int* h) { *w = g_client_w; *h = g_client_h; }

// GameCube button bits (PADStatus.button)
enum : uint16_t {
  PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040,
  PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000,
};

namespace {
struct ScriptEntry { uint32_t frame; uint16_t buttons; int8_t sx, sy, cx, cy; int port; };
std::vector<ScriptEntry> g_script;
uint32_t g_script_ports = 1;
}  // namespace

bool input_load_script(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    ScriptEntry e{};
    char* p = line;
    if (*p == '#' || *p == '\n' || *p == '\r') continue;
    e.frame = (uint32_t)strtoul(p, &p, 10);
    while (*p) {
      while (*p == ' ' || *p == '\t') ++p;
      if (!*p || *p == '\n' || *p == '\r' || *p == '#') break;
      char tok[32]; int n = 0;
      while (*p && *p != ' ' && *p != '+' && *p != '\n' && *p != '\r' && n < 31) tok[n++] = *p++;
      tok[n] = 0;
      if (*p == '+') ++p;
      if (!strcmp(tok, "A")) e.buttons |= PAD_A; else if (!strcmp(tok, "B")) e.buttons |= PAD_B;
      else if (!strcmp(tok, "X")) e.buttons |= PAD_X; else if (!strcmp(tok, "Y")) e.buttons |= PAD_Y;
      else if (!strcmp(tok, "Z")) e.buttons |= PAD_Z; else if (!strcmp(tok, "L")) e.buttons |= PAD_L;
      else if (!strcmp(tok, "R")) e.buttons |= PAD_R; else if (!strcmp(tok, "START")) e.buttons |= PAD_START;
      else if (!strcmp(tok, "DU")) e.buttons |= PAD_UP; else if (!strcmp(tok, "DD")) e.buttons |= PAD_DOWN;
      else if (!strcmp(tok, "DL")) e.buttons |= PAD_LEFT; else if (!strcmp(tok, "DR")) e.buttons |= PAD_RIGHT;
      else if (!strncmp(tok, "sx=", 3)) e.sx = (int8_t)atoi(tok + 3); else if (!strncmp(tok, "sy=", 3)) e.sy = (int8_t)atoi(tok + 3);
      else if (!strncmp(tok, "cx=", 3)) e.cx = (int8_t)atoi(tok + 3); else if (!strncmp(tok, "cy=", 3)) e.cy = (int8_t)atoi(tok + 3);
      else if (!strncmp(tok, "p=", 2)) { e.port = atoi(tok + 2) - 1; if (e.port < 0 || e.port > 3) e.port = 0; g_script_ports |= 1u << e.port; }
    }
    g_script.push_back(e);
  }
  fclose(f);
  return !g_script.empty();
}

void input_poll(PadState out[4]) {
  for (int i = 0; i < 4; ++i) { std::memset(&out[i], 0, sizeof out[i]); out[i].err = -1; }
  PadState& p = out[0];
  p.err = 0;
  if (!g_script.empty()) {
    // Scripts drive port 1 by default; entries with p=N drive port N (a port with any entry counts as plugged in).
    uint32_t frame = retrace_count();
    for (int port = 0; port < 4; ++port) {
      if (port && !(g_script_ports & (1u << port))) continue;
      out[port].err = 0;
      const ScriptEntry* cur = nullptr;
      for (const ScriptEntry& e : g_script) if (e.port == port && e.frame <= frame) cur = &e;
      if (cur) { PadState& q = out[port]; q.button = cur->buttons; q.stick_x = cur->sx; q.stick_y = cur->sy; q.sub_x = cur->cx; q.sub_y = cur->cy; }
    }
    return;
  }
  // Keyboard (player 1): arrows = stick, IJKL = c-stick, Z=A X=B C=X V=Y, Enter=Start, Q=L W=R E=Z, D-pad = TFGH
  std::lock_guard<std::mutex> lock(g_keys_mutex);
  auto key = [](int vk) { return g_keys[vk & 0xFF]; };
  int sx = 0, sy = 0, cx = 0, cy = 0;
  if (key(VK_LEFT)) sx -= 127; if (key(VK_RIGHT)) sx += 127; if (key(VK_UP)) sy += 127; if (key(VK_DOWN)) sy -= 127;
  if (key('J')) cx -= 127; if (key('L')) cx += 127; if (key('I')) cy += 127; if (key('K')) cy -= 127;
  if (key('Z')) p.button |= PAD_A; if (key('X')) p.button |= PAD_B; if (key('C')) p.button |= PAD_X; if (key('V')) p.button |= PAD_Y;
  if (key(VK_RETURN)) p.button |= PAD_START;
  if (key('Q')) { p.button |= PAD_L; p.trig_l = 255; } if (key('W')) { p.button |= PAD_R; p.trig_r = 255; } if (key('E')) p.button |= PAD_Z;
  if (key('T')) p.button |= PAD_UP; if (key('G')) p.button |= PAD_DOWN; if (key('F')) p.button |= PAD_LEFT; if (key('H')) p.button |= PAD_RIGHT;
  // XInput pad 0 overrides/adds
  XINPUT_STATE xs{};
  if (XInputGetState(0, &xs) == ERROR_SUCCESS) {
    auto& g = xs.Gamepad;
    auto axis = [](SHORT v) { int a = v / 258; return a > 127 ? 127 : a < -127 ? -127 : a; };
    if (abs(g.sThumbLX) > 7849 || abs(g.sThumbLY) > 7849) { sx = axis(g.sThumbLX); sy = axis(g.sThumbLY); }
    if (abs(g.sThumbRX) > 8689 || abs(g.sThumbRY) > 8689) { cx = axis(g.sThumbRX); cy = axis(g.sThumbRY); }
    if (g.wButtons & XINPUT_GAMEPAD_A) p.button |= PAD_A;
    if (g.wButtons & XINPUT_GAMEPAD_B) p.button |= PAD_B;
    if (g.wButtons & XINPUT_GAMEPAD_X) p.button |= PAD_X;
    if (g.wButtons & XINPUT_GAMEPAD_Y) p.button |= PAD_Y;
    if (g.wButtons & XINPUT_GAMEPAD_START) p.button |= PAD_START;
    if (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) p.button |= PAD_Z;
    if (g.wButtons & XINPUT_GAMEPAD_DPAD_UP) p.button |= PAD_UP;
    if (g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) p.button |= PAD_DOWN;
    if (g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) p.button |= PAD_LEFT;
    if (g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) p.button |= PAD_RIGHT;
    if (g.bLeftTrigger > 30) { p.trig_l = g.bLeftTrigger; if (g.bLeftTrigger > 200) p.button |= PAD_L; }
    if (g.bRightTrigger > 30) { p.trig_r = g.bRightTrigger; if (g.bRightTrigger > 200) p.button |= PAD_R; }
  }
  p.stick_x = (int8_t)sx; p.stick_y = (int8_t)sy; p.sub_x = (int8_t)cx; p.sub_y = (int8_t)cy;
}

}  // namespace host
