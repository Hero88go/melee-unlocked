// .slpz replays (see replay_slpz.h). A port of the slpz tool's lib.rs (Alexander Harrison, MIT or
// Apache-2.0): the same header, the same column order, the same zstd frame.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_slpz.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "zstd/zstd.h"

namespace slpz {
namespace {

constexpr uint8_t kEventPayloads = 0x35, kGameStart = 0x36;
constexpr uint8_t kRawHeader[11] = {0x7B, 0x55, 0x03, 0x72, 0x61, 0x77, 0x5B, 0x24, 0x55, 0x23, 0x6C};
constexpr uint32_t kVersion = 0;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void put_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

// The Event Payloads event (its first byte 0x35): each command's payload size. `count` = entries.
bool event_sizes(const uint8_t* ev, size_t size, std::array<uint16_t, 256>& sizes, size_t& count) {
  if (size < 2) return false;
  const size_t info = ev[1];
  if (info < 1 || size < info + 1) return false;
  count = (info - 1) / 3;
  sizes.fill(0);
  for (size_t i = 0; i < count; ++i) {
    const size_t at = i * 3 + 2;
    sizes[ev[at]] = (uint16_t)(ev[at + 1] << 8 | ev[at + 2]);
  }
  return true;
}

// Events after the Game Start, regrouped: the total, the command of each event in order, then for
// each command (0..255) its payloads as byte columns (byte 0 of every one, then byte 1, ...).
bool reorder(const uint8_t* ev, size_t n, const std::array<uint16_t, 256>& sizes, std::vector<uint8_t>& out) {
  std::array<uint32_t, 256> counts{};
  for (size_t i = 0; i < n;) {
    const uint16_t s = sizes[ev[i]];
    if (!s) return false;
    ++counts[ev[i]];
    i += 1 + (size_t)s;
  }
  std::array<uint64_t, 257> offset{};
  uint64_t total = 0;
  for (int c = 0; c < 256; ++c) { offset[c + 1] = offset[c] + (uint64_t)sizes[c] * counts[c]; total += counts[c]; }
  if (offset[256] + total != n) return false;   // an event ran past the end
  out.assign(4 + total + offset[256], 0);
  put_be32(out.data(), (uint32_t)total);
  uint8_t* order = out.data() + 4;
  uint8_t* cols = order + total;
  std::array<uint32_t, 256> written{};
  size_t k = 0;
  for (size_t i = 0; i < n; ++k) {
    const uint8_t c = ev[i];
    const size_t s = sizes[c], stride = counts[c];
    order[k] = c;
    uint8_t* dst = cols + offset[c] + written[c];
    for (size_t j = 0; j < s; ++j) dst[j * stride] = ev[i + 1 + j];
    ++written[c];
    i += 1 + s;
  }
  return true;
}

bool unorder(const uint8_t* b, size_t n, const std::array<uint16_t, 256>& sizes, std::vector<uint8_t>& out) {
  if (n < 4) return false;
  const uint64_t total = be32(b);
  if (4 + total > n) return false;
  const uint8_t* order = b + 4;
  const uint8_t* cols = order + total;
  const size_t cols_size = n - 4 - (size_t)total;
  std::array<uint32_t, 256> counts{};
  for (uint64_t i = 0; i < total; ++i) ++counts[order[i]];
  std::array<uint64_t, 257> offset{};
  for (int c = 0; c < 256; ++c) offset[c + 1] = offset[c] + (uint64_t)sizes[c] * counts[c];
  if (offset[256] != cols_size) return false;
  const size_t base = out.size();
  out.resize(base + (size_t)(offset[256] + total));
  uint8_t* dst = out.data() + base;
  std::array<uint32_t, 256> done{};
  size_t at = 0;
  for (uint64_t i = 0; i < total; ++i) {
    const uint8_t c = order[i];
    const size_t s = sizes[c], stride = counts[c];
    dst[at] = c;
    const uint8_t* src = cols + offset[c] + done[c];
    for (size_t j = 0; j < s; ++j) dst[at + 1 + j] = src[j * stride];
    ++done[c];
    at += 1 + s;
  }
  return true;
}

bool read_file(const std::string& utf8_path, std::vector<uint8_t>& out) {
  std::ifstream f(std::filesystem::u8path(utf8_path), std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

bool is_slp(const uint8_t* data, size_t size) { return size >= 11 && !std::memcmp(data, kRawHeader, 11); }
// From the 24-byte header alone (a caller may have read only that); decompress checks the rest.
bool is_slpz(const uint8_t* data, size_t size) {
  if (size < 24 || be32(data) > kVersion) return false;
  const uint32_t sizes_at = be32(data + 4), start_at = be32(data + 8), meta_at = be32(data + 12), events_at = be32(data + 16);
  return sizes_at == 24 && sizes_at < start_at && start_at <= meta_at && meta_at <= events_at &&
         (size <= sizes_at || data[sizes_at] == kEventPayloads);
}

bool compress(const std::vector<uint8_t>& slp, std::vector<uint8_t>& out, int level) {
  if (slp.size() < 16 || !is_slp(slp.data(), slp.size())) return false;
  const size_t raw_len = be32(slp.data() + 11);
  const size_t meta_at = 15 + raw_len;
  if (raw_len == 0 || slp.size() < meta_at || slp[15] != kEventPayloads) return false;
  std::array<uint16_t, 256> sizes;
  size_t types = 0;
  if (!event_sizes(slp.data() + 15, slp.size() - 15, sizes, types)) return false;
  const size_t sizes_len = 2 + types * 3;
  const size_t start_at = 15 + sizes_len, start_len = (size_t)sizes[kGameStart] + 1;
  if (slp.size() < start_at + start_len || slp[start_at] != kGameStart) return false;
  const size_t events_at = start_at + start_len;
  if (meta_at < events_at) return false;

  std::vector<uint8_t> columns;
  if (!reorder(slp.data() + events_at, meta_at - events_at, sizes, columns)) return false;
  out.assign(24, 0);
  put_be32(out.data(), kVersion);
  put_be32(out.data() + 4, (uint32_t)out.size());
  out.insert(out.end(), slp.begin() + 15, slp.begin() + 15 + sizes_len);
  put_be32(out.data() + 8, (uint32_t)out.size());
  out.insert(out.end(), slp.begin() + start_at, slp.begin() + events_at);
  put_be32(out.data() + 12, (uint32_t)out.size());
  out.insert(out.end(), slp.begin() + meta_at, slp.end());
  put_be32(out.data() + 16, (uint32_t)out.size());
  put_be32(out.data() + 20, (uint32_t)columns.size());
  const size_t head = out.size();
  out.resize(head + ZSTD_compressBound(columns.size()));
  const size_t z = ZSTD_compress(out.data() + head, out.size() - head, columns.data(), columns.size(), level);
  if (ZSTD_isError(z)) return false;
  out.resize(head + z);
  return true;
}

bool decompress(const std::vector<uint8_t>& slpz, std::vector<uint8_t>& out) {
  if (!is_slpz(slpz.data(), slpz.size())) return false;
  const size_t sizes_at = be32(slpz.data() + 4), start_at = be32(slpz.data() + 8), meta_at = be32(slpz.data() + 12);
  const size_t events_at = be32(slpz.data() + 16), columns_size = be32(slpz.data() + 20);
  if (events_at > slpz.size() || slpz[sizes_at] != kEventPayloads) return false;
  std::array<uint16_t, 256> sizes;
  size_t types = 0;
  if (!event_sizes(slpz.data() + sizes_at, start_at - sizes_at, sizes, types)) return false;
  std::vector<uint8_t> columns(columns_size);
  const size_t got = ZSTD_decompress(columns.data(), columns.size(), slpz.data() + events_at, slpz.size() - events_at);
  if (ZSTD_isError(got) || got != columns_size) return false;
  out.clear();
  out.reserve(columns_size + slpz.size());
  out.insert(out.end(), kRawHeader, kRawHeader + 11);
  out.insert(out.end(), 4, 0);
  out.insert(out.end(), slpz.begin() + sizes_at, slpz.begin() + start_at);
  out.insert(out.end(), slpz.begin() + start_at, slpz.begin() + meta_at);
  if (!unorder(columns.data(), columns.size(), sizes, out)) return false;
  const size_t meta_in_slp = out.size();
  out.insert(out.end(), slpz.begin() + meta_at, slpz.begin() + events_at);
  put_be32(out.data() + 11, (uint32_t)(meta_in_slp - 15));
  return true;
}

bool read_replay(const std::string& utf8_path, std::vector<uint8_t>& slp) {
  std::vector<uint8_t> bytes;
  if (!read_file(utf8_path, bytes)) return false;
  if (is_slp(bytes.data(), bytes.size())) { slp = std::move(bytes); return true; }
  return decompress(bytes, slp);
}

std::string compress_file(const std::string& utf8_slp_path, int level) {
  namespace fs = std::filesystem;
  std::vector<uint8_t> slp, slpz, check;
  if (!read_file(utf8_slp_path, slp) || !compress(slp, slpz, level) || !decompress(slpz, check) || check != slp) return {};
  fs::path out = fs::u8path(utf8_slp_path);
  out.replace_extension(".slpz");
  const fs::path part = fs::path(out).concat(".part");
  {
    std::ofstream f(part, std::ios::binary | std::ios::trunc);
    f.write((const char*)slpz.data(), (std::streamsize)slpz.size());
    if (!f) { std::error_code ec; fs::remove(part, ec); return {}; }
  }
  std::error_code ec;
  fs::rename(part, out, ec);
  if (ec) { fs::remove(part, ec); return {}; }
  // Only once the .slpz is in place and reads back as the same replay.
  std::vector<uint8_t> back;
  if (!read_replay(out.u8string(), back) || back != slp) { fs::remove(out, ec); return {}; }
  fs::remove(fs::u8path(utf8_slp_path), ec);
  return out.u8string();
}

}  // namespace slpz
