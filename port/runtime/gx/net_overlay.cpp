// SPDX-License-Identifier: GPL-2.0-or-later
#include "net_overlay.h"
#include "net_trace.h"
#include "host.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace net_overlay {
namespace {

constexpr size_t kWindow = 600;        // 10 seconds of ticks
constexpr float kFrameMs = 16.683f;    // one tick at the game's rate
constexpr float kTimeScaleMs = 50.0f;  // top of the frame time plot
constexpr float kPingScaleMs = 200.0f; // top of the ping line's scale
constexpr int kDepthScale = 7;         // deepest rollback the game makes

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_replay{false};   // a replay shown as it was played: see set_replay
std::mutex g_note_mutex;
std::string g_note;
double g_note_until = 0.0;

bool test_mode() {
  static const bool on = std::getenv("MELEE_TEST_NET_OVERLAY") != nullptr;
  return on;
}

// Sample data for screenshots (MELEE_TEST_NET_OVERLAY): a match with a hitch, a wait for the other
// player, a spread of rollbacks, one shed and one advanced frame.
size_t sample(net_trace::Record* out) {
  for (size_t i = 0; i < kWindow; ++i) {
    net_trace::Record r;
    r.frame = (int32_t)(1000 + i);
    r.wall = i * (kFrameMs / 1000.0) + (i >= 200 ? 0.020 : 0.0);
    r.sim_ms = 2.0f + (float)(i % 7) * 0.3f + (i == 200 ? 24.0f : 0.0f);
    r.ping_ms = (uint16_t)(48 + (i * 13) % 9 + (i > 330 && i < 420 ? 60 : 0));
    r.presents = 1;
    if (i >= 340 && i < 372) { r.flags |= net_trace::kWait; r.wait_frames = (uint16_t)(i - 339); }
    if (i % 23 == 5) { r.rollbacks = 1; r.rollback_depth = (uint8_t)(1 + (i / 23) % kDepthScale); }
    if (i == 90) r.flags |= net_trace::kShed;
    if (i == 480) r.flags |= net_trace::kAdvance;
    out[i] = r;
  }
  return kWindow;
}

}  // namespace

