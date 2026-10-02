// SPDX-License-Identifier: GPL-2.0-or-later
#include "mod_scan.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

namespace source_port::mods {
namespace fs = std::filesystem;

// ================================================================================================
// Small helpers
namespace {
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}
std::string trim(std::string s) {
  const auto keep = [](unsigned char c) { return !std::isspace(c) && c != 0; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), keep));
  s.erase(std::find_if(s.rbegin(), s.rend(), keep).base(), s.end());
  return s;
}
// One line of the cache, the log or detected.json never holds a tab or a line break.
std::string one_line(std::string s) {
  for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}
uint64_t fnv64(const uint8_t* p, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ull; }
  return h;
}
bool read_at(std::ifstream& in, uint64_t offset, void* dst, size_t size) {
  in.clear();
  in.seekg((std::streamoff)offset);
  return in.good() && (bool)in.read(static_cast<char*>(dst), (std::streamsize)size);
}
std::string display_path(const fs::path& p) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(p, ec);
  const fs::path here = fs::current_path(ec);
  const fs::path relative = absolute.lexically_relative(here);
  if (!relative.empty() && relative.native().rfind(L"..", 0) != 0) return relative.u8string();
  return absolute.u8string();
}

// BCrypt hash of any algorithm, lower-case hex.
class Hasher {
 public:
  explicit Hasher(LPCWSTR algorithm) {
    if (BCryptOpenAlgorithmProvider(&alg_, algorithm, nullptr, 0) < 0) { alg_ = nullptr; return; }
    DWORD size = 0, got = 0;
    if (BCryptGetProperty(alg_, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof size, &got, 0) < 0) return;
    object_.resize(size);
    if (BCryptCreateHash(alg_, &hash_, object_.data(), size, nullptr, 0, 0) < 0) hash_ = nullptr;
  }
  ~Hasher() {
    if (hash_) BCryptDestroyHash(hash_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
  }
  Hasher(const Hasher&) = delete;
  Hasher& operator=(const Hasher&) = delete;
  void update(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (hash_ && size) {
      const ULONG n = (ULONG)std::min<size_t>(size, 1u << 30);
      if (BCryptHashData(hash_, const_cast<PUCHAR>(p), n, 0) < 0) { BCryptDestroyHash(hash_); hash_ = nullptr; return; }
      p += n; size -= n;
    }
  }
  void update(const std::string& text) { update(text.data(), text.size()); }
  std::string hex() {
    if (!hash_) return {};
    DWORD length = 0, got = 0;
    if (BCryptGetProperty(alg_, BCRYPT_HASH_LENGTH, (PUCHAR)&length, sizeof length, &got, 0) < 0 || length > 64) return {};
    uint8_t digest[64];
    const bool ok = BCryptFinishHash(hash_, digest, length, 0) >= 0;
    BCryptDestroyHash(hash_); hash_ = nullptr;
    if (!ok) return {};
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (DWORD i = 0; i < length; ++i) { out += digits[digest[i] >> 4]; out += digits[digest[i] & 15]; }
    return out;
  }
 private:
  BCRYPT_ALG_HANDLE alg_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
  std::vector<uint8_t> object_;
};

bool read_file_bytes(const fs::path& file, std::vector<uint8_t>* out, uint64_t limit) {
  std::error_code ec;
  const uint64_t size = fs::file_size(file, ec);
  if (ec || size > limit) return false;
  std::ifstream in(file, std::ios::binary);
  out->resize((size_t)size);
  return in && (size == 0 || (bool)in.read((char*)out->data(), (std::streamsize)size));
}
}  // namespace

