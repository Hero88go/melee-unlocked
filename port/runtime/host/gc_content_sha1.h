// YouGame gcManifest content_sha1, version 2. No window or game dependencies.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace source_port::mods {
// make_sha1() supplies an incremental SHA-1 with update(data, size) and hex().
// Raw name bytes are retained: the manifest is latin1, never UTF-8 encoded or sorted.
// Empty on malformed/truncated images or a failed read/hash. Memory use is bounded.
template<class MakeSha1>
std::string gc_content_sha1(const std::filesystem::path& path, MakeSha1 make_sha1) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(path, ec);
  if (ec) return {};
  std::ifstream in(path, std::ios::binary);
  const auto read = [&](uint64_t off, void* dst, size_t n) {
    if (off > size || n > size - off) return false;
    in.clear(); in.seekg(static_cast<std::streamoff>(off));
    return in.good() && (n == 0 || bool(in.read(static_cast<char*>(dst), n)));
  };
  const auto be = [](const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  };
  std::vector<uint8_t> buffer(1 << 20);
  const auto digest = [&](uint64_t off, uint64_t n) {
    if (off > size || n > size - off) return std::string{};
    auto hash = make_sha1();
    for (uint64_t pos = 0; pos < n;) {
      const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), n - pos));
      if (!read(off + pos, buffer.data(), chunk)) return std::string{};
      hash.update(buffer.data(), chunk); pos += chunk;
    }
    return hash.hex();
  };
  auto total = make_sha1();
  const auto line = [&](const std::string& text) {
    total.update(text.data(), text.size()); total.update("\n", 1);
  };
  uint8_t boot[0x440];
  if (!read(0, boot, sizeof boot) || be(boot + 0x1c) != 0xc2339f3d) return {};
  const uint64_t dol_off = be(boot + 0x420), fst_off = be(boot + 0x424), fst_size = be(boot + 0x428);
  std::fill(boot + 0x420, boot + 0x42c, 0);
  auto boot_hash = make_sha1(); boot_hash.update(boot, sizeof boot);
  const auto boot_hex = boot_hash.hex();
  const auto bi2 = digest(0x440, 0x2000);
  if (boot_hex.empty() || bi2.empty()) return {};
  line("yougame gc-content 2"); line("boot " + boot_hex); line("bi2 " + bi2);
  uint8_t app[0x20], dol[0x100];
  if (!read(0x2440, app, sizeof app) || !read(dol_off, dol, sizeof dol)) return {};
  const uint64_t app_size = 0x20ull + be(app + 0x14) + be(app + 0x18);
  uint64_t dol_size = sizeof dol;
  for (unsigned i = 0; i < 18; ++i) {
    const uint64_t off = be(dol + 4 * i), len = be(dol + 0x90 + 4 * i);
    if (off && len) dol_size = std::max(dol_size, off + len);
  }
  const auto app_hex = digest(0x2440, app_size), dol_hex = digest(dol_off, dol_size);
  if (app_hex.empty() || dol_hex.empty()) return {};
  line("apploader " + std::to_string(app_size) + " " + app_hex);
  line("dol " + std::to_string(dol_size) + " " + dol_hex);
  // GameCube's FST is small; reject implausible sizes before allocating.
  if (fst_size < 12 || fst_size > 4u * 1024u * 1024u || fst_off > size || fst_size > size - fst_off) return {};
  std::vector<uint8_t> fst(static_cast<size_t>(fst_size));
  if (!read(fst_off, fst.data(), fst.size())) return {};
  const uint32_t count = be(fst.data() + 8);
  if (fst[0] != 1 || count < 1 || uint64_t(count) * 12 > fst_size) return {};
  const size_t names = size_t(count) * 12;
  for (uint32_t i = 1; i < count; ++i) {
    const uint8_t* entry = fst.data() + size_t(i) * 12;
    const size_t name_at = names + (be(entry) & 0xffffff);
    if (name_at >= fst.size()) return {};
    const auto end = std::find(fst.begin() + name_at, fst.end(), uint8_t(0));
    if (end == fst.end() || entry[0] > 1) return {};
    const std::string name(fst.begin() + name_at, end);
    const uint32_t second = be(entry + 4), third = be(entry + 8);
    if (entry[0]) {
      if (second >= i || third <= i || third > count) return {};
      line("dir " + std::to_string(i) + " " + std::to_string(second) + " " + std::to_string(third) + " " + name);
    } else {
      const auto hex = digest(second, third);
      if (hex.empty()) return {};
      line("file " + std::to_string(i) + " " + std::to_string(third) + " " + hex + " " + name);
    }
  }
  return total.hex();
}
} // namespace source_port::mods
