// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_bar.h"
#include "replay_control.h"
#include "host.h"
#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

namespace replay_bar {
namespace {

constexpr double kShowSeconds = 3.0;   // how long the bar stays after the last mouse move or control
std::atomic<bool> g_cursor{false};

// Frames as minutes and seconds of play.
void clock_text(int frames, char* out, size_t size) {
  const int seconds = std::max(frames, 0) / 60;
  std::snprintf(out, size, "%d:%02d", seconds / 60, seconds % 60);
}

}  // namespace

bool wants_cursor() { return g_cursor.load(std::memory_order_relaxed); }
bool active() { return replay_control::active(); }

void draw(float width, float height, bool menu_open) {
  const replay_control::Status st = replay_control::status();
  if (!st.active) {
    g_cursor.store(false, std::memory_order_relaxed);
    return;
  }
  replay_control::set_ui_busy(menu_open);
  const ImGuiIO& io = ImGui::GetIO();
  const double now = host::now_seconds();
  static ImVec2 last_mouse(-1.0f, -1.0f);   // UI thread only
  static double mouse_time = 0.0;
  if (ImGui::IsMousePosValid(&io.MousePos) &&
      (std::fabs(io.MousePos.x - last_mouse.x) > 1.0f || std::fabs(io.MousePos.y - last_mouse.y) > 1.0f)) {
    if (last_mouse.x >= 0.0f) mouse_time = now;   // the first position seen is not a move
    last_mouse = io.MousePos;
  }
  const bool mouse_recent = mouse_time > 0.0 && now - mouse_time < kShowSeconds;

  // H hides the bar for good (for recording): it stays away while paused, seeking or when the
  // mouse moves, and every control keeps working. H again brings it back. Read here, on the thread
  // that draws it, only while the game window has the focus and no menu has the keys.
  static bool hidden = false, h_was_down = true;   // UI thread only; "true" so a held H at start is no press
  static double hidden_note = 0.0;
  bool h_down = false;
  if (!menu_open) {
    const HWND front = GetForegroundWindow();
    DWORD pid = 0;
    if (front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId())
      h_down = (GetAsyncKeyState('H') & 0x8000) != 0;
  }
  if (h_down && !h_was_down) {
    hidden = !hidden;
    hidden_note = hidden ? now : 0.0;
    if (!hidden) mouse_time = now;   // show it at once
  }
  h_was_down = h_down || menu_open;
  if (hidden) {
    g_cursor.store(false, std::memory_order_relaxed);
    if (hidden_note > 0.0 && now - hidden_note < 2.0) {
      // Said once, briefly, so the key that brings it back is not a secret.
      const char* note = "Replay bar hidden. H: show it again";
      const ImVec2 size = ImGui::CalcTextSize(note);
      const ImVec2 at(16.0f, height - size.y - 20.0f);
      ImDrawList* nl = ImGui::GetForegroundDrawList();
      nl->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 5.0f), ImVec2(at.x + size.x + 8.0f, at.y + size.y + 5.0f),
                        IM_COL32(10, 12, 18, 190), 5.0f);
      nl->AddText(at, IM_COL32(236, 240, 246, 255), note);
    }
    return;
  }
  const bool visible = !menu_open && (st.paused || st.seeking || mouse_recent ||
                                      (st.last_input > 0.0 && now - st.last_input < kShowSeconds));
  g_cursor.store(visible && mouse_recent, std::memory_order_relaxed);
  if (!visible) return;

  const float text_h = ImGui::GetTextLineHeight();
  const float bar_w = width - 32.0f, bar_h = 2.0f * text_h + 34.0f;
  if (bar_w < 320.0f || height < bar_h + 40.0f) return;
  const ImVec2 o(16.0f, height - bar_h - 12.0f);
  const float line_x0 = o.x + 12.0f, line_x1 = o.x + bar_w - 12.0f;
  const float line_y = o.y + 8.0f + text_h + 9.0f;

  const ImU32 back = IM_COL32(10, 12, 18, 190), track = IM_COL32(70, 76, 92, 255);
  const ImU32 fill = IM_COL32(155, 107, 255, 255), text = IM_COL32(236, 240, 246, 255);
  const ImU32 dim = IM_COL32(160, 168, 184, 255), mark = IM_COL32(250, 214, 60, 255);

  const int total = std::max(1, st.last_frame - st.first_frame);
  const int at = std::clamp(st.frame - st.first_frame, 0, total);
  const float done = (float)at / (float)total;

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(o, ImVec2(o.x + bar_w, o.y + bar_h), back, 6.0f);
  dl->PushClipRect(o, ImVec2(o.x + bar_w, o.y + bar_h), true);

  // Time, then what playback is doing.
  char now_text[16], total_text[16], line[160];
  clock_text(at, now_text, sizeof now_text);
  clock_text(total, total_text, sizeof total_text);
  char state[64];
  if (st.seeking) {
    char target[16];
    clock_text(st.seek_target - st.first_frame, target, sizeof target);
    std::snprintf(state, sizeof state, "Seeking to %s", target);
  } else if (st.paused) std::snprintf(state, sizeof state, "Paused");
  else if (st.fast) std::snprintf(state, sizeof state, "Fast forward 4x");
  else if (st.speed < 0.375) std::snprintf(state, sizeof state, "Slow motion 1/4");
  else if (st.speed < 0.75) std::snprintf(state, sizeof state, "Slow motion 1/2");
  else std::snprintf(state, sizeof state, "Playing");
  std::snprintf(line, sizeof line, "%s / %s     %s%s", now_text, total_text, state,
                st.as_played ? "     shown as it was played" : "");
  dl->AddText(ImVec2(line_x0, o.y + 8.0f), text, line);

  // The progress line. A click on it jumps there.
  const bool over = ImGui::IsMousePosValid(&io.MousePos) && io.MousePos.x >= line_x0 - 4.0f && io.MousePos.x <= line_x1 + 4.0f &&
                    io.MousePos.y >= line_y - 10.0f && io.MousePos.y <= line_y + 10.0f;
  const float half = over ? 3.5f : 2.0f;
  dl->AddRectFilled(ImVec2(line_x0, line_y - half), ImVec2(line_x1, line_y + half), track, half);
  const float done_x = line_x0 + (line_x1 - line_x0) * done;
  dl->AddRectFilled(ImVec2(line_x0, line_y - half), ImVec2(done_x, line_y + half), fill, half);
  dl->AddCircleFilled(ImVec2(done_x, line_y), over ? 7.0f : 5.0f, fill);
  if (st.seeking) {
    const float target_x = line_x0 + (line_x1 - line_x0) *
                                         std::clamp((float)(st.seek_target - st.first_frame) / (float)total, 0.0f, 1.0f);
    dl->AddRectFilled(ImVec2(target_x - 1.0f, line_y - 7.0f), ImVec2(target_x + 1.0f, line_y + 7.0f), mark);
  }
  dl->AddText(ImVec2(line_x0, line_y + 10.0f), dim,
              "Space: pause     Left, Right: 5 seconds (Shift: 30)     Right or . while paused: one frame     "
              "[ ] or Down, Up: speed     Tab or R: fast forward     Home: restart     H: hide this bar");
  dl->PopClipRect();

  if (over) {
    // The time under the pointer, just above the bar.
    const float fraction = std::clamp((io.MousePos.x - line_x0) / (line_x1 - line_x0), 0.0f, 1.0f);
    const int frame = (int)(fraction * (float)total);
    char there[16];
    clock_text(frame, there, sizeof there);
    const float tw = ImGui::CalcTextSize(there).x;
    const ImVec2 at_text(std::clamp(io.MousePos.x - tw * 0.5f, line_x0, line_x1 - tw), o.y - text_h - 8.0f);
    dl->AddRectFilled(ImVec2(at_text.x - 6.0f, at_text.y - 3.0f), ImVec2(at_text.x + tw + 6.0f, at_text.y + text_h + 3.0f), back, 4.0f);
    dl->AddText(at_text, mark, there);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) replay_control::seek_to(st.first_frame + frame);
  }
}

}  // namespace replay_bar

namespace screen_label {
namespace {
std::mutex g_lock;
std::string g_text;
double g_until = 0.0;
}  // namespace

void show(const char* text, double seconds) {
  std::lock_guard<std::mutex> lock(g_lock);
  g_text = text ? text : "";
  g_until = host::now_seconds() + seconds;
}

void draw(float width, float height) {
  std::string text;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_text.empty() || host::now_seconds() >= g_until) return;
    text = g_text;
  }
  const ImVec2 size = ImGui::CalcTextSize(text.c_str());
  const ImVec2 at((width - size.x) * 0.5f, height - size.y - 28.0f);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(at.x - 10.0f, at.y - 6.0f), ImVec2(at.x + size.x + 10.0f, at.y + size.y + 6.0f),
                    IM_COL32(10, 12, 18, 200), 6.0f);
  dl->AddText(at, IM_COL32(236, 240, 246, 255), text.c_str());
}
}  // namespace screen_label
