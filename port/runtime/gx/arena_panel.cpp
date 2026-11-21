// The arena room in the side bars (see arena_panel.h).
//
// Drawn to the room sheet Chandler picked (the combined look and its four options), measured in the
// sheet's pixels: a bar there is 152 pixels wide, and everything scales with the real bar. The
// sheet's lettering is Arial Black (corner titles, records, tags), a heavy sans for names and
// Bahnschrift for the small print; each font's size here is the sheet's CSS size times the font's
// line height in ems, because ImGui sizes a font by that height and CSS by the em.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "arena_panel.h"
#include "arena_hand.h"
#include "cosmetic_mods.h"
#include "host.h"
#include "render_options.h"
#include "test_env.h"
#include "slippi_arena.h"
#include "slippi_online.h"
#include "native_practice.h"
#include "yougame_link.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "user_texture.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace arena_panel {
namespace {

using slippi::arena::Act;
using slippi::arena::Member;
using slippi::arena::Role;
using slippi::arena::Room;
using slippi::arena::View;
namespace ar = slippi::arena;

constexpr uint16_t kDLeft = 0x0001, kDRight = 0x0002, kDDown = 0x0004, kDUp = 0x0008, kZ = 0x0010, kR = 0x0020,
                   kA = 0x0100, kB = 0x0200, kX = 0x0400, kY = 0x0800, kStart = 0x1000;

// The sheet's colours.
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255), kBlack = IM_COL32(0, 0, 0, 255),
                kLineCol = IM_COL32(154, 166, 192, 255),      // --line #9aa6c0
                kGold = IM_COL32(243, 194, 46, 255),          // --gold #f3c22e
                kGold2 = IM_COL32(201, 138, 12, 255),         // --gold2 #c98a0c
                kRed = IM_COL32(216, 52, 60, 255),            // --red #d8343c
                kBlue = IM_COL32(58, 86, 216, 255),           // --blue #3a56d8
                kSlotBg = IM_COL32(26, 33, 51, 255),          // .slot #1a2133
                kSlotLine = IM_COL32(74, 86, 116, 255),       // #4a5674
                kIconLine = IM_COL32(223, 229, 242, 255),     // #dfe5f2
                kHintText = IM_COL32(207, 214, 230, 255),     // #cfd6e6
                kDimText = IM_COL32(154, 166, 192, 255),      // .dim #9aa6c0
                kInk = IM_COL32(35, 23, 0, 255),              // #231700
                kPaper = IM_COL32(232, 236, 245, 255),        // #e8ecf5
                kRecordCol = IM_COL32(255, 210, 74, 255),     // .rec #ffd24a
                kFoot = IM_COL32(18, 24, 38, 255);            // .foot #121826

enum class Page { None, Rules, Code };

struct State {
  bool loaded = false;
  Look look = Look::Combined;
  bool tabs = false;            // folded into tabs at the edges
  int button = 0;               // the Arena card out of a room: 0 briefly, 1 always, 2 never
  double first_shown = -1;      // this session's first Direct (under YouGame: YouGame Friends) character select
  float card_pop = 0;           // the Arena card out of a room: how far it has slid in (0..1)
  bool focus = false;           // the room menu has the controller
  Page page = Page::None;
  int row = 0;                  // the rules page's row, or the prompt's choice
  int key_row = 0, key_col = 0; // the on-screen keyboard
  std::string code;             // typed so far
  std::vector<std::string> recent;
  int recent_pick = -1;         // a recent code highlighted (-1: the keyboard)
  uint16_t prev = 0;
  int prev_dir = 0;
  double dir_at = 0;
  double shown_at = -1;         // when the bars began to slide in
  bool was_visible = false;
  // The hand: pushed against the left (1) or right (2) edge of the character select it goes into
  // that side's panel; the button under it lights up and A presses it.
  int side = 0;
  int push_side = 0, push_frames = 0, push_needed = 10;   // push_needed: kPushFrames, kPushFramesOverCard or kRestFrames
  float rest_y = 0;                         // out of a room: where the resting hand's count started (it must hold still)
  uint8_t hover = 0, hover_member = 0xFF;   // a Do and, for a player card, whose
  int stay_level = 2;                       // in a panel: how level the glove must stay (1 when it went in over the card from above)
  uint16_t swallow = 0;                     // panel buttons still held from the panel: kept from the game until let go
  float pop[2] = {0, 0};                    // Hidden: how far each panel has slid in (0..1)
  double pop_seen[2] = {-10, -10};          // when the hand was last near that side
  double last_time = 0;
  bool leave_armed = false;                 // Rules > Leave the room was pressed once
} g;

// What a lit-up button does.
enum class Do : uint8_t { None, Rules, AddCode, Spot, Ask, Doubles, Opt0, Opt1, Opt2, Member, Leave, Warmup, Pip };
// The room's match window's size button (watching): its label says what A on it does.
constexpr const char* kPipSmall = "Picture in picture";
constexpr const char* kPipBig = "Full size";
bool warmup_row_shown();   // the warm-up room's row at the foot of a room's left bar
struct Item { int side; float y0, y1; Do what; uint8_t member; };
std::vector<Item> g_items, g_last_items;   // this frame's buttons, and last frame's (to move the hand over)
float g_mid = 0;                            // the window's middle: left panel or right panel
// A button at x between y0 and y1: true while the hand is on it.
bool hot(float x, float y0, float y1, Do what, uint8_t member = 0xFF) {
  const int side = x < g_mid ? 1 : 2;
  g_items.push_back({side, y0, y1, what, member});
  return g.side == side && g.hover == (uint8_t)what && (what != Do::Member || g.hover_member == member);
}
Do do_of(char chip, const std::string& label) {
  switch (chip) {
    case 'Y': return Do::Rules;
    case 'Z': return Do::AddCode;
    case 'X': return Do::Spot;
    case 'R': return Do::Doubles;
    case 'B': return Do::Leave;
    case 'A': return label.rfind("Ask", 0) == 0 ? Do::Ask : label == kPipSmall || label == kPipBig ? Do::Pip : Do::None;
    case 'L': return Do::Warmup;   // "+ R  Warm up": L and R held, or A on the row
    default: return Do::None;
  }
}

Fonts g_fonts;

const char* const kLookNames[kLookCount] = {"Corners + cards", "Card", "Split", "Corners", "Ring"};
const char* const kModeNames[3] = {"WINNER STAYS", "TAKE TURNS", "ASK"};
const char* const kModeWords[3] = {"Winner stays", "Take turns", "Ask"};
const char* const kKeys[5] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ#", "0123456789", "<>"};   // last row: delete, done

std::filesystem::path settings_file() {
  return std::filesystem::u8path(slippi::online::config().user_dir) / "arena" / "panel.txt";
}
void load() {
  if (g.loaded) return;
  g.loaded = true;
  std::ifstream f(settings_file());
  int look = 0, tabs = 0, button = 0;
  if (f >> look >> tabs) {
    g.look = (Look)std::clamp(look, 0, kLookCount - 1);
    g.tabs = tabs != 0;
    if (f >> button) g.button = std::clamp(button, 0, kButtonModeCount - 1);
  }
}
void save() {
  std::error_code ec;
  std::filesystem::create_directories(settings_file().parent_path(), ec);
  std::ofstream f(settings_file());
  f << (int)g.look << " " << (g.tabs ? 1 : 0) << " " << g.button << "\n";
}
void load_recent() {
  g.recent.clear();
  std::ifstream f(std::filesystem::u8path(slippi::online::config().user_dir) / "direct-codes.json");
  const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
  if (!j.is_array()) return;
  for (const auto& e : j)
    if (e.is_object() && e.find("connectCode") != e.end() && e["connectCode"].is_string() && g.recent.size() < 6) {
      const std::string c = e["connectCode"];
      if (c.find('#') != std::string::npos) g.recent.push_back(c);
    }
}

