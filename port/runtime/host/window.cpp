// Win32 window, message pump, keyboard + XInput controller mapping to GameCube pads.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include "test_env.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_paths.h"
#include "yougame_agent_pad_trace.h"
#endif
#include <windows.h>
#include <xinput.h>
#include <hidsdi.h>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <thread>
#include <memory>
#include "yougame_link.h"
#include "yougame_friends.h"
#include "yougame_console_entry.h"
#include "yougame_parity.h"
#include "yougame_shortcuts.h"
#ifdef _WIN32
#include "yougame_embed_view.h"
#include "yougame_embed_toplevel.h"
#include "yougame_embed_volume.h"
#include "audio.h"
#endif
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_pad.h"
#include "yougame_agent_keys.h"
#include "yougame_agent_fight.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"
#else
#include "slippi_online.h"
#endif
#endif
#include "host.h"
#include "sim_work.h"
#include "early_input.h"
#include "window.h"
#include "cursor_idle.h"   // window_sdl.inl (Linux) hides the pointer after a rest
#include "walter_egg.h"
#include "replay_control.h"
#include "input_bindings.h"
#include "keyboard_ui_navigation.h"
#include "controller_relay.h"
#include "settings_chord.h"
#include "hid_pad.h"
#include "playstation_pad.h"
#ifndef _WIN32
#include "sdl_video_thread.h"
#include <SDL.h>
#ifdef MELEE_LINUX_X11_EMBED
#include <SDL_syswm.h>
#include "embed/yougame_embed_x11.h"
#include "embed_visibility.h"
#include "audio.h"
#include <sstream>
// Xlib defines None as a macro; host input enums use None as a member.
#undef None
#endif
#ifdef __APPLE__
#include "../../mac/embed_mac.h"
#endif
#ifdef __APPLE__
#include <SDL_syswm.h>
extern "C" bool mac_window_is_fullscreen(void*);
extern "C" void mac_toggle_fullscreen(void*);
#endif
#endif

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "xinput9_1_0.lib")

namespace host {

namespace {
// The caption the YouGame app looks for (yougame-native.json "embed.title").
constexpr const wchar_t* kBoxTitle = L"Melee Unlocked";
HWND g_hwnd = nullptr;
bool g_keys[256];
std::mutex g_keys_mutex;
bool g_closed = false;
yougame::shortcuts::Keys g_shortcut_keys;   // protected by g_keys_mutex
auto& g_overlay_key = g_shortcut_keys.menu();   // window thread's scripted taps
int g_client_w = 1280, g_client_h = 960;
ResizeCallback g_on_resize;
DpiCallback g_on_dpi;
std::atomic<bool> g_fullscreen_toggle{false};
std::atomic<bool> g_activated{false};   // the game window came back to the front (WM_ACTIVATEAPP)
std::atomic<bool> g_settings_toggle{false};
std::atomic<bool> g_legacy_settings_toggle{false};
std::atomic<int> g_trace_mark{0};   // 0 none, else the kind of the last mark key pressed (window.h)
std::atomic<int> g_settings_controller_port{-1};
std::atomic<bool> g_practice_toggle{false};
std::atomic<bool> g_escape_press{false};
yougame::parity::BoxEscape g_box_escape;   // window thread: an Esc the game's YouGame box took
std::atomic<bool> g_capturing{false};   // a control rebind is waiting for its key (input_begin_capture)
MessageCallback g_on_message;
std::atomic<bool> g_ui_capture{false};
std::atomic<uint16_t> g_ui_withheld{0};
std::atomic<uint8_t> g_keyboard_port_mask{0};   // bit n: the game's port n is fed by the keyboard (input_keyboard_button_label)
std::mutex g_ui_pad_mutex;
PadState g_ui_pad{};
PadState g_ui_pads[4]{};
PadState g_ui_navigation_pads[4]{};
bool g_ui_has_pad = false;
bool g_ui_gamecube = false;
bool g_ui_settings_chord_held[4]{};
bool pad_menu_chord();
yougame::parity::OverlayPad g_overlay_pad = [] {   // input/simulation thread only
  yougame::parity::OverlayPad pad;
  pad.standalone_menu = pad_menu_chord;
  return pad;
}();
int g_ui_start_waited[4]{};   // settings_shortcut::hold_lone_start, per port
#ifdef YOUGAME_AGENT_TEST_HOOK
int g_ui_agent_step = -1;   // protected by g_ui_pad_mutex, alongside the snapshot it identifies
#endif
bool g_ui_practice_chord_held[4]{};
#ifdef _WIN32
void raw_input(HRAWINPUT raw);
// Caller holds g_keys_mutex. Only the window thread updates the chord machines.
void apply_shortcuts() {
  const auto config = yougame::shortcuts::commands.snapshot();
  for (const char* line : g_shortcut_keys.configure(config).lines) yougame::parity::signals.emit(line);
  if (!yougame::stdio_bridge()) return;
  static bool logged = false;
  static yougame::shortcuts::Config last;
  if (logged && config == last) return;
  logged = true;
  last = config;
  static constexpr const char* actions[] = {
    "A", "B", "X", "Y", "Z", "Start", "L", "R", "DUp", "DDown", "DLeft", "DRight",
    "CUp", "CDown", "CLeft", "CRight", "SUp", "SDown", "SLeft", "SRight", "LAnalog", "RAnalog",
    "ModX", "ModY", "LightShield", "MidShield", "StickMod"
  };
  static_assert(std::size(actions) == static_cast<size_t>(BindAction::Count));
  for (size_t i = 0; i < std::size(actions); ++i) {
    const int vk = g_key_bindings.vk[i];
    for (const auto combo : {config.menu, config.chat}) {
      if (!vk || vk != combo.key) continue;
      std::string chord;
      if (combo.modifiers & yougame::shortcuts::Shift) chord += "Shift+";
      if (combo.modifiers & yougame::shortcuts::Ctrl) chord += "Ctrl+";
      if (combo.modifiers & yougame::shortcuts::Alt) chord += "Alt+";
      chord += vk == 9 ? "Tab" : vk == 13 ? "Enter" : vk == 192 ? "Backquote" :
          vk >= 112 && vk <= 123 ? "F" + std::to_string(vk - 111) : std::string(1, char(vk));
      log("embed: shortcut %s shares key %d with keyboard action %s (the plain key still goes to the game)",
          chord.c_str(), vk, actions[i]);
    }
  }
}
bool shortcut_swallowed(int vk) {
  return g_shortcut_keys.swallows(yougame::stdio_bridge(), vk);
}
#else
// Linux: /dev/hidraw reports, read on hid_linux.cpp's thread, go to the same readers.
void hidraw_start();
void hidraw_stop();
#endif
void input_device_removed(HANDLE device);
void ds4_init_defaults();
#ifdef _WIN32
// HID pads (a B0XX and other boxes, Frame1, DualShock, DualSense, Switch Pro, third-party pads) on a
// thread of their own. Their reports come as WM_INPUT messages, and on the game window those waited
// for its message pump, which runs once a rendered frame: a box polled at 1000 Hz was seen at the frame
// rate (240 Hz at 240 fps, 60 at a 60 cap), so a press could reach the game's poll up to a frame late.
// Here a message-only window takes every gamepad and joystick report and the thread sleeps in
// GetMessage, so each report is decoded the moment Windows has it. The readers lock their own state.
// MELEE_PAD_THREAD=0 puts them back on the window's thread (for comparing).
LRESULT CALLBACK pad_input_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_INPUT) { raw_input((HRAWINPUT)l); return 0; }
  if (m == WM_INPUT_DEVICE_CHANGE) { if (w == GIDC_REMOVAL) input_device_removed((HANDLE)l); return 0; }
  return DefWindowProcW(h, m, w, l);
}
bool pad_input_thread_start() {
  static std::atomic<int> state{0};   // 0 not tried, 1 running, 2 failed
  if (const int now = state.load()) return now == 1;
  if (const char* v = std::getenv("MELEE_PAD_THREAD"); v && *v == '0') { state = 2; return false; }
  struct Start { std::mutex m; std::condition_variable cv; int result = 0; };
  auto start = std::make_shared<Start>();
  std::thread([start] {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = pad_input_proc; wc.lpszClassName = L"MeleePadInput";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MeleePadInput", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x05; rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY; rid[0].hwndTarget = hwnd;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x04; rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY; rid[1].hwndTarget = hwnd;
    const bool ok = hwnd && RegisterRawInputDevices(rid, 2, sizeof rid[0]);
    { std::lock_guard<std::mutex> lock(start->m); start->result = ok ? 1 : 2; }
    start->cv.notify_all();
    if (!ok) { if (hwnd) DestroyWindow(hwnd); return; }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
  }).detach();
  std::unique_lock<std::mutex> lock(start->m);
  start->cv.wait_for(lock, std::chrono::seconds(5), [&] { return start->result != 0; });
  const bool ok = start->result == 1;
  state = ok ? 1 : 2;
  if (ok) log("input: HID pads read on their own thread, as each report arrives");
  return ok;
}
#endif

// The pointer also goes away after it has rested over the game for a few seconds in the two cases
// the rule below leaves it up: a menu or panel is open (a player driving it with a controller does
// not need the arrow), and the game window is not the focused one. It comes back the moment the
// mouse moves. The window thread only (wnd_proc and window_pump).
constexpr ULONGLONG kPointerRestMs = 2500;
ULONGLONG g_pointer_moved = 0;      // tick of the last real mouse movement over the window
LPARAM g_pointer_at = -1;           // where it was then (client coordinates as WM_MOUSEMOVE packs them)
bool g_pointer_rested = false;      // hidden by the rest rule, until the next movement
// The game window's last place: window-place.ini beside the settings file (window_remember_place), "" none.
std::string g_place_file;

#ifdef _WIN32
// The visible game window opens where it was left (a player's report 11-27: borderless full screen always
// opened on the first monitor, never the second the player plays on). place_save writes it as the window closes.
bool g_place_window = false;   // this window is the visible game window
RECT g_normal_rect{};          // its windowed rectangle from before borderless full screen (screen pixels)
void place_save();
// The renderer's startup runs on a worker while the window's own thread keeps answering its messages
// (window_responsive). Meanwhile that thread runs none of the renderer's callbacks (the worker is still
// installing them) and the progress panel's steps are queued to it (loading_show).
std::atomic<bool> g_startup_busy{false};
DWORD g_window_thread = 0;
bool off_window_thread() { return g_startup_busy.load() && GetCurrentThreadId() != g_window_thread; }
// Child mode keeps the app in the foreground; a real toplevel follows standalone focus.
std::atomic<bool> g_has_focus{false};
bool window_focused(HWND h) {
  if (yougame::embed_toplevel()) return GetForegroundWindow() == h;
  return yougame::embedded() ? g_has_focus.load() : GetForegroundWindow() == h;
}
// The player box's commands, posted from the bridge's reader thread to this window's thread.
constexpr UINT kBoxRect = WM_APP + 20, kBoxShow = WM_APP + 21, kBoxFocus = WM_APP + 22,
               kBoxView = WM_APP + 23, kBoxScreenRect = WM_APP + 24;
yg_embed_view_state g_embed_view;
yougame::embed_toplevel_policy::FocusReturn g_toplevel_focus;
bool g_toplevel_rect = false, g_toplevel_shown = true, g_toplevel_fallback = false;

uint32_t toplevel_app_pid(HWND h) {
  DWORD owner_pid = 0;
  GetWindowThreadProcessId(GetWindow(h, GW_OWNER), &owner_pid);
  const uint32_t env_pid = yougame::app_pid();
  static bool mismatch_logged = false;
  if (owner_pid && env_pid && owner_pid != env_pid && !mismatch_logged) {
    mismatch_logged = true;
    log("embed: toplevel owner pid %lu differs from YOUGAME_APP_PID %u", (unsigned long)owner_pid, env_pid);
  }
  return yougame::embed_toplevel_policy::preferred_app_pid(owner_pid, env_pid);
}

void return_toplevel_focus(HWND h) {
  if (!yougame::embed_toplevel_policy::emit_focus_return(GetForegroundWindow() == h, g_has_focus.load())) return;
  const uint32_t pid = toplevel_app_pid(h);
  if (pid && !AllowSetForegroundWindow(pid)) {
    const DWORD error = GetLastError();
    static bool failure_logged = false;
    if (!failure_logged) {
      failure_logged = true;
      log("embed: toplevel AllowSetForegroundWindow failed err=%lu", (unsigned long)error);
    }
  }
  yougame::parity::signals.emit(yougame::embed_toplevel_policy::kFocusReturn);
  if (g_toplevel_focus.changed())
    log("%s", yougame::embed_toplevel_policy::focus_return_message(pid).c_str());
}

void apply_toplevel_geometry(HWND h, WPARAM w, LPARAM l) {
  using namespace yougame::embed_toplevel_policy;
  static_assert(sizeof(WPARAM) == sizeof(uint64_t), "screen rect messages require Win64");
  // The app supplies physical screen pixels, including negative monitor origins.
  if (!SetWindowPos(h, nullptr, first(w), second(w), first(l), second(l), SWP_NOACTIVATE | SWP_NOZORDER)) return;
  g_toplevel_rect = true;
  if (g_toplevel_shown && !IsWindowVisible(h)) ShowWindow(h, SW_SHOWNA);
}

