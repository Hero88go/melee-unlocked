// The crash report zip: which files it holds, how much of each, and the size the relay accepts.
// Header-only and free of window code, so the launcher (launcher_crash.inl) and
// port_launcher_crash_zip_test build exactly the same zip.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "launcher_crash_privacy.h"

namespace launcher::crash {

// tools/crash_relay refuses anything larger.
constexpr size_t kMaxZipBytes = 8u * 1024 * 1024;

// Read only the crash text and two logs, then scrub them line by line (launcher_crash_privacy.h):
// names, accounts, addresses and full file locations are removed, the rest of the log is kept.
// Binary minidumps stay on the player's device.
struct Part { const char* name; bool launcher_folder; size_t max_bytes; };
inline constexpr Part kParts[] = {
  {"melee_port_crash.txt", false, 256 * 1024},
  {"melee_port.log", false, 2u * 1024 * 1024},
  {"lobby.log", true, 512 * 1024},
};

using File = std::pair<std::string, std::vector<uint8_t>>;   // name in the zip, contents

inline uint32_t crc32(const uint8_t* p, size_t n) {
  static uint32_t table[256] = {};
  if (!table[1]) for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// A plain (stored) zip: small, dependency-free, and any unzip tool reads it.
inline std::vector<uint8_t> make_zip(const std::vector<File>& files) {
  std::vector<uint8_t> out, central;
  auto u16 = [](std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); };
  auto u32 = [&](std::vector<uint8_t>& v, uint32_t x) { u16(v, (uint16_t)x); u16(v, (uint16_t)(x >> 16)); };
  for (const auto& f : files) {
    const uint32_t offset = (uint32_t)out.size(), crc = crc32(f.second.data(), f.second.size()), size = (uint32_t)f.second.size();
    u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0x21);
    u32(out, crc); u32(out, size); u32(out, size); u16(out, (uint16_t)f.first.size()); u16(out, 0);
    out.insert(out.end(), f.first.begin(), f.first.end());
    out.insert(out.end(), f.second.begin(), f.second.end());
    u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0x21);
    u32(central, crc); u32(central, size); u32(central, size); u16(central, (uint16_t)f.first.size());
    u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, offset);
    central.insert(central.end(), f.first.begin(), f.first.end());
  }
  const uint32_t cd_offset = (uint32_t)out.size();
  out.insert(out.end(), central.begin(), central.end());
  u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (uint16_t)files.size()); u16(out, (uint16_t)files.size());
  u32(out, (uint32_t)central.size()); u32(out, cd_offset); u16(out, 0);
  return out;
}

// The last `max` bytes of a file (all of it when smaller); empty when it is missing or unreadable.
inline std::vector<uint8_t> read_tail(const std::string& path, size_t max) {
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
  if (!in) return {};
  const std::streamoff size = in.tellg();
  if (size <= 0) return {};
  const std::streamoff start = size > (std::streamoff)max ? size - (std::streamoff)max : 0;
  in.seekg(start);
  std::vector<uint8_t> data((size_t)(size - start));
  in.read((char*)data.data(), (std::streamsize)data.size());
  data.resize((size_t)in.gcount());
  return data;
}

// The parts that exist and are not empty, in kParts order.
inline std::vector<File> collect(const std::string& game_dir, const std::string& launcher_dir) {
  std::vector<File> files;
  for (const Part& part : kParts) {
    auto data = read_tail((part.launcher_folder ? launcher_dir : game_dir) + "\\" + part.name, part.max_bytes);
    // A file at its cap was cut at the front, so its first line may be the tail of a longer one.
    if (!data.empty()) files.emplace_back(part.name, private_report_text(part.name, data, data.size() >= part.max_bytes));
  }
  return files;
}

// Only the rows around each marked moment of a trace: 10 s before a mark, 3 s after (60 rows a
// second), plus the header. A match is minutes long; what the player flagged is a few seconds.
inline std::vector<uint8_t> marked_rows(const std::vector<uint8_t>& data) {
  constexpr size_t kBefore = 600, kAfter = 180;
  std::vector<std::pair<size_t, size_t>> lines;   // [start, end) of each line, newline included
  for (size_t i = 0, start = 0; i < data.size(); ++i)
    if (data[i] == '\n' || i + 1 == data.size()) { lines.emplace_back(start, i + 1); start = i + 1; }
  if (lines.size() < 2) return data;
  // The "mark" column, found by name in the header so older traces read the same way.
  int mark_col = -1, col = 0;
  for (size_t i = lines[0].first, s = i; i <= lines[0].second; ++i) {
    if (i == lines[0].second || data[i] == ',' || data[i] == '\n' || data[i] == '\r') {
      if (std::string(data.begin() + s, data.begin() + i) == "mark") { mark_col = col; break; }
      ++col; s = i + 1;
      if (i < lines[0].second && (data[i] == '\n' || data[i] == '\r')) break;
    }
  }
  if (mark_col < 0) return data;
  std::vector<bool> keep(lines.size(), false);
  keep[0] = true;
  for (size_t l = 1; l < lines.size(); ++l) {
    int c = 0; size_t i = lines[l].first;
    while (i < lines[l].second && c < mark_col) { if (data[i] == ',') ++c; ++i; }
    if (c == mark_col && i < lines[l].second && data[i] != '0' && data[i] != ',' && data[i] != '\n' && data[i] != '\r') {
      for (size_t k = l > kBefore ? l - kBefore : 1; k <= std::min(lines.size() - 1, l + kAfter); ++k) keep[k] = true;
    }
  }
  std::vector<uint8_t> out;
  for (size_t l = 0; l < lines.size(); ++l)
    if (keep[l]) out.insert(out.end(), data.begin() + lines[l].first, data.begin() + lines[l].second);
  return out.size() > lines[0].second - lines[0].first ? out : std::vector<uint8_t>{};   // no mark: nothing
}

