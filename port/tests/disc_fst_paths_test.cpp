#include "disc_fst_paths.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  bytes[at] = (uint8_t)(value >> 24);
  bytes[at + 1] = (uint8_t)(value >> 16);
  bytes[at + 2] = (uint8_t)(value >> 8);
  bytes[at + 3] = (uint8_t)value;
}
std::vector<uint8_t> sample_fst() {
  constexpr size_t entries = 4, strings = entries * 12;
  const char names[] = "audio\0menu01.hps\0stage.hps\0other.dat\0";
  std::vector<uint8_t> fst(strings + sizeof names);
  put32(fst, 8, (uint32_t)entries);
  put32(fst, 12, 0x01000000u); put32(fst, 20, 4); // /audio directory
  put32(fst, 24, 6); put32(fst, 28, 0x1000); put32(fst, 32, 123);
  put32(fst, 36, 17); put32(fst, 40, 0x2000); put32(fst, 44, 456);
  std::memcpy(fst.data() + strings, names, sizeof names);
  return fst;
}
}

int main() {
  const auto fst = sample_fst();
  std::string path;
  if (!host::disc_fst::path_by_offset(fst, 0x1000, &path) || path != "/audio/menu01.hps") return 1;
  std::vector<std::string> paths;
  if (!host::disc_fst::music_paths(fst, &paths) || paths.size() != 2) return 2;
  if (paths[0] != "/audio/menu01.hps" || paths[1] != "/audio/stage.hps") return 3;
  if (host::disc_fst::path_by_offset(fst, 0xDEAD, &path)) return 4;
  auto bad = fst;
  put32(bad, 24, 0xFFFFFFu);
  if (host::disc_fst::music_paths(bad, &paths)) return 5;
  std::puts("disc FST path tests passed");
  return 0;
}
