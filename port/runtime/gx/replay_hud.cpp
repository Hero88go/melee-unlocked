// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_hud.h"

#include "practice_menu.h"
#include "replay_coach.h"
#include "replay_control.h"
#include "imgui.h"

#include <algorithm>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace replay_hud {
namespace {

// GameCube buttons as the pads report them.
constexpr uint16_t kDLeft = 0x0001, kDRight = 0x0002, kDDown = 0x0004, kDUp = 0x0008, kZ = 0x0010, kA = 0x0100,
                   kX = 0x0400, kY = 0x0800, kStart = 0x1000;
constexpr int32_t kLead = 60;            // a tip is jumped to a second before it happens
constexpr int32_t kJump = 5 * 60;
constexpr double kHoldBack = 0.6;        // Start held this long goes back to the replay
constexpr double kResultSeconds = 2.0;   // the try's result shows this long before the next try

struct State {
  int port = -1;                 // the player A takes over
  uint16_t prev = 0xFFFF;        // buttons last frame (all held at first: nothing held is a press)
  bool panel = false, panel_paused = false;
  bool start_was = false;
  double start_at = 0.0;
  bool start_used = false;       // the hold already went back
  int tries = 0;
  int32_t live_frame = replay_control::kNoFrame;
  double ended_at = 0.0;         // the try ran out: its result shows until kResultSeconds later
  std::string result_head, result_value, result_detail;
  int result_sign = 0;
  std::vector<replay_coach::Moment> moments;
  int moments_port = -1;
  size_t events_seen = (size_t)-1;
  double moments_at = -1.0;
};
State g;

ImU32 rgba(int r, int gr, int b, float a) { return IM_COL32(r, gr, b, (int)(255 * std::clamp(a, 0.0f, 1.0f))); }

void refresh_moments(double now) {
  if (g.moments_port == g.port && now - g.moments_at < 0.5) return;
  const std::vector<replay_coach::Event> all = replay_coach::events();
  if (g.moments_port == g.port && all.size() == g.events_seen) { g.moments_at = now; return; }
  g.moments = replay_coach::moments(all, g.port);
  g.moments_port = g.port;
  g.events_seen = all.size();
  g.moments_at = now;
}

int first_port(uint32_t ports) {
  for (int p = 0; p < 4; ++p) if (ports >> p & 1) return p;
  return 0;
}

int next_port(uint32_t ports, int from, int dir) {
  for (int i = 1; i <= 4; ++i) {
    const int p = (from + dir * i + 8) % 4;
    if (ports >> p & 1) return p;
  }
  return from;
}

ImU32 marker_color(replay_coach::Moment::Type t, float a) {
  switch (t) {
    case replay_coach::Moment::Unsafe: return rgba(226, 54, 66, a);
    case replay_coach::Moment::LCancel: return rgba(226, 176, 32, a);
    case replay_coach::Moment::Combo: return rgba(60, 124, 236, a);
    default: return rgba(150, 156, 170, a);
  }
}

// ---- the look: one card style, one type scale, fixed places ----
// Everything is placed in units of the game picture (u = picture height / 720), 24 from its edges,
// clear of Melee's own timer (top middle) and percents (bottom).
constexpr float kMargin = 24, kPad = 14, kRadius = 12, kCardW = 272;
constexpr float kCaption = 11, kBody = 14, kTitle = 19, kValue = 22;

ImU32 c_fill(float a) { return rgba(14, 16, 22, 0.84f * a); }
ImU32 c_line(float a) { return rgba(255, 255, 255, 0.10f * a); }
ImU32 c_caption(float a) { return rgba(150, 158, 178, a); }
ImU32 c_body(float a) { return rgba(214, 218, 230, a); }
ImU32 c_title(float a) { return rgba(246, 247, 250, a); }
ImU32 c_value(int sign, float a) {
  return sign > 0 ? rgba(108, 216, 124, a) : sign < 0 ? rgba(255, 102, 90, a) : c_title(a);
}
ImU32 c_live(float a) { return rgba(226, 52, 68, a); }

void card(ImDrawList* dl, float x0, float y0, float x1, float y1, float u, float a) {
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), c_fill(a), kRadius * u);
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), c_line(a), kRadius * u, 0, 1.0f * u);
}

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}
float tw(float px, const std::string& s) { return practice_menu::text_width(px, s.c_str()); }
void say(ImDrawList* dl, float x, float y, float px, ImU32 c, const std::string& s) {
  practice_menu::text(dl, x, y, px, c, s.c_str(), 0.0f, false);
}
// Text cut with "..." to fit `width`.
std::string fit_to(const std::string& s, float px, float width) {
  if (tw(px, s) <= width) return s;
  std::string t = s;
  while (!t.empty() && tw(px, t + "...") > width) t.pop_back();
  return t + "...";
}
// A small rounded label, "LIVE".
float pill(ImDrawList* dl, float x, float y, float u, const char* s, ImU32 bg, float a) {
  const float w = tw(kCaption * u, s) + 14 * u, h = 17 * u;
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, h * 0.5f);
  say(dl, x + 7 * u, y + 3 * u, kCaption * u, rgba(255, 255, 255, a), s);
  return w;
}

