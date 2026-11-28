// The "Brawl Online by sarahkittyy" look (launcher_skin.h). Layout, sizes and colours are the owner's pick B4
// (HANDOFF-YOUGAME-LOBBIES.md 9.6), measured from that mockup element by element: sidebar 216 px, the page, rail
// 272 px; the palette from the theme colour by the same OKLCH rules. Melee text is drawn at 0.78 of the mockup's
// nominal size (its cell), as the mockup does; page titles lean 12 degrees.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "launcher_skin.h"
#include "launcher_pad.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include "launcher_melee_look.h"
#include "launcher_yglobby.h"
#include "launcher_room.h"
#include "launcher_lang.h"

#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace launcher::skin {
namespace {
#include "launcher_skin_icons.inc"

HWND g_main = nullptr, g_window = nullptr, g_code = nullptr, g_search = nullptr;
std::vector<HWND> g_hidden;   // the Classic controls the skin hid, shown again when it goes
Callbacks g_cb;
COLORREF g_theme = RGB(0x4C, 0x5F, 0xD5);   // Melee blue
int g_page = 0;      // 0 Play, 1 Spectate
bool g_compact = false; int g_compact_nav = 2;   // only the sidebar, beside a launcher page
int g_rail_tab = 0;  // 0 Online, 1 Friends
// The status line's type, being chosen (A label in the page's font with the value in Melee's, B the page's font, C Melee's font); test runs pick it.
int g_status_style = [] { const char* v = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_STATUS_STYLE") : nullptr; return v ? std::atoi(v) : 0; }();
HBITMAP g_art = nullptr; int g_art_w = 0, g_art_h = 0;   // Bailey (the launcher's bitmap, not ours to free)
// A list the wheel scrolls by whole rows: the players in the rail, and the page's rooms or live games.
struct Scroll { int skip = 0, total = 0, fit = 0; RECT area{}; };
Scroll g_rail_scroll, g_list_scroll;
bool g_elo = false;
int g_hot = -1; int g_hot_act = -1, g_hot_arg = 0;   // the hovered item (index in the last paint's list, and which item it is)
bool g_tracking = false;
std::wstring g_toast; ULONGLONG g_toast_until = 0;
ULONG_PTR g_gdiplus = 0;

enum Act { A_BUILD_MENU, A_PLAYER_MENU, A_ADD_TYPED, A_SIGN_IN, A_STATUS, A_RAIL_ART, A_WATCH_PLAYER, A_NAV, A_PLAY_OFFLINE, A_QUEUE, A_JOIN_CODE, A_CREATE, A_ROOM, A_WATCH, A_RAIL_TAB, A_ELO, A_TOOL, A_ME, A_MY_MAIN, A_LEAVE_ROOM };
struct Hit { RECT r; int act, arg; };
std::vector<Hit> g_hits;

// The skin's scale: the window's DPI, made smaller when the window is smaller than the mockup (a small laptop
// screen shrinks it), so the page is never covered by the rail. Set at the start of each paint.
int g_fit_dpi = 0;
int dpi() { return g_fit_dpi ? g_fit_dpi : g_window ? GetDpiForWindow(g_window) : 96; }
int S(int n) { return MulDiv(n, dpi(), 96); }
int X(double v) { return (int)std::lround(v * dpi() / 96.0); }   // a mockup coordinate in this window's pixels
RECT rc(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }
RECT R(double x, double y, double w, double h) { return RECT{X(x), X(y), X(x + w), X(y + h)}; }

// ---------------------------------------------------------------- the palette (OKLCH, as the mockups)
struct Pal { COLORREF page, page_top, shell, raised, line_strong, accent, accent_hi, accent2, rim, soft_shell, soft_page,
             text = RGB(233, 233, 233), text2 = RGB(180, 180, 180), dim = RGB(147, 147, 147); } P;
void oklch_of(COLORREF c, double& L, double& C, double& H) {
  auto lin = [](double v) { v /= 255.0; return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
  const double r = lin(GetRValue(c)), g = lin(GetGValue(c)), b = lin(GetBValue(c));
  const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b), m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b), s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
  const double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s, B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
  C = std::hypot(A, B); H = std::atan2(B, A);
}
COLORREF from_oklch(double L, double C, double H) {
  const double A = C * std::cos(H), B = C * std::sin(H);
  const double l = std::pow(L + 0.3963377774 * A + 0.2158037573 * B, 3), m = std::pow(L - 0.1055613458 * A - 0.0638541728 * B, 3), s = std::pow(L - 0.0894841775 * A - 1.2914855480 * B, 3);
  auto gam = [](double v) { v = std::clamp(v, 0.0, 1.0); return (int)std::lround(255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055)); };
  return RGB(gam(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s), gam(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s), gam(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s));
}
COLORREF mix(COLORREF a, COLORREF b, double t) {
  return RGB((int)std::lround(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)std::lround(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t), (int)std::lround(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}
void palette() {
  double L, C, H; oklch_of(g_theme, L, C, H);
  const double pc = std::min(C * 0.38, 0.05), La = std::max(L, 0.62);
  P.page = from_oklch(0.277, pc, H); P.page_top = from_oklch(0.32, pc * 1.2, H); P.shell = from_oklch(0.245, pc, H);
  P.raised = from_oklch(0.325, pc + 0.006, H); P.line_strong = from_oklch(0.42, pc + 0.01, H);
  P.accent = from_oklch(La, C, H); P.accent_hi = from_oklch(std::min(La + 0.06, 0.8), C, H);
  P.accent2 = from_oklch(std::max(L - 0.15, 0.3), C * 0.77, H); P.rim = from_oklch(std::min(La + 0.12, 0.86), C * 0.8, H);
  P.soft_shell = mix(P.shell, RGB(255, 255, 255), 0.07); P.soft_page = mix(P.page, RGB(255, 255, 255), 0.07);   // rgba(255,255,255,.07)
}

// ---------------------------------------------------------------- drawing
HFONT g_f13 = nullptr, g_f12 = nullptr, g_f13b = nullptr, g_f15 = nullptr, g_f30 = nullptr, g_f11 = nullptr, g_f11b = nullptr; int g_font_dpi = 0;
HFONT font(double px, int weight, const wchar_t* face = L"Segoe UI") { LOGFONTW lf{}; lf.lfHeight = -X(px); lf.lfWeight = weight; lf.lfQuality = CLEARTYPE_QUALITY; wcscpy_s(lf.lfFaceName, face); return CreateFontIndirectW(&lf); }
void fonts() {
  if (g_font_dpi == dpi()) return;
  for (HFONT f : {g_f13, g_f12, g_f13b, g_f15, g_f30, g_f11, g_f11b}) if (f) DeleteObject(f);
  g_font_dpi = dpi();
  g_f13 = font(13, FW_NORMAL); g_f12 = font(12, FW_NORMAL); g_f13b = font(13, FW_SEMIBOLD); g_f15 = font(15, FW_NORMAL);
  g_f30 = font(30, FW_NORMAL); g_f11 = font(11.5, FW_NORMAL); g_f11b = font(11, FW_SEMIBOLD);
  for (HWND e : {g_code, g_search}) if (e) SendMessageW(e, WM_SETFONT, (WPARAM)g_f13, FALSE);   // never a deleted font
}
Gdiplus::Color gc(COLORREF c, int a = 255) { return Gdiplus::Color((BYTE)a, GetRValue(c), GetGValue(c), GetBValue(c)); }
void round_path(Gdiplus::GraphicsPath& p, double x, double y, double w, double h, double r) {
  const float fx = (float)x, fy = (float)y, fw = (float)w, fh = (float)h, d = (float)std::min({r * 2, w, h});
  if (d <= 0) { p.AddRectangle(Gdiplus::RectF(fx, fy, fw, fh)); return; }
  p.AddArc(fx, fy, d, d, 180, 90); p.AddArc(fx + fw - d, fy, d, d, 270, 90);
  p.AddArc(fx + fw - d, fy + fh - d, d, d, 0, 90); p.AddArc(fx, fy + fh - d, d, d, 90, 90); p.CloseFigure();
}
// A rounded box in mockup coordinates: filled, outlined (1 px inside its edge, as a CSS border), or both.
void box(HDC dc, double x, double y, double w, double h, double r, const COLORREF* fillc, const COLORREF* border) {
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const double k = dpi() / 96.0;
  if (fillc) { Gdiplus::GraphicsPath p; round_path(p, x * k, y * k, w * k, h * k, r * k); Gdiplus::SolidBrush b(gc(*fillc)); g.FillPath(&b, &p); }
  if (border) { Gdiplus::GraphicsPath p; const double t = k; round_path(p, x * k + t / 2, y * k + t / 2, w * k - t, h * k - t, std::max(0.0, r * k - t / 2));
    Gdiplus::Pen pen(gc(*border), (float)t); g.DrawPath(&pen, &p); }
}
void fill_box(HDC dc, double x, double y, double w, double h, double r, COLORREF c) { box(dc, x, y, w, h, r, &c, nullptr); }
void line_box(HDC dc, double x, double y, double w, double h, double r, COLORREF c) { box(dc, x, y, w, h, r, nullptr, &c); }
void hline(HDC dc, double x, double y, double w, COLORREF c) { RECT r = RECT{X(x), X(y), X(x + w), X(y) + std::max(1, X(1))}; HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
void dot(HDC dc, double cx, double cy, double d, COLORREF c) {
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const double k = dpi() / 96.0; Gdiplus::SolidBrush b(gc(c)); g.FillEllipse(&b, (float)((cx - d / 2) * k), (float)((cy - d / 2) * k), (float)(d * k), (float)(d * k));
}
// The mockup's filled icons (24x24 outlines) at a size, in mockup coordinates.
void icon(HDC dc, int id, double x, double y, double px, COLORREF c) {
  if (id < 0 || id >= I_COUNT) return;
  const float* v = kIcons[id]; const double k = dpi() / 96.0, s = px * k / 24.0, ox = x * k, oy = y * k;
  Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
  int i = 0; const int subs = (int)v[i++];
  for (int n = 0; n < subs; ++n) {
    const int segs = (int)v[i++]; Gdiplus::PointF at((float)(ox + v[i] * s), (float)(oy + v[i + 1] * s)); i += 2;
    path.StartFigure();
    for (int q = 0; q < segs; ++q, i += 6) {
      const Gdiplus::PointF c1((float)(ox + v[i] * s), (float)(oy + v[i + 1] * s)), c2((float)(ox + v[i + 2] * s), (float)(oy + v[i + 3] * s)), to((float)(ox + v[i + 4] * s), (float)(oy + v[i + 5] * s));
      path.AddBezier(at, c1, c2, to); at = to;
    }
    path.CloseFigure();
  }
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  Gdiplus::SolidBrush b(gc(c)); g.FillPath(&b, &path);
}
// Plain text from its top-left, as the mockup's text boxes (line height 1.33 of the size). Returns its width.
// The skin's fixed text in the player's language (player report 11-26: it stayed English). Drawing and measuring
// both translate, so layout measures the words it draws; text with no translation comes back unchanged.
std::wstring L10(const std::wstring& s) { return launcher::lang::txw(s); }
// A translated sentence with one {} filled in (a name, a count), so a language can put it where it reads right.
std::wstring fill(const wchar_t* english, const std::wstring& a) {
  std::wstring s = L10(english); const size_t p = s.find(L"{}"); if (p != std::wstring::npos) s.replace(p, 2, a); return s;
}
int text_w(HDC dc, HFONT f, const std::wstring& s0) { const std::wstring s = L10(s0); HGDIOBJ o = SelectObject(dc, f); SIZE z{}; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &z); SelectObject(dc, o); return z.cx; }
int tx(HDC dc, const std::wstring& s0, double x, double y, HFONT f, COLORREF c, double maxw = 0, UINT align = DT_LEFT) {
  const std::wstring s = L10(s0);
  const int w = text_w(dc, f, s);
  RECT r{X(x), X(y), maxw > 0 ? X(x + maxw) : X(x) + w + 2, X(y) + X(40)};
  HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
  DrawTextW(dc, s.c_str(), -1, &r, align | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS); SelectObject(dc, o);
  return w;
}
// Melee's font at the mockup's size n (its cell is 0.78 n), from the cell's top-left; else a bold system font.
int cell_of(double n) { return std::max(6, X(n * 0.78)); }
int mw(HDC dc, const std::wstring& s0, double n) {
  const std::wstring s = L10(s0);
  const int w = launcher::melee_look::width_cell(s, cell_of(n)); if (w >= 0) return w;
  HFONT f = font(n * 0.62, FW_SEMIBOLD); const int v = text_w(dc, f, s); DeleteObject(f); return v;
}
void mt(HDC dc, const std::wstring& s0, double x, double y, double n, COLORREF c, double maxw = 0, double skew = 0, UINT align = DT_LEFT) {
  const std::wstring s = L10(s0);
  // Too long for its room: the text gets smaller (down to 60%, centred where it was) before it is ever cut short.
  if (maxw > 0 && mw(dc, s, n) > X(maxw)) {
    double k = n; const double floor_n = n * 0.6;
    while (k - 0.5 >= floor_n && mw(dc, s, k) > X(maxw)) k -= 0.5;
    y += (n - k) * 0.78 / 2; n = k;
  }
  const int cell = cell_of(n);
  RECT r{X(x), X(y), maxw > 0 ? X(x + maxw) : X(x) + mw(dc, s, n) + X(2), X(y) + cell};
  if (launcher::melee_look::text_cell(dc, s, r, cell, c, align | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS, skew)) return;
  HFONT f = font(n * 0.62, FW_SEMIBOLD); HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
  DrawTextW(dc, s.c_str(), -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS); SelectObject(dc, o); DeleteObject(f);
}
void hit(RECT r, int act, int arg = 0) { g_hits.push_back({r, act, arg}); }
bool is_hot(int act, int arg) { return g_hot >= 0 && g_hot_act == act && g_hot_arg == arg; }   // any order: the list is rebuilt while painting
// The framed accent button (Melee's frame, the accent gradient), label centered in Melee's font.
void frame_button(HDC dc, double x, double y, double w, double h, const std::wstring& label, double n, int act, int arg = 0) {
  const RECT r = R(x, y, w, h); hit(r, act, arg); const bool hot = is_hot(act, arg);
  if (!launcher::melee_look::button(dc, r, hot ? P.accent_hi : P.accent, P.accent2, P.rim, true)) fill_box(dc, x, y, w, h, 6, hot ? P.accent_hi : P.accent);
  mt(dc, label, x, y + (h - n * 0.78) / 2, n, RGB(255, 255, 255), w, 0, DT_CENTER);
}
// The outlined small button (.obtn): Join in the accent, Watch with an eye.
void outline_button(HDC dc, double x, double y, double w, double h, const std::wstring& label, bool accent, bool eye, int act, int arg) {
  const RECT r = R(x, y, w, h); hit(r, act, arg);
  if (is_hot(act, arg)) fill_box(dc, x, y, w, h, 6, P.raised);
  line_box(dc, x, y, w, h, 6, accent ? P.accent : P.line_strong);
  const COLORREF c = accent ? P.accent : P.text; const double tw = text_w(dc, g_f13, label) * 96.0 / dpi();
  const double cw = (eye ? 14 + 5 : 0) + tw, cx = x + (w - cw) / 2;
  if (eye) icon(dc, I_EYE, cx, y + (h - 14) / 2, 14, c);
  tx(dc, label, cx + (eye ? 19 : 0), y + (h - 17) / 2, g_f13, c);
}
// A player's picture: their main's stock icon, else their profile picture, else a plain avatar.
void avatar(HDC dc, int main, double x, double y, double px, const std::string& pic = std::string()) {
  if (main >= 0) { launcher::yglobby::draw_stock(dc, main, X(x), X(y), X(px)); return; }
  if (launcher::yglobby::draw_avatar(dc, pic, X(x), X(y), X(px))) return;
  dot(dc, x + px / 2, y + px / 2, px, P.raised);
  icon(dc, I_PERSON, x + px * 0.2, y + px * 0.2, px * 0.6, P.dim);
}
const COLORREF kOk = RGB(0x74, 0xC2, 0x7A), kAmber = RGB(0xD9, 0xA4, 0x41), kLiveRed = RGB(0xE5, 0x53, 0x4B);
const COLORREF kTier[6] = {RGB(0xD9, 0x90, 0x63), RGB(0xC9, 0xD2, 0xDC), RGB(0xF0, 0xC2, 0x4A), RGB(0x5F, 0xD8, 0xC8), RGB(0x77, 0xBA, 0xF2), RGB(0xFF, 0xC8, 0x57)};
const wchar_t* kTierName[6] = {L"Bronze", L"Silver", L"Gold", L"Platinum", L"Diamond", L"Challenger"};
const wchar_t* kBuild[4] = {L"", L"Akaneia 1.0.1", L"ACE 2.0.1", L"20XX Melee 1.03"};
const COLORREF kBuildCol[4] = {0, RGB(255, 179, 168), RGB(168, 212, 255), RGB(214, 190, 255)};
const wchar_t kDot[] = L" \u00B7 ";

// ---------------------------------------------------------------- the sidebar (216 px)
void sidebar(HDC dc, const launcher::yglobby::Data& d, int H, double sw = 216) {
  const double o = sw - 216;   // narrower beside a launcher page
  { RECT r{0, 0, X(sw), H}; HBRUSH b = CreateSolidBrush(P.shell); FillRect(dc, &r, b); DeleteObject(b);
    RECT e{X(sw - 1), 0, X(sw), H}; b = CreateSolidBrush(P.soft_shell); FillRect(dc, &e, b); DeleteObject(b); }
  mt(dc, L"Melee Unlocked", 22, 17, 25, RGB(242, 242, 242));
  const int icons[6] = {I_PLAY, I_EYE, I_RULES, I_FILM, I_MODS, I_BUILD};
  const wchar_t* names[6] = {L"Play", L"Spectate", L"Game", L"Replays", L"Mods", L"Build"};
  for (int i = 0; i < 6; ++i) {
    const double y = 52.5 + 40 * i; const RECT r = R(12, y, 191 + o, 40);
    const bool sel = g_compact ? i == g_compact_nav : (i == 0 && g_page == 0) || (i == 1 && g_page == 1);
    if (sel) { fill_box(dc, 12, y, 191 + o, 40, 7, P.raised); launcher::melee_look::button(dc, r, P.raised, P.raised, P.accent, true); }
    else if (is_hot(A_NAV, i)) fill_box(dc, 12, y, 191 + o, 40, 7, P.raised);
    hit(r, A_NAV, i);
    icon(dc, icons[i], 24, y + 10, 20, sel ? RGB(255, 255, 255) : P.text2);
    mt(dc, names[i], 56, y + 11.8, 21, sel ? RGB(255, 255, 255) : RGB(201, 201, 201), 120 + o);
    if (i == 1 && d.known && !d.live.empty()) tx(dc, std::to_wstring(d.live.size()), 150 + o, y + 12, g_f12, P.dim, 41, DT_RIGHT);
  }
  // you, then the tools row
  const double my = (H * 96.0 / dpi()) - 88.5;   // 622.5 in a 711 px window
  // Bailey between the pages and your card, whole, centred in that space (as on the Classic rail)
  if (g_art && g_art_w > 0 && g_art_h > 0) {
    const double top = 52.5 + 40 * 6 + 10, bottom = my - 10, bw = 191 + o - 16, bh = bottom - 14 - top;   // she fits between the pages and the card
    if (bh >= 56) {   // too little room (a short page): no Bailey rather than over the pages
      const double k = std::min(bw / g_art_w, bh / g_art_h) * dpi() / 96.0, k2 = std::min(k, 1.0);   // never larger than the picture as loaded (it would blur)
      const int dw = (int)std::lround(g_art_w * k2), dh = (int)std::lround(g_art_h * k2);
      const int dx = X(12 + (191 + o) / 2) - dw / 2, dy = X(bottom - 14) - dh;   // low, just above the card, as on the Classic rail
      // Smoothly scaled (bicubic): AlphaBlend scales by nearest pixels, which made her blocky when smaller.
      DIBSECTION ds{};
      if (GetObjectW(g_art, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
        BYTE* scan0 = (BYTE*)ds.dsBm.bmBits; INT stride = ds.dsBm.bmWidthBytes;
        // the launcher makes her bitmap top row first
        Gdiplus::Bitmap pic(g_art_w, g_art_h, stride, PixelFormat32bppPARGB, scan0);
        Gdiplus::Graphics g(dc); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.DrawImage(&pic, Gdiplus::Rect(dx, dy, dw, dh), 0, 0, g_art_w, g_art_h, Gdiplus::UnitPixel);
      } else {
        HDC md = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(md, g_art);
        const BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(dc, dx, dy, dw, dh, md, 0, 0, g_art_w, g_art_h, bf);
        SelectObject(md, old); DeleteDC(md);
      }
      hit(RECT{dx, dy, dx + dw, dy + dh}, A_RAIL_ART);
    }
  }
  fill_box(dc, 12, my, 191 + o, 50.5, 9, P.raised);
  if (!d.me_known && launcher::yglobby::me_pending()) {
    icon(dc, I_PEOPLE, 22, my + 12, 26, P.dim);
    mt(dc, L"Loading...", 61, my + 10.9, 19, RGB(255, 255, 255), 96 + o);
    tx(dc, L"your YouGame account", 61, my + 26.5, g_f12, P.dim, 110 + o);
  } else if (d.me_known) {
    hit(R(12, my, 191 + o, 50.5), A_ME);   // the card: your YouGame page; the picture on it: your mains
    avatar(dc, d.me.main, 22, my + 10.2, 30, d.me.avatar); hit(R(22, my + 10.2, 30, 30), A_MY_MAIN);
    mt(dc, d.me.name, 61, d.me.code.empty() ? my + (50.5 - 20 * 0.78) / 2 : my + 10.9, 20, RGB(255, 255, 255), 96 + o);
    tx(dc, d.me.code, 61, my + 26.5, g_f12, P.dim, 96 + o);
    launcher::yglobby::draw_tier_badge(dc, launcher::yglobby::my_tier(d), X(163 + o), X(my + 10.2), X(30));
  } else {   // signed out: sign in by code (the code shows here while YouGame waits for it)
    std::wstring text; const auto cs = launcher::yglobby::code_sign_in(&text);
    icon(dc, I_PEOPLE, 22, my + 12, 26, P.dim);
    if (cs == launcher::yglobby::CodeSignIn::kWaiting && !text.empty()) {
      mt(dc, text, 61, my + 10.9, 19, RGB(255, 255, 255), 120 + o);
      tx(dc, L"at yougame.co/link", 61, my + 26.5, g_f12, P.dim, 130 + o);
    } else if (cs == launcher::yglobby::CodeSignIn::kStarting) {
      mt(dc, L"Sign in", 61, my + 10.9, 19, RGB(255, 255, 255), 120 + o);
      tx(dc, L"Getting a code...", 61, my + 26.5, g_f12, P.dim, 130 + o);
    } else {
      mt(dc, L"Sign in", 61, my + 10.9, 19, RGB(255, 255, 255), 120 + o);
      tx(dc, cs == launcher::yglobby::CodeSignIn::kFailed && !text.empty() ? text : std::wstring(L"with a YouGame code"), 61, my + 26.5, g_f12, cs == launcher::yglobby::CodeSignIn::kFailed ? kAmber : P.dim, 130 + o);
    }
    hit(R(12, my, 191 + o, 50.5), A_SIGN_IN);
  }
  const int tools[4] = {I_GEAR, I_PALETTE, I_BUG, I_GLOBE}; double tx_[4];
  for (int i = 0; i < 4; ++i) tx_[i] = 12 + (i + 0.5) * (191 + o) / 4 - 9;   // spaced around, as the mockup's row
  for (int i = 0; i < 4; ++i) {
    const double ty = my + 60.5;
    if (is_hot(A_TOOL, i)) fill_box(dc, tx_[i] - 7, ty - 5, 32, 28, 6, P.raised);
    icon(dc, tools[i], tx_[i], ty, 18, P.dim); hit(R(tx_[i] - 7, ty - 5, 32, 28), A_TOOL, i);
  }
}

void place_edit(HWND e, RECT r) {
  if (!e) return;
  RECT cur{}; GetWindowRect(e, &cur); MapWindowPoints(nullptr, g_window, (POINT*)&cur, 2);
  if (!EqualRect(&cur, &r)) MoveWindow(e, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
  if (!IsWindowVisible(e)) ShowWindow(e, SW_SHOWNA);
}
std::wstring status_line(const wchar_t* what) {
  switch (launcher::yglobby::source_state()) {
    case 0: return fill(L"Loading {} from YouGame...", L10(what));
    case 3: return L10(L"Can't reach YouGame right now. Trying again shortly.");
    default: return fill(L"YouGame hasn't opened {} to the launcher yet. This fills in by itself once it does.", L10(what));
  }
}
// A thin thumb at a list's right edge, only when the list has more rows than fit.
void scrollbar(HDC dc, const Scroll& s, double x, double top, double h) {
  if (s.total <= s.fit || s.total <= 0 || h < 20) return;
  const double th = std::max(24.0, h * s.fit / s.total), ty = top + (h - th) * s.skip / std::max(1, s.total - s.fit);
  fill_box(dc, x, top, 4, h, 2, P.soft_shell);
  fill_box(dc, x, std::min(ty, top + h - th), 4, th, 2, P.dim);
}
// The wheel over a list: three rows a notch, never past its last row.
bool wheel(POINT p, int delta) {
  for (Scroll* s : {&g_rail_scroll, &g_list_scroll}) if (PtInRect(&s->area, p)) {
    static int acc = 0; acc += delta;   // touchpads send small steps
    const int rows = acc * 3 / WHEEL_DELTA; acc -= rows * WHEEL_DELTA / 3;
    const int before = s->skip;
    s->skip = std::clamp(s->skip - rows, 0, std::max(0, s->total - s->fit));
    return s->skip != before;
  }
  return false;
}
void sparkline(HDC dc, const std::vector<int>& v, double x, double y, double w, double h, COLORREF c) {
  if (v.size() < 2) return;
  const int lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
  const double k = dpi() / 96.0; std::vector<Gdiplus::PointF> pts;
  for (size_t i = 0; i < v.size(); ++i)
    pts.push_back(Gdiplus::PointF((float)((x + (double)i / (v.size() - 1) * w) * k), (float)((y + h - 2 - (double)(v[i] - lo) / std::max(1, hi - lo) * (h - 4)) * k)));
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::Pen pen(gc(c), (float)(1.6 * k)); pen.SetLineJoin(Gdiplus::LineJoinRound); g.DrawLines(&pen, pts.data(), (INT)pts.size());
}

// ---------------------------------------------------------------- the Play page (the page column starts at 216)
void play_page(HDC dc, const launcher::yglobby::Data& d, double bottom) {
  mt(dc, L"Play", 244, 24.1, 46, RGB(255, 255, 255), 0, 12);
  if (d.known) {
    const double tx0 = 244 + mw(dc, L"Play", 46) * 96.0 / dpi() + 14;
    dot(dc, tx0 + 3.5, 45, 7, kOk); tx(dc, fill(d.online == 1 ? L"{} player in-game" : L"{} players in-game", std::to_wstring(d.online)), tx0 + 13, 36.5, g_f13, P.text2);
  }
  // The main way in: Melee itself (every mode, online too, from its own menus), as big as the page's heading.
  { const double w = std::max(190.0, mw(dc, L"Play Melee", 26) * 96.0 / dpi() + 48); frame_button(dc, 964 - w, 20, w, 44, L"Play Melee", 26, A_PLAY_OFFLINE); }
  // the queues and your rank
  fill_box(dc, 244, 80, 720, 130, 10, P.raised);
  const wchar_t* q[3] = {L"Play unranked", L"Play ranked", L"Play 2v2"};
  for (int i = 0; i < 3; ++i) {
    const double y = 92 + 37 * i;
    frame_button(dc, 264, y, 190, 32, q[i], 21, A_QUEUE, i);
    if (d.known) { icon(dc, i == 2 ? I_FRIEND : I_PEOPLE, 468, y + 8.5, 15, P.text2); tx(dc, fill(L"{} in queue", std::to_wstring(d.waiting[i])), 488, y + 7.5, g_f13, P.text2); }
  }
  {
    const int t = d.me_known ? launcher::yglobby::my_tier(d) : -1;
    std::wstring l1, l3; COLORREF c1 = P.text2; bool big = false;
    if (!d.me_known) { l1 = L"Not signed in"; l3 = L"Sign in on YouGame to see your rank"; }
    else if (t >= 0) {
      // YouGame's own label with its division ("Silver II") when it names this tier, as the YouGame page shows it.
      l1 = d.me_label.rfind(kTierName[t], 0) == 0 ? d.me_label : std::wstring(kTierName[t]); c1 = kTier[t]; big = true;
      if (d.me_matches >= 0) l3 = fill(d.me_matches == 1 ? L"{} ranked match" : L"{} ranked matches", std::to_wstring(d.me_matches));
      if (d.me_wins >= 0 && d.me_losses >= 0) l3 = fill(L"{} ranked sets", std::to_wstring(d.me_matches)) + kDot + std::to_wstring(d.me_wins) + L"W-" + std::to_wstring(d.me_losses) + L"L";
    } else { l1 = L"Unranked"; l3 = d.me_label.empty() ? L"Play ranked to place" : d.me_label; }
    const bool spark = big && d.me_history.size() >= 2;
    const double tw = std::max({140.0, text_w(dc, g_f15, l1) * 96.0 / dpi(), text_w(dc, g_f12, l3) * 96.0 / dpi()});
    const double h = big ? (spark ? 101.5 : 72) : 40, top = 80 + (130 - std::max(76.0, h)) / 2 + std::max(0.0, (76 - h) / 2);
    const double bx = 944 - tw - 16 - 76, by = 80 + (130 - 76) / 2.0;
    launcher::yglobby::draw_tier_badge(dc, t, X(bx), X(by), X(76));
    const double x = bx + 92;
    tx(dc, l1, x, top, g_f15, c1);
    if (big) { tx(dc, std::to_wstring(d.me.rating), x, top + 17, g_f30, P.text); if (!l3.empty()) tx(dc, l3, x, top + 54.5, g_f12, P.dim, tw); }
    else tx(dc, l3, x, top + 24, g_f12, P.dim, tw);
    if (spark) sparkline(dc, d.me_history, x, top + 76.5, 140, 22, c1);
  }
  // Public rooms
  const double rt = 226, rb = bottom - (g_cb.status ? 50 : 18);   // room for the status line
  line_box(dc, 244, rt, 720, rb - rt, 10, P.line_strong);
  mt(dc, L"Public rooms", 261, 247, 23, RGB(255, 255, 255));
  // In a room (made here or joined): the button leaves it, so a host can always close their room (owner 11-27: a
  // friend's room stayed listed with no way out of it short of closing the launcher).
  const bool in_room = !launcher::room::current().code.empty();
  const wchar_t* room_label = in_room ? L"Leave room" : L"Create room";
  const double cw = mw(dc, room_label, 20) * 96.0 / dpi() + 32, cx = 947 - cw;
  frame_button(dc, cx, 239, cw, 34, room_label, 20, in_room ? A_LEAVE_ROOM : A_CREATE);
  const double kx = cx - 10 - 200;
  line_box(dc, kx, 239, 200, 34, 7, P.line_strong);
  { const double jw = text_w(dc, g_f13b, L"Join") * 96.0 / dpi() + 20, jx = kx + 200 - 5 - jw; const RECT j = R(jx, 243, jw, 26); hit(j, A_JOIN_CODE);
    if (is_hot(A_JOIN_CODE, 0)) fill_box(dc, jx, 243, jw, 26, 5, P.raised);
    tx(dc, L"Join", jx + 10, 247.5, g_f13b, P.accent);
    place_edit(g_code, RECT{X(kx + 11), X(248), X(jx - 4), X(248) + X(17)}); }
  { const double sw = text_w(dc, g_f13, L"Standard") * 96.0 / dpi(), sx = kx - 10 - sw;
    tx(dc, L"Standard", sx, 247.5, g_f13, P.text2); icon(dc, I_RULES, sx - 5 - 17, 247.5, 17, P.text2); }
  hline(dc, 245, 285, 718, P.soft_page);
  double y = 286;
  if (!d.known || d.rooms.empty()) {
    RECT r = R(264, y + 30, 680, 60); HGDIOBJ o = SelectObject(dc, g_f13); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, P.dim);
    const std::wstring s = d.known ? L10(L"No public rooms right now. Create one!") : status_line(L"public rooms");
    DrawTextW(dc, s.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX); SelectObject(dc, o);
    return;
  }
  Scroll& s = g_list_scroll; s.area = R(244, 286, 720, rb - 286); s.total = (int)d.rooms.size(); s.skip = std::clamp(s.skip, 0, std::max(0, s.total - 1)); s.fit = 0;
  for (int i = s.skip; i < (int)d.rooms.size(); ++i) {
    const auto& r = d.rooms[i]; if (y + 54 > rb - 2) break;
    ++s.fit;
    if (i > s.skip) hline(dc, 245, y - 1, 718, P.soft_page);
    double ix = 261; for (int m : r.members) { if (ix > 340) break; avatar(dc, m, ix, y + 15.5, 22); ix += 24; }
    mt(dc, r.name, 365, y + 10.8, 19, RGB(240, 240, 240), 380);
    // host \u00B7 build \u00B7 mode \u00B7 [rules] rules
    double sx = 365; const double sy = y + 29.6;
    sx += text_w(dc, g_f12, (r.host.empty() ? r.code : r.host)) * 96.0 / dpi(); tx(dc, r.host.empty() ? r.code : r.host, 365, sy, g_f12, P.dim);
    if (r.build > 0 && r.build < 4) { sx += tx(dc, kDot, sx, sy, g_f12, P.dim) * 96.0 / dpi(); sx += tx(dc, kBuild[r.build], sx, sy, g_f12, kBuildCol[r.build]) * 96.0 / dpi(); }
    sx += tx(dc, std::wstring(kDot) + (r.mode == 1 ? L"Teams" : r.mode == 2 ? L"FFA" : L"1v1"), sx, sy, g_f12, P.dim) * 96.0 / dpi();
    if (!r.rules.empty()) { sx += tx(dc, kDot, sx, sy, g_f12, P.dim) * 96.0 / dpi(); icon(dc, I_RULES, sx, sy + 2, 11, P.dim); tx(dc, L" " + r.rules, sx + 11, sy, g_f12, P.dim); }
    tx(dc, std::to_wstring(r.players) + L"/" + std::to_wstring(r.size), 759, y + 18, g_f13, P.text2, 32, DT_RIGHT);
    tx(dc, r.in_game ? L"In game" : L"Waiting", 803, y + 18, g_f13, r.in_game ? kAmber : kOk);
    if (r.in_game) outline_button(dc, 883, y + 11.5, 64, 30, L"Watch", false, true, A_ROOM, i);
    else if (r.players >= r.size) { line_box(dc, 883, y + 11.5, 64, 30, 6, P.line_strong); tx(dc, L"Full", 883, y + 18, g_f13, P.dim, 64, DT_CENTER); }
    else outline_button(dc, 883, y + 11.5, 64, 30, L"Join", true, false, A_ROOM, i);
    y += 54;
  }
  scrollbar(dc, s, 957, 290, rb - 290 - 6);
}

// ---------------------------------------------------------------- the status line (Play and Spectate)
// Accounts, mods, version and language, as the Classic Play page lists them: one quiet line under the page's box.
void status_line_bar(HDC dc, double bottom) {
  if (!g_cb.status) return;
  const auto s = g_cb.status();
  const double y = bottom - 38, h = 24;
  const std::wstring extension = launcher::lang::txw(L"Launch with YouGame extension");
  const double language_width = text_w(dc, g_f12, s.language) * 96.0 / dpi();
  const double language_x = 960 - language_width;
  const double extension_width = text_w(dc, g_f12, extension) * 96.0 / dpi() + 20;
  const double extension_x = language_x - 34 - extension_width;
  const double content_right = extension_x - 12;
  double x = 248;
  auto part = [&](int id, bool dotted, COLORREF dot_c, const std::wstring& label, const std::wstring& value, COLORREF value_c, double limit = 0.0) {
    const double x0 = x; const bool hot = is_hot(A_STATUS, id);
    const double right = limit > 0 ? std::min(content_right, x0 + limit) : content_right;
    if (right <= x0 + 12) return;
    if (dotted) { dot(dc, x + 3.5, y + h / 2, 7, dot_c); x += 12; }
    const COLORREF vc = hot ? RGB(255, 255, 255) : value_c;
    if (g_status_style == 1) {   // B: one font (the page's), the value brighter and bolder
      if (!label.empty()) x += tx(dc, label, x, y + 4, g_f12, P.dim) * 96.0 / dpi() + 4;
      const double available = std::max(1.0, right-x);
      tx(dc, value, x, y+3.5, g_f13b, vc, available);
      x += std::min(text_w(dc,g_f13b,value)*96.0/dpi(),available);
    } else if (g_status_style == 2) {   // C: Melee's font throughout
      if (!label.empty()) { mt(dc, label, x, y + 7, 15, P.dim); x += mw(dc, label, 15) * 96.0 / dpi() + 6; }
      const double available = std::max(1.0, right-x);
      mt(dc, value, x, y + 5.6, 17, vc, available);
      x += std::min(mw(dc, value, 17)*96.0/dpi(), available);
    } else {   // A: the label in the page's font, the value in Melee's, on one baseline
      if (!label.empty()) x += tx(dc, label, x, y + 4.5, g_f12, P.dim) * 96.0 / dpi() + 5;
      // y + 4.6: the value's baseline on the label's (measured 11-24: at y + 6.6 every value sat 2 px under its
      // label, "none" most visibly, owner's report).
      const double available = std::max(1.0, right-x);
      mt(dc, value, x, y + 4.6, 17, vc, available);
      x += std::min(mw(dc, value, 17)*96.0/dpi(), available);
    }
    if (hot) hline(dc, x0 + (dotted ? 12 : 0), y + 20, x - x0 - (dotted ? 12 : 0), P.dim);
    hit(R(x0 - 4, y, x - x0 + 8, h), A_STATUS, id);
    x += 10;
  };
  part(0, true, s.slippi_ok ? kOk : P.dim, L"Slippi", s.slippi, P.text2, 126);
  part(1, true, s.yougame_ok ? kOk : P.dim, L"YouGame", s.yougame, s.yougame_ok ? P.text2 : P.accent, 134);
  std::wstring mods = s.mods;
  const size_t prefix = mods.find(L": ");
  if (prefix != std::wstring::npos) mods.erase(0,prefix+2);
  const double version_width = (g_status_style == 1 ? text_w(dc,g_f13b,s.version) : mw(dc,s.version,17))*96.0/dpi()+12;
  part(2, true, s.mods_on ? kOk : P.dim, L"Mods", mods, P.text2, std::max(45.0, content_right-x-version_width));
  part(3, false, 0, L"", s.version, P.text2);
  if (!s.update.empty()) part(4, false, 0, L"", s.update, P.accent);
  const bool extension_hot = is_hot(A_STATUS, 6);
  line_box(dc, extension_x, y+6, 12, 12, 3, extension_hot ? P.accent : P.dim);
  if (s.extension) {
    fill_box(dc, extension_x+2, y+8, 8, 8, 1, P.accent);
  }
  tx(dc, extension, extension_x+19, y+4, g_f12, extension_hot ? RGB(255,255,255) : P.text2);
  hit(R(extension_x-4,y,extension_width+8,h),A_STATUS,6);
  // the language, at the right edge, with the globe
  const double lw = language_width, lx = language_x;
  {
    const bool hot = is_hot(A_STATUS, 5);
    icon(dc, I_GLOBE, lx - 20, y + 4, 15, hot ? RGB(255, 255, 255) : P.dim);
    tx(dc, s.language, lx, y + 4, g_f12, hot ? RGB(255, 255, 255) : P.text2);
    hit(R(lx - 24, y, lw + 28, h), A_STATUS, 5);
  }
}

// ---------------------------------------------------------------- the Spectate page
void spectate_page(HDC dc, const launcher::yglobby::Data& d, double bottom) {
  if (g_code) ShowWindow(g_code, SW_HIDE);
  mt(dc, L"Spectate", 244, 24.1, 46, RGB(255, 255, 255), 0, 12);
  if (d.known) { const double tx0 = 244 + mw(dc, L"Spectate", 46) * 96.0 / dpi() + 14;
    dot(dc, tx0 + 3.5, 45, 7, kLiveRed); tx(dc, std::to_wstring(d.live.size()) + L" live games", tx0 + 13, 36.5, g_f13, P.text2); }
  if (!d.known || d.live.empty()) {
    RECT r = R(264, 140, 680, 60); HGDIOBJ o = SelectObject(dc, g_f13); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, P.dim);
    const std::wstring s = d.known ? (d.in_match > 0 ? fill(d.in_match == 1 ? L"{} player is in a match. YouGame doesn't list matches to watch yet; they show here once it does." : L"{} players are in a match. YouGame doesn't list matches to watch yet; they show here once it does.", std::to_wstring(d.in_match)) : L10(L"No matches being played right now.")) : status_line(L"matches being played");
    DrawTextW(dc, s.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX); SelectObject(dc, o);
    return;
  }
  const auto& top = d.live.front();
  fill_box(dc, 244, 80, 720, 164, 10, P.raised);
  fill_box(dc, 266, 98, 47, 20, 9, RGB(0x5A, 0x1F, 0x26)); tx(dc, L"\u25CF Live", 274, 100, g_f12, RGB(0xFF, 0x8F, 0x8F));
  tx(dc, top.what + (top.game.empty() ? L"" : kDot + top.game), 323.1, 100, g_f12, P.dim, 500);
  { const std::wstring n = std::to_wstring(top.viewers); const double nw = text_w(dc, g_f13, n) * 96.0 / dpi();
    tx(dc, n, 942 - nw, 99.5, g_f13, P.text2); icon(dc, I_EYE, 942 - nw - 5 - 15, 100.5, 15, P.text2); }
  { // the two sides, centered: [stock name] ... vs ... [name stock]
    auto side_w = [&](const std::vector<launcher::yglobby::Side>& s) { double w = 0; for (const auto& q : s) w += (w ? 8 : 0) + 52 + 8 + mw(dc, q.name, 21) * 96.0 / dpi(); return w; };
    const double vsw = mw(dc, L"vs", 26) * 96.0 / dpi(), total = side_w(top.a) + 26 + vsw + 26 + side_w(top.b);
    double x = 604 - total / 2; const double cy = 156;
    for (const auto& q : top.a) { avatar(dc, q.main, x, cy - 26, 52); x += 60; mt(dc, q.name, x, cy - 8.2, 21, RGB(255, 255, 255)); x += mw(dc, q.name, 21) * 96.0 / dpi() + 8; }
    x += 18; mt(dc, L"vs", x, cy - 10.1, 26, P.dim); x += vsw + 26;
    for (const auto& q : top.b) { mt(dc, q.name, x, cy - 8.2, 21, RGB(255, 255, 255)); x += mw(dc, q.name, 21) * 96.0 / dpi() + 8; avatar(dc, q.main, x, cy - 26, 52); x += 60; }
  }
  frame_button(dc, 494, 194, 220, 32, L"Watch", 21, A_WATCH, 0);
  const double lt = 260, lb = bottom - (g_cb.status ? 50 : 18);
  line_box(dc, 244, lt, 720, lb - lt, 10, P.line_strong);
  mt(dc, L"Live now", 261, 273, 23, RGB(255, 255, 255));
  hline(dc, 245, 302.9, 718, P.soft_page);
  double y = 303.9;
  Scroll& s = g_list_scroll; s.area = R(244, 304, 720, lb - 304); s.total = (int)d.live.size() - 1; s.skip = std::clamp(s.skip, 0, std::max(0, s.total - 1)); s.fit = 0;
  for (int i = 1 + s.skip; i < (int)d.live.size(); ++i) {
    const auto& m = d.live[i]; if (y + 54 > lb - 2) break;
    ++s.fit;
    if (i > 1 + s.skip) hline(dc, 245, y - 1, 718, P.soft_page);
    double x = 261;
    auto side = [&](const std::vector<launcher::yglobby::Side>& s) {
      for (const auto& q : s) { if (x > 600) return; avatar(dc, q.main, x, y + 15, 24); mt(dc, q.name, x + 30, y + 19.1, 19, RGB(240, 240, 240), std::min(140.0, 630 - x)); x += 30 + mw(dc, q.name, 19) * 96.0 / dpi() + 10; }
    };
    side(m.a); x += tx(dc, L"vs", x, y + 18.5, g_f12, P.dim) * 96.0 / dpi() + 10; side(m.b);
    tx(dc, m.what + (m.game.empty() ? L"" : kDot + m.game), 645, y + 18.5, g_f12, P.dim, 170);
    icon(dc, I_EYE, 827, y + 19.5, 14, P.text2); tx(dc, std::to_wstring(m.viewers), 846, y + 18, g_f13, P.text2);
    outline_button(dc, 883, y + 11.5, 64, 30, L"Watch", false, true, A_WATCH, i);
    y += 54;
  }
  scrollbar(dc, s, 957, 308, lb - 308 - 6);
}

// ---------------------------------------------------------------- the rail (272 px)
void rail(HDC dc, const launcher::yglobby::Data& d, int W, int H) {
  const double L0 = W * 96.0 / dpi() - 272;   // 992 in a 1264 px window
  { RECT r{X(L0), 0, W, H}; HBRUSH b = CreateSolidBrush(P.shell); FillRect(dc, &r, b); DeleteObject(b);
    RECT e{X(L0), 0, X(L0 + 1), H}; b = CreateSolidBrush(P.soft_shell); FillRect(dc, &e, b); DeleteObject(b); }
  const double x0 = L0 + 13;   // 1005, the rail's content edge
  int friends = 0; for (const auto& q : d.players) friends += q.friend_;
  const std::wstring tabs[2] = {L10(L"Online") + (d.known ? L" " + std::to_wstring(d.online) : L""), L10(L"Friends") + (d.known ? L" " + std::to_wstring(friends) : L"")};
  double x = x0 + 6;
  for (int i = 0; i < 2; ++i) {
    const double w = text_w(dc, g_f13, tabs[i]) * 96.0 / dpi();
    tx(dc, tabs[i], x, 16, g_f13, g_rail_tab == i ? RGB(255, 255, 255) : P.dim);
    if (g_rail_tab == i) hline(dc, x, 37, w, P.accent), hline(dc, x, 38, w, P.accent);
    hit(R(x - 4, 12, w + 8, 30), A_RAIL_TAB, i); x += w + 18;
  }
  { const double sx = x0 + 247 - 6 - 28; fill_box(dc, sx, 19.5, 28, 16, 8, g_elo ? P.accent : P.raised);
    dot(dc, sx + (g_elo ? 14 : 2) + 6, 27.5, 12, RGB(255, 255, 255));
    const double ew = text_w(dc, g_f11, L"Elo") * 96.0 / dpi(); tx(dc, L"Elo", sx - 6 - ew, 20, g_f11, g_elo ? RGB(255, 255, 255) : P.dim);
    hit(R(sx - 12 - ew, 14, ew + 40, 28), A_ELO); }
  hline(dc, x0, 49, 247, P.soft_shell);
  fill_box(dc, x0 + 4, 60, 239, 32, 7, P.page);
  icon(dc, I_SEARCH, x0 + 14, 68, 16, P.dim);
  place_edit(g_search, RECT{X(x0 + 38), X(68), X(x0 + 236), X(68) + X(17)});
  double y = 98;
  const double bottom = H * 96.0 / dpi();
  if (!d.known || d.players.empty()) {
    RECT r = R(x0 + 8, y + 60, 231, 80); HGDIOBJ o = SelectObject(dc, g_f13); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, P.dim);
    const std::wstring s = !d.known ? status_line(L"who's online") : !d.players_known ? L10(L"Sign in to see who's online.") : L10(L"Nobody else is online right now.");
    if (d.known && !d.players_known) hit(r, A_SIGN_IN);   // that line signs in too
    DrawTextW(dc, s.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX); SelectObject(dc, o);
    return;
  }
  wchar_t q[40] = {}; if (g_search) GetWindowTextW(g_search, q, 39);
  std::wstring filter = q; for (auto& c : filter) c = (wchar_t)towlower(c);
  auto matches = [&](const launcher::yglobby::Player& pl) { if (filter.empty()) return true; std::wstring n = pl.name; for (auto& c : n) c = (wchar_t)towlower(c); return n.find(filter) != std::wstring::npos; };
  auto section = [&](const wchar_t* name) { tx(dc, name, x0 + 6, y + 8, g_f12, P.dim); y += 26; };
  auto row = [&](const launcher::yglobby::Player& pl) {
    const bool build = pl.build > 0 && pl.build < 4;
    const double block = 13.26 + 3 + 16 + (build ? 16 : 0), content = std::max({33.0, block, g_elo ? 40.0 : 0.0}), h = content + 12;
    if (y + h > bottom - 4) return false;
    hit(R(x0, y, 247, h), A_PLAYER_MENU, (int)(&pl - &d.players[0]));   // the player: friend menu (the eye, added later, wins)
    if (is_hot(A_PLAYER_MENU, (int)(&pl - &d.players[0]))) fill_box(dc, x0, y + 1, 247, h - 2, 7, P.raised);
    const double ay = y + (h - 33) / 2;
    avatar(dc, pl.main, x0 + 6, ay, 30, pl.avatar);
    dot(dc, x0 + 33, ay + 30, 10, P.shell); dot(dc, x0 + 33, ay + 30, 6, pl.in_match ? kAmber : kOk);
    const double ty = y + 6 + (content - block) / 2;
    double nx = x0 + 42;
    if (pl.friend_) { icon(dc, I_FRIEND, nx, ty + 0.6, 12, P.accent); nx += 18; }
    mt(dc, pl.name, nx, ty, 17, RGB(240, 240, 240), x0 + 177 - nx);
    const int t = launcher::yglobby::tier_of_rating(pl.rating);
    double sx = x0 + 42; const double sy = ty + 16.26;
    sx += tx(dc, pl.in_match ? L"In match" : L"Online", sx, sy, g_f12, pl.in_match ? kAmber : P.dim) * 96.0 / dpi();
    if (!g_elo || t < 0) { sx += tx(dc, kDot, sx, sy, g_f12, P.dim) * 96.0 / dpi(); tx(dc, t >= 0 ? kTierName[t] : L"Unranked", sx, sy, g_f12, t >= 0 ? kTier[t] : P.dim); }   // no rating: say Unranked, Elo on or off
    if (build) tx(dc, kBuild[pl.build], x0 + 42, sy + 16, g_f12, kBuildCol[pl.build], 135);
    if (pl.in_match) { icon(dc, I_EYE, x0 + 184, y + (h - 16) / 2, 16, P.accent); hit(R(x0 + 180, y + (h - 24) / 2, 24, 24), A_WATCH_PLAYER); }
    const double by = g_elo ? y + 6 + (content - 40) / 2 : y + (h - 22) / 2;
    launcher::yglobby::draw_badge(dc, pl.rating, X(x0 + 213), X(by), X(22));
    if (g_elo) tx(dc, pl.rating >= 0 ? std::to_wstring(pl.rating) : L"\u2013", x0 + 207, by + 23, g_f11b, t >= 0 ? kTier[t] : P.dim, 34, DT_CENTER);
    y += h; return true;
  };
  int shown_friends = 0, shown_others = 0;
  for (const auto& pl : d.players) if (matches(pl)) (pl.friend_ ? shown_friends : shown_others)++;
  if (g_rail_tab == 1) shown_others = 0;
  g_rail_scroll.area = R(L0, 98, 272, bottom - 98);
  if (!shown_friends && !shown_others) {
    RECT r = R(x0 + 8, y + 40, 231, 60); HGDIOBJ o = SelectObject(dc, g_f13); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, P.dim);
    const std::wstring msg = !filter.empty() ? fill(L"No one here matches \u201C{}\u201D.", q) : g_rail_tab == 1 ? L10(L"None of your friends are online.") : L10(L"Nobody else is online right now.");
    if (!filter.empty()) {   // add that name as a friend
      const std::wstring add = fill(L"Add \u201C{}\u201D as a friend", q);
      const bool hot = is_hot(A_ADD_TYPED, 0);
      tx(dc, add, x0 + 8, y + 100, g_f13, hot ? RGB(255, 255, 255) : P.accent, 231, DT_CENTER);
      hit(R(x0 + 8, y + 96, 231, 24), A_ADD_TYPED, 0);
    }
    DrawTextW(dc, msg.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX); SelectObject(dc, o);
    return;
  }
  // Friends first, then everyone; the wheel skips whole rows (each section's heading stays with its first row shown).
  std::vector<const launcher::yglobby::Player*> list;
  for (const auto& pl : d.players) if (pl.friend_ && matches(pl) && shown_friends) list.push_back(&pl);
  for (const auto& pl : d.players) if (!pl.friend_ && matches(pl) && shown_others) list.push_back(&pl);
  Scroll& s = g_rail_scroll; s.total = (int)list.size(); s.skip = std::clamp(s.skip, 0, std::max(0, s.total - 1));
  int last = -1; s.fit = 0;
  for (int k = s.skip; k < s.total; ++k) {
    const int sec = list[k]->friend_ ? 0 : 1;
    if (sec != last) { if (y + 26 + 45 > bottom - 4) break; section(sec == 0 ? L"Friends" : L"Everyone"); last = sec; }
    if (!row(*list[k])) break;
    ++s.fit;
  }
  scrollbar(dc, s, L0 + 265, 98 + 4, bottom - 98 - 8);
}

