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

// A letter cut from one of the menu's own labels: the columns cut (to the middle of the gap to each
// neighbour, so its outline comes along but no neighbour's fill), the columns its white fill spans,
// and a width scale (the only capital F the menu has is in a condensed label). No pixels are kept
// here; the letters come from the player's own disc as the menu file is built. A whole label (Ranked,
// Unranked) is one "letter"; kSpace stands for the gap between two words.
struct Letter { uint8_t image, cut0, cut1, fill0, fill1; uint8_t scale_pct = 100; };
constexpr Letter kSpace = {0, 0, 0, 0, 0, 0};
constexpr uint8_t kY = 255;   // the Y of YouGame, made from the V of "VS. Mode" (the menu has no Y)
constexpr int kGap = 3;       // columns between two letters' fills, as the menu's own words space them
constexpr int kWordGap = 11;
constexpr int kGreyPct = 55;  // a greyed-out row's letters, against the menu's white  // between two words, as "Event Match" and "Regular Match" have it
// The labels the letters come from, as Melee 1.02 with Slippi's menu files has them (English).
constexpr struct { uint8_t image; uint32_t hash; } kSources[] = {
    {1, 0x7E718C33u},  {15, 0x1BCF3D50u}, {20, 0xFA6049D4u}, {26, 0x745BEA37u}, {29, 0xC3650ACBu},
    {33, 0xA933D462u}, {34, 0x97FF741Au}, {47, 0xDA03AAF9u}, {60, 0x2AA44EB1u}, {62, 0x082E8B42u}};
// "YouGame": Y (made), o (Log-out), u (Sound), G (Gallery), a (Ranked), m (Teams), e (Ranked).
#define YOUGAME {kY, 0, 0, 0, 0}, {60, 55, 70, 56, 69}, {20, 81, 97, 83, 96}, {15, 33, 59, 39, 58}, \
                {33, 58, 74, 59, 73}, {62, 93, 117, 95, 116}, {33, 105, 121, 107, 120}, kSpace
#define FRIENDS {47, 0, 12, 2, 11, 145}, {26, 47, 60, 49, 59}, {26, 93, 101, 96, 100}, {26, 119, 135, 120, 134}, \
                {34, 45, 61, 47, 60}, {34, 135, 153, 136, 150}, {26, 135, 152, 137, 149}
const Letter kRankedWord[] = {YOUGAME, {33, 35, 141, 41, 135}};
const Letter kUnrankedWord[] = {YOUGAME, {34, 21, 155, 27, 149}};
const Letter kFriendsWord[] = {YOUGAME, FRIENDS};
const Letter kSlippiWord[] = {{29, 34, 56, 37, 54}, {29, 133, 142, 135, 139}, {29, 107, 115, 109, 113},
                              {29, 56, 73, 58, 72}, {29, 56, 73, 58, 72},     {29, 107, 115, 109, 113}};
#undef YOUGAME
#undef FRIENDS

// One letter's columns (with its outline), stretched to scale_pct of its width; `lead` and `fill`
// are where its fill starts and how wide it is, after the stretch.
struct Columns { int width = 0, lead = 0, fill = 0; std::vector<uint8_t> a, l; };   // column-major
Columns cut(const Label& src, const Letter& g) {
  const int x0 = g.cut0, x1 = std::min(int(kWidth), int(g.cut1));
  const int w = x1 - x0, out_w = std::max(1, (w * g.scale_pct + 50) / 100);
  Columns c;
  c.width = out_w;
  c.lead = (int(g.fill0 - g.cut0) * g.scale_pct + 50) / 100;
  c.fill = (int(g.fill1 - g.fill0) * g.scale_pct + 50) / 100;
  c.a.resize(size_t(out_w) * kHeight);
  c.l.resize(size_t(out_w) * kHeight);
  for (int dx = 0; dx < out_w; ++dx) {
    const float sx = (dx + 0.5f) * float(w) / float(out_w) - 0.5f;
    const int s0 = std::clamp(int(sx), 0, w - 1), s1 = std::min(s0 + 1, w - 1);
    const float f = std::clamp(sx - float(s0), 0.0f, 1.0f);
    for (uint32_t y = 0; y < kHeight; ++y) {
      const size_t a0 = y * kWidth + size_t(x0 + s0), a1 = y * kWidth + size_t(x0 + s1);
      c.a[size_t(dx) * kHeight + y] = uint8_t(src.a[a0] + (src.a[a1] - src.a[a0]) * f + 0.5f);
      c.l[size_t(dx) * kHeight + y] = uint8_t(src.l[a0] + (src.l[a1] - src.l[a0]) * f + 0.5f);
    }
  }
  return c;
}

