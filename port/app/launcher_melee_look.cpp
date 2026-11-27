// The launcher's looks (launcher_melee_look.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "launcher_melee_look.h"
#include "launcher_melee_assets.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <list>
#include <map>
#include <tuple>
#include <vector>

#pragma comment(lib, "msimg32.lib")

namespace launcher::melee_look {
namespace {

int g_look = kBrawlOnline;   // the default look (owner 11-23); launcher.ini's look= picks another
int g_pointer = kPointer3d;   // launcher.ini pointer=
bool g_pointer_left = false;  // launcher.ini pointer_left=
namespace ma = launcher::melee_assets;

// A premultiplied 32-bit DIB in a memory DC, ready for AlphaBlend.
struct Surface {
  HDC dc = nullptr; HBITMAP bm = nullptr; HGDIOBJ old = nullptr; uint32_t* px = nullptr; int w = 0, h = 0;
  bool make(int width, int height) {
    w = width; h = height;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr); dc = CreateCompatibleDC(screen); void* bits = nullptr;
    bm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, screen);
    if (!bm) { DeleteDC(dc); dc = nullptr; return false; }
    px = (uint32_t*)bits; old = SelectObject(dc, bm); return true;
  }
  void free() { if (dc) { SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); } dc = nullptr; }
};

// ---------------------------------------------------------------- the font
// The glyph sheet at one cell size and colour (area-averaged down from 32 px cells), kept for reuse.
struct Sheet { int cell = 0; COLORREF color = 0; Surface s; };
std::list<Sheet> g_sheets;
Sheet* sheet(int cell, COLORREF color) {
  for (auto it = g_sheets.begin(); it != g_sheets.end(); ++it)
    if (it->cell == cell && it->color == color) { g_sheets.splice(g_sheets.begin(), g_sheets, it); return &g_sheets.front(); }
  const ma::Font& f = ma::font(); if (!f.ok) return nullptr;
  Sheet sh; sh.cell = cell; sh.color = color;
  const int W = 32 * cell, H = 9 * cell;   // 32 glyphs across, 9 rows
  if (!sh.s.make(W, H)) return nullptr;
  const double scale = 32.0 / cell;
  const uint32_t r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const double sx0 = x * scale, sx1 = (x + 1) * scale, sy0 = y * scale, sy1 = (y + 1) * scale;
      double sum = 0, area = 0;
      for (int sy = (int)sy0; sy < (int)std::ceil(sy1) && sy < f.sheet.h; ++sy)
        for (int sx = (int)sx0; sx < (int)std::ceil(sx1) && sx < f.sheet.w; ++sx) {
          const double wx = std::min(sx1, sx + 1.0) - std::max(sx0, (double)sx), wy = std::min(sy1, sy + 1.0) - std::max(sy0, (double)sy);
          sum += wx * wy * (f.sheet.bgra[(size_t)sy * f.sheet.w + sx] >> 24); area += wx * wy;
        }
      const uint32_t a = area > 0 ? (uint32_t)std::lround(std::min(255.0, sum / area * 1.08)) : 0;   // a touch bolder: small text stays legible
      sh.s.px[(size_t)y * W + x] = a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
    }
  g_sheets.push_front(std::move(sh));
  while (g_sheets.size() > 24) { g_sheets.back().s.free(); g_sheets.pop_back(); }
  return &g_sheets.front();
}
int cell_for(HFONT f) {
  LOGFONTW lf{}; GetObjectW(f, sizeof lf, &lf);
  return std::max(8, (int)std::lround(std::abs(lf.lfHeight ? lf.lfHeight : 16) * 1.08));
}
// Melee's font goes on titles, tabs, buttons, page names and player names (the launcher's bold text); small print
// stays in the launcher's own font, as in the owner's pick (round 6).
bool melee_size(HFONT f) { LOGFONTW lf{}; GetObjectW(f, sizeof lf, &lf); return lf.lfWeight >= FW_SEMIBOLD; }
bool drawable(const std::wstring& s) {
  const ma::Font& f = ma::font(); if (!f.ok || s.empty()) return false;
  for (wchar_t c : s) { if (c == L' ') continue; if (c >= 127 || c < 32 || f.glyph[c].index < 0) return false; }
  return true;
}
double advance(wchar_t c, int cell) {
  const ma::Font& f = ma::font(); const double s = cell / 32.0;
  if (c == L' ') return 9 * s;
  const ma::Glyph& g = f.glyph[c]; return (32 - g.left - g.right + 1) * s;
}

