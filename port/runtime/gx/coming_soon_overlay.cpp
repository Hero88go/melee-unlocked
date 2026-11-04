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
constexpr Anchor kRows[3] = {{336, 165}, {300, 222}, {278, 278}};

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

uint32_t hue(float h, float a = 1.0f) {
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.85f, 1.0f, r, g, b);
  return rgba(int(r * 255), int(g * 255), int(b * 255), a);
}

// A shape filled in thin vertical strips whose top and bottom follow `edge(x, top, bottom)`, each
// corner coloured by `paint(x, y)`: gradients, rainbows and shine sweeps run across it in any
// direction (ImGui's own shapes only colour their outline vertices).
template <class Edge, class Paint>
void strips(ImDrawList* dl, float x0, float x1, float step, Edge edge, Paint paint) {
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  const int n = std::max(1, int(std::ceil((x1 - x0) / std::max(step, 0.5f))));
  dl->PrimReserve(n * 6, n * 4);
  for (int i = 0; i < n; ++i) {
    const float a = x0 + (x1 - x0) * float(i) / float(n), b = x0 + (x1 - x0) * float(i + 1) / float(n);
    float at, ab, bt, bb;
    edge(a, at, ab);
    edge(b, bt, bb);
    const ImDrawIdx idx = ImDrawIdx(dl->_VtxCurrentIdx);
    dl->PrimWriteIdx(idx); dl->PrimWriteIdx(ImDrawIdx(idx + 1)); dl->PrimWriteIdx(ImDrawIdx(idx + 2));
    dl->PrimWriteIdx(idx); dl->PrimWriteIdx(ImDrawIdx(idx + 2)); dl->PrimWriteIdx(ImDrawIdx(idx + 3));
    dl->PrimWriteVtx(ImVec2(a, at), uv, paint(a, at));
    dl->PrimWriteVtx(ImVec2(b, bt), uv, paint(b, bt));
    dl->PrimWriteVtx(ImVec2(b, bb), uv, paint(b, bb));
    dl->PrimWriteVtx(ImVec2(a, ab), uv, paint(a, ab));
  }
}

// A capsule's top and bottom at x, around centre c, w wide and h high.
auto capsule(ImVec2 c, float w, float h) {
  return [=](float x, float& top, float& bottom) {
    const float r = h * 0.5f, d = std::fabs(x - c.x) - (w * 0.5f - r);
    const float dy = d > 0 ? std::sqrt(std::max(0.0f, r * r - d * d)) : r;
    top = c.y - dy;
    bottom = c.y + dy;
  };
}

// A white shine band sweeping left to right every `period` seconds, as a 0-1 strength at x.
float shine(float x, float y, ImVec2 c, float w, float t, float period) {
  const float phase = std::fmod(t, period) / period * 2.2f - 0.6f;   // past both ends between sweeps
  const float bx = c.x - w * 0.5f + phase * w + (y - c.y) * 0.6f;
  return std::clamp(1.0f - std::fabs(x - bx) / (w * 0.07f), 0.0f, 1.0f);
}

// A four-point twinkle.
void sparkle(ImDrawList* dl, ImVec2 o, float r, uint32_t col) {
  if (r <= 0.3f) return;
  dl->AddQuadFilled(ImVec2(o.x, o.y - r), ImVec2(o.x + r * 0.22f, o.y), ImVec2(o.x, o.y + r), ImVec2(o.x - r * 0.22f, o.y), col);
  dl->AddQuadFilled(ImVec2(o.x - r, o.y), ImVec2(o.x, o.y - r * 0.22f), ImVec2(o.x + r, o.y), ImVec2(o.x, o.y + r * 0.22f), col);
}

void label(ImDrawList* dl, ImVec2 c, float px, uint32_t col, const char* s, bool shadow = false) {
  const float w = practice_menu::text_width(px, s);
  practice_menu::text(dl, c.x - w * 0.5f, c.y - px * 0.55f, px, col, s, 0.0f, shadow);
}

