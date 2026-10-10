// Skin pictures: a fighter file from the player's own disc draws as a figure, and damaged files are refused.
// SPDX-License-Identifier: GPL-2.0-or-later
// usage: port_skin_thumbnail_test [melee.iso [out.png]]   (without a disc only the refusals run)
#include "skin_thumbnail.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "stb/stb_image_write.h"

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

uint32_t be32(const std::vector<uint8_t>& b, size_t at) { return (uint32_t)b[at] << 24 | b[at + 1] << 16 | b[at + 2] << 8 | b[at + 3]; }

// One file out of a GameCube disc image, by name.
std::vector<uint8_t> disc_file(const char* iso, const char* name) {
  std::ifstream in(iso, std::ios::binary);
  std::vector<uint8_t> head(0x440);
  if (!in.read((char*)head.data(), head.size())) return {};
  const uint32_t fst_at = be32(head, 0x424), fst_size = be32(head, 0x428);
  if (!fst_size || fst_size > 4u * 1024 * 1024) return {};
  std::vector<uint8_t> fst(fst_size);
  in.seekg(fst_at);
  if (!in.read((char*)fst.data(), fst_size)) return {};
  const uint32_t count = be32(fst, 8);
  for (uint32_t i = 1; i < count && (size_t)i * 12 + 12 <= fst.size(); ++i) {
    const uint32_t word = be32(fst, (size_t)i * 12);
    if (word >> 24) continue;
    const size_t at = (size_t)count * 12 + (word & 0xFFFFFF);
    if (at >= fst.size() || _stricmp((const char*)fst.data() + at, name) != 0) continue;
    std::vector<uint8_t> out(be32(fst, (size_t)i * 12 + 8));
    in.seekg(be32(fst, (size_t)i * 12 + 4));
    if (!in.read((char*)out.data(), out.size())) return {};
    return out;
  }
  return {};
}
}

int main(int argc, char** argv) {
  std::vector<uint8_t> pixels;
  // Not a fighter file at all, and files cut short: refused, nothing read past the end.
  const std::vector<uint8_t> zeros(4096, 0), noise(4096, 0xA5);
  CHECK(!host::render_costume_picture(zeros.data(), zeros.size(), 96, 128, &pixels));
  CHECK(!host::render_costume_picture(noise.data(), noise.size(), 96, 128, &pixels));
  CHECK(!host::render_costume_picture(nullptr, 0, 96, 128, &pixels));
  if (argc > 1) {
    for (const char* name : {"PlMsNr.dat", "PlFxNr.dat", "PlKbNr.dat", "PlPkNr.dat"}) {
      const std::vector<uint8_t> file = disc_file(argv[1], name);
      CHECK(!file.empty());
      if (file.empty()) continue;
      CHECK(host::render_costume_picture(file.data(), file.size(), 192, 256, &pixels));
      size_t covered = 0;
      for (size_t i = 3; i < pixels.size(); i += 4) covered += pixels[i] > 128;
      // A figure, not a blank or a filled square: between a twentieth and nine tenths of the picture.
      CHECK(covered > pixels.size() / 4 / 20 && covered < pixels.size() / 4 * 9 / 10);
      if (argc > 2) stbi_write_png((std::string(argv[2]) + "-" + name + ".png").c_str(), 192, 256, 4, pixels.data(), 192 * 4);
      // Every shorter copy of the file is refused or drawn, never a crash.
      for (size_t cut : {file.size() / 2, file.size() / 3, (size_t)0x40, (size_t)0x1000})
        host::render_costume_picture(file.data(), cut, 96, 128, &pixels);
      // And a copy with its pointers scrambled.
      std::vector<uint8_t> bad = file;
      for (size_t i = 0x20; i + 4 < bad.size(); i += 97) bad[i] ^= 0x5A;
      host::render_costume_picture(bad.data(), bad.size(), 96, 128, &pixels);
    }
  }
  if (g_failures) { std::printf("%d failure(s)\n", g_failures); return 1; }
  std::printf("skin thumbnails: ok\n");
  return 0;
}
