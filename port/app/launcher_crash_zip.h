// The crash report zip: which files it holds, how much of each, and the size the relay accepts.
// Header-only and free of window code, so the launcher (launcher_crash.inl) and
// port_launcher_crash_zip_test build exactly the same zip.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace launcher::crash {

// tools/crash_relay refuses anything larger.
constexpr size_t kMaxZipBytes = 8u * 1024 * 1024;

// Everything a report can hold, in zip order, each cut to its newest bytes: the error, the minidump
// and the game's log from the game's folder, the lobby log from the launcher's. Nothing else in
// either folder is ever read.
struct Part { const char* name; bool launcher_folder; size_t max_bytes; };
inline constexpr Part kParts[] = {
  {"melee_port_crash.txt", false, 256 * 1024},
  {"melee_port_crash.dmp", false, 4u * 1024 * 1024},
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
    if (!data.empty()) files.emplace_back(part.name, std::move(data));
  }
  return files;
}

// The zip of `files`, at most `max` bytes. Over the cap, the minidump goes first, then the largest
// log, until it fits; the crash text always stays (cut short only if it alone is too big, which
// its own 256 KB cap rules out at 8 MB). `files` is left holding exactly what the zip holds.
inline std::vector<uint8_t> capped_zip(std::vector<File>& files, size_t max = kMaxZipBytes) {
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
