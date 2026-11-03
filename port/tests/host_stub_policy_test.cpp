// Window-free checks of which compiled functions keep the host's version when a mod changes them
// (ppc::compiled_as_host_function). Beyond Melee on the Static engine stopped at boot because the
// last m-ex slot, gecko_hook_803D709C, was taken for a host stub (melee-native/bm-static-report.md).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_stub_policy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  using namespace ppc;
  // The names the recompiler gives Gecko code (port/recomp/analyze.py).
  CHECK(gecko_synthetic_name("gecko_hook_803D709C"));
  CHECK(gecko_synthetic_name("gecko_hook_800056BC"));
  CHECK(gecko_synthetic_name("gecko_cave_8065CC88"));
  CHECK(gecko_synthetic_name("gecko_fn_80667B24"));
  CHECK(!gecko_synthetic_name("PADRead"));
  CHECK(!gecko_synthetic_name("HSD_PadRenewRawStatus"));
  CHECK(!gecko_synthetic_name("geckoish"));
  CHECK(!gecko_synthetic_name(""));
  CHECK(!gecko_synthetic_name(nullptr));

  // A host implementation of a game function: a few dozen bytes of stub for hundreds of guest bytes.
  CHECK(looks_like_host_stub("PADRead", 0x400u, 48));
  CHECK(looks_like_host_stub("DVDReadAsyncPrio", 0x100u, 0x7Fu));
  // A translation: about four or more bytes of machine code per guest byte.
  CHECK(!looks_like_host_stub("HSD_PadRenewMasterStatus", 0x400u, 0x1400u));
  CHECK(!looks_like_host_stub("PADRead", 0x100u, 0x80u));   // exactly two bytes per instruction is not "under"
  // Too short to tell (eight instructions or fewer): always the mod's code.
  CHECK(!looks_like_host_stub("OSGetTime", 0x1Cu, 1));

  // The Beyond Melee case: the last of Slippi's m-ex slots runs, by the name table, to the first named
  // function in the code table loaded at 8065CC80. Its translated cave is a few hundred bytes.
  const uint32_t slot = 0x803D709Cu, next_named = 0x8065CCA8u;
  CHECK(!looks_like_host_stub("gecko_hook_803D709C", next_named - slot, 0x300));
  // A slot inside the block is followed by the next slot, so it was already too short to look like one.
  CHECK(!looks_like_host_stub("gecko_hook_803D7074", 0x803D7090u - 0x803D7074u, 0x600));
  // Slippi's helper slots below .text: the last one runs to the first function after them.
  CHECK(!looks_like_host_stub("gecko_hook_800056BC", 0x80005940u - 0x800056BCu, 0x40));
  // A C0 cave or a cave entry with a gap behind it is a translation too.
  CHECK(!looks_like_host_stub("gecko_cave_8065CC88", 0x10000u, 0x200));
  CHECK(!looks_like_host_stub("gecko_fn_80667B24", 0x10000u, 0x200));

  std::puts("host stub policy ok");
  return 0;
}