// The page's faint grain (white, 5%), as the mockup lays over it.
void grain(HDC dc, RECT r) {
  static HDC tile = nullptr; static int side = 0;
  if (!tile) {
    side = 64; BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = side; bi.bmiHeader.biHeight = -side;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; void* bits = nullptr;
    HBITMAP bm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); if (!bm) return;
    uint32_t seed = 0x9E3779B9u; uint32_t* px = (uint32_t*)bits;
    for (int i = 0; i < side * side; ++i) {
      seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
      const uint32_t a = (seed & 1) ? ((seed >> 8) % 70) * 5 / 100 : 0;   // the texture's alpha (mean ~28) at 5%
      px[i] = a << 24 | a << 16 | a << 8 | a;
    }
    tile = CreateCompatibleDC(nullptr); SelectObject(tile, bm);
  }
  const BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  for (int y = r.top; y < r.bottom; y += side) for (int x = r.left; x < r.right; x += side)
    AlphaBlend(dc, x, y, std::min(side, (int)(r.right - x)), std::min(side, (int)(r.bottom - y)), tile, 0, 0, std::min(side, (int)(r.right - x)), std::min(side, (int)(r.bottom - y)), bf);
}

void paint(HDC target) {
  RECT client{}; GetClientRect(g_window, &client); const int W = client.right, H = client.bottom;
  { const int real = GetDpiForWindow(g_window);
    const double f = g_compact ? 1.0 : std::min({1.0, W / (1264.0 * real / 96), H / (560.0 * real / 96)});
    g_fit_dpi = std::max(48, (int)std::floor(real * f)); }
  HDC dc = CreateCompatibleDC(target); HBITMAP bm = CreateCompatibleBitmap(target, W, H); HGDIOBJ old = SelectObject(dc, bm);
  fonts(); palette(); g_hits.clear(); g_elo = launcher::yglobby::elo();
  const auto d = launcher::yglobby::current();
  if (g_compact) {
    sidebar(dc, d, H, W * 96.0 / dpi());
    BitBlt(target, 0, 0, W, H, dc, 0, 0, SRCCOPY); SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); return;
  }
  const RECT page = RECT{X(216), 0, W - X(272), H};
  TRIVERTEX v[2] = {{page.left, 0, (COLOR16)(GetRValue(P.page_top) << 8), (COLOR16)(GetGValue(P.page_top) << 8), (COLOR16)(GetBValue(P.page_top) << 8), 0},
                    {page.right, std::min(H, X(320)), (COLOR16)(GetRValue(P.page) << 8), (COLOR16)(GetGValue(P.page) << 8), (COLOR16)(GetBValue(P.page) << 8), 0}};
  GRADIENT_RECT g{0, 1}; GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
  { RECT rest{page.left, std::min(H, X(320)), page.right, H}; HBRUSH b = CreateSolidBrush(P.page); FillRect(dc, &rest, b); DeleteObject(b); }
  grain(dc, page);
  sidebar(dc, d, H);
  const double bottom = H * 96.0 / dpi();
  if (g_page == 0) play_page(dc, d, bottom); else spectate_page(dc, d, bottom);
  status_line_bar(dc, bottom);
  rail(dc, d, W, H);
  if (!g_toast.empty() && GetTickCount64() < g_toast_until) {
    const double tw = text_w(dc, g_f13, g_toast) * 96.0 / dpi() + 28, tl = 216 + (W * 96.0 / dpi() - 488 - tw) / 2, tt = bottom - (g_cb.status ? 92 : 60);   // above the status line
    box(dc, tl, tt, tw, 34, 8, &P.raised, &P.line_strong); tx(dc, g_toast, tl, tt + 8.5, g_f13, P.text, tw, DT_CENTER);
  }
  BitBlt(target, 0, 0, W, H, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
}

