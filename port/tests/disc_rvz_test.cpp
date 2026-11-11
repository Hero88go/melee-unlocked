// disc_rvz: every fixture in tests/data/rvz (made by tests/rvz_fixtures.py from Dolphin's writer)
// must come back byte for byte as tests/data/rvz/disc.iso, and damaged or Wii images must be refused.
//   disc_rvz_test                 the fixture checks
//   disc_rvz_test <in> <out>      convert one image (the full Melee disc check)
#include "disc_rvz.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}
static std::vector<char> slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}
static void spit(const fs::path& p, const std::vector<char>& d) {
  std::ofstream f(p, std::ios::binary);
  f.write(d.data(), (std::streamsize)d.size());
}

int main(int argc, char** argv) {
  if (argc == 3) {
    std::string error;
    uint64_t last = 0;
    const bool ok = disc_rvz::convert(argv[1], argv[2], [&](uint64_t done, uint64_t total) {
      if (done - last >= (64u << 20) || done == total) { std::printf("%llu / %llu\n", (unsigned long long)done, (unsigned long long)total); last = done; }
      return true;
    }, &error);
    std::printf("%s %s\n", ok ? "converted" : "failed:", error.c_str());
    return ok ? 0 : 1;
  }
  const fs::path dir = fs::path(MU_RVZ_DATA);
  const std::vector<char> expected = slurp(dir / "disc.iso");
  check(expected.size() > 100000, "reference disc present");
  const fs::path tmp = fs::temp_directory_path() / "mu_disc_rvz_test";
  fs::create_directories(tmp);
  const char* names[] = {"rvz-zstd.rvz", "rvz-zstd-unpacked.rvz", "rvz-plain-groups.rvz", "rvz-none.rvz", "rvz-bzip2.rvz",
                         "rvz-lzma.rvz", "rvz-lzma2.rvz", "wia-none.wia", "wia-purge.wia", "wia-lzma2.wia"};
  for (const char* name : names) {
    const fs::path in = dir / name, out = tmp / (std::string(name) + ".iso");
    disc_rvz::Info info;
    std::string error;
    const bool probed = disc_rvz::probe(in, &info, &error);
    check(probed && info.gamecube && info.game_id == "GTST01" && info.revision == 2 &&
              info.iso_size == expected.size(), std::string(name) + " probe (" + info.compression + ")");
    check(disc_rvz::detect(in) == (name[0] == 'r' ? disc_rvz::Format::Rvz : disc_rvz::Format::Wia), std::string(name) + " detect");
    const bool ok = disc_rvz::convert(in, out, nullptr, &error);
    check(ok && slurp(out) == expected, std::string(name) + " converts byte for byte" + (ok ? "" : ": " + error));
    check(!fs::exists(fs::path(out).concat(".part")), std::string(name) + " leaves no .part");
  }
  check(disc_rvz::detect(dir / "disc.iso") == disc_rvz::Format::NotCompressed, "plain ISO is not compressed");

  // Damage: truncated file, corrupted compressed group, Wii disc type, cancel.
  std::vector<char> rvz = slurp(dir / "rvz-zstd.rvz");
  std::string error;
  spit(tmp / "short.rvz", std::vector<char>(rvz.begin(), rvz.begin() + rvz.size() / 2));
  bool refused = !disc_rvz::convert(tmp / "short.rvz", tmp / "short.iso", nullptr, &error);
  check(refused && !fs::exists(tmp / "short.iso"), "truncated image refused: " + error);
  std::vector<char> bad = rvz;
  for (size_t i = bad.size() - 24; i < bad.size(); ++i) bad[i] ^= 0x5A;   // the group table, stored last
  spit(tmp / "bad.rvz", bad);
  refused = !disc_rvz::convert(tmp / "bad.rvz", tmp / "bad.iso", nullptr, &error);
  check(refused && !fs::exists(tmp / "bad.iso") && !fs::exists(tmp / "bad.iso.part"), "corrupted image refused: " + error);
  std::vector<char> wii = rvz;
  wii[0x48 + 3] = 2;
  spit(tmp / "wii.rvz", wii);
  disc_rvz::Info info;
  refused = !disc_rvz::probe(tmp / "wii.rvz", &info, &error);
  check(refused && error.find("Wii") != std::string::npos, "Wii image refused: " + error);
  check(!disc_rvz::convert(dir / "rvz-zstd.rvz", tmp / "cancel.iso", [](uint64_t, uint64_t) { return false; }, &error) &&
            !fs::exists(tmp / "cancel.iso") && !fs::exists(tmp / "cancel.iso.part"), "cancel leaves nothing");
  std::error_code ec;
  fs::remove_all(tmp, ec);
  std::printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
