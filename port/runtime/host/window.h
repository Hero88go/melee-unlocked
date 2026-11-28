// Host window services.
#pragma once
#include <functional>
#include <cstdint>
#include <string>

namespace host {
using MessageCallback = std::function<bool(void*, uint32_t, uintptr_t, intptr_t)>;
#ifndef _WIN32
// Linux: the window's messages are SDL events. The callback gets the SDL_Event* with this message number.
constexpr uint32_t kSdlEventMessage = 0x7FFF0001u;
#endif
void window_set_message_callback(MessageCallback cb);
void window_input_capture(bool capture);
bool window_input_captured(); // shared by Windows, macOS and Linux; local console samples only
// GameCube buttons held back from the game while the overlay uses them (0: none). The overlay still
// reads them through window_ui_pads.
void window_withhold_buttons(uint16_t mask);
struct PadState;
bool window_ui_gamecube_pad(PadState& pad);
// Last routed state for every in-game port, captured before UI input suppression. This lets an
// overlay follow the controller port selected in Training instead of assuming port 1.
// Optional ImGui-only snapshot excludes bindings already handled by backend keyboard navigation.
// Both copies are read atomically and share one agent-script acknowledgement.
bool window_ui_pads(PadState pads[4], PadState navigation_pads[4] = nullptr);
using ResizeCallback = std::function<void(int, int)>;
#ifdef _WIN32
void* window_create(int w, int h, const wchar_t* title, bool visible = true, bool flip = false);
#else
void* window_create(int w, int h, const wchar_t* title, bool visible = true);
#endif
// Linux: the next window_create makes a Vulkan window instead of an OpenGL one (SDL allows one
// kind per window). Windows draws either way into the same window, so it ignores this.
void window_prefer_vulkan(bool on);
void window_prefer_background(bool on);   // prewarm: no app activation, before video initialization
void window_set_resize_callback(ResizeCallback cb);
// For a window that scales its own contents with the monitor (the standalone settings window).
// While a callback is set, a move to a monitor with another DPI calls it with the new DPI and then
// resizes the window to the rectangle Windows suggests. Without one the message is left alone, so
// the game window keeps its size in pixels wherever it is dragged.
using DpiCallback = std::function<void(int)>;
void window_set_dpi_callback(DpiCallback cb);
void window_pump();
void window_set_fullscreen(bool enabled);
bool window_is_fullscreen();
// Before the game window is made: it opens where it was last closed (Windows; window-place.ini in the
// settings file's folder). Only a visible game window uses it, never a hidden test window.
void window_remember_place(const std::string& settings_path);
// Windows: the game window (an HWND); and picture in picture for the warm-up room's two windows
// (hle/slippi_arena.cpp): mode 1 small (borderless, on top, dragged and sized with the mouse, never
// taking the focus), 2 big (borderless, filling what it is given), 0 back exactly as it was; mute
// turns that game's sound off while it is not back. window_send_pip asks another game's window (the
// room's viewer); window_of_process finds the game window of a process.
void* window_native();
void window_set_pip(int mode, bool mute);
void window_send_pip(void* window, int mode, bool mute);
int window_pip_answer();   // the mode the other window answered it took since the last send, -1 none yet
// The room's match window's view (options.spectate_view: 0 the game, 1 Slippi Lab view, 2 light): set in
// it from the game that opened it; reported back from its corner selector (window_spectate_choice
// answers it once, -1 when nothing new); the selector's own rectangle, which takes clicks.
void window_send_spectate_view(void* window, int view);
void window_report_spectate_view(int view);
int window_spectate_choice();
// The room's match window's buttons (its list): 1 WARM UP, 2 PICTURE IN PICTURE (make the match small),
// 3 FULL SIZE. Reported to the game that opened it, which takes each once (-1: none new).
void window_report_spectate_action(int action);
int window_spectate_action();
void window_set_hot_rect(int x0, int y0, int x1, int y1);
// This window's own picture in picture state (0 as it was, 1 small, 2 big): the small one draws its frame.
int window_pip_mode();
// The mouse was over this window in the last two seconds (the small window: its own hit test sees it).
bool window_hovered_recently();
// Clicks on the small window that did not move it, since the last call (they open its style menu).
int window_take_clicks();
// The room's match window tells the game that opened it what it shows, frame by frame: per port 14 bits
// (stocks << 10 | percent, stocks 15 = nobody there), port 0 lowest. The game's panel shows exactly that.
void window_report_score(uint64_t packed);
bool window_viewer_score(uint64_t* packed);   // false: nothing in the last second
bool window_moving();      // the small window is being dragged or sized with the mouse
// The small window picked with the controller (Practice menu, Move and resize): its grip and style
// menu show. Set here for this game's own window, or sent to the room's match window.
bool window_selected();
void window_set_selected(bool on);
void window_send_selected(void* window, bool on);
// The game picture's shape in this window (width / height, as the overlay draws it): the room's small
// window goes into the black bar beside a 4:3 picture.
void window_note_picture_aspect(float aspect);
float window_picture_aspect();
void* window_of_process(uint32_t pid);
#ifndef _WIN32
// Linux video/render thread: uses page visibility after X11 adoption, SDL otherwise.
bool window_visible();
#endif
#ifdef MELEE_LINUX_X11_EMBED
// Successful X11 adoption (false after graceful standalone fallback).
bool window_embedded();
#endif
#ifdef __APPLE__
void window_prefer_simple_fullscreen();   // macOS: non-Spaces full screen; before window_create
void window_defer_fullscreen_show();
#endif
#ifndef _WIN32
void window_presented(uint64_t frame);    // SDL hosts (Mac, Linux): after each swap; the window test knobs
#endif
// Resize the client area, as the resolution picker in the PC settings panel does. Ignored while
// the window is fullscreen (there the client area is the monitor) and clamped to the monitor.
void window_set_client_size(int w, int h);
bool window_take_fullscreen_toggle();   // true once per Alt+Enter press in the game window
// True once each time the game window comes back to the front: exclusive fullscreen, which Windows
// leaves on Alt+Tab, is entered again then.
bool window_take_activation();
bool window_take_escape();              // true once per game Esc press (also F1 under YouGame; Shift+Esc is app-only)
// Host-specific menu shortcut hint. Plain Esc also opens/backs the embedded game menu;
// Shift+Esc belongs to the app.
const char* window_menu_key();
bool window_take_settings_toggle();     // true once per F1 press (auto-repeat ignored), however late it is read
bool window_take_legacy_settings_toggle(); // true once per F11 press (auto-repeat ignored)
int window_take_trace_mark();             // once per mark key press, its kind: 1 plain (F8), 2 looked wrong (F6), 3 input wrong (F7), 4 sounded wrong (F9); else 0
int window_take_settings_controller_port(); // adapter port that last opened settings with Z+Start, or -1
bool window_take_practice_toggle();     // true once per Tab press (auto-repeat ignored)
double window_refresh_rate();
void window_destroy();
void window_set_title(const wchar_t* title);
// Start of every window title: the game, its version and the engine running it ("Source Port" or
// "Static Recomp"), so the title bar and any screenshot of it say which build is playing. main()
// sets it once before the window opens; every title the runtime writes begins with it.
inline std::wstring& window_title_base() { static std::wstring base = L"Melee Unlocked"; return base; }
// The version and engine ("Melee Unlocked 0.9.120  |  Source Port"): only in the title while the
// Performance overlay is on, with the display rates after it (threaded_backend.cpp).
inline std::wstring& window_title_debug() { static std::wstring debug = L"Melee Unlocked"; return debug; }
// A small progress panel over the game window for startup work that holds the first frame back
// (compiling the pipeline cache on the first launch of a new version). Call from the thread that
// owns the game window; nothing is shown for a hidden window.
void loading_show(const wchar_t* what, size_t done, size_t total);
void loading_close();
// Runs the renderer's startup (DLSS, the graphics device, shaders) on a worker while the calling thread,
// the window's own, keeps answering the window's messages and drawing the panel above; returns when it
// is done and rethrows what it threw. Only in an app-backed launch (the YouGame app hosts or overlays the
// game); any other launch, any other thread, or a second call inside one, runs it in place.
void window_responsive(const std::function<void()>& work);
bool window_closed();
void window_client_size(int* w, int* h);
// Scripted input: text file with lines "FRAME BUTTON+BUTTON [sx=N] [sy=N] [cx=N] [cy=N]"; state holds
// until the next line. Buttons: A B X Y Z L R START DU DD DL DR. A line with only a frame releases all.
bool input_load_script(const char* path);
void input_mark_match_start();   // online match reached frame 1: `@match` script sections begin now
}  // namespace host

#ifdef YOUGAME_AGENT_TEST_HOOK
namespace host { void input_console_script(PadState out[4], int local_port); }
#endif
