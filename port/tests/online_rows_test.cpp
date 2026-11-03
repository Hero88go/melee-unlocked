// online_rows::apply on a synthetic MnMaAll-shaped archive: the eight online rows' images move and
// the four new labels land; anything that does not match leaves the file untouched.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "online_rows.h"
#include "online_rows.inc"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
constexpr uint32_t kBytes = 176 * 32;
const int kRows[] = {33, 34, 58, 59, 60, 61, 62, 63};

struct Builder {
  std::vector<uint8_t> data;
  std::vector<uint32_t> relocs;
  uint32_t alloc(uint32_t n) { const uint32_t at = (uint32_t)data.size(); data.resize(data.size() + ((n + 31) & ~31u)); return at; }
  void u32(uint32_t o, uint32_t v) { data[o] = uint8_t(v >> 24); data[o + 1] = uint8_t(v >> 16); data[o + 2] = uint8_t(v >> 8); data[o + 3] = uint8_t(v); }
  void u16(uint32_t o, uint16_t v) { data[o] = uint8_t(v >> 8); data[o + 1] = uint8_t(v); }
  void ptr(uint32_t o, uint32_t target) { u32(o, target); relocs.push_back(o); }
};

struct Synthetic { std::vector<uint8_t> file; uint32_t image_data[64]; };

// width: the size the eight rows claim (176 is the real one).
Synthetic make(uint16_t width = 176) {
  Builder b;
  Synthetic s{};
  // Row 63's image sits at offset 0, as in the real file.
  s.image_data[63] = b.alloc(kBytes);
  for (int i : kRows) if (i != 63) s.image_data[i] = b.alloc(kBytes);
  for (int i : kRows) std::memset(b.data.data() + s.image_data[i], i, kBytes);   // each row's own bytes
  const uint32_t descs = b.alloc(64 * 24), table = b.alloc(64 * 4);
  for (int i = 0; i < 64; ++i) {
    const uint32_t d = descs + 24 * i;
    b.ptr(d, s.image_data[(i >= 33 && i <= 34) || i >= 58 ? i : 63]);
    b.u16(d + 4, i == 33 || i == 34 || i >= 58 ? width : 32);
    b.u16(d + 6, 30);
    b.u32(d + 8, 2);
    b.ptr(table + 4 * i, d);
  }
  const uint32_t texanim = b.alloc(24), matanim = b.alloc(16), j0 = b.alloc(12), j1 = b.alloc(12), j2 = b.alloc(12), j3 = b.alloc(12);
  b.ptr(texanim + 12, table);
  b.u16(texanim + 20, 64);
  b.ptr(matanim + 8, texanim);
  b.ptr(j0, j1);        // root.child
  b.ptr(j1, j2);        // .child
  b.ptr(j2 + 4, j3);    // .next
  b.ptr(j3 + 8, matanim);
  const char name[] = "MenMainCursor_Top_matanim_joint";
  std::vector<uint8_t>& f = s.file;
  f.assign(0x20, 0);
  f.insert(f.end(), b.data.begin(), b.data.end());
  auto put = [&](uint32_t v) { f.push_back(uint8_t(v >> 24)); f.push_back(uint8_t(v >> 16)); f.push_back(uint8_t(v >> 8)); f.push_back(uint8_t(v)); };
  for (uint32_t r : b.relocs) put(r);
  put(j0); put(0);   // one root, its name at string offset 0
  f.insert(f.end(), name, name + sizeof name);
  auto set = [&](size_t o, uint32_t v) { f[o] = uint8_t(v >> 24); f[o + 1] = uint8_t(v >> 16); f[o + 2] = uint8_t(v >> 8); f[o + 3] = uint8_t(v); };
  set(0, (uint32_t)f.size()); set(4, (uint32_t)b.data.size()); set(8, (uint32_t)b.relocs.size()); set(12, 1); set(16, 0);
  return s;
}

bool image_is(const Synthetic& s, int row, const uint8_t* want) {
  return std::memcmp(s.file.data() + 0x20 + s.image_data[row], want, kBytes) == 0;
}
bool image_filled(const Synthetic& s, int row, uint8_t value) {
  const uint8_t* p = s.file.data() + 0x20 + s.image_data[row];
  for (uint32_t i = 0; i < kBytes; ++i) if (p[i] != value) return false;
  return true;
}
}  // namespace

int main() {
  {
    Synthetic s = make();
    std::string why;
    CHECK(online_rows::apply(s.file, &why));
    CHECK(why.empty());
    CHECK(image_is(s, 33, online_rows::kYouGameRanked));
    CHECK(image_is(s, 34, online_rows::kYouGameDirect));
    CHECK(image_is(s, 58, online_rows::kSlippiUnranked));
    CHECK(image_is(s, 62, online_rows::kSlippiDirect));
    CHECK(image_filled(s, 63, 62));   // Teams moves down one row
    CHECK(image_filled(s, 59, 63));   // Party
    CHECK(image_filled(s, 60, 59));   // Log in takes the dead Log out row
    CHECK(image_filled(s, 61, 61));   // Update stays
  }
  {
    Synthetic s = make(160);          // not the menu the labels were drawn for
    const std::vector<uint8_t> before = s.file;
    std::string why;
    CHECK(!online_rows::apply(s.file, &why));
    CHECK(why == "label image differs");
    CHECK(s.file == before);
  }
  {
    Synthetic s = make();
    s.file[3] ^= 1;                   // header size no longer matches
    const std::vector<uint8_t> before = s.file;
    std::string why;
    CHECK(!online_rows::apply(s.file, &why));
    CHECK(s.file == before);
  }
  online_rows::set_applied(true);
  CHECK(online_rows::applied());
  std::printf("online_rows: ok\n");
  return 0;
}