// ---------------------------------------------------------------- drawing basics
// Over the window, after the game picture; or the list of the Arena card under the hands (g_under).
ImDrawList* g_target = nullptr;
ImDrawList* dl() { return g_target ? g_target : ImGui::GetForegroundDrawList(); }
// The Arena card out of a room, drawn into the game picture under the coins and hands when the frame
// has the native select's mark (under_hand_draw_data); built in the ImGui frame `frame`.
struct Under {
  bool wanted = false;           // the renderer draws the list at the mark this frame
  ImDrawList* list = nullptr;
  ImGuiContext* context = nullptr;   // the list's ImGui context (the panel makes a new one when HDR switches)
  int frame = -1;
  ImVec2 pos, size;              // the game picture in the window
  float efb_width = 0.0f;        // the picture's width in internal pixels (set_under_hand), 0 unknown
} g_under;
// Starts this frame's list over the game picture and makes it the target of the drawing.
void begin_under(float x0, float y0, float x1, float y1) {
  if (g_under.context != ImGui::GetCurrentContext()) {
    if (g_under.list) IM_DELETE(g_under.list);
    g_under.list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    g_under.context = ImGui::GetCurrentContext();
  }
  ImDrawList* d = g_under.list;
  d->_ResetForNewFrame();
  d->PushTexture(ImGui::GetIO().Fonts->TexRef);
  // Built against the whole window, as the foreground list (text is cut to the clip rectangle as it
  // is laid out); end_under then clips the commands to the picture and spill_under draws the rest.
  d->PushClipRectFullScreen();
  g_under.pos = ImVec2(x0, y0);
  g_under.size = ImVec2(x1 - x0, y1 - y0);
  g_under.frame = ImGui::GetFrameCount();
  g_target = d;
}
void end_under() {
  if (!g_target || g_target != g_under.list) return;
  g_target = nullptr;
  g_under.list->_PopUnusedDrawCmd();
  const ImVec4 picture(g_under.pos.x, g_under.pos.y, g_under.pos.x + g_under.size.x, g_under.pos.y + g_under.size.y);
  for (ImDrawCmd& cmd : g_under.list->CmdBuffer) cmd.ClipRect = picture;
}
bool under_this_frame() {
  return g_under.list && g_under.context == ImGui::GetCurrentContext() && g_under.frame == ImGui::GetFrameCount() &&
         g_under.list->VtxBuffer.Size > 0;
}
// The part of the card past the game picture's sides (a 4:3 picture's bar is narrower than the
// card, which then reaches over the picture's edge): over the window as before, cut at the
// picture's edges. The hands never reach past the picture.
void spill_under(float display_w, float display_h) {
  if (!under_this_frame()) return;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  const ImDrawList* u = g_under.list;
  const float px0 = g_under.pos.x, px1 = g_under.pos.x + g_under.size.x;
  const ImVec4 sides[2] = {ImVec4(0, 0, px0, display_h), ImVec4(px1, 0, display_w, display_h)};
  for (const ImVec4& side : sides) {
    if (side.z - side.x < 0.5f) continue;
    for (const ImDrawCmd& cmd : u->CmdBuffer) {
      if (cmd.UserCallback || cmd.ElemCount == 0) continue;
      unsigned lo = ~0u, hi = 0;
      for (unsigned i = 0; i < cmd.ElemCount; ++i) {
        const unsigned v = cmd.VtxOffset + u->IdxBuffer[(int)(cmd.IdxOffset + i)];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      const int count = (int)(hi - lo + 1);
      fg->PushTexture(cmd.TexRef);
      // The list's own clip is the picture (begin_under), which is what this part lies outside of.
      fg->PushClipRect(ImVec2(side.x, side.y), ImVec2(side.z, side.w), true);
      fg->PrimReserve((int)cmd.ElemCount, count);
      const unsigned base = fg->_VtxCurrentIdx;
      for (int j = 0; j < count; ++j) fg->_VtxWritePtr[j] = u->VtxBuffer[(int)lo + j];
      fg->_VtxWritePtr += count;
      for (unsigned i = 0; i < cmd.ElemCount; ++i)
        *fg->_IdxWritePtr++ = (ImDrawIdx)(base + cmd.VtxOffset + u->IdxBuffer[(int)(cmd.IdxOffset + i)] - lo);
      fg->_VtxCurrentIdx += (unsigned)count;
      fg->PopClipRect();
      fg->PopTexture();
    }
  }
}
float g_k = 1.0f;   // sheet pixels to window pixels: 1 at a 152-pixel bar
float px(float v) { return v * g_k; }

enum class Face { Title, Black, Bold, Body };
ImFont* font_of(Face f) {
  ImFont* p = f == Face::Title ? g_fonts.title : f == Face::Black ? g_fonts.black : f == Face::Bold ? g_fonts.bold : g_fonts.body;
  return p ? p : ImGui::GetFont();
}
// A font's line height in ems (hhea ascent + descent): ImGui's size is that height.
float em_ratio(Face f) {
  ImFont* p = font_of(f);
  if (p == g_fonts.title && g_fonts.title) return 1.4102f;   // Arial Black
  if (p == g_fonts.black && g_fonts.black) return 1.3506f;   // Segoe UI Black
  if (p == g_fonts.bold && g_fonts.bold) return 1.3301f;     // Segoe UI Bold
  if (p == g_fonts.body && g_fonts.body) return 1.2002f;     // Bahnschrift
  return 1.15f;
}
float size_of(Face f, float css) { return px(css) * em_ratio(f); }

struct Ink {
  Face face = Face::Body;
  float css = 12;             // the sheet's font-size
  ImU32 col = kWhite;
  bool italic = false;        // the sheet's Arial Black italic
  float shadow = 0;           // text-shadow 0 Npx 0 #000
  bool glow = false;          // and 0 0 3px #000
  float spacing = 0;          // letter-spacing, sheet pixels
};

float text_w(const Ink& k, const std::string& s) {
  const float w = font_of(k.face)->CalcTextSizeA(size_of(k.face, k.css), 1e6f, 0.f, s.c_str()).x;
  return w + (s.empty() ? 0 : px(k.spacing) * (float)(s.size() - 1));
}
// The glyph box's top for text set in a line box of `line` sheet pixels starting at y.
float line_top(const Ink& k, float y, float line) { return y + (px(line) - size_of(k.face, k.css)) * .5f; }

void raw_text(const Ink& k, float x, float y, ImU32 col, const std::string& s) {
  ImFont* f = font_of(k.face);
  const float size = size_of(k.face, k.css);
  if (k.spacing == 0) { dl()->AddText(f, size, ImVec2(x, y), col, s.c_str()); return; }
  float pen = x;
  for (char c : s) {
    const char one[2] = {c, 0};
    dl()->AddText(f, size, ImVec2(pen, y), col, one);
    pen += f->CalcTextSizeA(size, 1e6f, 0.f, one).x + px(k.spacing);
  }
}
// Tests (MELEE_TEST_ARENA_TEXTCHECK=1): every string's box, checked against its panel each frame.
struct TextBox { std::string s; float x0, y0, x1, y1; };
bool textcheck_on() {
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_TEXTCHECK"); return v && *v == '1'; }();
  return on;
}
std::vector<TextBox> g_text_boxes;
// Text with the sheet's shadow, slanted when italic (the vertices are sheared about the line).
void text(const Ink& k, float x, float y, const std::string& s) {
  if (s.empty()) return;
  if (textcheck_on()) {
    const float h = size_of(k.face, k.css);
    // the glyphs' own height sits inside the line box ImGui reports; italic leans up to 0.16 h right
    g_text_boxes.push_back({s, x, y + h * 0.12f, x + text_w(k, s) + (k.italic ? h * 0.16f : 0.f), y + h * 0.88f});
  }
  ImDrawList* d = dl();
  const int start = d->VtxBuffer.Size;
  const float a = (float)((k.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
  const ImU32 shade = IM_COL32(0, 0, 0, (int)(230 * a));
  if (k.glow)
    for (int i = 0; i < 4; ++i) raw_text(k, x + px(i < 2 ? (i ? 1.f : -1.f) : 0), y + px(i >= 2 ? (i == 2 ? 1.f : -1.f) : 0), IM_COL32(0, 0, 0, (int)(110 * a)), s);
  if (k.shadow > 0) raw_text(k, x, y + px(k.shadow), shade, s);
  raw_text(k, x, y, k.col, s);
  if (k.italic) {
    const float base = y + size_of(k.face, k.css) * .78f;
    for (int i = start; i < d->VtxBuffer.Size; ++i) d->VtxBuffer[i].pos.x += (base - d->VtxBuffer[i].pos.y) * 0.21f;
  }
}
void text_center(const Ink& k, float cx, float y, const std::string& s) { text(k, cx - text_w(k, s) * .5f, y, s); }
std::string fit(const Ink& k, const std::string& s, float w) {
  if (text_w(k, s) <= w) return s;
  std::string t = s;
  while (!t.empty() && text_w(k, t + ".") > w) t.pop_back();
  return t + ".";
}
// A name: set smaller (down to 70%) before it is shortened.
std::string fit_name(Ink& k, const std::string& s, float w) {
  const float full = k.css;
  while (text_w(k, s) > w && k.css > full * 0.7f) k.css -= full * 0.05f;
  return fit(k, s, w);
}

// Colours of the vertices drawn since `start`, from `top` at y0 to `bottom` at y1 (the sheet's
// vertical linear-gradients), alpha included.
ImU32 mix(ImU32 a, ImU32 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto ch = [&](int s) { return (int)(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t + .5f); };
  return (ImU32)(ch(0) | ch(8) << 8 | ch(16) << 16 | ch(24) << 24);
}
void shade(int start, float y0, float y1, ImU32 top, ImU32 bottom) {
  ImDrawList* d = dl();
  for (int i = start; i < d->VtxBuffer.Size; ++i) {
    const float t = y1 > y0 ? (d->VtxBuffer[i].pos.y - y0) / (y1 - y0) : 0.f;
    d->VtxBuffer[i].col = mix(top, bottom, t);
  }
}
void shade_x(int start, float x0, float x1, ImU32 left, ImU32 right) {
  ImDrawList* d = dl();
  for (int i = start; i < d->VtxBuffer.Size; ++i) {
    const float t = x1 > x0 ? (d->VtxBuffer[i].pos.x - x0) / (x1 - x0) : 0.f;
    d->VtxBuffer[i].col = mix(left, right, t);
  }
}
void rect_gradient(float x0, float y0, float x1, float y1, float r, ImU32 top, ImU32 bottom) {
  const int start = dl()->VtxBuffer.Size;
  dl()->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kWhite, r);
  shade(start, y0, y1, top, bottom);
}
// box-shadow 0 0 Npx colour: a few widening outlines fading out.
void glow(float x0, float y0, float x1, float y1, float r, ImU32 col, float spread) {
  const int steps = 5;
  for (int i = steps; i >= 1; --i) {
    const float e = spread * i / steps;
    const float a = (float)((col >> IM_COL32_A_SHIFT) & 0xFF) * (1.0f - (float)(i - 1) / steps) * 0.35f;
    dl()->AddRect(ImVec2(x0 - e, y0 - e), ImVec2(x1 + e, y1 + e), (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT), r + e, 0, spread / steps + px(.5f));
  }
}
void drop_shadow(float x0, float y0, float x1, float y1, float r) {   // 0 2px 4px rgba(0,0,0,.6)
  for (int i = 3; i >= 1; --i) {
    const float e = px(1.3f * i);
    dl()->AddRectFilled(ImVec2(x0 - e * .5f, y0 + px(2) - e * .5f), ImVec2(x1 + e * .5f, y1 + px(2) + e), IM_COL32(0, 0, 0, 40), r + e);
  }
}

// ---------------------------------------------------------------- portraits
// Each player's picture: the game's own character select portrait of the character and costume they
// picked (the disc's MnSlChr.usd), as one texture each, shown square from its top like the sheet.
struct Portrait { ImTextureData* tex = nullptr; bool tried = false; float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                  ImGuiContext* context = nullptr; };   // the overlay it is registered with (user_texture.h)
std::map<int, Portrait> g_portraits;

ImTextureData* portrait(uint8_t character, uint8_t costume) {
  if (character == ar::kNone || character >= 26) return nullptr;
  if (character == 19) return nullptr;   // Sheik: the disc keeps her in Zelda's cell; her name tile instead
  Portrait& p = g_portraits[character * 16 + (costume & 15)];
  if (p.tex) gx::keep_user_texture(p.tex, p.context);
  if (!p.tried) {
    p.tried = true;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (host::cosmetics::standard_portrait(character, costume, &rgba, &w, &h) && w > 0 && h > 0) {
      p.tex = IM_NEW(ImTextureData)();
      p.tex->Create(ImTextureFormat_RGBA32, w, h);
      std::memcpy(p.tex->GetPixels(), rgba.data(), rgba.size());
      p.tex->UsedRect = ImTextureRect{0, 0, (unsigned short)w, (unsigned short)h};
      gx::keep_user_texture(p.tex, p.context);
      // The face: a square from the top of the drawn figure, centred on it (the sheet crops each
      // portrait to the head the way the character select's own cards frame it).
      int top = -1;
      for (int y = 0; y < h && top < 0; ++y)
        for (int x = 0; x < w; ++x) if (rgba[((size_t)y * w + x) * 4 + 3] > 128) { top = y; break; }
      if (top < 0) top = 0;
      const int side = std::min({(int)(w * 0.64f), w, h});
      double sum = 0, count = 0;
      for (int y = top; y < std::min(h, top + side); ++y)
        for (int x = 0; x < w; ++x) if (rgba[((size_t)y * w + x) * 4 + 3] > 128) { sum += x; count += 1; }
      const int cx = count > 0 ? (int)(sum / count) : w / 2;
      const int x0 = std::clamp(cx - side / 2, 0, w - side);
      const int y0 = std::clamp(top - (int)(h * 0.02f), 0, h - side);
      p.u0 = (float)x0 / w; p.u1 = (float)(x0 + side) / w;
      p.v0 = (float)y0 / h; p.v1 = (float)(y0 + side) / h;
    }
  }
  return p.tex;
}
Portrait* portrait_of(uint8_t character, uint8_t costume) {
  if (!portrait(character, costume)) return nullptr;
  return &g_portraits[character * 16 + (costume & 15)];
}

// The sheet's .ic: a white 2px frame with a black hairline and a drop shadow, the face inside on
// the ground the sheet's crops had behind them (the red or blue card in a corner, the roster's
// slate tile in a card).
enum class Ground { Slate, Red, Blue };
void icon(float x, float y, float s, const Member* m, bool gold_frame = false, Ground ground = Ground::Slate) {
  const float r = px(5);
  drop_shadow(x, y, x + s, y + s, r);
  dl()->AddRectFilled(ImVec2(x - px(1), y - px(1)), ImVec2(x + s + px(1), y + s + px(1)), kBlack, r + px(1));
  if (ground == Ground::Red) rect_gradient(x, y, x + s, y + s, r, IM_COL32(222, 66, 66, 255), IM_COL32(120, 22, 28, 255));
  else if (ground == Ground::Blue) rect_gradient(x, y, x + s, y + s, r, IM_COL32(70, 100, 222, 255), IM_COL32(26, 40, 120, 255));
  else rect_gradient(x, y, x + s, y + s, r, IM_COL32(108, 126, 168, 255), IM_COL32(40, 50, 80, 255));
  Portrait* pt = m ? portrait_of(m->character, m->costume) : nullptr;
  ImTextureData* t = pt ? pt->tex : nullptr;
  if (t) {
    const float in = px(2);
    dl()->AddImageRounded(t->GetTexRef(), ImVec2(x + in, y + in), ImVec2(x + s - in, y + s - in), ImVec2(pt->u0, pt->v0),
                          ImVec2(pt->u1, pt->v1), kWhite, r - px(1));
  } else if (m && m->character < 26) {
    // A fighter with no portrait here (Sheik): its name, set to fit the tile.
    static const char* const kShort[26] = {"FALCON", "DK", "FOX", "G&W", "KIRBY", "BOWSER", "LINK", "LUIGI", "MARIO",
                                           "MARTH", "MEWTWO", "NESS", "PEACH", "PIKA", "ICIES", "PUFF", "SAMUS",
                                           "YOSHI", "ZELDA", "SHEIK", "FALCO", "Y.LINK", "DOC", "ROY", "PICHU", "GANON"};
    Ink k{Face::Title, s * .26f / g_k, IM_COL32(220, 226, 240, 255), true, 1};
    const std::string name = kShort[m->character];
    while (k.css > 4 && text_w(k, name) > s - px(6)) k.css *= 0.92f;
    text_center(k, x + s * .5f, y + (s - size_of(k.face, k.css)) * .5f, name);
  } else if (m) {
    Ink k{Face::Title, s * .42f / g_k, IM_COL32(200, 208, 226, 255), true, 2};
    const std::string letter = m->name.empty() ? "?" : std::string(1, (char)std::toupper((unsigned char)m->name[0]));
    text_center(k, x + s * .5f, y + (s - size_of(k.face, k.css)) * .5f, letter);
  }
  dl()->AddRect(ImVec2(x, y), ImVec2(x + s, y + s), gold_frame ? kGold : kIconLine, r, 0, px(2));
}

// The sheet's .tag: Arial Black italic 11px on a 17px slanted strip.
float tag(float x, float y, const char* label, ImU32 top, ImU32 bottom, ImU32 ink) {
  const Ink k{Face::Title, 11, ink, true, 0, false, .4f};
  const float h = px(17), w = text_w(k, label) + px(7 + 14);
  const int start = dl()->VtxBuffer.Size;
  const ImVec2 pts[4] = {ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w - px(9), y + h), ImVec2(x, y + h)};
  dl()->AddConvexPolyFilled(pts, 4, kWhite);
  shade(start, y, y + h, top, bottom);
  text(k, x + px(7), line_top(k, y, 17), label);
  return w;
}
void tag_gold(float x, float y, const char* l) { tag(x, y, l, IM_COL32(255, 228, 128, 255), kGold2, kInk); }
void tag_grey(float x, float y, const char* l) { tag(x, y, l, IM_COL32(201, 208, 222, 255), IM_COL32(127, 138, 163, 255), kInk); }
void tag_red(float x, float y, const char* l) { tag(x, y, l, IM_COL32(255, 128, 136, 255), IM_COL32(184, 31, 40, 255), kWhite); }

// The sheet's .gc button chips: X and Y grey kidney, A green disc, Z purple square, B red disc.
// The L and R triggers: a mirrored pair of shoulder buttons, the outer top corner round and the rest
// eased, a light band along the top and a dark edge under it (as the warm-up panel draws them);
// 21 wide, 16 high (the other chips are 16 square). Returns the width.
void corners(float x0, float y0, float x1, float y1, float tl, float tr, float br, float bl) {
  dl()->PathArcTo(ImVec2(x0 + tl, y0 + tl), tl, IM_PI, IM_PI * 1.5f, 8);
  dl()->PathArcTo(ImVec2(x1 - tr, y0 + tr), tr, IM_PI * 1.5f, IM_PI * 2.0f, 8);
  dl()->PathArcTo(ImVec2(x1 - br, y1 - br), br, 0.0f, IM_PI * 0.5f, 8);
  dl()->PathArcTo(ImVec2(x0 + bl, y1 - bl), bl, IM_PI * 0.5f, IM_PI, 8);
}
void glyph_center(const Ink& k, float cx, float cy, char c) {
  ImFont* f = font_of(k.face);
  const float size = size_of(k.face, k.css);
  ImFontBaked* baked = f ? f->GetFontBaked(size) : nullptr;
  const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback((ImWchar)(unsigned char)c) : nullptr;
  const char one[2] = {c, 0};
  if (!g) { text_center(k, cx, cy - size * .5f, one); return; }
  dl()->AddText(f, size, ImVec2(std::round(cx - (g->X0 + g->X1) * .5f), std::round(cy - (g->Y0 + g->Y1) * .5f)), k.col, one);
}
float trigger_chip(float x, float y, char b, float alpha = 1.0f) {
  auto fade = [&](ImU32 col) { return (col & ~IM_COL32_A_MASK) | ((ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha) << IM_COL32_A_SHIFT); };
  const float w = px(21), h = px(16), big = h * .6f, ease = px(2.8f), band = h * .42f, in = px(1.5f);
  const float tl = b == 'L' ? big : ease, tr = b == 'L' ? ease : big;
  corners(x, y + px(1.5f), x + w, y + h + px(1.5f), tl, tr, ease, ease);
  dl()->PathFillConvex(fade(IM_COL32(40, 42, 52, 255)));
  corners(x, y, x + w, y + h, tl, tr, ease, ease);
  dl()->PathFillConvex(fade(IM_COL32(178, 182, 196, 255)));
  corners(x + in, y + in, x + w - in, y + band, std::min(std::max(0.0f, tl - in), band - in), std::min(std::max(0.0f, tr - in), band - in), 0, 0);
  dl()->PathFillConvex(fade(IM_COL32(224, 228, 238, 255)));
  const Ink k{Face::Bold, 10, fade(IM_COL32(28, 30, 38, 255))};
  glyph_center(k, x + w * .5f, y + h * .5f, b);
  return w;
}
float lr_chips_width() {
  const Ink pk{Face::Bold, 11, kHintText};
  return px(21) * 2 + px(6) + text_w(pk, "+");
}
// L + R together (the warm-up's hold). Returns the width.
float lr_chips(float x, float y, float alpha = 1.0f) {
  float at = x + trigger_chip(x, y, 'L', alpha) + px(3);
  const Ink pk{Face::Bold, 11, (kHintText & ~IM_COL32_A_MASK) | ((ImU32)(255 * alpha) << IM_COL32_A_SHIFT)};
  text(pk, at, line_top(pk, y, 16), "+");
  at += text_w(pk, "+") + px(3);
  at += trigger_chip(at, y, 'R', alpha);
  return at - x;
}

void button_chip(float x, float y, char b, float alpha = 1.0f) {
  if (b == 'L' || b == 'R') { trigger_chip(x, y, b, alpha); return; }
  const float s = px(16);
  ImU32 c = IM_COL32(174, 180, 191, 255), ink = IM_COL32(21, 24, 31, 255);
  float r = px(8);
  if (b == 'A') { c = IM_COL32(23, 178, 107, 255); ink = kWhite; }
  if (b == 'B') { c = IM_COL32(220, 60, 60, 255); ink = kWhite; }
  if (b == 'Z') { c = IM_COL32(123, 82, 214, 255); ink = kWhite; r = px(4); }
  if (b == 'S') { c = IM_COL32(110, 118, 134, 255); ink = kWhite; r = px(4); }
  auto fade = [&](ImU32 col) { return (col & ~IM_COL32_A_MASK) | ((ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha) << IM_COL32_A_SHIFT); };
  dl()->AddRectFilled(ImVec2(x, y + px(1)), ImVec2(x + s, y + s + px(1)), fade(kBlack), r);
  dl()->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s), fade(c), r);
  const Ink k{Face::Bold, 10, fade(ink)};
  glyph_center(k, x + s * .5f, y + s * .5f, b);
}
// One row of the sheet's .hints: chip, 6px, 11.5px text.
void hint(float x, float y, char b, const std::string& label, float w, float alpha = 1.0f, bool live = true) {
  const Do what = live ? do_of(b, label) : Do::None;
  const bool lit = what != Do::None && hot(x, y - px(3), y + px(19), what);
  if (lit) {
    // The sheet's selected option: a gold bar, dark lettering.
    const int start = dl()->VtxBuffer.Size;
    dl()->AddRectFilled(ImVec2(x - px(4), y - px(3)), ImVec2(x + w + px(2), y + px(19)), kWhite, px(4));
    shade_x(start, x - px(4), x + w + px(2), kGold, IM_COL32(233, 168, 26, 255));
    dl()->AddRect(ImVec2(x - px(4), y - px(3)), ImVec2(x + w + px(2), y + px(19)), IM_COL32(255, 243, 194, 255), px(4), 0, px(1));
  }
  // 'L' stands for L + R held together (the warm-up): both triggers, then the label.
  float label_x = px(22);
  if (b == 'L') label_x = lr_chips(x, y, alpha) + px(6);
  else button_chip(x, y, b, alpha);
  const ImU32 col = lit ? kInk : (kHintText & ~IM_COL32_A_MASK) | ((ImU32)(255 * alpha) << IM_COL32_A_SHIFT);
  Ink k{lit ? Face::Bold : Face::Body, 11.5f, col};
  const std::string shown = fit_name(k, label, w - label_x);   // smaller before it is shortened ("Add a friend's co.")
  text(k, x + label_x, line_top(k, y, 16), shown);
}
// Hint rows stacked up from `bottom` (gap 5px), returning the top of the first.
float hints_up(float x, float w, float bottom, const std::vector<std::pair<char, std::string>>& items, float alpha = 1.0f) {
  float y = bottom - items.size() * px(16) - (items.empty() ? 0 : (items.size() - 1) * px(5));
  const float top = y;
  for (const auto& it : items) { hint(x, y, it.first, it.second, w, alpha); y += px(21); }
  return top;
}

