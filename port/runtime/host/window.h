// Host window services.
#pragma once
#include <functional>

namespace host {
using ResizeCallback = std::function<void(int, int)>;
void* window_create(int w, int h, const wchar_t* title);
void window_set_resize_callback(ResizeCallback cb);
void window_pump();
void window_set_title(const wchar_t* title);
bool window_closed();
void window_client_size(int* w, int* h);
// Scripted input: text file with lines "FRAME BUTTON+BUTTON [sx=N] [sy=N] [cx=N] [cy=N]"; state holds
// until the next line. Buttons: A B X Y Z L R START DU DD DL DR. A line with only a frame releases all.
bool input_load_script(const char* path);
}  // namespace host
