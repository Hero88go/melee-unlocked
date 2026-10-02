// "20XX CPUs" on the Static Recomp: the 20XX Hack Pack's own CPU AI block, read from the player's copy
// of the pack's disc under Mods and run for offline matches. The block itself never ships with the
// port: only its size and hash are here, and the bytes come from the player's disc at boot.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// How it runs (hackpack_ai.cpp): at every match start (StartMelee, through the dispatch table) the
// block is placed in the scene's object heap, its last word becomes the branch back into
// Fighter_procInput, and the game's own instruction at the pack's entry site (the store of the CPU's
// buttons) becomes a branch into the block; procInput then runs from RAM (interpreted) so the branch
// is followed. Off, online or in replay playback nothing is written and the retail word stays.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace host::hackpack_ai {

// ---- the rules, pure and header-only (port/tests/hackpack_ai_test.cpp runs them without the game) ----
namespace rules {

constexpr uint32_t kBlobSize = 20804;   // /ai_engine.bin on the 20XX Hack Pack 5.0.2 disc
constexpr char kBlobSha256[] = "c9b4489c0a58f2ee1a01d1567f173e597140f05361f3c73c66bfc8705dfac84f";
constexpr uint32_t kProcInput = 0x8006AD10u;    // Fighter_procInput, the function holding the entry site
constexpr uint32_t kEntrySite = 0x8006B008u;    // stw r3, 0x65C(r31): the CPU's buttons for the frame
constexpr uint32_t kEntryWord = 0x907F065Cu;    // the game's own instruction there
constexpr uint32_t kReturnSite = 0x8006B00Cu;   // the block's last word branches back here
constexpr uint32_t kMemAlloc = 0x8037F1E4u;     // HSD_MemAlloc
constexpr uint32_t kStartMelee = 0x8016E730u;   // fn_8016E730, the match start

// The pack's settings the block reads (all 32-bit loads), with the values the pack itself defaults
// to. Written only when the block is in effect; all of them are harmless offline in VS.
struct Setting { uint32_t address, value; };
constexpr Setting kDefaults[] = {
  {0x80003374u, 0},   {0x803FAED0u, 4},   {0x803FBAB8u, 6},   {0x803FA320u, 0},   {0x803FA324u, 0},
  {0x803FA330u, 100}, {0x803FA334u, 100}, {0x803FAEBCu, 15},  {0x803FAEC0u, 10},  {0x803FAEC4u, 10},
  {0x803FAF5Cu, 10},  {0x803FAF74u, 0},   {0x803FAF78u, 0},
};
constexpr size_t kDefaultCount = sizeof kDefaults / sizeof kDefaults[0];

// SHA-256, lower-case hex. Self-contained so the unit test needs no BCrypt.
inline std::string sha256_hex(const uint8_t* data, size_t size) {
  static const uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  const auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  const auto block = [&](const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  };
  size_t done = 0;
  for (; done + 64 <= size; done += 64) block(data + done);
  uint8_t tail[128] = {};
  const size_t rest = size - done;
  if (rest) std::memcpy(tail, data + done, rest);
  tail[rest] = 0x80;
  const size_t total = rest + 1 + 8 <= 64 ? 64 : 128;
  const uint64_t bits = (uint64_t)size * 8;
  for (int j = 0; j < 8; ++j) tail[total - 1 - j] = (uint8_t)(bits >> (8 * j));
  block(tail);
  if (total == 128) block(tail + 64);
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (uint32_t v : h)
    for (int j = 28; j >= 0; j -= 4) out += digits[(v >> j) & 15];
  return out;
}

// The bytes read from the disc are the pack's AI block: the known size and hash, nothing else.
inline bool verify_blob(const std::vector<uint8_t>& bytes, std::string* why = nullptr) {
  if (bytes.size() != kBlobSize) {
    if (why) *why = "size " + std::to_string(bytes.size()) + ", expected " + std::to_string(kBlobSize);
    return false;
  }
  if (sha256_hex(bytes.data(), bytes.size()) != kBlobSha256) {
    if (why) *why = "its contents are not the known AI block (hash differs)";
    return false;
  }
  return true;
}

// PowerPC `b target` at `from`: a 26-bit signed word displacement, so the target has to lie within
// 32 MB either way and on a word boundary.
inline bool branch_in_range(uint32_t from, uint32_t to) {
  const int64_t d = (int64_t)to - (int64_t)from;
  return !(d & 3) && d >= -0x2000000ll && d <= 0x1FFFFFCll;
}
inline uint32_t encode_branch(uint32_t from, uint32_t to) {
  if (!branch_in_range(from, to)) return 0;
  return 0x48000000u | ((to - from) & 0x03FFFFFCu);
}
inline bool is_branch(uint32_t word) { return (word & 0xFC000003u) == 0x48000000u; }   // b: relative, no link
inline uint32_t branch_target(uint32_t at, uint32_t word) {
  uint32_t li = word & 0x03FFFFFCu;
  if (li & 0x02000000u) li |= 0xFC000000u;
  return at + li;
}

// The block's last word, as the pack ships it a bare `b` (0x48000000), becomes the branch back to
// the instruction after the entry site, for a copy of the block placed at `base`.
inline bool patch_return_branch(std::vector<uint8_t>& blob, uint32_t base) {
  if (blob.size() != kBlobSize) return false;
  const uint32_t word = encode_branch(base + kBlobSize - 4, kReturnSite);
  if (!word) return false;
  uint8_t* last = blob.data() + kBlobSize - 4;
  last[0] = (uint8_t)(word >> 24); last[1] = (uint8_t)(word >> 16); last[2] = (uint8_t)(word >> 8); last[3] = (uint8_t)word;
  return true;
}

// The entry site may only be written over the game's own instruction, or over the branch this
// loader wrote itself (`ours`, 0 when it wrote none). Anything else is another code's and stays.
inline bool entry_site_free(uint32_t word, uint32_t ours) { return word == kEntryWord || (ours != 0 && word == ours); }

}  // namespace rules

// ---- the loader (hackpack_ai.cpp, Static Recomp only; the Source Port never calls these) ----
struct Status {
  bool looked = false;       // the Mods folder result has been read at least once
  bool disc_found = false;   // a 20XX Hack Pack disc is under Mods
  bool blob_ok = false;      // its AI block was read and verified
  std::string disc_path;
  std::string message;       // one plain line for the Game tab when blob_ok is false
};
// After the Mods folder scan at boot: find the pack's disc, read and verify its AI block, and put the
// match-start hook in place. Logs what it found.
void boot();
// The option was turned on mid-session (or the Mods folder changed): look for the disc again.
void reload();
Status status();
// The option (or MELEE_TEST_20XX_AI=1) is on, this is an offline session (no online mode, no replay
// playback, not a mod boot disc) and the block is ready: what the next match start will apply.
bool effective();

}  // namespace host::hackpack_ai
