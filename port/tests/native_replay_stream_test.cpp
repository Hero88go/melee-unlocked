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
  {
    // The live copy (the Slippi spectator server) is the raw element of the .slp, byte for byte:
    // the command table, Game Start, the split Gecko list and every event, in order.
    slippi::NativeReplayStream live;
    std::vector<uint8_t> sent;
    size_t calls = 0;
    live.set_event_sink([&](const uint8_t* data, uint32_t size) { sent.insert(sent.end(), data, data + size); ++calls; });
    std::vector<uint8_t> slippi_start(0x2F8, 1);
    CHECK(live.begin_slippi(slippi_start, std::vector<uint8_t>(600, 0xCD), 1700000000));
    std::vector<uint8_t> pre(66, 2), post(84, 3), book(8, 4), game_end(6, 5);
    CHECK(live.append(0x37, pre.data(), (uint32_t)pre.size()));
    CHECK(live.append(0x38, post.data(), (uint32_t)post.size()));
    CHECK(live.append(0x3C, book.data(), (uint32_t)book.size()));
    CHECK(live.append(0x39, game_end.data(), (uint32_t)game_end.size()));
    const auto file = live.encode();
    const uint32_t raw_size = be32(file.data() + 11);
    CHECK(file.size() > 15 + raw_size);
    CHECK(sent == std::vector<uint8_t>(file.begin() + 15, file.begin() + 15 + raw_size));
    CHECK(calls == 1 + 1 + 2 + 4);   // table, Game Start, two split parts, four events
  }
  std::puts("native replay stream tests passed");
}
