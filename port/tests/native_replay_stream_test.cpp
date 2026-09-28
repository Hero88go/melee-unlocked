// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_replay_stream.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
int main(int argc, char** argv) {
  slippi::NativeReplayStream stream;
  std::vector<uint8_t> start(0x2F8);
  start[0] = 3; start[1] = 19;
  CHECK(!stream.begin({}, {}));
  CHECK(stream.begin(start, {}));
  const uint8_t frame[] = {255,255,255,133,0,0,0,42,0,0,0,0};
  CHECK(stream.append(0x3A, frame, sizeof frame));
  const uint8_t bookend[] = {255,255,255,133,255,255,255,133};
  CHECK(stream.append(0x3C, bookend, sizeof bookend));
  CHECK(stream.last_frame() == -123);
  auto encoded = stream.encode();
  CHECK(!encoded.empty());
  const size_t raw_end = 15 + be32(encoded.data() + 11);
  CHECK(raw_end < encoded.size());
  CHECK(encoded[raw_end] == 'U');
  std::map<uint8_t, unsigned> sizes;
  for (size_t i = 17; i < 16 + encoded[16]; i += 3)
    sizes[encoded[i]] = unsigned(encoded[i+1]) << 8 | encoded[i+2];
  CHECK(sizes[0x36] == start.size());
  CHECK(sizes.count(0x3D) && sizes[0x3D] == 0);
  CHECK(sizes[0x3A] == 12 && sizes[0x3C] == 8);
  if (argc > 1) {
    std::ofstream file(argv[1], std::ios::binary);
    file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    CHECK(file.good());
  }
  CHECK(!stream.append(0x3A, frame, 8));
  CHECK(stream.encode().empty());
  CHECK(stream.begin(start, {}));
  CHECK(!stream.append(0x37, nullptr, 66));
  CHECK(stream.begin(start, {}));
  const uint8_t end[] = {2,255};
  CHECK(stream.append(0x39, end, sizeof end));
  CHECK(!stream.append(0x3C, bookend, sizeof bookend));
  CHECK(stream.begin(start, std::vector<uint8_t>(65536, 0xAB)));
  encoded = stream.encode();
  CHECK(encoded[16] == 7); // Start + splitter, no wrapped 16-bit Gecko size.
  CHECK(encoded[17] == 0x10 && encoded[18] == 2 && encoded[19] == 4);
  CHECK(!stream.begin(start, std::vector<uint8_t>(16 * 1024 * 1024 + 1)));
  std::puts("native replay stream tests passed");
}
