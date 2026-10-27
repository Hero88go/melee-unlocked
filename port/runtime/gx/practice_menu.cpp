// The Practice menu in Training: one menu model, controller navigation, and four looks.
//   Main Menu bars: the game's own Main Menu bars (gold rims, the yellow lit bar and its cursor
//                   ring, grey title, description box) in the lower right of the picture.
//   Side rails:     settings in the left black bar, the search on the right (over the outer edges
//                   of the picture when True widescreen leaves no bars).
//   Slots:          character-select style slots across the bottom.
//   Strip:          a thin line always on top of the picture; the menu drops under it.
// Text is drawn with Melee's own font, decoded from the player's disc (main.dol data: 287 glyphs of
// 32x32 I4 at 0x8040CD40, their margins at 0x8040CB00, the Shift-JIS table at 0x8040C8C0, the
// glyph codes at 0x8040C680). Nothing of the game is stored here.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "practice_menu.h"

#include "host.h"
#include "practice_cpu.h"
#include "practice_ghosts.h"
#include "ghost_ai.h"
#include "native_practice.h"
#include "slippi_online.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace practice_menu {

const char* style_name(int style) {
  switch (style) {
    case kBars: return "Main Menu bars";
    case kRails: return "Side rails";
    case kSlots: return "Character select slots";
    case kStrip: return "HUD strip";
    default: return "Window (mouse and keyboard)";
  }
}

// ---------------------------------------------------------------------------------------------
// Melee's font
namespace {

struct Glyph { int16_t index = -1; uint8_t left = 0, right = 0; };
struct Font {
  bool tried = false, ok = false;
  ImGuiContext* context = nullptr;
  ImTextureData tex;
  Glyph glyph[128];
};
Font g_font;

// Shift-JIS of the full-width form the game stores for each printable ASCII character.
uint16_t sjis_for(char c) {
  if (c >= '0' && c <= '9') return (uint16_t)(0x824F + (c - '0'));
  if (c >= 'A' && c <= 'Z') return (uint16_t)(0x8260 + (c - 'A'));
  if (c >= 'a' && c <= 'z') return (uint16_t)(0x8281 + (c - 'a'));
  switch (c) {
    case ',': return 0x8143; case '.': return 0x8144; case ':': return 0x8146; case ';': return 0x8147;
    case '?': return 0x8148; case '!': return 0x8149; case '^': return 0x814F; case '_': return 0x8151;
    case '/': return 0x815E; case '~': return 0x8160; case '|': return 0x8162; case '\'': return 0x8166;
    case '"': return 0x8168; case '(': return 0x8169; case ')': return 0x816A; case '[': return 0x816D;
    case ']': return 0x816E; case '{': return 0x816F; case '}': return 0x8170; case '+': return 0x817B;
    case '-': return 0x817C; case '=': return 0x8181; case '<': return 0x8183; case '>': return 0x8184;
    case '$': return 0x8190; case '%': return 0x8193; case '#': return 0x8194; case '&': return 0x8195;
    case '*': return 0x8196; case '@': return 0x8197;
    default: return 0;
  }
}

void font_load() {
  g_font.tried = true;
  std::vector<uint8_t> codes(576), sjis(576), metrics(576), pixels(287 * 512);
  if (!host::dol_data_read(0x8040C680u, codes.data(), 576) || !host::dol_data_read(0x8040C8C0u, sjis.data(), 576) ||
      !host::dol_data_read(0x8040CB00u, metrics.data(), 576) ||
      !host::dol_data_read(0x8040CD40u, pixels.data(), (uint32_t)pixels.size())) {
    host::log("practice menu: the disc's font was not found; the PC font is used");
    return;
  }
  int mapped = 0;
  for (int c = 32; c < 127; ++c) {
    const uint16_t want = sjis_for((char)c);
    if (!want) continue;
    for (int i = 0; i < 288; ++i) {
      if (((sjis[i * 2] << 8) | sjis[i * 2 + 1]) != want) continue;
      const int t = ((codes[i * 2] << 8) | codes[i * 2 + 1]) - 0x2000;
      if (t >= 0 && t < 287) {
        g_font.glyph[c] = Glyph{(int16_t)t, metrics[t * 2], metrics[t * 2 + 1]};
        ++mapped;
      }
      break;
    }
  }
  if (g_font.glyph['A'].index < 0 || g_font.glyph['0'].index < 0) {
    host::log("practice menu: the disc's font has no Latin letters; the PC font is used");
    return;
  }
  // 32 glyphs a row, white with the glyph's intensity as alpha (I4: 8x8 tiles, high nibble first).
  g_font.tex.Create(ImTextureFormat_RGBA32, 1024, 288);
  uint8_t* out = (uint8_t*)g_font.tex.GetPixels();
  for (int g = 0; g < 287; ++g) {
    const uint8_t* d = pixels.data() + g * 512;
    const int ox = (g % 32) * 32, oy = (g / 32) * 32;
    for (int tile = 0; tile < 16; ++tile)
      for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
          const uint8_t b = d[tile * 32 + r * 4 + c / 2];
          const uint8_t v = (uint8_t)(((c & 1) ? (b & 15) : (b >> 4)) * 17);
          uint8_t* px = out + ((oy + (tile / 4) * 8 + r) * 1024 + ox + (tile % 4) * 8 + c) * 4;
          px[0] = px[1] = px[2] = 255;
          px[3] = v;
        }
  }
  g_font.tex.UsedRect = ImTextureRect{0, 0, 1024, 288};
  g_font.ok = true;
  host::log("practice menu: the game's font is ready (%d characters)", mapped);
}

bool font_ready() {
  if (!g_font.tried) font_load();
  if (!g_font.ok) return false;
  if (g_font.context != ImGui::GetCurrentContext()) {
    g_font.context = ImGui::GetCurrentContext();
    if (g_font.tex.Status == ImTextureStatus_Destroyed || g_font.tex.TexID == ImTextureID_Invalid)
      g_font.tex.Status = ImTextureStatus_WantCreate;
    ImGui::RegisterUserTexture(&g_font.tex);
  }
  return true;
}

// Space between letters, in glyph pixels: the game's tight fit made digits touch at menu sizes.
constexpr float kTracking = 2.5f;
// The game's % stands taller than its digits; it is drawn a little smaller, on the same baseline.
float glyph_scale(unsigned char ch) { return ch == '%' ? 0.8f : 1.0f; }

void glyph_run(ImDrawList* dl, float x, float y, float px, ImU32 col, const char* s, float skew) {
  const float k0 = px / 32.0f, lean0 = skew * 32.0f * k0;
  float pen = x;
  const ImTextureRef tex = g_font.tex.GetTexRef();
  for (const char* p = s; *p; ++p) {
    const unsigned char ch = (unsigned char)*p;
    if (ch == ' ' || ch >= 128 || g_font.glyph[ch].index < 0) { pen += 15.0f * k0; continue; }
    const Glyph& g = g_font.glyph[ch];
    const float k = k0 * glyph_scale(ch), h = 32.0f * k, lean = lean0 * glyph_scale(ch);
    const float top = y + 32.0f * k0 - h - (32.0f * k0 - h) * 0.22f;   // baseline about 0.78 down the cell
    // Whole pixels when upright: sharper digits.
    const float x0 = skew == 0.0f ? std::floor(pen - (float)(g.left - 1) * k + 0.5f) : pen - (float)(g.left - 1) * k;
    const float y0 = skew == 0.0f ? std::floor(top + 0.5f) : top;
    const float u0 = (float)((g.index % 32) * 32) / 1024.0f, v0 = (float)((g.index / 32) * 32) / 288.0f;
    const float u1 = u0 + 32.0f / 1024.0f, v1 = v0 + 32.0f / 288.0f;
    dl->AddImageQuad(tex, ImVec2(x0 + lean, y0), ImVec2(x0 + h + lean, y0), ImVec2(x0 + h, y0 + h), ImVec2(x0, y0 + h),
                     ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1), col);
    pen += (32.0f - (float)(g.left + g.right - 2) + kTracking) * k;
  }
}

ImU32 with_alpha(ImU32 c, float a) {
  const int alpha = (int)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
  return (c & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}

}  // namespace

float text_width(float px, const char* s) {
  if (!font_ready()) return ImGui::GetFont()->CalcTextSizeA(px * 0.82f, FLT_MAX, 0.0f, s).x;
  const float k = px / 32.0f;
  float w = 0.0f;
  for (const char* p = s; *p; ++p) {
    const unsigned char ch = (unsigned char)*p;
    if (ch == ' ' || ch >= 128 || g_font.glyph[ch].index < 0) { w += 15.0f * k; continue; }
    w += (32.0f - (float)(g_font.glyph[ch].left + g_font.glyph[ch].right - 2) + kTracking) * k * glyph_scale(ch);
  }
  return w;
}

