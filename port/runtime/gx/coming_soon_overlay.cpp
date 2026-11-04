// SPDX-License-Identifier: GPL-2.0-or-later
#include "coming_soon_overlay.h"

#include "practice_menu.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace coming_soon_overlay {
namespace {

using Clock = std::chrono::steady_clock;
int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

std::atomic<int64_t> g_last_show{0};    // the last frame the page was showing
std::atomic<int64_t> g_since{0};        // when it started showing (the pop-in runs from here)

// Where each YouGame row's name ends on the menu's first page, and the row's middle, in the game's
// 640x480 picture (measured on the menu: the rows step left as they go down). A sticker starts just
// after the name, a little above the middle, so it never covers the name.
struct Anchor { float name_end, centre; };
constexpr Anchor kRows[3] = {{336, 165}, {300, 222}, {278, 278}};
constexpr const char* kText = "COMING SOON!";

uint32_t rgba(int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, int(std::clamp(a, 0.0f, 1.0f) * 255)); }
uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto ch = [&](int s) { return int(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t + 0.5f) << s; };
  return uint32_t(ch(0) | ch(8) | ch(16) | ch(24));
}
uint32_t hue(float h, float a) {
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 0.85f, 1.0f, r, g, b);
  return rgba(int(r * 255), int(g * 255), int(b * 255), a);
}

// The sticker is laid out flat around its centre, then turned and scaled in place.
void turn(ImDrawList* dl, int from, ImVec2 c, float degrees, float scale) {
  const float r = degrees * 3.14159265f / 180.0f, cs = std::cos(r) * scale, sn = std::sin(r) * scale;
  for (int i = from; i < dl->VtxBuffer.Size; ++i) {
    ImDrawVert& v = dl->VtxBuffer[i];
    const float x = v.pos.x - c.x, y = v.pos.y - c.y;
    v.pos = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
  }
}

// The pill filled in thin vertical strips, each corner coloured on its own, so the rainbow and the
// shine run along it (ImGui's own shapes only colour their outline vertices).
template <class Paint>
void pill(ImDrawList* dl, ImVec2 c, float w, float h, float step, Paint paint) {
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  auto edge = [&](float x, float& top, float& bottom) {
    const float r = h * 0.5f, d = std::fabs(x - c.x) - (w * 0.5f - r);
    const float dy = d > 0 ? std::sqrt(std::max(0.0f, r * r - d * d)) : r;
    top = c.y - dy;
    bottom = c.y + dy;
  };
  const float x0 = c.x - w * 0.5f, x1 = c.x + w * 0.5f;
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

// A rainbow pill with a white rim, colours flowing along it, a shine sweeping over it and twinkles at
// its ends: COMING SOON! `left` is where its left edge meets its middle line, `u` one game pixel in
// the window, `t` the seconds since it popped in.
void sticker(ImDrawList* dl, ImVec2 left, float u, float t) {
  const float px = 13 * u, tw = practice_menu::text_width(px, kText), w = tw + 18 * u, h = 22 * u;
  const ImVec2 c(left.x + w * 0.5f + 3 * u, left.y);
  const float pop = t < 0.0f ? 0.0f : t < 0.18f ? t / 0.18f * 1.18f : t < 0.32f ? 1.18f - (t - 0.18f) / 0.14f * 0.18f : 1.0f;
  if (pop <= 0.0f) return;
  const int start = dl->VtxBuffer.Size;
  dl->AddRectFilled(ImVec2(c.x - w / 2 + 3 * u, c.y - h / 2 + 4 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 4 * u),
                    rgba(0, 0, 0, 0.45f), h / 2);
  dl->AddRectFilled(ImVec2(c.x - w / 2 - 3 * u, c.y - h / 2 - 3 * u), ImVec2(c.x + w / 2 + 3 * u, c.y + h / 2 + 3 * u),
                    rgba(255, 255, 255), h / 2 + 3 * u);
  pill(dl, c, w, h, 1.5f * u, [&](float x, float y) {
    const uint32_t base = mix(hue((x - c.x) / w * 0.9f - t * 0.45f, 1.0f), rgba(0, 0, 0), (y - c.y) / h * 0.35f + 0.1f);
    return mix(base, rgba(255, 255, 255), shine(x, y, c, w, t, 1.8f) * 0.85f);
  });
  practice_menu::text(dl, c.x - tw * 0.5f, c.y - px * 0.55f, px, rgba(255, 255, 255), kText, 0.0f, true);
  for (int i = 0; i < 4; ++i) {
    const float k = std::max(0.0f, std::sin(t * 3.0f + i * 1.7f));
    const ImVec2 o(c.x + (i % 2 ? 1 : -1) * (w * 0.5f - (i < 2 ? 2 : 14) * u), c.y + (i < 2 ? -1 : 1) * (h * 0.5f + 1 * u));
    sparkle(dl, o, (4 + 3 * k) * u * k, rgba(255, 255, 210));
  }
  const float wobble = std::sin(t * 2.6f) * 2.0f, pulse = 1.0f + 0.035f * std::sin(t * 5.2f);
  turn(dl, start, c, -10.0f + wobble, pop * pulse);
}

}  // namespace

void showing() {
  const int64_t now = now_ms();
  if (now - g_last_show.load() > 400) g_since = now;   // a new visit to the page: pop in again
  g_last_show = now;
}

void draw(float x0, float y0, float x1, float y1) {
  const int64_t now = now_ms();
  if (now - g_last_show.load() > 120) return;   // the page is not on screen
  const float u = (y1 - y0) / 480.0f, cx = (x0 + x1) * 0.5f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float since = float(now - g_since.load()) / 1000.0f;
  for (int i = 0; i < 3; ++i) {
    const float t = since - 0.35f - 0.09f * float(i);   // after the rows settle, one after another
    sticker(dl, ImVec2(cx + (kRows[i].name_end + 6 - 320) * u, y0 + (kRows[i].centre - 9) * u), u, t);
  }
}

}  // namespace coming_soon_overlay