// The app's Chromium widget can end up above the game among the player box's children (seen after leaving
// full screen, 10-29): clicks then go to the page while keys and pads still reach the game. Every box update
// puts the visible game back on top of its siblings, without taking focus.
void raise_embed(HWND h) {
  if (yougame::embed_toplevel()) return;   // ownership supplies z-order
  if (IsWindowVisible(h)) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// The YouGame app's button over a game started on its own (yougame::app_button_shown): its page draws it
// 40x40 at 16,16 in the client's top-left corner, in a click-through window that takes clicks only once
// its 50 ms cursor poll finds the pointer over it. A click there that reached this window missed the
// button (the pointer had just arrived, or the click is the one that brings the game back in front, when
// the app keeps its button hidden), so the game asks for the app's menu itself, as Shift+Tab does.
constexpr int kAppButtonArea = 72;   // 16 + 40 + 16, at 96 dpi
bool in_app_button_area(HWND h, int x, int y) {
  if (yougame::embedded() || !yougame::app_button_shown()) return false;
  const int dpi = static_cast<int>(GetDpiForWindow(h));
  const int edge = MulDiv(kAppButtonArea, dpi > 0 ? dpi : 96, 96);
  return x >= 0 && y >= 0 && x < edge && y < edge;
}

void apply_embed_geometry(HWND h, WPARAM w, LPARAM l, bool is_view) {
  const yg_embed_box box{(int)(w >> 16), (int)(w & 0xFFFF), (int)(l >> 16), (int)(l & 0xFFFF)};
  const bool first_view = is_view && !g_embed_view.has_view;
  const auto result = yg_embed_view_update(&g_embed_view, box, is_view);
  if (!g_embed_view.has_view) {
    SetWindowPos(h, nullptr, box.x, box.y, box.w, box.h, SWP_NOZORDER | SWP_NOACTIVATE);
    raise_embed(h);
    return;
  }
  if (is_view) log("embed: view %d %d %d %d", box.x, box.y, box.w, box.h);
  if (result.warn_clamp) log("embed: rect outside view; clamped to view");
  const auto& clip = result.clip;
  HRGN region = CreateRectRgn(clip.x, clip.y, clip.x + clip.w, clip.y + clip.h);
  // Windows owns the region only after a successful SetWindowRgn.
  if (region && !SetWindowRgn(h, region, TRUE)) DeleteObject(region);
  if (result.geometry && (first_view || result.move || result.resize)) {
    const auto& window = result.window;
    SetWindowPos(h, nullptr, window.x, window.y, window.w, window.h,
                 SWP_NOZORDER | SWP_NOACTIVATE | (result.resize ? 0 : SWP_NOSIZE));
  }
  if (!result.resize) log("embed: clip %d %d %d %d (no resize)", clip.x, clip.y, clip.w, clip.h);
  ShowWindow(h, result.visible ? SW_SHOWNA : SW_HIDE);
  raise_embed(h);
}

// Picture in picture (the warm-up room's two windows, hle/slippi_arena.cpp): this game's window, or
// the room's match in its viewer window, made small (a borderless frame on top of the other one:
// dragged anywhere, resized from its edges, never taking the focus, its sound off) or big (filling
// the area the game had), and put back exactly as it was. The game that owns the room asks the
// other window with a registered message (wParam: 1 small, 2 big, 0 back, +4 muted).
UINT pip_message() {
  static const UINT m = RegisterWindowMessageW(L"MeleeUnlocked.PictureInPicture");
  return m;
}
int g_pip = 0;   // 0 as it was, 1 small, 2 big
bool g_pip_no_focus = false;   // the room's match window: never takes the focus from the game in Training
std::atomic<int> g_pip_answer{-1};   // the other window's answer: the mode it has taken
std::atomic<int> g_spectate_choice{-1};   // the match window's view chosen at its corner, -1 none new
std::atomic<int> g_spectate_action{-1};   // a button pressed on the match window (window_spectate_action), -1 none new
// The match window's corner selector, in client pixels: clicks there go to it, not to the drag.
std::atomic<int> g_hot_rect[4] = {0, 0, 0, 0};
std::atomic<uint64_t> g_hover_tick{0};   // GetTickCount64 when the mouse was last over the small window
std::atomic<int> g_pip_clicks{0};        // clicks on the small window that did not move it (its style menu)
std::atomic<bool> g_pip_moving{false};   // being dragged or sized with the mouse
std::atomic<bool> g_pip_selected{false}; // picked with the controller (Practice menu, Move and resize)
std::atomic<uint64_t> g_view_score{0}, g_view_score_tick{0};   // what the room's match window shows (packed)
LONG_PTR g_pip_style = 0, g_pip_exstyle = 0;
WINDOWPLACEMENT g_pip_place{sizeof(WINDOWPLACEMENT)};
void pip_apply(HWND h, int mode, bool mute, bool no_focus) {
  // Never shows a window that is not showing (hidden test runs stay hidden).
  const LONG_PTR visible = GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE;
  if (mode && !g_pip) {
    g_pip_style = GetWindowLongPtrW(h, GWL_STYLE);
    g_pip_exstyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
    GetWindowPlacement(h, &g_pip_place);
  }
  // The new mode first: the frame changes below send WM_NCCALCSIZE at once, and wnd_proc answers it by
  // g_pip (no frame only while small). Set after, a window going back kept "no frame" for good: its
  // title bar never came back, and its inside read as the whole window.
  const int was = g_pip;
  g_pip = mode;
  g_pip_no_focus = mode && no_focus;
  if (mode == 1) {
    // WS_THICKFRAME for the sizing edges (WM_NCHITTEST below); WM_NCCALCSIZE keeps the whole window
    // client, so no frame shows.
    SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_THICKFRAME | visible);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ((g_pip_exstyle | WS_EX_TOOLWINDOW | (no_focus ? WS_EX_NOACTIVATE : 0)) & ~(LONG_PTR)WS_EX_APPWINDOW));
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  } else if (mode == 2) {
    SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | visible);
    SetWindowLongPtrW(h, GWL_EXSTYLE, g_pip_exstyle | (no_focus ? WS_EX_NOACTIVATE : 0));
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  } else if (was) {
    SetWindowLongPtrW(h, GWL_STYLE, (g_pip_style & ~(LONG_PTR)WS_VISIBLE) | visible);
    SetWindowLongPtrW(h, GWL_EXSTYLE, g_pip_exstyle);
    if (visible) SetWindowPlacement(h, &g_pip_place);
    else SetWindowPos(h, nullptr, g_pip_place.rcNormalPosition.left, g_pip_place.rcNormalPosition.top,
                      g_pip_place.rcNormalPosition.right - g_pip_place.rcNormalPosition.left,
                      g_pip_place.rcNormalPosition.bottom - g_pip_place.rcNormalPosition.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  if (mode != was) log("window: picture in picture %s%s", mode == 1 ? "small" : mode == 2 ? "big" : "off", mode && mute ? " (sound off)" : "");
  audio_set_page_volume(mode && mute ? 0 : 100);
}

LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  // wParam +8: the other window's answer (the mode it took); else a request, answered to the window
  // in lParam once it is done (so that window moves this one only after it saved where it was).
  if (m == pip_message()) {
    if (w & 4096) {   // the room's match window: the stocks and percent it shows now
      g_view_score.store((uint64_t)l);
      g_view_score_tick.store(GetTickCount64());
      return 0;
    }
    if (w & 8) { g_pip_answer.store((int)(w & 3)); return 0; }
    if (w & 32) { g_spectate_choice.store((int)((w >> 8) & 3)); return 0; }   // the match window chose a view
    if (w & 8192) { g_spectate_action.store((int)((w >> 8) & 3)); return 0; }   // a button on the match window
    if (w & 64) {   // the game that opened us set one
      options.spectate_view = (int)((w >> 8) & 3);
      log("spectate: view %d set by the room's game", options.spectate_view);
      return 0;
    }
    if (w & 128) {   // the game that opened us picked this window with the controller (bit 10), or let it go
      g_pip_selected.store((w & 1024) != 0);
      return 0;
    }
    pip_apply(h, (int)(w & 3), (w & 4) != 0, (w & 16) != 0);
    if (l) PostMessageW((HWND)l, pip_message(), (WPARAM)(8 | (w & 3)), 0);
    return 0;
  }
  if (g_pip_no_focus && m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;   // a click never takes the focus from Training
  if (g_pip == 1) {
    if (m == WM_ENTERSIZEMOVE) { g_pip_moving.store(true); g_hover_tick.store(GetTickCount64()); }
    if (m == WM_EXITSIZEMOVE) { g_pip_moving.store(false); g_hover_tick.store(GetTickCount64()); }
    if (m == WM_NCLBUTTONDOWN && w == HTCAPTION) {
      // Dragging: the system's own move loop. A press that let go without moving it is a click (it
      // opens the style menu), counted for the overlay.
      RECT before{}, after{};
      GetWindowRect(h, &before);
      DefWindowProcW(h, m, w, l);
      GetWindowRect(h, &after);
      if (EqualRect(&before, &after)) g_pip_clicks.fetch_add(1);
      return 0;
    }
    if (m == WM_NCCALCSIZE && w) return 0;                 // no frame: the whole window is the picture
    if (m == WM_NCHITTEST) {                                // the edges size it, anywhere else drags it
      g_hover_tick.store(GetTickCount64());                 // the mouse is over it (its selector shows)
      RECT r{};
      GetWindowRect(h, &r);
      const int x = (int)(short)LOWORD(l), y = (int)(short)HIWORD(l), e = 8;
      POINT c{x, y};
      ScreenToClient(h, &c);
      if (c.x >= g_hot_rect[0].load() && c.x < g_hot_rect[2].load() && c.y >= g_hot_rect[1].load() && c.y < g_hot_rect[3].load())
        return HTCLIENT;                                    // the view selector takes its clicks
      const bool left = x < r.left + e, right = x >= r.right - e, top = y < r.top + e, bottom = y >= r.bottom - e;
      if (top && left) return HTTOPLEFT;
      if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT;
      if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT;
      if (right) return HTRIGHT;
      if (top) return HTTOP;
      if (bottom) return HTBOTTOM;
      return HTCAPTION;
    }
  }
  if (m == WM_MOUSEMOVE && l != g_pointer_at) {   // Windows also sends this when nothing moved
    g_pointer_at = l;
    g_pointer_moved = GetTickCount64();
    g_pointer_rested = false;
  }
  // The YouGame app's button is showing: the pointer stays an arrow over the game so the player can see
  // what they are clicking (it used to vanish over the game and flicker back only over the button itself),
  // and a click on the button's corner that reached the game opens the app's menu (in_app_button_area).
  if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT && !g_ui_capture.load() && !g_pip && !yougame::embedded() &&
      yougame::app_button_shown()) {
    POINT at{};
    const bool corner = GetCursorPos(&at) && ScreenToClient(h, &at) && in_app_button_area(h, at.x, at.y);
    if (!g_pointer_rested || corner) { SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE; }
  }
  if (m == WM_LBUTTONDOWN && !g_ui_capture.load() && !g_pip && in_app_button_area(h, (int)(short)LOWORD(l), (int)(short)HIWORD(l))) {
    const bool asked = yougame::channel_menu_key("keys");
    log("yougame: click on the YouGame button's corner reached the game (%d,%d); %s", (int)(short)LOWORD(l),
        (int)(short)HIWORD(l), asked ? "asked for the app's menu" : "not asked");
    if (asked) return 0;
  }
  if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT && g_pointer_rested) { SetCursor(nullptr); return TRUE; }
  // MELEE_TEST_BIG_WINDOW=1 (hidden test runs): a --window larger than this PC's desktop is kept
  // (1440p and ultrawide on a 1080p desktop); Windows otherwise caps it at the screen.
  if (m == WM_GETMINMAXINFO) {
    static const bool big = [] { const char* v = host::test_env::getenv("MELEE_TEST_BIG_WINDOW"); return v && *v == '1'; }();
    if (big) { auto* mmi = reinterpret_cast<MINMAXINFO*>(l); mmi->ptMaxTrackSize = POINT{16384, 16384}; mmi->ptMaxSize = POINT{16384, 16384}; return 0; }
  }
  // No mouse pointer over the game while it has focus and the settings panel is closed. The pointer
  // has nothing to do there, and after alt-tabbing back from a music player it sat in the middle of
  // the screen for the rest of the match. It comes back the moment the panel opens or focus leaves.
  // Before the ImGui handler, which would otherwise set the arrow itself.
  if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT && !g_ui_capture.load() && window_focused(h)) { SetCursor(nullptr); return TRUE; }
  // Esc with the game's YouGame box on screen is its Back, the same as Shift+Tab, taken here before
  // the embedded route, the Esc menu, ImGui and the game's bindings. It used to go through the next
  // drawn frame's shared Esc check (pc_settings.cpp), and on Windows Esc left the box up.
  if (w == VK_ESCAPE && (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) &&
      g_box_escape.key(m == WM_KEYDOWN || m == WM_SYSKEYDOWN, (l & (1LL << 30)) != 0,
                       [] { return yougame::sign_in_escape(); })) {
    std::lock_guard<std::mutex> lock(g_keys_mutex); g_keys[VK_ESCAPE] = false;
    return 0;
  }
  // One embedded Escape press has one game owner: bypass ImGui's NavCancel and pad bindings.
  if (yougame::stdio_bridge() && w == VK_ESCAPE &&
      (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP)) {
    const bool capture_key = yougame::parity::embedded_escape(
        m == WM_KEYDOWN || m == WM_SYSKEYDOWN, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (l & (1LL << 30)) != 0, g_capturing.load(),
        [](const char* key) { yougame::relay_key(key); }, [] { g_escape_press.store(true); });
    std::lock_guard<std::mutex> lock(g_keys_mutex); g_keys[VK_ESCAPE] = capture_key;
    return 0;
  }
  // App shortcuts run before ImGui, bindings, Tab's practice toggle and Alt+Enter.
  // Handle system-key messages too: Windows uses them for Alt chords.
  if (m == WM_KILLFOCUS || (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) ||
      (m == WM_ACTIVATEAPP && !w) || m == WM_CLOSE || m == WM_DESTROY) {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    for (const char* line : g_shortcut_keys.cancel().lines) yougame::parity::signals.emit(line);
  }
  if (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    apply_shortcuts();
    const unsigned mods = ((GetKeyState(VK_SHIFT) & 0x8000) ? yougame::shortcuts::Shift : 0) |
                          ((GetKeyState(VK_CONTROL) & 0x8000) ? yougame::shortcuts::Ctrl : 0) |
                          ((GetKeyState(VK_MENU) & 0x8000) ? yougame::shortcuts::Alt : 0);
    const auto event = g_shortcut_keys.window_event(g_keys, yougame::stdio_bridge(), static_cast<int>(w),
        m == WM_KEYDOWN || m == WM_SYSKEYDOWN, m == WM_SYSKEYDOWN || m == WM_SYSKEYUP,
        mods, (l & (1LL << 30)) != 0);
    for (const char* line : event.lines) yougame::parity::signals.emit(line);
    if (event.swallowed) return 0;
  }
  if (m == WM_CHAR || m == WM_SYSCHAR) {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    const int vk = static_cast<int>(MapVirtualKeyW((l >> 16) & 0xFF, MAPVK_VSC_TO_VK));
    if (shortcut_swallowed(vk)) return 0;
  }
  // Reserve Tab before ImGui or the game's keyboard bindings see it. The edge reaches the practice
  // UI whether a text field is active or the overlay is closed.
  if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && !(l & (1 << 30))) yougame::input_kind("keys");
  if (m == WM_KEYDOWN && w == VK_TAB) {
    // Shift+Tab in a game started on its own opens the connected app's menu instead.
    if (!(l & (1 << 30)) && !((GetKeyState(VK_SHIFT) & 0x8000) && yougame::channel_menu_key()))
      g_practice_toggle.store(true);
    // Tab is reserved for native matchmaking. Never let a binding on the same keyboard key reach
    // Melee for the frame that opens or closes the overlay.
    std::lock_guard<std::mutex> lock(g_keys_mutex); g_keys[VK_TAB] = false; return 0;
  }
  if (m == WM_KEYUP && w == VK_TAB) {
    std::lock_guard<std::mutex> lock(g_keys_mutex); g_keys[VK_TAB] = false; return 0;
  }
  // During renderer startup its worker is still installing ImGui's handler (window_responsive).
  if (!g_startup_busy.load())
    if (g_on_message && g_on_message(h, m, w, l)) return 1;
  switch (m) {
    case WM_CLOSE: place_save(); g_closed = true; request_user_exit(); return 0;
    case WM_APP + 7:   // kCursorRefresh, see window_input_capture
      if (window_focused(h)) SetCursor(w ? LoadCursor(nullptr, IDC_ARROW) : nullptr);
      return 0;
    // The player box (yougame_link.h set_box_handler): where the page's box is, shown or covered, focus.
    case kBoxScreenRect:
      if (yougame::embed_toplevel()) apply_toplevel_geometry(h, w, l);
      return 0;
    case kBoxRect: case kBoxView:
      apply_embed_geometry(h, w, l, m == kBoxView);
      return 0;
    case kBoxShow: {
      if (yougame::embed_toplevel()) {
        g_toplevel_shown = w != 0;
        if (!w) return_toplevel_focus(h);
        if (!w || g_toplevel_rect || g_toplevel_fallback) ShowWindow(h, w ? SW_SHOWNA : SW_HIDE);
        return 0;
      }
      yg_embed_view_show(&g_embed_view, w != 0);
      const bool visible = g_embed_view.has_view ? yg_embed_view_visible(&g_embed_view) : w != 0;
      // A hidden game must not keep the keyboard. The page hides the box without a blur (its play menu,
      // a sheet, the box scrolled away) and nobody can click a hidden window to take the keys back, so
      // they stopped reaching the page after a match.
      if (!visible && GetFocus() == h) {
        SetLastError(0);
        const HWND rc = SetFocus(GetParent(h));
        const DWORD error = GetLastError();
        const HWND after = GetFocus();
        host::log("embed: focus handback rc=%p err=%lu focus-after=%s", (void*)rc, (unsigned long)error,
                  !after ? "none" : after == h ? "self" : after == GetParent(h) ? "parent" : "other");
        host::log("embed: hidden while holding the keyboard; gave it back to the app");
      }
      ShowWindow(h, visible ? SW_SHOWNA : SW_HIDE);
      raise_embed(h);
      return 0;
    }
    case kBoxFocus:
      if (yougame::embed_toplevel()) {
        // The app calls SetForegroundWindow on gameHwnd itself; the game only focuses its own window.
        if (w && IsWindowVisible(h)) { g_toplevel_focus.focused(); SetFocus(h); }
        else if (!w) return_toplevel_focus(h);
        static bool foreground_logged = false;
        if (w && !foreground_logged) {
          foreground_logged = true;
          log("embed: toplevel foreground=%d", GetForegroundWindow() == h ? 1 : 0);
        }
        return 0;
      }
      // Only a visible game takes the keyboard: a focus for a hidden window would strand the keys there.
      if (w && IsWindowVisible(h)) {
        if (GetFocus() != h) host::log("embed: focus");
        SetFocus(h);
      } else if (w) {
        static int ignored = 0;
        if (ignored++ < 3) host::log("embed: focus ignored while the game is hidden");
      } else if (GetFocus() == h) {
        SetLastError(0);
        const HWND rc = SetFocus(GetParent(h));
        const DWORD error = GetLastError();
        const HWND after = GetFocus();
        host::log("embed: focus handback rc=%p err=%lu focus-after=%s", (void*)rc, (unsigned long)error,
                  !after ? "none" : after == h ? "self" : after == GetParent(h) ? "parent" : "other");
        host::log("embed: blur");
      }
      return 0;
    // A click into the game in the player box takes the keyboard: a child window does not get the
    // focus by itself.
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
      if (yougame::embedded() && GetFocus() != h) SetFocus(h);
      break;
    case WM_SETFOCUS: g_has_focus = true; g_toplevel_focus.focused(); break;
    case WM_INPUT: raw_input((HRAWINPUT)l); return 0;
    case WM_INPUT_DEVICE_CHANGE: if (w == GIDC_REMOVAL) input_device_removed((HANDLE)l); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SYSKEYDOWN:
      // Alt+Enter, first press only. In the player box the page owns full screen (it grows the box).
      if (w == VK_RETURN && (l & (1 << 29)) && !(l & (1 << 30))) { if (!yougame::embedded()) g_fullscreen_toggle.store(true); return 0; }
      break;
    case WM_SYSCHAR: if (w == VK_RETURN) return 0; break;   // no beep for Alt+Enter
    // Alt pressed and released alone opens the window menu in keyboard mode, and Windows then runs
    // its modal menu loop on this thread: the renderer draws nothing until another key ends it.
    case WM_SYSCOMMAND: if ((w & 0xFFF0) == SC_KEYMENU) return 0; break;
    case WM_KEYDOWN: {
      // F1 opens and closes the settings panel. Taken here, from the first key-down of a press
      // (bit 30 is set on auto-repeats), rather than from ImGui's key state: the panel only runs on
      // frames that are drawn, so presses made during a load queued up and were replayed afterwards,
      // and ImGui's repeat turned a held key into the panel flickering open and shut.
      //
      // Embedded Escape is routed before ImGui above. F1 keeps the same in-game action.
      if (yougame::stdio_bridge()) {
        if (w == VK_F11 && !(l & (1 << 30))) g_legacy_settings_toggle.store(true);
        if (w == VK_F1 && !(l & (1 << 30))) g_escape_press.store(true);
      } else {
        if (w == VK_F1 && !(l & (1 << 30))) g_settings_toggle.store(true);
        if (w == VK_F11 && !(l & (1 << 30))) g_legacy_settings_toggle.store(true);
        if (w == VK_ESCAPE && !(l & (1 << 30))) g_escape_press.store(true);   // the in-game menu, same rules as F1
      }
      if (!(l & (1 << 30))) {   // F6 looked wrong, F7 input wrong, F8 plain, F9 sounded wrong
        if (w == VK_F8) g_trace_mark.store(1);
        else if (w == VK_F6) g_trace_mark.store(2);
        else if (w == VK_F7) g_trace_mark.store(3);
        else if (w == VK_F9) g_trace_mark.store(4);
      }
      std::lock_guard<std::mutex> lock(g_keys_mutex); if (w < 256) g_keys[w] = true; return 0;
    }
    case WM_KEYUP: { std::lock_guard<std::mutex> lock(g_keys_mutex); if (w < 256) g_keys[w] = false; return 0; }
    case WM_SIZE:
      if (w == SIZE_MINIMIZED) host::log("window: minimized");
      if (w != SIZE_MINIMIZED) {
        // SetWindowRgn/show may send WM_SIZE with unchanged dimensions. Once a
        // view is known, clip-only updates must never reach the resize callback.
        if (g_embed_view.has_view && g_client_w == LOWORD(l) && g_client_h == HIWORD(l)) return 0;
        g_client_w = LOWORD(l); g_client_h = HIWORD(l);
        if (g_on_resize && g_client_w > 0 && g_client_h > 0) g_on_resize(g_client_w, g_client_h);
      }
      return 0;
    // The window moved to a monitor with another DPI. The callback hears the new DPI first, so the
    // frame drawn by the resize below is already at the new scale.
    case WM_DPICHANGED:
      if (yougame::embed_toplevel()) {
        if (g_on_dpi) g_on_dpi(HIWORD(w));
        return 0;   // the next app rect owns placement and size
      }
      if (g_on_dpi) {
        g_on_dpi(HIWORD(w));
        const RECT* to = (const RECT*)l;
        SetWindowPos(h, nullptr, to->left, to->top, to->right - to->left, to->bottom - to->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      break;
    case WM_KILLFOCUS: { g_has_focus = false; std::lock_guard<std::mutex> lock(g_keys_mutex); std::memset(g_keys, 0, sizeof g_keys); return 0; }
    // For stutter reports: switching away, a minimized window and a desktop mode change all change
    // how Windows presents the game, so the log marks when each happened.
    case WM_ACTIVATEAPP:
      host::log(w ? "window: the game is in front again" : "window: another program took the focus");
      if (w) g_activated.store(true);
      break;
    // The game window's place (place_save), the moment it changes: a drag or resize ends, or the window lands on
    // another monitor (Win+Shift+Arrow moves a borderless window with no drag). Not only at close: a game that
    // ends on its own may never get a close.
    case WM_EXITSIZEMOVE:
      place_save();
      break;
    case WM_WINDOWPOSCHANGED:
      if (g_place_window && h == g_hwnd && !IsIconic(h)) {
        static HMONITOR last = nullptr;
        const HMONITOR now = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        if (last && now != last) place_save();
        last = now;
      }
      break;
    case WM_DISPLAYCHANGE:
      host::log("window: the desktop display mode changed to %ux%u, %u bits", (unsigned)LOWORD(l), (unsigned)HIWORD(l), (unsigned)w);
      break;
  }
  return DefWindowProcW(h, m, w, l);
}
#else
// The SDL window feeds its focus events to the Linux compatibility layer.
bool window_focused(HWND) { return linux_compat::window_focused(); }
#endif
// Start with D-pad Down held, in a game started on its own: YouGame's menu, as Shift+Tab, only with
// the game window in front (a pad read in the background never opens the game's box behind the app).
bool pad_menu_chord() {
#ifdef _WIN32
  if (!window_focused(g_hwnd)) return false;
#else
  if (!window_focused(nullptr)) return false;
#endif
  return yougame::channel_menu_key("pad");
}
}  // namespace

void window_remember_place(const std::string& settings_path) {
  const size_t slash = settings_path.find_last_of("/\\");
  g_place_file = (slash == std::string::npos ? std::string() : settings_path.substr(0, slash + 1)) + "window-place.ini";
  log("window: its place is kept in %s", g_place_file.c_str());
}

#ifdef _WIN32
void window_prefer_background(bool) {}
void window_prefer_vulkan(bool) {}   // a Win32 window takes any renderer

