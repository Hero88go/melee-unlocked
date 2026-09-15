// Win32 window, message pump, keyboard + XInput controller mapping to GameCube pads.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <hidsdi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include <atomic>
#include "host.h"
#include "window.h"
#include "input_bindings.h"

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
std::atomic<bool> g_fullscreen_toggle{false};
MessageCallback g_on_message;
std::atomic<bool> g_ui_capture{false};
std::mutex g_ui_pad_mutex;
PadState g_ui_pad{};
bool g_ui_gamecube = false;
void ds4_input(HRAWINPUT raw);
void ds4_init_defaults();

LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (g_on_message && g_on_message(h, m, w, l)) return 1;
  switch (m) {
    case WM_CLOSE: g_closed = true; request_exit(0); return 0;
    case WM_INPUT: ds4_input((HRAWINPUT)l); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SYSKEYDOWN:
      if (w == VK_RETURN && (l & (1 << 29)) && !(l & (1 << 30))) { g_fullscreen_toggle.store(true); return 0; }   // Alt+Enter, first press only
      break;
    case WM_SYSCHAR: if (w == VK_RETURN) return 0; break;   // no beep for Alt+Enter
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
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);   // black, not white, before the first present
  RegisterClassW(&wc);
  ds4_init_defaults();
  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;
  rid.usUsage = 0x05;
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = g_hwnd;
  RECT r{0, 0, w, h};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  if (!g_hwnd) die("cannot create native window");
  rid.hwndTarget = g_hwnd;
  RegisterRawInputDevices(&rid, 1, sizeof rid);
  g_closed = false;
  g_client_w = w; g_client_h = h;
  if (visible) ShowWindow(g_hwnd, SW_SHOW);
  return g_hwnd;
}

void window_set_fullscreen(bool enabled) {
  static WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
  static bool fullscreen = false;
  if (!g_hwnd || enabled == fullscreen) return;
  if (enabled) {
    GetWindowPlacement(g_hwnd, &saved);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &info)) return;
    // Keep WS_VISIBLE: replacing the style with a bare WS_POPUP hid the window, so the desktop
    // compositor stopped showing our frames (black screen with a stale frame of the old window).
    SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(g_hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right-info.rcMonitor.left, info.rcMonitor.bottom-info.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  } else {
    SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPlacement(g_hwnd, &saved);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  }
  fullscreen = enabled;
}

double window_refresh_rate() {
  MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return 60.0;
  // DisplayConfig retains rational rates (e.g. 60000/1001) that DEVMODE rounds.
  for (int attempt = 0; attempt < 3; ++attempt) {
    UINT32 paths_count = 0, modes_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths_count, &modes_count) != ERROR_SUCCESS) break;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(paths_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modes_count);
    LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths_count, paths.data(), &modes_count, modes.data(), nullptr);
    if (status == ERROR_INSUFFICIENT_BUFFER) continue;
    if (status != ERROR_SUCCESS) break;
    for (UINT32 i = 0; i < paths_count; ++i) {
      const auto& path = paths[i];
      DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
      source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source.header.size = sizeof(source); source.header.adapterId = path.sourceInfo.adapterId;
      source.header.id = path.sourceInfo.id;
      if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, monitor.szDevice)) continue;
      const auto rate = path.targetInfo.refreshRate;
      if (rate.Numerator && rate.Denominator) return double(rate.Numerator) / rate.Denominator;
    }
    break;
  }
  DEVMODEW mode{}; mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) return mode.dmDisplayFrequency;
  return 60.0;
}

void window_set_message_callback(MessageCallback cb) { g_on_message = std::move(cb); }
void window_input_capture(bool capture) { g_ui_capture.store(capture); }
bool window_ui_gamecube_pad(PadState& pad) { std::lock_guard<std::mutex> lock(g_ui_pad_mutex); pad = g_ui_pad; return g_ui_gamecube; }
void window_set_resize_callback(ResizeCallback cb) { g_on_resize = std::move(cb); }
bool window_take_fullscreen_toggle() { return g_fullscreen_toggle.exchange(false); }

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

KeyBindings g_key_bindings = default_key_bindings();
std::array<PadBindings, 4> g_pad_bindings = default_pad_bindings();
std::array<GCBindings, 4> g_gc_bindings = default_gc_bindings();
std::array<PadBindings, 4> g_ds4_bindings = {};
std::array<PortSource, 4> g_port_sources = default_port_sources();

