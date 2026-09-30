// The crash report zip (launcher_crash_zip.h), built from a synthetic crash folder: exactly the four
// expected files, each cut to its newest bytes, nothing else from either folder, a zip that an unzip
// tool reads back byte for byte, and the relay's 8 MB cap (minidump dropped first, crash text kept).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_crash_zip.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using launcher::crash::File;

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

const size_t KB = 1024, MB = 1024 * 1024;

uint32_t le16(const std::vector<uint8_t>& z, size_t at) { return (uint32_t)z[at] | (uint32_t)z[at + 1] << 8; }
uint32_t le32(const std::vector<uint8_t>& z, size_t at) { return le16(z, at) | le16(z, at + 2) << 16; }

// Bit by bit, independent of the table the launcher uses.
uint32_t reference_crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}

struct Entry { std::string name; std::vector<uint8_t> data; };

// Reads a stored zip the way an unzip tool does (end record, central directory, local headers) and
// refuses any inconsistency between them.
bool read_zip(const std::vector<uint8_t>& z, std::vector<Entry>& out) {
  out.clear();
  if (z.size() < 22) return false;
  const size_t end = z.size() - 22;
  if (le32(z, end) != 0x06054b50u || le16(z, end + 20) != 0) return false;
  const uint32_t count = le16(z, end + 10), cd_size = le32(z, end + 12), cd_offset = le32(z, end + 16);
  if (le16(z, end + 8) != count || (size_t)cd_offset + cd_size != end) return false;
  size_t at = cd_offset;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 46 > end || le32(z, at) != 0x02014b50u) return false;
    const uint32_t method = le16(z, at + 10), crc = le32(z, at + 16), packed = le32(z, at + 20), size = le32(z, at + 24);
    const uint32_t name_len = le16(z, at + 28), extra = le16(z, at + 30), comment = le16(z, at + 32);
    const size_t local = le32(z, at + 42);
    if (method != 0 || packed != size || at + 46 + name_len > end) return false;
    Entry e;
    e.name.assign((const char*)&z[at + 46], name_len);
    if (local + 30 + name_len + size > cd_offset || le32(z, local) != 0x04034b50u) return false;
    if (le16(z, local + 8) != 0 || le32(z, local + 14) != crc || le32(z, local + 18) != size || le32(z, local + 22) != size ||
        le16(z, local + 26) != name_len || le16(z, local + 28) != 0 || std::memcmp(&z[local + 30], e.name.data(), name_len) != 0)
      return false;
    e.data.assign(z.begin() + (std::ptrdiff_t)(local + 30 + name_len), z.begin() + (std::ptrdiff_t)(local + 30 + name_len + size));
    if (reference_crc32(e.data.data(), e.data.size()) != crc) return false;
    out.push_back(std::move(e));
    at += 46 + name_len + extra + comment;
  }
  return at == end;
}

std::vector<uint8_t> pattern(size_t n, uint32_t seed) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; v[i] = (uint8_t)(seed >> 24); }
  return v;
}
std::vector<uint8_t> text(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
std::vector<uint8_t> tail(const std::vector<uint8_t>& v, size_t n) {
  return n >= v.size() ? v : std::vector<uint8_t>(v.end() - (std::ptrdiff_t)n, v.end());
}
void put(const std::string& utf8_path, const std::vector<uint8_t>& data) {
  const fs::path path = fs::u8path(utf8_path);
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write((const char*)data.data(), (std::streamsize)data.size());
}
std::vector<std::string> names(const std::vector<Entry>& entries) {
  std::vector<std::string> out;
  for (const auto& e : entries) out.push_back(e.name);
  return out;
}
std::vector<std::string> names(const std::vector<File>& files) {
  std::vector<std::string> out;
  for (const auto& f : files) out.push_back(f.first);
  return out;
}
// A zip of these files holds exactly this many bytes: 30 + 46 header bytes and the name twice per
// file, plus the 22-byte end record. Anything more would be content nobody listed.
size_t exact_size(const std::vector<File>& files) {
  size_t n = 22;
  for (const auto& f : files) n += 76 + 2 * f.first.size() + f.second.size();
  return n;
}
}  // namespace