void* window_create(int w, int h, const wchar_t* title, bool visible, bool flip) {
  // Starting DLSS and the graphics device keeps this thread away from its messages for 5 to 12 s on some
  // PCs (sl.dlss alone took 5.4 s on 11-19). After about 4 s Windows calls a window that has not read its
  // messages hung and swaps it for a "ghost" window of its own, which also takes the foreground: the game
  // vanished from the startup panel's second step until its first frame, and the YouGame app hid its
  // button ("not in front"). Without ghosting the window stays where it is, in front, throughout.
  static const bool no_ghost = [] {
    DisableProcessWindowsGhosting();
    log("window: no ghost window while startup steps hold this thread (the window stays in front)");
    return true;
  }();
  (void)no_ghost;
  if (yougame::embed_toplevel()) {
    g_toplevel_rect = false;
    g_toplevel_shown = visible;
    g_toplevel_fallback = false;
    g_toplevel_focus = {};
  }
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.hInstance = inst; wc.lpfnWndProc = wnd_proc; wc.lpszClassName = L"MeleePortWindow"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Resource id 1 is the application icon compiled in from app/melee_unlocked.rc. hIcon is the large
  // one (Alt-Tab, the window menu) and hIconSm the 16px one in the title bar; without these the
  // window shows Windows' default application icon even though Explorer shows ours.
  wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
  wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);   // black, not white, before the first present
  RegisterClassExW(&wc);
  ds4_init_defaults();
  // Usage 0x05 is Game Pad, which is what a DualShock calls itself. A Switch Pro Controller calls
  // itself usage 0x04, Joystick, so registering only 0x05 means its reports never arrive at all.
  // Anything else on 0x04 is filtered out by device id below, as it already was on 0x05.
  RAWINPUTDEVICE rid[2]{};
  // RIDEV_DEVNOTIFY: WM_INPUT_DEVICE_CHANGE when a pad is plugged in or pulled out.
  rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x05; rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
  rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x04; rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
  RECT r{0, 0, w, h};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  // The YouGame player box: the app makes this window a child of its own (the sandbox launcher does
  // it from outside, since a sandboxed process may not; yougame_link.h). It finds the window by class
  // and caption, so the caption stays fixed, and the window stays hidden until it is adopted.
  const bool boxed = yougame::embedded();
  if (yougame::embed_toplevel()) r = {0, 0, w, h};
  // A first size taller or wider than the desktop's work area put the picture's bottom edge (and the
  // status line drawn on it) under the taskbar; cap it the way apply_client_size does for later sizes.
  if (!boxed && !yougame::embed_toplevel()) {
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
      if (r.right - r.left > work.right - work.left) r.right = r.left + (work.right - work.left);
      if (r.bottom - r.top > work.bottom - work.top) r.bottom = r.top + (work.bottom - work.top);
    }
  }
  // Adopted = a real parent (not the desktop) and WS_CHILD: the launcher sets WS_CHILD before SetParent,
  // and Windows can leave the window on the desktop, so any non-null parent is not proof.
  const auto boxAdopted = [] {
    if (yougame::embed_toplevel()) return GetWindow(g_hwnd, GW_OWNER) != nullptr;
    const HWND parent = GetAncestor(g_hwnd, GA_PARENT);
    return parent && parent != GetDesktopWindow() && (GetWindowLongPtrW(g_hwnd, GWL_STYLE) & WS_CHILD);
  };
  using namespace yougame::embed_toplevel_policy;
  static_assert(popup == WS_POPUP && toolwindow == WS_EX_TOOLWINDOW && no_redirection == WS_EX_NOREDIRECTIONBITMAP);
  // Both Windows game backends promise flip here; other windows retain redirection by default.
  const auto style = styles(yougame::embed_toplevel(), flip, Backend::D3D12,
                            boxed ? WS_POPUP : WS_OVERLAPPEDWINDOW);
  g_hwnd = CreateWindowExW(style.exstyle, wc.lpszClassName, boxed ? kBoxTitle : title, style.style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  if (!g_hwnd) die("cannot create native window");
  g_window_thread = GetCurrentThreadId();
  if ((visible || host::test_env::getenv("MELEE_TEST_WINDOW_PLACE")) && !boxed && !yougame::embed_toplevel() &&
      !g_place_file.empty()) {
    g_place_window = true;
    // Where it was left, when that place is still on a monitor: the window keeps the size it was made
    // with, its top left corner kept inside that monitor's work area so its title bar can be reached.
    long x = 0, y = 0, pw = 0, ph = 0;
    std::FILE* f = _wfopen(std::filesystem::u8path(g_place_file).c_str(), L"rb");
    const bool read = f && std::fscanf(f, "%ld %ld %ld %ld", &x, &y, &pw, &ph) == 4 && pw > 0 && ph > 0;
    if (f) std::fclose(f);
    RECT saved{x, y, x + pw, y + ph}, now{};
    const HMONITOR monitor = read ? MonitorFromRect(&saved, MONITOR_DEFAULTTONULL) : nullptr;
    MONITORINFO mi{sizeof(mi)};
    if (monitor && GetMonitorInfoW(monitor, &mi) && GetWindowRect(g_hwnd, &now)) {
      const long ww = now.right - now.left, wh = now.bottom - now.top;
      x = std::max<long>(mi.rcWork.left, std::min<long>(x, mi.rcWork.right - std::min<long>(ww, mi.rcWork.right - mi.rcWork.left)));
      y = std::max<long>(mi.rcWork.top, std::min<long>(y, mi.rcWork.bottom - std::min<long>(wh, mi.rcWork.bottom - mi.rcWork.top)));
      SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      log("window: opened where it was left (%ld, %ld)", x, y);
    } else if (read) {
      log("window: its last place is on no monitor now; Windows places it");
    }
  }
  { std::lock_guard<std::mutex> lock(g_keys_mutex); apply_shortcuts(); }
  yougame::embed_window_ready(reinterpret_cast<uintptr_t>(g_hwnd));
  if (boxed) {
    HWND window = g_hwnd;
    RECT client{};
    GetClientRect(window, &client);
    g_embed_view = yg_embed_view_init(client.right, client.bottom, 1);
    yougame::set_box_handler([window](const std::string& line) {
      if (yougame::embed_toplevel()) {
        if (line.rfind("rect ", 0) == 0) {
          yg_embed_box box{};
          if (yougame::embed_toplevel_policy::rect(line, true, box))
            PostMessageW(window, kBoxScreenRect,
                         yougame::embed_toplevel_policy::pack(box.x, box.y),
                         yougame::embed_toplevel_policy::pack(box.w, box.h));
          return;
        }
        // The toplevel contract uses rect alone; child view/clipping is parent-relative.
        if (line.rfind("view ", 0) == 0) return;
      }
      int x, y, bw, bh;
      if (std::sscanf(line.c_str(), "rect %d %d %d %d", &x, &y, &bw, &bh) == 4 && x >= 0 && y >= 0 && bw > 0 && bh > 0)
        PostMessageW(window, kBoxRect, ((WPARAM)(x & 0xFFFF) << 16) | (WPARAM)(y & 0xFFFF), ((LPARAM)(bw & 0xFFFF) << 16) | (LPARAM)(bh & 0xFFFF));
      else if (std::sscanf(line.c_str(), "view %d %d %d %d", &x, &y, &bw, &bh) == 4 && x >= 0 && y >= 0 && bw >= 0 && bh >= 0)
        PostMessageW(window, kBoxView, ((WPARAM)(x & 0xFFFF) << 16) | (WPARAM)(y & 0xFFFF), ((LPARAM)(bw & 0xFFFF) << 16) | (LPARAM)(bh & 0xFFFF));
      else if (line == "hide" || line == "show") PostMessageW(window, kBoxShow, line == "show", 0);
      else if (line == "focus" || line == "blur") PostMessageW(window, kBoxFocus, line == "focus", 0);
      else if (line == "quit") request_exit(0);
      else if (line.rfind("volume ", 0) == 0) {
        // The page's volume control: a master over the game's own volume, as on Linux (window_sdl.inl).
        // One log line per change, so a page that sends volume can be told from a game that ignores it.
        static std::atomic<int> last{-1};
        const int volume = yg_embed_volume(line.c_str());
        if (volume < 0) log("embed: ignored a volume command");
        else {
          if (last.exchange(volume) != volume) log("embed: got volume %d", volume);
          audio_set_page_volume(volume);
        }
      }
    });
    // Wait for the app to adopt the window, answering messages meanwhile (the launcher's SetParent
    // sends some), then show it where the page's box is.
    const ULONGLONG until = GetTickCount64() + 30000;
    while ((!boxAdopted() || (yougame::embed_toplevel() && !g_toplevel_rect)) && GetTickCount64() < until) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
      Sleep(5);
    }
    if (yougame::embed_toplevel()) {
      if (boxAdopted()) toplevel_app_pid(g_hwnd);
      if (!boxAdopted() || !g_toplevel_rect) {
        g_toplevel_fallback = true;
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
        if (g_toplevel_shown) ShowWindow(g_hwnd, SW_SHOWNA);
        log("embed: toplevel never adopted; shown as a normal window");
      } else log("window: in the YouGame player box");
    }
    else if (!boxAdopted()) log("window: the YouGame app did not take the window into its player box; showing it on its own");
    else { raise_embed(g_hwnd); log("window: in the YouGame player box"); }
  }
  if (!pad_input_thread_start()) {
    rid[0].hwndTarget = g_hwnd; rid[1].hwndTarget = g_hwnd;
    RegisterRawInputDevices(rid, 2, sizeof rid[0]);
    log("input: HID pads read on the window's thread (their own thread did not start)");
  }
  g_closed = false;
  if (yougame::embed_toplevel()) {
    if (!g_toplevel_rect) { g_client_w = w; g_client_h = h; }
    return g_hwnd;
  }
  if (!boxed || !g_embed_view.has_view) {
    // What the client area really is: the work-area cap above (or Windows' own cap) can make it smaller
    // than asked, and the swapchain is sized from these.
    RECT client{};
    if (!boxed && GetClientRect(g_hwnd, &client) && client.right > 0 && client.bottom > 0) {
      g_client_w = client.right; g_client_h = client.bottom;
    } else { g_client_w = w; g_client_h = h; }
  }
  if (visible && (!boxed || !g_embed_view.has_view || yg_embed_view_visible(&g_embed_view)))
    ShowWindow(g_hwnd, boxed && boxAdopted() ? SW_SHOWNA : SW_SHOW);
  return g_hwnd;
}

namespace {
bool g_fullscreen = false;

// The game window's place as it closes: its windowed rectangle (the one from before borderless full screen
// while in it). A borderless window moved to another monitor (Win+Shift+Arrow) is saved on that monitor.
void place_save() {
  if (!g_place_window || !g_hwnd || g_place_file.empty() || IsIconic(g_hwnd)) return;
  RECT r{};
  if (g_fullscreen) r = g_normal_rect;
  else if (!GetWindowRect(g_hwnd, &r)) return;
  if (r.right <= r.left || r.bottom <= r.top) return;
  const HMONITOR now = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  if (MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST) != now && GetMonitorInfoW(now, &mi))
    OffsetRect(&r, mi.rcWork.left + 40 - r.left, mi.rcWork.top + 40 - r.top);
  std::FILE* f = _wfopen(std::filesystem::u8path(g_place_file).c_str(), L"wb");
  if (!f) return;
  std::fprintf(f, "%ld %ld %ld %ld\n", (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top));
  std::fclose(f);
  log("window: place saved (%ld, %ld)", (long)r.left, (long)r.top);
}
}