void text(ImDrawList* dl, float x, float y, float px, uint32_t color, const char* s, float skew, bool shadow) {
  const float a = (float)((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
  if (!font_ready()) {
    if (shadow) dl->AddText(nullptr, px * 0.82f, ImVec2(x + px * 0.06f, y + px * 0.16f), IM_COL32(0, 0, 0, (int)(200 * a)), s);
    dl->AddText(nullptr, px * 0.82f, ImVec2(x, y + px * 0.1f), color, s);
    return;
  }
  if (shadow) {
    // A hard dark drop, as the game's menu text has; the leaning gold labels also get a thin edge.
    const float o = std::max(1.0f, std::floor(px * 0.07f + 0.5f)), t = std::max(1.0f, px * 0.035f);
    const ImU32 edge = IM_COL32(0, 0, 0, (int)(130 * a)), drop = IM_COL32(0, 0, 0, (int)(200 * a));
    if (skew != 0.0f) {
      glyph_run(dl, x - t, y, px, edge, s, skew);
      glyph_run(dl, x + t, y, px, edge, s, skew);
      glyph_run(dl, x, y - t, px, edge, s, skew);
      glyph_run(dl, x, y + t, px, edge, s, skew);
    }
    glyph_run(dl, x + o, y + o, px, drop, s, skew);
  }
  glyph_run(dl, x, y, px, color, s, skew);
}

// ---------------------------------------------------------------------------------------------
// The menu model
namespace {

const char* const kMoves[MU_PC_MOVE_COUNT] = {"Stand", "Walk", "Dash dance", "Approach", "Run away", "Short hop",
                                              "Full hop", "Wavedash in", "Wavedash out", "Crouch"};
const char* const kAttacks[MU_PC_ATK_COUNT] = {"Jab", "Forward tilt", "Up tilt", "Down tilt", "Forward smash",
                                               "Up smash", "Down smash", "Dash attack", "Grab", "Boost grab",
                                               "Neutral air", "Forward air", "Back air", "Up air", "Down air",
                                               "Neutral B", "Side B", "Up B", "Down B", "Wavedash ftilt"};
const char* const kDefend[MU_PC_DEF_COUNT] = {"Nothing", "Shield", "Spot dodge", "Roll in", "Roll away", "Jump away"};
const char* const kOos[MU_PC_OOS_COUNT] = {"Hold shield", "Drop shield", "Jump", "Neutral air", "Up B",
                                           "Shine", "Grab", "Wavedash", "Roll", "Spot dodge"};
const char* const kDi[MU_PC_DI_COUNT] = {"No DI", "In", "Out", "Survival", "Down and in", "Down and out"};
const char* const kTech[MU_PC_TECH_COUNT] = {"In place", "Roll in", "Roll away", "Miss"};
const char* const kGetup[MU_PC_GETUP_COUNT] = {"Stand", "Roll in", "Roll away", "Getup attack", "Stay down"};
const char* const kLedge[MU_PC_LEDGE_COUNT] = {"Getup", "Roll", "Jump", "Attack", "Drop and jump", "Ledgedash"};
const char* const kAfter[MU_PC_AFTER_COUNT] = {"Nothing", "Jump", "Air dodge", "Neutral air", "Up B"};
const char* const kRecovery[MU_PC_RECOVER_COUNT] = {"Game's own", "High", "Low", "Mix"};
const char* const kSdi[] = {"None", "Light", "Medium", "Heavy"};
const char* const kPresetAbout[] = {
    "Moves, attacks and shields. All-round.",   "Stands still; DIs and techs. Combos.",
    "Short hop nairs and bairs, L-cancelled.",  "Runs and hops away. Practice chasing.",
    "Wavedash forward tilt, boost grabs.",      "Shields, then punishes out of shield.",
    "DIs down, mixes every tech and getup.",    "Mixes every option from the ledge.",
};

enum class Page : int {
  Root, Queue, Code, Blocked, Cpu, Moves, Attacks, Defense, Oos, Hit, Di, Tech, Getup, After, Ledge, Tools,
  Presets, Setups, Replay, ReplaySettings, Ghosts, Count
};
// The character select's number for a character name (a ghost's character), or -1.
int css_kind_of(const char* name) {
  static const char* const kNames[] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser",
      "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Jigglypuff",
      "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
  for (int i = 0; i < (int)(sizeof kNames / sizeof kNames[0]); ++i)
    if (name && std::strcmp(name, kNames[i]) == 0) return i;
  return -1;
}

const char* page_title(Page p) {
  switch (p) {
    case Page::Root: return "Practice";
    case Page::Queue: return "Search";
    case Page::Code: return "Direct code";
    case Page::Blocked: return "Skip in Unranked";
    case Page::Cpu: return "CPU";
    case Page::Moves: return "Movement";
    case Page::Attacks: return "Attacks";
    case Page::Defense: return "Defense";
    case Page::Oos: return "Out of shield";
    case Page::Hit: return "Hit reactions";
    case Page::Di: return "DI";
    case Page::Tech: return "Tech";
    case Page::Getup: return "Missed tech";
    case Page::After: return "Leaving hitstun";
    case Page::Ledge: return "Ledge";
    case Page::Tools: return "Tools";
    case Page::Presets: return "Presets";
    case Page::Setups: return "Your setups";
    case Page::Replay: return "Replay";
    case Page::ReplaySettings: return "Settings";
    case Page::Ghosts: return "Fight a pro";
    default: return "";
  }
}
// Pages whose rows change the CPU's behavior: a change there turns the Practice CPU on and leaves
// any preset.
bool cpu_page(Page p) {
  return p == Page::Cpu || p == Page::Moves || p == Page::Attacks || p == Page::Defense || p == Page::Oos ||
         p == Page::Hit || p == Page::Di || p == Page::Tech || p == Page::Getup || p == Page::After || p == Page::Ledge;
}

enum class Kind { Page, Toggle, Percent, Number, Choice, Odds, Flag, Action, Info };
enum Action : int {
  kActNone, kActUnranked, kActCancel, kActWindow, kActDefaults, kActPreset = 100, kActLoad = 200, kActSave = 300, kActGhost = 400,
  kActBlocked = 400, kActTeams = 500
};

struct Row {
  Kind kind = Kind::Info;
  std::string label, about;
  Page target = Page::Root;
  uint8_t* byte = nullptr;                // the setting (Toggle, Percent, Number, Choice, Odds, Flag)
  const uint8_t* group = nullptr;         // Odds: the whole group, for the share
  int group_count = 0;
  int low = 0, high = 100, step = 1;
  const char* format = "%d";
  const char* const* names = nullptr;     // Choice
  int name_count = 0;
  uint8_t mask = 0;                       // Flag
  int action = kActNone;
  bool enabled = true;
  std::string value_override;             // Page and Info rows: the summary on the right
};

MuPcConfig g_c;              // the settings, copied in at the start of each frame
bool g_c_changed = false;
Frame g_f;                   // this frame's inputs
std::string g_error;
double g_error_until = 0.0;

struct Level { Page page = Page::Root; int cursor = 0; int top = 0; };
std::vector<Level> g_stack{Level{}};
double g_page_changed_at = -10.0;

// The on-screen keyboard (Direct codes and the codes skipped in Unranked).
const char kKeys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#";
constexpr int kKeyCols = 13;
constexpr int kKeyCount = (int)sizeof(kKeys) - 1;   // 37 characters, then Delete / Paste / OK
int g_kb_target = 0;          // 0 Direct, 1..3 a skipped code, 4 Teams
std::string g_kb_text;
int g_kb_cursor = 0;          // 0..36 characters, 37 Delete, 38 Paste, 39 OK
std::string g_last_direct, g_last_teams;

int share_of(const uint8_t* group, int count, int index) {
  int total = 0;
  for (int i = 0; i < count; ++i) total += group[i];
  return total > 0 ? (group[index] * 100 + total / 2) / total : 0;
}
// The option a group picks most, with its share ("Shield 40%").
std::string top_of(const char* const* names, const uint8_t* group, int count) {
  int best = -1;
  for (int i = 0; i < count; ++i) if (group[i] && (best < 0 || group[i] > group[best])) best = i;
  if (best < 0) return "Never";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s %d%%", names[best], share_of(group, count, best));
  return buf;
}

// The option a group picks most, name only (the top level's short summaries).
std::string top_name(const char* const* names, const uint8_t* group, int count) {
  int best = -1;
  for (int i = 0; i < count; ++i) if (group[i] && (best < 0 || group[i] > group[best])) best = i;
  return best < 0 ? std::string("Never") : std::string(names[best]);
}
// What the CPU is doing now, capitalized ("Wavedash").
std::string plan_text(int plan) {
  std::string s = mu_pc_plan_name(plan);
  if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
  return s;
}
// Shortened with "..." to fit `width` in the game's font.
std::string fit(const std::string& s, float px, float width) {
  if (text_width(px, s.c_str()) <= width) return s;
  std::string t = s;
  while (!t.empty() && text_width(px, (t + "...").c_str()) > width) t.pop_back();
  while (!t.empty() && t.back() == ' ') t.pop_back();
  return t + "...";
}

Row page_row(const char* label, Page target, std::string value, const char* about) {
  Row r; r.kind = Kind::Page; r.label = label; r.target = target; r.value_override = std::move(value); r.about = about;
  return r;
}
Row odds_row(const char* label, uint8_t* group, int count, int index) {
  Row r; r.kind = Kind::Odds; r.label = label; r.byte = &group[index]; r.group = group; r.group_count = count;
  r.low = 0; r.high = 100; r.step = 5;
  const int share = share_of(group, count, index);
  char buf[96];
  if (!group[index]) std::snprintf(buf, sizeof buf, "%s: never.", label);
  else if (share >= 100) std::snprintf(buf, sizeof buf, "%s every time.", label);
  else {
    const int n = std::max(1, (share + 5) / 10);
    std::snprintf(buf, sizeof buf, n == 1 ? "%s 1 time in 10." : "%s %d times in 10.", label, n);
  }
  r.about = buf;
  return r;
}
Row percent_row(const char* label, uint8_t* v, const char* about) {
  Row r; r.kind = Kind::Percent; r.label = label; r.byte = v; r.step = 5; r.format = "%d%%"; r.about = about;
  return r;
}
Row number_row(const char* label, uint8_t* v, int low, int high, int step, const char* format, const char* about) {
  Row r; r.kind = Kind::Number; r.label = label; r.byte = v; r.low = low; r.high = high; r.step = step; r.format = format;
  r.about = about;
  return r;
}
Row toggle_row(const char* label, uint8_t* v, const char* about) {
  Row r; r.kind = Kind::Toggle; r.label = label; r.byte = v; r.about = about;
  return r;
}
Row choice_row(const char* label, uint8_t* v, const char* const* names, int count, const char* about) {
  Row r; r.kind = Kind::Choice; r.label = label; r.byte = v; r.names = names; r.name_count = count; r.about = about;
  return r;
}
Row action_row(const char* label, int action, std::string value, const char* about, bool enabled = true) {
  Row r; r.kind = Kind::Action; r.label = label; r.action = action; r.value_override = std::move(value); r.about = about;
  r.enabled = enabled;
  return r;
}

std::string value_of(const Row& r) {
  char buf[64];
  switch (r.kind) {
    case Kind::Toggle: return *r.byte ? "On" : "Off";
    case Kind::Flag: return (*r.byte & r.mask) ? "On" : "Off";
    case Kind::Percent:
    case Kind::Number: std::snprintf(buf, sizeof buf, r.format, (int)*r.byte); return buf;
    case Kind::Choice: return (*r.byte < r.name_count) ? r.names[*r.byte] : "";
    case Kind::Odds: {
      if (!*r.byte) return "Never";
      std::snprintf(buf, sizeof buf, "%d%%", share_of(r.group, r.group_count, (int)(r.byte - r.group)));
      return buf;
    }
    default: return r.value_override;
  }
}

std::vector<Row> rows_for(Page p) {
  std::vector<Row> rows;
  MuPcConfig& c = g_c;
  const Search& s = g_f.search;
  switch (p) {
    case Page::Root: {
      std::string q = s.searching ? s.mode + " " + s.elapsed : (s.available ? "Off" : "Training only");
      rows.push_back(page_row("Search", Page::Queue, q, s.searching ? "Searching. Keep training; the match starts when one is found."
                                                                    : "Find a match while you train."));
      const practice_cpu::Status st = practice_cpu::status();
      std::string cpu = !c.enabled ? "Off" : (st.driving ? plan_text(st.plan) : "On");
      rows.push_back(page_row("Fight a pro", Page::Ghosts, "", "A pro's habits from their own games, on their own character. Pick one and go."));
      rows.push_back(page_row("CPU", Page::Cpu, cpu, "How the Training CPU moves and attacks."));
      rows.push_back(page_row("Defense", Page::Defense, top_name(kDefend, c.defend_odds, MU_PC_DEF_COUNT),
                              "What it does when you attack near it."));
      rows.push_back(page_row("Hit reactions", Page::Hit, "Tech " + top_name(kTech, c.tech_odds, MU_PC_TECH_COUNT),
                              "DI, techs and getups."));
      rows.push_back(page_row("Ledge", Page::Ledge, top_name(kLedge, c.ledge_odds, MU_PC_LEDGE_COUNT),
                              "Ledge options and recovery."));
      int tools = 0;
      for (uint8_t m : {MU_PC_TOOL_DPAD, MU_PC_TOOL_HITBOXES, MU_PC_TOOL_HITBOXES_ONLY, MU_PC_TOOL_OVERLAY, MU_PC_TOOL_LOOP, MU_PC_TOOL_FRAME_ADVANCE})
        tools += (c.tools & m) ? 1 : 0;
      char t[24]; std::snprintf(t, sizeof t, tools ? "%d on" : "Off", tools);
      rows.push_back(page_row("Tools", Page::Tools, t, "Save states, hitboxes, inputs."));
      std::string preset = c.preset >= 1 && c.preset <= mu_pc_preset_count() ? mu_pc_preset_name(c.preset - 1) : "Your own";
      if ((c.preset & 0x80) && c.preset != MU_PC_PRESET_AI && (c.preset & 0x7F) < (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0]))
        preset = std::string(practice_ghosts::kGhosts[c.preset & 0x7F].name) + " AI";
      rows.push_back(page_row("Presets", Page::Presets, preset, "Ready-made CPUs and your own setups."));
      rows.push_back(action_row("All settings", kActWindow, "", "The full Practice window, for the mouse and keyboard."));
      break;
    }
    case Page::Queue:
      if (s.searching) {
        Row r; r.kind = Kind::Info; r.label = s.mode.empty() ? "Searching" : s.mode; r.value_override = s.elapsed;
        r.about = !s.opponent.empty() ? "Found " + s.opponent + "." : s.state;
        rows.push_back(r);
        rows.push_back(action_row("Stop searching", kActCancel, "", "Stop the search and keep training."));
      } else {
        rows.push_back(action_row("Unranked", kActUnranked, "", s.can_start ? "Search now. Keep training; the match starts by itself."
                                                                           : "Search from Training or an event match.", s.can_start));
        rows.push_back(page_row("Direct", Page::Code, g_last_direct, "Play someone by their connect code."));
        rows.push_back(action_row("Teams", kActTeams, g_last_teams, "Two against two: all four enter the same connect code."));
        int n = 0;
        for (const std::string& code : slippi::online::config().blocked_codes) n += code.empty() ? 0 : 1;
        char b[24]; std::snprintf(b, sizeof b, n ? "%d codes" : "None", n);
        rows.push_back(page_row("Skip in Unranked", Page::Blocked, b, "Codes you do not want to play in Unranked."));
      }
      break;
    case Page::Blocked:
      for (int i = 0; i < 3; ++i) {
        const std::string& code = slippi::online::config().blocked_codes[i];
        char label[24]; std::snprintf(label, sizeof label, "Code %d", i + 1);
        rows.push_back(action_row(label, kActBlocked + i, code.empty() ? "Empty" : code,
                                  "A: type it. Unranked turns such a match down and keeps searching."));
      }
      break;
    case Page::Cpu:
      rows.push_back(toggle_row("Practice CPU", &c.enabled, "On: the Training CPU follows these pages. Never online."));
      rows.push_back(toggle_row("Only react", &c.freeze, "It stands still and only DIs, techs and recovers."));
      rows.push_back(page_row("Movement", Page::Moves, top_of(kMoves, c.move_odds, MU_PC_MOVE_COUNT), "What it does between attacks."));
      {
        const practice_cpu::Status st = practice_cpu::status();
        const char* names[MU_PC_ATK_COUNT];
        for (int i = 0; i < MU_PC_ATK_COUNT; ++i) names[i] = kAttacks[i];
        static std::string special[4];
        for (int k = 0; k < 4; ++k)
          if (const char* name = practice_cpu::special_name(st.kind, k)) { special[k] = name; names[MU_PC_ATK_NEUTRAL_B + k] = special[k].c_str(); }
        rows.push_back(page_row("Attacks", Page::Attacks, top_of(names, c.attack_odds, MU_PC_ATK_COUNT), "Picked when you are in range."));
      }
      rows.push_back(percent_row("Aggression", &c.aggression, "How often it attacks rather than moves when you are in range."));
      rows.push_back(number_row("Attack range", &c.attack_range, 10, 80, 5, "%d", "How close you must be before it attacks."));
      rows.push_back(number_row("Reaction", &c.reaction_frames, 0, 40, 1, "%df", "Frames before it answers. People take 15 to 25."));
      rows.push_back(percent_row("Full hop", &c.full_hop_aerials, "Chance an aerial is full hopped, not short hopped."));
      rows.push_back(percent_row("Fast fall", &c.fast_fall, "Chance it fast falls its aerials."));
      rows.push_back(percent_row("L-cancel", &c.l_cancel, "Chance it L-cancels."));
      break;
    case Page::Moves:
      for (int i = 0; i < MU_PC_MOVE_COUNT; ++i) rows.push_back(odds_row(kMoves[i], c.move_odds, MU_PC_MOVE_COUNT, i));
      break;
    case Page::Attacks: {
      const practice_cpu::Status st = practice_cpu::status();
      static std::string labels[MU_PC_ATK_COUNT];
      for (int i = 0; i < MU_PC_ATK_COUNT; ++i) {
        labels[i] = kAttacks[i];
        if (i >= MU_PC_ATK_NEUTRAL_B && i < MU_PC_ATK_NEUTRAL_B + 4)
          if (const char* name = practice_cpu::special_name(st.kind, i - MU_PC_ATK_NEUTRAL_B)) labels[i] = name;
        rows.push_back(odds_row(labels[i].c_str(), c.attack_odds, MU_PC_ATK_COUNT, i));
      }
      break;
    }
    case Page::Defense:
      for (int i = 0; i < MU_PC_DEF_COUNT; ++i) rows.push_back(odds_row(kDefend[i], c.defend_odds, MU_PC_DEF_COUNT, i));
      rows.push_back(page_row("Out of shield", Page::Oos, top_of(kOos, c.oos_odds, MU_PC_OOS_COUNT), "Once shieldstun ends."));
      break;
    case Page::Oos:
      for (int i = 0; i < MU_PC_OOS_COUNT; ++i) rows.push_back(odds_row(kOos[i], c.oos_odds, MU_PC_OOS_COUNT, i));
      break;
    case Page::Hit:
      rows.push_back(page_row("DI", Page::Di, top_of(kDi, c.di_odds, MU_PC_DI_COUNT), "Picked for each hit. In and out are toward and away from you."));
      rows.push_back(choice_row("Smash DI", &c.sdi_amount, kSdi, 4, "How hard it smash DIs multi-hit moves."));
      rows.push_back(page_row("Tech", Page::Tech, top_of(kTech, c.tech_odds, MU_PC_TECH_COUNT), "When it lands in tumble."));
      rows.push_back(page_row("Missed tech", Page::Getup, top_of(kGetup, c.getup_odds, MU_PC_GETUP_COUNT), "After a missed tech."));
      rows.push_back(page_row("Leaving hitstun", Page::After, top_of(kAfter, c.after_odds, MU_PC_AFTER_COUNT), "As hitstun ends in the air."));
      break;
    case Page::Di:
      for (int i = 0; i < MU_PC_DI_COUNT; ++i) rows.push_back(odds_row(kDi[i], c.di_odds, MU_PC_DI_COUNT, i));
      break;
    case Page::Tech:
      for (int i = 0; i < MU_PC_TECH_COUNT; ++i) rows.push_back(odds_row(kTech[i], c.tech_odds, MU_PC_TECH_COUNT, i));
      break;
    case Page::Getup:
      for (int i = 0; i < MU_PC_GETUP_COUNT; ++i) rows.push_back(odds_row(kGetup[i], c.getup_odds, MU_PC_GETUP_COUNT, i));
      break;
    case Page::After:
      for (int i = 0; i < MU_PC_AFTER_COUNT; ++i) rows.push_back(odds_row(kAfter[i], c.after_odds, MU_PC_AFTER_COUNT, i));
      break;
    case Page::Ledge:
      for (int i = 0; i < MU_PC_LEDGE_COUNT; ++i) rows.push_back(odds_row(kLedge[i], c.ledge_odds, MU_PC_LEDGE_COUNT, i));
      rows.push_back(number_row("Ledge wait", &c.ledge_delay, 0, 120, 5, "%df", "The most frames it waits on the ledge."));
      rows.push_back(choice_row("Recovery", &c.recovery, kRecovery, MU_PC_RECOVER_COUNT, "Game's own uses the CPU level set in Training."));
      break;
    case Page::Tools: {
      const bool on = g_f.native_source;
      const char* only = "These run on the Source Port.";
      struct T { const char* label; uint8_t mask; const char* about; };
      const T tools[] = {
          {"D-pad states", MU_PC_TOOL_DPAD, "Right keeps this moment, left returns. Down records you as the CPU, up plays it."},
          {"Loop recording", MU_PC_TOOL_LOOP, "A played back recording starts again from the kept moment."},
          {"Hitboxes", MU_PC_TOOL_HITBOXES, "Hitboxes and hurtboxes over the fighters."},
          {"Hitboxes only", MU_PC_TOOL_HITBOXES_ONLY, "Hitboxes without the models."},
          {"Inputs", MU_PC_TOOL_OVERLAY, "Both players' inputs and your frame advantage."},
      };
      {
        Row r; r.kind = Kind::Flag; r.label = "Frame advance"; r.byte = &c.tools; r.mask = MU_PC_TOOL_FRAME_ADVANCE;
        r.about = "In Training, L + R + Z freezes the game. Z steps a frame; the D-pad moves the CPU.";
        rows.push_back(r);
      }
      for (const T& t : tools) {
        Row r; r.kind = Kind::Flag; r.label = t.label; r.byte = &c.tools; r.mask = t.mask; r.about = on ? t.about : only; r.enabled = on;
        rows.push_back(r);
      }
      break;
    }
    case Page::Ghosts:
      for (int i = 0; i < (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0]); ++i) {
        const practice_ghosts::Ghost& gh = practice_ghosts::kGhosts[i];
        char about[220], games[16];
        if (gh.games >= 1000) std::snprintf(games, sizeof games, "%d,%03d", gh.games / 1000, gh.games % 1000);
        else std::snprintf(games, sizeof games, "%d", gh.games);
        if (ghost_ai::available())
          std::snprintf(about, sizeof about,
                        "An AI that learned from %s's own %s games, frame by frame, and plays the way they do. "
                        "The CPU becomes %s and Training starts again.",
                        gh.name, gh.character, gh.character);
        else
          std::snprintf(about, sizeof about,
                        "%s's habits from %s of their games: attacks, movement, shielding, techs, getups, ledge "
                        "options. The CPU becomes %s and Training starts again on this stage.", gh.name, games, gh.character);
        rows.push_back(action_row(gh.name, kActGhost + i, gh.character, about));
      }
      break;
    case Page::Presets:
      for (int i = 0; i < mu_pc_preset_count(); ++i)
        rows.push_back(action_row(mu_pc_preset_name(i), kActPreset + i, c.preset == i + 1 ? "In use" : "",
                                  kPresetAbout[i % (int)(sizeof kPresetAbout / sizeof kPresetAbout[0])]));
      for (int i = 0; i < (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0]); ++i) {
        // Ghosts: the odds measured from a player's own games (port/runtime/hle/practice_ghosts.h).
        const practice_ghosts::Ghost& gh = practice_ghosts::kGhosts[i];
        char about[200];
        std::snprintf(about, sizeof about,
                      "%s's habits from %d of their %s games: attacks, specials, movement, shielding, techs, "
                      "getups, ledge options, L-cancels. Pick %s for the CPU.", gh.name, gh.games, gh.character, gh.character);
        rows.push_back(action_row((std::string(gh.name) + " ghost").c_str(), kActGhost + i, gh.character, about));
      }
      rows.push_back(page_row("Your setups", Page::Setups, "", "Save what is set now; load it any time."));
      rows.push_back(action_row("Defaults", kActDefaults, "", "Every CPU setting back to the start."));
      break;
    case Page::Setups:
      for (int i = 0; i < practice_cpu::kSlots; ++i) {
        MuPcConfig saved;
        const bool have = practice_cpu::slot(i, saved);
        char l[24], s2[24];
        std::snprintf(l, sizeof l, "Load setup %d", i + 1);
        std::snprintf(s2, sizeof s2, "Save setup %d", i + 1);
        rows.push_back(action_row(l, kActLoad + i, have ? "" : "Empty", "Everything as you saved it.", have));
        rows.push_back(action_row(s2, kActSave + i, "", "Keep everything set now in this slot."));
      }
      break;
    default: break;
  }
  return rows;
}