namespace {
std::atomic<bool> g_capturing{false};
bool g_capture_key_baseline[256]{};
unsigned short g_capture_pad_baseline[4]{};
PadState g_capture_gc_baseline[4]{};
uint16_t g_capture_ds4_baseline[4]{};
HANDLE g_ds4_devices[4]{};
uint16_t g_ds4_buttons[4]{};
PadState g_ds4_pads[4]{};
std::mutex g_ds4_mutex;

constexpr uint16_t ds4_action_mask(BindAction action) {
  switch (action) {
    case BindAction::A: return DS4_CROSS;
    case BindAction::B: return DS4_CIRCLE;
    case BindAction::X: return DS4_SQUARE;
    case BindAction::Y: return DS4_TRIANGLE;
    case BindAction::Start: return DS4_OPTIONS;
    case BindAction::L: return DS4_L1;
    case BindAction::R: return DS4_R1;
    case BindAction::Z: return DS4_R2;
    case BindAction::DUp: return DS4_DPAD_UP;
    case BindAction::DDown: return DS4_DPAD_DOWN;
    case BindAction::DLeft: return DS4_DPAD_LEFT;
    case BindAction::DRight: return DS4_DPAD_RIGHT;
    default: return 0;
  }
}

void ds4_init_defaults() {
  for (auto& bindings : g_ds4_bindings)
    for (int i = 0; i < (int)BindAction::Count; ++i) bindings.mask[i] = ds4_action_mask((BindAction)i);
}

int ds4_slot(HANDLE device) {
  for (int i = 0; i < 4; ++i) if (g_ds4_devices[i] == device) return i;
  for (int i = 0; i < 4; ++i) if (!g_ds4_devices[i]) { g_ds4_devices[i] = device; return i; }
  return -1;
}

bool ds4_device(HANDLE device) {
  RID_DEVICE_INFO info{}; info.cbSize = sizeof info;
  UINT size = sizeof info;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size) == (UINT)-1 || info.dwType != RIM_TYPEHID) return false;
  return info.hid.dwVendorId == 0x054C && (info.hid.dwProductId == 0x05C4 || info.hid.dwProductId == 0x09CC);
}