void window_set_fullscreen(bool enabled) {
  static WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
  bool& fullscreen = g_fullscreen;
  if (!g_hwnd || enabled == fullscreen) return;
  if (yougame::embedded()) return;   // the player box is the page's to size
  if (enabled) {
    GetWindowPlacement(g_hwnd, &saved);
    GetWindowRect(g_hwnd, &g_normal_rect);   // where it opens next time (place_save)
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
  place_save();
}

bool window_is_fullscreen() { return g_fullscreen; }

void* window_native() { return g_hwnd; }

void window_set_pip(int mode, bool mute) { if (g_hwnd) pip_apply(g_hwnd, mode, mute, false); }

void window_send_pip(void* window, int mode, bool mute) {
  // Posted, never waited on (a viewer still loading answers late): it answers when done, and
  // window_pip_answer() then says the mode it took.
  g_pip_answer.store(-1);
  // +16: the room's match window never takes the focus (the game in Training keeps it).
  if (window) PostMessageW((HWND)window, pip_message(), (WPARAM)((mode & 3) | (mute ? 4 : 0) | 16), (LPARAM)g_hwnd);
}

int window_pip_answer() { return g_pip_answer.load(); }

void window_send_spectate_view(void* window, int view) {
  if (window) PostMessageW((HWND)window, pip_message(), (WPARAM)(64 | ((view & 3) << 8)), 0);
}
void window_report_spectate_view(int view) {
  if (options.spectate_owner) PostMessageW((HWND)(uintptr_t)options.spectate_owner, pip_message(), (WPARAM)(32 | ((view & 3) << 8)), 0);
}
int window_spectate_choice() { return g_spectate_choice.exchange(-1); }
void window_report_spectate_action(int action) {
  if (options.spectate_owner) PostMessageW((HWND)(uintptr_t)options.spectate_owner, pip_message(), (WPARAM)(8192 | ((action & 3) << 8)), 0);
}
int window_spectate_action() { return g_spectate_action.exchange(-1); }
int window_pip_mode() { return g_pip; }
bool window_hovered_recently() { return GetTickCount64() - g_hover_tick.load() < 2000; }
int window_take_clicks() { return g_pip_clicks.exchange(0); }
void window_report_score(uint64_t packed) {
  if (options.spectate_owner) PostMessageW((HWND)(uintptr_t)options.spectate_owner, pip_message(), (WPARAM)4096, (LPARAM)packed);
}
bool window_viewer_score(uint64_t* packed) {
  if (GetTickCount64() - g_view_score_tick.load() > 1000) return false;
  *packed = g_view_score.load();
  return true;
}
bool window_moving() { return g_pip_moving.load(); }
bool window_selected() { return g_pip_selected.load(); }
void window_set_selected(bool on) { g_pip_selected.store(on); }
void window_send_selected(void* window, bool on) {
  if (window) PostMessageW((HWND)window, pip_message(), (WPARAM)(128 | (on ? 1024 : 0)), 0);
}
std::atomic<float> g_picture_aspect{4.0f / 3.0f};
void window_note_picture_aspect(float aspect) { if (aspect > 0.1f && aspect < 10.0f) g_picture_aspect.store(aspect); }
float window_picture_aspect() { return g_picture_aspect.load(); }
void window_set_hot_rect(int x0, int y0, int x1, int y1) {
  g_hot_rect[0] = x0; g_hot_rect[1] = y0; g_hot_rect[2] = x1; g_hot_rect[3] = y1;
}

void* window_of_process(uint32_t pid) {
  struct Find { DWORD pid; HWND found; } find{pid, nullptr};
  EnumWindows([](HWND h, LPARAM p) -> BOOL {
    auto* f = reinterpret_cast<Find*>(p);
    DWORD owner = 0;
    GetWindowThreadProcessId(h, &owner);
    wchar_t cls[64] = {};
    if (owner != f->pid || !GetClassNameW(h, cls, 64) || std::wcscmp(cls, L"MeleePortWindow") != 0) return TRUE;
    f->found = h;
    return FALSE;
  }, reinterpret_cast<LPARAM>(&find));
  return find.found;
}

namespace {
std::atomic<uint64_t> g_pending_client_size{0};   // (w << 32) | h, 0 = nothing requested

// SetWindowPos on a window owned by another thread blocks until that thread pumps, so the request
// is applied from window_pump instead: PeekMessage only ever succeeds on the owning thread.
void apply_client_size(int w, int h) {
  if (!g_hwnd || g_fullscreen || w < 320 || h < 240) return;
  MONITORINFO info{sizeof(info)};
  if (GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
    // A client area larger than the monitor's work area leaves the title bar off screen with no way
    // to drag it back, so cap it at what the desktop can actually show.
    RECT frame{0, 0, w, h};
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    long max_w = info.rcWork.right - info.rcWork.left - ((frame.right - frame.left) - w);
    long max_h = info.rcWork.bottom - info.rcWork.top - ((frame.bottom - frame.top) - h);
    if (w > max_w) w = (int)max_w;
    if (h > max_h) h = (int)max_h;
  }
  if (w == g_client_w && h == g_client_h) return;
  RECT r{0, 0, w, h};
  AdjustWindowRect(&r, (DWORD)GetWindowLongPtrW(g_hwnd, GWL_STYLE), FALSE);
  SetWindowPos(g_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  // AdjustWindowRect uses the system DPI, which is not this window's DPI on a scaled display, so
  // measure what the client area actually became and correct the frame by the difference. Windows
  // also caps a resizable window at its maximum tracking size, and that cap is not ours to beat:
  // one correction, not a loop.
  RECT client{};
  if (!GetClientRect(g_hwnd, &client)) return;
  int got_w = client.right, got_h = client.bottom;
  if (got_w == w && got_h == h) return;
  RECT frame{};
  if (!GetWindowRect(g_hwnd, &frame)) return;
  SetWindowPos(g_hwnd, nullptr, 0, 0, (frame.right - frame.left) + (w - got_w), (frame.bottom - frame.top) + (h - got_h),
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
}  // namespace

// The resolution picker in the PC settings panel. WM_SIZE then reaches the resize callback, which
// recreates the swapchain buffers and the EFB, so the new size applies without a restart.
void window_set_client_size(int w, int h) {
  if (w < 320 || h < 240) return;
  g_pending_client_size.store(((uint64_t)(uint32_t)w << 32) | (uint32_t)h);
}

static double query_refresh_rate();

// The settings panel asks for this on every frame it draws the Video tab (the default frame rate
// follows the monitor), and the answer comes from the display driver through the kernel, which some
// drivers make slow. A monitor's rate does not change from one frame to the next: ask once a second.
double window_refresh_rate() {
  static std::atomic<double> cached{0.0};
  static std::atomic<int64_t> cached_at{0};
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
  const double value = cached.load(std::memory_order_relaxed);
  if (value > 0.0 && now.QuadPart - cached_at.load(std::memory_order_relaxed) < freq.QuadPart) return value;
  const double fresh = query_refresh_rate();
  cached.store(fresh, std::memory_order_relaxed);
  cached_at.store(now.QuadPart, std::memory_order_relaxed);
  return fresh;
}

static double query_refresh_rate() {
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
// The pointer is hidden over the game and shown while a menu has input. Windows only asks for the
// cursor again when the mouse moves, so opening the settings or the Esc menu with the mouse still
// left it invisible; a change now tells the window thread to set it straight away.
constexpr UINT kCursorRefresh = WM_APP + 7;
void window_input_capture(bool capture) {
  if (g_ui_capture.exchange(capture) != capture && g_hwnd) PostMessageW(g_hwnd, kCursorRefresh, capture ? 1 : 0, 0);
}
void window_withhold_buttons(uint16_t mask) { g_ui_withheld.store(mask, std::memory_order_relaxed); }
bool window_ui_gamecube_pad(PadState& pad) { std::lock_guard<std::mutex> lock(g_ui_pad_mutex); pad = g_ui_pad; return g_ui_gamecube; }
bool window_ui_pads(PadState pads[4], PadState navigation_pads[4]) {
  std::lock_guard<std::mutex> lock(g_ui_pad_mutex);
  for (int i = 0; i < 4; ++i) {
    pads[i] = g_ui_pads[i];
    if (navigation_pads) navigation_pads[i] = g_ui_navigation_pads[i];
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (g_ui_agent_step >= 0 && !yougame::agent_test::pad_back.consume_ui(g_ui_agent_step)) {
    pads[0] = {};   // already consumed: release while waiting for input_poll to publish the next step
    if (navigation_pads) navigation_pads[0] = {};
  }
#endif
  return g_ui_has_pad;
}
void window_set_resize_callback(ResizeCallback cb) { g_on_resize = std::move(cb); }
void window_set_dpi_callback(DpiCallback cb) { g_on_dpi = std::move(cb); }
bool window_take_fullscreen_toggle() { return g_fullscreen_toggle.exchange(false); }
bool window_take_activation() { return g_activated.exchange(false); }
// Several presses while nothing was being drawn count as one: the player pressed F1 again because
// nothing seemed to happen, and wants the panel, not an even number of toggles.
bool window_take_settings_toggle() { return g_settings_toggle.exchange(false); }
int window_take_trace_mark() { return g_trace_mark.exchange(0); }
bool window_take_legacy_settings_toggle() { return g_legacy_settings_toggle.exchange(false); }
int window_take_settings_controller_port() { return g_settings_controller_port.exchange(-1); }
bool window_take_practice_toggle() { return g_practice_toggle.exchange(false); }
bool window_take_escape() { return g_escape_press.exchange(false); }
const char* window_menu_key() { return yougame::stdio_bridge() ? yougame::parity::kMenuHint : "Esc"; }

void window_destroy() {
  {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    for (const char* line : g_shortcut_keys.cancel().lines) yougame::parity::signals.emit(line);
  }
  place_save();
  if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
}

void window_pump() {
  // Renderer startup on its worker (a Vulkan prewarm pumps between pipelines): the window's own thread
  // is answering the messages meanwhile (window_responsive).
  if (off_window_thread()) return;
  if (yougame::stdio_bridge() && yougame::shortcuts::commands.take_settings()) g_escape_press.store(true);
  {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    apply_shortcuts();
  }
  if (uint64_t want = g_pending_client_size.exchange(0))
    apply_client_size((int)(want >> 32), (int)(want & 0xFFFFFFFFu));
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!g_closed) yougame::agent_test::key_script.pump(yougame::stdio_bridge(),
      yougame::agent_test::KeyScript::Clock::now(), yougame::parity::signals, g_overlay_key,
      [] { yougame::relay_key("escape"); },
      [](const std::string& line) { log("%s", line.c_str()); });
#endif
  // The rest rule: the pointer has been still over the game's picture for a while.
  if (g_hwnd && !g_pointer_rested && g_pointer_moved && GetTickCount64() - g_pointer_moved >= kPointerRestMs) {
    POINT at;
    RECT client;
    if (GetCursorPos(&at) && WindowFromPoint(at) == g_hwnd && GetClientRect(g_hwnd, &client) &&
        ScreenToClient(g_hwnd, &at) && PtInRect(&client, at) && !in_app_button_area(g_hwnd, at.x, at.y)) {
      g_pointer_rested = true;
      SetCursor(nullptr);
    }
  }
}

// In the player box the caption is how the app finds the window: it never changes there.
void window_set_title(const wchar_t* title) { if (g_hwnd && !yougame::embedded()) SetWindowTextW(g_hwnd, title); }

// Compiling the pipeline cache on the first launch of a new version keeps the first frame back for
// 20 seconds or more on some machines, with nothing in the game window, which looks like a hang.
// This panel sits over the game window meanwhile and shows how far along the work is.
namespace {
HWND g_loading = nullptr;
HFONT g_loading_title_font = nullptr, g_loading_font = nullptr;
std::wstring g_loading_text;
size_t g_loading_done = 0, g_loading_total = 0;

void loading_fill(HDC dc, const RECT& r, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &r, brush);
  DeleteObject(brush);
}

LRESULT CALLBACK loading_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_ERASEBKGND) return 1;   // WM_PAINT covers every pixel
  if (m != WM_PAINT) return DefWindowProcW(h, m, w, l);
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  RECT rc;
  GetClientRect(h, &rc);
  loading_fill(dc, rc, RGB(0x14, 0x17, 0x20));
  SetBkMode(dc, TRANSPARENT);
  const int pad = 18;
  RECT title{pad, 12, rc.right - pad, 40};
  SelectObject(dc, g_loading_title_font);
  SetTextColor(dc, RGB(0xF2, 0xF3, 0xF7));
  DrawTextW(dc, window_title_base().c_str(), -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  wchar_t line[192];
  swprintf_s(line, L"%ls   %zu / %zu", g_loading_text.c_str(), g_loading_done, g_loading_total);
  RECT text{pad, 44, rc.right - pad, 66};
  SelectObject(dc, g_loading_font);
  SetTextColor(dc, RGB(0x9A, 0xA3, 0xB5));
  DrawTextW(dc, line, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  const RECT track{pad, 78, rc.right - pad, 90};
  loading_fill(dc, track, RGB(0x2A, 0x2F, 0x3D));
  if (g_loading_total) {
    RECT bar = track;
    const double f = (double)std::min(g_loading_done, g_loading_total) / (double)g_loading_total;
    bar.right = track.left + (LONG)((track.right - track.left) * f);
    loading_fill(dc, bar, RGB(0x9B, 0x6B, 0xFF));
  }
  EndPaint(h, &ps);
  return 0;
}
// Panel steps reported from the renderer's startup worker (window_responsive): the panel belongs to the
// window's thread, which draws them in order as it services its messages.
struct LoadingStep { bool close; std::wstring what; size_t done, total; };
std::mutex g_loading_queue_mutex;
std::vector<LoadingStep> g_loading_queue;   // g_loading_queue_mutex
void loading_apply(const wchar_t* what, size_t done, size_t total) {
  if (!g_loading) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc; wc.hInstance = inst; wc.lpfnWndProc = loading_proc;
    wc.lpszClassName = L"MeleePortLoading"; wc.hCursor = LoadCursor(nullptr, IDC_APPSTARTING);
    RegisterClassExW(&wc);
    const int w = 460, h = 108;
    RECT owner{};
    GetWindowRect(g_hwnd, &owner);
    const int x = owner.left + ((owner.right - owner.left) - w) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - h) / 2;
    g_loading_title_font = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_loading_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    // Owned by the game window, so it stays above it, and never activated, so it takes no focus.
    g_loading = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP | WS_BORDER,
                                x, y, w, h, g_hwnd, nullptr, inst, nullptr);
    if (!g_loading) return;
    ShowWindow(g_loading, SW_SHOWNOACTIVATE);
  }
  g_loading_text = what;
  g_loading_done = done;
  g_loading_total = total;
  InvalidateRect(g_loading, nullptr, FALSE);
  UpdateWindow(g_loading);
  // Retrieving this thread's messages while it waits also keeps Windows from marking the game
  // window as not responding.
  MSG msg;
  while (PeekMessageW(&msg, g_loading, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}
void loading_close_now() {
  if (g_loading) { DestroyWindow(g_loading); g_loading = nullptr; }
  if (g_loading_title_font) { DeleteObject(g_loading_title_font); g_loading_title_font = nullptr; }
  if (g_loading_font) { DeleteObject(g_loading_font); g_loading_font = nullptr; }
}
void loading_service() {
  std::vector<LoadingStep> steps;
  { std::lock_guard<std::mutex> lock(g_loading_queue_mutex); steps.swap(g_loading_queue); }
  for (const auto& step : steps) {
    if (step.close) loading_close_now();
    else loading_apply(step.what.c_str(), step.done, step.total);
  }
}
}  // namespace

void loading_show(const wchar_t* what, size_t done, size_t total) {
  if (!g_hwnd || !IsWindowVisible(g_hwnd)) return;   // hidden and headless runs show nothing
  if (off_window_thread()) {
    std::lock_guard<std::mutex> lock(g_loading_queue_mutex);
    g_loading_queue.push_back({false, what ? what : L"", done, total});
    return;
  }
  loading_apply(what, done, total);
}

void loading_close() {
  if (off_window_thread()) {
    std::lock_guard<std::mutex> lock(g_loading_queue_mutex);
    g_loading_queue.push_back({true, std::wstring(), 0, 0});
    return;
  }
  loading_close_now();
}

// Starting the renderer took this thread away from its messages for 5 to 12 s on some PCs (sl.dlss alone
// 5.4 s and the graphics device 6.6 s on 11-19): Windows then treats the window as hung, and the YouGame
// app, which shows its button only over a game in front that answers, hid it until the first frame. The
// work now runs on a worker while this thread answers every message sent to the window (any PeekMessage
// does) and draws the panel's steps. Messages posted to the game window are taken and held for the end of
// startup, as before: the renderer is not ready for input, sizes or the app's box commands yet.
void window_responsive(const std::function<void()>& work) {
  if (!g_hwnd || GetCurrentThreadId() != g_window_thread || g_startup_busy.load()) { work(); return; }
  // 0.9.990: only where the YouGame app hosts or overlays the game (an app-backed launch: the launcher's
  // extension or app account, the app's stdio or player box). Every other launch starts on this thread
  // as before (exclusive fullscreen, frame generation and the compat builds were proven that way).
  const char* extension = std::getenv("MELEE_YOUGAME_EXTENSION");
  const bool app_backed = yougame::embedded() || yougame::stdio_bridge() || (extension && extension[0] == '1');
  if (!app_backed) { work(); return; }
  // MELEE_TEST_STARTUP_INLINE=1 (hidden test runs): the old way, on this thread, for a before/after check.
  if (std::getenv("MELEE_NO_GC_ADAPTER") && host::test_env::getenv("MELEE_TEST_STARTUP_INLINE")) { work(); return; }
  HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!finished) { work(); return; }
  std::exception_ptr error;
  g_startup_busy.store(true);
  std::thread worker([&] {
    try { work(); } catch (...) { error = std::current_exception(); }
    SetEvent(finished);
  });
  using Clock = std::chrono::steady_clock;
  const auto began = Clock::now();
  auto last = began;
  long long longest = 0;
  // Every queued message is taken (Windows calls a thread hung that leaves posted input waiting for
  // seconds, even while it answers sent ones). The game window's are kept and posted again at the end,
  // so they reach it at the next pump as they did before; paint, raw input and the panel's run now.
  std::vector<MSG> held;
  for (bool done = false; !done;) {
    // Wakes at once for anything for this thread, and every 25 ms for the panel.
    done = MsgWaitForMultipleObjects(1, &finished, FALSE, 25, QS_ALLINPUT) == WAIT_OBJECT_0;
    loading_service();
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {   // answers sent messages too
      const bool game = !msg.hwnd || msg.hwnd == g_hwnd || IsChild(g_hwnd, msg.hwnd);
      if (game && msg.message != WM_PAINT && msg.message != WM_INPUT && msg.message != WM_INPUT_DEVICE_CHANGE) {
        held.push_back(msg);
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    const auto now = Clock::now();
    longest = std::max<long long>(longest, std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count());
    last = now;
  }
  worker.join();
  CloseHandle(finished);
  g_startup_busy.store(false);
  loading_service();
  for (const MSG& msg : held) {
    if (msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
    else if (!msg.hwnd) PostThreadMessageW(GetCurrentThreadId(), msg.message, msg.wParam, msg.lParam);
    else PostMessageW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
  }
  log("startup: the renderer started on a worker while the window answered its messages (longest gap %lld ms in %lld ms, %zu held for after)",
      longest, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count(), held.size());
  if (error) std::rethrow_exception(error);
}
bool window_closed() { return g_closed; }
void window_client_size(int* w, int* h) { *w = g_client_w; *h = g_client_h; }
#else
#include "../../linux/window_sdl.inl"
// The renderer starts on the thread that owns the window (SDL keeps a GL context on it).
void window_responsive(const std::function<void()>& work) { work(); }
// The warm-up room's two windows and the match window's view are Windows-only for now (the arena room's
// viewer window is): these stand in so the shared code links.
void* window_native() { return nullptr; }
void window_set_pip(int, bool) {}
void window_send_pip(void*, int, bool) {}
int window_pip_answer() { return -1; }
void* window_of_process(uint32_t) { return nullptr; }
void window_send_spectate_view(void*, int) {}
void window_report_spectate_view(int) {}
int window_spectate_choice() { return -1; }
void window_report_spectate_action(int) {}
int window_spectate_action() { return -1; }
void window_set_hot_rect(int, int, int, int) {}
int window_pip_mode() { return 0; }
bool window_hovered_recently() { return false; }
int window_take_clicks() { return 0; }
void window_report_score(uint64_t) {}
bool window_viewer_score(uint64_t*) { return false; }
bool window_moving() { return false; }
bool window_selected() { return false; }
void window_set_selected(bool) {}
void window_send_selected(void*, bool) {}
void window_note_picture_aspect(float) {}
float window_picture_aspect() { return 4.0f / 3.0f; }
#ifdef __APPLE__
const char* window_menu_key() { return yougame::stdio_bridge() ? yougame::parity::kMenuHint : "Esc"; }
#else
// The in-game menu's key, as the Windows window says it: F1 under YouGame, Esc standalone.
const char* window_menu_key() { return yougame::stdio_bridge() ? "F1" : "Esc"; }
#endif
#endif

bool window_input_captured() { return g_ui_capture.load(); }

// GameCube button bits (PADStatus.button)
enum : uint16_t {
  PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040,
  PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000,
};

KeyBindings g_key_bindings = default_key_bindings();
int g_stick_modifier_percent = 50;
std::array<PadBindings, 4> g_pad_bindings = default_pad_bindings();
std::array<GCBindings, 4> g_gc_bindings = default_gc_bindings();
std::array<PadBindings, 4> g_ds4_bindings = {};
// Unlike the DS4 table above, this one takes its defaults here rather than in window_create, so a
// binding read out of port-settings.ini (which is loaded before the window exists) is not written
// over by the defaults a moment later.
std::array<PadBindings, 4> g_swpro_bindings = default_swpro_bindings();
std::array<HidBindings, 4> g_hid_bindings = default_hid_bindings();
std::array<Deadzone, (size_t)PadFamily::Count> g_deadzones{};
std::array<PortSource, 4> g_port_sources = default_port_sources();
std::array<std::string, 4> g_port_device_names;
// The device that actually fed each port on the last poll. Usually its port source; but a port whose
// source is missing (say "GC Adapter 1" while a program like Delfinovin holds the adapter and offers
// it as an Xbox pad) falls back to the first free pad, and rumble has to go where the input came from.
std::array<PortSource, 4> g_port_feeding{};
// The last state the game actually read, for the on-screen controller overlay. Taken here rather
// than polled again by the renderer, so the overlay shows what the game saw and polling the devices
// stays on one thread at one rate.
std::mutex g_last_pads_mutex;
PadState g_last_pads[4]{};
std::atomic<uint16_t> g_test_hold{0};   // input_test_hold (tests only)
static void midtick_note_poll();   // the mid-tick test press (below, with input_changed_since_poll)

namespace {
bool g_capture_key_baseline[256]{};
unsigned short g_capture_pad_baseline[4]{};
PadState g_capture_gc_baseline[4]{};
uint16_t g_capture_ds4_baseline[4]{};
uint16_t g_capture_swpro_baseline[4]{};
uint32_t g_capture_hid_baseline[4]{};
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
    // The travel of L2 and R2 always reached the game as the analog triggers; now it says so.
    case BindAction::LAnalog: return DS4_L2;
    case BindAction::RAnalog: return DS4_R2;
    default: return 0;
  }
}

// Once: the settings file is read before the window exists, and its PlayStation buttons must not be
// replaced by the defaults when the window is made (a saved DualSense profile came back as the
// default layout after every restart).
void ds4_init_defaults() {
  static bool done = false;
  if (done) return;
  done = true;
  for (auto& bindings : g_ds4_bindings)
    for (int i = 0; i < (int)BindAction::Count; ++i) bindings.mask[i] = ds4_action_mask((BindAction)i);
}

int ds4_slot(HANDLE device) {
  std::lock_guard<std::mutex> lock(g_ds4_mutex);
  for (int i = 0; i < 4; ++i) if (g_ds4_devices[i] == device) return i;
  for (int i = 0; i < 4; ++i) if (!g_ds4_devices[i]) { g_ds4_devices[i] = device; return i; }
  return -1;
}

// Windows said a device is gone. A PlayStation pad's slot is emptied (it used to keep its last
// report, so a direction held while it was unplugged stayed held); the other readers do their own.
void input_device_removed(HANDLE device) {
  {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    for (int i = 0; i < 4; ++i)
      if (g_ds4_devices[i] == device) {
        g_ds4_devices[i] = nullptr; g_ds4_buttons[i] = 0; g_ds4_pads[i] = PadState{};
        log("input: PlayStation pad %d disconnected", i + 1);
      }
  }
  hidpad_device_removed(device);
}

bool ds4_device(HANDLE device, bool* dualsense) {
  RID_DEVICE_INFO info{}; info.cbSize = sizeof info;
  UINT size = sizeof info;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size) == (UINT)-1 || info.dwType != RIM_TYPEHID) return false;
  *dualsense = dualsense_pad(info.hid.dwVendorId, info.hid.dwProductId);
  return playstation_pad(info.hid.dwVendorId, info.hid.dwProductId);
}

#ifdef _WIN32
// The decoding lives in playstation_pad.cpp, where it is tested byte by byte (port/tests).
void ds4_report(RAWINPUT* input, bool dualsense) {
  int slot = ds4_slot(input->header.hDevice);
  if (slot < 0 || !input->data.hid.dwSizeHid || !input->data.hid.dwCount) return;
  PadState pad{};
  uint16_t buttons = 0;
  // The newest of the reports Raw Input packed into this message (oldest first) is the pad's state now.
  const uint8_t* newest = input->data.hid.bRawData + (size_t)(input->data.hid.dwCount - 1) * input->data.hid.dwSizeHid;
  if (!playstation_parse(newest, input->data.hid.dwSizeHid, dualsense, pad, buttons)) return;
  std::lock_guard<std::mutex> lock(g_ds4_mutex); g_ds4_buttons[slot] = buttons; g_ds4_pads[slot] = pad;
}

// One WM_INPUT report, handed to whichever device path recognises it. Devices we do not know about
// (an Xbox pad, which is read through XInput instead, or a flight stick on usage 0x04) fall out of
// both and cost only the fetch.
void raw_input(HRAWINPUT raw) {
  UINT size = 0;
  if (GetRawInputData(raw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 || !size) return;
  std::vector<uint8_t> bytes(size);
  if (GetRawInputData(raw, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
  RAWINPUT* input = reinterpret_cast<RAWINPUT*>(bytes.data());
  if (input->header.dwType != RIM_TYPEHID) return;
  if (bool dualsense = false; ds4_device(input->header.hDevice, &dualsense)) { ds4_report(input, dualsense); return; }
  if (switchpro_raw_input(input->header.hDevice, input->data.hid.bRawData,
                          input->data.hid.dwSizeHid, input->data.hid.dwCount)) return;
  // Anything else that calls itself a gamepad: a B0XX, a Frame1, a vJoy feeder, a third-party pad.
  // It goes last so a device with a reader of its own is never taken by the generic path.
  hidpad_raw_input(input->header.hDevice, input->data.hid.bRawData,
                   input->data.hid.dwSizeHid, input->data.hid.dwCount);
}
#else
// One /dev/hidraw report (Windows' layout, report ID in byte 0), to whichever reader recognises it,
// in the same order as raw_input above.
void hidraw_report(HANDLE device, const uint8_t* bytes, uint32_t size) {
  if (bool dualsense = false; ds4_device(device, &dualsense)) {
    const int slot = ds4_slot(device);
    if (slot < 0 || !size) return;
    PadState pad{};
    uint16_t buttons = 0;
    if (!playstation_parse(bytes, size, dualsense, pad, buttons)) return;
    std::lock_guard<std::mutex> lock(g_ds4_mutex); g_ds4_buttons[slot] = buttons; g_ds4_pads[slot] = pad;
    return;
  }
  if (switchpro_raw_input(device, bytes, size, 1)) return;
  hidpad_raw_input(device, bytes, size, 1);
}
void hidraw_start() { linux_hid::start(hidraw_report, input_device_removed); }
void hidraw_stop() { linux_hid::stop(); }
#endif
}  // namespace

static bool xinput_state(int idx, XINPUT_STATE* state);   // below, with the helper thread for empty slots
void ds4_bindings_defaults() { ds4_init_defaults(); }

namespace {
uint32_t relay_switch_poll(PadState out[4], uint16_t buttons[4]) {
  if (!controller_relay::active()) return switchpro_poll(out, buttons);
  uint32_t raw[4]{}; const auto mask=controller_relay::poll(CaptureDevice::SwitchPro,out,raw);
  for(int i=0;i<4;++i) buttons[i]=(uint16_t)raw[i]; return mask;
}
// YouGame: the app takes the HID family only once it has sent a raw HID pad (yg-pads kind "hid").
// Until then boxes and generic HID pads stay with this game's own reader, as before the app's pads.
bool relay_owns_hid() { return controller_relay::active() && yougame::relay_pads::owns_hid(); }
uint32_t relay_hid_poll(PadState out[4], uint32_t buttons[4]) {
  return relay_owns_hid() ? controller_relay::poll(CaptureDevice::HidPad,out,buttons) : hidpad_poll(out,buttons);
}
bool relay_ps(int index, PadState& pad, uint16_t& buttons) {
  controller_relay::Sample sample;
  if (!controller_relay::sample(CaptureDevice::DS4Pad,index,sample)) return false;
  pad=controller_relay::raw_pad(sample); buttons=(uint16_t)sample.buttons; return true;
}
}
namespace { CaptureDevice g_capture_want = CaptureDevice::None; int g_capture_want_index = 0; }
void input_begin_capture(CaptureDevice want, int want_index) {
  g_capture_want = want; g_capture_want_index = want_index;
  input_begin_capture();
}
void input_begin_capture() {
  { std::lock_guard<std::mutex> lock(g_keys_mutex); std::memcpy(g_capture_key_baseline, g_keys, sizeof g_keys); }
  // gcadapter_poll fills four pads. It used to be handed the address of a single PadState, once per
  // iteration, so with an adapter connected it wrote three pads past the end of a stack variable
  // every time the rebinding UI opened, and every port ended up with port 1's baseline. Poll once,
  // into four.
  PadState gc[4]{};
  for (int i = 0; i < 4; ++i) gc[i].err = -1;
  gcadapter_poll(gc);
  for (int idx = 0; idx < 4; ++idx) {
    XINPUT_STATE xs{};
    g_capture_pad_baseline[idx] = xinput_state(idx, &xs) ?
        xinput_binding_buttons(xs.Gamepad.wButtons, xs.Gamepad.bLeftTrigger,
                               xs.Gamepad.bRightTrigger, 140, 140) : 0;
    g_capture_gc_baseline[idx] = gc[idx];
  }
  { std::lock_guard<std::mutex> lock(g_ds4_mutex); for (int idx = 0; idx < 4; ++idx) { PadState raw{}; uint16_t buttons=0; g_capture_ds4_baseline[idx] = controller_relay::active() ? (relay_ps(idx,raw,buttons),buttons) : g_ds4_buttons[idx]; } }
  {
    PadState swpro[4]; for (auto& s : swpro) { s = {}; s.err = -1; }
    uint16_t buttons[4]{};
    relay_switch_poll(swpro, buttons);
    for (int idx = 0; idx < 4; ++idx) g_capture_swpro_baseline[idx] = buttons[idx];
  }
  {
    PadState hid[4]; for (auto& s : hid) { s = {}; s.err = -1; }
    uint32_t buttons[4]{};
    relay_hid_poll(hid, buttons);
    for (int idx = 0; idx < 4; ++idx) g_capture_hid_baseline[idx] = buttons[idx];
  }
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
    const bool want_keyboard = g_capture_want == CaptureDevice::None || g_capture_want == CaptureDevice::Keyboard;
    for (int vk = 0; vk < 256 && want_keyboard; ++vk) {
      if (vk == VK_ESCAPE) continue;
      if (g_keys[vk] && !g_capture_key_baseline[vk]
#ifdef _WIN32
          && !shortcut_swallowed(vk)
#endif
          ) {
        g_capturing.store(false); device = CaptureDevice::Keyboard; value = vk; device_index = 0; return true;
      }
    }
  }
  // Only controllers of the kind being rebound are listened to (see input_begin_capture), but any
  // one of them: a player does not know which adapter socket or pad number theirs is, and the
  // settings panel moves to whichever one answered. `wanted` with no device named keeps the old
  // behaviour of taking whatever fires first.
  auto wanted = [](CaptureDevice kind, int) {
    return g_capture_want == CaptureDevice::None || g_capture_want == kind;
  };
  for (int idx = 0; idx < 4; ++idx) {
    if (!wanted(CaptureDevice::XInputPad, idx)) continue;
    XINPUT_STATE xs{};
    if (!xinput_state(idx, &xs)) continue;
    const uint16_t buttons = xinput_binding_buttons(xs.Gamepad.wButtons, xs.Gamepad.bLeftTrigger,
                                                    xs.Gamepad.bRightTrigger, 140, 140);
    unsigned short newly = buttons & ~g_capture_pad_baseline[idx];
    if (newly) {
      unsigned short lowest = newly & (~(newly - 1));
      g_capturing.store(false); device = CaptureDevice::XInputPad; value = lowest; device_index = idx; return true;
    }
  }
  PadState gc[4];
  uint32_t mask = gcadapter_poll(gc);
  for (int idx = 0; idx < 4; ++idx) {
    if (!(mask & (1u << idx)) || !wanted(CaptureDevice::GCAdapter, idx)) continue;
    uint16_t newly = gc[idx].button & ~g_capture_gc_baseline[idx].button;
    // A trigger pressed most of the way counts, not only one clicked all the way in.
    constexpr int kTriggerPress = 140;
    if (gc[idx].trig_l >= kTriggerPress && g_capture_gc_baseline[idx].trig_l < kTriggerPress) newly |= PAD_L;
    if (gc[idx].trig_r >= kTriggerPress && g_capture_gc_baseline[idx].trig_r < kTriggerPress) newly |= PAD_R;
    if (newly) {
      unsigned short lowest = newly & (~(newly - 1));
      g_capturing.store(false); device = CaptureDevice::GCAdapter; value = lowest; device_index = idx; return true;
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    for (int idx = 0; idx < 4; ++idx) {
      if (!wanted(CaptureDevice::DS4Pad, idx)) continue;
      PadState raw{}; uint16_t buttons=g_ds4_buttons[idx];
      if (controller_relay::active() && !relay_ps(idx,raw,buttons)) continue;
      uint16_t newly = buttons & ~g_capture_ds4_baseline[idx];
      if (newly) {
        uint16_t lowest = newly & (uint16_t)(~(newly - 1));
        g_capturing.store(false); device = CaptureDevice::DS4Pad; value = lowest; device_index = idx; return true;
      }
    }
  }
  {
    PadState swpro[4]; for (auto& s : swpro) { s = {}; s.err = -1; }
    uint16_t buttons[4]{};
    uint32_t mask = relay_switch_poll(swpro, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(mask & (1u << idx)) || !wanted(CaptureDevice::SwitchPro, idx)) continue;
      uint16_t newly = buttons[idx] & ~g_capture_swpro_baseline[idx];
      if (newly) {
        uint16_t lowest = newly & (uint16_t)(~(newly - 1));
        g_capturing.store(false); device = CaptureDevice::SwitchPro; value = lowest; device_index = idx; return true;
      }
    }
  }
  {
    PadState hid[4]; for (auto& s : hid) { s = {}; s.err = -1; }
    uint32_t buttons[4]{};
    uint32_t mask = relay_hid_poll(hid, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(mask & (1u << idx)) || !wanted(CaptureDevice::HidPad, idx)) continue;
      // Bit 31 is the hat's Left (HID_HAT_LEFT), so it must be capturable: leaving it out made
      // D-pad Left unbindable on every box controller that reports its D-pad as a hat. The result
      // travels as an int and is negative for that bit; binding_set takes it back as uint32_t.
      uint32_t newly = buttons[idx] & ~g_capture_hid_baseline[idx];
      if (newly) {
        uint32_t lowest = newly & ~(newly - 1);
        g_capturing.store(false); device = CaptureDevice::HidPad; value = (int)lowest; device_index = idx; return true;
      }
    }
  }
  return false;
}

namespace {
// tl/tr are the analog triggers. Melee shields from the analog value, not the digital L/R bit, so a
// script that only pressed L never actually shielded and shield behaviour could not be tested at all.
struct ScriptEntry {
  uint32_t frame;
  uint16_t buttons;
  int8_t sx, sy, cx, cy;
  uint8_t tl, tr;
  int port;
  bool relative = false;
  bool scene_match_relative = false;
  bool match_retrace_relative = false;
};
std::vector<ScriptEntry> g_script;
uint32_t g_script_ports = 1;
// `@match` makes later entries relative to the retrace at which an online match reached frame 1
// (they stay silent until then); `@loop N` repeats the relative section every N frames.
static bool g_script_relative_section = false;
static uint32_t g_script_loop = 0;
static uint32_t g_script_release_after = 0, g_script_release_at = 0;
static std::atomic<uint32_t> g_match_start_retrace{0};
// `@scene <major>[:<minor>]` starts a menu-relative section at the first matching scene. A later
// `@match` in the same script switches subsequent entries to match-frame-relative timing, leaving
// the menu entries on scene retraces and keeping in-match inputs silent until frame 1.
static bool g_script_has_scene_wait = false;
static bool g_script_scene_match_relative = false;
static uint32_t g_scene_wait_major = 0, g_scene_wait_minor = 0;
static bool g_scene_wait_has_minor = false;
static std::atomic<uint32_t> g_scene_start_retrace{0};   // 0 = not observed yet
// `@matchrt` (after `@scene`): later entries count retraces from the start of each match instead
// of match frames, so they keep advancing while the game is paused (the match frame stops there).
// A match frame lower than the last one seen marks a new match and restarts the count.
static bool g_script_match_retrace = false;
static uint32_t g_match_rt_start = 0, g_match_rt_last = 0;

// Turns a Switch controller's raw SWPRO_* bits into GameCube buttons through slot `idx`'s
// remappable table, and returns the same thing as BindAction bit indices for the settings panel.
// The L/R shoulders are digital on this pad, so a press bottoms the analog trigger out the way a
// digital press does on hardware: Melee shields from the trigger value, not from the L/R bit.
// Thirty-two bits back: the two travel actions sit above bit 16.
uint32_t hid_apply_bindings(int idx, uint32_t buttons, PadState& pad, box::State& box_state) {
  const HidBindings& bind = g_hid_bindings[idx];
  // The device's own trigger axes, when it has them, are already in trig_l/trig_r and pass through
  // (they have no source bit to bind); a button on L analog or R analog presses on top of them.
  const uint32_t actions = button_apply_bindings(pad, pad.trig_l, pad.trig_r,
      [&](int i) { return (bind.mask[i] & buttons) != 0; }, [&](int i) { return (int)bind.level[i]; });
  // A box has no analog triggers: its L and R are buttons, and the game needs a full press to see
  // a shield. Light shield on such a device is a firmware feature, reported on an analog axis when
  // the device has one.
  if (pad.button & kActionPadBit[(size_t)BindAction::L] && !pad.trig_l) pad.trig_l = 255;
  if (pad.button & kActionPadBit[(size_t)BindAction::R] && !pad.trig_r) pad.trig_r = 255;
  // Stick directions bound to buttons: with Mod X or Mod Y bound the box coordinates, otherwise full
  // pushes, as on the keyboard. They were saved but never applied here, so a box sending its stick
  // as buttons moved nothing. A device's own analog stick is left alone while none is held.
  if (!apply_box_actions(bind.mask[(size_t)BindAction::ModX] || bind.mask[(size_t)BindAction::ModY], actions, pad, box_state))
    apply_stick_actions(actions, pad.stick_x, pad.stick_y, g_stick_modifier_percent);
  return actions;
}

uint32_t swpro_apply_bindings(int idx, uint16_t buttons, PadState& pad) {
  // A Pro Controller has no analog triggers and arrives with both at 0; the Switch Online GameCube
  // controller's are already in trig_l/trig_r and pass through.
  const PadBindings& bind = g_swpro_bindings[idx];
  const uint32_t actions = button_apply_bindings(pad, pad.trig_l, pad.trig_r,
      [&](int i) { return (bind.mask[i] & buttons) != 0; }, [&](int i) { return (int)bind.level[i]; });
  if (pad.button & kActionPadBit[(size_t)BindAction::L]) pad.trig_l = 255;
  if (pad.button & kActionPadBit[(size_t)BindAction::R]) pad.trig_r = 255;
  return actions;
}

// A box whose layout is known gets it the first time it appears in a slot, as long as that slot
// still has the generic defaults (anything the player changed is kept). vJoy: the b0xx-ahk layout,
// which most B0XX-on-vJoy setups use. "Arduino Leonardo": a HayBox box in DInput mode.
void apply_known_box_layout(int idx) {
  static std::string seen[4];
  const std::string name = hidpad_name(idx);
  if (name == seen[idx]) return;
  seen[idx] = name;
  std::string lower = name;
  for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
  const HidBindings generic = default_hid_bindings()[0], before_hat = hid_bindings_before_hat();
  // A B0XX on firmware 4.x that an earlier version gave HayBox's layout (A and B swapped), and that the
  // player never changed, takes its own.
  const HidBindings haybox = haybox_dinput_bindings();
  if (hidpad_b0xx_firmware4(idx) && std::memcmp(&g_hid_bindings[idx], &haybox, sizeof haybox) == 0) {
    g_hid_bindings[idx] = b0xx_firmware4_bindings();
    host::log("hid pad %d: %s, a B0XX on firmware 4.x: its own layout (was HayBox's)", idx + 1, name.c_str());
    return;
  }
  if (std::memcmp(&g_hid_bindings[idx], &generic, sizeof generic) != 0 &&
      std::memcmp(&g_hid_bindings[idx], &before_hat, sizeof before_hat) != 0) return;
  if (hidpad_b0xx_firmware4(idx)) {
    g_hid_bindings[idx] = b0xx_firmware4_bindings();
    host::log("hid pad %d: %s, using the B0XX firmware 4.x layout", idx + 1, name.c_str());
  } else if (lower.find("vjoy") != std::string::npos) {
    g_hid_bindings[idx] = vjoy_b0xx_bindings();
    host::log("hid pad %d: %s, using the B0XX (vJoy) layout", idx + 1, name.c_str());
  } else if (hidpad_frame1_dinput(idx)) {
    g_hid_bindings[idx] = frame1_dinput_bindings();
    host::log("hid pad %d: %s, using the Frame1 DInput layout", idx + 1, name.c_str());
  } else if (lower.find("leonardo") != std::string::npos || lower.find("haybox") != std::string::npos ||
             hidpad_haybox_dinput(idx)) {
    // By name for the Arduino-based boxes; by descriptor for the Pico-based ones (a Frame1, say),
    // which carry no name of their own.
    g_hid_bindings[idx] = haybox_dinput_bindings();
    host::log("hid pad %d: %s, using the B0XX-layout box (HayBox DInput) layout", idx + 1, name.c_str());
  } else if (std::memcmp(&g_hid_bindings[idx], &before_hat, sizeof before_hat) == 0) {
    // Saved by a version whose defaults left the D-pad unbound, and never changed since.
    g_hid_bindings[idx] = generic;
  }
}

// The binding tables themselves are applied in input_bindings.h (apply_actions and the per-family
// functions beside it), where the unit tests reach them. Both callers of each (input_poll and
// input_debug_snapshot) go through the same function, so the game and the Controls tab agree.

// Box SOCD history must be independent for game polling, Controls preview and UI navigation.
box::State g_box_keyboard[2];
box::State g_box_keyboard_navigation;
box::State g_box_hid[2][4];

// The snapback filter (input_bindings.h snapback_axis) on an Xbox, PlayStation or Switch controller's
// control stick, before deadzone and radius (GitHub #72). [0] the game's read, [1] the Controls tab's
// preview, each with its own state.
SnapbackAxis g_snapback[2][(size_t)PadFamily::Count][4][2];
void apply_snapback(PadFamily family, int slot, int lane, PadState& pad) {
  if (family == PadFamily::GameCube || family == PadFamily::Box || slot < 0 || slot >= 4) return;
  const Deadzone& dz = g_deadzones[(size_t)family];
  SnapbackAxis* axes = g_snapback[lane & 1][(size_t)family][slot];
  const double now = now_seconds();
  pad.stick_x = snapback_axis(axes[0], pad.stick_x, now, dz.snapback_x, dz.snapback_ms_x);
  pad.stick_y = snapback_axis(axes[1], pad.stick_y, now, dz.snapback_y, dz.snapback_ms_y);
}

void apply_family_options(PadFamily family, PadState& pad, int slot = -1, int lane = 0) {
  apply_snapback(family, slot, lane, pad);
  const Deadzone& dz = g_deadzones[(size_t)family];
  apply_deadzone(dz, pad.stick_x, pad.stick_y, false);
  apply_deadzone(dz, pad.sub_x, pad.sub_y, true);
  apply_stick_radius(dz, pad.stick_x, pad.stick_y, false);
  apply_stick_radius(dz, pad.sub_x, pad.sub_y, true);
  // Native physical click switches retain their defaults. A configured threshold
  // overrides that click; XInput's default is synthesized while reading its axes.
  if (family != PadFamily::Xbox && dz.click_l != kDefaultTriggerClick) {
    pad.button &= (uint16_t)~PAD_L;
    apply_trigger_click(dz.click_l, pad.trig_l, pad.button, PAD_L);
  }
  if (family != PadFamily::Xbox && dz.click_r != kDefaultTriggerClick) {
    pad.button &= (uint16_t)~PAD_R;
    apply_trigger_click(dz.click_r, pad.trig_r, pad.button, PAD_R);
  }
  if (family == PadFamily::PlayStation) apply_playstation_default_click(dz, pad);
  apply_trigger_cap(dz.trig_l, pad.trig_l, pad.button, PAD_L);
  apply_trigger_cap(dz.trig_r, pad.trig_r, pad.button, PAD_R);
}
}  // namespace
static void g_pad_frame_reset();   // defined with g_pad_frame below (outside the anonymous namespace)
void input_mark_match_start() { g_match_start_retrace.store(retrace_count()); g_pad_frame_reset(); }

#ifdef YOUGAME_AGENT_TEST_HOOK
static bool g_console_script_profile = false;
#endif
bool input_load_script(const char* path) {
#ifdef YOUGAME_AGENT_TEST_HOOK
  const bool secured = yougame::agent_paths::same(path, yougame::agent_paths::policy.script);
  std::string snapshot;
  size_t offset = 0;
  if (secured && !yougame::agent_paths::script_bytes(path, snapshot)) return false;
  FILE* f = secured ? nullptr : fopen(path, "r");
  if (!secured && !f) return false;
#else
  FILE* f = fopen(path, "r");
  if (!f) return false;
#endif
#ifdef YOUGAME_AGENT_TEST_HOOK
  g_console_script_profile = secured;
#endif
  g_script.clear();
  g_script_ports = 1;
  g_script_relative_section = false;
  g_script_loop = 0;
  g_match_start_retrace.store(0);
  g_script_has_scene_wait = false;
  g_script_scene_match_relative = false;
  g_script_match_retrace = false;
  g_scene_wait_major = g_scene_wait_minor = 0;
  g_scene_wait_has_minor = false;
  g_scene_start_retrace.store(0);
  g_match_rt_start = g_match_rt_last = 0;
  g_script_release_after = 0; g_script_release_at = 0;
  char line[256];
  auto next_line = [&] {
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (secured) return yougame::agent_paths::line(snapshot, offset, line, sizeof line);
#endif
    return fgets(line, sizeof line, f) != nullptr;
  };
  while (next_line()) {
    ScriptEntry e{};
    char* p = line;
    if (*p == '#' || *p == '\n' || *p == '\r') continue;
    // `@release N`: N retraces after a match is first seen in progress, the script ends and the
    // real controllers drive every port (a scripted boot into a match, then a person plays).
    if (!strncmp(p, "@release", 8)) { g_script_release_after = (uint32_t)strtoul(p + 8, nullptr, 10); continue; }
    if (!strncmp(p, "@matchrt", 8)) {
      g_script_relative_section = true;
      g_script_scene_match_relative = false;
      g_script_match_retrace = g_script_has_scene_wait;
      continue;
    }
    if (!strncmp(p, "@match", 6)) {
      g_script_relative_section = true;
      g_script_scene_match_relative = g_script_has_scene_wait;
      g_script_match_retrace = false;
      continue;
    }
    if (!strncmp(p, "@loop", 5)) { g_script_loop = (uint32_t)strtoul(p + 5, nullptr, 10); continue; }
    if (!strncmp(p, "@scene", 6)) {
      char* q = p + 6;
      while (*q == ' ' || *q == '\t') ++q;
      g_scene_wait_major = (uint32_t)strtoul(q, &q, 0);
      g_scene_wait_has_minor = (*q == ':');
      if (g_scene_wait_has_minor) g_scene_wait_minor = (uint32_t)strtoul(q + 1, nullptr, 0);
      g_script_has_scene_wait = true;
      g_script_scene_match_relative = false;
      g_script_match_retrace = false;
      g_script_relative_section = true;
      continue;
    }
    e.relative = g_script_relative_section;
    e.scene_match_relative = g_script_scene_match_relative;
    e.match_retrace_relative = g_script_match_retrace;
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
      else if (!strncmp(tok, "l=", 2)) e.tl = (uint8_t)std::min(255, std::max(0, atoi(tok + 2)));
      else if (!strncmp(tok, "r=", 2)) e.tr = (uint8_t)std::min(255, std::max(0, atoi(tok + 2)));
      else if (!strcmp(tok, "DU")) e.buttons |= PAD_UP; else if (!strcmp(tok, "DD")) e.buttons |= PAD_DOWN;
      else if (!strcmp(tok, "DL")) e.buttons |= PAD_LEFT; else if (!strcmp(tok, "DR")) e.buttons |= PAD_RIGHT;
      else if (!strncmp(tok, "sx=", 3)) e.sx = (int8_t)atoi(tok + 3); else if (!strncmp(tok, "sy=", 3)) e.sy = (int8_t)atoi(tok + 3);
      else if (!strncmp(tok, "cx=", 3)) e.cx = (int8_t)atoi(tok + 3); else if (!strncmp(tok, "cy=", 3)) e.cy = (int8_t)atoi(tok + 3);
      else if (!strncmp(tok, "p=", 2)) { e.port = atoi(tok + 2) - 1; if (e.port < 0 || e.port > 5) e.port = 0; g_script_ports |= 1u << e.port; }   // p=5, p=6: Triples' pads 5 and 6
    }
    g_script.push_back(e);
  }
  if (f) fclose(f);
  return !g_script.empty();
}

// XInputGetState for one slot, without asking Windows about an empty slot on the read: on some
// systems that question takes milliseconds (it enumerates devices), and it used to be asked for
// all four slots on every controller read.
// The empty slots are asked about on a helper thread, never on the thread that runs the game: four
// such questions on every controller read is 20 ms of each 16.7 ms frame on a system where one
// takes 5 ms, and the game then runs in slow motion with crackling sound for a keyboard player.
// MELEE_TEST_XINPUT_SLOW_MS=<n> (tests) makes every question about an empty slot take n ms;
// MELEE_TEST_XINPUT_EVERY_READ=1 (tests) asks on the reading thread on every read, as before.
static std::atomic<bool> g_xinput_present[4];
// Per XInput slot: whether the pad sends a Frame1's grid (input_bindings.h, box_xinput_observe).
// Advanced by the game's read only; the controls screen reads the copy.
static BoxXInputDetect g_box_xinput[4];
static std::atomic<int> g_box_xinput_state[4];
// MELEE_TEST_XINPUT_SCRIPT=<file> (hidden tests): XInput slot 1 plays raw reports from the file
// instead of asking Windows, so a pad's real XInput reports (a Frame1's grid values, an Xbox pad's
// arbitrary ones) go through the real read path; slots 2 to 4 read as empty. Lines:
//   <frame> <buttons hex> <LT> <RT> <LX> <LY> <RX> <RY>
// frame in retraces from the input script's release (`@release N`), or from boot when the script
// has no @release (or there is none), each report held until the next; before the first line the
// pad rests at zero. '#' starts a comment. An optional ninth number puts that line on XInput slot 2
// to 4 instead; a slot named by any line reads as plugged in (two pads with no adapter, say).
struct XInputFeedRow { uint32_t frame; XINPUT_GAMEPAD pad; int slot = 0; };
static unsigned g_xinput_feed_slots = 1;   // bit per slot the feed plugs in
static const std::vector<XInputFeedRow>* xinput_feed() {
  static const std::vector<XInputFeedRow>* feed = [] () -> const std::vector<XInputFeedRow>* {
    const char* path = host::test_env::getenv("MELEE_TEST_XINPUT_SCRIPT");
    if (!path || !*path) return nullptr;
    auto* rows = new std::vector<XInputFeedRow>();
    if (FILE* f = fopen(path, "r")) {
      char line[256];
      while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        unsigned fr = 0, buttons = 0; int lt = 0, rt = 0, lx = 0, ly = 0, rx = 0, ry = 0, slot = 1;
        const int fields = sscanf(line, "%u %x %d %d %d %d %d %d %d", &fr, &buttons, &lt, &rt, &lx, &ly, &rx, &ry, &slot);
        if (fields != 8 && fields != 9) continue;
        XInputFeedRow r{fr, {}, std::clamp(slot, 1, 4) - 1};
        g_xinput_feed_slots |= 1u << r.slot;
        r.pad.wButtons = (WORD)buttons; r.pad.bLeftTrigger = (BYTE)lt; r.pad.bRightTrigger = (BYTE)rt;
        r.pad.sThumbLX = (short)lx; r.pad.sThumbLY = (short)ly; r.pad.sThumbRX = (short)rx; r.pad.sThumbRY = (short)ry;
        rows->push_back(r);
      }
      fclose(f);
    }
    log("input: test XInput feed %s, %zu reports, slots mask %x", path, rows->size(), g_xinput_feed_slots);
    return rows;
  }();
  return feed;
}
static DWORD xinput_ask(int idx, XINPUT_STATE* state) {
  if (const auto* feed = xinput_feed()) {
    if (idx < 0 || idx >= 4 || !(g_xinput_feed_slots & (1u << idx))) return ERROR_DEVICE_NOT_CONNECTED;
    const uint32_t now = retrace_count();
    const uint32_t start = g_script_release_after ? g_script_release_at : 1;
    *state = {};
    if (start && now >= start) {
      const uint32_t rel = now - start;
      for (const XInputFeedRow& r : *feed) { if (r.slot != idx) continue; if (r.frame > rel) break; state->Gamepad = r.pad; }
      state->dwPacketNumber = rel;
    }
    return ERROR_SUCCESS;
  }
  // The app's provider owns the Xbox family once installed (xinput_state): no native question, and
  // no raw relay seat either, which would hand a PlayStation or Switch pad to the Xbox family too.
  if (controller_relay::active()) return ERROR_DEVICE_NOT_CONNECTED;
  static const int slow_ms = [] { const char* v = host::test_env::getenv("MELEE_TEST_XINPUT_SLOW_MS"); return v ? std::atoi(v) : 0; }();
  const DWORD result = yougame::relay_pads::poll(idx, state, [&] {
    return XInputGetState((DWORD)idx, state) == ERROR_SUCCESS;
  }) ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
  if (result != ERROR_SUCCESS && slow_ms > 0) {
    perf::Scope wait_cost(perf::Stage::Wait);
    for (const double until = now_seconds() + slow_ms / 1000.0; now_seconds() < until;) YieldProcessor();
  }
  return result;
}
static void xinput_probe_start() {
  if (yougame::relay_pads::active()) return;
  if (controller_relay::active()) return;
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      bool reported = false;
      for (;;) {
        for (int idx = 0; idx < 4; ++idx) {
          if (yougame::relay_pads::active()) return;
          if (controller_relay::active()) return;
          if (g_xinput_present[idx].load(std::memory_order_relaxed)) continue;
          XINPUT_STATE state{};
          const double before = now_seconds();
          const bool present = xinput_ask(idx, &state) == ERROR_SUCCESS;
          const double took = now_seconds() - before;
          if (present) g_xinput_present[idx].store(true, std::memory_order_relaxed);
          else if (took > 0.001 && !reported) {
            reported = true;
            log("input: Windows takes %.1f ms to answer for an empty controller slot on this system; the game thread does not ask", took * 1000.0);
          }
        }
        Sleep(1000);
      }
    }).detach();
  });
}
static bool xinput_state(int idx, XINPUT_STATE* state) {
  // The app's provider first: it puts each relayed pad in its own family (yougame_controller_relay.h).
  // The raw relay seats read every relayed pad as an Xbox pad, so they answer only without it.
  if (controller_relay::active()) {
    *state={}; controller_relay::Sample s;
    if (!controller_relay::sample(CaptureDevice::XInputPad,idx,s)) return false;
    auto& g=state->Gamepad; g.wButtons=(uint16_t)s.buttons; g.bLeftTrigger=s.left; g.bRightTrigger=s.right;
    g.sThumbLX=s.axes[0]; g.sThumbLY=s.axes[1]; g.sThumbRX=s.axes[2]; g.sThumbRY=s.axes[3]; return true;
  }
  if (yougame::relay_pads::active()) return yougame::relay_pads::read(idx, state);
  static const bool every_read = [] { const char* v = host::test_env::getenv("MELEE_TEST_XINPUT_EVERY_READ"); return v && *v == '1'; }();
  if (every_read) return xinput_ask(idx, state) == ERROR_SUCCESS;
  xinput_probe_start();
  if (!g_xinput_present[idx].load(std::memory_order_relaxed)) return false;
  if (xinput_ask(idx, state) == ERROR_SUCCESS) return true;
  g_xinput_present[idx].store(false, std::memory_order_relaxed);   // unplugged: back to the helper thread
  return false;
}