std::string record(const Member& m) { return std::to_string(m.wins) + "-" + std::to_string(m.losses); }
std::string ping_text(const View& v, uint8_t id) {
  if (id == ar::owner(v.room)) return "";
  const int ms = id < ar::kMaxMembers ? v.ping_ms[id] : -1;
  return ms < 0 ? "" : std::to_string(ms) + " ms";
}

// The sheet's .slot: 5px 6px padding, a 34px portrait, the name; "you" glows gold with a YOU chip.
// The ping sits at the right in small print (the room's player list keeps everyone's ping).
constexpr float kSlotH = 46;
// The room's leader (the member who made it, ar::owner): a small gold-outlined HOST badge on the bottom edge
// of their portrait, centred on `cx`, so it never takes room from a name. Nothing for anyone else.
void host_badge(float cx, float edge_y, const View& v, const Member* m) {
  if (!m || m->id != ar::owner(v.room)) return;
  const Ink hk{Face::Bold, 8, kGold};
  const float tw = text_w(hk, "HOST") + px(6), th = px(12);
  const float y0 = edge_y - th * .6f;
  dl()->AddRectFilled(ImVec2(cx - tw * .5f, y0), ImVec2(cx + tw * .5f, y0 + th), IM_COL32(12, 16, 28, 235), px(3));
  dl()->AddRect(ImVec2(cx - tw * .5f, y0), ImVec2(cx + tw * .5f, y0 + th), kGold, px(3), 0, px(1));
  text_center(hk, cx, line_top(hk, y0, 12), "HOST");
}

void slot_card(float x, float y, float w, const View& v, const Member& m, bool you, bool asking, float icon_css = 34) {
  const float h = px(icon_css + 12), r = px(5);
  const bool lit = hot(x, y, y + h, Do::Member, m.id);
  if (you) glow(x, y, x + w, y + h, r, IM_COL32(243, 194, 46, 128), px(8));
  dl()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), kSlotBg, r);
  dl()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), you || lit ? kGold : kSlotLine, r, 0, lit ? px(2) : px(1));
  const float is = px(icon_css);
  icon(x + px(6), y + px(6), is, &m);
  float right = x + w - px(6);
  if (you) {
    const Ink yk{Face::Bold, 9, kInk};
    const float tw = text_w(yk, "YOU") + px(8), th = px(14);
    dl()->AddRectFilled(ImVec2(right - tw, y + h * .5f - th * .5f), ImVec2(right, y + h * .5f + th * .5f), kGold, px(3));
    text_center(yk, right - tw * .5f, line_top(yk, y + h * .5f - th * .5f, 14), "YOU");
    right -= tw + px(4);
  } else if (asking) {
    const Ink ak{Face::Bold, 9.5f, kGold};
    const float tw = text_w(ak, "asks");
    text(ak, right - tw, line_top(ak, y, kSlotH), "asks");
    right -= tw + px(4);
  }
  // In Training for the warm-up room (hle/slippi_warmup.h): a small "training" under the name's line.
  const bool warming = m.id < 16 && ((v.warming >> m.id) & 1);
  host_badge(x + px(6) + is * .5f, y + px(6) + is, v, &m);
  if (lit) {   // the hand on a player: their ping
    const std::string p = ping_text(v, m.id);
    if (!p.empty()) {
      const Ink pk{Face::Body, 9.5f, kDimText};
      const float tw = text_w(pk, p);
      text(pk, right - tw, line_top(pk, y, icon_css + 12), p);
      right -= tw + px(4);
    }
  }
  Ink nk{Face::Bold, 14, kWhite, false, 1, true, .3f};
  const float nx = x + px(6) + is + px(7);
  const std::string shown = fit_name(nk, m.name, right - nx);
  if (warming) {   // the name a little higher, "TRAINING" in small gold capitals under it
    text(nk, nx, line_top(nk, y - px(6), icon_css + 12), shown);
    const Ink wk{Face::Body, 9, kGold, false, 0, false, 1.2f};
    text(wk, nx, line_top(wk, y + px(10), icon_css + 12), "TRAINING");
  } else {
    text(nk, nx, line_top(nk, y, icon_css + 12), shown);
  }
}
// Members of a list that did not fit: "+3 more in line".
int line_after(const Room& r, uint8_t id) {
  const auto it = std::find(r.line.begin(), r.line.end(), id);
  return it == r.line.end() ? 0 : (int)(r.line.end() - it - 1);
}
int watchers_after(const Room& r, uint8_t id) {
  int n = 0; bool past = false;
  for (const Member& m : r.members) {
    if (past && ar::role(r, m.id) == Role::Watching) ++n;
    if (m.id == id) past = true;
  }
  return n;
}
float more_row(float x, float y, float w, int n, const char* what) {
  if (n <= 0) return y;
  const Ink dk{Face::Bold, 11, kDimText};
  text(dk, x + px(4), line_top(dk, y + px(4), 16), fit(dk, "+" + std::to_string(n) + " more " + what, w - px(4)));
  return y + px(20);
}
void open_spot(float x, float y, float w) {
  const float h = px(kSlotH);
  for (float t = x; t < x + w; t += px(7)) {
    dl()->AddLine(ImVec2(t, y), ImVec2(std::min(t + px(4), x + w), y), kSlotLine, px(1));
    dl()->AddLine(ImVec2(t, y + h), ImVec2(std::min(t + px(4), x + w), y + h), kSlotLine, px(1));
  }
  for (float t = y; t < y + h; t += px(7)) {
    dl()->AddLine(ImVec2(x, t), ImVec2(x, std::min(t + px(4), y + h)), kSlotLine, px(1));
    dl()->AddLine(ImVec2(x + w, t), ImVec2(x + w, std::min(t + px(4), y + h)), kSlotLine, px(1));
  }
  const Ink k{Face::Body, 11, IM_COL32(111, 123, 152, 255)};
  text_center(k, x + w * .5f, line_top(k, y, kSlotH), "open spot");
}

// The sheet's .steel panel: a #39445f to #1c2336 fade, a 2px border, a dark hairline inside.
void steel(float x0, float y0, float x1, float y1, ImU32 border) {
  rect_gradient(x0, y0, x1, y1, px(6), IM_COL32(57, 68, 95, 255), IM_COL32(28, 35, 54, 255));
  dl()->AddRect(ImVec2(x0 + px(2), y0 + px(2)), ImVec2(x1 - px(2), y1 - px(2)), IM_COL32(10, 13, 21, 255), px(5), 0, px(1));
  dl()->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), border, px(6), 0, px(2));
}

// ---------------------------------------------------------------- the room's state, in words
// The prompt that needs this member's answer, if any: 1 an ask to answer, 2 a doubles vote.
int prompt_kind(const View& v) {
  const Room& r = v.room;
  const int slot = ar::ring_slot(r, v.self);
  if (slot >= 0 && r.asker != ar::kNone && !(r.refused & (1u << slot)) && r.leaving_slot == ar::kNone) return 1;
  if (r.switch_pending) {
    const auto voters = ar::switch_voters(r);
    if (std::find(voters.begin(), voters.end(), v.self) != voters.end() && !(r.switch_votes & (1u << v.self))) return 2;
  }
  return 0;
}

// The X action of the sheet's bottom-right hint, for this member.
std::pair<char, std::string> x_action(const View& v) {
  const Role role = ar::role(v.room, v.self);
  if (role == Role::Fighter)
    return {'X', v.room.leaving_slot != ar::kNone && v.room.ring[v.room.leaving_slot] == v.self ? "Keep my spot" : "Give up my spot"};
  if (role == Role::InLine) return {'X', "Just watch"};
  return {'X', "Get in line"};
}