// ---------------------------------------------------------------------------------------------
// Input: edges, and repeat for held directions.
struct Input { bool up = false, down = false, left = false, right = false, a = false, b = false, start = false, l = false, r = false, x = false, y = false; };
uint16_t g_prev_buttons = 0xFFFF;
int g_held_dir = -1;
double g_held_since = 0.0, g_held_last = 0.0;

Input read_input(const Frame& f) {
  Input in;
  uint16_t b = f.armed ? f.buttons : 0xFFFF;
  if (f.stick_y > 52) b |= 0x0008;
  if (f.stick_y < -52) b |= 0x0004;
  if (f.stick_x < -52) b |= 0x0001;
  if (f.stick_x > 52) b |= 0x0002;
  if (!f.armed) { g_prev_buttons = 0xFFFF; return in; }
  const uint16_t pressed = (uint16_t)(b & ~g_prev_buttons);
  g_prev_buttons = b;
  in.a = pressed & 0x0100; in.b = pressed & 0x0200; in.x = pressed & 0x0400; in.y = pressed & 0x0800;
  in.start = pressed & 0x1000; in.l = pressed & 0x0040; in.r = pressed & 0x0020;
  // Directions: first press, then after 0.32 s every 0.065 s while held.
  int dir = -1;
  if (b & 0x0008) dir = 0; else if (b & 0x0004) dir = 1; else if (b & 0x0001) dir = 2; else if (b & 0x0002) dir = 3;
  bool fire = false;
  if (dir != g_held_dir) { g_held_dir = dir; g_held_since = g_held_last = f.time; fire = dir >= 0; }
  else if (dir >= 0 && f.time - g_held_since > 0.32 && f.time - g_held_last > 0.065) { g_held_last = f.time; fire = true; }
  if (fire) { in.up = dir == 0; in.down = dir == 1; in.left = dir == 2; in.right = dir == 3; }
  if (f.keyboard && !ImGui::GetIO().WantTextInput) {
    in.up |= ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
    in.down |= ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
    in.left |= ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
    in.right |= ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
    in.a |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    in.b |= ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  }
  return in;
}

Level& level() { return g_stack.back(); }
void push(Page p) { g_stack.push_back(Level{p, 0, 0}); g_page_changed_at = g_f.time; }
void pop() { if (g_stack.size() > 1) { g_stack.pop_back(); g_page_changed_at = g_f.time; } }

void apply_preset(int i) {
  const uint8_t tools = g_c.tools;
  mu_pc_preset(i, &g_c);
  g_c.tools = tools;
  g_c.enabled = 1;
  g_c.preset = (uint8_t)(i + 1);
  g_c_changed = true;
}

void change(Page p, Row& r, int dir) {
  if (!r.enabled || !r.byte) return;
  uint8_t before = *r.byte;
  switch (r.kind) {
    case Kind::Toggle: *r.byte = *r.byte ? 0 : 1; break;
    case Kind::Flag: *r.byte ^= r.mask; break;
    case Kind::Choice: *r.byte = (uint8_t)((*r.byte + r.name_count + dir) % r.name_count); break;
    case Kind::Percent: case Kind::Number: case Kind::Odds: {
      const int v = std::clamp((int)*r.byte + dir * r.step, r.low, r.high);
      *r.byte = (uint8_t)v;
      break;
    }
    default: return;
  }
  if (*r.byte == before) return;
  g_c_changed = true;
  if (cpu_page(p)) {
    g_c.preset = 0;
    if (!(p == Page::Cpu && r.byte == &g_c.enabled)) g_c.enabled = 1;
  }
}

void open_keyboard(int target, const std::string& text) {
  g_kb_target = target;
  g_kb_text = text;
  g_kb_cursor = 0;
  push(Page::Code);
}

void press(Page p, Row& r, Result& out) {
  if (!r.enabled) return;
  switch (r.kind) {
    case Kind::Page:
      if (r.target == Page::Code) open_keyboard(0, g_last_direct);
      else push(r.target);
      return;
    case Kind::Toggle: case Kind::Flag: change(p, r, 1); return;
    case Kind::Choice: change(p, r, 1); return;
    case Kind::Action: break;
    default: return;
  }
  if (r.action == kActUnranked) { out.start_unranked = true; return; }
  if (r.action == kActCancel) { out.cancel_search = true; return; }
  if (r.action == kActWindow) { out.open_window = true; return; }
  if (r.action == kActDefaults) {
    const uint8_t tools = g_c.tools, enabled = g_c.enabled;
    mu_pc_defaults(&g_c);
    g_c.tools = tools; g_c.enabled = enabled;
    g_c_changed = true;
    return;
  }
  if (r.action >= kActPreset && r.action < kActPreset + 100) { apply_preset(r.action - kActPreset); return; }
  if (r.action >= kActGhost && r.action < kActGhost + 100) {
    if (practice_cpu::ghost(r.action - kActGhost, g_c)) {
      g_c.enabled = 1;
      g_c_changed = true;
      // The CPU takes the player's own character (Training starts again for it when it differs).
      const int css = css_kind_of(practice_ghosts::kGhosts[r.action - kActGhost].character);
      if (css >= 0) slippi::native_practice::submit_train_against(css);
    }
    return;
  }
  if (r.action >= kActLoad && r.action < kActLoad + 100) {
    MuPcConfig saved;
    if (practice_cpu::slot(r.action - kActLoad, saved)) {
      const uint8_t tools = g_c.tools;
      g_c = saved; g_c.tools = tools; g_c.preset = 0;
      g_c_changed = true;
    }
    return;
  }
  if (r.action >= kActSave && r.action < kActSave + 100) { practice_cpu::set_slot(r.action - kActSave, g_c); out.changed = true; return; }
  if (r.action == kActTeams) { open_keyboard(4, g_last_teams); return; }
  if (r.action >= kActBlocked && r.action < kActBlocked + 3) {
    open_keyboard(1 + r.action - kActBlocked, slippi::online::config().blocked_codes[r.action - kActBlocked]);
    return;
  }
}

const char* kb_title() { return g_kb_target == 0 ? "Direct code" : g_kb_target == 4 ? "Teams code" : "Skip this code"; }

void keyboard_finish(Result& out) {
  if (g_kb_target == 0 || g_kb_target == 4) {
    if (g_kb_text.empty()) return;
    (g_kb_target == 0 ? g_last_direct : g_last_teams) = g_kb_text;
    (g_kb_target == 0 ? out.start_direct : out.start_teams) = true;
    out.direct_code = g_kb_text;
  } else {
    slippi::online::config().blocked_codes[g_kb_target - 1] = g_kb_text;
    out.changed = true;
    pop();
  }
}