// The pads an input script delivers at retrace `frame`: port 1 by default, entries with p=N drive
// port N (a port with any entry counts as plugged in). update_state: this is the game's own poll,
// which advances the scene-wait and match-relative bookkeeping; the early-input sampler asks with
// false to see the next retrace's pads without moving anything.
// MELEE_TEST_SCRIPT_PAD_FRAMES=1 (online measurements): a plain @match section counts online game
// frames (the frame whose pad this poll reads, independent of the input delay) instead of retraces. A stalled frame
// polls again without sending a pad, so with retraces every input after a stall landed on a
// different game frame and two runs of the same script played different matches.
static std::atomic<int32_t> g_pad_frame{0};
void input_note_pad_frame(int32_t frame) { g_pad_frame.store(frame, std::memory_order_relaxed); }
static void g_pad_frame_reset() { g_pad_frame.store(0, std::memory_order_relaxed); }
static bool script_pad_frames() {
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_TEST_SCRIPT_PAD_FRAMES"); return v && *v == '1'; }();
  return on;
}

#ifdef YOUGAME_AGENT_TEST_HOOK
static bool agent_input_allowed(yougame::agent_test::InputKey key) {
  return yougame::agent_test::input_policy.allowed(
      yougame::agent_test::live_input_state(slippi::online::session_mode()), key,
      [](const char* line) { log("%s", line); });
}
#endif