// Words wrapped to a width.
std::vector<std::string> wrap(const Ink& k, const std::string& words, float w) {
  std::vector<std::string> lines;
  std::string word, line;
  for (size_t i = 0; i <= words.size(); ++i) {
    const char c = i < words.size() ? words[i] : ' ';
    if (c != ' ') { word += c; continue; }
    const std::string trial = line.empty() ? word : line + " " + word;
    if (text_w(k, trial) > w && !line.empty()) { lines.push_back(line); line = word; }
    else line = trial;
    word.clear();
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// The sheet's one plain sentence for a member waiting to play: what happens next for them.
std::string next_sentence(const View& v) {
  const Room& r = v.room;
  if (v.active && !v.notice.empty()) return v.notice;   // a short-lived line from the room (it is full)
  if (!v.hosting && !v.linked) return v.status.empty() ? "Lost the link to the room. Reconnecting." : v.status;
  if (r.switch_pending) {
    const auto voters = ar::switch_voters(r);
    int yes = 0;
    for (uint8_t id : voters) if (id < 16 && (r.switch_votes & (1u << id))) ++yes;
    return std::string(r.doubles ? "Back to singles: " : "Doubles: ") + std::to_string(yes) + " of " +
           std::to_string(voters.size()) + " said yes.";
  }
  if (r.asker != ar::kNone && r.asker == v.self) return "You asked to play next.";
  if (ar::role(r, v.self) != Role::InLine) return "";
  const size_t at = (size_t)(std::find(r.line.begin(), r.line.end(), v.self) - r.line.begin());
  if (at > 0) return "You are number " + std::to_string(at + 1) + " in line.";
  if (r.mode == ar::Mode::WinnerStays) return "You play the winner next.";
  if (r.mode == ar::Mode::TakeTurns) {
    int out = -1;
    for (int s = 0; s < ar::ring_size(r); ++s)
      if (r.ring[s] != ar::kNone && (out < 0 || r.tenure[s] > r.tenure[out])) out = s;
    const Member* m = out >= 0 ? ar::find(r, r.ring[out]) : nullptr;
    return m ? "You play the next game. " + m->name + " has been in longest and steps out." : "You play the next game.";
  }
  return "You are first in line. Ask to play next.";
}

// The ask prompt (and the doubles vote): the sheet's .steel.ask box.
float prompt_height(float w, const View& v, int kind, std::vector<std::string>* lines_out, std::vector<std::string>* options_out) {
  const Room& r = v.room;
  std::string question;
  if (kind == 1) {
    const Member* a = ar::find(r, r.asker);
    question = (a ? a->name : std::string("Someone")) + " asks to play next. Give up your spot?";
    *options_out = {"Yes", "Yes, and I play the winner", "No"};
  } else {
    question = r.doubles ? "Back to singles?" : "Switch the four of you to doubles?";
    *options_out = {"Yes", "No"};
  }
  const Ink qk{Face::Bold, 12, kWhite};
  std::string word, line;
  for (size_t i = 0; i <= question.size(); ++i) {
    const char c = i < question.size() ? question[i] : ' ';
    if (c != ' ') { word += c; continue; }
    const std::string trial = line.empty() ? word : line + " " + word;
    if (text_w(qk, trial) > w - px(16) && !line.empty()) { lines_out->push_back(line); line = word; }
    else line = trial;
    word.clear();
  }
  if (!line.empty()) lines_out->push_back(line);
  float h = px(7) + lines_out->size() * px(15) + px(5);
  for (const std::string& o : *options_out) {
    const Ink ok{Face::Body, 11.5f, kHintText};
    h += px(2) + (text_w(ok, o) > w - px(26) ? px(28) : px(14) + px(8));
  }
  return h + px(6);
}
void prompt_box(float x, float y, float w, const View& v, int kind) {
  std::vector<std::string> lines, options;
  const float h = prompt_height(w, v, kind, &lines, &options);
  glow(x, y, x + w, y + h, px(6), IM_COL32(243, 194, 46, 90), px(10));
  steel(x, y, x + w, y + h, kGold);
  const Ink qk{Face::Bold, 12, kWhite};
  float yy = y + px(7);
  for (const std::string& l : lines) { text(qk, x + px(8), line_top(qk, yy, 15), l); yy += px(15); }
  yy += px(5);
  for (size_t i = 0; i < options.size(); ++i) {
    const bool two = text_w(Ink{Face::Body, 11.5f, kHintText}, options[i]) > w - px(26);
    const float oh = two ? px(28) : px(22);
    yy += px(2);
    const bool sel = hot(x, yy, yy + oh, (Do)((int)Do::Opt0 + (int)i));
    Ink ok{sel ? Face::Bold : Face::Body, 11.5f, sel ? kInk : kHintText};
    if (sel) {
      const int start = dl()->VtxBuffer.Size;
      dl()->AddRectFilled(ImVec2(x + px(7), yy), ImVec2(x + w - px(7), yy + oh), kWhite, px(4));
      shade_x(start, x + px(7), x + w - px(7), kGold, IM_COL32(233, 168, 26, 255));
      dl()->AddRect(ImVec2(x + px(7), yy), ImVec2(x + w - px(7), yy + oh), IM_COL32(255, 243, 194, 255), px(4), 0, px(1));
    }
    if (!two) text(ok, x + px(13), line_top(ok, yy, 22), options[i]);
    else {
      // "Yes, and I play the winner" in two lines, as the sheet wraps it.
      const std::string& o = options[i];
      const size_t cut = o.find(' ', o.size() / 2);
      text(ok, x + px(13), yy + px(1), o.substr(0, cut));
      text(ok, x + px(13), yy + px(14), cut == std::string::npos ? "" : o.substr(cut + 1));
    }
    yy += oh;
  }
}

// ---------------------------------------------------------------- the pages: rules, add a friend
void page_box(float x, float y, float w, const View& v) {
  const Ink head{Face::Title, 13, kWhite, true, 2};
  const Ink small{Face::Body, 10, kGold, false, 0, false, 1.5f};
  if (g.page == Page::Rules) {
    const float h = px(178);
    glow(x, y, x + w, y + h, px(6), IM_COL32(243, 194, 46, 90), px(10));
    steel(x, y, x + w, y + h, kGold);
    text(small, x + px(8), y + px(8), "ROOM");
    text(head, x + px(8), y + px(20), "RULES");
    const std::string values[3] = {kModeWords[(int)v.room.mode], kLookNames[(int)g.look], g.tabs ? "Tabs" : "Shown"};
    const char* labels[3] = {"Mode", "Look", "Panel"};
    for (int i = 0; i < 3; ++i) {
      const float yy = y + px(44) + i * px(28);
      if (g.row == i) {
        const int start = dl()->VtxBuffer.Size;
        dl()->AddRectFilled(ImVec2(x + px(6), yy - px(2)), ImVec2(x + w - px(6), yy + px(25)), kWhite, px(4));
        shade_x(start, x + px(6), x + w - px(6), kGold, IM_COL32(233, 168, 26, 255));
      }
      const Ink lk{Face::Body, 9.5f, g.row == i ? kInk : kDimText};
      const Ink vk{Face::Bold, 12, g.row == i ? kInk : kWhite};
      text(lk, x + px(12), yy, labels[i]);
      text(vk, x + px(12), yy + px(10), std::string("< ") + values[i] + " >");
    }
    {
      // The last row: leave the room (A). A host's room closes for everyone, and the row says so.
      const float yy = y + px(44) + 3 * px(28);
      const bool sel = g.row == 3;
      if (sel) {
        const int start = dl()->VtxBuffer.Size;
        dl()->AddRectFilled(ImVec2(x + px(6), yy - px(2)), ImVec2(x + w - px(6), yy + px(25)), kWhite, px(4));
        shade_x(start, x + px(6), x + w - px(6), kGold, IM_COL32(233, 168, 26, 255));
      }
      const Ink lk{Face::Body, 9.5f, sel ? kInk : kDimText};
      const Ink vk{Face::Bold, 12, sel ? kInk : kWhite};
      // Short lines: the box is as narrow as a bar beside a 4:3 picture.
      text(lk, x + px(12), yy, g.leave_armed ? "Sure?" : v.hosting ? "Closes it for all" : "Room");
      text(vk, x + px(12), yy + px(10), g.leave_armed ? "A again: leave" : "Leave the room");
    }
    hint(x + px(8), y + h - px(22), 'B', "Back", w - px(16), 1.0f, false);
    return;
  }
  if (g.page == Page::Code) {
    const float h = px(92) + g.recent.size() * px(18) + px(96);
    glow(x, y, x + w, y + h, px(6), IM_COL32(243, 194, 46, 90), px(10));
    steel(x, y, x + w, y + h, kGold);
    text(small, x + px(8), y + px(8), "ROOM");
    text(head, x + px(8), y + px(20), "ADD A FRIEND");
    const Ink dim{Face::Body, 10, kDimText};
    text(dim, x + px(8), y + px(40), "They enter your code too.");
    float yy = y + px(56);
    dl()->AddRectFilled(ImVec2(x + px(7), yy), ImVec2(x + w - px(7), yy + px(22)), IM_COL32(5, 7, 11, 255), px(3));
    dl()->AddRect(ImVec2(x + px(7), yy), ImVec2(x + w - px(7), yy + px(22)), kSlotLine, px(3));
    const Ink ck{Face::Bold, 13, kWhite};
    text(ck, x + px(12), line_top(ck, yy, 22), g.code + ((int)(ImGui::GetTime() * 2) % 2 ? "_" : " "));
    yy += px(28);
    for (size_t i = 0; i < g.recent.size(); ++i) {
      const bool sel = g.recent_pick == (int)i;
      if (sel) {
        const int start = dl()->VtxBuffer.Size;
        dl()->AddRectFilled(ImVec2(x + px(6), yy), ImVec2(x + w - px(6), yy + px(17)), kWhite, px(3));
        shade_x(start, x + px(6), x + w - px(6), kGold, IM_COL32(233, 168, 26, 255));
      }
      const Ink rk{Face::Bold, 11, sel ? kInk : kHintText};
      text(rk, x + px(12), line_top(rk, yy, 17), g.recent[i]);
      yy += px(18);
    }
    yy += px(4);
    const float cell = (w - px(14)) / 10.0f;
    for (int r = 0; r < 5; ++r) {
      const std::string keys = kKeys[r];
      for (size_t c = 0; c < keys.size(); ++c) {
        const float kx = x + px(7) + c * (r == 4 ? cell * 5 : cell), ky = yy + r * px(17);
        const float kw = r == 4 ? cell * 5 - px(2) : cell - px(2);
        const bool sel = g.recent_pick < 0 && g.key_row == r && g.key_col == (int)c;
        dl()->AddRectFilled(ImVec2(kx, ky), ImVec2(kx + kw, ky + px(15)), sel ? kGold : IM_COL32(45, 54, 78, 255), px(2));
        const std::string label = r == 4 ? (c == 0 ? "DEL" : "OK") : std::string(1, keys[c]);
        const Ink kk{Face::Bold, 9.5f, sel ? kInk : kWhite};
        text_center(kk, kx + kw * .5f, line_top(kk, ky, 15), label);
      }
    }
  }
}

// Z's word: under YouGame friends come in through the app's invite.
const char* add_label() { return yougame::session() ? "Invite a friend" : "Add a friend's code"; }

// ---------------------------------------------------------------- input
void send_act(Act a, uint8_t arg = 0, const std::string& text = {}) { ar::act(a, arg, text); }

void type_key(char c) {
  if (g.code.size() < 10) g.code += c;
}
void submit_code() {
  std::string c = g.code;
  for (char& ch : c) ch = (char)std::toupper((unsigned char)ch);
  if (c.find('#') == std::string::npos || c.size() < 3) return;
  send_act(Act::AddCode, 0, c);
  g.page = Page::None;
  g.code.clear();
}
void open_code_page() { g.page = Page::Code; g.code.clear(); g.recent_pick = -1; g.key_row = 0; g.key_col = 0; load_recent(); }

// The character select's hand reaches the panels: held against the left or right edge of the
// screen for a moment it goes into that side's panel (out of a room, also just resting beside the
// ARENA or WARM UP card: beside_card). The hand keeps moving as it does everywhere
// else: the button level with it lights up gold, A presses it, X, Y, R and Z do what their chips
// say, and the hand is out again once it leaves the edge (or on B). In a panel the game still sees
// the stick, but not those buttons.
// The ways in and their limits are plain functions of the hand's place (arena_hand.h, tested in
// tests/arena_rules_test.cpp): a push at the edge, or out of a room a rest beside a card.
using arena_hand::kLeftEdge;
using arena_hand::kRightEdge;
using arena_hand::kPushFrames;
using arena_hand::kPushFramesOverCard;
using arena_hand::kRestFrames;
using arena_hand::kRestStill;
constexpr uint16_t kPanelButtons = kA | kB | kX | kY | kZ | kR;
float g_bar_y0 = 0, g_bar_y1 = 1;

void leave_panel() {
  if (g.side) host::log("arena panel: the hand left the panel");
  g.side = 0; g.push_side = 0; g.push_frames = 0;
}

// Where the hand is on screen, the way the game draws it (the picture is 57.3 of the select's
// units tall, its middle 0; walter_egg.cpp measured it). The old estimate, (25 - hy) / 47 of the
// picture, sat a fifth off at the card's height (0.9.204, 2560x1440).
float hand_unit() { return (g_bar_y1 - g_bar_y0) / 57.3f; }
float hand_screen_y(float hy) { return (g_bar_y0 + g_bar_y1) * 0.5f - hy * hand_unit(); }
// A window row as the select's height (up positive).
float select_y(float screen_y) { return ((g_bar_y0 + g_bar_y1) * 0.5f - screen_y) / hand_unit(); }

// How level the glove is with that side's buttons (arena_hand::level, the best of them). In a room
// the whole edge is the panel.
int hand_level(int side, float hy, bool in_room) {
  int level = 0;
  for (const Item& it : g_last_items) {
    if (it.side != side) continue;
    if (in_room) return 2;
    level = std::max(level, arena_hand::level(hy, select_y(it.y0), select_y(it.y1)));
  }
  return level;
}

// Out of a room: the hand beside one of that side's cards without a push (arena_hand::beside).
bool beside_card(int side, float hx, float hy) {
  for (const Item& it : g_last_items)
    if (it.side == side && arena_hand::beside(side, hx, hy, select_y(it.y0), select_y(it.y1))) return true;
  return false;
}

// The button level with the hand on that side, from last frame's buttons.
void hover_nearest(int side, float hand_y) {
  const float y = hand_screen_y(hand_y);
  const Item* best = nullptr;
  float best_d = 1e9f;
  for (const Item& it : g_last_items) {
    if (it.side != side) continue;
    const float d = std::fabs((it.y0 + it.y1) * .5f - y);
    if (d < best_d) { best_d = d; best = &it; }
  }
  g.hover = best ? (uint8_t)best->what : 0;
  g.hover_member = best ? best->member : ar::kNone;
}

void press(Do what, const View& v) {
  const Role role = v.active ? ar::role(v.room, v.self) : Role::Absent;
  switch (what) {
    case Do::Rules: if (v.active) { g.page = Page::Rules; g.row = 0; } break;
    case Do::AddCode:
      // Under YouGame a friend comes in through the app's invite for the room, not a typed code.
      if (yougame::session()) send_act(Act::AddCode, 0, {});
      else open_code_page();
      break;
    case Do::Spot:
      if (!v.active) break;
      if (role == Role::Fighter) send_act(Act::GiveUp, 0);
      else send_act(Act::ToggleLine);
      break;
    case Do::Ask: if (v.active && role != Role::Fighter && v.room.asker == ar::kNone) send_act(Act::Ask); break;
    case Do::Doubles: if (v.active && !v.room.switch_pending && v.room.members.size() >= 4) send_act(Act::ProposeSwitch); break;
    case Do::Opt0: case Do::Opt1: case Do::Opt2: {
      const int prompt = v.active ? prompt_kind(v) : 0;
      const int i = (int)what - (int)Do::Opt0;
      if (prompt == 1) send_act(Act::Answer, (uint8_t)(i == 0 ? ar::Answer::Yes : i == 1 ? ar::Answer::YesAndPlayWinner : ar::Answer::No));
      if (prompt == 2) send_act(Act::VoteSwitch, i == 0 ? 1 : 0);
      break;
    }
    case Do::Leave: leave_panel(); break;
    case Do::Pip:
      host::log("arena panel: %s pressed", ar::watch_layout() == 1 ? kPipBig : kPipSmall);
      ar::toggle_watch_layout();
      break;
    case Do::Warmup:
      host::log("arena panel: WARM UP pressed");
      slippi::native_practice::submit_warmup();
      leave_panel();
      break;
    default: break;
  }
}

void handle_input(const Frame& f, const View& v) {
  const uint16_t pressed = f.buttons & ~g.prev;
  g.prev = f.buttons;
  int dir = 0;
  if ((f.buttons & kDUp) || f.stick_y > 60) dir = 1;
  else if ((f.buttons & kDDown) || f.stick_y < -60) dir = 2;
  else if ((f.buttons & kDLeft) || f.stick_x < -60) dir = 3;
  else if ((f.buttons & kDRight) || f.stick_x > 60) dir = 4;
  int step = 0;
  if (dir && (dir != g.prev_dir || f.time - g.dir_at > 0.18)) { step = dir; g.dir_at = f.time + (dir != g.prev_dir ? 0.2 : 0); }
  g.prev_dir = dir;

  if (g.page == Page::Code) {
    for (ImWchar ch : ImGui::GetIO().InputQueueCharacters)
      if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '#') type_key((char)ch);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !g.code.empty()) g.code.pop_back();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) submit_code();
    if (step == 1) {
      if (g.recent_pick < 0 && g.key_row == 0 && !g.recent.empty()) g.recent_pick = (int)g.recent.size() - 1;
      else if (g.recent_pick > 0) --g.recent_pick;
      else if (g.recent_pick < 0) g.key_row = std::max(0, g.key_row - 1);
    } else if (step == 2) {
      if (g.recent_pick >= 0) { if (++g.recent_pick >= (int)g.recent.size()) { g.recent_pick = -1; g.key_row = 0; } }
      else g.key_row = std::min(4, g.key_row + 1);
    } else if (step == 3) g.key_col = std::max(0, g.key_col - 1);
    else if (step == 4) g.key_col++;
    g.key_col = std::min(g.key_col, (int)std::string(kKeys[g.key_row]).size() - 1);
    if (pressed & kA) {
      if (g.recent_pick >= 0) { g.code = g.recent[g.recent_pick]; submit_code(); }
      else if (g.key_row == 4) { if (g.key_col == 0) { if (!g.code.empty()) g.code.pop_back(); } else submit_code(); }
      else type_key(kKeys[g.key_row][g.key_col]);
    }
    if (pressed & kStart) submit_code();
    if (pressed & kB) {
      if (!g.code.empty()) g.code.pop_back();
      else g.page = Page::None;
    }
    return;
  }
  if (g.page == Page::Rules) {
    if (step == 1) g.row = std::max(0, g.row - 1);
    if (step == 2) g.row = std::min(3, g.row + 1);
    if (g.row != 3) g.leave_armed = false;
    if (g.row == 3) {
      // Leave the room: A, then A again (one press on the way down the list never leaves by accident).
      if (pressed & kA) {
        if (!g.leave_armed) g.leave_armed = true;
        else {
          host::log("arena panel: leave the room");
          send_act(Act::LeaveRoom);
          g.leave_armed = false; g.page = Page::None; g.row = 0;
          leave_panel();
        }
      }
      if (pressed & kB) { g.leave_armed = false; g.page = Page::None; g.row = 0; }
      return;
    }
    const int delta = step == 3 ? -1 : step == 4 || (pressed & kA) ? 1 : 0;
    if (delta) {
      if (g.row == 0 && v.active) send_act(Act::SetMode, (uint8_t)(((int)v.room.mode + delta + 3) % 3));
      if (g.row == 1) { g.look = (Look)(((int)g.look + delta + kLookCount) % kLookCount); save(); }
      if (g.row == 2) { g.tabs = !g.tabs; save(); }
    }
    if (pressed & kB) { g.page = Page::None; g.row = 0; }
    return;
  }

  g.swallow &= f.buttons;   // let go: the game sees that button again
  float hx = 0, hy = 0;
  const bool have_hand = ar::css_hand(&hx, &hy);
  if (g.side == 0) {
    // In: the hand held against an edge, the stick still pushing out; or, out of a room, the hand
    // resting beside a card (beside_card).
    int want = 0;
    bool resting = false;
    if (have_hand && arena_hand::pushing(1, hx, f.stick_x)) want = 1;
    if (have_hand && arena_hand::pushing(2, hx, f.stick_x)) want = 2;
    // Only into a side that has buttons (out of a room the left has none: it went in and out every
    // 11 frames, taking a frame of input each time), and out of a room only beside the ARENA tab:
    // higher up the right edge the hand is on Ganondorf or Young Link, whose players hold the stick
    // right to reach them and then press A to pick.
    if (want) {
      // Level with the card: in after the usual push. Only the glove's lower part over the card (its
      // point is up on Young Link, whose players hold the stick right to reach him): in after a
      // longer push, so picking him never opens the panel, while pushing into the card still does
      // (0.9.204, 2560x1440).
      const int level = hand_level(want, hy, v.active);
      if (!level) want = 0;
      g.push_needed = level == 1 ? kPushFramesOverCard : kPushFrames;
    }
    if (!want && have_hand && !v.active) {
      if (beside_card(2, hx, hy)) want = 2;
      else if (beside_card(1, hx, hy)) want = 1;
      resting = want != 0;
    }
    if (resting) {
      // Still: the count starts over whenever the hand moves on (it slides along the edge past the card).
      if (want != g.push_side || std::fabs(hy - g.rest_y) > kRestStill) { g.rest_y = hy; g.push_side = want; g.push_frames = 0; }
      g.push_needed = kRestFrames;
    }
    {
      // MELEE_TEST_ARENA_HANDLOG=1 (tests): the hand and the push, every 15 frames while it is near an edge.
      static const bool log_hand = [] { const char* e = host::test_env::getenv("MELEE_TEST_ARENA_HANDLOG"); return e && *e == '1'; }();
      static int n = 0;
      if (log_hand && have_hand && (hx > 20.0f || hx < -29.0f) && ++n % 15 == 0)
        host::log("arena panel: hand %.1f %.1f stick %d want %d%s", hx, hy, (int)f.stick_x, want, resting ? " resting" : "");
    }
    if (want && want == g.push_side) ++g.push_frames;
    else { g.push_side = want; g.push_frames = want ? 1 : 0; }
    if (want && g.push_frames >= g.push_needed) {
      g.side = want;
      g.stay_level = g.push_needed == kPushFramesOverCard ? 1 : 2;
      g.push_frames = 0;
      hover_nearest(want, hy);
      host::log("arena panel: the hand went into the %s panel (on button %d%s)", want == 1 ? "left" : "right", (int)g.hover,
                resting ? ", resting beside the card" : "");
      g.prev_dir = dir;
      g.dir_at = f.time + 0.25;   // the push that brought the hand in moves nothing
    }
    return;
  }
  // In a panel: the hand moves on as the game moves it (nothing holds it at a button), and the
  // button level with it lights up. Out once it leaves the edge or is level with no button, or on B.
  g.swallow |= f.buttons & kPanelButtons;
  // Out of a room the cards reach in further (a hand let in resting beside one stays in).
  const bool at_edge = have_hand && arena_hand::at_side(g.side, hx, v.active);
  if (!at_edge || !(hand_level(g.side, hy, v.active) >= g.stay_level) || (pressed & kB)) { leave_panel(); return; }
  const uint8_t was = g.hover, was_member = g.hover_member;
  hover_nearest(g.side, hy);
  if (!g.hover) { leave_panel(); return; }
  if (g.hover != was || g.hover_member != was_member) host::log("arena panel: the hand is on button %d", (int)g.hover);
  if (pressed & kA) { host::log("arena panel: A on button %d", (int)g.hover); press((Do)g.hover, v); }
  if (pressed & kX) press(Do::Spot, v);
  if (pressed & kY) press(Do::Rules, v);
  if (pressed & kZ) press(Do::AddCode, v);
  if (pressed & kR) press(Do::Doubles, v);
}

