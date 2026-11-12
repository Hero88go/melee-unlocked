// .slpz replays (port/app/replay_slpz.cpp).
//   replay_slpz_test            a built replay: the format's own reordering example, header layout,
//                               round trip, damaged and unfinished files refused
//   replay_slpz_test <folder>   every .slp under it must come back byte for byte, with the sizes
#include "replay_slpz.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(Bytes& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back((uint8_t)(v >> s)); }

// A replay with the events of the format's reordering example (Readme: "cmd ABCD cmd2 EFG cmd ABCD
// cmd3 HI cmd2 EFG"), after an Event Payloads event and a Game Start, then a metadata element.
static Bytes built_replay() {
  Bytes events = {0x35, 13, 0x36, 0, 3, 0x40, 0, 4, 0x41, 0, 3, 0x42, 0, 2};   // sizes: 36:3 40:4 41:3 42:2
  const Bytes start = {0x36, 1, 2, 3};
  const Bytes rest = {0x40, 'A', 'B', 'C', 'D', 0x41, 'E', 'F', 'G', 0x40, 'A', 'B', 'C', 'D',
                      0x42, 'H', 'I', 0x41, 'E', 'F', 'G'};
  events.insert(events.end(), start.begin(), start.end());
  events.insert(events.end(), rest.begin(), rest.end());
  Bytes slp = {'{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l'};
  put32(slp, (uint32_t)events.size());
  slp.insert(slp.end(), events.begin(), events.end());
  const char meta[] = "U\x08metadata{}}";
  slp.insert(slp.end(), meta, meta + sizeof meta - 1);
  return slp;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::strcmp(argv[1], "--compress") == 0) {   // one file, as the game and launcher do it
    const std::string out = slpz::compress_file(argv[2]);
    std::printf("%s\n", out.empty() ? "FAIL not compressed" : out.c_str());
    return out.empty() ? 1 : 0;
  }
  if (argc > 1) {
    size_t files = 0, slp_bytes = 0, slpz_bytes = 0, bad = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(fs::u8path(argv[1]), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
      const auto& e = *it;
      std::error_code fe;
      if (!e.is_regular_file(fe) || fe || e.path().extension() != ".slp") continue;
      std::ifstream f(e.path(), std::ios::binary);
      const Bytes slp((std::istreambuf_iterator<char>(f)), {});
      Bytes z, back;
      if (!slpz::compress(slp, z)) continue;   // unfinished recordings
      ++files; slp_bytes += slp.size(); slpz_bytes += z.size();
      if (!slpz::decompress(z, back) || back != slp) { ++bad; std::printf("FAIL %s\n", e.path().u8string().c_str()); }
    }
    std::printf("%zu replays, %.1f MB as .slp, %.1f MB as .slpz (%.1fx), %zu differ\n", files, slp_bytes / 1048576.0,
                slpz_bytes / 1048576.0, slpz_bytes ? (double)slp_bytes / slpz_bytes : 0.0, bad);
    return bad || !files ? 1 : 0;
  }

  const Bytes slp = built_replay();
  check(slpz::is_slp(slp.data(), slp.size()) && !slpz::is_slpz(slp.data(), slp.size()), "a .slp is told apart");
  Bytes z;
  check(slpz::compress(slp, z), "compresses");
  check(slpz::is_slpz(z.data(), z.size()) && !slpz::is_slp(z.data(), z.size()), "the .slpz is told apart");
  check(slpz::is_slpz(z.data(), 24), "the .slpz is told apart from its 24-byte header alone (the game reads only that)");
  // Header (Readme "Header"): version 0, then the offsets of each section, then the column size.
  check(be32(z.data()) == 0 && be32(z.data() + 4) == 24, "version 0, event sizes right after the header");
  check(be32(z.data() + 8) == 24 + 14 && z[be32(z.data() + 8)] == 0x36, "game start after the event sizes");
  check(be32(z.data() + 12) == 24 + 14 + 4 && z[be32(z.data() + 12)] == 'U', "metadata after the game start");
  // Columns: count 5, the ids, then AABBCCDD EEFFGG HI (Readme "Reordering Example").
  const Bytes expected_columns = {0, 0, 0, 5, 0x40, 0x41, 0x40, 0x42, 0x41,
                                  'A', 'A', 'B', 'B', 'C', 'C', 'D', 'D', 'E', 'E', 'F', 'F', 'G', 'G', 'H', 'I'};
  check(be32(z.data() + 20) == expected_columns.size(), "column section size as the format's example");
  Bytes back;
  check(slpz::decompress(z, back) && back == slp, "decompresses to the same bytes");

  Bytes cut(z.begin(), z.end() - 3);
  check(!slpz::decompress(cut, back), "a cut .slpz is refused");
  Bytes newer = z; newer[3] = 1;
  check(!slpz::decompress(newer, back), "a newer format version is refused");
  Bytes live = slp; live[11] = live[12] = live[13] = live[14] = 0;   // a recording still being written
  check(!slpz::compress(live, z), "an unfinished recording is not compressed");

  const fs::path dir = fs::temp_directory_path() / "mu_slpz_test";
  fs::create_directories(dir);
  const fs::path file = dir / "Game_test.slp";
  { std::ofstream f(file, std::ios::binary); f.write((const char*)slp.data(), (std::streamsize)slp.size()); }
  const std::string out = slpz::compress_file(file.u8string());
  check(!out.empty() && !fs::exists(file) && fs::exists(fs::u8path(out)), "compress_file swaps the .slp for a .slpz");
  Bytes read;
  check(slpz::read_replay(out, read) && read == slp, "read_replay gives the .slp bytes of a .slpz");
  fs::remove_all(dir);
  std::printf("%s\n", g_failures ? "FAILED" : "all passed");
  return g_failures ? 1 : 0;
}