// Latency probe (MELEE_TRACE_LATENCY=1 with --script): each button the script turns on, on any
// port, is a press. Numbered and stamped into this tick (tick_timing.h); the presentation thread
// logs one "latency probe:" line per press (threaded_backend.cpp). Game thread only.
static void note_script_presses(const PadState out[4]) {
  constexpr uint16_t kButtons = PAD_LEFT | PAD_RIGHT | PAD_DOWN | PAD_UP | PAD_Z | PAD_R | PAD_L |
                                PAD_A | PAD_B | PAD_X | PAD_Y | PAD_START;
  static uint16_t held[4] = {};
  static unsigned presses = 0;
  bool pressed = false;
  for (int port = 0; port < 4; ++port) {
    const uint16_t now = out[port].err == 0 ? (uint16_t)(out[port].button & kButtons) : 0;
    if (now & ~held[port]) pressed = true;
    held[port] = now;
  }
  if (!pressed) return;
  TickTiming& tick = tick_timing();
  tick.probe = ++presses;
  tick.probe_inject = tick.target > 0 ? tick.target : tick.wake > 0 ? tick.wake : tick.pad;
  tick.probe_pad = tick.pad;
}

// first/count: the ports filled, out[0] being port `first` (input_poll_extra asks for 4 and 5, Triples).
static void script_pads(uint32_t frame, bool update_state, PadState out[], int first = 0, int count = 4) {
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!agent_input_allowed(yougame::agent_test::InputKey::Script)) return;
#endif
  // @scene takes over the same "relative section" that @match uses, but starts counting from
  // the retrace where the requested mode/scene was first observed instead of an online match
  // reaching frame 1. Checked every poll (once per retrace) so the wait is not sensitive to
  // when input_poll happens to be called relative to the scene actually changing.
  uint32_t match_frame = 0;
  if (g_script_has_scene_wait) {
    uint32_t major, minor;
    current_scene(&major, &minor, &match_frame);
    if (!g_scene_start_retrace.load() &&
        major == g_scene_wait_major &&
        (!g_scene_wait_has_minor || minor == g_scene_wait_minor))
      if (update_state) g_scene_start_retrace.store(frame);
  }
  uint32_t start = g_script_has_scene_wait ? g_scene_start_retrace.load() : g_match_start_retrace.load();
  bool use_match_frame = g_script_has_scene_wait && start && match_frame != 0;
  if (update_state) {
    if (match_frame == 0) {
      g_match_rt_start = 0;
    } else if (!g_match_rt_start || match_frame < g_match_rt_last) {
      g_match_rt_start = frame;
    }
    g_match_rt_last = match_frame;
  }
  const uint32_t match_rt = g_match_rt_start ? frame - g_match_rt_start : 0;
  bool in_section = start && frame >= start;
  uint32_t scene_rel = in_section ? frame - start : 0;
  uint32_t rel = use_match_frame ? match_frame : (in_section ? frame - start : 0);
  if (in_section && !g_script_has_scene_wait && script_pad_frames() && g_pad_frame.load(std::memory_order_relaxed) > 0)
    rel = (uint32_t)g_pad_frame.load(std::memory_order_relaxed) + 1;   // the game frame this poll is for
  if (in_section && g_script_loop && (!g_script_has_scene_wait || use_match_frame)) rel %= g_script_loop;
  for (int slot = 0; slot < count; ++slot) {
    const int port = first + slot;
    if (port && !(g_script_ports & (1u << port))) continue;
    out[slot].err = 0;
    const ScriptEntry* cur = nullptr;
    for (const ScriptEntry& e : g_script) {
      if (e.port != port) continue;
      if (e.relative) {
        if (e.match_retrace_relative) {
          if (g_match_rt_start && e.frame <= match_rt) cur = &e;
        } else if (e.scene_match_relative) {
          if (use_match_frame && e.frame <= rel) cur = &e;
        } else if (g_script_has_scene_wait) {
          if (in_section && e.frame <= scene_rel) cur = &e;
        } else if (in_section && e.frame <= rel) {
          cur = &e;
        }
      } else if (!in_section && e.frame <= frame) cur = &e;
    }
    if (cur) {
      PadState& q = out[slot];
      q.button = cur->buttons; q.stick_x = cur->sx; q.stick_y = cur->sy; q.sub_x = cur->cx; q.sub_y = cur->cy;
      // A digital L/R press on hardware bottoms the trigger out, so mirror that when the script
      // did not ask for a specific analog value (light shield needs the explicit l=/r= token).
      q.trig_l = cur->tl ? cur->tl : (uint8_t)((cur->buttons & PAD_L) ? 255 : 0);
      q.trig_r = cur->tr ? cur->tr : (uint8_t)((cur->buttons & PAD_R) ? 255 : 0);
    }
  }
}

// Every physical source is read, then each in-game port takes whichever device g_port_sources[port]
// assigns it (keyboard, an Xbox pad and the GC adapter can drive different ports at once).
// side_effects: the game's own poll, which may re-point a port at a renamed HID slot and records
// what feeds each port; the early-input sampler reads with false, under the same lock.
static std::mutex g_devices_mutex;
// Triples (MuHostApi.pad_read_extra): pads 5 and 6 as the game's last poll left them, game thread only.
static PadState g_extra_pads[2] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, -1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, -1}};
static PortSource g_extra_feeding[2] = {{DeviceKind::None, -1}, {DeviceKind::None, -1}};   // what feeds pads 5 and 6 (rumble)
// extra: when given, pads 5 and 6 (Triples): the next connected controllers that no port 1-4 is set to.
static void read_devices(PadState out[4], InputDebugSnapshot& debug, bool side_effects,
                         PadState* keyboard_navigation = nullptr, bool console = false, PadState* extra = nullptr) {
  // Poll every physical source unconditionally, then route each in-game port to
  // whichever device g_port_sources[port] assigns it to. This lets keyboard, an
  // Xbox pad, and the GC adapter all drive different ports at the same time.
  PadState gc[4];
  for (auto& s : gc) { s = {}; s.err = -1; }
  uint32_t gc_mask = gcadapter_poll(gc);
  debug.gc_mask = gc_mask;

  // Whether the game window has focus: without it, input follows the Background input option.
  const bool focused = g_hwnd && window_focused(g_hwnd);
  PadState kb{}; kb.err = 0;
  uint32_t keyboard_actions = 0;
  {
    // Keyboard: every action, the control stick and C-stick included, comes from g_key_bindings.
    // The stick used to be hard wired to the arrow keys here while everything else was a setting,
    // which is the one thing players could not rebind; the arrows are now just its defaults.
    // The keyboard only counts while the game window has focus. Background input is for
    // controllers: reading the keyboard globally made typing in any other window play the game.
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    // Background input is for controllers: keys typed into another window (chat, a browser) must not
    // reach the game, so the keyboard is only read while the game window has focus.
    // The keys are read as they are now, at the pad read. The window messages are handled by the
    // presentation thread between its frames, up to 2 ms after the press, and a press made in that
    // gap missed the tick. The message rules still hold: Tab stays reserved for matchmaking, and
    // with Alt held the messages decide, since those keys arrive as system keys and never played.
    // In the player box the keys come from the window's messages only: the foreground window is
    // the app's, and a sandboxed program cannot read the keyboard while another program is in front.
    const bool live = focused && !yougame::embedded() && !(GetAsyncKeyState(VK_MENU) & 0x8000);
    // Shift+Tab, Shift+Enter, Shift+Backspace and Shift+Escape belong to YouGame (its menu, chats and
    // back), in the app and in its mini window. The mini takes them with a hotkey, which hides the
    // key's message but not its state, so the state read below still saw Enter go down and the menu
    // chose a row. A key pressed while Shift is held is not the game's until it is let go, even when
    // Shift is let go first.
    static bool s_chord_down[256], s_chord_held[256];
    auto youGame_chord = [&](int vk) {
      if (vk != VK_TAB && vk != VK_RETURN && vk != VK_BACK && vk != VK_ESCAPE) return false;
      const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0 || g_keys[vk];
      if (!down) s_chord_held[vk] = false;
      else if (!s_chord_down[vk] && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) s_chord_held[vk] = true;
      s_chord_down[vk] = down;
      return s_chord_held[vk];
    };
    auto key = [&](int vk) {
      vk &= 0xFF;
      if (!focused) return false;
#ifdef _WIN32
      if (shortcut_swallowed(vk)) return false;
      if (youGame_chord(vk)) return false;
#endif
      if (live && vk >= 8 && vk != VK_TAB) return (GetAsyncKeyState(vk) & 0x8000) != 0;
      return g_keys[vk];
    };
    keyboard_actions = keyboard_apply_bindings(kb, g_key_bindings, key, g_box_keyboard[0], g_stick_modifier_percent);
    if (keyboard_navigation) {
      *keyboard_navigation = {};
      const bool shift_only = key(VK_SHIFT) && !key(VK_CONTROL) && !key(VK_MENU) &&
                              !key(VK_LWIN) && !key(VK_RWIN);
      keyboard_apply_bindings(*keyboard_navigation, g_key_bindings,
          [&](int vk) { return !imgui_keyboard_navigation_key(vk, shift_only) && key(vk); },
          g_box_keyboard_navigation, g_stick_modifier_percent);
    }
  }

  PadState xin[4];
  bool xin_connected[4] = {};
  for (int idx = 0; idx < 4; ++idx) {
    PadState& x = xin[idx]; x = {}; x.err = -1;
    XINPUT_STATE xs{};
    if (!xinput_state(idx, &xs)) { g_box_xinput[idx] = {}; g_box_xinput_state[idx].store(0, std::memory_order_relaxed); continue; }
    xin_connected[idx] = true;
    debug.xinput_connected[idx] = true;
    x.err = 0;
    auto& g = xs.Gamepad;
    // A Frame1 in XInput mode sends GameCube units on its own grid (box_xinput_observe).
    const int16_t raw_axes[4] = {g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
    const int8_t was_box = g_box_xinput[idx].state;
    box_xinput_observe(g_box_xinput[idx], raw_axes);
    const BoxXInputDetect det = g_box_xinput[idx];
    g_box_xinput_state[idx].store((int)(uint8_t)det.state | ((int)(uint8_t)det.first << 8), std::memory_order_relaxed);
    if (det.state > 0 && was_box <= 0)
      host::log("xinput pad %d: a box in XInput mode (Frame1): its sticks and triggers read in GameCube units", idx);
    if (det.state > 0) { g.bLeftTrigger = box_xinput_trigger(g.bLeftTrigger); g.bRightTrigger = box_xinput_trigger(g.bRightTrigger); }
    auto axis = [&det](SHORT v) { return (int)box_xinput_axis(det, v); };
    // No deadzone and no trigger floor, as in Dolphin. These used Microsoft's recommended Xbox
    // deadzone, which zeroed both axes whenever both sat inside about 30 of Melee's 80 units: larger
    // than the game's own deadzone, and exactly where a box controller in XInput mode puts some of its
    // modifier coordinates, which then did nothing. The game already ignores a resting stick.
    const int sx = axis(g.sThumbLX), sy = axis(g.sThumbLY), cx = axis(g.sThumbRX), cy = axis(g.sThumbRY);
    x.stick_x = (int8_t)sx; x.stick_y = (int8_t)sy; x.sub_x = (int8_t)cx; x.sub_y = (int8_t)cy;
    const Deadzone& dz = g_deadzones[(size_t)PadFamily::Xbox];
    // The triggers reach the game only through bindings: their travel through L analog / R analog,
    // their click through L / R. LT and RT as binding sources press at the family's full-press
    // point, or at the binding's own.
    debug.xinput_actions[idx] = xinput_apply_bindings(g_pad_bindings[idx], g.wButtons, g.bLeftTrigger, g.bRightTrigger, dz.click_l, dz.click_r, x);
  }

  PadState ds4[4]{};
  uint16_t ds4_buttons[4]{};
  bool ds4_connected[4]{};
  {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    for (int idx = 0; idx < 4; ++idx) {
      ds4_buttons[idx] = g_ds4_buttons[idx]; ds4[idx] = g_ds4_pads[idx];
      ds4_connected[idx] = controller_relay::active() ? relay_ps(idx,ds4[idx],ds4_buttons[idx]) : g_ds4_devices[idx] != nullptr;
      ds4[idx].err = ds4_connected[idx] ? 0 : -1;
      if (!ds4_connected[idx]) continue;
      debug.ds4_actions[idx] = ds4_apply_bindings(g_ds4_bindings[idx], ds4_buttons[idx], ds4[idx]);
      debug.ds4_connected[idx] = true;
    }
  }

  PadState swpro[4];
  bool swpro_connected[4]{};
  {
    for (auto& s : swpro) { s = {}; s.err = -1; }
    uint16_t buttons[4]{};
    uint32_t swpro_mask = relay_switch_poll(swpro, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(swpro_mask & (1u << idx))) { swpro[idx] = {}; swpro[idx].err = -1; continue; }
      swpro_connected[idx] = true;
      debug.swpro_connected[idx] = true;
      debug.swpro_actions[idx] = swpro_apply_bindings(idx, buttons[idx], swpro[idx]);
    }
  }

  PadState hid[4];
  bool hid_connected[4]{};
  {
    for (auto& s : hid) { s = {}; s.err = -1; }
    uint32_t buttons[4]{};
    uint32_t hid_mask = relay_hid_poll(hid, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(hid_mask & (1u << idx))) { hid[idx] = {}; hid[idx].err = -1; continue; }
      apply_known_box_layout(idx);
      hid_connected[idx] = true;
      debug.hid_connected[idx] = true;
      debug.hid_actions[idx] = hid_apply_bindings(idx, buttons[idx], hid[idx], g_box_hid[0][idx]);
    }
  }

  for (int idx = 0; idx < 4; ++idx)
    if (gc_mask & (1u << idx)) debug.gc_actions[idx] = gc_apply_bindings(g_gc_bindings[idx], gc[idx]);

  // Per-family options (deadzones, trigger values). Before routing, so the port overlay and everything downstream
  // see what the game gets.
  for (int idx = 0; idx < 4; ++idx) {
    if (gc_mask & (1u << idx)) apply_family_options(PadFamily::GameCube, gc[idx]);
    if (xin_connected[idx]) apply_family_options(PadFamily::Xbox, xin[idx], idx, 0);
    if (ds4_connected[idx]) apply_family_options(PadFamily::PlayStation, ds4[idx], idx, 0);
    if (swpro_connected[idx]) apply_family_options(PadFamily::Switch, swpro[idx], idx, 0);
    if (hid_connected[idx]) apply_family_options(PadFamily::Box, hid[idx]);
  }

  const InputDeviceFacts facts = controller_relay::fallback_facts(input_device_facts(debug));
  // A port given a named box follows that box to whatever HID slot it is in this time.
  for (int port = 0; side_effects && port < 4; ++port) {
    PortSource& src = g_port_sources[port];
    const std::string& want = g_port_device_names[port];
    if (src.kind != DeviceKind::HidPad || want.empty()) continue;
    const bool here = src.index >= 0 && src.index < 4 && hid_connected[src.index] && port_device_key(hidpad_name(src.index)) == want;
    if (here) continue;
    for (int i = 0; i < 4; ++i) {
      if (!hid_connected[i] || port_device_key(hidpad_name(i)) != want) continue;
      log("controls: port %d follows %s to HID slot %d (it was listed as slot %d)", port + 1, want.c_str(), i + 1, src.index + 1);
      src.index = i;
      break;
    }
  }
  for (int port = 0; port < 4; ++port) {
    PortSource src = effective_port_source(g_port_sources, facts, port);
    // A held window-keyboard action beats the app's idle page-keyboard pad in every read, menus
    // included: embedded on Windows the game window has focus, so the page pad never sees the keys.
    if (keyboard_actions) src = controller_relay::console_source(src);
    if (side_effects) {
      g_port_feeding[port] = src;
      controller_relay::routed(src.kind, src.index, port);
    }
    switch (src.kind) {
      case DeviceKind::Keyboard: out[port] = kb; break;
      case DeviceKind::XInputPad:
        if (src.index >= 0 && src.index < 4 && xin_connected[src.index]) out[port] = xin[src.index];
        break;
      case DeviceKind::DS4Pad:
        if (src.index >= 0 && src.index < 4 && ds4_connected[src.index]) out[port] = ds4[src.index];
        break;
      case DeviceKind::SwitchPro:
        if (src.index >= 0 && src.index < 4 && swpro_connected[src.index]) out[port] = swpro[src.index];
        break;
      case DeviceKind::HidPad:
        if (src.index >= 0 && src.index < 4 && hid_connected[src.index]) out[port] = hid[src.index];
        break;
      case DeviceKind::GCAdapter:
        if (src.index >= 0 && src.index < 4 && (gc_mask & (1u << src.index))) out[port] = gc[src.index];
        break;
      case DeviceKind::None: default: break;
    }
    debug.ports[port] = out[port];
    debug.feeding[port] = src;
  }
  // Triples: pads 5 and 6 take the next connected controllers no port 1-4 is set to, in the order GameCube
  // adapter, Xbox, PlayStation, Switch Pro, box: four adapter ports and two Xbox pads are six players
  // with nothing to set up. Same poll as ports 1-4, so they are exactly as fresh.
  if (extra) {
    extra[0] = {}; extra[0].err = -1; extra[1] = {}; extra[1].err = -1;
    PortSource feeding[2] = {{DeviceKind::None, -1}, {DeviceKind::None, -1}};
    int n = 0;
    auto take = [&](DeviceKind kind, int index, bool connected, const PadState& pad) {
      if (!connected || n >= 2) return;
      for (int port = 0; port < 4; ++port)
        if (debug.feeding[port].kind == kind && debug.feeding[port].index == index) return;
      feeding[n] = {kind, index};
      extra[n++] = pad;
    };
    for (int i = 0; i < 4; ++i) take(DeviceKind::GCAdapter, i, (gc_mask & (1u << i)) != 0, gc[i]);
    for (int i = 0; i < 4; ++i) take(DeviceKind::XInputPad, i, xin_connected[i], xin[i]);
    for (int i = 0; i < 4; ++i) take(DeviceKind::DS4Pad, i, ds4_connected[i], ds4[i]);
    for (int i = 0; i < 4; ++i) take(DeviceKind::SwitchPro, i, swpro_connected[i], swpro[i]);
    for (int i = 0; i < 4; ++i) take(DeviceKind::HidPad, i, hid_connected[i], hid[i]);
    if (side_effects) { g_extra_feeding[0] = feeding[0]; g_extra_feeding[1] = feeding[1]; }
  }
  // Background input off and another window in front: every port stays plugged in but neutral.
  if (!focused && !g_background_input) {
    for (int port = 0; port < 4; ++port)
      if (out[port].err == 0) { const auto err = out[port].err; out[port] = {}; out[port].err = err; debug.ports[port] = out[port]; }
    if (extra)
      for (int i = 0; i < 2; ++i)
        if (extra[i].err == 0) { extra[i] = {}; extra[i].err = 0; }
  }
  debug.keyboard_actions = keyboard_actions;
}