// ================================================================================================
// The game's save block cipher (sysdolphin/baselib/crypt.c: HSD_Checksum, HSD_Encrypt, HSD_Decrypt)
namespace {
const uint8_t kCryptKeys[13] = {0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E};

void block_checksum(const uint8_t* src, size_t len, uint8_t out[16]) {
  static const uint8_t seed[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                   0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
  std::memcpy(out, seed, 16);
  for (size_t i = 0; i < len; ++i) out[i % 16] = (uint8_t)(out[i % 16] + src[i]);
  for (int i = 1; i < 16; ++i)
    if (out[i - 1] == out[i]) out[i] ^= 0xFF;
}

uint8_t encrypt_byte(uint8_t prev, uint8_t cur) {
  const uint32_t v = (uint32_t)(prev ^ cur ^ kCryptKeys[prev % 13]);
  switch (prev % 7) {
  case 0: return (uint8_t)((v & 1) | ((v << 3) & 0x10) | ((v >> 1) & 2) | ((v << 2) & 0x20) | ((v >> 2) & 4) |
                           ((v << 1) & 0x40) | ((v >> 3) & 8) | (v & 0x80));
  case 1: return (uint8_t)(((v << 3) & 8) | ((v >> 1) & 1) | (v & 4) | ((v << 3) & 0x40) | ((v << 1) & 0x20) |
                           ((v >> 1) & 0x10) | ((v << 1) & 0x80) | ((v >> 6) & 2));
  case 2: return (uint8_t)(((v << 6) & 0x40) | ((v << 4) & 0x20) | ((v >> 2) & 1) | ((v >> 2) & 2) | ((v >> 1) & 8) |
                           ((v << 2) & 0x80) | ((v >> 4) & 4) | ((v >> 3) & 0x10));
  case 3: return (uint8_t)(((v << 1) & 2) | ((v << 2) & 8) | ((v << 5) & 0x80) | ((v << 1) & 0x10) | ((v >> 4) & 1) |
                           ((v >> 3) & 4) | ((v >> 1) & 0x20) | ((v >> 1) & 0x40));
  case 4: return (uint8_t)(((v << 7) & 0x80) | ((v << 1) & 4) | ((v << 3) & 0x20) | ((v >> 3) & 1) | ((v << 2) & 0x40) |
                           ((v >> 4) & 2) | ((v >> 2) & 0x10) | ((v >> 4) & 8));
  case 5: return (uint8_t)(((v & 1) << 5) | ((v << 5) & 0x40) | ((v << 2) & 0x10) | (v & 8) | ((v << 3) & 0x80) |
                           ((v >> 5) & 1) | ((v >> 5) & 2) | ((v >> 5) & 4));
  default: return (uint8_t)(((v << 2) & 4) | (v & 2) | ((v & 4) << 4) | ((v << 4) & 0x80) | (v & 0x10) |
                            ((v >> 2) & 8) | ((v >> 6) & 1) | ((v >> 2) & 0x20));
  }
}

uint8_t decrypt_byte(uint8_t prev, uint8_t in) {
  const uint32_t c = in;
  uint32_t v;
  switch (prev % 7) {
  case 0: v = (c & 1) | ((c << 1) & 4) | ((c << 2) & 0x10) | ((c << 3) & 0x40) | ((c >> 3) & 2) | ((c >> 2) & 8) |
              ((c >> 1) & 0x20) | (c & 0x80); break;
  case 1: v = ((c << 1) & 2) | ((c << 6) & 0x80) | (c & 4) | ((c >> 3) & 1) | ((c << 1) & 0x20) | ((c >> 1) & 0x10) |
              ((c >> 3) & 8) | ((c >> 1) & 0x40); break;
  case 2: v = ((c & 1) << 2) | ((c << 2) & 8) | ((c << 4) & 0x40) | ((c << 1) & 0x10) | ((c << 3) & 0x80) |
              ((c >> 4) & 2) | ((c >> 6) & 1) | ((c >> 2) & 0x20); break;
  case 3: v = ((c << 4) & 0x10) | ((c >> 1) & 1) | ((c << 3) & 0x20) | ((c >> 2) & 2) | ((c >> 1) & 8) |
              ((c << 1) & 0x40) | ((c << 1) & 0x80) | ((c >> 5) & 4); break;
  case 4: v = ((c << 3) & 8) | ((c << 4) & 0x20) | ((c >> 1) & 2) | ((c << 4) & 0x80) | ((c << 2) & 0x40) |
              ((c >> 3) & 4) | ((c >> 2) & 0x10) | ((c >> 7) & 1); break;
  case 5: v = ((c & 1) << 5) | ((c << 5) & 0x40) | ((c & 4) << 5) | (c & 8) | ((c >> 2) & 4) | ((c >> 5) & 1) |
              ((c >> 5) & 2) | ((c >> 3) & 0x10); break;
  default: v = ((c << 6) & 0x40) | (c & 2) | ((c >> 2) & 1) | ((c << 2) & 0x20) | (c & 0x10) | ((c << 2) & 0x80) |
               ((c >> 4) & 4) | ((c >> 4) & 8); break;
  }
  return (uint8_t)((v & 0xFF) ^ kCryptKeys[prev % 13] ^ prev);
}
}  // namespace

void encrypt_block(uint8_t* block, size_t size) {
  if (size <= 16) return;
  block_checksum(block + 16, size - 16, block);
  for (size_t i = 16; i < size; ++i) block[i] = encrypt_byte(block[i - 1], block[i]);
}

bool decrypt_block(uint8_t* block, size_t size) {
  if (size <= 16) return false;
  uint8_t prev = block[15];
  for (size_t i = 16; i < size; ++i) {
    const uint8_t cur = block[i];
    block[i] = decrypt_byte(prev, cur);
    prev = cur;
  }
  uint8_t check[16];
  block_checksum(block + 16, size - 16, check);
  return std::memcmp(check, block, 16) == 0;
}

// ================================================================================================
// Melee saves
namespace {
constexpr uint32_t kSector = 0x2000;
constexpr uint32_t kSaveBlocks = 11;                   // Melee's save: the header block and ten data blocks
constexpr uint32_t kGameDataSize = 0x1790;              // GmSaveData (logical block 1)
constexpr uint32_t kBankSize = 0x1F2C;                  // NameTagDataBank: 19 name tags of 0x1A4 (blocks 2-8)
constexpr uint32_t kSlotSize = 0x1A4;
constexpr uint32_t kBankBase = 0x8045D6B8u;             // where the console loads bank 0; the seven banks follow
constexpr const char kMeleeSaveName[] = "SuperSmashBros0110290334";

// The HSD card library's sequence order (fn_803ACB74): the newer of two copies, with wraparound.
bool seq_newer(int a, int b) {
  if (b < 0) return true;
  if (a == 0 && b == 0xFF) return true;
  if (a == 0xFF && b == 0) return false;
  const int d = a - b;
  if (d > 0x80) return false;
  if (d < -0x80) return true;
  return d > 0;
}

std::string gci_code(const uint8_t* h) { return std::string((const char*)h, 6); }
std::string gci_name(const uint8_t* h) { return std::string((const char*)h + 8, strnlen((const char*)h + 8, 32)); }

// A card file: 64-byte directory entry, then whole 8 KiB blocks.
bool looks_like_gci(const uint8_t* h, uint64_t size) {
  if (size < 64 + kSector || (size - 64) % kSector) return false;
  const uint32_t blocks = be16(h + 0x38);
  if (!blocks || 64ull + (uint64_t)blocks * kSector != size || h[6] != 0xFF) return false;
  for (int i = 0; i < 6; ++i)
    if (!std::isalnum(h[i])) return false;
  return true;
}
}  // namespace

bool read_melee_save(const std::vector<uint8_t>& gci, MeleeSave* out, std::string* why) {
  std::string local;
  if (!why) why = &local;
  if (gci.size() < 64 || !looks_like_gci(gci.data(), gci.size())) { *why = "not a memory card file"; return false; }
  if (gci_code(gci.data()) != "GALE01" || gci_name(gci.data()) != kMeleeSaveName) {
    *why = "not a Melee NTSC save";
    return false;
  }
  if (be16(gci.data() + 0x38) != kSaveBlocks) { *why = "unexpected Melee save size"; return false; }
  MeleeSave save;
  std::memcpy(save.header, gci.data(), 64);
  save.seq.fill(-1);
  std::vector<uint8_t> block(kSector);
  for (uint32_t k = 1; k < kSaveBlocks; ++k) {
    std::memcpy(block.data(), gci.data() + 64 + (size_t)k * kSector, kSector);
    if (!decrypt_block(block.data(), kSector)) continue;   // a torn or foreign block: the game skips it too
    const uint32_t id = be16(block.data() + 0x10);
    const int seq = block[0x12];
    if (id < 1 || id > 8 || !seq_newer(seq, save.seq[id])) continue;
    const uint32_t size = id == 1 ? kGameDataSize : kBankSize;
    save.section[id].assign(block.begin() + 0x20, block.begin() + 0x20 + size);
    save.seq[id] = seq;
  }
  if (save.seq[1] < 0) { *why = "the save's game data block is damaged"; return false; }
  *out = std::move(save);
  return true;
}

// ================================================================================================
// 20XX TE v2d r4: its code payload
//
// The regions 20XX TE's official source writes (every !loc block of source/20xxte.mgc and the files
// it includes, 20XXTE repository at the v2d r4 release): the ACE loaders, the datacopy tables, the
// Gecko codehandler, the v1.00/v1.01/v1.02 code lists, its strings, menus and tables; 23 regions,
// 39,962 bytes, all inside the seven name tag banks (console addresses 0x8045D6B8..0x8046B0EC). The
// game data block (records, settings, unlocks, logical block 1) holds none of it. Each region is split
// at name tag slots (0x1A4 bytes), because the game rewrites a whole slot when a name tag is made or
// played with; the hash is FNV-1a 64 of that slot's payload bytes in the official 20XXTE-v2d-r4-USA.gci.
namespace {
struct TeSegment { uint32_t address; uint16_t length; uint64_t fnv; };
const TeSegment kTePayload[] = {
  {0x8045D850u, 0x00Cu, 0x91D1C0D44BA046BFull}, {0x8045D85Cu, 0x10Cu, 0xFCC2F7E837EEC725ull}, {0x8045DA00u, 0x114u, 0x9AABADD01915AD78ull},
  {0x8045E610u, 0x038u, 0x4217FE2E840029B3ull}, {0x8045E6E0u, 0x040u, 0x8140C36B3899A687ull}, {0x8045E720u, 0x1A4u, 0x92E0A7C5F2ECF257ull},
  {0x8045E8C4u, 0x1A4u, 0xA510E1DE66F494D7ull}, {0x8045EA68u, 0x1A4u, 0xE6AAAF47608B9913ull}, {0x8045EC0Cu, 0x1A4u, 0x6D51665F5AB1BF92ull},
  {0x8045EDB0u, 0x1A4u, 0x1C1DD36E9A014A45ull}, {0x8045EF54u, 0x1A4u, 0x4156EDF86D38010Dull}, {0x8045F0F8u, 0x1A4u, 0x546506D44A5A3209ull},
  {0x8045F29Cu, 0x1A4u, 0x79E235FDD5D10E9Bull}, {0x8045F440u, 0x128u, 0xE12A57F7C6498FE1ull}, {0x8045F8F8u, 0x034u, 0xFA52804311F6137Aull},
  {0x8045F92Cu, 0x004u, 0xB700B8C4BC8C111Full}, {0x8045F964u, 0x16Cu, 0xED89BC7F095E389Cull}, {0x8045FAD0u, 0x1A4u, 0x54DAC264886E0BF8ull},
  {0x8045FC74u, 0x1A4u, 0x9EF70D1AAF496AFBull}, {0x8045FE18u, 0x0ACu, 0xE387876F24F5D17Aull}, {0x80460664u, 0x070u, 0x4645EE198AA3F013ull},
  {0x80460864u, 0x070u, 0xD1B3F898681FDBFDull}, {0x80460A64u, 0x0D4u, 0xBA4CC25822FA6CEFull}, {0x80460B38u, 0x05Cu, 0xBA7C7F63BE920F92ull},
  {0x80460C64u, 0x078u, 0x4661BB03766A575Full}, {0x80460CDCu, 0x1A4u, 0x47D0D91E32CC3FD8ull}, {0x80460E80u, 0x1A4u, 0x0A841A830CE7334Dull},
  {0x80461024u, 0x1A4u, 0xB399ADD991FE7DFBull}, {0x804611C8u, 0x1A4u, 0x92236160289620EFull}, {0x8046136Cu, 0x1A4u, 0x6AC738DD07EC6A28ull},
  {0x80461510u, 0x1A4u, 0x675D16182D49682Dull}, {0x804616B4u, 0x166u, 0x2305A56BAA618449ull}, {0x80461824u, 0x00Bu, 0x467C4DF0D49D5163ull},
  {0x80461830u, 0x028u, 0x925F7384D78A255Full}, {0x80461858u, 0x008u, 0x0A33F0ECE896CEC1ull}, {0x80461864u, 0x198u, 0x144F67D26BC67AA6ull},
  {0x804619FCu, 0x1A4u, 0xF13BAB7C78E36645ull}, {0x80461BA0u, 0x1A4u, 0xBC9EDD972A19F085ull}, {0x80461D44u, 0x1A4u, 0x965F6629FF8D294Full},
  {0x80461EE8u, 0x1A4u, 0xEDCBCC19C51A4027ull}, {0x8046208Cu, 0x1A4u, 0xDFA459A2F7BA5C96ull}, {0x80462230u, 0x1A4u, 0xD1AB930FF4D4166Dull},
  {0x804623D4u, 0x1A4u, 0xFEAE7C222FA303C1ull}, {0x80462578u, 0x0F9u, 0x1C53243BF8A5495Full}, {0x80462958u, 0x10Cu, 0x0D079D7C93A7213Aull},
  {0x80462A64u, 0x1A4u, 0x16D22E0242DCC962ull}, {0x80462C08u, 0x1A4u, 0x9DE16E41DB6676E1ull}, {0x80462DACu, 0x1A4u, 0xB0421DFA82477415ull},
  {0x80462F50u, 0x1A4u, 0xF2559CD4575941CBull}, {0x804630F4u, 0x0C8u, 0x3C5FAD0896F07029ull}, {0x80463558u, 0x088u, 0x877B7F4913380699ull},
  {0x804635E0u, 0x1A4u, 0x347AD6AF41096CC9ull}, {0x80463784u, 0x1A4u, 0xB427DEDC5912FB10ull}, {0x80463928u, 0x1A4u, 0xB8B7D02AF080165Bull},
  {0x80463ACCu, 0x1A4u, 0xBD55B3078A6E0303ull}, {0x80463C70u, 0x0E7u, 0xBD45608F1DC3B327ull}, {0x80463D58u, 0x0BCu, 0xC80B8D6BB64E730Bull},
  {0x80463E14u, 0x1A4u, 0x7351CEF5C933408Aull}, {0x80463FB8u, 0x1A4u, 0x17F75C0FBFD0FC3Bull}, {0x8046415Cu, 0x194u, 0x3450BEA9BA95CFAFull},
  {0x804642F4u, 0x00Cu, 0x6C3FDE24A7FD80B8ull}, {0x80464300u, 0x01Du, 0xD449A16C169CDD15ull}, {0x80464320u, 0x184u, 0x96E6DBA34A0A4D56ull},
  {0x804644A4u, 0x1A4u, 0x4C443D7ABF89FA58ull}, {0x80464648u, 0x1A4u, 0xF2F0C0D9EC1445E2ull}, {0x804647ECu, 0x1A4u, 0xD824719EE655392Dull},
  {0x80464990u, 0x1A4u, 0x41E60E566A49E0FCull}, {0x80464B34u, 0x198u, 0x151AA045B10B3AC6ull}, {0x80465000u, 0x020u, 0x4EB51C4285C7368Aull},
  {0x80465020u, 0x1A4u, 0x7198BE822EB8AEDCull}, {0x804651C4u, 0x1A4u, 0x3911F8B78CD3190Eull}, {0x80465368u, 0x1A4u, 0x9A4E6F73DEC70B30ull},
  {0x8046550Cu, 0x1A4u, 0x49BA8023ABDFCDA6ull}, {0x804656B0u, 0x1A4u, 0xBB48217C757FB9B7ull}, {0x80465854u, 0x1A4u, 0xE48D6C6BA1DFEC3Bull},
  {0x804659F8u, 0x1A4u, 0x81CE169B86324D80ull}, {0x80465B9Cu, 0x1A4u, 0x1DBE756DF3A110C7ull}, {0x80465D40u, 0x1A4u, 0x199C70E4A137F678ull},
  {0x80465EE4u, 0x1A4u, 0xFBA674CF5702472Eull}, {0x80466088u, 0x1A4u, 0x71CB4B2322AFC3DAull}, {0x8046622Cu, 0x1A4u, 0xAFC9FF7015553271ull},
  {0x804663D0u, 0x1A4u, 0x91BB24C240541B4Cull}, {0x80466574u, 0x1A4u, 0x6C26453B867EAA1Aull}, {0x80466718u, 0x1A4u, 0x58FA7C9AECA46C64ull},
  {0x804668BCu, 0x1A4u, 0x026702A35A4194E1ull}, {0x80466A60u, 0x1A4u, 0xC1CEF52DC1FE9723ull}, {0x80466C04u, 0x1A4u, 0xD901108F911FCFE3ull},
  {0x80466DA8u, 0x1A4u, 0x4D33B9F0A838D49Dull}, {0x80466F4Cu, 0x1A4u, 0xC920BA06D4362340ull}, {0x804670F0u, 0x1A4u, 0xDBFEBA710F1B8C05ull},
  {0x80467294u, 0x1A4u, 0x8BB7BB17E700A903ull}, {0x80467438u, 0x1A4u, 0x172FB49E04AA69AFull}, {0x804675DCu, 0x1A4u, 0x7006FD601615063Full},
  {0x80467780u, 0x1A4u, 0x601F965AB0B98938ull}, {0x80467924u, 0x1A4u, 0xADD9A9EB5EDCE19Eull}, {0x80467AC8u, 0x1A4u, 0xF0348B759293B7BFull},
  {0x80467C6Cu, 0x1A4u, 0x69DF05F3373FFA46ull}, {0x80467E10u, 0x1A4u, 0x0716FB5941208861ull}, {0x80467FB4u, 0x1A4u, 0x291D8FAB02A00C3Aull},
  {0x80468158u, 0x1A4u, 0x77B3A2036C4399C4ull}, {0x804682FCu, 0x1A4u, 0x00698B7C15B831B3ull}, {0x804684A0u, 0x1A4u, 0xB70292653ABED92Bull},
  {0x80468644u, 0x0BCu, 0x982682791CE7346Aull}, {0x80468C64u, 0x070u, 0xC7DBD56E6DDA7213ull}, {0x80468CD4u, 0x1A4u, 0xAE4EF7BF915990C5ull},
  {0x80468E78u, 0x1A4u, 0x40F67735156DDCC8ull}, {0x8046901Cu, 0x1A4u, 0xA4FC6533B5C2F1E0ull}, {0x804691C0u, 0x1A4u, 0xE3091745773E0953ull},
  {0x80469364u, 0x1A4u, 0x57FF0FA7FD87C9A3ull}, {0x80469508u, 0x1A4u, 0xBE70001291480877ull}, {0x804696ACu, 0x1A4u, 0xC5A64DBCEEA9824Dull},
  {0x80469850u, 0x1A4u, 0x93AA2ECA0902F576ull}, {0x804699F4u, 0x0F8u, 0x0E2C95A5E90A9B78ull}, {0x8046AB00u, 0x100u, 0xAD7D1EA088E90359ull},
  {0x8046AC00u, 0x060u, 0xD50DB497A2F4BFE4ull}, {0x8046AD00u, 0x0A4u, 0x6A9B9FF32FD29D13ull}, {0x8046ADA4u, 0x0BCu, 0x079B6D5B21C272A7ull},
  {0x8046AF00u, 0x048u, 0xA608239A14C0A05Bull}, {0x8046AF48u, 0x128u, 0x2873CCB4C6EE7E68ull},
};
constexpr const char kTeSupported[] = "v2d r4";
constexpr const char kTeTitle[] = "20XX Tournament Edition ";   // TE's title screen text, then its version
constexpr const char kTeSettingsName[] = "20XXTESettings";       // the save name TE switches the game to
constexpr const char kTeReleases[] = "https://github.com/dansalvato/20XXTE/releases";

size_t find_bytes(const std::vector<uint8_t>& hay, const char* needle) {
  const size_t n = std::strlen(needle);
  const auto it = std::search(hay.begin(), hay.end(), needle, needle + n);
  return it == hay.end() ? std::string::npos : (size_t)(it - hay.begin());
}

// "v.2d 4" (TE's own text) as "v2d r4".
std::string te_version_text(const std::string& raw) {
  std::string v = trim(raw);
  if (v.rfind("v.", 0) == 0) v = "v" + v.substr(2);
  const size_t space = v.find(' ');
  if (space != std::string::npos) {
    const std::string rev = trim(v.substr(space + 1));
    v = v.substr(0, space);
    if (!rev.empty() && std::all_of(rev.begin(), rev.end(), [](unsigned char c) { return std::isdigit(c); }))
      v += " r" + rev;
    else if (!rev.empty()) v += " " + rev;
  }
  return v;
}
}  // namespace

TeCheck check_te(const std::vector<uint8_t>& gci) {
  TeCheck check;
  MeleeSave save;
  if (!read_melee_save(gci, &save)) return check;
  check.melee_save = true;
  std::vector<uint8_t> banks((size_t)kBankSize * 7, 0);
  for (int j = 0; j < 7; ++j)
    if (save.seq[2 + j] >= 0) std::memcpy(banks.data() + (size_t)j * kBankSize, save.section[2 + j].data(), kBankSize);
  for (const auto& segment : kTePayload) {
    check.payload_bytes += segment.length;
    const size_t offset = segment.address - kBankBase;
    if (offset + segment.length <= banks.size() && fnv64(banks.data() + offset, segment.length) == segment.fnv)
      check.matching_bytes += segment.length;
  }
  const size_t title = find_bytes(banks, kTeTitle);
  if (title != std::string::npos) {
    std::string raw;
    for (size_t i = title + std::strlen(kTeTitle); i < banks.size() && raw.size() < 16; ++i) {
      if (banks[i] < 0x20 || banks[i] > 0x7E) break;
      raw += (char)banks[i];
    }
    check.version = te_version_text(raw);
  }
  check.te = title != std::string::npos || find_bytes(banks, kTeSettingsName) != std::string::npos || check.supported();
  if (check.supported() && check.version.empty()) check.version = kTeSupported;
  return check;
}

TeCheck check_te_file(const fs::path& gci) {
  std::vector<uint8_t> bytes;
  if (!read_file_bytes(gci, &bytes, 64ull + 2048ull * kSector)) return {};
  return check_te(bytes);
}

// ================================================================================================
// Discs
namespace {
constexpr uint32_t kDiscMagic = 0xC2339F3Du;            // every GameCube disc, at 0x1C
constexpr uint32_t kRetailDolSize = 0x4385E0u;          // host.cpp disc_has_vanilla_dol: the same check
const uint8_t kRetailDolSha1[20] = {0x08, 0xe0, 0xbf, 0x20, 0x13, 0x4d, 0xfc, 0xb2, 0x60, 0x69,
                                    0x96, 0x71, 0x00, 0x45, 0x27, 0xb2, 0xd6, 0xbb, 0x1a, 0x45};
// Whole-disc MD5s: NTSC 1.02 as dumped from the retail disc, and the official 20XX Hack Pack builds
// (from its installer's "ReadMe and MD5s.txt": each is the xdelta patch applied to that disc).
constexpr const char kVanillaMd5[] = "0e63d4223b01d9aba596259dc155a174";
struct KnownDisc { const char* hash; const char* name; const char* version; const char* id = ""; };
const KnownDisc kHackPackMd5[] = {
  {"4e400c49b68bc3474dbe7970ad4df6d9", "20XX Hack Pack", "4.0 Beta02"},
  {"f79a61286f2c845f72975bcdaafca6aa", "20XX Hack Pack", "4.0 Beta03"},
  {"8ba44dfc2ceedc4ddce636d9c9159389", "20XX Hack Pack", "4.0 Beta04"},
  {"51a29ae081ef69e6d9856d22e42777c5", "20XX Hack Pack", "4.05"},
  {"6ed757739de410658799e4ada97a31a1", "20XX Hack Pack", "4.06"},
  {"c8ccc5cd8788fe0cf8f24bca123e4a2e", "20XX Hack Pack", "4.07+"},
  {"17756217401a39227feea1ebb8542376", "20XX Hack Pack", "4.07++"},
  {"b67c7f8c107107b9db7e6d00a2b40817", "20XX Hack Pack", "5.0"},
  {"c8c019de7bcf08e096804802a9fd0693", "20XX Hack Pack", "5.0.1"},
  {"d926ba5b39551f5245fd655bc1dfeb3f", "20XX Hack Pack", "5.0.2"},
};
// m-ex builds: whole-disc SHA-256 of a known build, or its content fingerprint against the retail
// disc (the same fingerprint as source_host.cpp's iso_layer_fingerprint and kKnownPacks).
const KnownDisc kMexSha256[] = {
  {"b1b60a188421c8d0e564fa276a4762fe67e8de271ac2e131c20f70ee715af6cd", "Akaneia", "1.0.1", "akaneia"},
  // ACE is built on Akaneia 1.0.0 and keeps that title in its disc header, so by the header alone
  // it was listed as "Akaneia 1.0.0". It is named by its own bytes.
  {"0d7ba36bef3505cdf6c0209d9ae34977e23cf6cae4d24366e8d405810f798993", "ACE", "2.0.0", "ace"},
};
const KnownDisc kMexFingerprint[] = {
  {"9a1a48b999a2f8f4deaee2554e5f177cb265666b95676a508406db80b181246b", "Akaneia", "1.0.1", "akaneia"},
};
constexpr const char kTmceSupported[] = "v1.4 d1";   // the Training Mode CE compiled into the game (tmce/src/events.h)

struct DiscFile { uint32_t offset = 0, length = 0; bool dir = false; };
struct DiscInfo {
  bool ok = false;
  std::string id;                       // GALE01
  uint8_t revision = 0;
  std::string title;                    // the disc's own game name
  std::map<std::string, DiscFile> paths;   // lower-case "/dir/name" -> entry (directories too)
  uint32_t dol_offset = 0, dol_size = 0;
  uint64_t size = 0;
  bool has(const char* path) const { return paths.count(path) != 0; }
};

// The header, filesystem table and main.dol extent of a disc image, the same way the game reads them.
DiscInfo parse_disc(std::ifstream& in, uint64_t image_size) {
  DiscInfo info;
  info.size = image_size;
  uint8_t header[0x440];
  if (image_size < sizeof header || !read_at(in, 0, header, sizeof header) || be32(header + 0x1C) != kDiscMagic) return info;
  info.id.assign((const char*)header, 6);
  info.revision = header[7];
  info.title = trim(std::string((const char*)header + 0x20, strnlen((const char*)header + 0x20, 0x3E0)));
  const uint64_t fst_offset = be32(header + 0x424), fst_size = be32(header + 0x428);
  if (fst_size < 12 || fst_size > 4u * 1024u * 1024u || fst_offset > image_size || fst_size > image_size - fst_offset) return info;
  std::vector<uint8_t> fst((size_t)fst_size);
  if (!read_at(in, fst_offset, fst.data(), fst.size())) return info;
  const uint32_t count = be32(fst.data() + 8);
  if (count < 1 || (uint64_t)count * 12 > fst_size) return info;
  const size_t names = (size_t)count * 12;
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const uint8_t* e = fst.data() + (size_t)i * 12;
    const uint32_t name_offset = be32(e) & 0xFFFFFFu;
    if (names + name_offset >= fst.size()) return info;
    const char* start = (const char*)fst.data() + names + name_offset;
    const std::string name = lower(std::string(start, strnlen(start, fst.size() - names - name_offset)));
    const std::string path = dirs.back().second + "/" + name;
    DiscFile file;
    file.dir = e[0] != 0;
    file.offset = be32(e + 4);
    file.length = be32(e + 8);
    if (file.dir) {
      if (file.length <= i || file.length > dirs.back().first) return info;
      dirs.push_back({file.length, path});
    } else if ((uint64_t)file.offset + file.length > image_size) {
      return info;
    }
    info.paths[path] = file;
  }
  info.dol_offset = be32(header + 0x420);
  uint8_t dol[0x100];
  if (info.dol_offset && info.dol_offset + sizeof dol <= image_size && read_at(in, info.dol_offset, dol, sizeof dol)) {
    uint64_t extent = sizeof dol;
    for (int i = 0; i < 18; ++i) extent = std::max<uint64_t>(extent, (uint64_t)be32(dol + i * 4) + be32(dol + 0x90 + i * 4));
    if (info.dol_offset + extent <= image_size && extent <= 64u * 1024u * 1024u) info.dol_size = (uint32_t)extent;
  }
  info.ok = true;
  return info;
}

bool retail_dol_by_hash(std::ifstream& in, const DiscInfo& info) {
  if (!info.dol_offset || info.dol_offset + (uint64_t)kRetailDolSize > info.size) return false;
  std::vector<uint8_t> image(kRetailDolSize);
  if (!read_at(in, info.dol_offset, image.data(), image.size())) return false;
  Hasher sha1(BCRYPT_SHA1_ALGORITHM);
  sha1.update(image.data(), image.size());
  const std::string hex = sha1.hex();
  static const char digits[] = "0123456789abcdef";
  std::string expected;
  for (uint8_t b : kRetailDolSha1) { expected += digits[b >> 4]; expected += digits[b & 15]; }
  return hex == expected;
}

// The player's retail disc, parsed once per path, size and time.
struct BaseDisc {
  std::string identity;
  fs::path path;
  DiscInfo info;
  std::vector<std::string> files;   // non-directory paths, sorted
};
std::shared_ptr<const BaseDisc> load_base(const fs::path& iso) {
  static std::mutex mutex;
  static std::shared_ptr<const BaseDisc> cached;
  if (iso.empty()) return nullptr;
  std::error_code ec;
  const uint64_t size = fs::file_size(iso, ec);
  if (ec) return nullptr;
  const auto stamp = fs::last_write_time(iso, ec).time_since_epoch().count();
  const std::string identity = fs::absolute(iso, ec).u8string() + "|" + std::to_string(size) + "|" + std::to_string(stamp);
  std::lock_guard<std::mutex> lock(mutex);
  if (cached && cached->identity == identity) return cached;
  std::ifstream in(iso, std::ios::binary);
  if (!in) return nullptr;
  auto base = std::make_shared<BaseDisc>();
  base->identity = identity;
  base->path = iso;
  base->info = parse_disc(in, size);
  if (!base->info.ok || !base->info.dol_size) return nullptr;
  for (const auto& kv : base->info.paths) if (!kv.second.dir) base->files.push_back(kv.first);
  cached = base;
  return cached;
}

// What a disc changes against the retail disc, the same way ModOverlay::add_iso and source_host's
// iso_layer_fingerprint see it: changed and added files, main.dol byte runs, retail files it lacks.
struct LayerDiff {
  bool ok = false;
  uint32_t changed = 0, added = 0, deleted = 0, dol_runs = 0;
  std::string fingerprint;
};
LayerDiff diff_against_base(std::ifstream& in, const DiscInfo& info, const BaseDisc& base) {
  LayerDiff diff;
  std::ifstream base_in(base.path, std::ios::binary);
  if (!base_in) return diff;
  std::vector<uint8_t> a(1 << 20), b(1 << 20);
  Hasher total(BCRYPT_SHA256_ALGORITHM);
  for (const auto& kv : info.paths) {
    if (kv.second.dir) continue;
    const auto original = base.info.paths.find(kv.first);
    bool identical = false;
    if (original != base.info.paths.end() && !original->second.dir && original->second.length == kv.second.length) {
      identical = true;
      for (uint64_t pos = 0; pos < kv.second.length; pos += a.size()) {
        const uint32_t n = (uint32_t)std::min<uint64_t>(a.size(), kv.second.length - pos);
        if (!read_at(in, kv.second.offset + pos, a.data(), n) || !read_at(base_in, original->second.offset + pos, b.data(), n))
          return diff;
        if (std::memcmp(a.data(), b.data(), n)) { identical = false; break; }
      }
    }
    if (identical) continue;
    if (original == base.info.paths.end()) ++diff.added; else ++diff.changed;
    total.update(kv.first + "\t" + std::to_string(kv.second.length) + "\n");
    for (uint64_t pos = 0; pos < kv.second.length; pos += a.size()) {
      const uint32_t n = (uint32_t)std::min<uint64_t>(a.size(), kv.second.length - pos);
      if (!read_at(in, kv.second.offset + pos, a.data(), n)) return diff;
      total.update(a.data(), n);
    }
  }
  // main.dol: runs of changed bytes over the common extent, runs closer than 32 bytes merged
  if (info.dol_size && base.info.dol_size) {
    const uint64_t common = std::min(info.dol_size, base.info.dol_size);
    int64_t run_start = -1;
    uint64_t last_diff = 0;
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    for (uint64_t pos = 0; pos < common; pos += a.size()) {
      const uint32_t n = (uint32_t)std::min<uint64_t>(a.size(), common - pos);
      if (!read_at(in, info.dol_offset + pos, a.data(), n) || !read_at(base_in, base.info.dol_offset + pos, b.data(), n)) break;
      for (uint32_t k = 0; k < n; ++k) {
        if (a[k] == b[k]) continue;
        const uint64_t at = pos + k;
        if (run_start >= 0 && at - last_diff > 32) {
          runs.push_back({(uint32_t)run_start, (uint32_t)(last_diff + 1 - run_start)});
          run_start = -1;
        }
        if (run_start < 0) run_start = (int64_t)at;
        last_diff = at;
      }
    }
    if (run_start >= 0) runs.push_back({(uint32_t)run_start, (uint32_t)(last_diff + 1 - run_start)});
    for (const auto& run : runs) total.update("dol " + std::to_string(run.first) + " " + std::to_string(run.second) + "\n");
    diff.dol_runs = (uint32_t)runs.size();
  }
  for (const auto& file : base.files) {
    if (info.paths.count(file)) continue;
    total.update("deleted " + file + "\n");
    ++diff.deleted;
  }
  diff.fingerprint = total.hex();
  diff.ok = !diff.fingerprint.empty();
  return diff;
}

// main.dol byte-identical to the retail disc's (the base disc when there is one, else its SHA-1).
bool dol_is_retail(std::ifstream& in, const DiscInfo& info, const BaseDisc* base) {
  if (!base) return retail_dol_by_hash(in, info);
  if (info.dol_size != base->info.dol_size || !info.dol_size) return false;
  std::ifstream base_in(base->path, std::ios::binary);
  std::vector<uint8_t> a(1 << 20), b(1 << 20);
  for (uint64_t pos = 0; pos < info.dol_size; pos += a.size()) {
    const uint32_t n = (uint32_t)std::min<uint64_t>(a.size(), info.dol_size - pos);
    if (!read_at(in, info.dol_offset + pos, a.data(), n) || !read_at(base_in, base->info.dol_offset + pos, b.data(), n) ||
        std::memcmp(a.data(), b.data(), n))
      return false;
  }
  return true;
}

// Training Mode CE's version text in its event menu file ("TM-CE v1.4 d1"), "" when missing.
std::string tmce_version(std::ifstream& in, const DiscInfo& info) {
  const auto it = info.paths.find("/tm/eventmenu.dat");
  if (it == info.paths.end() || it->second.length > 8u * 1024u * 1024u) return {};
  std::vector<uint8_t> data(it->second.length);
  if (!read_at(in, it->second.offset, data.data(), data.size())) return {};
  const size_t at = find_bytes(data, "TM-CE v");
  if (at == std::string::npos) return {};
  std::string v;
  for (size_t i = at + 6; i < data.size() && v.size() < 24; ++i) {
    if (data[i] < 0x20 || data[i] > 0x7E) break;
    v += (char)data[i];
  }
  return trim(v);
}

bool standard_title(const std::string& title) {
  const std::string t = lower(trim(title));
  return t.empty() || t == "super smash bros melee" || t == "super smash bros. melee";
}

// Compressed disc formats Dolphin writes: RVZ, WIA, GCZ, CISO.
bool compressed_disc(const uint8_t* h, size_t n) {
  if (n < 4) return false;
  return std::memcmp(h, "RVZ\x01", 4) == 0 || std::memcmp(h, "WIA\x01", 4) == 0 ||
         (h[0] == 0x01 && h[1] == 0xC0 && h[2] == 0x0B && h[3] == 0xB1) || std::memcmp(h, "CISO", 4) == 0;
}

std::string content_key(const std::string& hash) { return hash.size() >= 16 ? hash.substr(0, 16) : hash; }

// A card mod's name: its comment when it says something other than Melee's own, else the file name.
std::string card_mod_name(const std::vector<uint8_t>& gci, const fs::path& file) {
  const uint32_t comment = be32(gci.data() + 0x3C);
  if (comment != 0xFFFFFFFFu && 64ull + comment + 64 <= gci.size()) {
    auto text = [&](size_t at) {
      std::string s;
      for (size_t i = 0; i < 32; ++i) {
        const uint8_t c = gci[at + i];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return std::string();   // not plain text (a Japanese save, say)
        s += (char)c;
      }
      return trim(s);
    };
    const std::string title = text(64 + comment), line = text(64 + comment + 32);
    const bool melee_title = lower(title).rfind("super smash bros", 0) == 0;
    const bool melee_line = lower(line).rfind("game data", 0) == 0;
    if (!(melee_title && (melee_line || line.empty()))) {
      if (melee_title) return line;
      if (!title.empty()) return line.empty() ? title : title + " " + line;
    }
  }
  return file.stem().u8string();
}

void set_support(Detected& d, Engine needs, Support status, bool can_enable) {
  d.needs = needs; d.status = status; d.can_enable = can_enable;
}
}  // namespace

