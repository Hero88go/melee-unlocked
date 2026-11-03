// Which compiled guest functions count as host implementations when a mod changes them. Kept apart
// from ppc_runtime.cpp (which needs the dispatch table and Win32) so the rule is checked on any
// machine (port/tests/host_stub_policy_test.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ppc {

// The functions the recompiler makes for Gecko code (port/recomp/analyze.py): a hook outside every
// game function (gecko_hook_<hook>, Slippi's helper slots and the m-ex slots at 803D7058..),
// a C0 cave (gecko_cave_) and an entry inside a cave (gecko_fn_).
inline bool gecko_synthetic_name(const char* name) {
  return name && std::strncmp(name, "gecko_", 6) == 0;
}

// `extent`: guest bytes from the function's entry to the next name in the name table. `host_bytes`:
// its machine code. A host implementation (PADRead, the SI and DVD calls) is a short stub that calls
// the host, so far less machine code than a translation of that many instructions could be.
//
// Gecko code is never one: the recompiler stands in for game functions only, and every Gecko function
// is a translation of the code table's own words. Its extent says nothing about its size either: the
// name table has no sizes, and a hook slot is one word (`b cave`) followed by data or by nothing the
// recompiler named. The last m-ex slot, gecko_hook_803D709C, "ran" to the next named function in the
// code table in the heap, 2.6 MB away, so its translated cave looked like a stub; Beyond Melee's own
// 803D709C was ignored and the game took Slippi's, which does not return the symbol in r4 as
// Beyond Melee's callers expect. They read a function pointer through r4 = FFFFFFFF and jumped to
// C7B6B105 (melee-native/bm-static-report.md).
inline bool looks_like_host_stub(const char* name, uint32_t extent, size_t host_bytes) {
  if (extent < 0x20u || gecko_synthetic_name(name)) return false;
  return host_bytes < extent / 2;   // under two bytes per guest instruction
}

}  // namespace ppc
