// Host window services.
#pragma once
#include <functional>
#include <cstdint>
#include <string>

namespace host {
using MessageCallback = std::function<bool(void*, uint32_t, uintptr_t, intptr_t)>;
void window_set_message_callback(MessageCallback cb);
void window_input_capture(bool capture);
struct PadState;
bool window_ui_gamecube_pad(PadState& pad);
// Last routed state for every in-game port, captured before UI input suppression. This lets an
// overlay follow the controller port selected in Training instead of assuming port 1.
bool window_ui_pads(PadState pads[4]);
using ResizeCallback = std::function<void(int, int)>;
void* window_create(int w, int h, const wchar_t* title, bool visible = true);
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
// Resize the client area, as the resolution picker in the PC settings panel does. Ignored while
// the window is fullscreen (there the client area is the monitor) and clamped to the monitor.
void window_set_client_size(int w, int h);
bool window_take_fullscreen_toggle();   // true once per Alt+Enter press in the game window
bool window_take_escape();              // true once per Esc press, like the F1 toggle
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
// A small progress panel over the game window for startup work that holds the first frame back
// (compiling the pipeline cache on the first launch of a new version). Call from the thread that
// owns the game window; nothing is shown for a hidden window.
void loading_show(const wchar_t* what, size_t done, size_t total);
void loading_close();
bool window_closed();
void window_client_size(int* w, int* h);
// Scripted input: text file with lines "FRAME BUTTON+BUTTON [sx=N] [sy=N] [cx=N] [cy=N]"; state holds
// until the next line. Buttons: A B X Y Z L R START DU DD DL DR. A line with only a frame releases all.
bool input_load_script(const char* path);
void input_mark_match_start();   // online match reached frame 1: `@match` script sections begin now
}  // namespace host