// ---------------------------------------------------------------- the looks
struct Bars { float lx0, lx1, rx0, rx1, y0, y1; float px0 = 0, px1 = 0; };   // px0, px1: the picture's left and right
float g_card_left = 0;   // this frame's Arena card (out of a room): its left edge, 0 when not drawn

// The legend at the foot of a bar: the sheet's hints. They are buttons: the hand pushed into the
// panel lights one up and A presses it (and X, Y and Z work while the hand is in the panel).
float legend(float x, float w, float bottom, std::vector<std::pair<char, std::string>> items) {
  return hints_up(x, w, bottom, items, 1.0f);
}
float z_line(float, float, float above) { return above; }

// One corner of the combined look: the sheet's .corner (title, 80px portrait, name, record and
// streak) over its red or blue fade, with the 3px edge toward the picture. Returns its bottom.
float corner(float x0, float x1, float y0, bool red, const View& v, int side, bool tall_to = 0, float tall_y1 = 0) {
  const Room& r = v.room;
  const float w = x1 - x0, cx = x0 + w * .5f;
  const int fighters = r.doubles ? 2 : 1;
  const float icon_css = r.doubles ? 52 : (tall_to ? 88 : 80);
  // Height: 12 + title 15 + 11 + icon + 8 + name 18 + 5 + record 21 + 12 (the sheet's boxes).
  float h = px(12 + 15 + 11) + px(icon_css) + px(8 + 18 + 5 + 21 + 12);
  if (r.doubles) h += px(18);   // a second name under the pair
  const float y1 = tall_to ? tall_y1 : y0 + h;
  const ImU32 c0 = red ? IM_COL32(216, 52, 60, 140) : IM_COL32(58, 86, 216, 153);
  const ImU32 c1 = red ? IM_COL32(216, 52, 60, 15) : IM_COL32(58, 86, 216, 15);
  {
    const int start = dl()->VtxBuffer.Size;
    const float y85 = y0 + (y1 - y0) * .85f;
    dl()->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y85), kWhite);
    shade(start, y0, y85, c0, c1);
    dl()->AddRectFilled(ImVec2(x0, y85), ImVec2(x1, y1), c1);
  }
  if (red) dl()->AddRectFilled(ImVec2(x1 - px(3), y0), ImVec2(x1, y1), kRed);
  else dl()->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + px(3), y1), kBlue);
  float y = y0 + px(12);
  const Ink title{Face::Title, 15, kWhite, true, 2};
  text_center(title, cx, line_top(title, y, 15), red ? "RED CORNER" : "BLUE CORNER");
  y += px(15 + 11);
  const Member* m[2] = {nullptr, nullptr};
  for (int k = 0; k < fighters; ++k) m[k] = ar::find(r, r.ring[r.doubles ? side * 2 + k : side]);
  const float is = px(icon_css);
  if (fighters == 1) {
    icon(cx - is * .5f, y, is, m[0], false, red ? Ground::Red : Ground::Blue);
    host_badge(cx, y + is, v, m[0]);
  } else {
    const float gap = px(6);
    icon(cx - is - gap * .5f, y, is, m[0], false, red ? Ground::Red : Ground::Blue);
    icon(cx + gap * .5f, y, is, m[1], false, red ? Ground::Red : Ground::Blue);
    host_badge(cx - gap * .5f - is * .5f, y + is, v, m[0]);
    host_badge(cx + gap * .5f + is * .5f, y + is, v, m[1]);
  }
  // A fighter still in Training (the warm-up room) on the way back: a small gold TRAINING over the
  // foot of the portrait.
  for (int k = 0; k < fighters; ++k) {
    if (!m[k] || m[k]->id >= 16 || !((v.warming >> m[k]->id) & 1)) continue;
    const Ink wk{Face::Bold, 9, kInk};
    const float tw = text_w(wk, "TRAINING") + px(8), th = px(14);
    const float icx = fighters == 1 ? cx : (k == 0 ? cx - px(3) - is * .5f : cx + px(3) + is * .5f);
    const float ty = y + is - th - px(4);
    dl()->AddRectFilled(ImVec2(icx - tw * .5f, ty), ImVec2(icx + tw * .5f, ty + th), kGold, px(3));
    text_center(wk, icx, line_top(wk, ty, 14), "TRAINING");
  }
  y += is + px(8);
  const Ink big{Face::Black, 15, kWhite, false, 1};
  for (int k = 0; k < fighters; ++k) {
    const std::string name = m[k] ? m[k]->name : std::string("...");
    text_center(big, cx, line_top(big, y, 18), fit(big, name, w - px(16)));
    y += px(18);
  }
  y += px(5);
  // The record, the streak beside it, the ping under it (small).
  const Member* lead = m[0];
  if (lead) {
    Ink wl{Face::Title, 21, kGold, true, 2};
    const Ink sm{Face::Body, 10, kPaper, false, 1, false, .5f};
    const std::string rec = record(*lead);
    std::string streak;
    for (int k = 0; k < fighters; ++k)
      if (m[k] && r.streak_id == m[k]->id && r.streak >= 2) streak = "x" + std::to_string(r.streak) + " STREAK";
    // Long records (3 digits) and streaks: the word STREAK goes first, then the record gets smaller.
    const float avail = (x1 - x0) - px(14);
    auto line_w = [&] { return text_w(wl, rec) + (wl.italic ? size_of(wl.face, wl.css) * .16f : 0.f) +
                               (streak.empty() ? 0.f : text_w(sm, streak) + px(5)); };
    if (line_w() > avail && !streak.empty()) streak = "x" + std::to_string(r.streak);
    while (line_w() > avail && wl.css > 21 * 0.65f) wl.css -= 0.5f;
    const float rw = text_w(wl, rec), sw = streak.empty() ? 0 : text_w(sm, streak) + px(5);
    const float left = cx - (rw + sw) * .5f;
    text(wl, left, line_top(wl, y, 21), rec);
    if (!streak.empty()) text(sm, left + rw + px(5), y + px(21) - size_of(sm.face, sm.css) * .95f, streak);
    const bool lit = hot(x0 + px(4), y0, y1, Do::Member, lead->id);
    if (lit) dl()->AddRect(ImVec2(cx - is * .5f - px(3), y0 + px(12 + 15 + 11) - px(3)),
                           ImVec2(cx + is * .5f + px(3), y0 + px(12 + 15 + 11) + is + px(3)), kGold, px(7), 0, px(2));
    const std::string p = lit ? ping_text(v, lead->id) : std::string();
    if (!p.empty()) {
      const Ink pk{Face::Body, 9.5f, IM_COL32(232, 236, 245, 170)};
      text_center(pk, cx, y + px(23), p);
    }
  }
  return y1;
}