void ds4_input(HRAWINPUT raw) {
  UINT size = 0;
  if (GetRawInputData(raw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 || !size) return;
  std::vector<uint8_t> bytes(size);
  if (GetRawInputData(raw, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
  RAWINPUT* input = reinterpret_cast<RAWINPUT*>(bytes.data());
  if (input->header.dwType != RIM_TYPEHID || !ds4_device(input->header.hDevice)) return;
  int slot = ds4_slot(input->header.hDevice);
  if (slot < 0 || !input->data.hid.dwSizeHid || !input->data.hid.dwCount) return;
  const uint8_t* report = input->data.hid.bRawData;
  // USB reports use ID 0x01 with axes at byte 1; Bluetooth reports use ID
  // 0x11, a counter byte, then axes at byte 2.
  size_t offset = report[0] == 0x11 ? 2 : report[0] == 0x01 ? 1 : 0;
  if (input->data.hid.dwSizeHid < offset + 9) return;
  const uint8_t dpad = report[offset + 4] & 0x0F;
  uint16_t buttons = 0;
  if (dpad == 0 || dpad == 1 || dpad == 7) buttons |= DS4_DPAD_UP;
  if (dpad == 3 || dpad == 4 || dpad == 5) buttons |= DS4_DPAD_DOWN;
  if (dpad == 5 || dpad == 6 || dpad == 7) buttons |= DS4_DPAD_LEFT;
  if (dpad == 1 || dpad == 2 || dpad == 3) buttons |= DS4_DPAD_RIGHT;
  const uint8_t face = report[offset + 5], system = report[offset + 6];
  if (face & 0x10) buttons |= DS4_SQUARE; if (face & 0x20) buttons |= DS4_CROSS;
  if (face & 0x40) buttons |= DS4_CIRCLE; if (face & 0x80) buttons |= DS4_TRIANGLE;
  if (face & 0x01) buttons |= DS4_L1; if (face & 0x02) buttons |= DS4_R1;
  if (system & 0x01) buttons |= DS4_SHARE; if (system & 0x02) buttons |= DS4_OPTIONS;
  if (system & 0x04) buttons |= DS4_L3; if (system & 0x08) buttons |= DS4_R3;
  PadState pad{}; pad.err = 0; pad.stick_x = (int8_t)((int)report[offset] - 128); pad.stick_y = (int8_t)(128 - (int)report[offset + 1]);
  pad.sub_x = (int8_t)((int)report[offset + 2] - 128); pad.sub_y = (int8_t)(128 - (int)report[offset + 3]);
  if (report[offset + 8] > 30) { buttons |= DS4_R2; pad.trig_r = report[offset + 8]; }
  if (report[offset + 7] > 30) { buttons |= DS4_L2; pad.trig_l = report[offset + 7]; }
  std::lock_guard<std::mutex> lock(g_ds4_mutex); g_ds4_buttons[slot] = buttons; g_ds4_pads[slot] = pad;
}
}  // namespace

void input_begin_capture() {
  { std::lock_guard<std::mutex> lock(g_keys_mutex); std::memcpy(g_capture_key_baseline, g_keys, sizeof g_keys); }
  for (int idx = 0; idx < 4; ++idx) {
    XINPUT_STATE xs{};
    g_capture_pad_baseline[idx] = (XInputGetState(idx, &xs) == ERROR_SUCCESS) ? xs.Gamepad.wButtons : 0;
    PadState gc{}; gc.err = -1;
    gcadapter_poll(&gc); // poll once to prime baseline for any GC adapter port if present
    g_capture_gc_baseline[idx] = gc;
  }
  { std::lock_guard<std::mutex> lock(g_ds4_mutex); for (int idx = 0; idx < 4; ++idx) g_capture_ds4_baseline[idx] = g_ds4_buttons[idx]; }
  g_capturing.store(true);
}

void input_cancel_capture() { g_capturing.store(false); }

bool input_poll_capture(CaptureDevice& device, int& value, int& device_index) {
  if (!g_capturing.load()) return false;
  {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    if (g_keys[VK_ESCAPE] && !g_capture_key_baseline[VK_ESCAPE]) {
      g_capturing.store(false); device = CaptureDevice::None; value = 0; device_index = 0; return true;
    }
    for (int vk = 0; vk < 256; ++vk) {
      if (vk == VK_ESCAPE) continue;
      if (g_keys[vk] && !g_capture_key_baseline[vk]) {
        g_capturing.store(false); device = CaptureDevice::Keyboard; value = vk; device_index = 0; return true;
      }
    }
  }
  for (int idx = 0; idx < 4; ++idx) {
    XINPUT_STATE xs{};
    if (XInputGetState(idx, &xs) != ERROR_SUCCESS) continue;
    unsigned short newly = xs.Gamepad.wButtons & ~g_capture_pad_baseline[idx];
    if (newly) {
      unsigned short lowest = newly & (~(newly - 1));
      g_capturing.store(false); device = CaptureDevice::XInputPad; value = lowest; device_index = idx; return true;
    }
  }
  PadState gc[4];
  uint32_t mask = gcadapter_poll(gc);
  for (int idx = 0; idx < 4; ++idx) {
    if (!(mask & (1u << idx))) continue;
    uint16_t newly = gc[idx].button & ~g_capture_gc_baseline[idx].button;
    if (newly) {
      unsigned short lowest = newly & (~(newly - 1));
      g_capturing.store(false); device = CaptureDevice::GCAdapter; value = lowest; device_index = idx; return true;
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    for (int idx = 0; idx < 4; ++idx) {
      uint16_t newly = g_ds4_buttons[idx] & ~g_capture_ds4_baseline[idx];
      if (newly) {
        uint16_t lowest = newly & (uint16_t)(~(newly - 1));
        g_capturing.store(false); device = CaptureDevice::DS4Pad; value = lowest; device_index = idx; return true;
      }
    }
  }
  return false;
}

namespace {
struct ScriptEntry { uint32_t frame; uint16_t buttons; int8_t sx, sy, cx, cy; int port;  bool relative = false; };
std::vector<ScriptEntry> g_script;
uint32_t g_script_ports = 1;
// `@match` makes later entries relative to the retrace at which an online match reached frame 1
// (they stay silent until then); `@loop N` repeats the relative section every N frames.
static bool g_script_relative_section = false;
static uint32_t g_script_loop = 0;
static std::atomic<uint32_t> g_match_start_retrace{0};
}  // namespace
void input_mark_match_start() { g_match_start_retrace.store(retrace_count()); }

bool input_load_script(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    ScriptEntry e{};
    char* p = line;
    if (*p == '#' || *p == '\n' || *p == '\r') continue;
    if (!strncmp(p, "@match", 6)) { g_script_relative_section = true; continue; }
    if (!strncmp(p, "@loop", 5)) { g_script_loop = (uint32_t)strtoul(p + 5, nullptr, 10); continue; }
    e.relative = g_script_relative_section;
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
  struct UiSnapshot {
    PadState* pads; bool gamecube = false;
    ~UiSnapshot() {
      std::lock_guard<std::mutex> lock(g_ui_pad_mutex); g_ui_pad = pads[0]; g_ui_gamecube = gamecube;
      if (g_ui_capture.load()) { pads[0] = {}; pads[0].err = 0; }
    }
  } ui{out};
  for (int i = 0; i < 4; ++i) { std::memset(&out[i], 0, sizeof out[i]); out[i].err = -1; }
  if (!g_script.empty()) {
    // Scripts drive port 1 by default; entries with p=N drive port N (a port with any entry counts as plugged in).
    uint32_t frame = retrace_count();
    uint32_t start = g_match_start_retrace.load();
    bool in_match = start && frame >= start;
    uint32_t rel = in_match ? frame - start : 0;
    if (in_match && g_script_loop) rel %= g_script_loop;
    for (int port = 0; port < 4; ++port) {
      if (port && !(g_script_ports & (1u << port))) continue;
      out[port].err = 0;
      const ScriptEntry* cur = nullptr;
      for (const ScriptEntry& e : g_script) {
        if (e.port != port) continue;
        if (e.relative) { if (in_match && e.frame <= rel) cur = &e; }
        else if (!in_match && e.frame <= frame) cur = &e;
      }
      if (cur) { PadState& q = out[port]; q.button = cur->buttons; q.stick_x = cur->sx; q.stick_y = cur->sy; q.sub_x = cur->cx; q.sub_y = cur->cy; }
    }
    return;
  }
  // Poll every physical source unconditionally, then route each in-game port to
  // whichever device g_port_sources[port] assigns it to. This lets keyboard, an
  // Xbox pad, and the GC adapter all drive different ports at the same time.
  PadState gc[4];
  for (auto& s : gc) { s = {}; s.err = -1; }
  uint32_t gc_mask = gcadapter_poll(gc);
  ui.gamecube = gc_mask != 0;

  InputDebugSnapshot debug{};
  debug.gc_mask = gc_mask;

  PadState kb{}; kb.err = 0;
  {
    // Keyboard: arrows = stick, IJKL = c-stick, rest from g_key_bindings.
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    auto key = [](int vk) { return g_keys[vk & 0xFF]; };
    int sx = 0, sy = 0, cx = 0, cy = 0;
    if (key(VK_LEFT)) sx -= 127; if (key(VK_RIGHT)) sx += 127; if (key(VK_UP)) sy += 127; if (key(VK_DOWN)) sy -= 127;
    if (key('J')) cx -= 127; if (key('L')) cx += 127; if (key('I')) cy += 127; if (key('K')) cy -= 127;
    for (int i = 0; i < (int)BindAction::Count; ++i) {
      int vk = g_key_bindings.vk[i];
      if (vk && key(vk)) kb.button |= kActionPadBit[i];
    }
    if (int vk = g_key_bindings.vk[(size_t)BindAction::L]; vk && key(vk)) kb.trig_l = 255;
    if (int vk = g_key_bindings.vk[(size_t)BindAction::R]; vk && key(vk)) kb.trig_r = 255;
    kb.stick_x = (int8_t)sx; kb.stick_y = (int8_t)sy; kb.sub_x = (int8_t)cx; kb.sub_y = (int8_t)cy;
  }

  PadState xin[4];
  bool xin_connected[4] = {};
  for (int idx = 0; idx < 4; ++idx) {
    PadState& x = xin[idx]; x = {}; x.err = -1;
    XINPUT_STATE xs{};
    if (XInputGetState(idx, &xs) != ERROR_SUCCESS) continue;
    xin_connected[idx] = true;
    debug.xinput_connected[idx] = true;
    x.err = 0;
    auto& g = xs.Gamepad;
    auto axis = [](SHORT v) { int a = v / 258; return a > 127 ? 127 : a < -127 ? -127 : a; };
    int sx = 0, sy = 0, cx = 0, cy = 0;
    if (abs(g.sThumbLX) > 7849 || abs(g.sThumbLY) > 7849) { sx = axis(g.sThumbLX); sy = axis(g.sThumbLY); }
    if (abs(g.sThumbRX) > 8689 || abs(g.sThumbRY) > 8689) { cx = axis(g.sThumbRX); cy = axis(g.sThumbRY); }
    for (int i = 0; i < (int)BindAction::Count; ++i) {
      unsigned short mask = g_pad_bindings[idx].mask[i];
      if (mask && (g.wButtons & mask)) x.button |= kActionPadBit[i];
    }
    if (g.bLeftTrigger > 30) { x.trig_l = g.bLeftTrigger; if (g.bLeftTrigger > 200) x.button |= PAD_L; }
    if (g.bRightTrigger > 30) { x.trig_r = g.bRightTrigger; if (g.bRightTrigger > 200) x.button |= PAD_R; }
    x.stick_x = (int8_t)sx; x.stick_y = (int8_t)sy; x.sub_x = (int8_t)cx; x.sub_y = (int8_t)cy;
    for (int i = 0; i < (int)BindAction::Count; ++i)
      if ((x.button & kActionPadBit[i]) != 0) debug.xinput_actions[idx] |= (uint16_t)(1u << i);
  }

  PadState ds4[4]{};
  uint16_t ds4_buttons[4]{};
  bool ds4_connected[4]{};
  {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    for (int idx = 0; idx < 4; ++idx) {
      ds4_buttons[idx] = g_ds4_buttons[idx]; ds4[idx] = g_ds4_pads[idx];
      ds4_connected[idx] = g_ds4_devices[idx] != nullptr;
      ds4[idx].err = ds4_connected[idx] ? 0 : -1;
      if (!ds4_connected[idx]) continue;
      for (int i = 0; i < (int)BindAction::Count; ++i)
        if (g_ds4_bindings[idx].mask[i] & ds4_buttons[idx]) ds4[idx].button |= kActionPadBit[i];
      debug.ds4_connected[idx] = true;
      for (int i = 0; i < (int)BindAction::Count; ++i)
        if (ds4[idx].button & kActionPadBit[i]) debug.ds4_actions[idx] |= (uint16_t)(1u << i);
    }
  }

  for (int idx = 0; idx < 4; ++idx)
    if (gc_mask & (1u << idx))
      for (int i = 0; i < (int)BindAction::Count; ++i)
        if (gc[idx].button & kActionPadBit[i]) debug.gc_actions[idx] |= (uint16_t)(1u << i);

  for (int port = 0; port < 4; ++port) {
    const PortSource& src = g_port_sources[port];
    switch (src.kind) {
      case DeviceKind::Keyboard: out[port] = kb; break;
      case DeviceKind::XInputPad:
        if (src.index >= 0 && src.index < 4 && xin_connected[src.index]) out[port] = xin[src.index];
        break;
      case DeviceKind::DS4Pad:
        if (src.index >= 0 && src.index < 4 && ds4_connected[src.index]) out[port] = ds4[src.index];
        break;
      case DeviceKind::GCAdapter:
        if (src.index >= 0 && src.index < 4 && (gc_mask & (1u << src.index))) out[port] = gc[src.index];
        break;
      case DeviceKind::None: default: break;
    }
    debug.ports[port] = out[port];
  }

  debug.keyboard_actions = 0;
  for (int i = 0; i < (int)BindAction::Count; ++i) if (kb.button & kActionPadBit[i]) debug.keyboard_actions |= (uint16_t)(1u << i);
  for (int idx = 0; idx < 4; ++idx) if (xin_connected[idx]) debug.xinput_connected[idx] = true;
  input_debug_snapshot(debug);
}

void input_debug_snapshot(InputDebugSnapshot& snapshot) {
  snapshot = {};
  PadState gc[4];
  for (auto& s : gc) { s = {}; s.err = -1; }
  uint32_t gc_mask = gcadapter_poll(gc);
  snapshot.gc_mask = gc_mask;

  PadState kb{}; kb.err = 0;
  {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    auto key = [](int vk) { return g_keys[vk & 0xFF]; };
    int sx = 0, sy = 0, cx = 0, cy = 0;
    if (key(VK_LEFT)) sx -= 127; if (key(VK_RIGHT)) sx += 127; if (key(VK_UP)) sy += 127; if (key(VK_DOWN)) sy -= 127;
    if (key('J')) cx -= 127; if (key('L')) cx += 127; if (key('I')) cy += 127; if (key('K')) cy -= 127;
    for (int i = 0; i < (int)BindAction::Count; ++i) {
      int vk = g_key_bindings.vk[i];
      if (vk && key(vk)) kb.button |= kActionPadBit[i];
    }
    if (int vk = g_key_bindings.vk[(size_t)BindAction::L]; vk && key(vk)) kb.trig_l = 255;
    if (int vk = g_key_bindings.vk[(size_t)BindAction::R]; vk && key(vk)) kb.trig_r = 255;
    kb.stick_x = (int8_t)sx; kb.stick_y = (int8_t)sy; kb.sub_x = (int8_t)cx; kb.sub_y = (int8_t)cy;
  }
  snapshot.keyboard_actions = 0;
  for (int i = 0; i < (int)BindAction::Count; ++i) if (kb.button & kActionPadBit[i]) snapshot.keyboard_actions |= (uint16_t)(1u << i);

  PadState xin[4];
  for (int idx = 0; idx < 4; ++idx) {
    xin[idx] = {}; xin[idx].err = -1;
    XINPUT_STATE xs{};
    if (XInputGetState(idx, &xs) != ERROR_SUCCESS) continue;
    snapshot.xinput_connected[idx] = true;
    auto& g = xs.Gamepad;
    auto axis = [](SHORT v) { int a = v / 258; return a > 127 ? 127 : a < -127 ? -127 : a; };
    int sx = 0, sy = 0, cx = 0, cy = 0;
    if (abs(g.sThumbLX) > 7849 || abs(g.sThumbLY) > 7849) { sx = axis(g.sThumbLX); sy = axis(g.sThumbLY); }
    if (abs(g.sThumbRX) > 8689 || abs(g.sThumbRY) > 8689) { cx = axis(g.sThumbRX); cy = axis(g.sThumbRY); }
    for (int i = 0; i < (int)BindAction::Count; ++i) {
      unsigned short mask = g_pad_bindings[idx].mask[i];
      if (mask && (g.wButtons & mask)) xin[idx].button |= kActionPadBit[i];
    }
    if (g.bLeftTrigger > 30) { xin[idx].trig_l = g.bLeftTrigger; if (g.bLeftTrigger > 200) xin[idx].button |= PAD_L; }
    if (g.bRightTrigger > 30) { xin[idx].trig_r = g.bRightTrigger; if (g.bRightTrigger > 200) xin[idx].button |= PAD_R; }
    xin[idx].stick_x = (int8_t)sx; xin[idx].stick_y = (int8_t)sy; xin[idx].sub_x = (int8_t)cx; xin[idx].sub_y = (int8_t)cy;
    for (int i = 0; i < (int)BindAction::Count; ++i) if (xin[idx].button & kActionPadBit[i]) snapshot.xinput_actions[idx] |= (uint16_t)(1u << i);
  }

  PadState ds4[4]{};
  uint16_t ds4_buttons[4]{};
  for (int idx = 0; idx < 4; ++idx) {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    ds4_buttons[idx] = g_ds4_buttons[idx]; ds4[idx] = g_ds4_pads[idx];
    if (!g_ds4_devices[idx]) { ds4[idx].err = -1; continue; }
    snapshot.ds4_connected[idx] = true; ds4[idx].err = 0;
    for (int i = 0; i < (int)BindAction::Count; ++i) {
      if (g_ds4_bindings[idx].mask[i] & ds4_buttons[idx]) ds4[idx].button |= kActionPadBit[i];
      if (ds4[idx].button & kActionPadBit[i]) snapshot.ds4_actions[idx] |= (uint16_t)(1u << i);
    }
  }

  for (int idx = 0; idx < 4; ++idx) {
    snapshot.gc_actions[idx] = 0;
    if (!(gc_mask & (1u << idx))) continue;
    for (int i = 0; i < (int)BindAction::Count; ++i) if (gc[idx].button & kActionPadBit[i]) snapshot.gc_actions[idx] |= (uint16_t)(1u << i);
    snapshot.ports[idx] = gc[idx];
  }
  for (int port = 0; port < 4; ++port) {
    const PortSource& src = g_port_sources[port];
    switch (src.kind) {
      case DeviceKind::Keyboard: snapshot.ports[port] = kb; break;
      case DeviceKind::XInputPad:
        if (src.index >= 0 && src.index < 4 && snapshot.xinput_connected[src.index]) snapshot.ports[port] = xin[src.index];
        break;
      case DeviceKind::DS4Pad:
        if (src.index >= 0 && src.index < 4 && snapshot.ds4_connected[src.index]) snapshot.ports[port] = ds4[src.index];
        break;
      case DeviceKind::GCAdapter:
        if (src.index >= 0 && src.index < 4 && (gc_mask & (1u << src.index))) snapshot.ports[port] = gc[src.index];
        break;
      case DeviceKind::None: default: break;
    }
  }
}

}  // namespace host
