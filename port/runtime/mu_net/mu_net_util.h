// Small shared pieces of mu_net: fixed byte arrays, bounds-checked little-endian reading and writing, hex.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mu_net {

using Bytes16 = std::array<uint8_t, 16>;
using Bytes24 = std::array<uint8_t, 24>;
using Bytes32 = std::array<uint8_t, 32>;
using Bytes64 = std::array<uint8_t, 64>;

// The mu_net wire is little-endian throughout. Only the replies handed to the game (the B0 and B3
// contracts) are big-endian, because the game reads them that way.
struct Writer {
  std::vector<uint8_t>& out;
  explicit Writer(std::vector<uint8_t>& target) : out(target) {}
  void u8(uint8_t v) { out.push_back(v); }
  void u16(uint16_t v) { out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); }
  void u64(uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back((uint8_t)(v >> (8 * i))); }
  void i32(int32_t v) { u32((uint32_t)v); }
  void bytes(const uint8_t* data, size_t size) { if (size) out.insert(out.end(), data, data + size); }
  // A string with a one-byte length in front; longer text is cut at `max` bytes (max <= 255).
  void str8(const std::string& text, size_t max) {
    const size_t n = text.size() < max ? text.size() : max;
    u8((uint8_t)n);
    bytes(reinterpret_cast<const uint8_t*>(text.data()), n);
  }
};

// Every read checks the remaining length. After the first short read `ok` is false, every later
// read returns zero, and nothing past the end of the buffer is ever touched.
struct Reader {
  const uint8_t* data;
  size_t size;
  size_t at = 0;
  bool ok = true;
  Reader(const uint8_t* bytes, size_t length) : data(bytes), size(length) {}
  bool need(size_t n) {
    if (!ok || size - at < n) { ok = false; return false; }
    return true;
  }
  uint8_t u8() { return need(1) ? data[at++] : 0; }
  uint16_t u16() {
    if (!need(2)) return 0;
    const uint16_t v = (uint16_t)(data[at] | (data[at + 1] << 8));
    at += 2;
    return v;
  }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)data[at + i] << (8 * i);
    at += 4;
    return v;
  }
  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)data[at + i] << (8 * i);
    at += 8;
    return v;
  }
  int32_t i32() { return (int32_t)u32(); }
  bool bytes(uint8_t* to, size_t n) {
    if (!need(n)) { if (n) std::memset(to, 0, n); return false; }
    if (n) std::memcpy(to, data + at, n);
    at += n;
    return true;
  }
  bool str8(std::string& to, size_t max) {
    const size_t n = u8();
    if (!ok || n > max || !need(n)) { ok = false; to.clear(); return false; }
    to.assign(reinterpret_cast<const char*>(data + at), n);
    at += n;
    return true;
  }
  bool done() const { return ok && at == size; }
};

inline void put_be32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back((uint8_t)(v >> 24)); out.push_back((uint8_t)(v >> 16));
  out.push_back((uint8_t)(v >> 8)); out.push_back((uint8_t)v);
}
inline uint32_t get_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline std::string to_hex(const uint8_t* data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string text(size * 2, '0');
  for (size_t i = 0; i < size; ++i) { text[2 * i] = digits[data[i] >> 4]; text[2 * i + 1] = digits[data[i] & 15]; }
  return text;
}
inline bool from_hex(const char* text, size_t length, uint8_t* out, size_t size) {
  if (length != size * 2) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < size; ++i) {
    const int hi = nibble(text[2 * i]), lo = nibble(text[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi * 16 + lo);
  }
  return true;
}
template <size_t N> bool all_zero(const std::array<uint8_t, N>& value) {
  uint8_t any = 0;
  for (uint8_t b : value) any |= b;
  return any == 0;
}

// The system random generator (peer_identity.cpp). False when Windows refuses, which callers treat
// as a failed start: there is no fallback generator.
bool random_bytes(void* out, size_t size);

}  // namespace mu_net