// ---------------------------------------------------------------- the frame
struct Frame { int w, h; COLORREF top, bot, rim; Surface s; bool css = false; };
std::list<Frame> g_frames;
}  // namespace

void set_look(int l) { g_look = l >= (int)kClassic && l <= (int)kBrawlOnline ? l : (int)kClassic; }   // unknown: Classic
int look() { return g_look; }
void set_pointer(int style, bool left) { g_pointer = style >= kPointer3d && style <= kPointerWindows ? style : (int)kPointer3d; g_pointer_left = left; }
int pointer() { return g_pointer; }
bool pointer_left() { return g_pointer_left; }
bool melee_art() { return g_look != kClassic && ma::font().ok; }

int text_width(const std::wstring& s, HFONT size_from) {
  if (!melee_art() || !melee_size(size_from) || !drawable(s)) return -1;
  const int cell = cell_for(size_from); double w = 0;
  for (wchar_t c : s) w += advance(c, cell);
  return (int)std::ceil(w);
}

bool draw_line(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt, int cell_override = 0, double skew = 0);
bool text(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt) {
  if (!melee_size(size_from)) return false;
  return draw_line(dc, s, r, size_from, color, fmt);
}
bool caption(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt) {
  if (!melee_art() || !drawable(s)) return false;
  // A caption steps down a size or two before it would be cut, to keep its whole label on the button.
  int cell = cell_for(size_from); const int room = r.right - r.left - MulDiv(12, GetDeviceCaps(dc, LOGPIXELSX), 96);
  auto wid = [&](int cl) { double v = 0; for (wchar_t ch : s) v += advance(ch, cl); return v; };
  while (cell > 9 && wid(cell) > room) --cell;
  return draw_line(dc, s, r, size_from, color, fmt, cell);
}
bool draw_line(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt, int cell_override, double skew) {
  if (!melee_art() || !drawable(s)) return false;
  const int cell = cell_override > 0 ? cell_override : cell_for(size_from);
  Sheet* sh = sheet(cell, color); if (!sh) return false;
  std::wstring shown = s;
  const int room = r.right - r.left;
  auto wid = [&](const std::wstring& x) { double v = 0; for (wchar_t ch : x) v += advance(ch, cell); return (int)std::ceil(v); };
  if ((fmt & DT_END_ELLIPSIS) && wid(shown) > room) {   // cut with "..." to fit
    while (!shown.empty() && wid(shown + L"...") > room) shown.pop_back();
    shown += L"...";
  }
  double w = 0; for (wchar_t c : shown) w += advance(c, cell);
  double x = r.left;
  if (fmt & DT_CENTER) x = r.left + (room - w) / 2.0; else if (fmt & DT_RIGHT) x = r.right - w;
  int y = r.top;
  if (fmt & DT_VCENTER) y = r.top + (r.bottom - r.top - cell) / 2 + cell / 16;   // the letters' middle on the line's middle
  else if (fmt & DT_BOTTOM) y = r.bottom - cell;
  const ma::Font& f = ma::font(); const double sc = cell / 32.0;
  const BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  if (skew != 0) {   // a slanted title: the line drawn upright, then each row slid right by its height over the baseline
    const double lean = std::tan(skew * 3.14159265358979 / 180.0);
    const int pad = (int)std::ceil(cell * lean) + 2, W = (int)std::ceil(w) + 2;
    Surface up, sl;
    if (!up.make(W, cell) || !sl.make(W + pad, cell)) { up.free(); sl.free(); return false; }
    std::fill(up.px, up.px + (size_t)W * cell, 0u); std::fill(sl.px, sl.px + (size_t)(W + pad) * cell, 0u);
    double gx = 0;
    for (wchar_t c : shown) {
      if (c != L' ') {
        const ma::Glyph& g = f.glyph[c];
        const int sx = (int)std::lround(((g.index % 32) * 32 + g.left) * sc), sy = (g.index / 32) * cell;
        const int gw = std::max(1, (int)std::lround((32 - g.left - g.right) * sc));
        AlphaBlend(up.dc, (int)std::lround(gx), 0, gw, cell, sh->s.dc, sx, sy, gw, cell, bf);
      }
      gx += advance(c, cell);
    }
    GdiFlush();
    for (int j = 0; j < cell; ++j) {
      const double shift = (cell - 1 - j) * lean; const int whole = (int)std::floor(shift); const double frac = shift - whole;
      for (int i = 0; i < W + pad; ++i) {
        const int a = i - whole, b = a - 1;   // the pixel that lands here, and its left neighbour
        const uint32_t pa = a >= 0 && a < W ? up.px[(size_t)j * W + a] : 0, pb = b >= 0 && b < W ? up.px[(size_t)j * W + b] : 0;
        uint32_t out = 0;
        for (int k = 0; k < 32; k += 8) out |= (uint32_t)std::lround(((pa >> k) & 255) * (1 - frac) + ((pb >> k) & 255) * frac) << k;
        sl.px[(size_t)j * (W + pad) + i] = out;
      }
    }
    const int save = SaveDC(dc); IntersectClipRect(dc, r.left, std::min((int)r.top, y), r.right + pad, std::max((int)r.bottom, y + cell));
    AlphaBlend(dc, (int)std::lround(x), y, W + pad, cell, sl.dc, 0, 0, W + pad, cell, bf);
    RestoreDC(dc, save); up.free(); sl.free();
    return true;
  }
  const int save = SaveDC(dc); IntersectClipRect(dc, r.left, std::min((int)r.top, y), r.right, std::max((int)r.bottom, y + cell));
  for (wchar_t c : shown) {
    if (c != L' ') {
      const ma::Glyph& g = f.glyph[c];
      const int sx = (int)std::lround(((g.index % 32) * 32 + g.left) * sc), sy = (g.index / 32) * cell;
      const int gw = std::max(1, (int)std::lround((32 - g.left - g.right) * sc));
      AlphaBlend(dc, (int)std::lround(x), y, gw, cell, sh->s.dc, sx, sy, gw, cell, bf);
    }
    x += advance(c, cell);
  }
  RestoreDC(dc, save);
  return true;
}

