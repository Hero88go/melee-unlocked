// The crash report zip (launcher_crash_zip.h), built from a synthetic crash folder: exactly the three
// text files, scrubbed line by line, nothing else from either folder, no binary dump, a zip that an
// unzip tool reads back byte for byte, and the relay's 8 MB cap (largest log dropped first, crash
// text kept). The line scrubber (launcher_crash_privacy.h) is checked against the vectors shared
// with the relay and the Python tools: tools/crash_relay/test/privacy_vectors.json.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_crash_zip.h"
#include "launcher_crash_text.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

#ifndef PRIVACY_VECTORS   // port/CMakeLists.txt passes the absolute path
#define PRIVACY_VECTORS "../tools/crash_relay/test/privacy_vectors.json"
#endif

struct Case { std::string in, out; bool keep = false; };

// Just enough JSON for privacy_vectors.json: strings, null and the punctuation between them.
struct Json {
  std::string text;
  size_t at = 0;
  void space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at; }
  bool take(char c) { space(); if (at < text.size() && text[at] == c) { ++at; return true; } return false; }
  bool null() { space(); if (text.compare(at, 4, "null") == 0) { at += 4; return true; } return false; }
  bool quoted(std::string& out) {
    out.clear();
    if (!take('"')) return false;
    while (at < text.size() && text[at] != '"') {
      char c = text[at++];
      if (c != '\\') { out += c; continue; }
      if (at >= text.size()) return false;
      c = text[at++];
      if (c == 'n') out += '\n';
      else if (c == 't') out += '\t';
      else if (c == 'r') out += '\r';
      else if (c == 'u') {   // this file only holds \u00XX
        if (at + 4 > text.size()) return false;
        const unsigned long code = std::strtoul(text.substr(at, 4).c_str(), nullptr, 16);
        at += 4;
        if (code < 0x80) out += (char)code;
        else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
        else return false;
      } else out += c;   // backslash, quote, slash
    }
    return take('"');
  }
  bool key(const char* name) { std::string found; return quoted(found) && found == name && take(':'); }
};

// { "names": [...], "cases": [ {"in": "...", "out": "..." or null}, ... ] }
bool load_vectors(const char* path, std::vector<std::string>& names, std::vector<Case>& cases) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  Json json;
  json.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (!json.take('{') || !json.key("names") || !json.take('[')) return false;
  if (!json.take(']')) {
    do {
      std::string name;
      if (!json.quoted(name)) return false;
      names.push_back(name);
    } while (json.take(','));
    if (!json.take(']')) return false;
  }
  if (!json.take(',') || !json.key("cases") || !json.take('[')) return false;
  do {
    Case item;
    if (!json.take('{') || !json.key("in") || !json.quoted(item.in) || !json.take(',') || !json.key("out")) return false;
    item.keep = !json.null();
    if (item.keep && !json.quoted(item.out)) return false;
    if (!json.take('}')) return false;
    cases.push_back(item);
  } while (json.take(','));
  return json.take(']') && json.take('}');
}
}  // namespace