const char* kind_id(Kind kind) {
  switch (kind) {
  case Kind::Te: return "te";
  case Kind::TmCe: return "tmce";
  case Kind::Vanilla: return "vanilla";
  case Kind::AssetMod: return "asset_mod";
  case Kind::Mex: return "mex";
  case Kind::HackPack: return "hack_pack";
  case Kind::CodeMod: return "code_mod";
  case Kind::CardMod: return "card_mod";
  default: return "unsupported";
  }
}
const char* engine_id(Engine engine) {
  return engine == Engine::Source ? "source" : engine == Engine::Recomp ? "recomp" : "either";
}
const char* support_id(Support support) {
  return support == Support::Supported ? "supported" : support == Support::Untested ? "untested" : "not_supported_yet";
}
bool runs_on(const Detected& d, bool source_port) {
  if (d.needs == Engine::Either) return true;
  return source_port ? d.needs == Engine::Source : d.needs == Engine::Recomp;
}

std::string hack_pack_version(const std::string& md5) {
  for (const auto& pack : kHackPackMd5) if (md5 == pack.hash) return pack.version;
  return {};
}

bool read_disc_file(const fs::path& image, const char* path, std::vector<uint8_t>* out) {
  std::error_code ec;
  const uint64_t size = fs::file_size(image, ec);
  std::ifstream in(image, std::ios::binary);
  if (ec || !in || !out) return false;
  const DiscInfo info = parse_disc(in, size);
  if (!info.ok) return false;
  const auto found = info.paths.find(lower(path));
  if (found == info.paths.end() || found->second.dir) return false;
  out->resize(found->second.length);
  return out->empty() || read_at(in, found->second.offset, out->data(), out->size());
}