void keyboard_input(const Input& in, Result& out) {
  // The grid: 3 rows of 13 characters (the last row 11), then Delete, Paste, OK.
  int& k = g_kb_cursor;
  const int row = k < kKeyCount ? k / kKeyCols : 3;
  if (in.left) k = k < kKeyCount ? (k % kKeyCols == 0 ? std::min(k + kKeyCols - 1, kKeyCount - 1) : k - 1) : (k == kKeyCount ? kKeyCount + 2 : k - 1);
  if (in.right) k = k < kKeyCount ? ((k % kKeyCols == kKeyCols - 1 || k == kKeyCount - 1) ? k - k % kKeyCols : k + 1) : (k == kKeyCount + 2 ? kKeyCount : k + 1);
  if (in.down) {
    if (row < 2) k = std::min(k + kKeyCols, kKeyCount - 1);
    else if (row == 2) k = kKeyCount + std::min(2, (k % kKeyCols) / 5);
    else k = std::min((k - kKeyCount) * 5, kKeyCols - 1);
  }
  if (in.up) {
    if (row == 3) k = 2 * kKeyCols + std::min(10, (k - kKeyCount) * 5);
    else if (row > 0) k -= kKeyCols;
    else k = kKeyCount + std::min(2, k / 5);
  }
  const auto add = [](char c) { if (g_kb_text.size() < 16) g_kb_text.push_back(c); };
  if (in.a) {
    if (k < kKeyCount) add(kKeys[k]);
    else if (k == kKeyCount) { if (!g_kb_text.empty()) g_kb_text.pop_back(); }
    else if (k == kKeyCount + 1) {
      if (const char* clip = ImGui::GetClipboardText())
        for (const char* p = clip; *p; ++p) { const char c = (char)std::toupper((unsigned char)*p); if (std::strchr(kKeys, c) && c) add(c); }
    } else keyboard_finish(out);
  }
  if (in.y) { if (!g_kb_text.empty()) g_kb_text.pop_back(); }
  if (in.start) keyboard_finish(out);
  if (in.b) { if (!g_kb_text.empty()) g_kb_text.pop_back(); else pop(); }
  if (g_f.keyboard) {
    for (ImWchar c : ImGui::GetIO().InputQueueCharacters) {
      const char u = (char)std::toupper((int)c);
      if (c < 128 && u && std::strchr(kKeys, u)) add(u);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !g_kb_text.empty()) g_kb_text.pop_back();
  }
}

// ---------------------------------------------------------------------------------------------
// Drawing helpers shared by the looks
float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

void fill_gradient(ImDrawList* dl, ImU32 top, ImU32 bottom, float y0, float y1) {
  const int v0 = dl->VtxBuffer.Size;
  dl->PathFillConvex(top);
  ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, y0), ImVec2(0, y1), top, bottom);
}
void rect_gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding) {
  const int v0 = dl->VtxBuffer.Size;
  dl->AddRectFilled(a, b, top, rounding);
  ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, a.y), ImVec2(0, b.y), top, bottom);
}

// A Main Menu bar: arrow-shaped left end, round right end.
void bar_path(ImDrawList* dl, float left, float top, float right, float h, float tip) {
  const float cy = top + h * 0.5f, r = h * 0.5f;
  dl->PathLineTo(ImVec2(left - tip, cy));
  dl->PathLineTo(ImVec2(left, top));
  dl->PathLineTo(ImVec2(right - r, top));
  dl->PathArcTo(ImVec2(right - r, cy), r, -IM_PI * 0.5f, IM_PI * 0.5f, 14);
  dl->PathLineTo(ImVec2(left, top + h));
}

// The GameCube buttons the hints show.
void button_glyph(ImDrawList* dl, float x, float cy, float u, char which, float a) {
  const ImU32 white = with_alpha(IM_COL32(255, 255, 255, 255), a);
  switch (which) {
    case 'A': {
      const float r = 12.0f * u;
      dl->AddCircleFilled(ImVec2(x + r, cy + 1.5f * u), r, with_alpha(IM_COL32(10, 77, 23, 255), a), 20);
      dl->AddCircleFilled(ImVec2(x + r, cy), r, with_alpha(IM_COL32(47, 170, 70, 255), a), 20);
      text(dl, x + r - text_width(13.0f * u, "A") * 0.5f, cy - 7.0f * u, 13.0f * u, white, "A", 0, false);
      break;
    }
    case 'B': {
      const float r = 9.5f * u;
      dl->AddCircleFilled(ImVec2(x + r, cy + 1.5f * u), r, with_alpha(IM_COL32(92, 13, 8, 255), a), 20);
      dl->AddCircleFilled(ImVec2(x + r, cy), r, with_alpha(IM_COL32(214, 44, 34, 255), a), 20);
      text(dl, x + r - text_width(11.0f * u, "B") * 0.5f, cy - 6.0f * u, 11.0f * u, white, "B", 0, false);
      break;
    }
    case 'S': {
      const float w = 30.0f * u, h = 14.0f * u;
      dl->AddRectFilled(ImVec2(x, cy - h * 0.5f), ImVec2(x + w, cy + h * 0.5f), with_alpha(IM_COL32(190, 192, 202, 255), a), h * 0.5f);
      text(dl, x + 3.5f * u, cy - 5.0f * u, 9.0f * u, with_alpha(IM_COL32(30, 30, 36, 255), a), "START", 0, false);
      break;
    }
    case 'D': {
      const float s = 22.0f * u, t = s / 3.0f;
      const ImU32 c = with_alpha(IM_COL32(214, 216, 224, 255), a);
      dl->AddRectFilled(ImVec2(x + t, cy - s * 0.5f), ImVec2(x + 2 * t, cy + s * 0.5f), c, 2.0f * u);
      dl->AddRectFilled(ImVec2(x, cy - t * 0.5f), ImVec2(x + s, cy + t * 0.5f), c, 2.0f * u);
      break;
    }
    case 'X':
    case 'Y': {   // the kidney buttons: light grey pills
      const char label[2] = {which, 0};
      const float w = 24.0f * u, h = 18.0f * u;
      dl->AddRectFilled(ImVec2(x, cy - h * 0.5f), ImVec2(x + w, cy + h * 0.5f), with_alpha(IM_COL32(214, 216, 224, 255), a), h * 0.5f);
      text(dl, x + w * 0.5f - text_width(11.0f * u, label) * 0.5f, cy - 6.0f * u, 11.0f * u, with_alpha(IM_COL32(40, 40, 46, 255), a), label, 0, false);
      break;
    }
    case 'Z': {
      const float w = 22.0f * u, h = 16.0f * u;
      dl->AddRectFilled(ImVec2(x, cy - h * 0.5f), ImVec2(x + w, cy + h * 0.5f), with_alpha(IM_COL32(108, 72, 196, 255), a), 4.0f * u);
      text(dl, x + w * 0.5f - text_width(11.0f * u, "Z") * 0.5f, cy - 6.0f * u, 11.0f * u, white, "Z", 0, false);
      break;
    }
    case 'L':
    case 'R': {   // the triggers
      const char label[2] = {which, 0};
      const float w = 22.0f * u, h = 16.0f * u;
      dl->AddRectFilled(ImVec2(x, cy - h * 0.5f), ImVec2(x + w, cy + h * 0.5f), with_alpha(IM_COL32(150, 154, 168, 255), a), 4.0f * u);
      text(dl, x + w * 0.5f - text_width(11.0f * u, label) * 0.5f, cy - 6.0f * u, 11.0f * u, white, label, 0, false);
      break;
    }
    default: break;
  }
}
float glyph_width(char which, float u) {
  switch (which) {
    case 'A': case 'X': case 'Y': return 24.0f * u;
    case 'B': return 19.0f * u;
    case 'S': return 30.0f * u;
    case 'D': case 'Z': case 'L': case 'R': return 22.0f * u;
    default: return 0;
  }
}
// "A:Pick B:Back ...": a row of button glyphs and words; returns the width.
float hints(ImDrawList* dl, float x, float cy, float u, const char* spec, float a, bool draw = true) {
  float pen = x;
  const char* p = spec;
  while (*p) {
    const char which = *p++;
    if (*p == ':') ++p;
    std::string word;
    while (*p && *p != '|') word.push_back(*p++);
    if (*p == '|') ++p;
    if (draw) button_glyph(dl, pen, cy, u, which, a);
    pen += glyph_width(which, u) + 6.0f * u;
    if (draw) text(dl, pen, cy - 9.0f * u, 17.0f * u, with_alpha(IM_COL32(255, 255, 255, 255), a), word.c_str());
    pen += text_width(17.0f * u, word.c_str()) + 16.0f * u;
  }
  return pen - x;
}

// Wrap text into lines no wider than `width`.
std::vector<std::string> wrap(const std::string& s, float px, float width) {
  std::vector<std::string> lines;
  std::string line, word;
  const auto flush_word = [&]() {
    if (word.empty()) return;
    const std::string trial = line.empty() ? word : line + " " + word;
    if (!line.empty() && text_width(px, trial.c_str()) > width) { lines.push_back(line); line = word; }
    else line = trial;
    word.clear();
  };
  for (char c : s) { if (c == ' ') flush_word(); else word.push_back(c); }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string about_now(const Row* r) {
  if (!g_error.empty() && g_f.time < g_error_until) return g_error;
  return r ? r->about : std::string();
}

// ---------------------------------------------------------------------------------------------
// Look 1: Main Menu bars
constexpr ImU32 kGoldTop = IM_COL32(240, 200, 112, 255), kGoldBottom = IM_COL32(168, 122, 30, 255);
constexpr ImU32 kGold = IM_COL32(201, 150, 47, 255), kGoldText = IM_COL32(233, 180, 76, 255);
constexpr ImU32 kYellowTop = IM_COL32(255, 243, 150, 255), kYellowBottom = IM_COL32(240, 180, 0, 255);

void draw_bar(ImDrawList* dl, float left, float top, float right, float h, float u, bool lit, const char* label,
              const std::string& value, bool enabled, float a, bool notch) {
  const float tip = h * 0.48f, rim = 5.0f * u * (h / (50.0f * u));
  // Shadow, rim, inside.
  bar_path(dl, left + 2.0f * u, top + 3.0f * u, right + 2.0f * u, h, tip);
  dl->PathFillConvex(with_alpha(IM_COL32(0, 0, 0, 150), a));
  if (lit) {
    bar_path(dl, left, top, right, h, tip);
    dl->PathFillConvex(with_alpha(IM_COL32(255, 210, 40, 70), a));
    bar_path(dl, left - 3 * u, top - 3 * u, right + 3 * u, h + 6 * u, tip + 3 * u);
    dl->PathFillConvex(with_alpha(IM_COL32(255, 203, 0, 46), a));
  }
  bar_path(dl, left, top, right, h, tip);
  fill_gradient(dl, with_alpha(lit ? IM_COL32(255, 240, 122, 255) : kGoldTop, a), with_alpha(lit ? IM_COL32(233, 173, 0, 255) : kGoldBottom, a), top, top + h);
  bar_path(dl, left + rim * 0.15f, top + rim, right - rim, h - 2 * rim, tip - rim * 1.05f);
  if (lit) fill_gradient(dl, with_alpha(kYellowTop, a), with_alpha(kYellowBottom, a), top + rim, top + h - rim);
  else fill_gradient(dl, with_alpha(IM_COL32(40, 36, 28, 255), a), with_alpha(IM_COL32(0, 0, 0, 255), a), top + rim, top + h * 0.6f);
  if (lit) {   // the lit bar's top shine
    bar_path(dl, left + rim * 0.15f + 2 * u, top + rim + 1.5f * u, right - rim - 6 * u, (h - 2 * rim) * 0.36f, tip - rim * 1.05f - 2 * u);
    dl->PathFillConvex(with_alpha(IM_COL32(255, 255, 255, 70), a));
  }
  // The open ring at the right end of a plain bar.
  if (notch && !lit) {
    const float r = (h - 2 * rim) * 0.27f, cx = right - rim - 6.0f * u - r, cy = top + h * 0.5f;
    dl->PathArcTo(ImVec2(cx, cy), r, -IM_PI * 0.5f, IM_PI * 0.5f, 12);
    dl->PathStroke(with_alpha(kGold, a), 0, 3.6f * u);
    dl->AddLine(ImVec2(cx - 3.0f * u, cy - r), ImVec2(cx, cy - r), with_alpha(kGold, a), 3.6f * u);
    dl->AddLine(ImVec2(cx - 3.0f * u, cy + r), ImVec2(cx, cy + r), with_alpha(kGold, a), 3.6f * u);
  }
  const float px = h * 0.5f, vy = top + h * 0.5f - px * 0.56f;
  const float da = enabled ? a : a * 0.45f;
  if (lit) text(dl, left + 18.0f * u, vy, px, with_alpha(IM_COL32(18, 16, 10, 255), da), label, 0.0f, false);
  else text(dl, left + 18.0f * u, vy, px, with_alpha(kGoldText, da), label, 0.19f);
  if (!value.empty()) {
    const float vpx = px * 0.84f, w = text_width(vpx, value.c_str());
    const float vx = right - rim - (lit ? 40.0f : 22.0f) * u - w;
    text(dl, vx, top + h * 0.5f - vpx * 0.56f, vpx, with_alpha(lit ? IM_COL32(18, 16, 10, 255) : IM_COL32(255, 255, 255, 255), da), value.c_str(), 0, !lit);
  }
}

// The cursor ring the game draws at the right end of the lit bar.
void draw_ring(ImDrawList* dl, ImVec2 c, float u, float t, float a) {
  const float pulse = 0.82f + 0.18f * std::sin(t * 5.2f);
  dl->AddCircleFilled(c, 31.0f * u, with_alpha(IM_COL32(255, 248, 160, 26), a * pulse), 40);
  dl->AddCircle(c, 22.5f * u, with_alpha(IM_COL32(230, 224, 74, 210), a * pulse), 40, 5.0f * u);
  dl->AddCircleFilled(c, 18.0f * u, with_alpha(IM_COL32(255, 245, 154, 60), a), 36);
  dl->AddCircle(c, 10.5f * u, with_alpha(IM_COL32(220, 214, 62, 255), a), 32, 5.0f * u);
  dl->AddCircleFilled(c, 7.0f * u, with_alpha(IM_COL32(255, 253, 232, 255), a), 24);
}

void draw_title(ImDrawList* dl, float x, float y, float u, const char* title, float a) {
  const float px = 30.0f * u;
  text(dl, x, y, px, with_alpha(IM_COL32(163, 168, 184, 255), a), title, 0.21f);
  float sx = x + text_width(px, title) + 16.0f * u;
  for (int i = 0; i < 5; ++i, sx += 9.5f * u) {   // the red slashes after the title
    const float yt = y + 8.0f * u, yb = y + 26.0f * u;
    dl->AddQuadFilled(ImVec2(sx + 7 * u, yt), ImVec2(sx + 11 * u, yt), ImVec2(sx + 4 * u, yb), ImVec2(sx, yb), with_alpha(IM_COL32(255, 74, 58, 255), a));
  }
}

// Returns the box's height.
float draw_description(ImDrawList* dl, float x, float y, float w, float u, const std::string& s, float a) {
  float px = 20.0f * u;
  std::vector<std::string> lines = wrap(s, px, w - 24.0f * u);
  if (lines.size() > 2) { px = 17.0f * u; lines = wrap(s, px, w - 24.0f * u); }
  const float h = std::max(48.0f * u, (float)lines.size() * px * 1.05f + 22.0f * u);
  dl->AddRectFilled(ImVec2(x + 2 * u, y + 3 * u), ImVec2(x + w + 2 * u, y + h + 3 * u), with_alpha(IM_COL32(0, 0, 0, 150), a), 8.0f * u);
  rect_gradient(dl, ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(26, 28, 38, 240), a), with_alpha(IM_COL32(7, 8, 14, 240), a), 7.0f * u);
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(211, 214, 223, 255), a), 7.0f * u, 0, 3.0f * u);
  float ty = y + (h - (float)lines.size() * px * 1.05f) * 0.5f;
  for (const std::string& line : lines) {
    text(dl, x + (w - text_width(px, line.c_str())) * 0.5f, ty, px, with_alpha(IM_COL32(255, 255, 255, 255), a), line.c_str());
    ty += px * 1.05f;
  }
  return h;
}