// The Y: the V of "VS. Mode" (columns 13-40, its point under column 30). Its left arm moves in row by
// row so the two arms meet halfway down, the right arm is its mirror, and below the meeting row its
// middle (one stroke with its outline) runs down as the stem to the V's own bottom edge.
Columns make_y(const Label& vs) {
  constexpr int kX0 = 13, kMid = 30 - kX0, kN = 2 * kMid + 1;
  constexpr uint32_t kJoin = 13;
  Columns c;
  c.width = kN;
  c.a.assign(size_t(kN) * kHeight, 0);
  c.l.assign(size_t(kN) * kHeight, 0);
  auto put = [&](int x, uint32_t y, uint8_t a, uint8_t l) {
    if (x < 0 || x >= kN || !a) return;
    const size_t o = size_t(x) * kHeight + y;
    c.l[o] = c.a[o] ? std::max(c.l[o], l) : l;
    c.a[o] = std::max(c.a[o], a);
  };
  auto src = [&](int x, uint32_t y) { return y * kWidth + size_t(kX0 + x); };
  for (uint32_t y = 0; y <= kJoin; ++y) {
    const int s = int(std::lround(std::max(0, int(y) - 3) * 4.0 / (kJoin - 3)));
    for (int x = 0; x <= kMid; ++x) {
      put(x + s, y, vs.a[src(x, y)], vs.l[src(x, y)]);
      put(kN - 1 - x - s, y, vs.a[src(x, y)], vs.l[src(x, y)]);
    }
  }
  uint32_t bottom = kJoin;   // the V's last row of white fill under its point
  for (uint32_t y = kJoin; y < kHeight; ++y)
    if (vs.a[src(kMid, y)] >= 12 && vs.l[src(kMid, y)] >= 12) bottom = y;
  for (uint32_t y = kJoin + 1; y < kHeight; ++y)
    for (int x = kMid - 5; x <= kMid + 5; ++x) {
      const size_t o = size_t(x) * kHeight + y;
      if (y <= bottom) { c.a[o] = c.a[size_t(x) * kHeight + kJoin]; c.l[o] = c.l[size_t(x) * kHeight + kJoin]; }
      else { c.a[o] = vs.a[src(x, y)]; c.l[o] = vs.l[src(x, y)]; }
    }
  int f0 = kN, f1 = -1;
  for (int x = 0; x < kN; ++x)
    for (uint32_t y = 0; y < kHeight; ++y)
      if (c.a[size_t(x) * kHeight + y] >= 12 && c.l[size_t(x) * kHeight + y] >= 12) { f0 = std::min(f0, x); f1 = std::max(f1, x); }
  c.lead = f0;
  c.fill = f1 - f0 + 1;
  return c;
}

// A line set from the menu's own letters. Where two letters' edges meet, the white fill wins over
// the neighbour's black outline. A line that fits is centred like the menu's labels; a longer one is
// narrowed to the label's width, as the menu's own long labels ("Home-Run Contest") are.
template <size_t N>
Label word(const Label* sources, const Letter (&letters)[N]) {
  constexpr int kCanvas = 512;
  std::vector<uint8_t> ca(size_t(kCanvas) * kHeight), cl(size_t(kCanvas) * kHeight);   // column-major
  int x = 32, fill_lo = -1, fill_hi = 0, lo = kCanvas, hi = -1;
  for (const Letter& g : letters) {
    if (g.image == 0 && g.cut1 == 0) { x += kWordGap - kGap; continue; }
    const Columns c = g.image == kY ? make_y(sources[1]) : cut(sources[g.image], g);
    if (fill_lo < 0) fill_lo = x;
    fill_hi = x + c.fill;
    const int left = x - c.lead;
    for (int dx = 0; dx < c.width; ++dx) {
      const int px = left + dx;
      if (px < 0 || px >= kCanvas) continue;
      for (uint32_t y = 0; y < kHeight; ++y) {
        const uint8_t a = c.a[size_t(dx) * kHeight + y], l = c.l[size_t(dx) * kHeight + y];
        const size_t o = size_t(px) * kHeight + y;
        if (!a) continue;
        cl[o] = ca[o] ? std::max(cl[o], l) : l;
        ca[o] = std::max(ca[o], a);
        lo = std::min(lo, px);
        hi = std::max(hi, px);
      }
    }
    x += c.fill + kGap;
  }
  Label out;
  if (hi < lo) return out;
  const int ink = hi - lo + 1;
  if (ink <= int(kWidth)) {
    const int shift = (int(kWidth) - (fill_hi - fill_lo)) / 2 - fill_lo;
    for (int px = lo; px <= hi; ++px) {
      const int ox = px + shift;
      if (ox < 0 || ox >= int(kWidth)) continue;
      for (uint32_t y = 0; y < kHeight; ++y) {
        out.a[y * kWidth + size_t(ox)] = ca[size_t(px) * kHeight + y];
        out.l[y * kWidth + size_t(ox)] = cl[size_t(px) * kHeight + y];
      }
    }
    return out;
  }
  // Narrowed: each label column averages the canvas columns it covers (intensity weighted by alpha).
  const int out_w = int(kWidth) - 2, off = 1;
  for (int dx = 0; dx < out_w; ++dx) {
    const double x0 = lo + double(dx) * ink / out_w, x1 = lo + double(dx + 1) * ink / out_w;
    for (uint32_t y = 0; y < kHeight; ++y) {
      double wa = 0, wl = 0, tot = 0;
      for (int sx = int(x0); sx < x1 && sx <= hi; ++sx) {
        const double cover = std::min(x1, double(sx + 1)) - std::max(x0, double(sx));
        const size_t o = size_t(sx) * kHeight + y;
        wa += ca[o] * cover;
        wl += double(cl[o]) * ca[o] * cover;
        tot += cover;
      }
      const size_t o = y * kWidth + size_t(off + dx);
      out.a[o] = uint8_t(std::clamp(std::lround(wa / tot), 0L, 15L));
      out.l[o] = wa > 0 ? uint8_t(std::clamp(std::lround(wl / wa), 0L, 15L)) : 0;
    }
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
  const int rows[] = {33, 34, 58, 59, 60, 61, 62, 63, 1, 15, 20, 26, 29, 47, 7};
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
    encode(word(src.data(), kRankedWord), base + data[33]);
    encode(word(src.data(), kUnrankedWord), base + data[34]);
    encode(word(src.data(), kFriendsWord), base + data[58]);
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