constexpr int32_t kDropGrace = 30;          // half a second after the opponent can act again
constexpr int32_t kNoHitLimit = 10 * 60;    // a try that lands nothing ends after ten seconds

int32_t try_limit(int length) { return length == 1 ? 10 * 60 : length == 2 ? 20 * 60 : 0; }

// The live player's combo this try (measured by the game, not read from the file): whether a hit
// landed, and the frame the opponent could act again after the last one (Training Mode's "the combo
// dropped"). The game reports a hit once both can act, so a combo reports when it ends.
struct Combo { int hits = 0; int32_t free_at = 0; };
Combo combo_of(int port, int32_t from, int32_t to) {
  Combo c;
  int32_t last = INT32_MIN;
  for (const replay_coach::Event& e : replay_coach::events()) {
    if (e.from_file || e.actor != port || e.kind != replay_coach::Kind::Hit || e.frame < from || e.frame > to) continue;
    ++c.hits;
    if (e.frame >= last) {
      last = e.frame;
      c.free_at = e.frame + (e.free_at > 0 ? e.free_at : 30);
    }
  }
  return c;
}

// What the player did since taking over: the last frame data reading.
void capture_result(int port, int32_t from, int32_t to) {
  g.result_head = "Try " + std::to_string(g.tries);
  g.result_value.clear();
  g.result_detail.clear();
  g.result_sign = 0;
  const replay_coach::Event* last = nullptr;
  const std::vector<replay_coach::Event> all = replay_coach::events();
  for (const replay_coach::Event& e : all) {
    if (e.from_file || e.frame < from || e.frame > to || e.actor != port) continue;
    if (e.kind == replay_coach::Kind::Hit || e.kind == replay_coach::Kind::Shield) last = &e;
  }
  if (last) {
    const replay_coach::Readout r = replay_coach::readout(*last);
    g.result_value = r.head + "  " + r.value;
    g.result_sign = r.sign;
    g.result_detail = r.detail;
  }
}

// Tests: MELEE_TEST_REPLAY_PRESS="<replay frame>:<button>,..." presses A, X, Y, Z, S (Start) or U D L R
// (the D-pad) once each when playback reaches that frame (hidden runs have no controller here).
uint16_t test_presses(int32_t frame) {
  struct Press { int32_t frame; uint16_t button; bool fired; };
  static std::vector<Press> list;
  static bool parsed = false;
  if (!parsed) {
    parsed = true;
    if (const char* v = std::getenv("MELEE_TEST_REPLAY_PRESS")) {
      const std::string all(v);
      for (size_t pos = 0; pos < all.size();) {
        size_t comma = all.find(',', pos);
        if (comma == std::string::npos) comma = all.size();
        const std::string item = all.substr(pos, comma - pos);
        pos = comma + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos || colon + 1 >= item.size()) continue;
        uint16_t b = 0;
        switch (item[colon + 1]) {
          case 'A': b = kA; break; case 'X': b = kX; break; case 'Y': b = kY; break; case 'Z': b = kZ; break;
          case 'S': b = kStart; break; case 'U': b = kDUp; break; case 'D': b = kDDown; break;
          case 'L': b = kDLeft; break; case 'R': b = kDRight; break; default: break;
        }
        if (b) list.push_back({(int32_t)std::strtol(item.c_str(), nullptr, 10), b, false});
      }
    }
  }
  uint16_t out = 0;
  for (Press& p : list)
    if (!p.fired && frame >= p.frame) { p.fired = true; out |= p.button; }
  return out;
}

}  // namespace