// The code keyboard in the gold look (and, recolored, in the blue looks).
struct KeyColors { ImU32 rim_top, rim_bottom, face_top, face_bottom, text, lit_top, lit_bottom, lit_text; };
void draw_keyboard(ImDrawList* dl, float cx, float top, float u, const KeyColors& k, float a, const char* title) {
  const float key = 44.0f * u, gap = 6.0f * u, w = kKeyCols * key + (kKeyCols - 1) * gap;
  const float x0 = cx - w * 0.5f;
  // Panel behind it.
  const ImVec2 p0(x0 - 24 * u, top - 92 * u), p1(x0 + w + 24 * u, top + 4 * (key + gap) + 70 * u);
  dl->AddRectFilled(ImVec2(p0.x + 3 * u, p0.y + 4 * u), ImVec2(p1.x + 3 * u, p1.y + 4 * u), with_alpha(IM_COL32(0, 0, 0, 140), a), 14 * u);
  rect_gradient(dl, p0, p1, with_alpha(IM_COL32(16, 16, 22, 236), a), with_alpha(IM_COL32(4, 4, 8, 236), a), 14 * u);
  dl->AddRect(p0, p1, with_alpha(k.rim_top, a), 14 * u, 0, 3 * u);
  text(dl, p0.x + 22 * u, p0.y + 14 * u, 24 * u, with_alpha(k.lit_top, a), title);
  // The code so far, one box a character.
  const int cells = 10;
  const float cw = 34.0f * u, ch = 44.0f * u, cg = 6.0f * u, cells_w = cells * cw + (cells - 1) * cg;
  float bx = cx - cells_w * 0.5f;
  const float by = top - 54.0f * u;
  for (int i = 0; i < cells; ++i, bx += cw + cg) {
    const bool cur = i == (int)g_kb_text.size();
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + cw, by + ch), with_alpha(IM_COL32(2, 4, 10, 255), a), 5 * u);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + cw, by + ch), with_alpha(cur ? k.lit_top : k.rim_bottom, a), 5 * u, 0, cur ? 3 * u : 2 * u);
    if (i < (int)g_kb_text.size()) {
      char s[2] = {g_kb_text[i], 0};
      text(dl, bx + (cw - text_width(28 * u, s)) * 0.5f, by + 7 * u, 28 * u, with_alpha(IM_COL32(255, 255, 255, 255), a), s);
    } else if (cur && std::fmod(g_f.time, 1.0) < 0.6) {
      dl->AddRectFilled(ImVec2(bx + 8 * u, by + ch - 10 * u), ImVec2(bx + cw - 8 * u, by + ch - 6 * u), with_alpha(k.lit_top, a));
    }
  }
  // The keys.
  const auto key_box = [&](float x, float y, float kw, bool lit, const char* label, ImU32 face_top, ImU32 face_bottom) {
    dl->AddRectFilled(ImVec2(x + 2 * u, y + 3 * u), ImVec2(x + kw + 2 * u, y + key + 3 * u), with_alpha(IM_COL32(0, 0, 0, 150), a), 8 * u);
    rect_gradient(dl, ImVec2(x, y), ImVec2(x + kw, y + key), with_alpha(lit ? k.lit_top : k.rim_top, a), with_alpha(lit ? k.lit_bottom : k.rim_bottom, a), 8 * u);
    rect_gradient(dl, ImVec2(x + 3 * u, y + 3 * u), ImVec2(x + kw - 3 * u, y + key - 3 * u), with_alpha(lit ? k.lit_top : face_top, a),
                  with_alpha(lit ? k.lit_bottom : face_bottom, a), 6 * u);
    const float px = 22.0f * u;
    text(dl, x + (kw - text_width(px, label)) * 0.5f, y + (key - px) * 0.5f - 1.0f * u, px,
         with_alpha(lit ? k.lit_text : k.text, a), label, 0, !lit);
  };
  for (int i = 0; i < kKeyCount; ++i) {
    char s[2] = {kKeys[i], 0};
    key_box(x0 + (i % kKeyCols) * (key + gap), top + (i / kKeyCols) * (key + gap), key, g_kb_cursor == i, s, k.face_top, k.face_bottom);
  }
  const float wide = (w - 2 * gap) / 3.0f, y3 = top + 3 * (key + gap);
  key_box(x0, y3, wide, g_kb_cursor == kKeyCount, "Delete", k.face_top, k.face_bottom);
  key_box(x0 + wide + gap, y3, wide, g_kb_cursor == kKeyCount + 1, "Paste", k.face_top, k.face_bottom);
  key_box(x0 + 2 * (wide + gap), y3, wide, g_kb_cursor == kKeyCount + 2, (g_kb_target == 0 || g_kb_target == 4) ? "Connect" : "OK",
          IM_COL32(48, 150, 72, 255), IM_COL32(18, 90, 40, 255));
  const float hy = y3 + key + 30 * u;
  const char* spec = (g_kb_target == 0 || g_kb_target == 4) ? "A:Type|B:Delete|S:Connect" : "A:Type|B:Delete|S:Done";
  const float hw = hints(dl, 0, 0, u, spec, a, false);
  hints(dl, cx - hw * 0.5f, hy, u, spec, a);
  const std::string about = about_now(nullptr);
  if (!about.empty()) text(dl, cx - text_width(17 * u, about.c_str()) * 0.5f, hy + 20 * u, 17 * u, with_alpha(IM_COL32(255, 120, 100, 255), a), about.c_str());
}

const KeyColors kGoldKeys{IM_COL32(240, 200, 112, 255), IM_COL32(168, 122, 30, 255), IM_COL32(40, 36, 28, 255), IM_COL32(0, 0, 0, 255),
                          IM_COL32(233, 180, 76, 255), IM_COL32(255, 240, 122, 255), IM_COL32(240, 180, 0, 255), IM_COL32(18, 16, 10, 255)};
const KeyColors kBlueKeys{IM_COL32(156, 200, 255, 255), IM_COL32(63, 111, 200, 255), IM_COL32(59, 127, 232, 255), IM_COL32(24, 71, 168, 255),
                          IM_COL32(255, 255, 255, 255), IM_COL32(255, 246, 168, 255), IM_COL32(246, 178, 28, 255), IM_COL32(10, 10, 10, 255)};

struct Anim { float x = 0, target = 0; };
std::vector<Anim> g_bar_anim;

void draw_bars(ImDrawList* dl, const Frame& f, std::vector<Row>& rows, float a) {
  const float gh = f.game_y1 - f.game_y0, u = gh / 720.0f;
  Level& lv = level();
  const float t = (float)f.time;
  if (lv.page == Page::Code) {
    draw_keyboard(dl, (f.game_x0 + f.game_x1) * 0.5f, f.game_y0 + gh * 0.42f, u, kGoldKeys, a,
                  kb_title());
    return;
  }
  const int visible = 6, count = (int)rows.size();
  if (lv.cursor < lv.top) lv.top = lv.cursor;
  if (lv.cursor >= lv.top + visible) lv.top = lv.cursor - visible + 1;
  lv.top = std::clamp(lv.top, 0, std::max(0, count - visible));
  const int shown = std::min(visible, count);

  // Width from the longest row on the page (labels and values in the game's font).
  float need = 0.0f;
  for (const Row& r : rows) {
    const float px = 25.0f * u;
    need = std::max(need, text_width(px, r.label.c_str()) + text_width(px * 0.84f, value_of(r).c_str()) + 118.0f * u);
  }
  const float w = std::clamp(need, 320.0f * u, (f.game_x1 - f.game_x0) * 0.56f);
  const float h = 50.0f * u, pitch = 60.0f * u;
  // Room on the right for the cursor ring (it reaches past the bar) even when the picture fills the window.
  const float right = f.game_x1 - 46.0f * u;
  const float desc_h = 48.0f * u, bottom = f.game_y1 - 116.0f * u;
  const float block_top = bottom - desc_h - 22.0f * u - (shown - 1) * pitch - h;
  // The bars step in and out like the Main Menu's.
  static const float kStagger[6] = {0.0f, -16.0f, -34.0f, -22.0f, -34.0f, -16.0f};
  g_bar_anim.resize((size_t)count);
  const float since = (float)(f.time - g_page_changed_at);
  for (int i = 0; i < shown; ++i) {
    const int index = lv.top + i;
    const bool lit = index == lv.cursor;
    Anim& an = g_bar_anim[(size_t)index];
    an.target = (kStagger[i % 6] + (lit ? 16.0f : 0.0f)) * u;
    an.x += (an.target - an.x) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    const float slide = (1.0f - ease((since - 0.025f * i) / 0.18f)) * 90.0f * u;
    const float ba = a * ease((since - 0.025f * i) / 0.14f);
    const float top = block_top + i * pitch;
    const float l = right - w + an.x + slide, r = right + an.x - 16.0f * u + slide;
    const Row& row = rows[(size_t)index];
    const float label_w = text_width(h * 0.5f, row.label.c_str());
    const std::string shown_value = fit(value_of(row), h * 0.5f * 0.84f, (r - l) - label_w - 72.0f * u);
    draw_bar(dl, l, top, r, h, u, lit, row.label.c_str(), shown_value, row.enabled, ba, false);
    if (lit) draw_ring(dl, ImVec2(r - 6.0f * u, top + h * 0.5f), u, t, ba);
    if (lit && (row.kind == Kind::Odds || row.kind == Kind::Percent || row.kind == Kind::Number || row.kind == Kind::Choice) && row.enabled) {
      // Left and right change it: small arrows either side of the value.
      const float vpx = h * 0.5f * 0.84f, vw = text_width(vpx, shown_value.c_str());
      const float vx = r - 5.0f * u - 40.0f * u - vw, cy = top + h * 0.5f;
      const ImU32 dark = with_alpha(IM_COL32(18, 16, 10, 255), ba);
      dl->AddTriangleFilled(ImVec2(vx - 9 * u, cy - 6 * u), ImVec2(vx - 9 * u, cy + 6 * u), ImVec2(vx - 17 * u, cy), dark);
      dl->AddTriangleFilled(ImVec2(vx + vw + 7 * u, cy - 6 * u), ImVec2(vx + vw + 7 * u, cy + 6 * u), ImVec2(vx + vw + 15 * u, cy), dark);
    }
  }
  // Title (with the path when deeper than one level), scroll marks, the description box, hints.
  std::string title = page_title(lv.page);
  draw_title(dl, right - w - 28.0f * u, block_top - 52.0f * u, u, title.c_str(), a);
  if (count > visible) {
    const float x = right + 8.0f * u, y0 = block_top + 4.0f * u, y1 = block_top + (shown - 1) * pitch + h - 4.0f * u;
    dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 5 * u, y1), with_alpha(IM_COL32(255, 255, 255, 34), a), 3 * u);
    const float span = y1 - y0, th = span * (float)visible / (float)count, ty = y0 + (span - th) * (float)lv.top / (float)(count - visible);
    dl->AddRectFilled(ImVec2(x, ty), ImVec2(x + 5 * u, ty + th), with_alpha(kGold, a), 3 * u);
  }
  const Row* cur = count ? &rows[(size_t)lv.cursor] : nullptr;
  const float dx = right - w - 36.0f * u, dw = w + 36.0f * u;
  const float dy = block_top + (shown - 1) * pitch + h + 22.0f * u;
  const float dh = draw_description(dl, dx, dy, dw, u, about_now(cur), a);
  const bool edits = cur && (cur->kind == Kind::Odds || cur->kind == Kind::Percent || cur->kind == Kind::Number || cur->kind == Kind::Choice);
  const char* spec = edits ? "D:Change|B:Back|S:Close" : (g_stack.size() > 1 ? "A:Pick|B:Back|S:Close" : "A:Pick|S:Close");
  // The buttons sit under the description, right-aligned: on the title's line a long title met them,
  // and the Training damage display sits there too.
  const float hw = hints(dl, 0, 0, u * 0.9f, spec, a, false);
  hints(dl, dx + dw - hw, dy + dh + 18.0f * u, u * 0.9f, spec, a * 0.92f);
  (void)dx; (void)dy;
}

// After an online session, back in Training: the question, centred near the top, in every look.
void draw_offer(ImDrawList* dl, const Frame& f) {
  if (f.search.offer.empty()) return;
  // Sized like the closed search bar, centred between the Training logo and its damage display.
  const float u = (f.game_y1 - f.game_y0) / 720.0f * 0.82f;
  const float pulse = 0.8f + 0.2f * std::sin((float)f.time * 3.0f);
  const float w = text_width(25 * u, f.search.offer.c_str()) + 110 * u;
  const float left = (f.game_x0 + f.game_x1 - w) * 0.5f, top = f.game_y0 + 170 * u, h = 50 * u;
  draw_bar(dl, left, top, left + w, h, u, true, f.search.offer.c_str(), "", true, pulse, false);
  const char* spec = "D:Up  Search|D:Down  Keep training";
  const float hw = hints(dl, 0, 0, u, spec, 1.0f, false);
  hints(dl, (f.game_x0 + f.game_x1 - hw) * 0.5f, top + h + 8 * u, u, spec, 1.0f);
}