// ================================================================================================
// Identifying one file
namespace {
Detected identify_gci(const fs::path& file, Detected d, std::string* log) {
  std::vector<uint8_t> bytes;
  if (!read_file_bytes(file, &bytes, 64ull + 2048ull * kSector) || bytes.size() < 64) {
    d.message = "Could not read this file.";
    return d;
  }
  Hasher sha(BCRYPT_SHA256_ALGORITHM);
  sha.update(bytes.data(), bytes.size());
  d.hash = sha.hex();
  const std::string code = gci_code(bytes.data());
  d.card = true;
  if (code == "GALE01" && gci_name(bytes.data()) == kMeleeSaveName) {
    const TeCheck te = check_te(bytes);
    if (log) *log = "20XX TE payload " + std::to_string(te.matching_bytes) + "/" + std::to_string(te.payload_bytes) +
                    " bytes, text " + (te.te ? "found" : "not found") + (te.version.empty() ? "" : ", version " + te.version);
    if (te.supported()) {
      d.kind = Kind::Te; d.name = "20XX TE"; d.version = kTeSupported; d.key = "te"; d.catalog_id = "te";
      set_support(d, Engine::Source, Support::Supported, true);
      d.message = "Detected: 20XX TE " + d.version;
      return d;
    }
    if (te.te) {
      d.kind = Kind::Te; d.name = "20XX TE"; d.version = te.version; d.key = content_key(d.hash);
      set_support(d, Engine::Either, Support::NotSupportedYet, false);
      d.message = te.version == kTeSupported
          ? std::string("This 20XX TE save was changed too much to use (its name tags were rewritten). Get a fresh copy: ") + kTeReleases
          : "Found 20XX TE, but this version is not supported. Supported: v2d r4.";
      return d;
    }
  }
  if (code == "GALE01") {
    d.kind = Kind::CardMod; d.name = card_mod_name(bytes, file); d.key = content_key(d.hash);
    set_support(d, Engine::Recomp, Support::NotSupportedYet, false);
    d.message = "Melee save found, not supported yet.";
    d.restart = true;
    return d;
  }
  d.card = false;
  d.kind = Kind::Unsupported; d.name = file.filename().u8string();
  d.message = code.rfind("GAL", 0) == 0 ? "This save is for another region of Melee. Only NTSC (USA) saves work."
                                        : "This memory card file is not a Melee save.";
  return d;
}

// *used_base: the answer depends on the retail disc it was compared with (the scan cache keys it so).
Detected identify_disc(const fs::path& file, Detected d, const ScanOptions& options, std::string* log, bool* used_base) {
  std::error_code ec;
  const uint64_t size = fs::file_size(file, ec);
  std::ifstream in(file, std::ios::binary);
  if (ec || !in || size > 16ull * 1024 * 1024 * 1024) { d.message = "Could not read this disc image."; return d; }
  const DiscInfo info = parse_disc(in, size);
  d.name = file.filename().u8string();
  if (!info.ok) { d.message = "Could not read this disc image."; return d; }
  // The whole file once: MD5 for the known builds, SHA-256 for its identity.
  {
    Hasher md5(BCRYPT_MD5_ALGORITHM), sha(BCRYPT_SHA256_ALGORITHM);
    std::vector<uint8_t> chunk(4u << 20);
    in.clear(); in.seekg(0);
    uint64_t done = 0;
    while (in) {
      in.read((char*)chunk.data(), (std::streamsize)chunk.size());
      const std::streamsize n = in.gcount();
      if (n <= 0) break;
      md5.update(chunk.data(), (size_t)n);
      sha.update(chunk.data(), (size_t)n);
      done += (uint64_t)n;
    }
    if (done != size) { d.message = "Could not read this disc image."; return d; }
    const std::string md5_hex = md5.hex();
    d.hash = sha.hex();
    in.clear();
    if (md5_hex == kVanillaMd5) {
      d.kind = Kind::Vanilla; d.name = "Melee NTSC 1.02"; d.key = content_key(d.hash);
      set_support(d, Engine::Either, Support::Supported, false);
      d.message = "Unmodified Melee 1.02 disc: there is nothing to load from it.";
      return d;
    }
    if (const std::string version = hack_pack_version(md5_hex); !version.empty()) {
      d.kind = Kind::HackPack; d.name = "20XX Hack Pack"; d.version = version; d.key = content_key(d.hash); d.catalog_id = "hackpack";
      // 5.0.2 boots and plays on the Static Recomp with its own code and no Slippi codes (offline, no
      // replays). Older builds take the same path and have not been run. The Source Port loads the
      // disc's files over the normal game (stages, music, costumes) and runs none of its code. It is
      // not a switchable layer (can_enable stays false): it loads only when the player starts it
      // from the launcher's Mods page or with --mod-iso, so the normal game is never changed by a
      // disc that is in Mods for "20XX CPUs" or its skins.
      // The Source Port side (Engine::Either) is written but has never been run: it stays off the
      // launcher until it has been played through.
      set_support(d, Engine::Recomp, version == "5.0.2" ? Support::Supported : Support::Untested, false);
      d.message = "Detected: " + d.name + " " + d.version + ". Plays on the Static Recomp, offline (no online play or replays).";
      return d;
    }
  }
  d.key = content_key(d.hash);
  if (info.id == "GTME01" && info.has("/tm/eventmenu.dat")) {
    const std::string text = tmce_version(in, info);                     // "v1.4 d1"
    d.kind = Kind::TmCe; d.name = "Training Mode CE"; d.version = text; d.key = "tmce"; d.catalog_id = "tmce"; d.restart = true;
    if (text == kTmceSupported) {
      set_support(d, Engine::Source, Support::Supported, true);
      d.message = "Detected: Training Mode CE " + text;
    } else {
      set_support(d, Engine::Source, Support::NotSupportedYet, false);
      d.key = content_key(d.hash);
      d.message = std::string("Found Training Mode CE") + (text.empty() ? "" : " " + text) +
                  ", but this version is not supported. Supported: " + kTmceSupported + ".";
    }
    return d;
  }
  if (info.id != "GALE01" || info.revision != 2) {
    d.kind = Kind::Unsupported;
    set_support(d, Engine::Either, Support::NotSupportedYet, false);
    d.message = info.id.rfind("GAL", 0) == 0 ? "This disc is another version or region of Melee. Only NTSC 1.02 works."
                                              : "This disc is not Melee.";
    return d;
  }
  const std::shared_ptr<const BaseDisc> base = load_base(options.base_iso);
  if (used_base) *used_base = true;
  const bool dol_retail = dol_is_retail(in, info, base.get());
  bool has_codes = false;
  for (const auto& kv : info.paths)
    if (!kv.second.dir && kv.first.size() > 4 && kv.first.compare(kv.first.size() - 4, 4, ".gct") == 0) has_codes = true;
  const std::string title = standard_title(info.title) ? file.stem().u8string() : info.title;
  d.restart = true;
  if (info.has("/mxdt.dat")) {
    d.kind = Kind::Mex; d.name = title;
    const KnownDisc* known = nullptr;
    for (const auto& pack : kMexSha256) if (d.hash == pack.hash) known = &pack;
    if (!known && base) {
      const LayerDiff diff = diff_against_base(in, info, *base);
      if (log) *log = "content fingerprint " + diff.fingerprint.substr(0, 16);
      for (const auto& pack : kMexFingerprint) if (diff.fingerprint == pack.hash) known = &pack;
    }
    if (known) { d.name = known->name; d.version = known->version; d.catalog_id = known->id; }
    if (known && d.hash == known->hash && used_base) *used_base = false;   // a known build by its own bytes
    set_support(d, Engine::Recomp, known && std::string(known->id) == "akaneia" ? Support::Supported : Support::Untested, true);
    d.message = "Detected: " + d.name + (d.version.empty() ? "" : " " + d.version) +
                ". Plays on the Static Recomp: launcher > Mods > Play.";
    return d;
  }
  if (!dol_retail || has_codes) {
    d.kind = Kind::CodeMod; d.name = title;
    set_support(d, Engine::Recomp, Support::NotSupportedYet, false);
    d.message = "Code mod found, not supported yet.";
    return d;
  }
  // The retail game's code: a data-only mod, or the retail game in another layout.
  if (base) {
    const LayerDiff diff = diff_against_base(in, info, *base);
    if (log) *log = std::to_string(diff.changed) + " files changed, " + std::to_string(diff.added) + " added, " +
                    std::to_string(diff.deleted) + " removed";
    if (diff.ok && !diff.changed && !diff.added && !diff.deleted && !diff.dol_runs) {
      d.kind = Kind::Vanilla; d.name = "Melee NTSC 1.02";
      set_support(d, Engine::Either, Support::Supported, false);
      d.message = "Same game files as your Melee disc: there is nothing to load from it.";
      d.restart = false;
      return d;
    }
  }
  d.kind = Kind::AssetMod; d.name = title;
  set_support(d, Engine::Either, Support::Supported, true);
  d.message = "Detected: " + title + " (changes the game's files)";
  return d;
}
// A file is listed when it is a disc image or a card save (its message is never empty then); any
// other file comes back with an empty message and is not listed.
Detected identify_file(const fs::path& file, const ScanOptions& options, std::string* log, bool* used_base) {
  Detected d;
  d.path = display_path(file);
  if (used_base) *used_base = false;
  std::error_code ec;
  const uint64_t size = fs::file_size(file, ec);
  if (ec) return d;
  uint8_t head[0x40]{};
  {
    std::ifstream in(file, std::ios::binary);
    if (!in || !in.read((char*)head, (std::streamsize)std::min<uint64_t>(sizeof head, size))) return d;
  }
  if (looks_like_gci(head, size)) return identify_gci(file, d, log);
  if (size >= 0x440 && be32(head + 0x1C) == kDiscMagic) return identify_disc(file, d, options, log, used_base);
  if (compressed_disc(head, (size_t)std::min<uint64_t>(sizeof head, size))) {
    d.kind = Kind::Unsupported; d.name = file.filename().u8string();
    d.message = "Compressed disc image: convert it to .iso in Dolphin (right-click, Convert File) to use it.";
    return d;
  }
  return d;
}
}  // namespace

