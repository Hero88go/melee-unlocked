// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_hud.h"

#include "practice_menu.h"
#include "replay_coach.h"
#include "replay_control.h"
#include "imgui.h"

#include <algorithm>
#include <climits>
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

// A dark plate behind text, fading out at its right end like the game's own name plates.
void plate(ImDrawList* dl, float x0, float y0, float x1, float y1, float a) {
  dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), rgba(0, 0, 0, 0.62f * a), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f),
                              rgba(0, 0, 0, 0.62f * a));
}

// The latest frame data reading (hits and shield hits), under the top left label for five seconds.
void draw_reading(ImDrawList* dl, float x, float y, float u) {
  replay_coach::Event ev;
  double age = 0.0;
  if (!replay_coach::latest(ev, age) || age > 5.0) return;
  const float a = std::clamp((float)(5.0 - age) / 0.6f, 0.0f, 1.0f);
  const replay_coach::Readout r = replay_coach::readout(ev);
  const float hp = 20 * u, vp = 26 * u;
  const float head_w = practice_menu::text_width(hp, r.head.c_str());
  // The detail wraps short of the match timer at the top middle.
  const std::vector<std::string> detail = practice_menu::wrap_text(r.detail, 14 * u, 250 * u);
  float w = head_w + 12 * u + practice_menu::text_width(vp, r.value.c_str());
  for (const std::string& line : detail) w = std::max(w, practice_menu::text_width(14 * u, line.c_str()));
  plate(dl, x - 10 * u, y - 4 * u, x + w + 40 * u, y + (34 + 18 * (float)detail.size()) * u, a);
  practice_menu::text(dl, x, y + 4 * u, hp, rgba(240, 240, 244, a), r.head.c_str());
  const ImU32 c = r.sign > 0 ? rgba(110, 230, 120, a) : r.sign < 0 ? rgba(255, 92, 80, a) : rgba(240, 240, 244, a);
  practice_menu::text(dl, x + head_w + 12 * u, y, vp, c, r.value.c_str());
  for (size_t i = 0; i < detail.size(); ++i)
    practice_menu::text(dl, x, y + (34 + 18 * (float)i) * u, 14 * u, rgba(205, 208, 220, a), detail[i].c_str());
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
  const std::string& name = replay_control::player_name(live >= 0 ? live : g.port);
  float label_bottom = y0 + 20 * u;
  if (live >= 0) {
    // LIVE badge, with the countdown when a try has a set length.
    const int32_t limit = try_limit(rs.length);
    std::string badge = "LIVE  " + name;
    char left[16] = "";
    if (limit) std::snprintf(left, sizeof left, "%d", std::max(0, (limit - (st.frame - g.live_frame) + 59) / 60));
    const float bw = practice_menu::text_width(20 * u, badge.c_str()) + (limit ? practice_menu::text_width(28 * u, left) + 18 * u : 0) + 28 * u;
    dl->AddRectFilled(ImVec2(x0 + 22 * u, y0 + 18 * u), ImVec2(x0 + 22 * u + bw, y0 + 58 * u), rgba(214, 40, 58, 0.95f), 6 * u);
    practice_menu::text(dl, x0 + 36 * u, y0 + 28 * u, 20 * u, rgba(255, 255, 255, 1), badge.c_str());
    if (limit) practice_menu::text(dl, x0 + 22 * u + bw - 14 * u - practice_menu::text_width(28 * u, left), y0 + 23 * u, 28 * u, rgba(255, 255, 255, 1), left);
    label_bottom = y0 + 66 * u;
  } else {
    const std::string who = "Take over:  " + name;
    const float w = practice_menu::text_width(18 * u, who.c_str());
    plate(dl, x0 + 12 * u, y0 + 16 * u, x0 + 60 * u + w + 60 * u, y0 + 52 * u, 1.0f);
    practice_menu::text(dl, x0 + 24 * u, y0 + 25 * u, 18 * u, rgba(255, 222, 120, 1), who.c_str());
    practice_menu::legend(dl, x0 + 34 * u + w, y0 + 34 * u, u * 0.8f, "D:", 1.0f);
    label_bottom = y0 + 60 * u;
  }
  draw_reading(dl, x0 + 30 * u, label_bottom + 6 * u, u);

  // The tip banner at its moment (watching).
  if (live < 0 && rs.tips) {
    for (size_t i = 0; i < g.moments.size(); ++i) {
      const replay_coach::Moment& m = g.moments[i];
      if (st.frame < m.frame - 20 || st.frame > m.frame + 150) continue;
      const float a = std::clamp((float)(st.frame - (m.frame - 20)) / 10.0f, 0.0f, 1.0f) *
                      std::clamp((float)(m.frame + 150 - st.frame) / 15.0f, 0.0f, 1.0f);
      char k[64];
      std::snprintf(k, sizeof k, "%s   Tip %zu of %zu", name.c_str(), i + 1, g.moments.size());
      const float tw = std::max({practice_menu::text_width(26 * u, (m.what + "  " + m.value).c_str()),
                                 practice_menu::text_width(15 * u, m.advice.c_str()), 360 * u});
      const float cx = (x0 + x1) * 0.5f, top = y0 + (y1 - y0) * 0.17f;
      const float bw = std::min(tw + 80 * u, (x1 - x0) - 40 * u);
      dl->AddRectFilledMultiColor(ImVec2(cx - bw * 0.5f, top), ImVec2(cx, top + 104 * u), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.8f * a),
                                  rgba(0, 0, 0, 0.8f * a), rgba(0, 0, 0, 0));
      dl->AddRectFilledMultiColor(ImVec2(cx, top), ImVec2(cx + bw * 0.5f, top + 104 * u), rgba(0, 0, 0, 0.8f * a), rgba(0, 0, 0, 0),
                                  rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.8f * a));
      practice_menu::text(dl, cx - practice_menu::text_width(13 * u, k) * 0.5f, top + 6 * u, 13 * u, rgba(255, 207, 58, a), k, 0, false);
      const float ww = practice_menu::text_width(26 * u, m.what.c_str());
      const float vw = m.value.empty() ? 0 : practice_menu::text_width(26 * u, m.value.c_str()) + 14 * u;
      const float lx = cx - (ww + vw) * 0.5f;
      practice_menu::text(dl, lx, top + 24 * u, 26 * u, rgba(255, 255, 255, a), m.what.c_str(), 0.12f);
      if (!m.value.empty())
        practice_menu::text(dl, lx + ww + 14 * u, top + 24 * u, 26 * u,
                            m.sign > 0 ? rgba(110, 230, 120, a) : m.sign < 0 ? rgba(255, 92, 80, a) : rgba(255, 255, 255, a), m.value.c_str());
      const std::vector<std::string> lines = practice_menu::wrap_text(m.advice, 15 * u, std::min(bw - 60 * u, 640 * u));
      for (size_t l = 0; l < lines.size() && l < 2; ++l)
        practice_menu::text(dl, cx - practice_menu::text_width(15 * u, lines[l].c_str()) * 0.5f, top + (58 + 19 * (float)l) * u, 15 * u,
                            rgba(222, 226, 238, a), lines[l].c_str());
      break;
    }
  }

  // The try's result when its time ran out.
  if (live >= 0 && g.ended_at > 0.0) {
    const float cw = 420 * u, cx = (x0 + x1) * 0.5f, top = y0 + (y1 - y0) * 0.3f;
    dl->AddRectFilled(ImVec2(cx - cw * 0.5f, top), ImVec2(cx + cw * 0.5f, top + 104 * u), rgba(6, 10, 22, 0.92f), 10 * u);
    dl->AddRect(ImVec2(cx - cw * 0.5f, top), ImVec2(cx + cw * 0.5f, top + 104 * u), rgba(255, 207, 58, 1), 10 * u, 0, 2 * u);
    practice_menu::text(dl, cx - cw * 0.5f + 20 * u, top + 12 * u, 15 * u, rgba(255, 207, 58, 1), g.result_head.c_str(), 0, false);
    const std::string v = g.result_value.empty() ? std::string("No hits this try") : g.result_value;
    practice_menu::text(dl, cx - cw * 0.5f + 20 * u, top + 36 * u, 24 * u,
                        g.result_sign > 0 ? rgba(110, 230, 120, 1) : g.result_sign < 0 ? rgba(255, 92, 80, 1) : rgba(255, 255, 255, 1), v.c_str(), 0.1f);
    practice_menu::text(dl, cx - cw * 0.5f + 20 * u, top + 72 * u, 14 * u, rgba(207, 213, 228, 1), g.result_detail.c_str(), 0, false);
  }

  // The timeline with this player's tips (watching).
  const float ly = y1 - 26 * u;
  if (live < 0 && rs.tips && st.last_frame > st.first_frame) {
    const float tx0 = x0 + 40 * u, tx1 = x1 - 40 * u, ty = ly - 30 * u;
    const auto at = [&](int32_t frame) {
      return tx0 + (tx1 - tx0) * std::clamp((float)(frame - st.first_frame) / (float)(st.last_frame - st.first_frame), 0.0f, 1.0f);
    };
    dl->AddRectFilled(ImVec2(tx0, ty - 2 * u), ImVec2(tx1, ty + 2 * u), rgba(255, 255, 255, 0.22f), 2 * u);
    dl->AddRectFilled(ImVec2(tx0, ty - 2 * u), ImVec2(at(st.frame), ty + 2 * u), rgba(185, 163, 255, 0.95f), 2 * u);
    for (const replay_coach::Moment& m : g.moments) {
      const float mx = at(m.frame);
      dl->AddRectFilled(ImVec2(mx - 1.5f * u, ty - 7 * u), ImVec2(mx + 1.5f * u, ty + 7 * u), marker_color(m.type, 0.95f), 1.5f * u);
    }
  }

  // The buttons.
  const char* spec = live >= 0 ? "S:Retry|S:Hold: back to the replay"
                     : st.paused ? "A:Take over|X:Last tip|Y:Next tip|D:Frame|Z:Play|S:Settings"
                                 : "A:Take over|X:Last tip|Y:Next tip|D:5 s|Z:Pause|R:Fast|S:Settings";
  const float lw = practice_menu::legend(dl, 0, ly, u * 0.85f, spec, 1.0f, false);
  const float lx = std::max(x0 + 16 * u, (x0 + x1) * 0.5f - lw * 0.5f);
  plate(dl, lx - 16 * u, ly - 16 * u, lx + lw + 40 * u, ly + 16 * u, 0.7f);
  practice_menu::legend(dl, lx, ly, u * 0.85f, spec, 1.0f);
}

}  // namespace replay_hud