// While closed, the gold look keeps the search on screen as one lit bar at the top right.
void draw_bars_closed(ImDrawList* dl, const Frame& f) {
  if (!f.search.searching) return;
  const float u = (f.game_y1 - f.game_y0) / 720.0f * 0.82f;
  const float h = 50.0f * u, right = f.game_x1 - 24.0f * u, top = f.game_y0 + 26.0f * u;
  const std::string v = f.search.elapsed;
  const float w = text_width(25 * u, f.search.mode.c_str()) + text_width(21 * u, v.c_str()) + 110 * u;
  draw_bar(dl, right - w, top, right, h, u, true, f.search.mode.c_str(), v, true, 1.0f, false);
  const float dot = 0.55f + 0.45f * std::sin((float)f.time * 4.0f);
  dl->AddCircleFilled(ImVec2(right - 20 * u, top + h * 0.5f), 5.5f * u, IM_COL32(40, 170, 70, (int)(255 * dot)), 16);
  const std::string sub = f.search.opponent.empty() ? std::string("Start + Up + Z: menu") : "Found " + f.search.opponent;
  text(dl, right - w + 4 * u, top + h + 6 * u, 15 * u, IM_COL32(230, 232, 240, 230), sub.c_str());
}

// ---------------------------------------------------------------------------------------------
// The blue looks (rails, slots, strip) share these colors.
constexpr ImU32 kNavyTop = IM_COL32(8, 19, 48, 255), kNavyBottom = IM_COL32(11, 29, 72, 255);
constexpr ImU32 kBlueEdge = IM_COL32(47, 111, 224, 255), kLabel = IM_COL32(134, 163, 224, 255);
constexpr ImU32 kAmber = IM_COL32(255, 210, 58, 255), kGreen = IM_COL32(61, 220, 106, 255);

// One rail card: small label, value, optional chips; lit cards get the gold frame.
float rail_card(ImDrawList* dl, float x, float y, float w, float u, const char* label, const std::string& value, bool lit,
                bool enabled, float a, float value_px = 19.0f, const char* chip = nullptr, ImU32 chip_col = 0, bool progress = false) {
  const float h = (value_px + 13.0f + 22.0f) * u + (chip ? 22.0f * u : 0.0f) + (progress ? 10.0f * u : 0.0f);
  const ImVec2 p0(x, y), p1(x + w, y + h);
  if (lit) {
    dl->AddRectFilled(ImVec2(p0.x - 4 * u, p0.y - 4 * u), ImVec2(p1.x + 4 * u, p1.y + 4 * u), with_alpha(IM_COL32(255, 207, 58, 40), a), 12 * u);
    rect_gradient(dl, p0, p1, with_alpha(IM_COL32(255, 207, 58, 64), a), with_alpha(IM_COL32(255, 207, 58, 16), a), 9 * u);
    dl->AddRect(p0, p1, with_alpha(kAmber, a), 9 * u, 0, 2.0f * u);
    dl->AddRectFilled(ImVec2(p0.x + 1 * u, p0.y + 6 * u), ImVec2(p0.x + 4 * u, p1.y - 6 * u), with_alpha(kAmber, a), 2 * u);
  } else {
    rect_gradient(dl, p0, p1, with_alpha(IM_COL32(255, 255, 255, 20), a), with_alpha(IM_COL32(255, 255, 255, 7), a), 9 * u);
    dl->AddRect(p0, p1, with_alpha(IM_COL32(255, 255, 255, 30), a), 9 * u, 0, 1.0f * u);
  }
  const float da = enabled ? a : a * 0.45f;
  std::string upper = label;
  for (char& c : upper) c = (char)std::toupper((unsigned char)c);
  float label_px = 13 * u;   // a long label (a replay moment) shrinks to the card's width
  const float label_w = text_width(label_px, upper.c_str());
  if (label_w > w - 22 * u) label_px = std::max(9 * u, label_px * (w - 22 * u) / label_w);
  text(dl, x + 11 * u, y + 7 * u + (13 * u - label_px) * 0.5f, label_px, with_alpha(lit ? IM_COL32(255, 229, 138, 255) : kLabel, da),
       fit(upper, label_px, w - 22 * u).c_str(), 0, false);
  const std::string shown = fit(value, value_px * u, w - 22 * u);
  text(dl, x + 11 * u, y + 22 * u, value_px * u, with_alpha(IM_COL32(255, 255, 255, 255), da), shown.c_str());
  float cy = y + (22.0f + value_px + 6.0f) * u;
  if (chip) {
    const float cw = text_width(12 * u, chip) + 18 * u;
    rect_gradient(dl, ImVec2(x + 11 * u, cy), ImVec2(x + 11 * u + cw, cy + 18 * u), with_alpha(chip_col, a),
                  with_alpha(IM_COL32((chip_col >> 0 & 0xFF) * 6 / 10, (chip_col >> 8 & 0xFF) * 6 / 10, (chip_col >> 16 & 0xFF) * 6 / 10, 255), a), 9 * u);
    text(dl, x + 20 * u, cy + 3 * u, 12 * u, with_alpha(IM_COL32(255, 255, 255, 255), a), chip, 0, false);
    cy += 22.0f * u;
  }
  if (progress) {
    const float bx0 = x + 11 * u, bx1 = x + w - 11 * u, by = cy + 2 * u;
    dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + 4 * u), with_alpha(IM_COL32(255, 255, 255, 30), a), 2 * u);
    const float phase = (float)std::fmod(g_f.time * 0.6, 1.0);
    const float sw = (bx1 - bx0) * 0.38f, sx = bx0 + (bx1 - bx0 + sw) * phase - sw;
    const float l = std::max(bx0, sx), r = std::min(bx1, sx + sw);
    if (r > l) dl->AddRectFilledMultiColor(ImVec2(l, by), ImVec2(r, by + 4 * u), with_alpha(IM_COL32(61, 220, 106, 0), a),
                                           with_alpha(kGreen, a), with_alpha(kGreen, a), with_alpha(IM_COL32(61, 220, 106, 0), a));
  }
  return h;
}

void rail_background(ImDrawList* dl, float x0, float x1, float y0, float y1, bool right_side, bool overlay, float a) {
  if (overlay) {
    // Over the picture: see-through navy that fades into the game toward the middle.
    const ImU32 solid = with_alpha(IM_COL32(8, 19, 48, 222), a), clear = with_alpha(IM_COL32(8, 19, 48, 0), a);
    const float fade = (x1 - x0) * 0.22f;
    if (!right_side) {
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1 - fade, y1), solid);
      dl->AddRectFilledMultiColor(ImVec2(x1 - fade, y0), ImVec2(x1, y1), solid, clear, clear, solid);
    } else {
      dl->AddRectFilled(ImVec2(x0 + fade, y0), ImVec2(x1, y1), solid);
      dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x0 + fade, y1), clear, solid, solid, clear);
    }
    return;
  }
  rect_gradient(dl, ImVec2(x0, y0), ImVec2(x1, y1), with_alpha(kNavyTop, a), with_alpha(kNavyBottom, a), 0);
  const float ex = right_side ? x0 : x1 - 2.0f;
  dl->AddRectFilled(ImVec2(ex, y0), ImVec2(ex + 2.0f, y1), with_alpha(kBlueEdge, a));
  const float gx = right_side ? x0 + 2.0f : x1 - 12.0f;
  dl->AddRectFilledMultiColor(ImVec2(gx, y0), ImVec2(gx + 10.0f, y1),
                              with_alpha(right_side ? IM_COL32(47, 111, 224, 70) : IM_COL32(47, 111, 224, 0), a),
                              with_alpha(right_side ? IM_COL32(47, 111, 224, 0) : IM_COL32(47, 111, 224, 70), a),
                              with_alpha(right_side ? IM_COL32(47, 111, 224, 0) : IM_COL32(47, 111, 224, 70), a),
                              with_alpha(right_side ? IM_COL32(47, 111, 224, 70) : IM_COL32(47, 111, 224, 0), a));
}

void rail_header(ImDrawList* dl, float x, float y, float w, float u, const char* title, float a) {
  std::string upper = title;
  for (char& c : upper) c = (char)std::toupper((unsigned char)c);
  text(dl, x, y, 24 * u, with_alpha(kAmber, a), upper.c_str());
  dl->AddRectFilledMultiColor(ImVec2(x, y + 30 * u), ImVec2(x + w, y + 32 * u), with_alpha(kAmber, a), with_alpha(IM_COL32(255, 210, 58, 0), a),
                              with_alpha(IM_COL32(255, 210, 58, 0), a), with_alpha(kAmber, a));
}

// The rails' right side: the search and what the CPU is doing.
void rail_status(ImDrawList* dl, float x, float y, float w, float u, const Frame& f, float a, bool with_hints) {
  rail_header(dl, x, y, w, u, "Search", a);
  y += 44 * u;
  if (f.search.searching) {
    y += rail_card(dl, x, y, w, u, f.search.mode.c_str(), f.search.elapsed, true, true, a, 34.0f,
                   f.search.opponent.empty() ? "searching" : "found", IM_COL32(69, 216, 112, 255), true) + 9 * u;
  } else {
    y += rail_card(dl, x, y, w, u, "Unranked", f.search.available ? "Off" : "Training only", false, true, a) + 9 * u;
  }
  int n = 0;
  for (const std::string& code : slippi::online::config().blocked_codes) n += code.empty() ? 0 : 1;
  char b[24]; std::snprintf(b, sizeof b, n ? "%d codes" : "None", n);
  y += rail_card(dl, x, y, w, u, "Skipped codes", b, false, true, a) + 9 * u;
  const practice_cpu::Status st = practice_cpu::status();
  y += rail_card(dl, x, y, w, u, "CPU now", !g_c.enabled ? "Off" : (st.driving ? plan_text(st.plan) : "Waiting"), false, true, a) + 9 * u;
  y += rail_card(dl, x, y, w, u, "Save states", (g_c.tools & MU_PC_TOOL_DPAD) ? "D-pad" : "Off", false, true, a) + 9 * u;
  (void)with_hints;
}

void draw_rails(ImDrawList* dl, const Frame& f, std::vector<Row>& rows, float a) {
  const float gh = f.game_y1 - f.game_y0, u = gh / 720.0f;
  const float bar_l = f.game_x0, bar_r = f.display_w - f.game_x1;
  const bool overlay = std::min(bar_l, bar_r) < 150.0f * u;
  const float rw = overlay ? 228.0f * u : std::min(bar_l, 260.0f * u);
  const float lx0 = overlay ? f.game_x0 : bar_l - rw, lx1 = lx0 + rw;
  const float rx1 = overlay ? f.game_x1 : f.game_x1 + std::min(bar_r, 260.0f * u), rx0 = rx1 - (overlay ? rw : std::min(bar_r, 260.0f * u));
  const float y0 = f.game_y0, y1 = f.game_y1;
  Level& lv = level();
  rail_background(dl, lx0, lx1, y0, y1, false, overlay, a);
  rail_background(dl, rx0, rx1, y0, y1, true, overlay, a);
  const float pad = 12.0f * u;
  const float cw_l = (lx1 - lx0) - 2 * pad - (overlay ? 40 * u : 0), cw_r = (rx1 - rx0) - 2 * pad - (overlay ? 40 * u : 0);
  const float cx_l = lx0 + pad, cx_r = rx0 + pad + (overlay ? 40 * u : 0);
  if (lv.page == Page::Code) {
    draw_keyboard(dl, (f.game_x0 + f.game_x1) * 0.5f, f.game_y0 + gh * 0.42f, u, kBlueKeys, a, kb_title());
    rail_status(dl, cx_r, y0 + 16 * u, cw_r, u, f, a, false);
    return;
  }
  // The replay menu puts what the lit row means on the right rail (the coach's advice is long).
  const bool replay = lv.page == Page::Replay || lv.page == Page::ReplaySettings;
  // Left: the page. Header with the path back, then the rows as cards (scrolling with the cursor).
  std::string title = page_title(lv.page);
  rail_header(dl, cx_l, y0 + 16 * u, cw_l, u, title.c_str(), a);
  float y = y0 + 60 * u;
  const float limit = y1 - 64 * u;
  // Keep the cursor in view: measure cards from lv.top.
  const auto card_h = [&](const Row& r) { (void)r; return (19.0f + 13.0f + 22.0f) * u + 7 * u; };
  if (lv.cursor < lv.top) lv.top = lv.cursor;
  while (true) {
    float hh = 0;
    for (int i = lv.top; i <= lv.cursor && i < (int)rows.size(); ++i) hh += card_h(rows[(size_t)i]);
    if (y + hh <= limit || lv.top >= lv.cursor) break;
    ++lv.top;
  }
  const float since = (float)(f.time - g_page_changed_at);
  for (int i = lv.top; i < (int)rows.size(); ++i) {
    const Row& r = rows[(size_t)i];
    if (y + card_h(r) > limit + 2 * u) break;
    const float ca = a * ease((since - 0.02f * (i - lv.top)) / 0.14f);
    const float slide = (1.0f - ease((since - 0.02f * (i - lv.top)) / 0.18f)) * -24.0f * u;
    std::string v = value_of(r);
    if (r.kind == Kind::Page && v.empty()) v = ">";
    if (r.kind == Kind::Action && v.empty()) v = "Press A";
    y += rail_card(dl, cx_l + slide, y, cw_l, u, r.label.c_str(), v, i == lv.cursor, r.enabled, ca) + 7 * u;
  }
  // Under the cards: what the lit one means, then the buttons.
  const Row* cur = rows.empty() ? nullptr : &rows[(size_t)lv.cursor];
  if (replay) {
    hints(dl, cx_l, y1 - 26 * u, u * 0.82f, "D:Move|A:Pick|B:Close", a);
    rail_header(dl, cx_r, y0 + 16 * u, cw_r, u, "About", a);
    float ry = y0 + 64 * u;
    for (const std::string& line : wrap(about_now(cur), 16 * u, cw_r)) {
      text(dl, cx_r, ry, 16 * u, with_alpha(IM_COL32(220, 228, 250, 255), a), line.c_str(), 0, false);
      ry += 20 * u;
    }
    return;
  }
  const std::vector<std::string> lines = wrap(about_now(cur), 14 * u, cw_l);
  float ly = y1 - 56 * u - (float)lines.size() * 16 * u;
  for (const std::string& line : lines) { text(dl, cx_l, ly, 14 * u, with_alpha(IM_COL32(200, 214, 245, 255), a), line.c_str(), 0, false); ly += 16 * u; }
  hints(dl, cx_l, y1 - 26 * u, u * 0.82f, g_stack.size() > 1 ? "D:Move|A:Pick|B:Back" : "D:Move|A:Pick|S:Close", a);
  // Right: the search and the CPU.
  rail_status(dl, cx_r, y0 + 16 * u, cw_r, u, f, a, true);
}