void draw_combined(const Bars& b, const View& v, bool corners_look) {
  const Room& r = v.room;
  const float lw = b.lx1 - b.lx0, rw = b.rx1 - b.rx0;
  const float pad = px(8), bottom = b.y1 - px(10);
  // Corners (the option): tall corners down to a foot box, as the sheet's Corners frame.
  if (corners_look) {
    const float foot_h = px(9 + 17 + 6 + 32 + 10 + 37 + 10);   // tag, a row, two hint rows (the sheet's .foot)
    const float fy = b.y1 - foot_h;
    corner(b.lx0, b.lx1, b.y0, true, v, 0, true, fy);
    corner(b.rx0, b.rx1, b.y0, false, v, 1, true, fy);
    for (int s = 0; s < 2; ++s) {
      const float x0 = s ? b.rx0 : b.lx0, x1 = s ? b.rx1 : b.lx1;
      dl()->AddRectFilled(ImVec2(x0, fy), ImVec2(x1, b.y1), kFoot);
      dl()->AddRectFilled(ImVec2(x0, fy), ImVec2(x1, fy + px(2)), kSlotLine);
    }
    float y = fy + px(9);
    tag_gold(b.lx0 + pad, y, "NEXT UP");
    y += px(17 + 6);
    const Ink nk{Face::Bold, 14, kWhite, false, 1, true, .3f};
    if (!r.line.empty()) {
      const Member* m = ar::find(r, r.line[0]);
      if (m) {
        const Ink mk{Face::Bold, 11, kDimText};
        const std::string more = r.line.size() > 1 ? "+" + std::to_string(r.line.size() - 1) : "";
        const float mw = more.empty() ? 0.f : text_w(mk, more) + px(4);
        icon(b.lx0 + pad, y, px(32), m);
        Ink nk2 = nk;
        text(nk2, b.lx0 + pad + px(39), line_top(nk2, y, 32), fit_name(nk2, m->name, lw - pad * 2 - px(39) - mw));
        if (!more.empty()) text(mk, b.lx1 - pad - mw + px(4), line_top(mk, y, 32), more);
      }
    } else {
      const Ink dk{Face::Body, 11, IM_COL32(111, 123, 152, 255)};
      text(dk, b.lx0 + pad, line_top(dk, y, 32), "open spot");
    }
    hints_up(b.lx0 + pad, lw - pad * 2, bottom, {x_action(v)}, g.focus ? 1.f : .55f);
    y = fy + px(9);
    tag_grey(b.rx0 + pad, y, "WATCHING");
    y += px(17 + 6);
    const Member* w0 = nullptr;
    for (const Member& m : r.members) if (ar::role(r, m.id) == Role::Watching) { w0 = &m; break; }
    if (w0) {
      int watchers = 0;
      for (const Member& m : r.members) if (ar::role(r, m.id) == Role::Watching) ++watchers;
      const Ink mk{Face::Bold, 11, kDimText};
      const std::string more = watchers > 1 ? "+" + std::to_string(watchers - 1) : "";
      const float mw = more.empty() ? 0.f : text_w(mk, more) + px(4);
      icon(b.rx0 + pad, y, px(32), w0);
      Ink nk2 = nk;
      text(nk2, b.rx0 + pad + px(39), line_top(nk2, y, 32), fit_name(nk2, w0->name, rw - pad * 2 - px(39) - mw));
      if (!more.empty()) text(mk, b.rx1 - pad - mw + px(4), line_top(mk, y, 32), more);
    }
    else { const Ink dk{Face::Body, 11, IM_COL32(111, 123, 152, 255)}; text(dk, b.rx0 + pad, line_top(dk, y, 32), "nobody"); }
    hints_up(b.rx0 + pad, rw - pad * 2, bottom, {{'Y', "Rules"}, {'Z', add_label()}}, g.focus ? 1.f : .55f);
    if (!g.focus) z_line(b.rx0 + pad, rw - pad * 2, fy - px(4));
    const int prompt = prompt_kind(v);
    if (prompt) {
      const int my_slot = ar::ring_slot(r, v.self);
      const bool left = my_slot == 0 || (my_slot == 1 && r.doubles);
      std::vector<std::string> l, o;
      const float w = (left ? lw : rw) - pad * 2;
      const float h = prompt_height(w, v, prompt, &l, &o);
      prompt_box(left ? b.lx0 + pad : b.rx0 + pad, fy - px(10) - h, w, v, prompt);
    } else if (const std::string said = next_sentence(v); !said.empty()) {
      // The plain line every other look shows (the link to the room lost, a doubles vote, your own
      // ask, your place in line), at the foot of the red corner.
      const Ink dk{Face::Body, 11, v.notice.empty() ? kDimText : kWhite};   // a notice stands out
      const std::vector<std::string> lines = wrap(dk, said, lw - pad * 2);
      float sy = fy - px(8) - lines.size() * px(15);
      for (const std::string& l : lines) { text(dk, b.lx0 + pad, line_top(dk, sy, 15), l); sy += px(15); }
    }
    return;
  }

  // The combined look (the default): corners on top, the line and the stands as cards under them.
  const float ly = corner(b.lx0, b.lx1, b.y0, true, v, 0);
  const float ry = corner(b.rx0, b.rx1, b.y0, false, v, 1);
  // The room's match window in its small size sits under this card (slippi_arena pip_rect).
  if (ImGui::GetIO().DisplaySize.y > 0) ar::note_side_card_bottom(ry / ImGui::GetIO().DisplaySize.y);
  // Left under: NEXT UP and the line, with an open spot after it.
  float y = ly + px(10);
  tag_gold(b.lx0 + pad, y, "NEXT UP");
  y += px(17);
  for (uint8_t id : r.line) {
    const Member* m = ar::find(r, id);
    if (!m) continue;
    y += px(6);
    slot_card(b.lx0 + pad, y, lw - pad * 2, v, *m, id == v.self, r.asker == id);
    y += px(kSlotH);
    if (y > b.y1 - px(190)) { y = more_row(b.lx0 + pad, y, lw - pad * 2, line_after(r, id), "in line"); break; }
  }
  if (r.line.size() < 2) { y += px(6); open_spot(b.lx0 + pad, y, lw - pad * 2); y += px(kSlotH); }
  // Right under: WATCHING, only when someone is just watching.
  float wy = ry + px(10);
  bool any = false;
  for (const Member& m : r.members) {
    if (ar::role(r, m.id) != Role::Watching) continue;
    if (!any) { tag_grey(b.rx0 + pad, wy, "WATCHING"); wy += px(17); any = true; }
    wy += px(6);
    slot_card(b.rx0 + pad, wy, rw - pad * 2, v, m, m.id == v.self, r.asker == m.id);
    wy += px(kSlotH);
    if (wy > b.y1 - px(170)) { wy = more_row(b.rx0 + pad, wy, rw - pad * 2, watchers_after(r, m.id), "watching"); break; }
  }
  // Left foot: the prompt (under your own corner) or the plain sentence, then Y and Z.
  const int prompt = prompt_kind(v);
  const int my_slot = ar::ring_slot(r, v.self);
  const bool prompt_left = my_slot != 1 || r.doubles;   // the blue corner's player answers on the right
  std::vector<std::pair<char, std::string>> left_items =
      prompt && prompt_left && g.focus ? std::vector<std::pair<char, std::string>>{{'A', "Choose"}}
                                       : std::vector<std::pair<char, std::string>>{{'Y', "Rules"}, {'Z', add_label()}};
  if (warmup_row_shown()) left_items.insert(left_items.begin(), {'L', "Warm up"});
  float lfoot = legend(b.lx0 + pad, lw - pad * 2, bottom, left_items);
  lfoot = z_line(b.lx0 + pad, lw - pad * 2, lfoot);
  if (prompt && prompt_left) {
    std::vector<std::string> l, o;
    const float w = lw - pad * 2;
    const float h = prompt_height(w, v, prompt, &l, &o);
    prompt_box(b.lx0 + pad, lfoot - px(7) - h, w, v, prompt);
  } else {
    const Ink dk{Face::Body, 11, v.notice.empty() ? kDimText : kWhite};   // a notice stands out
    const std::vector<std::string> lines = wrap(dk, next_sentence(v), lw - pad * 2);
    float sy = lfoot - px(8) - lines.size() * px(15);
    for (const std::string& l : lines) { text(dk, b.lx0 + pad, line_top(dk, sy, 15), l); sy += px(15); }
  }
  // Right foot: the mode in gold capitals, then this member's X action (and A to ask when free).
  std::vector<std::pair<char, std::string>> right = {x_action(v)};
  const Role role = ar::role(r, v.self);
  // Watching with the room's match window up: A on this makes it small (picture in picture) or full size.
  if (role != Role::Fighter && ar::watch_layout() != 0) right.insert(right.begin(), {'A', ar::watch_layout() == 1 ? kPipBig : kPipSmall});
  if (g.side == 2 && role != Role::Fighter && r.asker == ar::kNone) right.push_back({'A', "Ask to play next"});
  if (g.side == 2 && r.members.size() >= 4 && !r.switch_pending) right.push_back({'R', r.doubles ? "Back to singles" : "Doubles"});
  if (g.side == 2) right.push_back({'B', "Close"});
  float rfoot = legend(b.rx0 + pad, rw - pad * 2, bottom, right);
  const Ink cap{Face::Body, 10, kGold, false, 0, false, 1.5f};
  text(cap, b.rx0 + pad, rfoot - px(6) - px(12), std::string(r.doubles ? "DOUBLES  " : "") + kModeNames[(int)r.mode]);
  if (prompt && !prompt_left) {
    std::vector<std::string> l, o;
    const float w = rw - pad * 2;
    const float h = prompt_height(w, v, prompt, &l, &o);
    prompt_box(b.rx0 + pad, rfoot - px(26) - h, w, v, prompt);
  }
}

// The sheet's .row (Card look): a portrait, the name and the record in gold at the right.
void card_row(float x, float y, float w, const View& v, const Member& m) {
  icon(x, y, px(34), &m, m.id == v.self);
  const Ink rk{Face::Bold, 12, kRecordCol, false, 0, false, .5f};
  const std::string rec = record(m);
  const float rw = text_w(rk, rec);
  text(rk, x + w - rw, line_top(rk, y, 34), rec);
  host_badge(x + px(17), y + px(34), v, &m);
  Ink nk{Face::Bold, 14, kWhite, false, 1, true, .3f};
  const std::string shown = fit_name(nk, m.name, w - px(41) - rw - px(4));
  text(nk, x + px(41), line_top(nk, y, 34), shown);
}

// The sheet's .mode: small gold capitals, then the mode in Arial Black italic, two lines when it
// does not fit (the Card's "WINNER / STAYS"). Returns its height.
float mode_block(float x, float y, float w, const char* small_caps, const View& v) {
  const Ink sm{Face::Body, 10, kGold, false, 0, false, 1.5f};
  const Ink md{Face::Title, 15, kWhite, true, 2};
  text(sm, x, y, fit(sm, small_caps, w));
  const std::string mode = std::string(v.room.doubles ? "DOUBLES " : "") + kModeNames[(int)v.room.mode];
  float yy = y + px(13);
  if (text_w(md, mode) <= w) { text(md, x, line_top(md, yy, 16), mode); return px(13 + 16); }
  std::string first = mode, rest;
  const size_t cut = mode.rfind(' ');
  if (cut != std::string::npos) { first = mode.substr(0, cut); rest = mode.substr(cut + 1); }
  text(md, x, line_top(md, yy, 16), first);
  if (!rest.empty()) text(md, x, line_top(md, yy + px(16), 16), rest);
  return px(13 + 32);
}

// Card (right bar only) and Ring (the ring box at bar width).
void draw_card(const Bars& b, const View& v, bool ring_box) {
  const Room& r = v.room;
  const float x0 = b.rx0 + px(8), x1 = b.rx1 - px(8), y0 = b.y0 + px(10), y1 = b.y1 - px(10);
  if (!ring_box) steel(x0, y0, x1, y1, kLineCol);
  const float x = ring_box ? x0 : x0 + px(8), w = ring_box ? x1 - x0 : x1 - x0 - px(16);
  float y = (ring_box ? y0 : y0 + px(10));
  y += mode_block(x, y, w - px(4), "ARENA", v) + px(2);
  const Ink nk{Face::Bold, 12, kWhite, false, 1, true};
  if (ring_box) {
    // The sheet's .ringbox: a deep blue ring with red, white and blue ropes round it.
    const float bx0 = x + px(7), bx1 = x + w - px(7), by0 = y + px(14), bh = px(94);
    const float rr = px(11);
    dl()->AddRect(ImVec2(bx0 - px(5), by0 - px(5)), ImVec2(bx1 + px(5), by0 + bh + px(5)), kBlue, rr + px(5), 0, px(2));
    dl()->AddRect(ImVec2(bx0 - px(3), by0 - px(3)), ImVec2(bx1 + px(3), by0 + bh + px(3)), kIconLine, rr + px(3), 0, px(2));
    dl()->AddRect(ImVec2(bx0 - px(1), by0 - px(1)), ImVec2(bx1 + px(1), by0 + bh + px(1)), kRed, rr + px(1), 0, px(2));
    rect_gradient(bx0, by0, bx1, by0 + bh, rr, IM_COL32(70, 85, 124, 255), IM_COL32(27, 35, 56, 255));
    dl()->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by0 + bh), kIconLine, rr, 0, px(2));
    for (int side = 0; side < 2; ++side) {
      const Member* m = ar::find(r, r.ring[r.doubles ? side * 2 : side]);
      // The sheet's ring: portrait (40), 5px, VS, 5px, portrait, centred in the box.
      const float row = px(40 + 5 + 22 + 5 + 40), left = (bx0 + bx1) * .5f - row * .5f;
      const float cx = side ? left + row - px(20) : left + px(20);
      icon(cx - px(20), by0 + px(10), px(40), m, m && m->id == v.self);
      if (m) {
        text_center(nk, cx, by0 + px(54), fit(nk, m->name, (bx1 - bx0) * .45f));
        const Ink rk{Face::Bold, 11, kRecordCol};
        text_center(rk, cx, by0 + px(70), record(*m));
      }
    }
    const Ink vs{Face::Title, 13, kGold, true, 1};
    text_center(vs, (bx0 + bx1) * .5f, by0 + px(22), "VS");
    y = by0 + bh + px(18);
  } else {
    y += px(10);
    tag_red(x, y, "IN THE RING");
    y += px(17 + 2);
    for (int s = 0; s < ar::ring_size(r); ++s) {
      const Member* m = ar::find(r, r.ring[s]);
      if (m) { y += px(6); card_row(x, y, w, v, *m); y += px(34); }
    }
    y += px(10);
  }
  tag_gold(x, y, ring_box ? "THE LINE" : "NEXT UP");
  y += px(17);
  for (uint8_t id : r.line) {
    const Member* m = ar::find(r, id);
    if (!m) continue;
    y += px(6);
    if (ring_box) { slot_card(x, y, w, v, *m, id == v.self, r.asker == id); y += px(kSlotH); }
    else { card_row(x, y, w, v, *m); y += px(34); }
    if (y > y1 - px(200)) { y = more_row(x, y, w, line_after(r, id), "in line"); break; }
  }
  if (r.line.empty()) { y += px(6); open_spot(x, y, w); y += px(kSlotH); }
  bool any = false;
  for (const Member& m : r.members) {
    if (ar::role(r, m.id) != Role::Watching) continue;
    if (!any) { y += px(ring_box ? 12 : 10); tag_grey(x, y, ring_box ? "STANDS" : "WATCHING"); y += px(17); any = true; }
    y += px(6);
    if (ring_box) { slot_card(x, y, w, v, m, m.id == v.self, r.asker == m.id); y += px(kSlotH); }
    else { card_row(x, y, w, v, m); y += px(34); }
    if (y > y1 - px(170)) { y = more_row(x, y, w, watchers_after(r, m.id), "watching"); break; }
  }
  std::vector<std::pair<char, std::string>> items = {x_action(v), {'Y', "Rules"}, {'Z', add_label()}};
  const int prompt = prompt_kind(v);
  if (prompt && g.focus) items = {{'A', "Choose"}};
  if (g.focus) items.push_back({'B', "Close"});
  float foot = legend(x, w, ring_box ? y1 : y1 - px(10), items);
  foot = z_line(x, w, foot);
  if (prompt) {
    std::vector<std::string> l, o;
    const float h = prompt_height(w, v, prompt, &l, &o);
    prompt_box(x, foot - px(7) - h, w, v, prompt);
  } else {
    const std::string said = next_sentence(v);
    const std::string line = !v.linked && !v.hosting ? said : !said.empty() && (r.switch_pending || r.asker == v.self) ? said : v.status;
    const Ink dk{Face::Body, 11, v.notice.empty() ? kDimText : kWhite};   // a notice stands out
    std::vector<std::string> lines = wrap(dk, line, w);
    if (lines.size() > 2) { lines.resize(2); lines[1] = fit(dk, lines[1] + " ...", w); }
    float sy = foot - px(8) - lines.size() * px(15);
    for (const std::string& l : lines) { text(dk, x, line_top(dk, sy, 15), l); sy += px(15); }
  }
}

