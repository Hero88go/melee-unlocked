// The rules of the "20XX CPUs" loader (host/hackpack_ai.h), without the game: the block is accepted
// only by its known size and hash, the branches it needs are encoded as the PowerPC does, the pack's
// settings table is the 13 known words, the block's last word is patched in place, and the entry
// site is only ever written over the game's own word or this loader's branch.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Optional: a path to a copy of the pack's ai_engine.bin as the first argument verifies the real
// block (it is never in the repository).
#include "hackpack_ai.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
}

int main(int argc, char** argv) {
  using namespace host::hackpack_ai::rules;

  // SHA-256 itself, against the published vectors, then across the one- and two-block tail cases.
  CHECK(sha256_hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_hex((const uint8_t*)"abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  {
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   // 56 bytes: the length spills into a second block
    CHECK(sha256_hex((const uint8_t*)two, 56) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string million(1000000, 'a');
    CHECK(sha256_hex((const uint8_t*)million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }

  // Blob verification: the size alone is not enough, and a changed byte is refused.
  {
    std::vector<uint8_t> wrong_size(kBlobSize - 4, 0);
    std::string why;
    CHECK(!verify_blob(wrong_size, &why) && why.find("expected 20804") != std::string::npos);
    std::vector<uint8_t> right_size(kBlobSize, 0);
    CHECK(!verify_blob(right_size, &why) && why.find("hash") != std::string::npos);
    CHECK(std::string(kBlobSha256).size() == 64);
    if (argc > 1) {
      std::ifstream in(argv[1], std::ios::binary);
      std::vector<uint8_t> real((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      const bool verified = verify_blob(real, &why);
      CHECK(verified);
      std::printf("real block (%zu bytes): %s\n", real.size(), verified ? "verified" : why.c_str());
      if (real.size() == kBlobSize) {
        CHECK(be32(real.data() + kBlobSize - 4) == 0x48000000u);   // the pack ships the last word as a bare `b`
        real[100] ^= 1;
        CHECK(!verify_blob(real, &why));                           // one changed byte: refused
      }
    }
  }

  // Branch encoding: the `b` form, the plan's own formula, a forward and a backward (wrapping) case,
  // the range limits and alignment.
  {
    const uint32_t base = 0x80D00000u;   // a copy of the block in the heap
    const uint32_t from = base + kBlobSize - 4;
    const uint32_t word = encode_branch(from, kReturnSite);
    CHECK(word != 0 && is_branch(word));
    CHECK(word == (0x48000000u | ((kReturnSite - (base + 20800)) & 0x03FFFFFCu)));
    CHECK((word & 0x02000000u) != 0);                      // backwards: the displacement is negative
    CHECK(branch_target(from, word) == kReturnSite);
    const uint32_t entry = encode_branch(kEntrySite, base);
    CHECK(entry != 0 && is_branch(entry) && !(entry & 0x02000000u) && branch_target(kEntrySite, entry) == base);
    CHECK(entry != kEntryWord && !is_branch(kEntryWord));
    // A block near the top of the console's RAM still reaches the return site.
    const uint32_t high = 0x817F0000u;
    CHECK(branch_in_range(high + kBlobSize - 4, kReturnSite) && branch_target(high + kBlobSize - 4, encode_branch(high + kBlobSize - 4, kReturnSite)) == kReturnSite);
    // Limits: +0x1FFFFFC and -0x2000000 are the last reachable displacements.
    CHECK(branch_in_range(0x80000000u, 0x81FFFFFCu) && !branch_in_range(0x80000000u, 0x82000000u));
    CHECK(branch_in_range(0x82000000u, 0x80000000u) && !branch_in_range(0x82000004u, 0x80000000u));
    CHECK(!branch_in_range(0x80000000u, 0x80000002u) && encode_branch(0x80000000u, 0x80000002u) == 0);
    CHECK(branch_target(0x80000000u, 0x48000000u) == 0x80000000u);   // `b .`
  }

  // The 13 settings the block reads, with the pack's defaults.
  {
    const Setting expected[] = {
      {0x80003374u, 0},   {0x803FAED0u, 4},   {0x803FBAB8u, 6},   {0x803FA320u, 0},   {0x803FA324u, 0},
      {0x803FA330u, 100}, {0x803FA334u, 100}, {0x803FAEBCu, 15},  {0x803FAEC0u, 10},  {0x803FAEC4u, 10},
      {0x803FAF5Cu, 10},  {0x803FAF74u, 0},   {0x803FAF78u, 0},
    };
    CHECK(kDefaultCount == 13);
    std::set<uint32_t> addresses;
    for (size_t i = 0; i < kDefaultCount && i < 13; ++i) {
      CHECK(kDefaults[i].address == expected[i].address && kDefaults[i].value == expected[i].value);
      CHECK((kDefaults[i].address & 3) == 0 && kDefaults[i].address >= 0x80000000u && kDefaults[i].address < 0x81800000u);
      addresses.insert(kDefaults[i].address);
    }
    CHECK(addresses.size() == 13);
  }

  // The last-word patch on a buffer: only the last four bytes change, and they decode to the return site.
  {
    std::vector<uint8_t> blob(kBlobSize, 0xA5);
    std::vector<uint8_t> before = blob;
    const uint32_t base = 0x80C12340u;
    CHECK(patch_return_branch(blob, base));
    CHECK(std::equal(blob.begin(), blob.end() - 4, before.begin()));
    const uint32_t last = be32(blob.data() + kBlobSize - 4);
    CHECK(is_branch(last) && branch_target(base + kBlobSize - 4, last) == kReturnSite);
    std::vector<uint8_t> short_blob(kBlobSize - 8, 0);
    CHECK(!patch_return_branch(short_blob, base));
    CHECK(!patch_return_branch(blob, 0x90000000u));   // out of range: refused, nothing to write
  }

  // The entry site: the game's own word or our own branch may be replaced, a foreign word never.
  {
    const uint32_t ours = encode_branch(kEntrySite, 0x80D00000u);
    const uint32_t other = encode_branch(kEntrySite, 0x80E00000u);
    CHECK(entry_site_free(kEntryWord, 0));
    CHECK(entry_site_free(kEntryWord, ours));
    CHECK(entry_site_free(ours, ours));
    CHECK(!entry_site_free(ours, 0));            // a branch there before this loader wrote one
    CHECK(!entry_site_free(other, ours));        // somebody else's branch
    CHECK(!entry_site_free(0x60000000u, 0));     // a nop from another patch
    CHECK(!entry_site_free(0x907F065Du, 0));     // nearly the game's word
    CHECK(kProcInput < kEntrySite && kEntrySite + 4 == kReturnSite && kEntrySite < kProcInput + 0xB1Cu);
  }

  if (g_failures == 0) std::printf("hackpack ai: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
