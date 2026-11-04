// SPDX-License-Identifier: GPL-2.0-or-later
#include "coming_soon_overlay.h"

#include "practice_menu.h"
#include "test_env.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace coming_soon_overlay {
namespace {

using Clock = std::chrono::steady_clock;
int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

std::atomic<int64_t> g_last_show{0};    // the last frame the page was showing
std::atomic<int64_t> g_since{0};        // when it started showing (the pop-in runs from here)
std::atomic<int> g_row{-1};
std::atomic<int64_t> g_refused[3] = {};
std::atomic<int> g_style{-1};

// Where each YouGame row's name ends on the menu's first page, and the row's middle, in the game's
// 640x480 picture (measured on the menu: the rows step left as they go down). A sticker starts just
// after the name, a little above the middle, so it never covers the name.
struct Anchor { float name_end, centre; };
constexpr Anchor kRows[3] = {{292, 165}, {272, 222}, {234, 278}};

uint32_t rgba(int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, int(std::clamp(a, 0.0f, 1.0f) * 255)); }
uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto ch = [&](int s) { return int(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t + 0.5f) << s; };
  return uint32_t(ch(0) | ch(8) | ch(16) | ch(24));
}

// Everything a sticker draws is laid out flat around its centre, then turned and scaled in place.
struct Piece {
  ImDrawList* dl;
  int start;
  ImVec2 c;
  explicit Piece(ImDrawList* d, ImVec2 centre) : dl(d), start(d->VtxBuffer.Size), c(centre) {}
  int mark() const { return dl->VtxBuffer.Size; }
  // Top-to-bottom colour across the vertices added since `from` (within top..bottom).
  void gradient(int from, float top, float bottom, uint32_t ctop, uint32_t cbottom) {
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
      ImDrawVert& v = dl->VtxBuffer[i];
      const float t = (v.pos.y - top) / std::max(1.0f, bottom - top);
      const uint32_t alpha = v.col & IM_COL32_A_MASK;
      v.col = (mix(ctop, cbottom, t) & ~IM_COL32_A_MASK) | alpha;
    }
  }
  void finish(float degrees, float scale) {
    const float r = degrees * 3.14159265f / 180.0f, cs = std::cos(r) * scale, sn = std::sin(r) * scale;
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
      ImDrawVert& v = dl->VtxBuffer[i];
      const float x = v.pos.x - c.x, y = v.pos.y - c.y;
      v.pos = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
    }
  }
};

void rounded(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, uint32_t col) { dl->AddRectFilled(a, b, col, r); }

void label(ImDrawList* dl, ImVec2 c, float px, uint32_t col, const char* s, bool shadow = false) {
  const float w = practice_menu::text_width(px, s);
  practice_menu::text(dl, c.x - w * 0.5f, c.y - px * 0.55f, px, col, s, 0.0f, shadow);
}

// Five looks, one per option sent for review. `u` is one game pixel in the window; `t` the seconds
// since the page appeared; `shake` 0-1 after a refused pick.
float half_width(int style, float u) {
  switch (style) {
    case 1: return 31 * u;
    case 2: return (practice_menu::text_width(12 * u, "COMING SOON") + 22 * u) * 0.5f + 2 * u;
    case 3: return (practice_menu::text_width(12 * u, "ALMOST HERE!") + 16 * u) * 0.5f + 2.5f * u;
    case 4: return (practice_menu::text_width(12 * u, "UNLOCKS SOON") + 14 * u) * 0.5f;
    default: return (practice_menu::text_width(13 * u, "COMING SOON!") + 16 * u) * 0.5f + 2.5f * u;
  }
}