void frame(int style, const practice_menu::Frame& f) {
  const replay_control::Status st = replay_control::status();
  if (!st.active) {
    replay_control::set_pad_layer(false);
    g = State{};
    return;
  }
  replay_control::set_pad_layer(true);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float u = (f.game_y1 - f.game_y0) / 720.0f;
  const double now = f.time;
  const uint32_t ports = replay_control::player_ports();
  if (g.port < 0 || !(ports >> g.port & 1)) g.port = first_port(ports);
  const int live = replay_control::takeover_port();
  {
    // Tests: MELEE_TEST_REPLAY_LENGTH=<0..3> sets how a try ends (practice_menu::ReplaySettings::length).
    static bool length_set = false;
    if (!length_set) {
      length_set = true;
      if (const char* v = std::getenv("MELEE_TEST_REPLAY_LENGTH")) practice_menu::replay_settings().length = (uint8_t)std::atoi(v);
    }
  }
  const practice_menu::ReplaySettings& rs = practice_menu::replay_settings();
  replay_control::set_defense(rs.defense);
  const uint16_t held = f.buttons;
  const uint16_t pressed = (uint16_t)((held & ~g.prev) | test_presses(st.frame));
  g.prev = held;
  refresh_moments(now);

  // ---- the settings panel (Start while watching) ----
  if (g.panel) {
    practice_menu::Frame pf = f;
    pf.open = true;
    pf.armed = true;
    if (practice_menu::replay_panel(style, pf).close) {
      g.panel = false;
      g.prev = 0xFFFF;   // the closing press is not a press for the layer
      if (g.panel_paused && st.paused) replay_control::toggle_pause();
    }
    return;
  }

  // ---- input ----
  if (live < 0) {
    g.live_frame = replay_control::kNoFrame;
    g.ended_at = 0.0;
    if (pressed & kDUp) g.port = next_port(ports, g.port, -1);
    if (pressed & kDDown) g.port = next_port(ports, g.port, 1);
    if (pressed & kA) {
      g.tries = 1;
      replay_control::take_over(g.port);
    }
    if (pressed & (kX | kY)) {
      // Previous / next tip from where playback is (a tip's jump lands a second before it).
      const replay_coach::Moment* pick = nullptr;
      for (const replay_coach::Moment& m : g.moments) {
        if ((pressed & kY) && m.frame - kLead > st.frame + 5) { pick = &m; break; }
        if ((pressed & kX) && m.frame - kLead < st.frame - 5) pick = &m;
      }
      if (pick) replay_control::seek_to(pick->frame - kLead);
    }
    if (pressed & kDLeft) replay_control::seek_to(st.frame - kJump);
    if (pressed & kDRight) {
      if (st.paused) replay_control::step_frame();
      else replay_control::seek_to(st.frame + kJump);
    }
    if (pressed & kZ) replay_control::toggle_pause();
    if (pressed & kStart) {
      g.panel = true;
      g.panel_paused = !st.paused;
      if (!st.paused) replay_control::toggle_pause();
      return;
    }
  } else {
    // Live: Start comes from the live port (kept from the game). Tap: retry. Hold: back to the replay.
    const bool start = replay_control::live_start_held() || (held & kStart);
    if (g.live_frame == replay_control::kNoFrame) g.live_frame = replay_control::takeover_frame();
    if (start && !g.start_was) { g.start_at = now; g.start_used = false; }
    if (start && !g.start_used && now - g.start_at >= kHoldBack) {
      g.start_used = true;
      replay_coach::forget_from(g.live_frame);
      replay_control::stop_takeover();
    }
    if (!start && g.start_was && !g.start_used) {
      ++g.tries;
      g.ended_at = 0.0;
      replay_coach::forget_from(g.live_frame);
      replay_control::retry_takeover();
    }
    g.start_was = start;
    // The try's end: when the combo drops (or a set time), show what happened, then go again.
    const int32_t into = st.frame - g.live_frame;
    const Combo combo = combo_of(live, g.live_frame, st.frame);
    const int32_t limit = try_limit(rs.length);
    const bool over = rs.length == 0 ? (combo.hits ? st.frame >= combo.free_at + kDropGrace : into >= kNoHitLimit)
                                     : (limit && into >= limit);
    if (over && g.ended_at == 0.0 && !st.paused) {
      g.ended_at = now;
      capture_result(live, g.live_frame, st.frame);
      if (!st.paused) replay_control::toggle_pause();
    }
    if (g.ended_at > 0.0 && now - g.ended_at >= kResultSeconds) {
      g.ended_at = 0.0;
      ++g.tries;
      replay_coach::forget_from(g.live_frame);
      replay_control::retry_takeover();
    }
  }

  // ---- drawing ----
  const float x0 = f.game_x0, x1 = f.game_x1, y0 = f.game_y0, y1 = f.game_y1;
  const float M = kMargin * u, P = kPad * u, W = kCardW * u, inner = W - 2 * P;
  const std::string name = replay_control::player_name(live >= 0 ? live : g.port);

  // Top left: the player card. Who A takes over (or LIVE), then the latest frame data reading.
  {
    const float cx = x0 + M, cy = y0 + M;
    // The latest reading fades in under a divider and out after five seconds.
    replay_coach::Event ev;
    double age = 0.0;
    float ra = 0.0f;
    std::vector<std::string> detail;
    replay_coach::Readout r;
    if (replay_coach::latest(ev, age) && age < 5.0) {
      ra = std::clamp((float)(5.0 - age) / 0.6f, 0.0f, 1.0f);
      r = replay_coach::readout(ev);
      detail = practice_menu::wrap_text(r.detail, kBody * u - 1 * u, inner);
      if (detail.size() > 2) detail.resize(2);
    }
    const float head_h = 22 * u + 28 * u;
    const float reading_h = ra > 0 ? (10 + 26 + 19 * (float)detail.size()) * u : 0.0f;
    card(dl, cx, cy, cx + W, cy + P + head_h + reading_h + P - 6 * u, u, 1);
    float y = cy + P;
    const int32_t limit = try_limit(rs.length);
    std::string right;
    if (live >= 0) {
      pill(dl, cx + P, y - 2 * u, u, "LIVE", c_live(1), 1);
      right = "TRY " + std::to_string(std::max(1, g.tries));
    } else {
      say(dl, cx + P, y, kCaption * u, c_caption(1), "TAKE OVER");
      right = "D-PAD  CHANGE";
    }
    say(dl, cx + W - P - tw(kCaption * u, right), y, kCaption * u, c_caption(1), right);
    y += 22 * u;
    say(dl, cx + P, y, kTitle * u, c_title(1), name);
    if (live >= 0 && limit) {
      char left[16];
      std::snprintf(left, sizeof left, "%d s", std::max(0, (limit - (st.frame - g.live_frame) + 59) / 60));
      say(dl, cx + W - P - tw(kTitle * u, left), y, kTitle * u, c_title(1), left);
    }
    y += 28 * u;
    if (ra > 0) {
      dl->AddLine(ImVec2(cx + P, y), ImVec2(cx + W - P, y), c_line(ra), 1.0f * u);
      y += 10 * u;
      const float vw = tw(kValue * u, r.value);
      say(dl, cx + P, y + 3 * u, kBody * u + 2 * u, c_body(ra), fit_to(r.head, kBody * u + 2 * u, inner - vw - 8 * u));
      say(dl, cx + W - P - vw, y - 2 * u, kValue * u, c_value(r.sign, ra), r.value);
      y += 26 * u;
      for (const std::string& line : detail) {
        say(dl, cx + P, y, kBody * u - 1 * u, c_caption(ra), line);
        y += 19 * u;
      }
    }
  }

  // Top right: the tip at its moment (watching).
  if (live < 0 && rs.tips) {
    for (size_t i = 0; i < g.moments.size(); ++i) {
      const replay_coach::Moment& m = g.moments[i];
      if (st.frame < m.frame - 20 || st.frame > m.frame + 150) continue;
      const float a = std::clamp((float)(st.frame - (m.frame - 20)) / 10.0f, 0.0f, 1.0f) *
                      std::clamp((float)(m.frame + 150 - st.frame) / 15.0f, 0.0f, 1.0f);
      const float cx = x1 - M - W, cy = y0 + M;
      std::vector<std::string> body = practice_menu::wrap_text(m.advice, kBody * u, inner);
      if (body.size() > 4) body.resize(4);
      const float h = P + 22 * u + 28 * u + 19 * u * (float)body.size() + P - 4 * u;
      card(dl, cx, cy, cx + W, cy + h, u, a);
      float y = cy + P;
      char k[48];
      std::snprintf(k, sizeof k, "TIP %d OF %d", (int)i + 1, (int)g.moments.size());
      say(dl, cx + P, y, kCaption * u, rgba(255, 204, 64, a), k);
      const std::string when = replay_coach::clock(m.frame);
      say(dl, cx + W - P - tw(kCaption * u, when), y, kCaption * u, c_caption(a), when);
      y += 22 * u;
      const float vw = m.value.empty() ? 0.0f : tw(kTitle * u, m.value) + 8 * u;
      say(dl, cx + P, y, kTitle * u, c_title(a), fit_to(m.what, kTitle * u, inner - vw));
      if (!m.value.empty()) say(dl, cx + W - P - tw(kTitle * u, m.value), y, kTitle * u, c_value(m.sign, a), m.value);
      y += 28 * u;
      for (const std::string& line : body) {
        say(dl, cx + P, y, kBody * u, c_body(a), line);
        y += 19 * u;
      }
      break;
    }
  }

  // Middle: a try's result when it ended.
  if (live >= 0 && g.ended_at > 0.0) {
    const float w = 320 * u, cx = (x0 + x1) * 0.5f - w * 0.5f, cy = y0 + (y1 - y0) * 0.30f;
    const std::vector<std::string> detail = practice_menu::wrap_text(g.result_detail, kBody * u, w - 2 * P);
    const float h = P + 22 * u + 30 * u + 19 * u * (float)detail.size() + 26 * u + P - 6 * u;
    card(dl, cx, cy, cx + w, cy + h, u, 1);
    float y = cy + P;
    say(dl, cx + P, y, kCaption * u, c_caption(1), upper(g.result_head));
    y += 22 * u;
    say(dl, cx + P, y, kValue * u, g.result_value.empty() ? c_title(1) : c_value(g.result_sign, 1),
        g.result_value.empty() ? std::string("No hits this try") : g.result_value);
    y += 30 * u;
    for (const std::string& line : detail) { say(dl, cx + P, y, kBody * u, c_body(1), line); y += 19 * u; }
    char next[32];
    std::snprintf(next, sizeof next, "NEXT TRY IN %d S", std::max(1, (int)(kResultSeconds - (now - g.ended_at) + 0.99)));
    say(dl, cx + P, y + 6 * u, kCaption * u, c_caption(1), next);
  }

  // Bottom: the dock, the timeline of this player's tips over one row of buttons.
  {
    const char* spec = live >= 0 ? "S:Retry|S:Hold to go back to the replay"
                       : st.paused ? "A:Take over|X:Last tip|Y:Next tip|D:Frame|Z:Play|S:Settings"
                                   : "A:Take over|X:Last tip|Y:Next tip|D:5 s|Z:Pause|R:Fast|S:Settings";
    const float ls = u * 0.8f;
    const float lw = practice_menu::legend(dl, 0, 0, ls, spec, 1.0f, false);
    const bool timeline = live < 0 && rs.tips && st.last_frame > st.first_frame;
    const float dw = std::min(std::max(lw + 2 * P + 8 * u, (x1 - x0) * 0.62f), (x1 - x0) - 2 * M);
    const float dh = (timeline ? 50 : 36) * u;
    const float dx = (x0 + x1) * 0.5f - dw * 0.5f, dy = y1 - 10 * u - dh;
    card(dl, dx, dy, dx + dw, dy + dh, u, 1);
    if (timeline) {
      const float tx0 = dx + P + 4 * u, tx1 = dx + dw - P - 4 * u, ty = dy + 12 * u;
      const auto at = [&](int32_t frame) {
        return tx0 + (tx1 - tx0) * std::clamp((float)(frame - st.first_frame) / (float)(st.last_frame - st.first_frame), 0.0f, 1.0f);
      };
      dl->AddRectFilled(ImVec2(tx0, ty - 1.5f * u), ImVec2(tx1, ty + 1.5f * u), rgba(255, 255, 255, 0.16f), 1.5f * u);
      dl->AddRectFilled(ImVec2(tx0, ty - 1.5f * u), ImVec2(at(st.frame), ty + 1.5f * u), rgba(255, 255, 255, 0.55f), 1.5f * u);
      for (const replay_coach::Moment& m : g.moments) {
        const float mx = at(m.frame);
        dl->AddRectFilled(ImVec2(mx - 1.2f * u, ty - 5 * u), ImVec2(mx + 1.2f * u, ty + 5 * u), marker_color(m.type, 0.95f), 1.2f * u);
      }
      dl->AddCircleFilled(ImVec2(at(st.frame), ty), 4.5f * u, rgba(255, 255, 255, 1), 16);
    }
    practice_menu::legend(dl, (x0 + x1) * 0.5f - lw * 0.5f, dy + dh - 18 * u, ls, spec, 1.0f);
  }
}

}  // namespace replay_hud