Detected identify(const fs::path& file, const ScanOptions& options, std::string* log) {
  return identify_file(file, options, log, nullptr);
}

// ================================================================================================
// The scan cache: Mods/.cache/scan.txt, one line per file keyed by path, size and time (and, for
// discs, the retail disc they were compared with), so a boot never reads an unchanged disc again.
namespace {
constexpr const char kCacheHeader[] = "mu-scan 9";   // 9: the 20XX Hack Pack runs on either engine
struct CacheEntry { std::string key, base; Detected d; };

std::string cache_key(const fs::path& file) {
  std::error_code ec;
  const uint64_t size = fs::file_size(file, ec);
  const auto stamp = fs::last_write_time(file, ec).time_since_epoch().count();
  return fs::absolute(file, ec).u8string() + "|" + std::to_string(size) + "|" + std::to_string(stamp);
}

Kind kind_from(const std::string& s) {
  static const Kind kinds[] = {Kind::Te, Kind::TmCe, Kind::Vanilla, Kind::AssetMod, Kind::Mex,
                               Kind::HackPack, Kind::CodeMod, Kind::CardMod, Kind::Unsupported};
  for (Kind k : kinds) if (s == kind_id(k)) return k;
  return Kind::Unsupported;
}
Engine engine_from(const std::string& s) { return s == "source" ? Engine::Source : s == "recomp" ? Engine::Recomp : Engine::Either; }
Support support_from(const std::string& s) {
  return s == "supported" ? Support::Supported : s == "untested" ? Support::Untested : Support::NotSupportedYet;
}

std::vector<CacheEntry> load_cache(const fs::path& file) {
  std::vector<CacheEntry> out;
  std::ifstream in(file);
  std::string line;
  if (!std::getline(in, line) || line != kCacheHeader) return out;
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    size_t start = 0;
    for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
    f.push_back(line.substr(start));
    if (f.size() != 15) continue;
    CacheEntry e;
    e.key = f[0]; e.base = f[1];
    e.d.path = fs::u8path(f[2]);
    e.d.kind = kind_from(f[3]); e.d.name = f[4]; e.d.version = f[5]; e.d.hash = f[6]; e.d.key = f[7];
    e.d.needs = engine_from(f[8]); e.d.status = support_from(f[9]);
    e.d.can_enable = f[10] == "1"; e.d.card = f[11] == "1"; e.d.restart = f[12] == "1"; e.d.message = f[13]; e.d.catalog_id = f[14];
    out.push_back(std::move(e));
  }
  return out;
}