// `c` is where the sticker's left edge meets its middle line.
void sticker(ImDrawList* dl, int style, ImVec2 c, float u, float t, float shake, float alpha) {
  c.x += half_width(style, u);
  const float pop = t < 0.0f ? 0.0f : t < 0.18f ? t / 0.18f * 1.18f : t < 0.32f ? 1.18f - (t - 0.18f) / 0.14f * 0.18f : 1.0f;
  if (pop <= 0.0f) return;
  const float wobble = std::sin(t * 2.6f) * 2.0f + std::sin(shake * 40.0f) * 9.0f * shake;
  const float pulse = 1.0f + 0.035f * std::sin(t * 5.2f);
  Piece p(dl, c);
  switch (style) {
    default:
    case 0: {   // a yellow-to-orange pill with a white rim: COMING SOON!
      const char* s = "COMING SOON!";
      const float px = 13 * u, w = practice_menu::text_width(px, s) + 16 * u, h = 21 * u;
      rounded(dl, ImVec2(c.x - w / 2 + 3 * u, c.y - h / 2 + 4 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 4 * u), h / 2, rgba(0, 0, 0, 0.45f * alpha));
      rounded(dl, ImVec2(c.x - w / 2 - 2.5f * u, c.y - h / 2 - 2.5f * u), ImVec2(c.x + w / 2 + 2.5f * u, c.y + h / 2 + 2.5f * u), h / 2 + 2.5f * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      rounded(dl, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), h / 2, rgba(255, 255, 255, alpha));
      p.gradient(fill, c.y - h / 2, c.y + h / 2, rgba(255, 226, 64), rgba(255, 120, 24));
      label(dl, c, px, rgba(70, 20, 0, alpha), s);
      p.finish(-11.0f + wobble, pop * pulse);
      break;
    }
    case 1: {   // a red and yellow starburst: SOON!
      const float r0 = 30 * u, r1 = 23 * u;
      const int spikes = 14;
      auto star = [&](ImVec2 o, float grow, uint32_t col) {
        for (int i = 0; i < spikes; ++i) {
          const float a0 = (i * 2.0f) * 3.14159265f / spikes, a1 = (i * 2.0f + 1) * 3.14159265f / spikes,
                      a2 = (i * 2.0f + 2) * 3.14159265f / spikes;
          const ImVec2 q0(o.x + std::cos(a0) * (r1 + grow), o.y + std::sin(a0) * (r1 + grow)),
              q1(o.x + std::cos(a1) * (r0 + grow), o.y + std::sin(a1) * (r0 + grow)),
              q2(o.x + std::cos(a2) * (r1 + grow), o.y + std::sin(a2) * (r1 + grow));
          dl->AddTriangleFilled(o, q0, q1, col);
          dl->AddTriangleFilled(o, q1, q2, col);
        }
      };
      star(ImVec2(c.x + 3 * u, c.y + 4 * u), 0, rgba(0, 0, 0, 0.45f * alpha));
      star(c, 3 * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      star(c, 0, rgba(255, 255, 255, alpha));
      p.gradient(fill, c.y - r0, c.y + r0, rgba(255, 70, 50), rgba(255, 190, 40));
      label(dl, ImVec2(c.x, c.y), 11 * u, rgba(255, 255, 255, alpha), "SOON!", true);
      p.finish(10.0f + wobble * 1.5f, pop * (1.0f + 0.06f * std::sin(t * 6.0f)));
      break;
    }
    case 2: {   // a pink-to-violet ribbon: YouGame over COMING SOON
      const char* s = "COMING SOON";
      const float px = 12 * u, w = practice_menu::text_width(px, s) + 22 * u, h = 26 * u;
      rounded(dl, ImVec2(c.x - w / 2 + 3 * u, c.y - h / 2 + 4 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 4 * u), 4 * u, rgba(0, 0, 0, 0.45f * alpha));
      rounded(dl, ImVec2(c.x - w / 2 - 2 * u, c.y - h / 2 - 2 * u), ImVec2(c.x + w / 2 + 2 * u, c.y + h / 2 + 2 * u), 5 * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      rounded(dl, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), 4 * u, rgba(255, 255, 255, alpha));
      p.gradient(fill, c.y - h / 2, c.y + h / 2, rgba(255, 70, 170), rgba(120, 50, 230));
      label(dl, ImVec2(c.x, c.y - 6 * u), 8 * u, rgba(255, 230, 250, alpha), "YouGame");
      label(dl, ImVec2(c.x, c.y + 5 * u), px, rgba(255, 255, 255, alpha), s, true);
      p.finish(-8.0f + wobble, pop * pulse);
      break;
    }
    case 3: {   // a cyan speech bubble pointing at the row: ALMOST HERE!
      const char* s = "ALMOST HERE!";
      const float px = 12 * u, w = practice_menu::text_width(px, s) + 16 * u, h = 22 * u;
      const float bob = std::sin(t * 3.4f) * 2.0f * u;
      const ImVec2 o(c.x, c.y + bob);
      const ImVec2 tail0(o.x - w * 0.28f, o.y + h / 2 - 1 * u), tail1(o.x - w * 0.10f, o.y + h / 2 - 1 * u), tip(o.x - w * 0.40f, o.y + h / 2 + 9 * u);
      rounded(dl, ImVec2(o.x - w / 2 + 3 * u, o.y - h / 2 + 4 * u), ImVec2(o.x + w / 2 + 3 * u, o.y + h / 2 + 4 * u), 9 * u, rgba(0, 0, 0, 0.45f * alpha));
      rounded(dl, ImVec2(o.x - w / 2 - 2.5f * u, o.y - h / 2 - 2.5f * u), ImVec2(o.x + w / 2 + 2.5f * u, o.y + h / 2 + 2.5f * u), 11 * u, rgba(255, 255, 255, alpha));
      dl->AddTriangleFilled(ImVec2(tail0.x - 3 * u, tail0.y), ImVec2(tail1.x + 3 * u, tail1.y), ImVec2(tip.x - 2 * u, tip.y + 3 * u), rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      rounded(dl, ImVec2(o.x - w / 2, o.y - h / 2), ImVec2(o.x + w / 2, o.y + h / 2), 9 * u, rgba(255, 255, 255, alpha));
      dl->AddTriangleFilled(tail0, tail1, tip, rgba(255, 255, 255, alpha));
      p.gradient(fill, o.y - h / 2, o.y + h / 2 + 9 * u, rgba(110, 240, 255), rgba(20, 150, 235));
      label(dl, o, px, rgba(5, 40, 80, alpha), s);
      p.finish(-6.0f + wobble * 0.6f, pop);
      break;
    }
    case 4: {   // a red rubber stamp: UNLOCKS SOON
      const char* s = "UNLOCKS SOON";
      const float px = 12 * u, w = practice_menu::text_width(px, s) + 14 * u, h = 20 * u;
      dl->AddRectFilled(ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), rgba(255, 245, 230, 0.92f * alpha), 3 * u);
      dl->AddRect(ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), rgba(220, 30, 40, alpha), 3 * u, 0, 2.5f * u);
      dl->AddRect(ImVec2(c.x - w / 2 + 3 * u, c.y - h / 2 + 3 * u), ImVec2(c.x + w / 2 - 3 * u, c.y + h / 2 - 3 * u), rgba(220, 30, 40, 0.8f * alpha), 2 * u, 0, 1.0f * u);
      label(dl, c, px, rgba(215, 25, 35, alpha), s);
      // A stamp lands: bigger and fainter first.
      const float land = t < 0.25f ? 1.6f - t / 0.25f * 0.6f : 1.0f;
      p.finish(-14.0f + wobble * 0.5f, land);
      break;
    }
  }
}

}  // namespace