void set_enabled(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_replay(bool on) { g_replay.store(on, std::memory_order_relaxed); }
void set_replay_note(const char* text, double seconds) {
  std::lock_guard<std::mutex> lock(g_note_mutex);
  g_note = text ? text : "";
  g_note_until = host::now_seconds() + seconds;
}

void draw(float width, float height) {
  net_trace::note_present();
  const bool test = test_mode();
  const bool replay = g_replay.load(std::memory_order_relaxed);
  if (!test && !replay && !g_enabled.load(std::memory_order_relaxed)) return;
  static net_trace::Record recs[kWindow];   // UI thread only
  size_t n = net_trace::snapshot(recs, kWindow);
  // Between matches the ring keeps the last match's end; the overlay leaves with the match. A
  // replay's records carry the session's clock, not this run's, and stay while the replay plays.
  if (!replay && n > 0 && host::now_seconds() - recs[n - 1].wall > 2.0) n = 0;
  if (n == 0 && test) n = sample(recs);
  if (n == 0) return;
  std::string note;
  if (replay) {
    std::lock_guard<std::mutex> lock(g_note_mutex);
    if (host::now_seconds() < g_note_until) note = g_note;
  }

  const float text_h = ImGui::GetTextLineHeight();
  const float panel_w = std::min(640.0f, width - 24.0f);
  const float panel_h = 2.0f * text_h + 116.0f + (note.empty() ? 0.0f : text_h + 4.0f);
  if (panel_w < 240.0f || height < panel_h + 24.0f) return;
  const ImVec2 o(width - panel_w - 12.0f, 12.0f);   // top right: the FPS and ping lines are top left
  const float plot_x = o.x + 8.0f, plot_w = panel_w - 16.0f;
  const float time_y = o.y + 8.0f + text_h + 4.0f, time_h = 64.0f;   // frame time, waits, sync, ping
  const float roll_y = time_y + time_h + 4.0f, roll_h = 24.0f;       // rollbacks
  const float col_w = plot_w / (float)kWindow;
  const float bar_w = std::max(1.0f, col_w);

  const ImU32 back = IM_COL32(10, 12, 18, 175), plot_back = IM_COL32(20, 22, 28, 170);
  const ImU32 text = IM_COL32(236, 240, 246, 255), dim = IM_COL32(160, 168, 184, 255);
  const ImU32 interval_col = IM_COL32(90, 110, 140, 255), work_col = IM_COL32(120, 220, 140, 255);
  const ImU32 slow_col = IM_COL32(250, 214, 60, 255), wait_col = IM_COL32(230, 70, 70, 220);
  const ImU32 shed_col = IM_COL32(250, 214, 60, 255), advance_col = IM_COL32(120, 200, 255, 255);
  const ImU32 roll_col = IM_COL32(255, 150, 60, 255), ping_col = IM_COL32(0, 255, 255, 200);

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(o, ImVec2(o.x + panel_w, o.y + panel_h), back, 6.0f);
  dl->AddRectFilled(ImVec2(plot_x, time_y), ImVec2(plot_x + plot_w, time_y + time_h), plot_back);
  dl->AddRectFilled(ImVec2(plot_x, roll_y), ImVec2(plot_x + plot_w, roll_y + roll_h), plot_back);

  // Newest tick at the right edge; a match younger than the window fills in from the right.
  auto column = [&](size_t i) { return plot_x + col_w * (float)(kWindow - n + i); };
  unsigned rollbacks = 0, deepest = 0, waited = 0, shed = 0, advanced = 0;
  float prev_ping_x = 0, prev_ping_y = 0;
  for (size_t i = 0; i < n; ++i) {
    const net_trace::Record& r = recs[i];
    const float x = column(i);
    if (r.flags & net_trace::kWait) {
      // The game stood still on this tick: nothing else to say about it.
      ++waited;
      dl->AddRectFilled(ImVec2(x, time_y), ImVec2(x + bar_w, time_y + time_h), wait_col);
    } else {
      // Time since the tick before (a late tick is a tall bar), and inside it the tick's own work.
      const float interval = i > 0 ? (float)((r.wall - recs[i - 1].wall) * 1000.0) : kFrameMs;
      const float ih = time_h * std::clamp(interval / kTimeScaleMs, 0.0f, 1.0f);
      const float wh = time_h * std::clamp(r.sim_ms / kTimeScaleMs, 0.0f, 1.0f);
      dl->AddRectFilled(ImVec2(x, time_y + time_h - ih), ImVec2(x + bar_w, time_y + time_h),
                        interval > kFrameMs * 1.5f ? slow_col : interval_col);
      dl->AddRectFilled(ImVec2(x, time_y + time_h - wh), ImVec2(x + bar_w, time_y + time_h), work_col);
    }
    if (r.flags & (net_trace::kShed | net_trace::kAdvance)) {
      const bool is_shed = (r.flags & net_trace::kShed) != 0;
      ++(is_shed ? shed : advanced);
      const ImU32 col = is_shed ? shed_col : advance_col;
      dl->AddRectFilled(ImVec2(x - 1.0f, time_y), ImVec2(x + 2.0f, time_y + 10.0f), col);
      dl->AddTriangleFilled(ImVec2(x - 4.0f, time_y + 10.0f), ImVec2(x + 5.0f, time_y + 10.0f),
                            ImVec2(x + 0.5f, time_y + 16.0f), col);
    }
    if (r.rollbacks) {
      rollbacks += r.rollbacks;
      deepest = std::max<unsigned>(deepest, r.rollback_depth);
      // Height is the depth in frames; a rollback of unknown depth is a short mark.
      const float depth = r.rollback_depth ? (float)std::min<int>(r.rollback_depth, kDepthScale) : 0.5f;
      const float rh = roll_h * depth / (float)kDepthScale;
      dl->AddRectFilled(ImVec2(x, roll_y + roll_h - rh), ImVec2(x + std::max(2.0f, bar_w), roll_y + roll_h), roll_col);
    }
    const float px = x + bar_w * 0.5f;
    const float py = time_y + time_h - time_h * std::clamp((float)r.ping_ms / kPingScaleMs, 0.0f, 1.0f);
    if (i > 0) dl->AddLine(ImVec2(prev_ping_x, prev_ping_y), ImVec2(px, py), ping_col, 1.0f);
    prev_ping_x = px; prev_ping_y = py;
  }
  // One tick at the game's rate, as a guide across the frame time plot.
  const float guide_y = time_y + time_h - time_h * (kFrameMs / kTimeScaleMs);
  dl->AddLine(ImVec2(plot_x, guide_y), ImVec2(plot_x + plot_w, guide_y), IM_COL32(210, 215, 225, 90), 1.0f);

  const net_trace::Record& last = recs[n - 1];
  char line[160];
  std::snprintf(line, sizeof line, "%s   ping %u ms   rollbacks %u (deepest %u)   waited %u frames   shed %u   advanced %u",
                replay ? "As it was played" : "Network and timing",
                (unsigned)last.ping_ms, rollbacks, deepest, waited, shed, advanced);
  dl->PushClipRect(o, ImVec2(o.x + panel_w, o.y + panel_h), true);
  dl->AddText(ImVec2(plot_x, o.y + 6.0f), text, line);
  // Legend, each word beside the color of its mark.
  struct Key { const char* label; ImU32 col; };
  const Key keys[] = {
      {"frame time", interval_col}, {"work", work_col}, {"waiting for inputs", wait_col}, {"rollback (height: frames)", roll_col},
      {"shed", shed_col}, {"advanced", advance_col}, {"ping (top: 200 ms)", ping_col},
  };
  float kx = plot_x;
  const float ky = roll_y + roll_h + 4.0f;
  for (const Key& k : keys) {
    dl->AddRectFilled(ImVec2(kx, ky + 3.0f), ImVec2(kx + 8.0f, ky + text_h - 3.0f), k.col);
    dl->AddText(ImVec2(kx + 11.0f, ky), dim, k.label);
    kx += 11.0f + ImGui::CalcTextSize(k.label).x + 10.0f;
  }
  if (!note.empty()) dl->AddText(ImVec2(plot_x, ky + text_h + 4.0f), slow_col, note.c_str());
  dl->PopClipRect();
}

}  // namespace net_overlay