void save_cache(const fs::path& file, const std::vector<CacheEntry>& entries) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path temporary = fs::path(file) += ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    out << kCacheHeader << '\n';
    for (const auto& e : entries) {
      const Detected& d = e.d;
      out << one_line(e.key) << '\t' << one_line(e.base) << '\t' << one_line(d.path.u8string()) << '\t' << kind_id(d.kind) << '\t'
          << one_line(d.name) << '\t' << one_line(d.version) << '\t' << d.hash << '\t' << one_line(d.key) << '\t'
          << engine_id(d.needs) << '\t' << support_id(d.status) << '\t' << (d.can_enable ? 1 : 0) << '\t' << (d.card ? 1 : 0)
          << '\t' << (d.restart ? 1 : 0) << '\t' << one_line(d.message) << '\t' << one_line(d.catalog_id) << '\n';
    }
  }
  fs::rename(temporary, file, ec);
  if (ec) { fs::remove(file, ec); fs::rename(temporary, file, ec); }
}
}  // namespace

// ================================================================================================
// The player's choices
namespace {
struct ChoiceStore {
  std::mutex mutex;
  std::map<std::string, int> values;
  std::atomic<uint32_t> version{0};
  std::atomic<bool> auto_detect{false};
};
ChoiceStore& store() { static ChoiceStore* s = new ChoiceStore; return *s; }   // never destroyed: worker threads may outlive main
}  // namespace

void set_choices(const std::map<std::string, int>& values) {
  std::lock_guard<std::mutex> lock(store().mutex);
  store().values = values;
}
std::map<std::string, int> choices() {
  std::lock_guard<std::mutex> lock(store().mutex);
  return store().values;
}
uint32_t choices_version() { return store().version.load(); }
void set_auto_detect(bool on) { store().auto_detect = on; }
bool auto_detect() { return store().auto_detect; }

bool parse_choice(const std::string& key, const std::string& value, std::map<std::string, int>* values) {
  if (key == "mod_te_enabled") { (*values)["te"] = value != "0"; return true; }
  if (key == "mod_tmce_enabled") { (*values)["tmce"] = value != "0"; return true; }
  if (key == "mod_enabled") {
    const size_t space = value.find(' ');
    if (space == std::string::npos) return false;
    const std::string id = value.substr(0, space), state = trim(value.substr(space + 1));
    if (id.empty() || id.size() > 64 || id.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
    (*values)[id] = state != "0";
    return true;
  }
  return false;
}

std::string choice_lines(const std::map<std::string, int>& values) {
  std::string out;
  for (const auto& kv : values) {
    if (kv.second < 0) continue;
    if (kv.first == "te") out += "\nmod_te_enabled " + std::to_string(kv.second);
    else if (kv.first == "tmce") out += "\nmod_tmce_enabled " + std::to_string(kv.second);
    else out += "\nmod_enabled " + kv.first + " " + std::to_string(kv.second);
  }
  return out;
}

// ================================================================================================
// Scanning the Mods folder
namespace {
void apply_choices(ScanResult& result, std::map<std::string, int>* values) {
  // One copy of 20XX TE and one Training Mode CE disc are used; further copies are listed only.
  std::map<std::string, const Detected*> first;
  for (auto& d : result.items) {
    if (!d.can_enable) continue;
    const auto seen = first.find(d.key);
    if (seen != first.end()) {
      d.can_enable = false;
      d.message = (d.kind == Kind::Te || d.kind == Kind::TmCe ? "Another copy of " + d.name : "The same file as " + seen->second->name) +
                  " is in use: " + seen->second->path.u8string();
      continue;
    }
    first[d.key] = &d;
  }
  // First sight turns a pack on, except a card save while another card save is on.
  const Detected* card_on = nullptr;
  for (auto& d : result.items)
    if (d.can_enable && d.card) {
      const auto it = values->find(d.key);
      if (it != values->end() && it->second == 1 && !card_on) card_on = &d;
    }
  for (auto& d : result.items) {
    if (!d.can_enable) continue;
    auto it = values->find(d.key);
    if (it == values->end()) {
      const bool on = !(d.card && card_on);
      (*values)[d.key] = on ? 1 : 0;
      result.log.push_back(std::string(on ? "turned on " : "left off ") + d.name + " (found for the first time)");
      if (!on)
        result.notes.push_back(d.name + " was found. Only one save mod can be on at a time: switch it on here to use it "
                               "instead of " + card_on->name + ".");
      if (on && d.card) card_on = &d;
      it = values->find(d.key);
    }
    d.enabled = it->second == 1;
  }
  // Settings written by hand could turn on two card saves: the first one stays on.
  const Detected* kept = nullptr;
  for (auto& d : result.items) {
    if (!d.card || !d.enabled) continue;
    if (!kept) { kept = &d; continue; }
    d.enabled = false;
    (*values)[d.key] = 0;
    result.notes.push_back("Only one save mod can be on at a time: " + kept->name + " is on, " + d.name + " was turned off.");
  }
}
}  // namespace

ScanResult scan(const ScanOptions& options, std::map<std::string, int>* values) {
  ScanResult result;
  std::map<std::string, int> local;
  if (!values) values = &local;
  const auto begin = std::chrono::steady_clock::now();
  const fs::path cache_file = options.mods_dir / ".cache" / "scan.txt";
  std::vector<CacheEntry> cache = options.use_cache ? load_cache(cache_file) : std::vector<CacheEntry>{};
  std::vector<CacheEntry> kept_cache;
  const std::shared_ptr<const BaseDisc> base = load_base(options.base_iso);
  const std::string base_id = base ? base->identity : std::string();
  std::vector<fs::path> files;
  std::error_code ec;
  if (fs::is_directory(options.mods_dir, ec)) {
    fs::recursive_directory_iterator it(options.mods_dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const fs::path& p = it->path();
      if (it.depth() == 0 && it->is_directory(ec)) {
        const std::string name = lower(p.filename().u8string());
        // .cache holds this scan's own files; Files is the loose disc file overlay, not packs.
        if (name == ".cache" || name == ".downloads" || name == "files") { it.disable_recursion_pending(); continue; }
      }
      if (it->is_regular_file(ec)) files.push_back(p);
    }
  }
  // Large packs may stay outside the game folder. Relative entries use Mods as their base.
  std::ifstream links(options.mods_dir / "links.txt");
  std::string linked;
  while (std::getline(links, linked)) {
    linked = trim(linked);
    if (linked.empty() || linked[0] == '#') continue;
    if (linked.size() >= 2 && linked.front() == '"' && linked.back() == '"')
      linked = linked.substr(1, linked.size() - 2);
    fs::path p = fs::u8path(linked);
    if (p.is_relative()) p = options.mods_dir / p;
    ec.clear();
    if (fs::is_regular_file(p, ec)) files.push_back(p);
    else result.notes.push_back("Linked mod file is missing: " + linked);
  }
  // A linked file can also be inside Mods; identify it once.
  std::set<fs::path> seen;
  files.erase(std::remove_if(files.begin(), files.end(), [&](const fs::path& p) {
    ec.clear(); const fs::path canonical = fs::weakly_canonical(p, ec);
    return ec || !seen.insert(canonical).second;
  }), files.end());
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    const std::string key = cache_key(file);
    // An entry made against another retail disc (or without one) is read again once a disc is known;
    // without one (the launcher's settings window) any entry for the unchanged file is used.
    const CacheEntry* hit = nullptr;
    for (const auto& e : cache)
      if (e.key == key && (e.base == "-" || e.base == base_id || base_id.empty())) { hit = &e; break; }
    Detected d;
    std::string base_used;
    if (hit) {
      d = hit->d;
      d.path = display_path(file);
      base_used = hit->base;
      ++result.cached;
    } else {
      std::string why;
      bool used_base = false;
      const auto started = std::chrono::steady_clock::now();
      d = identify_file(file, options, &why, &used_base);
      // "-": the answer does not depend on the retail disc; "": it would, but there was none to compare.
      base_used = used_base ? base_id : "-";
      if (!d.message.empty()) {
        ++result.hashed;
        result.log.push_back("identified " + d.path.u8string() + " as " + kind_id(d.kind) + (d.name.empty() ? "" : " " + d.name) +
                             (d.version.empty() ? "" : " " + d.version) + (why.empty() ? "" : " (" + why + ")") + " in " +
                             std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started).count()) + " ms");
      }
    }
    kept_cache.push_back({key, base_used, d});   // files that are not mods are remembered too, so they are not opened again
    if (d.message.empty()) continue;
    result.items.push_back(std::move(d));
  }
  if (options.use_cache) save_cache(cache_file, kept_cache);
  // 20XX TE and Training Mode CE first, then everything else in path order.
  std::stable_sort(result.items.begin(), result.items.end(), [](const Detected& a, const Detected& b) {
    auto rank = [](const Detected& d) { return d.kind == Kind::Te ? 0 : d.kind == Kind::TmCe ? 1 : 2; };
    return rank(a) < rank(b);
  });
  apply_choices(result, values);
  result.log.push_back("scanned " + options.mods_dir.u8string() + ": " + std::to_string(result.items.size()) + " items, " +
                       std::to_string(result.hashed) + " read, " + std::to_string(result.cached) + " from the cache, " +
                       std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - begin).count()) + " ms");
  return result;
}