void draw_rails_closed(ImDrawList* dl, const Frame& f) {
  if (!f.search.searching) return;
  const float u = (f.game_y1 - f.game_y0) / 720.0f;
  const float bar_r = f.display_w - f.game_x1;
  const bool overlay = bar_r < 150.0f * u;
  const float w = overlay ? 190 * u : std::min(bar_r, 260.0f * u) - 24 * u;
  const float x = overlay ? f.game_x1 - w - 16 * u : f.game_x1 + 12 * u;
  rail_card(dl, x, f.game_y0 + 16 * u, w, u, f.search.mode.c_str(), f.search.elapsed, true, true, 1.0f, 30.0f,
            f.search.opponent.empty() ? "searching" : "found", IM_COL32(69, 216, 112, 255), true);
}

// ---------------------------------------------------------------------------------------------
// Look 3: character select slots
void draw_slots(ImDrawList* dl, const Frame& f, std::vector<Row>& rows, float a) {
  const float gw = f.game_x1 - f.game_x0, gh = f.game_y1 - f.game_y0, u = gh / 720.0f;
  Level& lv = level();
  if (lv.page == Page::Code) {
    draw_keyboard(dl, (f.game_x0 + f.game_x1) * 0.5f, f.game_y0 + gh * 0.42f, u, kBlueKeys, a, kb_title());
    return;
  }
  const int per = 4, count = (int)rows.size();
  if (lv.cursor < lv.top) lv.top = lv.cursor;
  if (lv.cursor >= lv.top + per) lv.top = lv.cursor - per + 1;
  lv.top = std::clamp(lv.top, 0, std::max(0, count - per));
  const float sw = 206 * u, sh = 172 * u, gap = 12 * u;
  const float total = per * sw + (per - 1) * gap;
  // The replay viewer's bar sits along the bottom while paused: the replay menu's slots stand above it.
  const bool replay = lv.page == Page::Replay || lv.page == Page::ReplaySettings;
  const float x0 = f.game_x0 + (gw - total) * 0.5f, y0 = f.game_y1 - sh - (replay ? 84 : 14) * u;
  // Dim the bottom of the picture so the slots read.
  dl->AddRectFilledMultiColor(ImVec2(f.game_x0, y0 - 80 * u), ImVec2(f.game_x1, f.game_y1), with_alpha(IM_COL32(0, 0, 0, 0), a),
                              with_alpha(IM_COL32(0, 0, 0, 0), a), with_alpha(IM_COL32(0, 0, 0, 200), a), with_alpha(IM_COL32(0, 0, 0, 200), a));
  static const ImU32 kTab[4][2] = {{IM_COL32(255, 90, 78, 255), IM_COL32(181, 29, 20, 255)},
                                   {IM_COL32(90, 155, 255, 255), IM_COL32(24, 70, 180, 255)},
                                   {IM_COL32(255, 224, 102, 255), IM_COL32(211, 154, 0, 255)},
                                   {IM_COL32(95, 215, 122, 255), IM_COL32(24, 132, 58, 255)}};
  const float since = (float)(f.time - g_page_changed_at);
  for (int i = 0; i < per && lv.top + i < count; ++i) {
    const int index = lv.top + i;
    const Row& r = rows[(size_t)index];
    const bool lit = index == lv.cursor;
    const float sa = a * ease((since - 0.03f * i) / 0.16f);
    const float x = x0 + i * (sw + gap), y = y0 + (1.0f - ease((since - 0.03f * i) / 0.2f)) * 40 * u;
    if (lit) dl->AddRectFilled(ImVec2(x - 6 * u, y - 6 * u), ImVec2(x + sw + 6 * u, y + sh + 6 * u), with_alpha(IM_COL32(255, 203, 0, 60), sa), 12 * u);
    dl->AddRectFilled(ImVec2(x + 3 * u, y + 5 * u), ImVec2(x + sw + 3 * u, y + sh + 5 * u), with_alpha(IM_COL32(0, 0, 0, 170), sa), 9 * u);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sw, y + sh), with_alpha(IM_COL32(14, 15, 19, 255), sa), 9 * u);
    rect_gradient(dl, ImVec2(x + 3 * u, y + 3 * u), ImVec2(x + sw - 3 * u, y + sh - 3 * u), with_alpha(IM_COL32(93, 97, 109, 255), sa),
                  with_alpha(IM_COL32(44, 47, 56, 255), sa), 7 * u);
    // Diagonal brushed lines, as on the slots.
    dl->PushClipRect(ImVec2(x + 3 * u, y + 3 * u), ImVec2(x + sw - 3 * u, y + sh - 3 * u), true);
    for (float d = -sh; d < sw; d += 12 * u)
      dl->AddLine(ImVec2(x + d, y + sh), ImVec2(x + d + sh, y), with_alpha(IM_COL32(255, 255, 255, 9), sa), 6 * u);
    dl->AddCircle(ImVec2(x + sw - 38 * u, y + sh - 38 * u), 26 * u, with_alpha(IM_COL32(255, 255, 255, 20), sa), 32, 6 * u);
    dl->PopClipRect();
    dl->AddRect(ImVec2(x + 3 * u, y + 3 * u), ImVec2(x + sw - 3 * u, y + sh - 3 * u), with_alpha(lit ? IM_COL32(255, 255, 255, 255) : IM_COL32(143, 149, 165, 255), sa), 7 * u, 0, 2 * u);
    if (lit) dl->AddRect(ImVec2(x - 1 * u, y - 1 * u), ImVec2(x + sw + 1 * u, y + sh + 1 * u), with_alpha(IM_COL32(255, 203, 0, 255), sa), 10 * u, 0, 3 * u);
    // The colored tab with the row's name.
    std::string upper = r.label;
    for (char& c : upper) c = (char)std::toupper((unsigned char)c);
    float tab_px = 16 * u;   // a long label (a replay moment) shrinks to the slot
    if (text_width(tab_px, upper.c_str()) > sw - 58 * u) tab_px = std::max(10 * u, tab_px * (sw - 58 * u) / text_width(tab_px, upper.c_str()));
    upper = fit(upper, tab_px, sw - 58 * u);
    const float tw = std::min(sw - 24 * u, text_width(tab_px, upper.c_str()) + 34 * u);
    const int ci = index % 4;
    const int v0 = dl->VtxBuffer.Size;
    dl->AddQuadFilled(ImVec2(x + 8 * u, y), ImVec2(x + 8 * u + tw, y), ImVec2(x + 8 * u + tw - 14 * u, y + 28 * u), ImVec2(x + 8 * u, y + 28 * u), kTab[ci][0]);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, y), ImVec2(0, y + 28 * u), with_alpha(kTab[ci][0], sa), with_alpha(kTab[ci][1], sa));
    text(dl, x + 16 * u, y + 5 * u + (16 * u - tab_px) * 0.5f, tab_px, with_alpha(IM_COL32(255, 255, 255, 255), sa * (r.enabled ? 1.0f : 0.5f)), upper.c_str());
    // Value, with arrows when it can change; then what it means.
    std::string v = value_of(r);
    if (v.empty()) v = r.kind == Kind::Page ? "Open" : (r.kind == Kind::Action ? "Press A" : "");
    const float vpx = v.size() > 10 ? 22 * u : 27 * u;
    const bool edits = r.kind == Kind::Odds || r.kind == Kind::Percent || r.kind == Kind::Number || r.kind == Kind::Choice || r.kind == Kind::Toggle || r.kind == Kind::Flag;
    text(dl, x + 14 * u, y + 46 * u, vpx, with_alpha(IM_COL32(255, 255, 255, 255), sa * (r.enabled ? 1.0f : 0.5f)), v.c_str());
    if (lit && edits && r.enabled) {
      const float ax = x + sw - 22 * u, ay = y + 46 * u + vpx * 0.5f;
      dl->AddTriangleFilled(ImVec2(ax - 6 * u, ay - 4 * u), ImVec2(ax + 6 * u, ay - 4 * u), ImVec2(ax, ay - 13 * u), with_alpha(IM_COL32(255, 203, 0, 255), sa));
      dl->AddTriangleFilled(ImVec2(ax - 6 * u, ay + 4 * u), ImVec2(ax + 6 * u, ay + 4 * u), ImVec2(ax, ay + 13 * u), with_alpha(IM_COL32(255, 203, 0, 255), sa));
    }
    const std::vector<std::string> lines = wrap(r.about, 14 * u, sw - 26 * u);
    float ly = y + 92 * u;
    for (size_t li = 0; li < lines.size() && li < 3; ++li, ly += 17 * u)
      text(dl, x + 14 * u, ly, 14 * u, with_alpha(IM_COL32(215, 219, 230, 255), sa), lines[li].c_str(), 0, false);
  }
  // Page name and buttons above the slots; little marks when there are more slots either side.
  std::string path;
  for (size_t i = 0; i < g_stack.size(); ++i) { if (i) path += " / "; path += page_title(g_stack[i].page); }
  const float py = y0 - 40 * u;
  const float pw = text_width(18 * u, path.c_str()) + 28 * u;
  dl->AddRectFilled(ImVec2(x0, py), ImVec2(x0 + pw, py + 30 * u), with_alpha(IM_COL32(0, 0, 0, 190), a), 15 * u);
  text(dl, x0 + 14 * u, py + 5 * u, 18 * u, with_alpha(kAmber, a), path.c_str());
  const char* spec = g_stack.size() > 1 ? "D:Slot / change|A:Open|B:Back" : "D:Slot / change|A:Open|S:Close";
  const float hw = hints(dl, 0, 0, u * 0.9f, spec, a, false);
  dl->AddRectFilled(ImVec2(x0 + total - hw - 20 * u, py), ImVec2(x0 + total, py + 30 * u), with_alpha(IM_COL32(0, 0, 0, 190), a), 15 * u);
  hints(dl, x0 + total - hw - 10 * u, py + 15 * u, u * 0.9f, spec, a);
  if (lv.top > 0) dl->AddTriangleFilled(ImVec2(x0 - 10 * u, y0 + sh * 0.5f - 12 * u), ImVec2(x0 - 10 * u, y0 + sh * 0.5f + 12 * u), ImVec2(x0 - 22 * u, y0 + sh * 0.5f), with_alpha(kAmber, a));
  if (lv.top + per < count) dl->AddTriangleFilled(ImVec2(x0 + total + 10 * u, y0 + sh * 0.5f - 12 * u), ImVec2(x0 + total + 10 * u, y0 + sh * 0.5f + 12 * u), ImVec2(x0 + total + 22 * u, y0 + sh * 0.5f), with_alpha(kAmber, a));
}

// ---------------------------------------------------------------------------------------------
// Look 8: HUD strip
// Set while the replay menu draws: the strip line shows the replay instead of the practice CPU.
std::string g_strip_replay;

float draw_strip_line(ImDrawList* dl, const Frame& f, float u, float a) {
  const practice_cpu::Status st = practice_cpu::status();
  std::vector<std::pair<std::string, ImU32>> parts;
  if (!g_strip_replay.empty()) {
    parts.push_back({"REPLAY", IM_COL32(255, 255, 255, 255)});
    parts.push_back({g_strip_replay, IM_COL32(255, 255, 255, 255)});
  } else {
  parts.push_back({"PRACTICE", IM_COL32(255, 255, 255, 255)});
  parts.push_back({!g_c.enabled ? "CPU off" : (st.driving ? plan_text(st.plan) : "CPU on"), IM_COL32(255, 255, 255, 255)});
  parts.push_back({top_of(kDefend, g_c.defend_odds, MU_PC_DEF_COUNT), IM_COL32(255, 255, 255, 255)});
  parts.push_back({"Tech " + top_name(kTech, g_c.tech_odds, MU_PC_TECH_COUNT), IM_COL32(255, 255, 255, 255)});
  if (f.search.searching) parts.push_back({f.search.mode + " " + f.search.elapsed, IM_COL32(93, 240, 138, 255)});
  }
  const float px = 16 * u, h = 40 * u, sep = 24 * u;
  float w = 34 * u;
  for (auto& p : parts) w += text_width(px, p.first.c_str()) + sep;
  const float x = f.game_x0 + 12 * u, y = f.game_y0 + 10 * u;
  dl->AddRectFilled(ImVec2(x - 2 * u, y - 2 * u), ImVec2(x + w + 2 * u, y + h + 2 * u), with_alpha(IM_COL32(0, 0, 0, 255), a), h * 0.5f + 2 * u);
  rect_gradient(dl, ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(10, 21, 48, 238), a), with_alpha(IM_COL32(4, 10, 28, 238), a), h * 0.5f);
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(63, 120, 224, 255), a), h * 0.5f, 0, 2 * u);
  const bool live = g_strip_replay.empty() && g_c.enabled;
  const float dot = live && st.driving ? 0.6f + 0.4f * std::sin((float)f.time * 4.0f) : 1.0f;
  dl->AddCircleFilled(ImVec2(x + 18 * u, y + h * 0.5f), 6 * u, with_alpha(live ? kGreen : IM_COL32(120, 128, 150, 255), a * dot), 16);
  float pen = x + 32 * u;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) { dl->AddLine(ImVec2(pen - sep * 0.5f, y + 10 * u), ImVec2(pen - sep * 0.5f, y + h - 10 * u), with_alpha(IM_COL32(255, 255, 255, 50), a), 1.0f * u); }
    text(dl, pen, y + h * 0.5f - px * 0.56f, px, with_alpha(parts[i].second, a), parts[i].first.c_str());
    pen += text_width(px, parts[i].first.c_str()) + sep;
  }
  return y + h;
}