bool button(HDC dc, RECT r, COLORREF top, COLORREF bot, COLORREF rim, bool css_mask) {
  if (!melee_art()) return false;
  const ma::Image& ri = ma::button_rim(); const ma::Image& fi = ma::button_fill();
  if (!ri.w || !fi.w) return false;
  const int w = r.right - r.left, h = r.bottom - r.top; if (w < 8 || h < 8) return false;
  Frame* fr = nullptr;
  for (auto it = g_frames.begin(); it != g_frames.end(); ++it)
    if (it->w == w && it->h == h && it->top == top && it->bot == bot && it->rim == rim && it->css == css_mask) { g_frames.splice(g_frames.begin(), g_frames, it); fr = &g_frames.front(); break; }
  if (!fr) {
    Frame f{w, h, top, bot, rim, {}, css_mask};
    if (!f.s.make(w, h)) return false;
    // A 9-slice of the 48x32 rules box: its 10 px border kept at about 9 px (the launcher's scale), the middle stretched.
    const int src_b = 10, dst_b = std::min({MulDiv(9, GetDeviceCaps(dc, LOGPIXELSX), 96), w / 2 - 1, h / 2 - 1});
    auto map = [&](int d, int dst_len, int src_len) {
      if (d < dst_b) return (double)d * src_b / dst_b;
      if (d >= dst_len - dst_b) return src_len - (double)(dst_len - d) * src_b / dst_b;
      return src_b + (double)(d - dst_b) * (src_len - 2 * src_b) / std::max(1, dst_len - 2 * dst_b);
    };
    // Bilinear: the art is scaled smoothly (nearest pixels left a jagged outline). The alpha, and for the soft
    // outline the art's own brightness (its bevel).
    auto sample = [css_mask](const ma::Image& im, double sx, double sy, bool intensity) {
      sx -= 0.5; sy -= 0.5;
      const int x0 = std::clamp((int)std::floor(sx), 0, im.w - 1), y0 = std::clamp((int)std::floor(sy), 0, im.h - 1);
      const int x1 = std::min(x0 + 1, im.w - 1), y1 = std::min(y0 + 1, im.h - 1);
      const double fx = std::clamp(sx - std::floor(sx), 0.0, 1.0), fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
      auto one = [&](int x, int y) {
        const uint32_t p = im.bgra[(size_t)y * im.w + x]; const double a = (p >> 24) / 255.0;
        if (!intensity || css_mask) return a;   // as a CSS mask: the alpha only
        const double lum = std::max({(p >> 16) & 255u, (p >> 8) & 255u, p & 255u}) / 255.0;
        return a * (0.35 + 0.65 * lum) * 0.7;   // softer: shaded by the art, never a solid line
      };
      return (one(x0, y0) * (1 - fx) + one(x1, y0) * fx) * (1 - fy) + (one(x0, y1) * (1 - fx) + one(x1, y1) * fx) * fy;
    };
    for (int y = 0; y < h; ++y) {
      const double t = h > 1 ? (double)y / (h - 1) : 0;
      const double fr_ = GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * t, fg = GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * t, fb = GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * t;
      for (int x = 0; x < w; ++x) {
        const double sx = map(x, w, ri.w) + 0.5, sy = map(y, h, ri.h) + 0.5;
        const double fa = sample(fi, sx, sy, false), ra = sample(ri, sx, sy, true);
        const double a = ra + fa * (1 - ra);
        const double R = GetRValue(rim) * ra + fr_ * fa * (1 - ra), G = GetGValue(rim) * ra + fg * fa * (1 - ra), B = GetBValue(rim) * ra + fb * fa * (1 - ra);
        f.s.px[(size_t)y * w + x] = (uint32_t)std::lround(a * 255) << 24 | (uint32_t)std::lround(R) << 16 | (uint32_t)std::lround(G) << 8 | (uint32_t)std::lround(B);
      }
    }
    g_frames.push_front(std::move(f)); fr = &g_frames.front();
    while (g_frames.size() > 64) { g_frames.back().s.free(); g_frames.pop_back(); }
  }
  const BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  AlphaBlend(dc, r.left, r.top, w, h, fr->s.dc, 0, 0, w, h, bf);
  return true;
}