// "Send logs": the crash files that exist (a crash text only when it is newer than `since`, so an
// old crash is not mixed into a new report), plus the newest session trace from the replay folders
// when it was written in the last day. A trace is frame rows of inputs, timing and checksums only.
inline std::vector<File> collect_logs(const std::string& game_dir, const std::string& launcher_dir,
                                      const std::vector<std::string>& replay_dirs, bool include_crash) {
  std::vector<File> files;
  for (const Part& part : kParts) {
    if (!include_crash && std::string(part.name) == "melee_port_crash.txt") continue;
    auto data = read_tail((part.launcher_folder ? launcher_dir : game_dir) + "\\" + part.name, part.max_bytes);
    if (!data.empty()) files.emplace_back(part.name, private_report_text(part.name, data, data.size() >= part.max_bytes));
  }
  // Every session trace from the last day, oldest first. A match keeps its trace only when the player
  // marked a moment in it, so these are exactly the matches they flagged. They travel as one
  // session.trace: each match starts with a "# match <name>" line (tools/pull_reports.py splits them
  // again). When they would not all fit, the newest are kept.
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> traces;
  std::error_code ec;
  const auto now = std::filesystem::file_time_type::clock::now();
  for (const auto& dir : replay_dirs) {
    for (std::filesystem::recursive_directory_iterator it(std::filesystem::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
      if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
      if (!it->is_regular_file(ec) || it->path().extension() != ".trace") continue;
      const auto when = it->last_write_time(ec);
      if (!ec && now - when < std::chrono::hours(24)) traces.emplace_back(when, it->path());
    }
    ec.clear();
  }
  std::sort(traces.begin(), traces.end());
  constexpr size_t kTraceBytes = 3u * 1024 * 1024;
  std::vector<std::vector<uint8_t>> parts;
  size_t total = 0;
  for (auto t = traces.rbegin(); t != traces.rend(); ++t) {   // newest first while it fits
    const auto u8 = t->second.u8string();
    auto data = marked_rows(read_tail(std::string(u8.begin(), u8.end()), 64u * 1024 * 1024));
    if (data.empty()) continue;
    const auto name = t->second.filename().u8string();
    std::string head = "# match " + std::string(name.begin(), name.end()) + "\n";
    if (total + head.size() + data.size() > kTraceBytes) break;
    std::vector<uint8_t> part(head.begin(), head.end());
    part.insert(part.end(), data.begin(), data.end());
    if (part.back() != '\n') part.push_back('\n');
    total += part.size();
    parts.push_back(std::move(part));
  }
  if (!parts.empty()) {
    std::vector<uint8_t> all;
    for (auto p = parts.rbegin(); p != parts.rend(); ++p) all.insert(all.end(), p->begin(), p->end());   // oldest first
    files.emplace_back("session.trace", private_report_text("session.trace", all, false));
  }
  return files;
}

// Scrub and restrict `files`, then ZIP at most `max` bytes. Over the cap, the largest log goes
// first; crash text always stays. `files` is left holding exactly what the outgoing ZIP holds.
inline std::vector<uint8_t> capped_zip(std::vector<File>& files, size_t max = kMaxZipBytes) {
  // Recheck this boundary even for hand-built reports: no binary payloads or unknown files.
  std::vector<File> safe;
  for (const auto& f : files)
    if (f.first == "melee_port_crash.txt" || f.first == "melee_port.log" || f.first == "lobby.log" || f.first == "session.trace")
      safe.emplace_back(f.first, private_report_text(f.first, f.second));
  files = std::move(safe);
  std::vector<uint8_t> zip = make_zip(files);
  while (zip.size() > max && !files.empty()) {
    size_t drop = files.size();
    for (size_t i = 0; i < files.size(); ++i) {
      if (files[i].first == "melee_port_crash.txt") continue;
      if (files[i].first == "melee_port_crash.dmp") { drop = i; break; }
      if (drop == files.size() || files[i].second.size() > files[drop].second.size()) drop = i;
    }
    if (drop == files.size()) {   // only the crash text is left: keep its start, where the error is
      auto& text = files.front().second;
      text.resize(text.size() > zip.size() - max ? text.size() - (zip.size() - max) : 0);
      zip = make_zip(files);
      break;
    }
    files.erase(files.begin() + (std::ptrdiff_t)drop);
    zip = make_zip(files);
  }
  return zip;
}

}  // namespace launcher::crash