void draw_strip(ImDrawList* dl, const Frame& f, std::vector<Row>& rows, float a) {
  const float gh = f.game_y1 - f.game_y0, u = gh / 720.0f;
  Level& lv = level();
  const float below = draw_strip_line(dl, f, u, 1.0f);
  if (lv.page == Page::Code) {
    draw_keyboard(dl, (f.game_x0 + f.game_x1) * 0.5f, f.game_y0 + gh * 0.44f, u, kBlueKeys, a, kb_title());
    return;
  }
  const int visible = 8, count = (int)rows.size();
  if (lv.cursor < lv.top) lv.top = lv.cursor;
  if (lv.cursor >= lv.top + visible) lv.top = lv.cursor - visible + 1;
  lv.top = std::clamp(lv.top, 0, std::max(0, count - visible));
  const int shown = std::min(visible, count);
  float need = 260 * u;
  for (const Row& r : rows) need = std::max(need, text_width(17 * u, r.label.c_str()) + text_width(17 * u, value_of(r).c_str()) + 64 * u);
  const float w = std::min(need, (f.game_x1 - f.game_x0) * 0.5f), rh = 32 * u;
  const float x = f.game_x0 + 12 * u, y = below + 8 * u;
  const std::vector<std::string> about = wrap(about_now(rows.empty() ? nullptr : &rows[(size_t)lv.cursor]), 14 * u, w - 24 * u);
  const float h = 40 * u + shown * rh + 14 * u + (float)about.size() * 17 * u + 34 * u;
  const float open = ease((float)(f.time - g_page_changed_at) / 0.14f);
  dl->PushClipRect(ImVec2(x - 4 * u, y - 4 * u), ImVec2(x + w + 6 * u, y + 4 * u + h * open), true);
  dl->AddRectFilled(ImVec2(x - 2 * u, y - 2 * u), ImVec2(x + w + 2 * u, y + h + 2 * u), with_alpha(IM_COL32(0, 0, 0, 255), a), 16 * u);
  rect_gradient(dl, ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(10, 21, 48, 244), a), with_alpha(IM_COL32(4, 10, 28, 244), a), 14 * u);
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), with_alpha(IM_COL32(63, 120, 224, 255), a), 14 * u, 0, 2 * u);
  std::string path;
  for (size_t i = 0; i < g_stack.size(); ++i) { if (i) path += " / "; path += page_title(g_stack[i].page); }
  text(dl, x + 14 * u, y + 10 * u, 17 * u, with_alpha(kAmber, a), path.c_str());
  float ry = y + 40 * u;
  for (int i = 0; i < shown; ++i) {
    const int index = lv.top + i;
    const Row& r = rows[(size_t)index];
    const bool lit = index == lv.cursor;
    if (lit) {
      rect_gradient(dl, ImVec2(x + 7 * u, ry), ImVec2(x + w - 7 * u, ry + rh - 2 * u), with_alpha(IM_COL32(255, 207, 58, 60), a), with_alpha(IM_COL32(255, 207, 58, 20), a), 8 * u);
      dl->AddRect(ImVec2(x + 7 * u, ry), ImVec2(x + w - 7 * u, ry + rh - 2 * u), with_alpha(kAmber, a), 8 * u, 0, 2 * u);
    }
    const float da = r.enabled ? a : a * 0.45f;
    text(dl, x + 18 * u, ry + rh * 0.5f - 10.5f * u, 17 * u, with_alpha(IM_COL32(255, 255, 255, 255), da), r.label.c_str());
    std::string v = value_of(r);
    if (r.kind == Kind::Page && v.empty()) v = ">";
    v = fit(v, 17 * u, w - 50 * u - text_width(17 * u, r.label.c_str()));
    text(dl, x + w - 18 * u - text_width(17 * u, v.c_str()), ry + rh * 0.5f - 10.5f * u, 17 * u, with_alpha(kAmber, da), v.c_str());
    ry += rh;
  }
  ry += 6 * u;
  dl->AddLine(ImVec2(x + 14 * u, ry), ImVec2(x + w - 14 * u, ry), with_alpha(IM_COL32(255, 255, 255, 30), a), 1.0f);
  ry += 8 * u;
  for (const std::string& line : about) { text(dl, x + 14 * u, ry, 14 * u, with_alpha(IM_COL32(170, 190, 230, 255), a), line.c_str(), 0, false); ry += 17 * u; }
  hints(dl, x + 14 * u, ry + 14 * u, u * 0.82f, g_stack.size() > 1 ? "A:Pick|B:Back|S:Close" : "A:Pick|B:Fold up", a);
  dl->PopClipRect();
}

}  // namespace

float legend(ImDrawList* dl, float x, float cy, float u, const char* spec, float a, bool draw) {
  return hints(dl, x, cy, u, spec, a, draw);
}
std::vector<std::string> wrap_text(const std::string& s, float px, float width) { return wrap(s, px, width); }

// ---------------------------------------------------------------------------------------------
void opened() {
  if (g_stack.empty()) g_stack.push_back(Level{});
  // Back to the top level unless the player is mid-way through a page.
  while (g_stack.size() > 1 && g_stack.back().page == Page::Code) g_stack.pop_back();
  g_page_changed_at = ImGui::GetTime();
  g_prev_buttons = 0xFFFF;
  g_error.clear();
}

void show_error(const std::string& s) { g_error = s; g_error_until = g_f.time + 4.0; }

ReplaySettings& replay_settings() {
  static ReplaySettings settings;
  return settings;
}

// The replay viewer's panel (Start while watching), in the practice menu's look: take over, the tips,
// and the settings one page down.
ReplayPanel replay_panel(int style, const Frame& f, const ReplayPanelIn& panel_in) {
  static const char* const kLengths[] = {"Combo drops", "10 seconds", "20 seconds", "No limit"};
  static const char* const kDefense[] = {"Smart", "No DI or techs", "Practice CPU", "Recording"};
  ReplayPanel out;
  g_f = f;
  g_c = practice_cpu::config();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  font_ready();
  static bool was_open = false;
  if (!f.open) { was_open = false; return out; }
  if (!was_open) { was_open = true; g_prev_buttons = 0xFFFF; g_page_changed_at = f.time; }
  ReplaySettings& rs = replay_settings();
  enum { kBack = 700, kTake = 701, kTip = 800 };
  static Level panel_level{Page::Replay, 0, 0};
  static Level settings_level{Page::ReplaySettings, 0, 0};
  static bool in_settings = false;
  std::vector<Row> rows;
  if (!in_settings) {
    rows.push_back(action_row("Take over", kTake, panel_in.player,
                              "Play this player yourself from here; the other player does what they did. Left and right change the player."));
    for (size_t i = 0; i < panel_in.tips.size(); ++i) {
      const ReplayTip& t = panel_in.tips[i];
      rows.push_back(action_row((t.when + "  " + t.what).c_str(), kTip + (int)i, t.value,
                                t.advice.empty() ? "Jump a second before it." : t.advice.c_str()));
    }
    rows.push_back(page_row("Settings", Page::ReplaySettings, "", "When a try ends, the opponent once you change things, and the tips."));
    rows.push_back(action_row("Back to the replay", kBack, "", "Close this and keep watching."));
  } else {
  rows.push_back(choice_row("Try ends", &rs.length, kLengths, 4,
                            "When a try goes back to where you took over: when your combo drops, after a set time, or never. Start retries any time."));
  rows.push_back(choice_row("Opponent", &rs.defense, kDefense, 4,
                            "Once you change what happened, the opponent stops following the replay. Smart: defends with everything, never attacks first. "
                            "Practice CPU: your Practice menu settings. Recording: keeps its recorded inputs."));
  rows.push_back(toggle_row("Tips", &rs.tips, "A banner at each moment worth practicing, and the markers along the timeline."));
  }
  Level& lv = in_settings ? settings_level : panel_level;
  lv.cursor = std::clamp(lv.cursor, 0, (int)rows.size() - 1);
  const Input in = read_input(f);
  const bool horizontal = style == kSlots;
  const int count = (int)rows.size();
  const bool prev = horizontal ? in.left : in.up, next = horizontal ? in.right : in.down;
  const bool less = horizontal ? in.down : in.left, more = horizontal ? in.up : in.right;
  if (prev) lv.cursor = (lv.cursor + count - 1) % count;
  if (next) lv.cursor = (lv.cursor + 1) % count;
  Row& lit = rows[(size_t)lv.cursor];
  if (in_settings) {
    if (less) change(Page::ReplaySettings, lit, -1);
    if (more) change(Page::ReplaySettings, lit, 1);
    if (in.b) { in_settings = false; g_page_changed_at = f.time; }
    if (in.start) { in_settings = false; out.close = true; }
  } else {
    if (lit.action == kTake && (less || more)) out.player_step = less ? -1 : 1;
    if (in.a) {
      if (lit.action == kTake) { out.take_over = true; out.close = true; }
      else if (lit.action >= kTip) {
        out.jump = panel_in.tips[(size_t)(lit.action - kTip)].frame;
        out.close = true;
      } else if (lit.kind == Kind::Page) {
        in_settings = true;
        settings_level.cursor = settings_level.top = 0;
        g_page_changed_at = f.time;
      } else if (lit.action == kBack) out.close = true;
    }
    if (in.b || in.start) out.close = true;
  }
  // Draw with the practice menu's own drawers, on a stack holding just this page.
  std::vector<Level> saved;
  saved.swap(g_stack);
  g_stack.push_back(lv);
  const float a = ease((float)(f.time - g_page_changed_at) / 0.10f) * 0.25f + 0.75f;
  switch (style) {
    case kRails: draw_rails(dl, f, rows, a); break;
    case kSlots: draw_slots(dl, f, rows, a); break;
    case kStrip:
      g_strip_replay = in_settings ? "Settings" : "Paused";
      draw_strip(dl, f, rows, a);
      g_strip_replay.clear();
      break;
    default: draw_bars(dl, f, rows, a); break;
  }
  lv = g_stack.back();
  g_stack.swap(saved);
  return out;
}

Result frame(int style, const Frame& f) {
  Result out;
  g_f = f;
  g_c = practice_cpu::config();
  g_c_changed = false;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  font_ready();
  // Tests: MELEE_TEST_PRACTICE_MENU=<page>,<cursor> opens straight on a page.
  static bool test_done = false;
  if (!test_done && f.open) {
    test_done = true;
    if (const char* v = std::getenv("MELEE_TEST_PRACTICE_MENU")) {
      int page = 0, cursor = 0;
      if (std::sscanf(v, "%d,%d", &page, &cursor) >= 1 && page > 0 && page < (int)Page::Count) {
        g_stack.assign(1, Level{});
        g_stack.push_back(Level{(Page)page, cursor, 0});
        if ((Page)page == Page::Code) { g_kb_target = 0; g_kb_text = "HERO#08"; g_kb_cursor = 34; }
      } else g_stack.assign(1, Level{Page::Root, cursor, 0});
      g_page_changed_at = f.time - 1.0;
    }
  }
  if (!f.open) {
    draw_offer(dl, f);
    if (f.hint && style != kStrip) {
      // How to open it, in the game's font at the top right (dimmer after the first 20 seconds).
      const float u = (f.game_y1 - f.game_y0) / 720.0f, alpha = f.time < 20.0 ? 0.95f : 0.5f;
      const char* s = "Practice: Start + Up + Z";
      text(dl, f.game_x1 - text_width(17 * u, s) - 20 * u, f.game_y0 + 18 * u, 17 * u, with_alpha(IM_COL32(255, 255, 255, 255), alpha), s);
    }
    switch (style) {
      case kBars: draw_bars_closed(dl, f); break;
      case kRails: draw_rails_closed(dl, f); break;
      case kStrip: if (f.search.available) draw_strip_line(dl, f, (f.game_y1 - f.game_y0) / 720.0f, 1.0f); break;
      case kSlots: draw_bars_closed(dl, f); break;
      default: break;
    }
    return out;
  }
  if (g_stack.empty()) g_stack.push_back(Level{});
  // Input first, on this frame's rows.
  std::vector<Row> rows = rows_for(level().page);
  Level& lv = level();
  lv.cursor = rows.empty() ? 0 : std::clamp(lv.cursor, 0, (int)rows.size() - 1);
  const Input in = read_input(f);
  if (lv.page == Page::Code) {
    keyboard_input(in, out);
  } else {
    const bool horizontal = style == kSlots;   // slots: left/right pick, up/down change
    const int count = (int)rows.size();
    const bool prev = horizontal ? in.left : in.up, next = horizontal ? in.right : in.down;
    const bool less = horizontal ? in.down : in.left, more = horizontal ? in.up : in.right;
    if (count) {
      if (prev) lv.cursor = (lv.cursor + count - 1) % count;
      if (next) lv.cursor = (lv.cursor + 1) % count;
      Row& r = rows[(size_t)lv.cursor];
      if (less) change(lv.page, r, -1);
      if (more) change(lv.page, r, 1);
      if (in.a) press(lv.page, r, out);
    }
    if (in.b) { if (g_stack.size() > 1) pop(); else out.close = true; }
    if (in.start) out.close = true;
  }
  if (g_c_changed) { practice_cpu::set_config(g_c); out.changed = true; }
  // Draw what the input left.
  rows = rows_for(level().page);
  if (!rows.empty()) level().cursor = std::clamp(level().cursor, 0, (int)rows.size() - 1);
  const float a = ease((float)(f.time - g_page_changed_at) / 0.10f) * 0.25f + 0.75f;
  switch (style) {
    case kRails: draw_rails(dl, f, rows, a); break;
    case kSlots: draw_slots(dl, f, rows, a); break;
    case kStrip: draw_strip(dl, f, rows, a); break;
    default: draw_bars(dl, f, rows, a); break;
  }
  return out;
}

}  // namespace practice_menu
