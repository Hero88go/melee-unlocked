// SPDX-License-Identifier: GPL-2.0-or-later
#include "online_rows.h"
#include "online_rows.inc"
#include <algorithm>
#include <atomic>
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

}  // namespace

bool apply(std::vector<uint8_t>& file, std::string* why) {
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
  // The eight online rows' images: Ranked, Unranked, Direct, Log-in, Log-out, Update, Teams, Party.
  const int rows[] = {33, 34, 58, 59, 60, 61, 62, 63};
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
  auto copy_of = [&](int i) { return std::vector<uint8_t>(base + data[i], base + data[i] + kBytes); };
  const auto teams = copy_of(62), party = copy_of(63), login = copy_of(59);
  // Rows in the menu's order: slots 0-7 show images 33, 34, 58, 62, 63, 59, 60, 61.
  std::memcpy(base + data[33], kYouGameRanked, kBytes);
  std::memcpy(base + data[34], kYouGameDirect, kBytes);
  std::memcpy(base + data[58], kSlippiUnranked, kBytes);
  std::memcpy(base + data[62], kSlippiDirect, kBytes);
  std::memcpy(base + data[63], teams.data(), kBytes);
  std::memcpy(base + data[59], party.data(), kBytes);
  std::memcpy(base + data[60], login.data(), kBytes);
  if (why) why->clear();
  return true;
}

void set_applied(bool applied) { g_applied = applied; }
bool applied() { return g_applied; }

}  // namespace online_rows