// Ten looks, one per option sent for review (5-9: the second round, all COMING SOON!). `u` is one game pixel in the window; `t` the seconds
// since the page appeared; `shake` 0-1 after a refused pick.
float half_width(int style, float u) {
  switch (style) {
    case 1: return 31 * u;
    case 2: return (practice_menu::text_width(12 * u, "COMING SOON") + 22 * u) * 0.5f + 2 * u;
    case 3: return (practice_menu::text_width(12 * u, "ALMOST HERE!") + 16 * u) * 0.5f + 2.5f * u;
    case 4: return (practice_menu::text_width(12 * u, "UNLOCKS SOON") + 14 * u) * 0.5f;
    case 5: return (practice_menu::text_width(13 * u, "COMING SOON!") + 18 * u) * 0.5f + 3 * u;
    case 6: return practice_menu::text_width(13 * u, "COMING SOON!") * 0.5f + 30 * u;
    case 7: return (practice_menu::text_width(13 * u, "COMING SOON!") + 20 * u) * 0.5f + 5 * u;
    case 8: return (practice_menu::text_width(13 * u, "COMING SOON!") + 12 * u) * 0.5f + 15 * u;
    case 9: return (practice_menu::text_width(13 * u, "COMING SOON!") + 18 * u) * 0.5f + 13 * u;
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
    case 5: {   // a rainbow pill, colours flowing along it, a shine sweeping over, twinkles around
      const char* s = "COMING SOON!";
      const float px = 13 * u, w = practice_menu::text_width(px, s) + 18 * u, h = 22 * u;
      rounded(dl, ImVec2(c.x - w / 2 + 3 * u, c.y - h / 2 + 4 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 4 * u), h / 2, rgba(0, 0, 0, 0.45f * alpha));
      rounded(dl, ImVec2(c.x - w / 2 - 3 * u, c.y - h / 2 - 3 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 3 * u), h / 2 + 3 * u, rgba(255, 255, 255, alpha));
      strips(dl, c.x - w / 2, c.x + w / 2, 1.5f * u, capsule(c, w, h), [&](float x, float y) {
        const uint32_t base = mix(hue((x - c.x) / w * 0.9f - t * 0.45f, alpha), rgba(0, 0, 0, alpha), (y - c.y) / h * 0.35f + 0.1f);
        return mix(base, rgba(255, 255, 255, alpha), shine(x, y, c, w, t, 1.8f) * 0.85f);
      });
      label(dl, c, px, rgba(255, 255, 255, alpha), s, true);
      for (int i = 0; i < 4; ++i) {
        const float ph = t * 3.0f + i * 1.7f, k = std::max(0.0f, std::sin(ph));
        const ImVec2 o(c.x + (i % 2 ? 1 : -1) * (w * 0.5f - (i < 2 ? 2 : 14) * u), c.y + (i < 2 ? -1 : 1) * (h * 0.5f + 1 * u));
        sparkle(dl, o, (4 + 3 * k) * u * k, rgba(255, 255, 210, alpha));
      }
      p.finish(-10.0f + wobble, pop * pulse);
      break;
    }
    case 6: {   // a comic burst, yellow to red with halftone dots, bouncing: COMING SOON!
      const char* s = "COMING SOON!";
      const float px = 13 * u, rx = practice_menu::text_width(px, s) * 0.5f + 27 * u, ry = 21 * u;
      constexpr int kPts = 22;
      constexpr float kJag[kPts] = {1.00f, 0.80f, 1.06f, 0.82f, 0.97f, 0.78f, 1.08f, 0.84f, 0.99f, 0.80f, 1.04f,
                                    0.79f, 1.02f, 0.83f, 0.95f, 0.80f, 1.07f, 0.81f, 1.00f, 0.78f, 1.05f, 0.83f};
      auto burst = [&](ImVec2 o, float grow, uint32_t col) {
        for (int i = 0; i < kPts; ++i) {
          const int j = (i + 1) % kPts;
          const float a0 = i * 6.2831853f / kPts, a1 = j * 6.2831853f / kPts;
          dl->AddTriangleFilled(o, ImVec2(o.x + std::cos(a0) * (rx * kJag[i] + grow), o.y + std::sin(a0) * (ry * kJag[i] + grow)),
                                ImVec2(o.x + std::cos(a1) * (rx * kJag[j] + grow), o.y + std::sin(a1) * (ry * kJag[j] + grow)), col);
        }
      };
      burst(ImVec2(c.x + 3 * u, c.y + 4 * u), 3 * u, rgba(0, 0, 0, 0.5f * alpha));
      burst(c, 3.5f * u, rgba(25, 10, 10, alpha));
      burst(c, 1.5f * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      burst(c, 0, rgba(255, 255, 255, alpha));
      p.gradient(fill, c.y - ry, c.y + ry, rgba(255, 240, 60), rgba(255, 60, 40));
      for (float y = -ry * 0.6f; y <= ry * 0.6f; y += 4.5f * u)
        for (float x = -rx * 0.8f; x <= rx * 0.8f; x += 4.5f * u)
          if ((x * x) / (rx * rx * 0.64f) + (y * y) / (ry * ry * 0.36f) <= 1.0f)
            dl->AddCircleFilled(ImVec2(c.x + x + (int(y / u) % 2 ? 2.2f * u : 0), c.y + y), 0.9f * u, rgba(200, 30, 20, 0.30f * alpha), 6);
      label(dl, ImVec2(c.x + 1.5f * u, c.y + 1.5f * u), px, rgba(120, 0, 0, alpha), s);
      label(dl, c, px, rgba(255, 255, 255, alpha), s, true);
      const float boing = 1.0f + 0.07f * std::fabs(std::sin(t * 4.0f));
      p.finish(8.0f + wobble * 1.4f, pop * boing);
      break;
    }
    case 7: {   // a neon sign: a pink tube around glowing cyan letters that flicker now and then
      const char* s = "COMING SOON!";
      const float px = 13 * u, w = practice_menu::text_width(px, s) + 20 * u, h = 23 * u;
      const float cyc = std::fmod(t, 3.1f);
      const float lit = t < 0.25f ? 0.4f : (cyc > 2.70f && cyc < 2.76f) || (cyc > 2.84f && cyc < 2.88f) ? 0.35f : 1.0f;
      const ImVec2 a(c.x - w / 2, c.y - h / 2), b(c.x + w / 2, c.y + h / 2);
      rounded(dl, ImVec2(a.x + 3 * u, a.y + 4 * u), ImVec2(b.x + 3 * u, b.y + 4 * u), 7 * u, rgba(0, 0, 0, 0.5f * alpha));
      rounded(dl, ImVec2(a.x - 2 * u, a.y - 2 * u), ImVec2(b.x + 2 * u, b.y + 2 * u), 8 * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      rounded(dl, a, b, 7 * u, rgba(255, 255, 255, alpha));
      p.gradient(fill, a.y, b.y, rgba(40, 10, 70), rgba(10, 5, 30));
      for (int k = 4; k >= 1; --k) {
        const float e = k * 1.6f * u;
        dl->AddRect(ImVec2(a.x + 3 * u - e, a.y + 3 * u - e), ImVec2(b.x - 3 * u + e, b.y - 3 * u + e), rgba(255, 50, 200, 0.16f * lit * alpha), 5 * u + e, 0, 2.5f * u);
      }
      dl->AddRect(ImVec2(a.x + 3 * u, a.y + 3 * u), ImVec2(b.x - 3 * u, b.y - 3 * u), rgba(255, 70, 210, (0.3f + 0.7f * lit) * alpha), 5 * u, 0, 2.0f * u);
      dl->AddRect(ImVec2(a.x + 3 * u, a.y + 3 * u), ImVec2(b.x - 3 * u, b.y - 3 * u), rgba(255, 220, 250, lit * alpha), 5 * u, 0, 0.7f * u);
      for (int k = 0; k < 8; ++k) {
        const float an = k * 0.785398f;
        label(dl, ImVec2(c.x + std::cos(an) * 1.6f * u, c.y + std::sin(an) * 1.6f * u), px, rgba(0, 240, 255, 0.22f * lit * alpha), s);
      }
      label(dl, c, px, mix(rgba(60, 120, 140, alpha), rgba(215, 255, 255, alpha), lit), s);
      p.finish(-7.0f + wobble * 0.7f, pop * (1.0f + 0.02f * std::sin(t * 7.0f)));
      break;
    }
    case 8: {   // a puffy cloud bubble trailing back to the row, floating, with twinkles
      const char* s = "COMING SOON!";
      const float px = 13 * u, w = practice_menu::text_width(px, s) + 12 * u, h = 23 * u;
      const float bob = std::sin(t * 2.8f) * 2.0f * u;
      const ImVec2 o(c.x, c.y + bob);
      struct Puff { float x, y, r; };
      Puff puffs[16];
      int n = 0;
      const int across = 5;
      for (int i = 0; i < across; ++i) {
        const float fx = -0.5f + (i + 0.5f) / across;
        puffs[n++] = {o.x + fx * w, o.y - h * 0.32f, h * (i % 2 ? 0.50f : 0.44f)};
        puffs[n++] = {o.x + fx * w + 2 * u, o.y + h * 0.30f, h * (i % 2 ? 0.40f : 0.46f)};
      }
      puffs[n++] = {o.x - w * 0.5f, o.y, h * 0.52f};
      puffs[n++] = {o.x + w * 0.5f, o.y, h * 0.52f};
      const ImVec2 trail[2] = {ImVec2(o.x - w * 0.5f - 6 * u, o.y + h * 0.85f), ImVec2(o.x - w * 0.5f - 12 * u, o.y + h * 1.25f)};
      const float trail_r[2] = {3.2f * u, 2.0f * u};
      auto cloud = [&](float dx, float dy, float grow, uint32_t col) {
        for (int i = 0; i < n; ++i) dl->AddCircleFilled(ImVec2(puffs[i].x + dx, puffs[i].y + dy), puffs[i].r + grow, col, 24);
        dl->AddRectFilled(ImVec2(o.x - w / 2 + dx, o.y - h * 0.4f + dy), ImVec2(o.x + w / 2 + dx, o.y + h * 0.4f + dy), col);
        for (int i = 0; i < 2; ++i) dl->AddCircleFilled(ImVec2(trail[i].x + dx, trail[i].y + dy), trail_r[i] + grow, col, 16);
      };
      cloud(3 * u, 4 * u, 0, rgba(0, 0, 0, 0.42f * alpha));
      cloud(0, 0, 2.5f * u, rgba(255, 255, 255, alpha));
      const int fill = p.mark();
      cloud(0, 0, 0, rgba(255, 255, 255, alpha));
      p.gradient(fill, o.y - h, o.y + h * 1.3f, rgba(190, 150, 255), rgba(255, 110, 200));
      label(dl, o, px, rgba(255, 255, 255, alpha), s, true);
      for (int i = 0; i < 3; ++i) {
        const float k = std::max(0.0f, std::sin(t * 2.6f + i * 2.1f));
        const ImVec2 sp(o.x + (i == 0 ? -0.35f : i == 1 ? 0.15f : 0.55f) * w, o.y + (i == 1 ? -1.05f : -0.9f) * h);
        sparkle(dl, sp, 5 * u * k, rgba(255, 250, 200, alpha));
      }
      p.finish(-5.0f + wobble * 0.6f, pop);
      break;
    }
    case 9: {   // a gold banner with swallowtail ends, a shine sweeping over, confetti bursting out
      const char* s = "COMING SOON!";
      const float px = 13 * u, w = practice_menu::text_width(px, s) + 18 * u, h = 21 * u;
      const float l = c.x - w / 2, r = c.x + w / 2, top = c.y - h / 2, bot = c.y + h / 2;
      const uint32_t tail = rgba(190, 110, 10, alpha), tail_dark = rgba(120, 60, 0, alpha);
      for (int side = -1; side <= 1; side += 2) {   // the ribbon's folded ends, behind and lower
        const float in = side < 0 ? l + 6 * u : r - 6 * u, out = side < 0 ? l - 13 * u : r + 13 * u, notch = side < 0 ? l - 6 * u : r + 6 * u;
        const float t0 = top + 5 * u, b0 = bot + 5 * u, mid = c.y + 5 * u;
        dl->AddQuadFilled(ImVec2(in + 3 * u, t0 + 4 * u), ImVec2(out + 3 * u, t0 + 4 * u), ImVec2(notch + 3 * u, mid + 4 * u), ImVec2(in + 3 * u, mid + 4 * u), rgba(0, 0, 0, 0.4f * alpha));
        dl->AddQuadFilled(ImVec2(in + 3 * u, mid + 4 * u), ImVec2(notch + 3 * u, mid + 4 * u), ImVec2(out + 3 * u, b0 + 4 * u), ImVec2(in + 3 * u, b0 + 4 * u), rgba(0, 0, 0, 0.4f * alpha));
        dl->AddQuadFilled(ImVec2(in, t0), ImVec2(out, t0), ImVec2(notch, mid), ImVec2(in, mid), tail);
        dl->AddQuadFilled(ImVec2(in, mid), ImVec2(notch, mid), ImVec2(out, b0), ImVec2(in, b0), tail);
        dl->AddTriangleFilled(ImVec2(side < 0 ? l : r, bot), ImVec2(in, bot), ImVec2(in, b0), tail_dark);
      }
      rounded(dl, ImVec2(l + 3 * u, top + 4 * u), ImVec2(r + 3 * u, bot + 4 * u), 3 * u, rgba(0, 0, 0, 0.45f * alpha));
      rounded(dl, ImVec2(l - 2.5f * u, top - 2.5f * u), ImVec2(r + 2.5f * u, bot + 2.5f * u), 4 * u, rgba(255, 255, 255, alpha));
      strips(dl, l, r, 1.5f * u, [&](float, float& a, float& b) { a = top; b = bot; }, [&](float x, float y) {
        const float k = (y - top) / h;
        const uint32_t gold = k < 0.5f ? mix(rgba(255, 245, 160, alpha), rgba(255, 200, 40, alpha), k * 2) : mix(rgba(255, 200, 40, alpha), rgba(215, 130, 10, alpha), (k - 0.5f) * 2);
        return mix(gold, rgba(255, 255, 255, alpha), shine(x, y, c, w, t, 2.2f) * 0.9f);
      });
      label(dl, ImVec2(c.x, c.y + 1 * u), px, rgba(255, 250, 220, alpha), s);
      label(dl, c, px, rgba(90, 40, 0, alpha), s);
      // Confetti: a burst as it lands, then again every three seconds.
      constexpr uint32_t kConfetti[5] = {IM_COL32(255, 70, 90, 255), IM_COL32(60, 200, 255, 255), IM_COL32(120, 255, 120, 255),
                                         IM_COL32(255, 230, 60, 255), IM_COL32(220, 110, 255, 255)};
      const float ct = std::fmod(std::max(0.0f, t), 3.0f);
      if (ct < 1.3f) {
        const float fade = 1.0f - ct / 1.3f;
        for (int i = 0; i < 16; ++i) {
          const float an = -3.14159265f * (0.08f + 0.84f * float(i) / 15.0f) + (i % 3) * 0.07f;
          const float sp = (34 + (i * 7) % 18) * u;
          const ImVec2 q(c.x + std::cos(an) * sp * ct * 1.6f, c.y + std::sin(an) * sp * ct * 1.2f + 30 * u * ct * ct);
          const float rot = ct * (6 + i % 4) + i, cs = std::cos(rot) * 2.2f * u, sn = std::sin(rot) * 1.1f * u;
          const uint32_t col = (kConfetti[i % 5] & ~IM_COL32_A_MASK) | (uint32_t(fade * alpha * 255) << IM_COL32_A_SHIFT);
          dl->AddQuadFilled(ImVec2(q.x - cs + sn, q.y - sn - cs), ImVec2(q.x + cs + sn, q.y + sn - cs), ImVec2(q.x + cs - sn, q.y + sn + cs), ImVec2(q.x - cs - sn, q.y - sn + cs), col);
        }
      }
      p.finish(-9.0f + wobble, pop * pulse);
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
    g_style = v ? std::clamp(std::atoi(v), 0, 9) : 0;
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