// ================================================================================================
// detected.json: what the launcher reads (it does not link the host)
namespace {
std::string json_string(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
      else out += (char)c;
    }
  }
  return out + "\"";
}
}  // namespace

bool write_detected_json(const fs::path& file, const ScanResult& result, bool source_port) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::ostringstream out;
  out << "{\n  \"version\": 1,\n  \"engine\": " << json_string(source_port ? "source" : "recomp") << ",\n  \"items\": [";
  for (size_t i = 0; i < result.items.size(); ++i) {
    const Detected& d = result.items[i];
    out << (i ? "," : "") << "\n    {"
        << "\"path\": " << json_string(d.path.u8string())
        << ", \"id\": " << json_string(d.catalog_id.empty() ? d.key : d.catalog_id)
        << ", \"kind\": " << json_string(kind_id(d.kind))
        << ", \"name\": " << json_string(d.name)
        << ", \"version\": " << json_string(d.version)
        << ", \"hash\": " << json_string(d.hash)
        << ", \"key\": " << json_string(d.key)
        << ", \"enabled\": " << (d.enabled ? "true" : "false")
        << ", \"needs_engine\": " << json_string(engine_id(d.needs))
        << ", \"status\": " << json_string(support_id(d.status))
        << ", \"card\": " << (d.card ? "true" : "false")
        << ", \"restart\": " << (d.restart ? "true" : "false")
        << ", \"message\": " << json_string(d.message) << "}";
  }
  out << (result.items.empty() ? "" : "\n  ") << "],\n  \"notes\": [";
  for (size_t i = 0; i < result.notes.size(); ++i) out << (i ? ", " : "") << json_string(result.notes[i]);
  out << "]\n}\n";
  const fs::path temporary = fs::path(file) += ".tmp";
  {
    std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string text = out.str();
    f.write(text.data(), (std::streamsize)text.size());
    if (!f) return false;
  }
#ifdef _WIN32
  // UI readers briefly hold the old report open. Preserve it until a complete replacement
  // succeeds, instead of deleting it on a transient sharing violation.
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const DWORD error = GetLastError();
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) break;
    Sleep(10);
  }
  fs::remove(temporary, ec);
  return false;
#else
  fs::rename(temporary, file, ec);
  return !ec;
#endif
}

// ================================================================================================
// The memory card folder
namespace {
struct CardFile {
  fs::path path;
  std::string code, name;
  uint32_t save_time = 0;
  uint64_t created = 0;
  bool te = false;
};

uint64_t creation_time(const fs::path& file) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return UINT64_MAX;
  return ((uint64_t)data.ftCreationTime.dwHighDateTime << 32) | data.ftCreationTime.dwLowDateTime;
}

// The file name the game gives its own saves (source_host.cpp card_safe_name, hle_card.cpp safe_name).
std::string game_file_name(const CardFile& f) {
  std::string name = f.code + "-";
  for (char ch : f.name) name += (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.') ? ch : '_';
  return name + ".gci";
}

fs::path free_name(const fs::path& dir, const fs::path& file) {
  std::error_code ec;
  fs::path target = dir / file.filename();
  for (int n = 2; fs::exists(target, ec) && n < 1000; ++n)
    target = dir / fs::u8path(file.stem().u8string() + " (" + std::to_string(n) + ")" + file.extension().u8string());
  return target;
}

bool move_file(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
}
}  // namespace

std::vector<CardMove> protect_card_folder(const fs::path& card_dir, const fs::path& saves_dir,
                                          std::vector<std::string>* notes, std::vector<std::string>* log) {
  std::vector<std::string> local_notes, local_log;
  if (!notes) notes = &local_notes;
  if (!log) log = &local_log;
  std::vector<CardMove> moves;
  std::map<std::string, std::vector<CardFile>> groups;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(card_dir, ec)) {
    if (ec || !entry.is_regular_file(ec) || lower(entry.path().extension().u8string()) != ".gci") continue;
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(entry.path(), &bytes, 64ull + 2048ull * kSector) || bytes.size() < 64 ||
        !looks_like_gci(bytes.data(), bytes.size()))
      continue;
    CardFile f;
    f.path = entry.path();
    f.code = gci_code(bytes.data());
    f.name = gci_name(bytes.data());
    f.save_time = be32(bytes.data() + 0x28);
    f.created = creation_time(f.path);
    f.te = check_te(bytes).te;
    groups[f.code + "/" + f.name].push_back(std::move(f));
  }
  for (auto& kv : groups) {
    auto& files = kv.second;
    const bool melee = files[0].code == "GALE01" && files[0].name == kMeleeSaveName;
    if (files.size() == 1 && !(melee && files[0].te)) continue;
    // The player's own save: never a 20XX TE save; then the file the game itself writes; then the one
    // that was here first.
    const CardFile* own = nullptr;
    for (const auto& f : files) if (!f.te && f.path.filename().u8string() == game_file_name(f)) { own = &f; break; }
    if (!own) {
      for (const auto& f : files) {
        if (f.te) continue;
        if (!own || f.created < own->created || (f.created == own->created && f.save_time < own->save_time)) own = &f;
      }
    }
    for (const auto& f : files) {
      if (&f == own) continue;
      const fs::path target = free_name(saves_dir, f.path);
      if (!move_file(f.path, target)) {
        log->push_back("card: could not move " + f.path.u8string() + " to " + target.u8string() + " (error " +
                       std::to_string(GetLastError()) + ")");
        notes->push_back("Could not move " + f.path.filename().u8string() + " out of the memory card folder. "
                         "Close any program using it and restart.");
        continue;
      }
      moves.push_back({f.path, target, f.te});
      log->push_back("card: moved " + f.path.u8string() + " to " + target.u8string() +
                     (f.te ? " (20XX TE save)" : " (same save name as " + (own ? own->path.filename().u8string() : std::string("another file")) + ")"));
      notes->push_back(f.te ? "Moved " + f.path.filename().u8string() + " from the memory card folder to Mods\\Saves: it is "
                              "20XX TE, and your own Melee save stays in use."
                            : "Moved " + f.path.filename().u8string() + " from the memory card folder to Mods\\Saves: it had the "
                              "same save name as your own save" + (own ? " (" + own->path.filename().u8string() + ")" : std::string()) +
                              ", which stays in use.");
    }
  }
  return moves;
}

// ================================================================================================
// Sessions
std::vector<Layer> source_port_layers(const ScanResult& result) {
  std::vector<Layer> layers;
  for (const auto& d : result.items)
    if (d.enabled && d.can_enable && d.kind == Kind::TmCe) layers.push_back({LayerKind::Iso, d.path});
  for (const auto& d : result.items)
    if (d.enabled && d.can_enable && d.kind == Kind::AssetMod) layers.push_back({LayerKind::Iso, d.path});
  return layers;
}