int width_cell(const std::wstring& s, int cell) {
  if (!melee_art() || !drawable(s)) return -1;
  double w = 0; for (wchar_t c : s) w += advance(c, cell); return (int)std::ceil(w);
}
bool text_cell(HDC dc, const std::wstring& s, RECT r, int cell, COLORREF color, UINT fmt, double skew) { return draw_line(dc, s, r, nullptr, color, fmt, cell, skew); }

HCURSOR hand_cursor(int pose) {
  // One set per pointer style (3D or flat) and side (as drawn, or mirrored to point left).
  static HCURSOR cursors[2][2][3] = {}; static bool tried[2][2][3] = {}; static int hot_xs[2][2] = {{-1, -1}, {-1, -1}}, hot_y = 1;
  if (!melee_art() || g_pointer == kPointerWindows) return nullptr;
  pose = std::clamp(pose, 0, 2);
  const int flat = g_pointer == kPointerFlat ? 1 : 0, left = g_pointer_left ? 1 : 0;
  if (flat) pose = 0;   // the first hand has one pose
  if (tried[flat][left][pose]) return cursors[flat][left][pose] ? cursors[flat][left][pose] : pose ? hand_cursor(0) : nullptr;
  tried[flat][left][pose] = true;
  static const ma::Image none{};
  const ma::Image& posed = flat ? none : ma::hand_pose(pose);
  const ma::Image& im = posed.w ? posed : ma::hand(); if (!im.w || (pose && !posed.w)) return pose ? hand_cursor(0) : nullptr;
  HCURSOR& cursor = cursors[flat][left][pose];
  int& hot_x = hot_xs[flat][left];
  const int H = GetSystemMetrics(SM_CYCURSOR) * 3 / 2, W = H * im.w / im.h;   // a bit larger than an arrow, as on the character select
  BITMAPV5HEADER bh{}; bh.bV5Size = sizeof bh; bh.bV5Width = W; bh.bV5Height = -H; bh.bV5Planes = 1; bh.bV5BitCount = 32; bh.bV5Compression = BI_BITFIELDS;
  bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
  HDC screen = GetDC(nullptr); void* bits = nullptr;
  HBITMAP color = CreateDIBSection(screen, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, screen);
  if (!color) return nullptr;
  uint32_t* px = (uint32_t*)bits;
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    const int sx = left ? W - 1 - x : x;   // mirrored: the art's right edge drawn on the left
    const uint32_t p = im.bgra[(size_t)(y * im.h / H) * im.w + (sx * im.w / W)];
    const uint32_t a = p >> 24, i = (posed.w ? (p & 255) : 0xE8) * a / 255;   // the glove: the pose's own shading (premultiplied), else flat white
    px[(size_t)y * W + x] = a << 24 | i << 16 | i << 8 | i;
  }
  HBITMAP mask = CreateBitmap(W, H, 1, 1, nullptr);
  // The fingertip (the pointing pose's topmost inked pixel); every pose keeps that spot so the hand never jumps.
  if (hot_x < 0) {
    const ma::Image& p0 = !flat && ma::hand_pose(0).w ? ma::hand_pose(0) : ma::hand(); hot_x = W / 3;
    for (int y = 0; y < p0.h && hot_x == W / 3; ++y) for (int x = 0; x < p0.w; ++x) if ((p0.bgra[(size_t)y * p0.w + x] >> 24) > 128) { hot_x = x * W / p0.w; hot_y = y * H / p0.h + 1; break; }
    if (left) hot_x = W - 1 - hot_x;
  }
  ICONINFO ii{}; ii.fIcon = FALSE; ii.xHotspot = (DWORD)hot_x; ii.yHotspot = (DWORD)hot_y; ii.hbmColor = color; ii.hbmMask = mask;
  cursor = (HCURSOR)CreateIconIndirect(&ii);
  DeleteObject(color); DeleteObject(mask);
  return cursor;
}
std::atomic<int> g_pose_now{0};
HCURSOR hand_now(bool over_clickable, UINT mouse_msg) {
  const bool held = mouse_msg == WM_LBUTTONDOWN || mouse_msg == WM_LBUTTONDBLCLK || (mouse_msg != WM_LBUTTONUP && (GetKeyState(VK_LBUTTON) & 0x8000));
  const int pose = held ? 1 : over_clickable ? 2 : 0; g_pose_now.store(pose);
  return hand_cursor(pose);
}
int hand_pose_now() { return g_pose_now.load(); }

}  // namespace launcher::melee_look