// Replay takeover: the port the player took over gets their controller when it has none of its own
// (a replay of P2 played from a controller in port 1).
static std::pair<int, int> route_takeover(PadState out[4]) {
  const int port = replay_control::takeover_port();
  if (port < 0 || port > 3) return {-1, -1};
  int source = port;
  if (out[port].err != 0) {
    for (int i = 0; i < 4; ++i)
      if (i != port && out[i].err == 0) { out[port] = out[i]; source = i; break; }
  }
  // Start retries the attempt (gx/replay_hud.h): it is the viewer's, not the match's pause.
  replay_control::set_live_start((out[port].button & 0x1000) != 0);
  out[port].button &= (uint16_t)~0x1000u;
  return {port, source};
}

// Shared by the offline poll and the console hardware read; never reads a device itself.
struct UiSnapshot {
  PadState* pads; bool gamecube = false;
  bool console = false; // sessions publish UI, but never poll entry/Friends or suppress the raw sample
  PadState keyboard_navigation{};
  bool keyboard_ports[4]{};
#ifdef YOUGAME_AGENT_TEST_HOOK
  PortSource feeding[4]{};
#endif
  ~UiSnapshot() {
#ifdef YOUGAME_AGENT_TEST_HOOK
    // Override the routed port at the common read boundary, before either the chord detector
    // or ImGui sees it, and before UI capture suppresses gameplay. Covers the script early return
    // too. Physical polling/routing stays intact, so the next read after completion restores it.
    auto scripted = yougame::agent_test::pad_back.sample_for_ui(yougame::agent_test::PadBack::Clock::now());
    if (scripted.active && !agent_input_allowed(yougame::agent_test::InputKey::PadBack))
      scripted = {};
    static int reported_step = -1;
    if (scripted.active) {
      feeding[0] = {DeviceKind::None, 0};
      keyboard_ports[0] = false;
      pads[0] = {};
      pads[0].button = scripted.pad.button;
      pads[0].err = scripted.pad.err;
      gamecube = true;
      if (scripted.step != reported_step) {
        reported_step = scripted.step;
        log("[yougame] agent test: pad %s", yougame::agent_test::pad_back_ui_steps[scripted.step].name);
      }
      std::lock_guard<std::mutex> lock(g_last_pads_mutex);
      g_last_pads[0] = pads[0];
    } else if (reported_step >= 0) {
      log("[yougame] agent test: pad done");
      reported_step = -2;
    }
    // Both hosts supply a live sample only during their game PADRead. Fight input wins
    // over padBack on the local port, so the UI script cannot send a settings chord into combat.
    if (yougame::agent_test::input_owner(input_script_active(),
            yougame::agent_test::current_fight_sample.port >= 0 &&
            yougame::agent_test::current_fight_sample.port < 4) == yougame::agent_test::InputOwner::Fight &&
        yougame::agent_test::apply_fight(pads, yougame::agent_test::current_fight_sample,
            yougame::agent_test::live_input_state(slippi::online::session_mode()),
            [](const char* line) { log("%s", line); })) {
      feeding[yougame::agent_test::current_fight_sample.port] = {DeviceKind::None, 0};
      keyboard_ports[yougame::agent_test::current_fight_sample.port] = false;
      gamecube = true;
      std::lock_guard<std::mutex> lock(g_last_pads_mutex);
      g_last_pads[yougame::agent_test::current_fight_sample.port] =
          pads[yougame::agent_test::current_fight_sample.port];
    }
#endif
    {
      uint8_t keyboard_mask = 0;
      for (int i = 0; i < 4; ++i) if (keyboard_ports[i]) keyboard_mask |= (uint8_t)(1u << i);
      g_keyboard_port_mask.store(keyboard_mask, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(g_ui_pad_mutex);
#ifdef YOUGAME_AGENT_TEST_HOOK
    g_ui_agent_step = scripted.step;
#endif
    const uint64_t menu_now = static_cast<uint64_t>(now_seconds() * 1000.0);
    yougame::parity::signals.emit(g_overlay_pad.tick(menu_now));
    bool toggle_settings = false;
    uint32_t scene_major = 0, scene_minor = 0, scene_frame = 0;
    current_scene(&scene_major, &scene_minor, &scene_frame);
    const bool training = scene_major == 0x1C;   // Training, where a lone Start opens its pause menu
    const bool home_input = g_background_input || (g_hwnd && window_focused(g_hwnd));
    if (settings_shortcut::recording_active()) {
      // Settings is recording the Practice menu's buttons: it gets them, nothing else does.
      uint16_t held = 0;
      for (int i = 0; i < 4; ++i)
        if (pads[i].err == 0) { held |= static_cast<uint16_t>(pads[i].button & settings_shortcut::kChordButtons); pads[i].button = 0; }
      settings_shortcut::recorded_held().store(held, std::memory_order_relaxed);
    }
    for (int i = 0; i < 4; ++i) {
      // Only ImGui receives the filtered keyboard copy. All chord/console/friends readers
      // below keep the original routed pad, including Start and the arrow-made stick.
      g_ui_navigation_pads[i] = keyboard_ports[i] ? keyboard_navigation : pads[i];
      const uint16_t raw_buttons = pads[i].button;
      const bool opened_here =
          settings_shortcut::poll_z_start(pads[i].err == 0, pads[i].button,
                                          g_ui_settings_chord_held[i]);
      uint16_t menu_buttons = raw_buttons;
      if (pads[i].err == 0 && raw_buttons && !keyboard_ports[i]) yougame::input_kind("pad");
      yougame::parity::signals.emit(g_overlay_pad.poll(yougame::stdio_bridge(), i,
          pads[i].err == 0, menu_buttons, menu_now, g_ui_settings_chord_held[i]));
      if (!console && yougame::active() && !opened_here && !g_ui_capture.load()) {
        // These follow the game's background-input rule, not focus: START/B in a home work exactly
        // when the menu does. With background input off an unfocused window reads as unplugged, so a
        // button held while focus comes back must be released first.
        if (
#ifdef MELEE_DOUBLES
            yougame::doubles::ui.poll(i, pads[i].err == 0 && home_input, menu_buttons) ||
#endif
            yougame::console::entry.poll(i, pads[i].err == 0 && home_input, menu_buttons)) {
          const auto err = pads[i].err;
          pads[i] = {}; pads[i].err = err;
          g_ui_navigation_pads[i] = pads[i];
          menu_buttons = 0;
        } else {
#ifdef MELEE_DOUBLES
          if (const uint16_t carried = yougame::doubles::ui.carried_over(i)) {
            pads[i].button &= static_cast<uint16_t>(~carried);
            menu_buttons &= static_cast<uint16_t>(~carried);
          }
#endif
          yougame::friends::events.poll_pad(i, pads[i].err == 0, menu_buttons);  // the game's menu takes this A unfocused too
        }
      }
      if (!(menu_buttons & 0x1000)) pads[i].button &= static_cast<uint16_t>(~0x1000);
      g_ui_navigation_pads[i].button &= pads[i].button; // preserve consumed chords/shortcuts
      toggle_settings |= opened_here;
      if (opened_here) g_settings_controller_port.store(i, std::memory_order_release);
      if (settings_shortcut::poll_practice(pads[i].err == 0, pads[i].button, g_ui_practice_chord_held[i]))
        g_practice_toggle.store(true, std::memory_order_release);   // same as Tab
      const bool start_taken = g_ui_settings_chord_held[i] || g_ui_practice_chord_held[i] ||
                               ((raw_buttons & 0x1000) && !(menu_buttons & 0x1000));
      settings_shortcut::hold_lone_start(training && pads[i].err == 0, start_taken, raw_buttons,
                                         pads[i].button, g_ui_start_waited[i]);
    }
    if (toggle_settings) g_settings_toggle.store(true, std::memory_order_release);
    walter_egg::watch_pads(pads);   // the character select's easter egg: reads only
    g_ui_pad = pads[0];
    g_ui_gamecube = gamecube;
    g_ui_has_pad = false;
    for (int i = 0; i < 4; ++i) {
      g_ui_pads[i] = pads[i];
      if (pads[i].err == 0) g_ui_has_pad = true;
    }
    // Buttons the overlay holds from the game (window_withhold_buttons), after the overlay's copy.
    if (const uint16_t withheld = g_ui_withheld.load(std::memory_order_relaxed))
      for (int i = 0; i < 4; ++i) pads[i].button &= static_cast<uint16_t>(~withheld);
    if (!console && g_ui_capture.load()) {
      // UI capture is global, not player-one-only. Keeping ports 2-4 live made menu navigation
      // leak into Training and, during handoff, into the first online gameplay inputs.
      for (int i = 0; i < 4; ++i) {
        const int8_t err = pads[i].err;
        pads[i] = {};
        pads[i].err = err;
      }
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    // After chord suppression (and offline capture): console capture is applied at console_hardware.
    for (int i = 0; i < 4; ++i) {
      const auto& p = pads[i];
      const auto src = feeding[i];
      std::string source = "none";
      if (p.err == 0 && !input_script_active()) {
        if (src.kind == DeviceKind::Keyboard) source = "keyboard";
        else if (src.kind == DeviceKind::GCAdapter) source = "adapter";
        else if (src.kind != DeviceKind::None) {
          source = "sdl";
          // The mixed relay slot differs from the index within a controller family.
          const auto read = controller_relay::reader.load(std::memory_order_acquire);
          if (read && (src.kind != DeviceKind::HidPad || relay_owns_hid())) {
            const auto snapshot = read();
            for (size_t slot = 0; slot < snapshot.devices.size(); ++slot) {
              const auto& device = snapshot.devices[slot];
              if (device.family == controller_relay::family_of(src.kind) && device.index == src.index) {
                source = "relayed slot " + std::to_string(slot + 1);
                break;
              }
            }
          }
        }
      }
      yougame::agent_test::pad_trace.poll(i, {p.button, p.stick_x, p.stick_y, p.sub_x, p.sub_y,
                                             p.trig_l, p.trig_r, p.err, source});
    }
#endif
  }
};

void input_poll(PadState out[4]) {
  controller_relay::GamePoll relay_poll;
  perf::Scope work_cost(perf::Stage::Input);
  struct InputCost { double start = now_seconds(); ~InputCost() { sim_cost_add(SIM_INPUT, now_seconds() - start); } } input_cost;
  UiSnapshot ui{out};
  if (TickTiming& tick = tick_timing(); tick.pad == 0) tick.pad = now_seconds();
  early_input::note_poll();   // before the read: a change after this point is the next frame's
  for (int i = 0; i < 4; ++i) { std::memset(&out[i], 0, sizeof out[i]); out[i].err = -1; }
  if (!g_script.empty() && g_script_release_after) {
    uint32_t major, minor, match_frame;
    current_scene(&major, &minor, &match_frame);
    if (!g_script_release_at && match_frame > 1) g_script_release_at = retrace_count() + g_script_release_after;
    if (g_script_release_at && retrace_count() >= g_script_release_at) { g_script.clear(); log("script: released, controllers live"); }
  }
  if (input_script_active()) {
    script_pads(retrace_count(), true, out);
    g_extra_pads[0] = {}; g_extra_pads[0].err = -1; g_extra_pads[1] = {}; g_extra_pads[1].err = -1;
    script_pads(retrace_count(), false, g_extra_pads, 4, 2);   // p=5, p=6 (Triples)
    if (latency_trace_enabled()) note_script_presses(out);
    route_takeover(out);
    if (const uint16_t held = g_test_hold.load(std::memory_order_relaxed)) { out[0].button |= held; out[0].err = 0; }
    midtick_note_poll();
    // Scripted runs took this return before the overlay's copy was made below, so the controller
    // overlay stayed blank whenever a script was driving. It is the only way to see the overlay
    // without a controller in hand, so keep it fed here too.
    { std::lock_guard<std::mutex> lock(g_last_pads_mutex); for (int p = 0; p < 4; ++p) g_last_pads[p] = out[p]; }
    return;
  }
  InputDebugSnapshot debug{};
  {
    std::lock_guard<std::mutex> devices(g_devices_mutex);
    read_devices(out, debug, true, &ui.keyboard_navigation, false, g_extra_pads);
  }
  ui.gamecube = debug.gc_mask != 0;
  const auto takeover = route_takeover(out);
  if (const uint16_t held = g_test_hold.load(std::memory_order_relaxed)) { out[0].button |= held; out[0].err = 0; }
  for (int i = 0; i < 4; ++i)
    ui.keyboard_ports[i] = debug.feeding[i].kind == DeviceKind::Keyboard;
  if (takeover.first >= 0) ui.keyboard_ports[takeover.first] = ui.keyboard_ports[takeover.second];
#ifdef YOUGAME_AGENT_TEST_HOOK
  for (int i = 0; i < 4; ++i) ui.feeding[i] = debug.feeding[i];
  if (takeover.first >= 0) ui.feeding[takeover.first] = debug.feeding[takeover.second];
#else
  (void)takeover;
#endif
  if (const uint16_t held = g_test_hold.load(std::memory_order_relaxed)) { out[0].button |= held; out[0].err = 0; }
  { std::lock_guard<std::mutex> lock(g_last_pads_mutex); for (int port = 0; port < 4; ++port) g_last_pads[port] = out[port]; }
  // MELEE_TEST_PAD_TRACE=<file> (tests only, hidden runs): one line each time a port's input changes,
  // as the game will read it, so a test that drives a controller can check every press arrived.
  if (options.no_gc_adapter) {
    static FILE* trace = [] { const char* p = host::test_env::getenv("MELEE_TEST_PAD_TRACE"); return p && *p ? std::fopen(p, "w") : nullptr; }();
    static PadState last[4] = {};
    if (trace)
      for (int port = 0; port < 4; ++port) {
        const PadState& p = out[port];
        if (!std::memcmp(&p, &last[port], sizeof p)) continue;
        last[port] = p;
        std::fprintf(trace, "%u port %d err %d button %04X stick %d %d cstick %d %d trig %u %u\n", retrace_count(), port + 1,
                     p.err, p.button, p.stick_x, p.stick_y, p.sub_x, p.sub_y, p.trig_l, p.trig_r);
        std::fflush(trace);
      }
  }

  // Measurement only (MELEE_TRACE_PAD_ARRIVAL=1): the moment each new button press reaches the game, read
  // directly or relayed by the YouGame app, on the performance counter, so a tool that pressed the
  // button can time press to arrival without the screen.
  static const bool trace_arrival = [] { const char* v = std::getenv("MELEE_TRACE_PAD_ARRIVAL"); return v && *v == '1'; }();
  if (trace_arrival) {
    static uint16_t held[4] = {};
    for (int port = 0; port < 4; ++port) {
      const uint16_t now = out[port].err == 0 ? (uint16_t)out[port].button : 0;
      if (now & ~held[port]) {
        LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
        log("pad arrival: qpc %lld freq %lld port %d buttons %04x", (long long)t.QuadPart, (long long)f.QuadPart, port + 1, now);
      }
      held[port] = now;
    }
  }
  input_debug_snapshot(debug);
}

namespace {
// A short on-screen name for a key, the ones the settings page shows for the common keys.
bool short_key_name(int vk, char* out, size_t n) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { std::snprintf(out, n, "%c", (char)vk); return true; }
  if (vk >= VK_F1 && vk <= VK_F12) { std::snprintf(out, n, "F%d", vk - VK_F1 + 1); return true; }
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9) { std::snprintf(out, n, "Num %d", vk - VK_NUMPAD0); return true; }
  const char* name = nullptr;
  switch (vk) {
    case VK_RETURN: name = "Enter"; break;
    case VK_SPACE: name = "Space"; break;
    case VK_TAB: name = "Tab"; break;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: name = "Shift"; break;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: name = "Ctrl"; break;
    case VK_MENU: case VK_LMENU: case VK_RMENU: name = "Alt"; break;
    case VK_BACK: name = "Backspace"; break;
    case VK_LEFT: name = "Left"; break;
    case VK_RIGHT: name = "Right"; break;
    case VK_UP: name = "Up"; break;
    case VK_DOWN: name = "Down"; break;
    case VK_OEM_1: name = ";"; break;
    case VK_OEM_PLUS: name = "="; break;
    case VK_OEM_COMMA: name = ","; break;
    case VK_OEM_MINUS: name = "-"; break;
    case VK_OEM_PERIOD: name = "."; break;
    case VK_OEM_2: name = "/"; break;
    case VK_OEM_3: name = "`"; break;
    case VK_OEM_4: name = "["; break;
    case VK_OEM_5: name = "\\"; break;
    case VK_OEM_6: name = "]"; break;
    case VK_OEM_7: name = "'"; break;
    default: return false;   // an unusual key: the caller keeps the GameCube name
  }
  std::snprintf(out, n, "%s", name);
  return true;
}
}  // namespace

bool input_keyboard_button_label(int port, uint16_t pad_button, char* out, size_t n) {
  if (!out || n < 2 || port < 0 || port > 3 || !pad_button) return false;
  if (!(g_keyboard_port_mask.load(std::memory_order_relaxed) & (1u << port))) return false;
  int vk = 0;
  for (size_t i = 0; i < (size_t)BindAction::Count; ++i)
    if (kActionPadBit[i] == pad_button) { vk = g_key_bindings.vk[i]; break; }
  return vk != 0 && short_key_name(vk, out, n);
}

// Mid-tick test press and change detection for the tick re-run (host.h).
static double g_midtick_inject = 0.0;        // when the test press went down or up, until a poll has read it
static uint16_t g_midtick_polled = 0;        // the test hold as the last poll saw it
static void midtick_note_poll() {
  const uint16_t held = g_test_hold.load(std::memory_order_relaxed);
  if (held != g_midtick_polled && g_midtick_inject > 0.0) {
    TickTiming& tick = tick_timing();
    tick.probe = tick.probe + 1;
    tick.probe_inject = g_midtick_inject;
    tick.probe_pad = now_seconds();
    g_midtick_inject = 0.0;
  }
  g_midtick_polled = held;
}
struct MidtickTest {
  unsigned every = 0, shift = 0; double offset = 0.0;   // <every n ticks>:<ms into the tick>[:<ticks earlier>]
  MidtickTest() { if (const char* v = host::test_env::getenv("MELEE_TEST_MIDTICK_PRESS")) { double ms = 0; if (std::sscanf(v, "%u:%lf:%u", &every, &ms, &shift) >= 2) offset = ms / 1000.0; else every = 0; } }
};
// Read on first use, through the test switch policy (a release session reads no test switches).
static const MidtickTest& midtick_test() { static const MidtickTest t; return t; }
bool input_midtick_test() { return midtick_test().every != 0; }
bool input_changed_since_poll(double tick_start) {
  const MidtickTest& g_midtick_test = midtick_test();
  if (g_midtick_test.every) {
    // Down on every n-th tick, up six ticks later, both at the same point inside the tick. Matches only.
    static uint32_t fired = 0;
    const uint32_t tick = retrace_count();
    uint32_t major = 0, minor = 0, match_frame = 0;
    current_scene(&major, &minor, &match_frame);
    const uint32_t phase = (tick + g_midtick_test.shift) % g_midtick_test.every;
    if (match_frame > 120 && tick != fired && (phase == 0 || phase == 6) && now_seconds() - tick_start >= g_midtick_test.offset) {
      fired = tick;
      g_test_hold.store(phase == 0 ? PAD_X : 0, std::memory_order_relaxed);
      g_midtick_inject = now_seconds();
    }
    return g_test_hold.load(std::memory_order_relaxed) != g_midtick_polled;
  }
  if (input_script_active()) return false;
  PadState now[4], last[4];
  for (auto& p : now) { p = {}; p.err = -1; }
  input_read_devices(now);
  input_last_pads(last);
  for (int port = 0; port < 4; ++port) {
    if (now[port].err != 0 || last[port].err != 0) continue;
    if (now[port].button != last[port].button) return true;
    // Sticks and triggers: a move of 12 steps or more since the poll (a flick, not drift or noise).
    if (std::abs(now[port].stick_x - last[port].stick_x) >= 12 || std::abs(now[port].stick_y - last[port].stick_y) >= 12 ||
        std::abs(now[port].sub_x - last[port].sub_x) >= 12 || std::abs(now[port].sub_y - last[port].sub_y) >= 12 ||
        std::abs((int)now[port].trig_l - (int)last[port].trig_l) >= 24 || std::abs((int)now[port].trig_r - (int)last[port].trig_r) >= 24)
      return true;
  }
  return false;
}

void input_poll_extra(PadState out[2]) {
  out[0] = g_extra_pads[0];
  out[1] = g_extra_pads[1];
}

void input_read_devices(PadState out[4]) {
  InputDebugSnapshot debug{};
  std::lock_guard<std::mutex> devices(g_devices_mutex);
  read_devices(out, debug, false);
}

void input_read_console_devices(PadState out[4]) {
  controller_relay::GamePoll relay_poll; // same one-snapshot boundary as offline PADRead
  UiSnapshot ui{out};
  ui.console = true;
  InputDebugSnapshot debug{};
  for (int i = 0; i < 4; ++i) { out[i] = {}; out[i].err = -1; }
  {
    std::lock_guard<std::mutex> devices(g_devices_mutex);
    read_devices(out, debug, true, &ui.keyboard_navigation, true);
  }
  ui.gamecube = debug.gc_mask != 0;
#ifdef YOUGAME_AGENT_TEST_HOOK
  for (int i = 0; i < 4; ++i) ui.feeding[i] = debug.feeding[i];
#endif
  bool controller = false;
  for (int i = 0; i < 4; ++i)
    controller |= out[i].err == 0 && debug.feeding[i].kind != DeviceKind::Keyboard;
  if (controller) for (int i = 0; i < 4; ++i)
    if (debug.feeding[i].kind == DeviceKind::Keyboard) { out[i] = {}; out[i].err = -1; }
  for (int i = 0; i < 4; ++i)
    ui.keyboard_ports[i] = out[i].err == 0 && debug.feeding[i].kind == DeviceKind::Keyboard;
  { std::lock_guard<std::mutex> lock(g_last_pads_mutex); for (int i = 0; i < 4; ++i) g_last_pads[i] = out[i]; }
}

#ifdef YOUGAME_AGENT_TEST_HOOK
// Console hardware slots are local devices, not session seats. The engine samples the first
// connected slot and sends it as config.my_port through its ordinary delay/rollback queue.
void input_console_script(PadState out[4], int local_port) {
  if (!g_console_script_profile || local_port < 1 || local_port > 4 || !input_script_active()) return;
  PadState scripted[4]{};
  for (auto& p : scripted) p.err = -1;
  script_pads(retrace_count(),true,scripted);
  if (scripted[0].err != 0) return;
  out[0] = scripted[0]; // Route p=1 means this seat's own pad, whatever its assigned session port.
  static bool logged = false;
  if (!logged) { log("agent-test: pad script drives console port %d",local_port); logged = true; }
}
#endif

bool input_script_active() {
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!g_script.empty() && !agent_input_allowed(yougame::agent_test::InputKey::Script)) return false;
#endif
  return !g_script.empty();
}