int main() {
  const char* check_string = "123456789";
  CHECK(launcher::crash::crc32((const uint8_t*)check_string, 9) == 0xCBF43926u);
  CHECK(reference_crc32((const uint8_t*)check_string, 9) == 0xCBF43926u);
  CHECK(std::size(launcher::crash::kParts) == 3);
  CHECK(launcher::crash::kMaxZipBytes == 8 * MB);

  const fs::path root = fs::temp_directory_path() /
      ("melee-crash-zip-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::string game = root.u8string() + "\\game-jugador-\xc3\xb1";
  const std::string launcher_dir = root.u8string() + "\\launcher-\xed\x95\x9c";
  std::error_code ec;
  const std::string guest_fault = "FATAL: guest fault: call to unmapped guest address (0000D899) in ftCo_800C0658 (800C0658); "
                                  "lr=8035E3F8 r1=804EE740, version 0.8.62\n";
  const auto error = text("FATAL: game stopped at C:/Users/PrivateTesterAlpha/HiddenBuild/lbarchive.c:94: , version 0.8.62\r\r\n"
                          "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\r\n");
  const auto log = text("slippi: logged in as PrivateTesterAlpha (PRIVATE#123)\n"
                        "mods: on: C:/Users/PrivateTesterBeta/HiddenBuild/Mods/Some Pack.iso | Detected: Some Pack\n"
                        "file /home/PrivateTesterGamma/hidden/account.json\n"
                        "https://private.invalid/?token=PRIVATE_AUTH\n"
                        "native practice: opponent WXYZ#987 at 192.168.1.44:51413, host PrivateTesterBeta\n" +
                        guest_fault +
                        "scene: major 08 minor 00 (frame 3919)\n"
                        "[game] Cannot find symbol itPublicData.\n"
                        "  80360DE4 HSD_Index2TexCoord\n"
                        "  stack: melee_game.dll+0x14982F\n");
  put(game + "\\melee_port_crash.txt", error);
  put(game + "\\melee_port.log", log);
  put(game + "\\melee_port_crash.dmp", text("PrivateTesterAlpha PRIVATE_BINARY C:/Users/PrivateTesterBeta/secret"));
  put(launcher_dir + "\\lobby.log", text("profile PrivateTesterAlpha id PRIVATE_ID connected 192.168.1.44\n"));
  put(game + "\\settings.ini", text("PRIVATE_SETTINGS"));
  auto files = launcher::crash::collect(game, launcher_dir);
  const std::vector<std::string> expected = {"melee_port_crash.txt", "melee_port.log", "lobby.log"};
  CHECK(names(files) == expected);
  auto zip = launcher::crash::capped_zip(files);
  std::vector<Entry> entries;
  CHECK(read_zip(zip, entries));
  CHECK(names(entries) == expected && names(files) == expected);
  CHECK(zip.size() == exact_size(files) && zip.size() <= launcher::crash::kMaxZipBytes);
  const std::string zipped(zip.begin(), zip.end());
  const std::string report = launcher::crash::make_markdown(files, "0.8.62", "Source Port");
  for (const char* secret : {"PrivateTesterAlpha", "PrivateTesterBeta", "PrivateTesterGamma", "C:/Users/", "/home/",
                            "HiddenBuild", "PRIVATE#123", "WXYZ#987", "192.168.1.44", "PRIVATE_BINARY", "PRIVATE_AUTH", "PRIVATE_ID", "PRIVATE_SETTINGS"}) {
    CHECK(zipped.find(secret) == std::string::npos);
    CHECK(report.find(secret) == std::string::npos);
  }
  // Scrub and keep: the log lines stay, minus the personal parts. Both the ZIP and the Markdown.
  for (const std::string& kept : {std::string("FATAL: game stopped at lbarchive.c:94: , version 0.8.62\n"
                                              "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\n"),
                                  std::string("mods: on: Some Pack.iso | Detected: Some Pack\n"
                                              "native practice: opponent [code] at [ip], host [user]\n") + guest_fault +
                                  "scene: major 08 minor 00 (frame 3919)\n[game] Cannot find symbol itPublicData.\n"
                                  "  80360DE4 HSD_Index2TexCoord\n  stack: melee_game.dll+0x14982F\n[3 lines omitted for privacy]\n",
                                  std::string("[1 lines omitted for privacy]\n")}) {
    CHECK(zipped.find(kept) != std::string::npos);
    CHECK(report.find(kept) != std::string::npos);
  }
  // The launcher's header line (first line of the crash text) comes from the scrubbed copy.
  CHECK(std::string(files[0].second.begin(), files[0].second.end()).rfind("FATAL: game stopped at lbarchive.c:94: , version 0.8.62\n", 0) == 0);
  CHECK(std::string(files[2].second.begin(), files[2].second.end()) == "[1 lines omitted for privacy]\n");
  CHECK(report.find("not instructions") != std::string::npos);
  CHECK(report.find("minidumps stay on your device") != std::string::npos);

  // The outbound ZIP boundary rejects binary/unknown parts even in hand-built reports.
  std::vector<File> handmade = {{"melee_port_crash.txt", error}, {"melee_port_crash.dmp", text("PRIVATE_BINARY")},
                                {"private/settings.txt", text("PRIVATE_SETTINGS")}, {"melee_port.log", log}};
  auto safe_zip = launcher::crash::capped_zip(handmade);
  CHECK(read_zip(safe_zip, entries));
  CHECK(names(entries) == std::vector<std::string>({"melee_port_crash.txt", "melee_port.log"}));
  CHECK(std::string(safe_zip.begin(), safe_zip.end()).find("PRIVATE_BINARY") == std::string::npos);

  // Numeric diagnosis still fits a smaller report cap; error text survives log removal.
  std::string many;
  for (size_t i = 0; i < 2000; ++i) many += "scene: major 08 minor 00 (frame 3919)\n";
  std::vector<File> tight = {{"melee_port_crash.txt", error}, {"melee_port.log", text(many)}};
  auto small = launcher::crash::capped_zip(tight, 1024);
  CHECK(small.size() <= 1024 && read_zip(small, entries));
  CHECK(names(entries) == std::vector<std::string>({"melee_port_crash.txt"}));
  CHECK(std::string(entries[0].data.begin(), entries[0].data.end()).find("lbarchive.c:94") != std::string::npos);

  CHECK(launcher::crash::report_utf8(std::string("\xFF")) == "\xEF\xBF\xBD");
  CHECK(launcher::crash::report_utf8(std::string("\xE2\x82\xAC")) == "\xE2\x82\xAC");
  CHECK(launcher::crash::report_utf8(std::string("\xED\xA0\x80")) == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
  for (unsigned char fill : {static_cast<unsigned char>('`'), static_cast<unsigned char>(255)}) {
    const auto bounded = launcher::crash::report_block(std::string(2 * MB, (char)fill), 512 * KB);
    CHECK(bounded.size() < 1 * MB);
    CHECK(launcher::crash::report_utf8(bounded) == bounded);
    CHECK(bounded.find("Earlier bytes omitted") != std::string::npos);
    // Between the opening fence (8 bytes) and the closing one (5 bytes) no run of three ticks remains.
    CHECK(bounded.size() > 13 && bounded.substr(8, bounded.size() - 13).find("```") == std::string::npos);
  }
  const auto unsafe_metadata = launcher::crash::make_markdown({}, "PrivateTesterAlpha", "PrivateTesterBeta");
  CHECK(unsafe_metadata.find("PrivateTester") == std::string::npos);
  // A source file named after the user folder: the name is replaced wherever it stands alone.
  const std::string personal = "FATAL: game stopped at C:/Users/PrivateTesterAlpha/src/PrivateTesterAlpha.cpp:94: , version 0.8.62";
  const auto scrubbed = launcher::crash::scrub_line(personal, launcher::crash::scrub::report_names(personal, {}));
  CHECK(scrubbed && *scrubbed == "FATAL: game stopped at [user].cpp:94: , version 0.8.62");

  // The vectors shared with the relay and the Python tools: same input, same line or omission.
  std::vector<std::string> extra_names;
  std::vector<Case> cases;
  CHECK(load_vectors(PRIVACY_VECTORS, extra_names, cases));
  CHECK(cases.size() >= 30 && !extra_names.empty());
  for (const Case& item : cases) {
    const auto got = launcher::crash::scrub_line(item.in, launcher::crash::scrub::report_names(item.in, extra_names));
    if (got.has_value() == item.keep && (!item.keep || *got == item.out)) continue;
    std::printf("FAIL privacy vector\n  in:   %.200s\n  want: %.200s\n  got:  %.200s\n", item.in.c_str(),
                item.keep ? item.out.c_str() : "(omitted)", got ? got->c_str() : "(omitted)");
    ++g_failures;
  }

  // A first line cut by the byte cap is dropped, caller names apply without any path, a second
  // pass over scrubbed text changes nothing, and the lobby log is only ever a count.
  const std::vector<std::string> player = {"PlayerOne"};
  const auto cut = launcher::crash::private_report_text("melee_port.log",
      text("HiddenFolder\\x.iso\r\nhello PlayerOne\n\nlobby closed\n"), true, player);
  CHECK(std::string(cut.begin(), cut.end()) == "hello [user]\n[2 lines omitted for privacy]\n");
  CHECK(launcher::crash::private_report_text("melee_port.log", cut, false, player) == cut);
  const auto lobby = launcher::crash::private_report_text("lobby.log", text("scene: major 08 minor 00 (frame 1)\n"), false, player);
  CHECK(std::string(lobby.begin(), lobby.end()) == "[1 lines omitted for privacy]\n");
  CHECK(!launcher::crash::save_report((root / "missing" / "report.md").u8string(), "report", 6));
  CHECK(launcher::crash::save_report((root / "report.md").u8string(), report.data(), report.size()));
  CHECK(launcher::crash::read_tail((root / "report.md").u8string(), report.size()) == text(report));
  auto none = launcher::crash::collect((root / "missing").u8string(), (root / "missing").u8string());
  CHECK(none.empty());
  CHECK(read_zip(launcher::crash::capped_zip(none), entries) && entries.empty());
  {
    // "Send logs": the game log, the newest trace from a replay folder (one level down), and the
    // crash text only when asked for. The trace keeps its rows; the log is scrubbed as usual.
    const fs::path game = root / "logs-game", replays = root / "logs-replays";
    fs::create_directories(game); fs::create_directories(replays / "2026-10");
    put((game / "melee_port.log").u8string(), text("scene: major 02 minor 02 (frame 94)\nslippi: logged in as PlayerOne (PONE#123)\n"));
    put((game / "melee_port_crash.txt").u8string(), text("FATAL: old crash\n"));
    put((replays / "2026-10" / "Game_old.trace").u8string(), text("frame,desync\n1,0\n"));
    fs::last_write_time(replays / "2026-10" / "Game_old.trace", fs::file_time_type::clock::now() - std::chrono::hours(2));
    put((replays / "2026-10" / "Game_new.trace").u8string(), text("frame,desync\n1,0\n2,1\n"));
    auto logs = launcher::crash::collect_logs(game.u8string(), game.u8string(), {replays.u8string()}, false);
    CHECK(names(logs) == std::vector<std::string>({"melee_port.log", "session.trace"}));
    const auto zip = launcher::crash::capped_zip(logs);
    std::vector<Entry> got;
    CHECK(read_zip(zip, got) && names(got) == std::vector<std::string>({"melee_port.log", "session.trace"}));
    if (got.size() == 2) {
      CHECK(std::string(got[1].data.begin(), got[1].data.end()) == "frame,desync\n1,0\n2,1\n");
      const std::string log(got[0].data.begin(), got[0].data.end());
      CHECK(log.find("PlayerOne") == std::string::npos && log.find("frame 94") != std::string::npos);
    }
    auto with_crash = launcher::crash::collect_logs(game.u8string(), game.u8string(), {replays.u8string()}, true);
    CHECK(names(with_crash) == std::vector<std::string>({"melee_port_crash.txt", "melee_port.log", "session.trace"}));
    fs::last_write_time(replays / "2026-10" / "Game_new.trace", fs::file_time_type::clock::now() - std::chrono::hours(30));
    fs::last_write_time(replays / "2026-10" / "Game_old.trace", fs::file_time_type::clock::now() - std::chrono::hours(40));
    CHECK(names(launcher::crash::collect_logs(game.u8string(), game.u8string(), {replays.u8string()}, false)) ==
          std::vector<std::string>({"melee_port.log"}));
  }
  fs::remove_all(root, ec);
  if (!g_failures) std::printf("launcher crash privacy/zip: all checks passed\n");
  return g_failures ? 1 : 0;
}