// Split: who fights and the score on the left, who waits on the right.
void draw_split(const Bars& b, const View& v) {
  const Room& r = v.room;
  const float lw = b.lx1 - b.lx0, rw = b.rx1 - b.rx0, pad = px(8);
  const float x = b.lx0 + pad, w = lw - pad * 2;
  float y = b.y0 + px(10);
  const Member* host = ar::find(r, ar::owner(r));
  std::string caps = (host ? host->name : std::string("ROOM")) + "'S ROOM";
  for (char& c : caps) c = (char)std::toupper((unsigned char)c);
  y += mode_block(x, y, w, caps.c_str(), v) + px(12);
  tag_red(x, y, "IN THE RING");
  y += px(17);
  for (int s = 0; s < ar::ring_size(r); ++s) {
    const Member* m = ar::find(r, r.ring[s]);
    if (m) { y += px(6); slot_card(x, y, w, v, *m, m->id == v.self, false, 36); y += px(48); }
  }
  const Member* a = ar::find(r, r.ring[0]);
  const Member* c = ar::find(r, r.ring[r.doubles ? 2 : 1]);
  if (a && c) {
    const Ink sc{Face::Title, 28, kWhite, true, 2};
    const Ink dash{Face::Title, 17, kGold, true, 2};
    const std::string l = std::to_string(a->wins), rr = std::to_string(c->wins);
    const float tw = text_w(sc, l) + px(6) + text_w(dash, "-") + px(6) + text_w(sc, rr);
    float sx = x + w * .5f - tw * .5f;
    const float sy = y + px(14);
    text(sc, sx, line_top(sc, sy, 28), l); sx += text_w(sc, l) + px(6);
    text(dash, sx, line_top(dash, sy, 28), "-"); sx += text_w(dash, "-") + px(6);
    text(sc, sx, line_top(sc, sy, 28), rr);
  }
  float lfoot = legend(x, w, b.y1 - px(10), {{'Y', "Rules"}, {'Z', add_label()}});
  lfoot = z_line(x, w, lfoot);
  {
    const std::string said = next_sentence(v);
    const std::string line = !v.linked && !v.hosting ? said : !said.empty() && (r.switch_pending || r.asker == v.self) ? said : v.status;
    const Ink dk{Face::Body, 11, v.notice.empty() ? kDimText : kWhite};   // a notice stands out
    std::vector<std::string> lines = wrap(dk, line, w);
    if (lines.size() > 2) { lines.resize(2); lines[1] = fit(dk, lines[1] + " ...", w); }
    float sy = lfoot - px(8) - lines.size() * px(15);
    for (const std::string& l : lines) { text(dk, x, line_top(dk, sy, 15), l); sy += px(15); }
  }
  // Right: the line, then the watchers.
  const float rx = b.rx0 + pad, rwid = rw - pad * 2;
  float ry = b.y0 + px(10);
  tag_gold(rx, ry, "THE LINE");
  ry += px(17);
  for (uint8_t id : r.line) {
    const Member* m = ar::find(r, id);
    if (!m) continue;
    ry += px(6);
    slot_card(rx, ry, rwid, v, *m, id == v.self, r.asker == id);
    ry += px(kSlotH);
    if (ry > b.y1 - px(200)) { ry = more_row(rx, ry, rwid, line_after(r, id), "in line"); break; }
  }
  if (r.line.size() < 2) { ry += px(6); open_spot(rx, ry, rwid); ry += px(kSlotH); }
  bool any = false;
  for (const Member& m : r.members) {
    if (ar::role(r, m.id) != Role::Watching) continue;
    if (!any) { ry += px(14); tag_grey(rx, ry, "WATCHING"); ry += px(17); any = true; }
    ry += px(6);
    slot_card(rx, ry, rwid, v, m, m.id == v.self, r.asker == m.id);
    ry += px(kSlotH);
    if (ry > b.y1 - px(170)) { ry = more_row(rx, ry, rwid, watchers_after(r, m.id), "watching"); break; }
  }
  const int prompt = prompt_kind(v);
  std::vector<std::pair<char, std::string>> items = {x_action(v)};
  if (prompt && g.focus) items = {{'A', "Choose"}};
  if (g.focus) items.push_back({'B', "Close"});
  const float rfoot = legend(rx, rwid, b.y1 - px(10), items);
  if (prompt) {
    std::vector<std::string> l, o;
    const float h = prompt_height(rwid, v, prompt, &l, &o);
    prompt_box(rx, rfoot - px(7) - h, rwid, v, prompt);
  }
}

bool test_open_card() {
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_CARD"); return v && *v == '1'; }();
  return on;
}

// ---------------------------------------------------------------- the warm-up room's button
// (hle/slippi_warmup.h): Training while staying connected (Direct, Teams) or while waiting for a
// turn in a room. Out of a room a card in the left bar, beside the Arena card on the right; in a
// room a row at the foot of the left bar. L and R held do the same (native_practice.cpp).
slippi::native_practice::Snapshot::Warmup g_warmup_view;
bool warmup_row_shown() { return gx::RenderOptions::live_warmup_button() && g_warmup_view.offered && g_warmup_view.enabled; }

void draw_warmup_card(const Bars& b) {
  const auto& w = g_warmup_view;
  if (!gx::RenderOptions::live_warmup_button() || !w.offered) return;
  const bool open = g.side == 1;
  const float tw = b.lx1 - b.lx0 - px(16), th = px(58);
  float x = b.lx0 + px(8);
  const float y = b.y0 + (b.y1 - b.y0) * .42f;
  // Widescreen: beside the hand's reach at the 4:3 select's left edge (as the Arena card on the right).
  const float pic_w = b.px1 - b.px0, pic_h = b.y1 - b.y0;
  if (pic_h > 1.0f && pic_w / pic_h > 1.40f) {
    const float unit = pic_h / 57.3f;
    const float reach = (b.px0 + b.px1) * .5f + (kLeftEdge - 5.0f) * unit - px(10) - tw;
    x = std::max(x, reach);
  }
  steel(x, y, x + tw, y + th, open && w.enabled ? kGold : IM_COL32(154, 166, 192, w.enabled ? 200 : 110));
  if (w.enabled) hot(x, y, y + th, Do::Warmup);
  // Two lines centred across the box, one face (as the room's cards): its name, and under it the way in
  // (A while the hand is on it, else L and R held). The same 11 above the name as below the buttons.
  const float cx = x + tw * .5f;
  const Ink name{Face::Bold, 15, w.enabled ? kWhite : kDimText};
  text_center(name, cx, line_top(name, y + px(11), 18), "WARM UP");
  const float ry = y + th - px(11) - px(16);   // the button row, 16 tall
  if (!w.enabled) {
    Ink how{Face::Body, 10.5f, IM_COL32(200, 208, 224, 255)};
    const std::string why = fit_name(how, w.why.empty() ? std::string("Not now") : w.why, tw - px(20));
    text_center(how, cx, line_top(how, ry, 16), why);
  } else {
    const Ink hk{Face::Body, 11.5f, IM_COL32(214, 220, 234, 255)};
    const std::string label = open ? "Go now" : "Hold";
    const float chips = open ? px(16) : lr_chips_width(), gap = px(7);
    float at = cx - (chips + gap + text_w(hk, label)) * .5f;
    if (open) button_chip(at, ry, 'A');
    else lr_chips(at, ry);
    text(hk, at + chips + gap, line_top(hk, ry, 16), label);
  }
  if (w.hold > 0.0f) {   // L+R held: the card fills toward going
    const float x1 = x + px(4) + (tw - px(8)) * std::clamp(w.hold, 0.0f, 1.0f);
    dl()->AddRectFilled(ImVec2(x + px(4), y + th - px(6)), ImVec2(x1, y + th - px(3)), kGold, px(2));
  }
  // Who on the other side is warming up, centred under the box.
  std::string other;
  for (int i = 0; i < w.peers && i < 3; ++i)
    if (w.peer[i].state == 2) { other = w.peer[i].name + " is warming up"; break; }
  if (!other.empty()) {
    const Ink ok{Face::Bold, 10.5f, IM_COL32(255, 228, 128, 255)};
    const std::vector<std::string> lines = wrap(ok, other, tw);
    float ly = y + th + px(8);
    for (const std::string& l : lines) { text_center(ok, cx, line_top(ok, ly, 15), l); ly += px(15); }
  }
}

void draw_not_in_room(const Bars& b, const View& v) {
  // Not in a room: the Arena card in the right bar. Pushing the hand into the right edge beside it
  // lights it up; A (or Z) then adds a friend's code, which starts a room.
  if (g.page != Page::None) return;
  const bool open = g.side == 2;
  const float tw = b.rx1 - b.rx0 - px(16), th = px(58);   // the WARM UP box's twin across the select
  float x = b.rx1 - tw - px(8);
  const float y = b.y0 + (b.y1 - b.y0) * .42f;
  // A picture wider than 4:3 (widescreen): the hand stops at the 4:3 select's own edge, far short of
  // the screen's (2560x1440 report: "I can't put my hand there"). The card then sits right beside the
  // fingertip at the hand's limit, so the hand reaches it and pushes into it.
  const float pic_w = b.px1 - b.px0, pic_h = b.y1 - b.y0;
  if (pic_h > 1.0f && pic_w / pic_h > 1.40f) {
    const float unit = pic_h / 57.3f;   // the select's units at the hand's depth (walter_egg.cpp measured it)
    const float reach = (b.px0 + b.px1) * .5f + (kRightEdge + 5.0f) * unit + px(10);
    x = std::min(x, reach);
  }
  g_card_left = x;
  // Over the game picture with the native select's mark this frame: into the picture's own list,
  // so the coins and hands pass over the card as over the icons (a part past the picture's edge is
  // drawn over the window, spill_under). Elsewhere (in the bar beside a 4:3 picture in a wide
  // window, other scenes, the Static Recomp) over the window as before.
  struct EndUnder { ~EndUnder() { end_under(); } } end_under_at_return;
  const bool sharp_enough = g_under.efb_width <= 0.0f || g_under.efb_width >= 0.6f * (b.px1 - b.px0);
  if (g_under.wanted && sharp_enough && x < b.px1 && x + tw > b.px0) begin_under(b.px0, b.y0, b.px1, b.y1);
  if (!v.notice.empty()) {   // a room that said no or did not open: the reason, under the card (above it is Young Link)
    const Ink nk{Face::Bold, 11, kWhite};
    std::vector<std::string> lines = wrap(nk, v.notice, tw);
    float ny = y + th + px(10);
    dl()->AddRectFilled(ImVec2(x - px(4), ny - px(6)), ImVec2(x + tw + px(4), ny + lines.size() * px(16) + px(4)), IM_COL32(0, 0, 0, 190), px(5));
    for (const std::string& l : lines) { text(nk, x, line_top(nk, ny, 16), l); ny += px(16); }
  }
  steel(x, y, x + tw, y + th, open ? kGold : IM_COL32(154, 166, 192, 200));
  hot(x, y, y + th, Do::AddCode);   // so the hand lands on it
  // As the WARM UP box: its name centred, under it the way in, one face, the same inset top and bottom.
  const float cx = x + tw * .5f;
  const Ink name{Face::Bold, 15, kWhite};
  text_center(name, cx, line_top(name, y + px(11), 18), "ARENA");
  const float ry = y + th - px(11) - px(16);
  if (open) {
    Ink hk{Face::Body, 11.5f, IM_COL32(214, 220, 234, 255)};
    const std::string label = fit_name(hk, add_label(), tw - px(16) - px(7) - px(16));
    float at = cx - (px(16) + px(7) + text_w(hk, label)) * .5f;
    button_chip(at, ry, 'Z');
    text(hk, at + px(16) + px(7), line_top(hk, ry, 16), label);
  } else {
    Ink how{Face::Body, 11, IM_COL32(200, 208, 224, 255)};
    const std::string push = fit_name(how, "Push the hand here", tw - px(20));
    text_center(how, cx, line_top(how, ry, 16), push);
  }
}

// A panel folded narrow over the picture (the hand not in it): a box with the room's name and, when there is
// one, the room's short line (a failed invite), at the full text size, clear of the select's icons.
void draw_narrow(const Bars& b, const View& v, int side, float narrow, bool notice) {
  const float m = px(4), w = narrow - m * 2, pad = px(7), tw = w - pad * 2;
  const float x = side ? b.rx1 - m - w : b.lx0 + m, y = b.y0 + (b.y1 - b.y0) * .3f;
  Ink name{Face::Bold, 15, kWhite};
  Ink nk{Face::Body, 11, kWhite};
  const std::vector<std::string> lines = notice ? wrap(nk, v.notice, tw) : std::vector<std::string>{};
  const float h = px(11 + 18 + 11) + (lines.empty() ? 0.f : lines.size() * px(15) + px(4));
  steel(x, y, x + w, y + h, IM_COL32(154, 166, 192, 200));
  text_center(name, x + w * .5f, line_top(name, y + px(11), 18), fit_name(name, "ARENA", tw));
  float ly = y + px(11 + 18 + 4);
  for (const std::string& l : lines) { text(nk, x + pad, line_top(nk, ly, 15), fit_name(nk, l, tw)); ly += px(15); }
}

void draw_tabs(const Bars& b, const View& v, const float alpha[2]) {
  // Hidden: a slim red and blue edge where each panel waits; it slides out when the hand comes.
  const float tw = px(10), th = px(96), ty = b.y0 + (b.y1 - b.y0) * .3f;
  for (int side = 0; side < 2; ++side) {
    const float a = alpha[side];
    if (a <= 0.01f) continue;
    const float x = side ? b.rx1 - tw : b.lx0;
    const int start = dl()->VtxBuffer.Size;
    dl()->AddRectFilled(ImVec2(x, ty), ImVec2(x + tw, ty + th), kWhite, px(4));
    const ImU32 top = side ? IM_COL32(58, 86, 216, (int)(230 * a)) : IM_COL32(216, 52, 60, (int)(230 * a));
    const ImU32 bottom = side ? IM_COL32(30, 44, 110, (int)(230 * a)) : IM_COL32(110, 26, 30, (int)(230 * a));
    shade(start, ty, ty + th, top, bottom);
    (void)v;
  }
}