int main() {
  const char* check_string = "123456789";
  CHECK(launcher::crash::crc32((const uint8_t*)check_string, 9) == 0xCBF43926u);
  CHECK(reference_crc32((const uint8_t*)check_string, 9) == 0xCBF43926u);

  // The part caps fit under the relay's limit together, so a normal report never loses a file.
  {
    size_t all = 22;
    for (const auto& part : launcher::crash::kParts) all += 76 + 2 * std::strlen(part.name) + part.max_bytes;
    CHECK(all <= launcher::crash::kMaxZipBytes);
    CHECK(launcher::crash::kMaxZipBytes == 8 * MB);   // tools/crash_relay MAX_BYTES
  }

  const fs::path root = fs::temp_directory_path() /
      ("melee-crash-zip-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  fs::remove_all(root, ec);
  // Folder names beyond ASCII, as a player's Windows account name can be (UTF-8, like the launcher's).
  const std::string game = root.u8string() + "\\game-jugador-\xc3\xb1", launcher_dir = root.u8string() + "\\launcher-\xed\x95\x9c";

  // Every part larger than its cap, and files beside them that must never be sent.
  const std::vector<uint8_t> crash_text = text("CRASH: exception C0000005 at 0000000082A1B2C3 (melee_game.dll+0x21B2C3), version test\r\n"
                                               "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\r\n");
  const std::vector<uint8_t> dump = pattern(4 * MB + 12345, 1), game_log = pattern(2 * MB + 777, 2), lobby = pattern(512 * KB + 99, 3);
  put(game + "\\melee_port_crash.txt", crash_text);
  put(game + "\\melee_port_crash.dmp", dump);
  put(game + "\\melee_port.log", game_log);
  put(launcher_dir + "\\lobby.log", lobby);
  for (const char* decoy : {"\\port-settings.ini", "\\melee_crash_report.zip", "\\melee_port_crash.txt.bak", "\\User\\Slippi\\user.json",
                            "\\Replays\\Game_20260930T000000.slp"})
    put(game + decoy, text("private"));
  for (const char* decoy : {"\\launcher.ini", "\\lobby-peer-identity.json", "\\lobby-game-status.json"}) put(launcher_dir + decoy, text("private"));

  {
    auto files = launcher::crash::collect(game, launcher_dir);
    const auto zip = launcher::crash::capped_zip(files);
    std::vector<Entry> entries;
    CHECK(read_zip(zip, entries));
    const std::vector<std::string> expected = {"melee_port_crash.txt", "melee_port_crash.dmp", "melee_port.log", "lobby.log"};
    CHECK(names(entries) == expected);
    CHECK(names(files) == expected);
    if (entries.size() == 4) {
      CHECK(entries[0].data == crash_text);
      CHECK(entries[1].data == tail(dump, 4 * MB));
      CHECK(entries[2].data == tail(game_log, 2 * MB));
      CHECK(entries[3].data == tail(lobby, 512 * KB));
    }
    CHECK(zip.size() <= launcher::crash::kMaxZipBytes);
    CHECK(zip.size() == exact_size(files));
    std::printf("full report: %zu files, %zu bytes\n", entries.size(), zip.size());

    // Tighter caps: the minidump goes first, then the largest log; the crash text stays.
    auto three = files;
    const auto zip3 = launcher::crash::capped_zip(three, 3 * MB);
    CHECK(zip3.size() <= 3 * MB);
    CHECK(read_zip(zip3, entries) && names(entries) == std::vector<std::string>({"melee_port_crash.txt", "melee_port.log", "lobby.log"}));
    CHECK(names(three) == names(entries));
    auto one = files;
    const auto zip1 = launcher::crash::capped_zip(one, 1 * MB);
    CHECK(zip1.size() <= 1 * MB);
    CHECK(read_zip(zip1, entries) && names(entries) == std::vector<std::string>({"melee_port_crash.txt", "lobby.log"}));
    auto tiny = files;
    const auto zip_tiny = launcher::crash::capped_zip(tiny, 64 * KB);
    CHECK(zip_tiny.size() <= 64 * KB);
    CHECK(read_zip(zip_tiny, entries) && names(entries) == std::vector<std::string>({"melee_port_crash.txt"}));
    if (entries.size() == 1) CHECK(entries[0].data == crash_text);
  }

  // The real 8 MB cap with a part that is over it on its own (a list built by hand, past collect's caps).
  {
    std::vector<File> files = {{"melee_port_crash.txt", crash_text}, {"melee_port_crash.dmp", pattern(9 * MB, 4)},
                               {"melee_port.log", pattern(1 * MB, 5)}};
    const auto zip = launcher::crash::capped_zip(files);
    std::vector<Entry> entries;
    CHECK(zip.size() <= launcher::crash::kMaxZipBytes);
    CHECK(read_zip(zip, entries) && names(entries) == std::vector<std::string>({"melee_port_crash.txt", "melee_port.log"}));
  }

  // Crash text alone and too big for the cap: its start (the error line) is what stays.
  {
    std::vector<uint8_t> big = crash_text;
    const auto filler = pattern(100 * KB, 6);
    big.insert(big.end(), filler.begin(), filler.end());
    std::vector<File> files = {{"melee_port_crash.txt", big}};
    const auto zip = launcher::crash::capped_zip(files, 50 * KB);
    std::vector<Entry> entries;
    CHECK(zip.size() == 50 * KB);
    CHECK(read_zip(zip, entries) && entries.size() == 1);
    if (entries.size() == 1) {
      CHECK(entries[0].data.size() < big.size());
      CHECK(std::equal(entries[0].data.begin(), entries[0].data.end(), big.begin()));
    }
  }

  // Missing and empty parts are left out rather than sent as empty entries.
  {
    const std::string sparse = root.u8string() + "\\sparse";
    put(sparse + "\\melee_port_crash.txt", crash_text);
    put(sparse + "\\melee_port.log", {});
    auto files = launcher::crash::collect(sparse, root.u8string() + "\\no-launcher-folder");
    const auto zip = launcher::crash::capped_zip(files);
    std::vector<Entry> entries;
    CHECK(read_zip(zip, entries) && names(entries) == std::vector<std::string>({"melee_port_crash.txt"}));
    auto none = launcher::crash::collect(root.u8string() + "\\missing", root.u8string() + "\\missing");
    CHECK(none.empty());
    CHECK(read_zip(launcher::crash::capped_zip(none), entries) && entries.empty());
  }

  fs::remove_all(root, ec);
  if (g_failures == 0) std::printf("launcher crash zip: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