void toast(const std::wstring& s) { g_toast = L10(s); g_toast_until = GetTickCount64() + 4000; SetTimer(g_window, 1, 4100, nullptr); InvalidateRect(g_window, nullptr, FALSE); }

void act(int a, int arg) {
  const auto d = launcher::yglobby::current();
  switch (a) {
    case A_NAV:
      if (arg <= 1) { if (g_page != arg) g_list_scroll.skip = 0; g_page = arg; if (g_compact && g_cb.open_page) { g_cb.open_page(-1); return; } break; }
      if (g_cb.open_page) g_cb.open_page(arg - 2); return;
    case A_PLAY_OFFLINE: if (launcher::yglobby::repeat_press()) return; if (g_cb.play_offline) g_cb.play_offline(); return;
    case A_QUEUE: if (launcher::yglobby::repeat_press()) return; launcher::yglobby::play(arg); toast(L"Starting the game..."); return;
    case A_CREATE: if (launcher::yglobby::repeat_press()) return; launcher::yglobby::create_room(true); return;
    case A_LEAVE_ROOM: launcher::room::leave(); toast(L"You left the room."); InvalidateRect(g_window, nullptr, FALSE); return;
    case A_JOIN_CODE: { wchar_t b[32] = {}; if (g_code) GetWindowTextW(g_code, b, 31); if (!b[0]) { toast(L"Type a room code first."); return; }
      const std::wstring code = launcher::yglobby::room_code(b);
      if (code.empty()) { toast(L"A room code is 6 to 12 letters or numbers."); return; }
      if (launcher::yglobby::repeat_press()) return;
      launcher::yglobby::join(code); toast(fill(L"Opening room {} in YouGame...", code)); return; }
    case A_ROOM: if (arg >= 0 && arg < (int)d.rooms.size() && !launcher::yglobby::repeat_press()) {
        // Watch joins too: in an Arena room everyone not fighting watches the match (the game's own Arena spectating).
        launcher::yglobby::join(d.rooms[arg].code);
        toast(d.rooms[arg].in_game ? L"Joining the room: in an Arena room you watch the match from there." : L"Opening it in YouGame..."); } return;
    case A_WATCH: if (arg >= 0 && arg < (int)d.live.size() && !launcher::yglobby::repeat_press()) { launcher::yglobby::watch(d.live[arg].code); toast(L"Opening the match in YouGame..."); } return;
    case A_RAIL_TAB: g_rail_tab = arg; g_rail_scroll.skip = 0; break;
    case A_ELO: g_elo = !g_elo; launcher::yglobby::set_elo(g_elo); launcher::yglobby::settings_changed(); break;
    case A_ME: launcher::yglobby::open_yougame_page(); return;
    case A_RAIL_ART: if (g_cb.rail_picture) g_cb.rail_picture(); return;
    case A_STATUS:
      if (g_cb.status_click) g_cb.status_click(arg);
      InvalidateRect(g_window, nullptr, FALSE);
      return;
    case A_BUILD_MENU: if (g_cb.build_menu) g_cb.build_menu(); break;
    case A_PLAYER_MENU: if (arg >= 0 && arg < (int)d.players.size()) {
        const auto& pl = d.players[arg]; const std::string user(pl.code.begin(), pl.code.end());
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 3, L10(L"Challenge in a YouGame room").c_str());
        if (pl.link == 1) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, fill(L"You and {} are friends", pl.name).c_str());
        else if (pl.link == 2) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L10(L"Friend request sent").c_str());
        else if (pl.link == 3) AppendMenuW(m, MF_STRING, 2, fill(L"Accept {}'s friend request", pl.name).c_str());
        else AppendMenuW(m, MF_STRING, 1, fill(L"Add {} as a friend", pl.name).c_str());
        POINT p{}; GetCursorPos(&p);
        const int pick = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, g_window, nullptr); DestroyMenu(m);
        if (pick == 3) {
          launcher::yglobby::create_room(false);
          launcher::room::invite();
        }
        if ((pick == 1 || pick == 2) && !user.empty())
          launcher::yglobby::friend_action(user, pick == 2, [](const std::wstring& say) { if (g_window) PostMessageW(g_window, WM_APP + 3, 0, (LPARAM)new std::wstring(say)); });
      } return;
    case A_ADD_TYPED: { wchar_t b[40] = {}; if (g_search) GetWindowTextW(g_search, b, 39); std::string user; for (wchar_t c : std::wstring(b)) if (c > 32 && c < 127) user += (char)std::tolower(c);
      if (!user.empty()) launcher::yglobby::friend_action(user, false, [](const std::wstring& say) { if (g_window) PostMessageW(g_window, WM_APP + 3, 0, (LPARAM)new std::wstring(say)); });
      return; }
    case A_SIGN_IN:   // waiting: the link again; otherwise a new code
      if (launcher::yglobby::code_sign_in(nullptr) == launcher::yglobby::CodeSignIn::kWaiting) launcher::yglobby::open_code_link();
      else launcher::yglobby::start_code_sign_in();
      break;
    case A_WATCH_PLAYER: toast(L"Watching in the game needs YouGame's match list and watch tickets. Not out yet."); return;   // no match to open: say so, no browser
    case A_MY_MAIN: { POINT p{}; GetCursorPos(&p); launcher::yglobby::pick_main(g_window, p); break; }
    case A_TOOL:
      if (arg == 0 && g_cb.settings) g_cb.settings();
      if (arg == 1 && g_cb.look_menu) g_cb.look_menu();
      if (arg == 2 && g_cb.send_logs) g_cb.send_logs();
      if (arg == 3 && g_cb.language) g_cb.language();
      return;
  }
  InvalidateRect(g_window, nullptr, FALSE);
}

LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_DROPFILES: return SendMessageW(GetParent(w), WM_DROPFILES, wp, lp);   // the launcher's own handler
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps); paint(dc); EndPaint(w, &ps); return 0; }
    case WM_PRINT: case WM_PRINTCLIENT: paint((HDC)wp);
      for (HWND e : {g_code, g_search}) if (m == WM_PRINT && e && IsWindowVisible(e)) {
        RECT r{}; GetWindowRect(e, &r); MapWindowPoints(nullptr, w, (POINT*)&r, 2); const int s = SaveDC((HDC)wp);
        POINT o{}; GetViewportOrgEx((HDC)wp, &o); SetViewportOrgEx((HDC)wp, o.x + r.left, o.y + r.top, nullptr);
        SendMessageW(e, WM_PRINT, wp, PRF_CLIENT | PRF_ERASEBKGND); RestoreDC((HDC)wp, s); }
      return 0;
    case WM_TIMER: KillTimer(w, wp); InvalidateRect(w, nullptr, FALSE); return 0;
    case WM_APP + 1: InvalidateRect(w, nullptr, FALSE); return 0;   // the lobby's worker learned something
    case WM_APP + 3: { std::unique_ptr<std::wstring> say((std::wstring*)lp); toast(*say); return 0; }   // a friend request's answer
    case WM_COMMAND: if ((HWND)lp == g_search && HIWORD(wp) == EN_CHANGE) { g_rail_scroll.skip = 0; InvalidateRect(w, nullptr, FALSE); } return 0;
    case WM_MOUSEWHEEL: {
      POINT p{(short)LOWORD(lp), (short)HIWORD(lp)}; ScreenToClient(w, &p);
      if (wheel(p, GET_WHEEL_DELTA_WPARAM(wp))) InvalidateRect(w, nullptr, FALSE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      // Scripted hidden test runs: only the script's moves (marked 0x4000) count; the hidden desktop's own pointer
      // sends moves of its own that would clear the hover the script set.
      if (static const bool script_run = std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT"); script_run && !(wp & 0x4000)) return 0;
      POINT p{(short)LOWORD(lp), (short)HIWORD(lp)}; int hot = -1;
      for (int i = 0; i < (int)g_hits.size(); ++i) if (PtInRect(&g_hits[i].r, p)) hot = i;
      if (hot != g_hot) { g_hot = hot; g_hot_act = hot >= 0 ? g_hits[hot].act : -1; g_hot_arg = hot >= 0 ? g_hits[hot].arg : 0; InvalidateRect(w, nullptr, FALSE); }
      // Scripted hidden test runs have no real pointer over the window (leave would come at once): hover stays.
      static const bool scripted = std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT");
      if (!g_tracking && !scripted) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, w, 0}; TrackMouseEvent(&t); g_tracking = true; }
      return 0;
    }
    case WM_MOUSELEAVE: g_tracking = false; if (g_hot >= 0) { g_hot = -1; g_hot_act = -1; InvalidateRect(w, nullptr, FALSE); } return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const HCURSOR h = launcher::melee_look::hand_now(g_hot >= 0, HIWORD(lp));
        if (launcher::pad::hand_shown()) { SetCursor(nullptr); return TRUE; }   // one hand: the controller's
        if (h) { SetCursor(h); return TRUE; }
        if (g_hot >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
      }
      break;
    case WM_LBUTTONDOWN: {
      POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
      for (int i = (int)g_hits.size() - 1; i >= 0; --i) if (PtInRect(&g_hits[i].r, p)) { act(g_hits[i].act, g_hits[i].arg); break; }
      return 0;
    }
    case WM_CTLCOLOREDIT: { HDC dc = (HDC)wp; SetTextColor(dc, P.text); SetBkColor(dc, P.page); static HBRUSH b1 = nullptr; static COLORREF c1 = 0;
      if (!b1 || c1 != P.page) { if (b1) DeleteObject(b1); b1 = CreateSolidBrush(P.page); c1 = P.page; }
      return (LRESULT)b1; }
    case WM_SIZE: InvalidateRect(w, nullptr, FALSE); return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}
}  // namespace