// Every string inside its own panel (the bar it starts in) and clear of every other string. Each
// problem is logged once per run; a count every two seconds.
void check_text(const Bars& b) {
  static std::set<std::string> said;
  static int frames = 0, problems = 0;
  const float tol = 1.0f;
  auto say = [&](const std::string& line) { if (said.insert(line).second) { ++problems; host::log("arena panel textcheck: %s", line.c_str()); } };
  char buf[512];
  for (size_t i = 0; i < g_text_boxes.size(); ++i) {
    const TextBox& t = g_text_boxes[i];
    const bool left = t.x0 < (b.lx1 + b.rx0) * 0.5f;
    // The Arena card beside a widescreen hand's reach is its own panel, reaching in from the bar.
    const float right0 = g_card_left > 0.0f ? std::min(b.rx0, g_card_left - px(4)) : b.rx0;
    const float px0 = left ? b.lx0 : right0, px1 = left ? b.lx1 : b.rx1;
    if (t.x0 < px0 - tol || t.x1 > px1 + tol || t.y0 < b.y0 - tol || t.y1 > b.y1 + tol) {
      std::snprintf(buf, sizeof buf, "look %d '%s' at %.0f..%.0f x %.0f..%.0f leaves the %s panel %.0f..%.0f x %.0f..%.0f",
                    (int)g.look, t.s.c_str(), t.x0, t.x1, t.y0, t.y1, left ? "left" : "right", px0, px1, b.y0, b.y1);
      say(buf);
    }
    for (size_t j = i + 1; j < g_text_boxes.size(); ++j) {
      const TextBox& u = g_text_boxes[j];
      if (u.s == t.s && std::abs(u.x0 - t.x0) < 0.5f && std::abs(u.y0 - t.y0) < 0.5f) continue;   // the same string again
      const float ox = std::min(t.x1, u.x1) - std::max(t.x0, u.x0), oy = std::min(t.y1, u.y1) - std::max(t.y0, u.y0);
      if (ox > tol && oy > tol) {
        std::snprintf(buf, sizeof buf, "look %d '%s' overlaps '%s' (%.0f x %.0f px)", (int)g.look, t.s.c_str(), u.s.c_str(), ox, oy);
        say(buf);
      }
    }
  }
  if (++frames % 120 == 0)
    host::log("arena panel textcheck: %zu strings this frame, %d problems so far (look %d, %.0fx%.0f bars %.0f)",
              g_text_boxes.size(), problems, (int)g.look, b.rx1, b.y1 - b.y0, b.lx1 - b.lx0);
}

}  // namespace

void set_fonts(const Fonts& fonts) { g_fonts = fonts; }

void set_under_hand(bool on, float efb_width) { g_under.wanted = on; g_under.efb_width = efb_width; }

bool under_hand_draw_data(ImDrawData* out, float x, float y, float w, float h, float target_w, float target_h) {
  if (!under_this_frame() || g_under.size.x <= 0 || g_under.size.y <= 0 || w <= 0 || h <= 0) return false;
  // The picture's window rectangle lands on (x, y, w, h) of the target. ImGui's renderers put
  // DisplayPos at the target's corner, so the corner is that far up and left of the picture.
  const ImVec2 k(w / g_under.size.x, h / g_under.size.y);
  out->Clear();
  out->Valid = true;
  out->AddDrawList(g_under.list);
  out->DisplayPos = ImVec2(g_under.pos.x - x / k.x, g_under.pos.y - y / k.y);
  out->DisplaySize = ImVec2(target_w / k.x, target_h / k.y);
  out->FramebufferScale = k;
  out->Textures = &ImGui::GetPlatformIO().Textures;   // the font atlas, should it change this frame
  return true;
}
int look() { load(); return (int)g.look; }
const char* look_name(int l) { return kLookNames[std::clamp(l, 0, kLookCount - 1)]; }
void set_look(int l) { load(); g.look = (Look)std::clamp(l, 0, kLookCount - 1); save(); }
bool tabs() { load(); return g.tabs; }
int button_mode() { load(); return g.button; }
const char* button_mode_name(int mode) {
  static const char* const names[kButtonModeCount] = {"Show briefly", "Always", "Never"};
  return names[std::clamp(mode, 0, kButtonModeCount - 1)];
}
void set_button_mode(int mode) { load(); g.button = std::clamp(mode, 0, kButtonModeCount - 1); save(); }
void set_tabs(bool on) { load(); g.tabs = on; save(); }
// The pages (rules, the code keyboard) take the controller. The hand in a panel keeps the stick and
// loses only the panel's buttons, and any of them still held from the panel until they are let go.
bool wants_capture() { return g.page != Page::None; }
uint16_t withheld_buttons() { return g.side != 0 && g.page == Page::None ? kPanelButtons : g.swallow; }

void frame(const Frame& f) {
  // On a YouGame row (session(): the app, or a YouGame row under the launcher) the room is the arena's own
  // YouGame room (yougame::arena_room), never Direct searches beside the game's own.
  if (!ar::rooms_enabled()) { leave_panel(); g.page = Page::None; return; }   // tests only: rooms off
  load();
  if (const int forced = ar::test_fixture_look(); forced >= 0) {   // tests only: the fixture's cycle
    g.look = (Look)forced;
    static int last = -1;
    static double since = 0;
    static bool shot = false;
    const int index = ar::test_fixture_index();
    if (index != last) { last = index; since = f.time; shot = false; }
    if (!shot && f.time - since > 0.6) {   // after the slide-in
      shot = true;
      gx::gx_capture_request_set(1);
      host::log("arena panel: fixture shot %s look %d", ar::test_fixture_state(index), forced);
    }
  }
  const View v = ar::view();
  g_warmup_view = slippi::native_practice::snapshot().warmup;
  const bool visible = f.on_online_menus && !(v.active && v.room.in_game && ar::role(v.room, v.self) == Role::Fighter);
  if (!visible) {
    g.was_visible = false;
    g.card_pop = 0;
    leave_panel();
    g.page = Page::None;
    g.prev = f.buttons;
    g_last_items.clear();
    return;
  }
  if (!g.was_visible) { g.was_visible = true; g.shown_at = f.time; g.pop[0] = g.pop[1] = 0; }
  // The bars beside the picture; a picture that fills the window gets panels over its edges.
  Bars b;
  float bar = f.game_x0;
  const bool overlay = bar < 90.0f;
  if (overlay) bar = std::min(f.display_w * 0.17f, 220.0f);
  // In a room those panels open over the picture. One the hand is not in stays narrow, inside the room
  // beside the select's outer icons (Dr. Mario and Falco on the left, Ganondorf and Young Link on the
  // right, their frames about 31 of the select's units from its middle), and shows only a box with the
  // room's name and its short line (draw_narrow): 1280x960 two-seat walk, 217 wide over all four icons.
  // With the hand in it a panel is its full width at full size: the hand is there, not on an icon.
  float narrow = bar;
  if (overlay && v.active) {
    const float unit = (f.game_y1 - f.game_y0) / 57.3f, mid = (f.game_x0 + f.game_x1) * .5f, icons = 31.2f * unit;
    narrow = std::min(bar, std::min(mid - icons, f.display_w - (mid + icons)) - 4.0f);
  }
  // Sized by the bar's width, but never past what the bar's height holds: every layout is made for
  // a bar about 540 sheet pixels high (16:9 and 16:10 windows); the wider bars of an ultrawide
  // window are no taller, and at their width's scale the lists, the ask prompt and the foot ran
  // into each other (2560x1080: 360 sheet pixels high).
  g_k = std::clamp(std::min(bar / 152.0f, (f.game_y1 - f.game_y0) / 540.0f), 0.6f, 3.0f);
  g_mid = f.display_w * .5f;
  g_bar_y0 = f.game_y0; g_bar_y1 = f.game_y1;
  b.lx0 = 0; b.lx1 = bar;
  b.rx0 = f.display_w - bar; b.rx1 = f.display_w;
  b.y0 = f.game_y0; b.y1 = f.game_y1;
  b.px0 = f.game_x0; b.px1 = f.game_x1;
  g_card_left = 0;
  handle_input(f, v);
  g.focus = g.side != 0 || g.page != Page::None;
  // Hidden: each panel slides out when the hand comes near its side (or is in it), and back in a
  // moment after it leaves; the mouse over an edge does the same.
  const double dt = std::clamp(f.time - g.last_time, 0.0, 0.1);
  g.last_time = f.time;
  float hand_x = 0, hand_y = 0;
  const bool have_hand = ar::css_hand(&hand_x, &hand_y);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  static const bool test_open = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_FIXTURE"); return v && std::strstr(v, ":open"); }();
  // A short line from the room (a failed invite) opens the panel that shows it: the right in the Card and
  // Ring looks, else the left (over the picture, its narrow box unless the hand is in it).
  const int notice_side = v.active && !v.notice.empty() ? (g.look == Look::Card || g.look == Look::Ring ? 1 : 0) : -1;
  for (int s = 0; s < 2; ++s) {
    // Over the picture (no bars) only the hand pushed into the edge opens a panel: near the edge the
    // hand is still on the outer icons, which an opening panel would cover.
    const bool notice_here = notice_side == s;
    const bool near = g.side == s + 1 || (g.page != Page::None && s == 1) || test_open || notice_here ||
                      (!overlay && have_hand && (s == 0 ? hand_x <= -30.0f : hand_x >= 21.0f)) ||
                      (s == 0 ? mouse.x >= 0 && mouse.x < bar : mouse.x > f.display_w - bar && mouse.x <= f.display_w);
    if (near) g.pop_seen[s] = f.time;
    const bool out = f.time - g.pop_seen[s] < 0.6;
    g.pop[s] = std::clamp(g.pop[s] + (float)(dt / 0.28) * (out ? 1.f : -1.f), 0.f, 1.f);
  }
  // Slide in from the sides as the character select comes back: ease out over a third of a second.
  auto ease = [](double t) { t = std::clamp(t, 0.0, 1.0); return (float)(1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t)); };
  const float enter = ease((f.time - g.shown_at) / 0.35);
  float shown[2] = {enter, enter};
  if (!v.active) {
    if (g.first_shown < 0) g.first_shown = f.time;
    const bool near_right = g.side == 2 || g.page != Page::None || test_open_card() || !v.notice.empty() ||
                            (have_hand && hand_x >= 15.0f) || (mouse.x > f.display_w - bar && mouse.x <= f.display_w);
    const bool want = g.button == 1 || (g.button == 0 && (f.time - g.first_shown < 10.0 || near_right));
    g.card_pop = std::clamp(g.card_pop + (float)(dt / 0.35) * (want ? 1.f : -1.f), 0.f, 1.f);
    shown[1] = std::min(shown[1], ease(g.card_pop));
    if (g.button == 2 || shown[1] < 1e-3f) shown[1] = 0.f;
  }
  const bool folded = (g.tabs || overlay) && v.active;   // no bars: always folded into the tabs
  if (folded) {
    for (int s = 0; s < 2; ++s) shown[s] = std::min(shown[s], ease(g.pop[s]) < 1e-3f ? 0.f : ease(g.pop[s]));
    const float tab_alpha[2] = {1.f - ease(g.pop[0]), 1.f - ease(g.pop[1])};
    draw_tabs(b, v, tab_alpha);
  }
  g_items.clear();
  g_text_boxes.clear();
  ImDrawList* draw = dl();
  const int start = draw->VtxBuffer.Size;
  // Wide: the hand in that panel (or the right's pages open), or the mouse in its narrow box and then
  // anywhere over the panel. Else, over the picture in a room, narrow.
  bool wide[2] = {true, true};
  static bool mouse_wide[2] = {false, false};
  for (int s = 0; s < 2; ++s) {
    const float reach = mouse_wide[s] ? bar : narrow;
    mouse_wide[s] = s == 0 ? mouse.x >= 0 && mouse.x < reach : mouse.x > f.display_w - reach && mouse.x <= f.display_w;
    if (overlay && v.active && narrow < bar - 0.5f)
      wide[s] = g.side == s + 1 || (s == 1 && g.page != Page::None) || test_open || mouse_wide[s];
  }
  if (overlay && v.active) {   // the panels' own ground over the picture, sliding with them
    if (shown[0] > 0.f && wide[0]) draw->AddRectFilled(ImVec2(b.lx0, b.y0), ImVec2(b.lx1, b.y1), IM_COL32(0, 0, 0, 200));
    if (shown[1] > 0.f && wide[1]) draw->AddRectFilled(ImVec2(b.rx0, b.y0), ImVec2(b.rx1, b.y1), IM_COL32(0, 0, 0, 200));
  }
  if (!v.active) {
    if (g.button != 2 && (shown[1] > 0.f || g.page != Page::None)) draw_not_in_room(b, v);
    if (g.page == Page::None) draw_warmup_card(b);
  } else if (!folded || shown[0] > 0.f || shown[1] > 0.f || overlay) {
    // Over the picture the layout is drawn even with both panels folded away (slid off below): its buttons
    // are what lets the hand push into a panel (hand_level).
    const int layout_from = draw->VtxBuffer.Size;
    switch (g.look) {
      case Look::Combined: draw_combined(b, v, false); break;
      case Look::Corners: draw_combined(b, v, true); break;
      case Look::Card: draw_card(b, v, false); break;
      case Look::Ring: draw_card(b, v, true); break;
      case Look::Split: draw_split(b, v); break;
    }
    // A narrow panel's full layout goes off the window (its buttons still count); its box takes its place.
    for (int i = layout_from; i < draw->VtxBuffer.Size; ++i) {
      ImDrawVert& vert = draw->VtxBuffer[i];
      const int s = vert.pos.x < g_mid ? 0 : 1;
      if (!wide[s]) vert.pos.x += s ? 100000.0f : -100000.0f;
    }
    for (int s = 0; s < 2; ++s)
      if (!wide[s] && shown[s] > 0.f) draw_narrow(b, v, s, narrow, notice_side == s);
  }
  if (g.page != Page::None) page_box(b.rx0 + px(8), b.y0 + px(10), b.rx1 - b.rx0 - px(16), v);
  // Move what was just drawn: the left half to the left, the right half to the right, each by how
  // far its panel is from fully in.
  // The Arena card beside a widescreen hand's reach slides the whole way out from there.
  const float right_span = g_card_left > 0.0f ? std::max(bar, f.display_w - g_card_left) : bar;
  const float shift[2] = {(1.0f - shown[0]) * (bar + px(10)), (1.0f - shown[1]) * (right_span + px(10))};
  auto slide = [&](ImDrawList* list, int from) {
    for (int i = from; i < list->VtxBuffer.Size; ++i) {
      ImDrawVert& vert = list->VtxBuffer[i];
      if (vert.pos.x < g_mid) vert.pos.x -= shift[0];
      else vert.pos.x += shift[1];
    }
  };
  if (shift[0] > 0.5f || shift[1] > 0.5f) {
    slide(draw, start);
    if (under_this_frame()) slide(g_under.list, 0);
  }
  spill_under(f.display_w, f.display_h);
  if (textcheck_on() && shown[0] >= 0.999f && shown[1] >= 0.999f) check_text(b);
  // The buttons drawn this frame are the ones the hand moves over next frame.
  g_last_items.swap(g_items);
  g_items.clear();
}

}  // namespace arena_panel