bool te_enabled(const ScanResult& result) {
  return std::any_of(result.items.begin(), result.items.end(),
                     [](const Detected& d) { return d.kind == Kind::Te && d.enabled && d.can_enable; });
}

fs::path static_recomp_card(const ScanResult& result, const fs::path& ordinary_card, std::vector<std::string>* log) {
  std::vector<std::string> local;
  if (!log) log = &local;
  const Detected* save = nullptr;
  for (const auto& d : result.items)
    if (d.card && d.enabled && d.can_enable && runs_on(d, false)) { save = &d; break; }
  if (!save) return {};
  std::string identity, error;
  if (!gci_identity(save->path, &identity, &error)) { log->push_back("card: " + error); return {}; }
  const std::string folder = safe_name(save->kind == Kind::Te ? std::string("20XX TE") : save->name);
  const fs::path card = ordinary_card.parent_path() / "Profiles" / fs::u8path(folder);
  std::vector<std::string> notes;
  if (!prepare_profile_card(ordinary_card, card, {{save->path, identity, save->hash}}, &notes, &error)) {
    log->push_back("card: " + error);
    return {};
  }
  for (const auto& n : notes) log->push_back("card: " + n);
  // The mod's save goes in unless the folder already has its copy (the game saved into it).
  bool present = false;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(card, ec)) {
    std::string id, why;
    if (!ec && lower(entry.path().extension().u8string()) == ".gci" && gci_identity(entry.path(), &id, &why) && id == identity)
      present = true;
  }
  if (!present) {
    const size_t slash = identity.find('/');
    CardFile f;
    f.code = identity.substr(0, slash);
    f.name = identity.substr(slash + 1);
    const fs::path target = card / fs::u8path(game_file_name(f));
    fs::copy_file(save->path, target, fs::copy_options::skip_existing, ec);
    log->push_back(ec ? "card: could not copy " + save->path.u8string() + ": " + ec.message()
                      : "card: " + save->name + " copied into " + card.u8string());
  }
  log->push_back("card: Static Recomp uses " + card.u8string() + " for " + save->name);
  return card;
}

// ================================================================================================
// Startup and the settings panel
namespace {
struct Panel {
  std::mutex mutex;
  PanelView view;
  ScanOptions options;
  bool options_set = false;
  std::atomic<bool> busy{false};
};
Panel& panel() { static Panel* p = new Panel; return *p; }   // never destroyed: worker threads may outlive main

// Runs a scan with the player's choices and records every first-time decision.
ScanResult scan_with_choices(const ScanOptions& options) {
  std::map<std::string, int> values = choices();
  const auto before = values;
  ScanResult result = scan(options, &values);
  // Only what the scan itself decided is written back: a switch the player flipped meanwhile stays.
  bool wrote = false;
  {
    std::lock_guard<std::mutex> lock(store().mutex);
    for (const auto& kv : values) {
      const auto was = before.find(kv.first);
      if (was != before.end() && was->second == kv.second) continue;
      store().values[kv.first] = kv.second;
      wrote = true;
    }
  }
  if (wrote) ++store().version;
  return result;
}

void publish(const ScanResult& result, const ScanOptions& options) {
  write_detected_json(options.mods_dir / ".cache" / "detected.json", result, options.source_port);
  std::lock_guard<std::mutex> lock(panel().mutex);
  panel().view.result = result;
  panel().view.scanned = true;
}

// 20XX TE on the Source Port is a feature switch: it follows the player's choice at once.
void follow_te_choice() {
  auto& s = status();
  if (!s.source_port || !s.detected) return;
  std::lock_guard<std::mutex> lock(panel().mutex);
  s.te_owned = te_enabled(panel().view.result);
}
}  // namespace

Startup startup(const StartupOptions& options) {
  Startup s;
  std::error_code ec;
  fs::create_directories(options.mods_dir, ec);   // so "Open Mods folder" always has a folder to open
  std::vector<std::string> card_notes;
  protect_card_folder(options.card_dir, options.mods_dir / "Saves", &card_notes, &s.log);
  ScanOptions scan_options;
  scan_options.mods_dir = options.mods_dir;
  scan_options.base_iso = options.base_iso;
  scan_options.source_port = options.source_port;
  {
    std::lock_guard<std::mutex> lock(panel().mutex);
    panel().options = scan_options;
    panel().options_set = true;
  }
  ScanResult result = scan_with_choices(scan_options);
  result.notes.insert(result.notes.begin(), card_notes.begin(), card_notes.end());
  for (const auto& line : result.log) s.log.push_back(line);
  for (const auto& d : result.items)
    s.log.push_back(std::string(d.enabled ? "on:  " : "off: ") + d.path.u8string() + " | " + d.message + " | needs " +
                    engine_id(d.needs) + ", " + support_id(d.status));
  if (options.source_port) {
    s.layers = source_port_layers(result);
    s.te = te_enabled(result);
  } else if (options.use_profile_card) {
    s.card_dir = static_recomp_card(result, options.card_dir, &s.log);
  }
  publish(result, scan_options);
  return s;
}

PanelView panel_view() {
  std::lock_guard<std::mutex> lock(panel().mutex);
  PanelView view = panel().view;
  view.scanning = view.importing ? false : panel().busy.load();
  return view;
}

namespace {
ScanOptions panel_options() {
  std::lock_guard<std::mutex> lock(panel().mutex);
  if (panel().options_set) return panel().options;
  ScanOptions options;
  options.source_port = status().source_port;
  return options;
}
}  // namespace

void rescan_async() {
  if (panel().busy.exchange(true)) return;
  std::thread([] {
    const ScanOptions options = panel_options();
    const ScanResult result = scan_with_choices(options);
    publish(result, options);
    follow_te_choice();
    panel().busy = false;
  }).detach();
}

void import_async(const fs::path& picked) {
  if (panel().busy.exchange(true)) return;
  {
    std::lock_guard<std::mutex> lock(panel().mutex);
    panel().view.importing = true;
    panel().view.import_message = "Copying " + picked.filename().u8string() + " into the Mods folder...";
  }
  std::thread([picked] {
    const ScanOptions options = panel_options();
    std::string message;
    std::error_code ec;
    uint8_t head[0x40]{};
    const uint64_t size = fs::file_size(picked, ec);
    {
      std::ifstream in(picked, std::ios::binary);
      if (!ec && in) in.read((char*)head, (std::streamsize)std::min<uint64_t>(sizeof head, size));
    }
    const bool gci = !ec && looks_like_gci(head, size);
    const bool disc = !ec && size >= 0x440 && be32(head + 0x1C) == kDiscMagic;
    fs::path target;
    if (ec) {
      message = "Could not read " + picked.filename().u8string() + ".";
    } else if (!gci && !disc) {
      message = compressed_disc(head, sizeof head)
                    ? "That is a compressed disc image. Convert it to .iso in Dolphin (right-click, Convert File), then add it."
                    : "That is not a disc image (.iso) or a memory card save (.gci). Nothing was copied.";
    } else {
      const fs::path mods = fs::absolute(options.mods_dir, ec);
      const fs::path source = fs::absolute(picked, ec);
      const auto rel = source.lexically_relative(mods);
      if (!rel.empty() && rel.native().rfind(L"..", 0) != 0) {
        target = picked;   // already in the Mods folder
      } else {
        target = free_name(options.mods_dir / (gci ? "Saves" : "Discs"), picked);
        fs::create_directories(target.parent_path(), ec);
        if (!CopyFileExW(picked.c_str(), target.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS)) {
          message = "Could not copy " + picked.filename().u8string() + " into the Mods folder (error " +
                    std::to_string(GetLastError()) + ").";
          target.clear();
        }
      }
    }
    if (!target.empty()) {
      const Detected d = identify(target, options);
      const std::string where = display_path(target);
      const bool usable = d.can_enable && (d.status == Support::Supported || d.status == Support::Untested);
      message = (target == picked ? "Already in the Mods folder: " : "Copied into " + where + ". ") +
                (usable ? std::string("Recognized: ") + d.name + (d.version.empty() ? "" : " " + d.version) + "."
                        : "Not recognized as a mod this game can use: " + (d.message.empty() ? std::string("unknown file.") : d.message));
      const ScanResult result = scan_with_choices(options);
      publish(result, options);
      follow_te_choice();
    }
    {
      std::lock_guard<std::mutex> lock(panel().mutex);
      panel().view.importing = false;
      panel().view.import_message = message;
    }
    panel().busy = false;
  }).detach();
}

void choose(const std::string& key, bool on, std::vector<std::string>* notes) {
  std::vector<std::string> local;
  if (!notes) notes = &local;
  {
    std::lock_guard<std::mutex> panel_lock(panel().mutex);
    std::lock_guard<std::mutex> lock(store().mutex);
    auto& items = panel().view.result.items;
    const auto chosen = std::find_if(items.begin(), items.end(), [&](const Detected& d) { return d.key == key && d.can_enable; });
    store().values[key] = on ? 1 : 0;
    if (chosen != items.end()) {
      chosen->enabled = on;
      if (on && chosen->card) {
        for (auto& d : items) {
          if (&d == &*chosen || !d.card || !d.enabled) continue;
          d.enabled = false;
          store().values[d.key] = 0;
          notes->push_back("Only one save mod can be on at a time: turned off " + d.name + ".");
        }
      }
    }
    panel().view.choice_notes = *notes;
    ++store().version;
  }
  follow_te_choice();
  const PanelView view = panel_view();
  if (view.scanned) write_detected_json(panel_options().mods_dir / ".cache" / "detected.json", view.result, panel_options().source_port);
}

fs::path mods_folder() {
  std::error_code ec;
  return fs::absolute(panel_options().mods_dir, ec);
}

bool open_mods_folder() {
  const fs::path folder = mods_folder();
  std::error_code ec;
  fs::create_directories(folder, ec);
  return (INT_PTR)ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

bool open_official_page(Kind kind) {
  const wchar_t* page = nullptr;
  if (kind == Kind::Te) page = L"https://github.com/dansalvato/20XXTE/releases";
  if (kind == Kind::TmCe) page = L"https://github.com/AlexanderHarrison/TrainingMode-CommunityEdition/releases";
  return page && (INT_PTR)ShellExecuteW(nullptr, L"open", page, nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

}  // namespace source_port::mods