void init(HWND main_window, const Callbacks& callbacks) {
  g_main = main_window; g_cb = callbacks;
  if (!g_gdiplus) { Gdiplus::GdiplusStartupInput in; Gdiplus::GdiplusStartup(&g_gdiplus, &in, nullptr); }
  WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"MeleeUnlockedSkin"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  g_elo = launcher::yglobby::elo();
  if (const char* pg = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_SKIN_PAGE") : nullptr) g_page = std::string(pg) == "spectate" ? 1 : 0;   // test runs only
}
void show() {
  if (!g_main) return;
  g_compact = false;
  RECT client{}; GetClientRect(g_main, &client);
  if (!g_window) {
    g_window = CreateWindowExW(WS_EX_ACCEPTFILES, L"MeleeUnlockedSkin", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, client.right, client.bottom, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    fonts();
    g_code = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    for (HWND e : {g_code, g_search}) SendMessageW(e, WM_SETFONT, (WPARAM)g_f13, FALSE);
    SendMessageW(g_code, EM_SETCUEBANNER, TRUE, (LPARAM)L10(L"Room code").c_str()); SendMessageW(g_code, EM_LIMITTEXT, 16, 0);
    SetWindowSubclass(g_code, [](HWND e, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) -> LRESULT {
      // the launcher's dialog loop keeps Enter for itself unless the box asks for it
      if (m == WM_GETDLGCODE && lp && ((MSG*)lp)->message == WM_KEYDOWN && ((MSG*)lp)->wParam == VK_RETURN) return DLGC_WANTALLKEYS;
      if (m == WM_KEYDOWN && wp == VK_RETURN) { act(A_JOIN_CODE, 0); return 0; }
      if (m == WM_CHAR && (wp == L'\r' || wp == L'\n')) return 0;   // no beep
      return DefSubclassProc(e, m, wp, lp);
    }, 1, 0);
    SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L10(L"Search players").c_str()); SendMessageW(g_search, EM_LIMITTEXT, 30, 0);
  }
  // Nothing of the Classic look may draw over the skin: its controls go hidden, and any shown again later is
  // clipped under it (the skin stays on top of its siblings).
  for (HWND c = GetWindow(g_main, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    if (c == g_window) continue;
    SetWindowLongPtrW(c, GWL_STYLE, GetWindowLongPtrW(c, GWL_STYLE) | WS_CLIPSIBLINGS);
    if (IsWindowVisible(c)) { ShowWindow(c, SW_HIDE); g_hidden.push_back(c); }
  }
  SetWindowPos(g_window, HWND_TOP, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
  ShowWindow(g_window, SW_SHOW); InvalidateRect(g_window, nullptr, FALSE);
  launcher::yglobby::attach_view(g_window);
}
void hide() {
  if (g_window) ShowWindow(g_window, SW_HIDE);
  for (HWND c : g_hidden) if (IsWindow(c)) ShowWindow(c, SW_SHOWNA);
  g_hidden.clear();
}
bool shown() { return g_window && IsWindowVisible(g_window); }
bool is_compact() { return g_compact; }
namespace {
// Draws for another window in its pixels: the skin's units at that window's own DPI while it lasts.
struct PxScope { int saved; explicit PxScope(HWND w) : saved(g_fit_dpi) { g_fit_dpi = GetDpiForWindow(w ? w : g_main); fonts(); palette(); } ~PxScope() { g_fit_dpi = saved; } };
double U(int px) { return px * 96.0 / dpi(); }
}
Look look() { palette(); return {P.page_top, P.page, P.raised, P.line_strong, P.text, P.text2, P.dim, P.accent, kOk, kAmber}; }
void page_background(HDC dc, HWND w, RECT c) {
  PxScope k(w);
  TRIVERTEX v[2] = {{c.left, c.top, (COLOR16)(GetRValue(P.page_top) << 8), (COLOR16)(GetGValue(P.page_top) << 8), (COLOR16)(GetBValue(P.page_top) << 8), 0},
                    {c.right, std::min(c.bottom, (LONG)X(320)), (COLOR16)(GetRValue(P.page) << 8), (COLOR16)(GetGValue(P.page) << 8), (COLOR16)(GetBValue(P.page) << 8), 0}};
  GRADIENT_RECT g{0, 1}; GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
  if (c.bottom > X(320)) { RECT rest{c.left, X(320), c.right, c.bottom}; HBRUSH b = CreateSolidBrush(P.page); FillRect(dc, &rest, b); DeleteObject(b); }
}
void heading(HDC dc, HWND w, const std::wstring& s, int x, int y) { PxScope k(w); mt(dc, s, U(x), U(y), 40, RGB(255, 255, 255), 0, 12); }
void melee_text(HDC dc, HWND w, const std::wstring& s, int x, int y, double size, COLORREF c, int max_w) { PxScope k(w); mt(dc, s, U(x), U(y), size, c, max_w > 0 ? U(max_w) : 0); }
int melee_width(HDC dc, HWND w, const std::wstring& s, double size) { PxScope k(w); return mw(dc, s, size); }
void card(HDC dc, HWND w, RECT r) { PxScope k(w); fill_box(dc, U(r.left), U(r.top), U(r.right - r.left), U(r.bottom - r.top), 10, P.raised); }
void button(HDC dc, HWND w, RECT r, const std::wstring& label, bool accent, bool down, bool disabled) {
  // One kind of button: Melee's frame and font, the accent for the chosen or main action, the raised panel colour for
  // the others (as the sidebar's selected page), dim when it can't be pressed; the label keeps room on both sides.
  PxScope k(w);
  const double x = U(r.left), y = U(r.top), bw = U(r.right - r.left), bh = U(r.bottom - r.top);
  const COLORREF top = accent && !disabled ? (down ? P.accent2 : P.accent) : down ? P.page : P.raised;
  const COLORREF bot = accent && !disabled ? P.accent2 : down ? P.page : P.raised;
  const COLORREF rim = accent && !disabled ? P.rim : disabled ? P.soft_page : P.line_strong;
  if (!launcher::melee_look::button(dc, r, top, bot, rim, true)) { fill_box(dc, x, y, bw, bh, 6, top); if (!accent || disabled) line_box(dc, x, y, bw, bh, 6, rim); }
  const double pad = std::min(10.0, bw * 0.05), n = std::min(accent ? 36.0 : 20.0, bh * 0.68);   // small buttons: bigger label, less side room
  mt(dc, label, x + pad, y + (bh - n * 0.78) / 2, n, disabled ? P.dim : accent ? RGB(255, 255, 255) : P.text, bw - 2 * pad, 0, DT_CENTER);
}
void set_rail_art(HBITMAP bitmap, int w, int h) { g_art = bitmap; g_art_w = w; g_art_h = h; if (g_window) InvalidateRect(g_window, nullptr, FALSE); }
void fit() {   // the launcher window changed size: cover it again
  if (!shown()) return;
  RECT client{}; GetClientRect(g_main, &client);
  SetWindowPos(g_window, HWND_TOP, 0, 0, g_compact ? MulDiv(190, GetDpiForWindow(g_main), 96) : client.right, client.bottom, SWP_NOACTIVATE);
}
void compact(int nav) {
  if (!g_main) return;
  if (!g_window) show();
  const int rail = MulDiv(190, GetDpiForWindow(g_main), 96);
  // The launcher's own rail controls go back (under this sidebar); the page's own are the launcher's to show.
  for (HWND c : g_hidden) if (IsWindow(c)) { RECT r{}; GetWindowRect(c, &r); MapWindowPoints(nullptr, g_main, (POINT*)&r, 2); if (r.left < rail) ShowWindow(c, SW_SHOWNA); }
  g_hidden.clear();
  for (HWND e : {g_code, g_search}) if (e) ShowWindow(e, SW_HIDE);
  g_compact = true; g_compact_nav = nav;
  RECT client{}; GetClientRect(g_main, &client);
  SetWindowPos(g_window, HWND_TOP, 0, 0, rail, client.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW); InvalidateRect(g_window, nullptr, FALSE);
}
void set_color(COLORREF theme) { g_theme = theme; if (g_window) InvalidateRect(g_window, nullptr, FALSE); }

}  // namespace launcher::skin