void input_script_peek(uint32_t retrace, PadState out[4]) {
  for (int i = 0; i < 4; ++i) { std::memset(&out[i], 0, sizeof out[i]); out[i].err = -1; }
  if (input_script_active()) script_pads(retrace, false, out);
}

std::atomic<bool> g_rumble_enabled{true};
bool g_background_input = true;

namespace {
// Serialize motor commands with the all-off action. A motor-on request that began before
// the Controls switch changed must not land after the all-off command.
std::mutex g_rumble_mutex;
// An Xbox pad's motors: both at full while the game asks for rumble, off otherwise. Only written
// when the state changes, and never on the caller's thread: XInputSetState is a call into the
// driver that can take milliseconds (longer on a wireless pad), and the caller is the game thread
// in the middle of a frame, most often on a hit. A writer thread sends the newest state, the way
// the GameCube adapter's rumble has always been sent.
std::mutex g_xrumble_mutex;
std::condition_variable g_xrumble_wake, g_xrumble_done;
bool g_xrumble_wanted[4] = {}, g_xrumble_applied[4] = {};
bool g_xrumble_started = false;
void xinput_rumble_writer() {
  std::unique_lock<std::mutex> lock(g_xrumble_mutex);
  for (;;) {
    int index = -1;
    for (int i = 0; i < 4 && index < 0; ++i) if (g_xrumble_wanted[i] != g_xrumble_applied[i]) index = i;
    if (index < 0) { g_xrumble_done.notify_all(); g_xrumble_wake.wait(lock); continue; }
    const bool on = g_xrumble_wanted[index];
    lock.unlock();
    XINPUT_VIBRATION v{};
    v.wLeftMotorSpeed = v.wRightMotorSpeed = on ? 65535 : 0;
    const auto began = std::chrono::steady_clock::now();
    yougame::relay_pads::rumble([&] { XInputSetState((DWORD)index, &v); });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    // Said once, with the figure: it tells from a player's log whether their pad is a slow one.
    static bool told = false;
    if (ms > 4.0 && !told) { told = true; log("rumble: the Xbox controller took %.1f ms to take a rumble command (sent off the game thread)", ms); }
    lock.lock();
    g_xrumble_applied[index] = on;
  }
}
void xinput_rumble(int index, bool on) {
  if (index < 0 || index > 3) return;
  if (yougame::relay_pads::active()) { yougame::relay_pads::rumble([] {}); return; }
  std::lock_guard<std::mutex> lock(g_xrumble_mutex);
  if (g_xrumble_wanted[index] == on) return;
  g_xrumble_wanted[index] = on;
  if (!g_xrumble_started) { g_xrumble_started = true; std::thread(xinput_rumble_writer).detach(); }
  g_xrumble_wake.notify_one();
}
// For the all-off action: the motors must be off before the caller goes on (it runs when the game
// is closing or the Controls switch turns rumble off). Bounded, so a stuck driver cannot hold it.
void xinput_rumble_settle() {
  std::unique_lock<std::mutex> lock(g_xrumble_mutex);
  if (!g_xrumble_started) return;
  perf::Scope wait_cost(perf::Stage::Wait);
  g_xrumble_done.wait_for(lock, std::chrono::milliseconds(200), [] {
    for (int i = 0; i < 4; ++i) if (g_xrumble_wanted[i] != g_xrumble_applied[i]) return false;
    return true;
  });
}
// The first rumble each port asks for, and where it went: players report "rumble does not work"
// without saying which controller, and this line answers that from their log.
void log_first_rumble(int game_port, const char* where) {
  static bool logged[5] = {};
  const int i = game_port < 0 ? 4 : game_port;
  if (logged[i]) return;
  logged[i] = true;
  log("rumble: %s%d -> %s%s", game_port < 0 ? "local player" : "port ", game_port < 0 ? 0 : game_port + 1, where,
      g_rumble_enabled.load() ? "" : " (switched off in Controls)");
}
}  // namespace

void input_rumble(int game_port, bool on) {
  if (game_port < 0 || game_port > 5) return;   // 4 and 5: Triples' pads 5 and 6
  std::lock_guard<std::mutex> lock(g_rumble_mutex);
  // The Lossless adapter's lag test (lag_test_codes.h): its start signal is the character select's
  // rumble on all four ports, which the adapter itself listens for. Dolphin in that setup sends port
  // N's rumble to adapter socket N, so this does too, whatever feeds the port (no report may have
  // arrived yet a few frames into the launch) and whatever the rumble switch says.
  static const bool lag_test = [] { const char* v = std::getenv("MELEE_LAG_TEST"); return v && *v; }();
  if (lag_test) {
    gcadapter_rumble(game_port, on);
    if (on) log_first_rumble(game_port, "GameCube adapter (lag test)");
    return;
  }
  if (!g_rumble_enabled.load()) on = false;
  const PortSource& src = game_port < 4 ? g_port_feeding[game_port] : g_extra_feeding[game_port - 4];   // where the input came from, fallback included
  const bool valid = src.index >= 0 && src.index < 4;
  if (valid && controller_relay::rumble(src.kind,src.index,on)) return;
  if (src.kind == DeviceKind::GCAdapter && valid) { gcadapter_rumble(src.index, on); if (on) log_first_rumble(game_port, "GameCube adapter"); }
  else if (src.kind == DeviceKind::XInputPad && valid) { xinput_rumble(src.index, on); if (on) log_first_rumble(game_port, "Xbox controller"); }
  else if (on) log_first_rumble(game_port, "a controller without rumble support (keyboard, PlayStation, Switch or box)");
}

void input_rumble_local(bool on) {
  std::lock_guard<std::mutex> lock(g_rumble_mutex);
  if (!g_rumble_enabled.load()) on = false;
  // Online the match's slot is not the socket the controller is in, so the caller cannot name a
  // port: it only knows the rumble was meant for the local player. Rumble the one controller that
  // is actually feeding a port, not every device on the machine.
  //
  // This used to poll the adapter and XInput and buzz everything connected, which meant a player
  // on port 3 in a Direct match felt their port 1 controller go off as well, and anyone with a
  // second pad plugged in felt it on both. g_port_feeding already records which device feeds each
  // port, with the same fallback the offline path in input_rumble() uses, so the local player's
  // controller is the first port that has a real one behind it.
  for (int port = 0; port < 4; ++port) {
    const PortSource& src = g_port_feeding[port];
    const bool valid = src.index >= 0 && src.index < 4;
    if (!valid) continue;
    if (controller_relay::rumble(src.kind,src.index,on)) return;
    if (src.kind == DeviceKind::GCAdapter) {
      gcadapter_rumble(src.index, on);
      if (on) log_first_rumble(-1, "GameCube adapter");
      return;
    }
    if (src.kind == DeviceKind::XInputPad) {
      xinput_rumble(src.index, on);
      if (on) log_first_rumble(-1, "Xbox controller");
      return;
    }
  }
  if (on) log_first_rumble(-1, "a controller without rumble support (keyboard, PlayStation, Switch or box)");
}

void input_stop_all_rumble() {
  std::lock_guard<std::mutex> lock(g_rumble_mutex);
  for (int index = 0; index < 4; ++index) {
    for (auto kind : {DeviceKind::XInputPad,DeviceKind::DS4Pad,DeviceKind::SwitchPro,DeviceKind::HidPad}) controller_relay::rumble(kind,index,false);
    gcadapter_rumble(index, false);
    xinput_rumble(index, false);
  }
  xinput_rumble_settle();
}

void input_test_hold(uint16_t buttons) { g_test_hold.store(buttons, std::memory_order_relaxed); }

void input_last_pads(PadState out[4]) {
  std::lock_guard<std::mutex> lock(g_last_pads_mutex);
  for (int port = 0; port < 4; ++port) out[port] = g_last_pads[port];
}

void input_debug_snapshot(InputDebugSnapshot& snapshot) {
  snapshot = InputDebugSnapshot{};
  PadState gc[4];
  for (auto& s : gc) { s = {}; s.err = -1; }
  uint32_t gc_mask = gcadapter_poll(gc);
  snapshot.gc_mask = gc_mask;

  PadState kb{}; kb.err = 0;
  {
    std::lock_guard<std::mutex> lock(g_keys_mutex);
    auto key = [](int vk) {
      vk &= 0xFF;
#ifdef _WIN32
      if (shortcut_swallowed(vk)) return false;
#endif
      return g_keys[vk];
    };
    snapshot.keyboard_actions = keyboard_apply_bindings(kb, g_key_bindings, key, g_box_keyboard[1], g_stick_modifier_percent);
  }

  PadState xin[4];
  for (int idx = 0; idx < 4; ++idx) {
    xin[idx] = {}; xin[idx].err = -1;
    XINPUT_STATE xs{};
    if (!xinput_state(idx, &xs)) continue;
    snapshot.xinput_connected[idx] = true;
    xin[idx].err = 0;
    auto& g = xs.Gamepad;
    const int packed = g_box_xinput_state[idx].load(std::memory_order_relaxed);
    BoxXInputDetect det; det.state = (int8_t)(packed & 0xFF); det.first = (int8_t)(packed >> 8);
    if (det.state > 0) { g.bLeftTrigger = box_xinput_trigger(g.bLeftTrigger); g.bRightTrigger = box_xinput_trigger(g.bRightTrigger); }
    auto axis = [&det](SHORT v) { return (int)box_xinput_axis(det, v); };
    // Same reading as input_poll: no deadzone, no trigger floor.
    const int sx = axis(g.sThumbLX), sy = axis(g.sThumbLY), cx = axis(g.sThumbRX), cy = axis(g.sThumbRY);
    xin[idx].stick_x = (int8_t)sx; xin[idx].stick_y = (int8_t)sy; xin[idx].sub_x = (int8_t)cx; xin[idx].sub_y = (int8_t)cy;
    const Deadzone& dz = g_deadzones[(size_t)PadFamily::Xbox];
    snapshot.xinput_actions[idx] = xinput_apply_bindings(g_pad_bindings[idx], g.wButtons, g.bLeftTrigger, g.bRightTrigger, dz.click_l, dz.click_r, xin[idx]);
  }

  PadState ds4[4]{};
  uint16_t ds4_buttons[4]{};
  for (int idx = 0; idx < 4; ++idx) {
    std::lock_guard<std::mutex> lock(g_ds4_mutex);
    ds4_buttons[idx] = g_ds4_buttons[idx]; ds4[idx] = g_ds4_pads[idx];
    if (controller_relay::active() ? !relay_ps(idx,ds4[idx],ds4_buttons[idx]) : !g_ds4_devices[idx]) { ds4[idx].err = -1; continue; }
    snapshot.ds4_connected[idx] = true; ds4[idx].err = 0;
    snapshot.ds4_actions[idx] = ds4_apply_bindings(g_ds4_bindings[idx], ds4_buttons[idx], ds4[idx]);
  }

  PadState swpro[4];
  {
    for (auto& s : swpro) { s = {}; s.err = -1; }
    uint16_t buttons[4]{};
    uint32_t swpro_mask = relay_switch_poll(swpro, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(swpro_mask & (1u << idx))) { swpro[idx] = {}; swpro[idx].err = -1; continue; }
      snapshot.swpro_connected[idx] = true;
      snapshot.swpro_buttons[idx] = buttons[idx];
      snapshot.swpro_actions[idx] = swpro_apply_bindings(idx, buttons[idx], swpro[idx]);
    }
  }

  // This function does not read back what input_poll computed: it rebuilds the whole snapshot from
  // the devices itself, which is why every family above is polled again here. A generic pad added
  // to input_poll alone therefore showed its name and its live axes, which are read straight from
  // the device, while reporting "not connected" and refusing to rebind, because those two come
  // from the snapshot.
  PadState hid[4];
  {
    for (auto& s : hid) { s = {}; s.err = -1; }
    uint32_t buttons[4]{};
    uint32_t hid_mask = relay_hid_poll(hid, buttons);
    for (int idx = 0; idx < 4; ++idx) {
      if (!(hid_mask & (1u << idx))) { hid[idx] = {}; hid[idx].err = -1; continue; }
      snapshot.hid_connected[idx] = true;
      snapshot.hid_actions[idx] = hid_apply_bindings(idx, buttons[idx], hid[idx], g_box_hid[1][idx]);
    }
  }

  const InputDeviceFacts facts = controller_relay::fallback_facts(input_device_facts(snapshot));
  for (int idx = 0; idx < 4; ++idx) {
    snapshot.gc_actions[idx] = 0;
    if (!(gc_mask & (1u << idx))) continue;
    snapshot.gc_actions[idx] = gc_apply_bindings(g_gc_bindings[idx], gc[idx]);
  }
  for (int idx = 0; idx < 4; ++idx) {
    if (facts.gc[idx]) apply_family_options(PadFamily::GameCube, gc[idx]);
    if (facts.xinput[idx]) apply_family_options(PadFamily::Xbox, xin[idx], idx, 1);
    if (facts.ds4[idx]) apply_family_options(PadFamily::PlayStation, ds4[idx], idx, 1);
    if (facts.switch_pro[idx]) apply_family_options(PadFamily::Switch, swpro[idx], idx, 1);
    if (facts.hid[idx]) apply_family_options(PadFamily::Box, hid[idx]);
  }
  snapshot.keyboard_pad = kb;
  for (int idx = 0; idx < 4; ++idx) {
    snapshot.xinput_pad[idx] = xin[idx]; snapshot.ds4_pad[idx] = ds4[idx]; snapshot.swpro_pad[idx] = swpro[idx];
    snapshot.hid_pad[idx] = hid[idx];
    if (gc_mask & (1u << idx)) snapshot.gc_pad[idx] = gc[idx];
  }
  for (int port = 0; port < 4; ++port) {
    const PortSource src = effective_port_source(g_port_sources, facts, port);
    snapshot.feeding[port] = src;
    switch (src.kind) {
      case DeviceKind::Keyboard: snapshot.ports[port] = kb; break;
      case DeviceKind::XInputPad:
        if (src.index >= 0 && src.index < 4 && snapshot.xinput_connected[src.index]) snapshot.ports[port] = xin[src.index];
        break;
      case DeviceKind::DS4Pad:
        if (src.index >= 0 && src.index < 4 && snapshot.ds4_connected[src.index]) snapshot.ports[port] = ds4[src.index];
        break;
      case DeviceKind::SwitchPro:
        if (src.index >= 0 && src.index < 4 && snapshot.swpro_connected[src.index]) snapshot.ports[port] = swpro[src.index];
        break;
      case DeviceKind::HidPad:
        if (src.index >= 0 && src.index < 4 && snapshot.hid_connected[src.index]) snapshot.ports[port] = hid[src.index];
        break;
      case DeviceKind::GCAdapter:
        if (src.index >= 0 && src.index < 4 && (gc_mask & (1u << src.index))) snapshot.ports[port] = gc[src.index];
        break;
      case DeviceKind::None: default: break;
    }
  }
}

}  // namespace host