void showing(int row) {
  const int64_t now = now_ms();
  if (now - g_last_show.load() > 400) g_since = now;   // a new visit to the page: pop in again
  g_last_show = now;
  g_row = row;
}

void refused(int row) {
  if (row >= 0 && row < 3) g_refused[row] = now_ms();
}

void set_style(int style) { g_style = style; }

void draw(float x0, float y0, float x1, float y1) {
  const int64_t now = now_ms();
  if (now - g_last_show.load() > 120) return;   // the page is not on screen
  if (g_style.load() < 0) {
    const char* v = host::test_env::getenv("MELEE_TEST_SOON_STYLE");
    g_style = v ? std::clamp(std::atoi(v), 0, 4) : 0;
  }
  const float gh = y1 - y0, u = gh / 480.0f, cx = (x0 + x1) * 0.5f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float since = float(now - g_since.load()) / 1000.0f;
  for (int i = 0; i < 3; ++i) {
    const float t = since - 0.35f - 0.09f * float(i);   // after the rows settle, one after another
    const int64_t r = g_refused[i].load();
    const float shake = r && now - r < 450 ? 1.0f - float(now - r) / 450.0f : 0.0f;
    const ImVec2 c(cx + (kRows[i].name_end + 6 - 320) * u, y0 + (kRows[i].centre - 9) * u);
    sticker(dl, g_style.load(), c, u, t, shake, 1.0f);
  }
}

}  // namespace coming_soon_overlay
