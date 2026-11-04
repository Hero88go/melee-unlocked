// SPDX-License-Identifier: GPL-2.0-or-later
#include "online_rows.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace online_rows {
namespace {

std::atomic<bool> g_applied{false};

constexpr uint32_t kWidth = 176, kHeight = 30, kBytes = 176 * 32;   // IA4, rows padded to 4
constexpr uint32_t kFormatIA4 = 2;
constexpr uint16_t kImages = 64;
constexpr const char* kRoot = "MenMainCursor_Top_matanim_joint";

struct Archive {
  const std::vector<uint8_t>& f;
  uint32_t data_size = 0;
  std::unordered_set<uint32_t> relocs;
  uint32_t u32(uint32_t o) const { const uint8_t* p = f.data() + 0x20 + o; return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
  uint16_t u16(uint32_t o) const { const uint8_t* p = f.data() + 0x20 + o; return uint16_t(p[0] << 8 | p[1]); }
  bool in(uint32_t o, uint32_t n) const { return uint64_t(o) + n <= data_size; }
  // A pointer field: only an offset the relocation table lists holds a pointer.
  uint32_t ptr(uint32_t o) const { return in(o, 4) && relocs.count(o) ? u32(o) : 0; }
};

bool fail(std::string* why, const char* what) { if (why) *why = what; return false; }

// A label as alpha and intensity nibbles (0-15), the IA4 texel split, row by row.
struct Label {
  std::array<uint8_t, kWidth * kHeight> a{}, l{};
};
uint32_t tile_index(uint32_t x, uint32_t y) {   // GX IA4: 8x4 tiles, one byte per texel
  return ((y / 4) * (kWidth / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8;
}
Label decode(const uint8_t* p) {
  Label out;
  for (uint32_t y = 0; y < kHeight; ++y)
    for (uint32_t x = 0; x < kWidth; ++x) {
      const uint8_t v = p[tile_index(x, y)];
      out.a[y * kWidth + x] = v >> 4;
      out.l[y * kWidth + x] = v & 15;
    }
  return out;
}
void encode(const Label& in, uint8_t* p) {
  std::memset(p, 0, kBytes);   // the padding rows stay empty
  for (uint32_t y = 0; y < kHeight; ++y)
    for (uint32_t x = 0; x < kWidth; ++x)
      p[tile_index(x, y)] = uint8_t(in.a[y * kWidth + x] << 4 | in.l[y * kWidth + x]);
}
uint32_t fnv1a(const uint8_t* p, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
  return h;
}

// The labels the letters come from, as Melee 1.02 with Slippi's menu files has them (English).
constexpr struct { uint8_t image; uint32_t hash; } kSources[] = {
    {1, 0x7E718C33u},  {19, 0x8ACD1283u}, {20, 0xFA6049D4u}, {24, 0x24A09B0Du}, {26, 0x745BEA37u},
    {29, 0xC3650ACBu}, {33, 0xA933D462u}, {34, 0x97FF741Au}, {44, 0x95433836u}};

// ---- Slippi: one label's own letters, cut whole with their outline ----

// A letter cut from one of the menu's own labels: the columns cut (to the middle of the gap to each
// neighbour, so its outline comes along but no neighbour's fill) and the columns its white fill spans.
struct Letter { uint8_t image, cut0, cut1, fill0, fill1; };
constexpr int kGap = 3;   // columns between two letters' fills, as the menu's own words space them
const Letter kSlippiWord[] = {{29, 34, 56, 37, 54}, {29, 133, 142, 135, 139}, {29, 107, 115, 109, 113},
                              {29, 56, 73, 58, 72}, {29, 56, 73, 58, 72},     {29, 107, 115, 109, 113}};

// A word set from the menu's own letters, centred like the menu's labels. Where two letters' edges
// meet, the white fill wins over the neighbour's black outline.
template <size_t N>
Label word(const Label* sources, const Letter (&letters)[N]) {
  int fills = int(N - 1) * kGap;   // from the first fill's start to the last fill's end
  for (const Letter& g : letters) fills += g.fill1 - g.fill0;
  Label out;
  int x = (int(kWidth) - fills) / 2;   // where the next letter's fill starts
  for (const Letter& g : letters) {
    const Label& src = sources[g.image];
    for (int sx = g.cut0; sx < g.cut1; ++sx) {
      const int px = x - (g.fill0 - g.cut0) + (sx - g.cut0);
      if (px < 0 || px >= int(kWidth)) continue;
      for (uint32_t y = 0; y < kHeight; ++y) {
        const uint8_t a = src.a[y * kWidth + size_t(sx)], l = src.l[y * kWidth + size_t(sx)];
        const size_t o = y * kWidth + size_t(px);
        if (!a) continue;
        out.l[o] = out.a[o] ? std::max(out.l[o], l) : l;
        out.a[o] = std::max(out.a[o], a);
      }
    }
    x += g.fill1 - g.fill0 + kGap;
  }
  return out;
}

// ---- YouGame's rows: letter shapes from several labels, one line, one outline ----
//
// The letters come from labels that share one baseline (x-height rows 11-24); the game's own labels
// draw capitals and ascenders one row taller than Slippi's (rows 4-24 against 5-24), so those lose a
// row in their upper part. Only the white shapes are taken: the line is narrowed to the label like
// the menu's long labels ("Home-Run Contest") and then gets one even outline, as the menu draws it.

// White coverage 0-1, kHeight rows of `w` columns (row-major).
struct Shape {
  int w = 0;
  std::vector<float> f;
  float& at(int x, int y) { return f[size_t(y) * size_t(w) + size_t(x)]; }
  float at(int x, int y) const { return f[size_t(y) * size_t(w) + size_t(x)]; }
};
float white(const Label& src, int x, int y) {
  const size_t o = size_t(y) * kWidth + size_t(x);
  return float(src.a[o]) * float(src.l[o]) / 225.0f;
}
// One row out of the upper part (row 8), a blank row on top: a capital as tall as Slippi's.
void drop_row(Shape& s) {
  for (int y = 8; y > 0; --y)
    for (int x = 0; x < s.w; ++x) s.at(x, y) = s.at(x, y - 1);
  for (int x = 0; x < s.w; ++x) s.at(x, 0) = 0.0f;
}
// The fill of columns fill0..fill1 of a label, with one column of soft edge each side.
Shape shape(const Label& src, int fill0, int fill1, bool taller) {
  Shape s;
  s.w = fill1 - fill0 + 3;
  s.f.assign(size_t(s.w) * kHeight, 0.0f);
  for (int x = 0; x < s.w; ++x)
    for (int y = 0; y < int(kHeight); ++y) {
      const float v = white(src, fill0 - 1 + x, y);
      s.at(x, y) = (x == 0 || x == s.w - 1) ? std::min(v, 0.6f) : v;
    }
  if (taller) drop_row(s);
  return s;
}
// The Y (the menu has none): the V of "VS. Mode" with its left arm moved in row by row so the arms
// meet halfway down, the right arm its mirror, and the meeting row repeated down as the stem.
Shape make_y(const Label& vs) {
  constexpr int kJoin = 13;
  const Shape v = shape(vs, 21, 39, false);
  Shape y;
  y.w = v.w;
  y.f.assign(v.f.size(), 0.0f);
  const int mid = (v.w - 1) / 2;
  for (int row = 0; row <= kJoin; ++row) {
    const int sh = int(std::lround(std::max(0, row - 4) * 4.0 / (kJoin - 4)));
    for (int x = 0; x <= mid; ++x) {
      const float c = v.at(x, row);
      if (x + sh < y.w) y.at(x + sh, row) = std::max(y.at(x + sh, row), c);
      if (v.w - 1 - x - sh >= 0) y.at(v.w - 1 - x - sh, row) = std::max(y.at(v.w - 1 - x - sh, row), c);
    }
  }
  int bottom = kJoin;
  for (int row = kJoin; row < int(kHeight); ++row)
    if (v.at(mid, row) > 0.7f) bottom = row;
  for (int row = kJoin + 1; row <= bottom; ++row)
    for (int x = 0; x < y.w; ++x) y.at(x, row) = y.at(x, kJoin);
  drop_row(y);
  return y;
}
// The F (the menu's only capital F is in a condensed label): the E of "Erase Data" without its
// bottom bar.
Shape make_f(const Label& erase) {
  Shape f = shape(erase, 11, 24, false);
  int left = -1, stem = 0;   // the stem's columns, from a row with only the stem
  for (int x = 0; x < f.w; ++x)
    if (f.at(x, 20) > 0.7f) { if (left < 0) left = x; ++stem; }
  for (int row = 16; row < int(kHeight); ++row)
    for (int x = left + stem + 1; x < f.w; ++x) f.at(x, row) = 0.0f;
  drop_row(f);
  return f;
}

// A part of a line: a label's letter (taller: from the game's own labels, see above), a made letter,
// or the gap between two words.
struct Part { uint8_t image; uint8_t fill0, fill1; bool taller; };
constexpr uint8_t kMadeY = 250, kMadeF = 251, kWordSpace = 252;
#define YOUGAME {kMadeY, 0, 0, false}, {20, 66, 79, true}, {20, 83, 95, true}, {44, 19, 33, true}, \
                {44, 44, 54, true}, {19, 74, 94, true}, {19, 125, 138, true}, {kWordSpace, 0, 0, false}
const Part kRankedLine[] = {YOUGAME, {33, 41, 134, false}};
const Part kUnrankedLine[] = {YOUGAME, {34, 27, 148, false}};
const Part kFriendsLine[] = {YOUGAME, {kMadeF, 0, 0, false}, {26, 49, 57, true}, {26, 96, 99, true}, {26, 120, 133, true},
                             {34, 47, 59, false}, {34, 136, 148, false}, {26, 137, 148, true}};
#undef YOUGAME
constexpr int kWordGap = 12;   // fill to fill between two words, as "Event Match" has it
constexpr int kOutline = 4;    // columns the outline adds each side
constexpr int kGreyPct = 55;   // a greyed-out row's letters, against the menu's white

template <size_t N>
Label line(const Label* sources, const Part (&parts)[N]) {
  constexpr int kCanvas = 640;
  std::vector<float> canvas(size_t(kCanvas) * kHeight, 0.0f);   // row-major
  int x = 10, lo = -1, hi = 0;
  for (const Part& p : parts) {
    if (p.image == kWordSpace) { x += kWordGap - kGap; continue; }
    const Shape s = p.image == kMadeY ? make_y(sources[1])
                    : p.image == kMadeF ? make_f(sources[24])
                                        : shape(sources[p.image], p.fill0, p.fill1, p.taller);
    const int left = x - 1;
    int first = -1, last = -1;
    for (int dx = 0; dx < s.w; ++dx)
      for (int y = 0; y < int(kHeight); ++y) {
        const float v = s.at(dx, y);
        float& c = canvas[size_t(y) * kCanvas + size_t(left + dx)];
        c = std::max(c, v);
        if (v > 0.5f) { if (first < 0) first = dx; last = dx; }
      }
    if (lo < 0) lo = left + first;
    hi = left + last;
    x = hi + kGap;
  }
  // Narrow the white to the label (each column averages the canvas columns it covers).
  const int src0 = lo - 1, src_w = hi - lo + 3;
  const int out_w = std::min(src_w, int(kWidth) - 2 * kOutline), off = (int(kWidth) - out_w) / 2;
  std::vector<float> fill(size_t(kWidth) * kHeight, 0.0f);
  for (int dx = 0; dx < out_w; ++dx) {
    const double x0 = double(dx) * src_w / out_w, x1 = double(dx + 1) * src_w / out_w;
    for (int y = 0; y < int(kHeight); ++y) {
      double acc = 0, tot = 0;
      for (int sx = int(x0); sx < x1 && sx < src_w; ++sx) {
        const double cover = std::min(x1, double(sx + 1)) - std::max(x0, double(sx));
        acc += canvas[size_t(y) * kCanvas + size_t(src0 + sx)] * cover;
        tot += cover;
      }
      fill[size_t(y) * kWidth + size_t(off + dx)] = float(acc / tot);
    }
  }
  // The outline: full within two pixels of the white, a soft third, as on the menu's labels.
  Label out;
  for (int y = 0; y < int(kHeight); ++y)
    for (int px = 0; px < int(kWidth); ++px) {
      float d = 99.0f;
      for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx) {
          const int sy = y + dy, sx = px + dx;
          if (sy < 0 || sy >= int(kHeight) || sx < 0 || sx >= int(kWidth)) continue;
          if (fill[size_t(sy) * kWidth + size_t(sx)] >= 0.5f) d = std::min(d, std::sqrt(float(dx * dx + dy * dy)));
        }
      const float w = fill[size_t(y) * kWidth + size_t(px)];
      const float a = std::max(w, std::clamp(3.4f - d, 0.0f, 1.0f));
      const size_t o = size_t(y) * kWidth + size_t(px);
      out.a[o] = uint8_t(std::lround(a * 15.0f));
      out.l[o] = a > 0.0f ? uint8_t(std::lround(w / a * 15.0f)) : 0;
    }
  return out;
}

}  // namespace

bool apply(std::vector<uint8_t>& file, std::string* why, bool coming_soon) {
  if (file.size() < 0x20) return fail(why, "too small");
  auto be = [&](size_t o) { return uint32_t(file[o]) << 24 | uint32_t(file[o + 1]) << 16 | uint32_t(file[o + 2]) << 8 | file[o + 3]; };
  if (be(0) != file.size()) return fail(why, "header size differs from the file");
  Archive a{file};
  a.data_size = be(4);
  const uint32_t nreloc = be(8), nroot = be(12), nref = be(16);
  const uint64_t reloc_at = 0x20ull + a.data_size, roots_at = reloc_at + 4ull * nreloc,
                 strings_at = roots_at + 8ull * (nroot + nref);
  if (strings_at > file.size()) return fail(why, "tables past the end");
  for (uint32_t i = 0; i < nreloc; ++i) a.relocs.insert(be(size_t(reloc_at + 4ull * i)));
  uint32_t root = 0;
  bool found = false;
  for (uint32_t i = 0; i < nroot + nref && !found; ++i) {
    const uint32_t off = be(size_t(roots_at + 8ull * i)), name = be(size_t(roots_at + 8ull * i + 4));
    const uint64_t s = strings_at + name;
    if (s >= file.size()) continue;
    const char* str = reinterpret_cast<const char*>(file.data() + s);
    if (strnlen(str, file.size() - size_t(s)) == std::strlen(kRoot) && std::strcmp(str, kRoot) == 0) { root = off; found = true; }
  }
  if (!found) return fail(why, "no menu cursor animation");
  // The labels' texture animation: root, its child, that child's child, then its next sibling.
  uint32_t j = a.ptr(root);                      // MatAnimJoint.child
  j = j ? a.ptr(j) : 0;                          // its child
  j = j ? a.ptr(j + 4) : 0;                      // next sibling
  const uint32_t matanim = j ? a.ptr(j + 8) : 0;
  const uint32_t texanim = matanim ? a.ptr(matanim + 8) : 0;
  if (!texanim || !a.in(texanim, 24)) return fail(why, "no label animation");
  const uint32_t table = a.ptr(texanim + 12);
  if (!table || a.u16(texanim + 20) != kImages || !a.in(table, 4u * kImages)) return fail(why, "label count differs");
  // The eight online rows' images (Ranked, Unranked, Direct, Log-in, Log-out, Update, Teams, Party),
  // the labels the new words take their letters from, and Online Play (the fallback Slippi row).
  const int rows[] = {33, 34, 58, 59, 60, 61, 62, 63, 1, 19, 20, 24, 26, 29, 44, 7};
  uint32_t data[64] = {};
  for (int i : rows) {
    const uint32_t desc = a.ptr(table + 4u * i);
    if (!desc || !a.in(desc, 12)) return fail(why, "label image missing");
    // Offset 0 is a real place (the start of the data): the relocation entry says it is a pointer.
    if (!a.relocs.count(desc)) return fail(why, "label image missing");
    data[i] = a.u32(desc);
    if (a.u16(desc + 4) != kWidth || a.u16(desc + 6) != kHeight || a.u32(desc + 8) != kFormatIA4 ||
        !a.in(data[i], kBytes))
      return fail(why, "label image differs");
  }
  uint8_t* base = file.data() + 0x20;
  // Slippi's own labels, exactly as Slippi draws them, kept before any row is rewritten.
  std::vector<uint8_t> unranked(base + data[34], base + data[34] + kBytes), direct(base + data[58], base + data[58] + kBytes),
      teams(base + data[62], base + data[62] + kBytes), party(base + data[63], base + data[63] + kBytes),
      online_play(base + data[7], base + data[7] + kBytes);
  // The four new labels (YouGame Ranked, YouGame Unranked, YouGame Friends, Slippi), set from the
  // menu's own letters when those labels are the ones the letters were measured on (English menu
  // files); otherwise Ranked, Unranked and Direct as Slippi has them, and the game's Online Play.
  bool letters_ok = true;
  for (const auto& s : kSources) letters_ok &= fnv1a(base + data[s.image], kBytes) == s.hash;
  if (letters_ok) {
    std::vector<Label> src(64);
    for (const auto& s : kSources) src[s.image] = decode(base + data[s.image]);
    encode(line(src.data(), kRankedLine), base + data[33]);
    encode(line(src.data(), kUnrankedLine), base + data[34]);
    encode(line(src.data(), kFriendsLine), base + data[58]);
    encode(word(src.data(), kSlippiWord), base + data[62]);
  } else {
    std::memcpy(base + data[62], online_play.data(), kBytes);   // Friends keeps Slippi's Direct label
  }
  // Rows in the menu's order: slots 0-7 show images 33, 34, 58, 62, 63, 59, 60, 61. Top page: YouGame
  // Ranked, YouGame Unranked, YouGame Friends, Slippi. Slippi's page: its own four labels, unchanged.
  std::memcpy(base + data[63], unranked.data(), kBytes);
  std::memcpy(base + data[59], direct.data(), kBytes);
  std::memcpy(base + data[60], teams.data(), kBytes);
  std::memcpy(base + data[61], party.data(), kBytes);
  // Coming soon: YouGame's three rows greyed out (their white letters dimmed, the outline kept); the
  // sticker over them is the host's overlay.
  if (coming_soon)
    for (int i : {33, 34, 58}) {
      Label g = decode(base + data[i]);
      for (uint8_t& l : g.l) l = uint8_t((l * kGreyPct + 50) / 100);
      encode(g, base + data[i]);
    }
  if (why) why->clear();
  return true;
}

void set_applied(bool applied) { g_applied = applied; }
bool applied() { return g_applied; }

}  // namespace online_rows
