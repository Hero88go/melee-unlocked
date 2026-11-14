// gecko_function_patch.h: which rewritten functions mean "return N at once" for the Source Port.
// The instruction sequences are made up, in the shapes the real codes meet.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gecko_function_patch.h"
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

constexpr uint32_t kLi0 = 0x38600000, kLi1 = 0x38600001, kBlr = 0x4E800020;
constexpr uint32_t kBnelr = 0x4C820020, kBeqlr = 0x4D820020;
constexpr uint32_t kLoadObj = 0x80A3002C;     // lwz r5,0x2C(r3): r3 is only the base
constexpr uint32_t kLoadFlag0 = 0x88050010;   // lbz r0,0x10(r5)
constexpr uint32_t kLoadFlag1 = 0x88050011;
constexpr uint32_t kTestBit = 0x5400D7FF;     // rlwinm. r0,r0,26,31,31
constexpr uint32_t kCall = 0x48000101;        // bl +0x100
constexpr uint32_t kStoreR4 = 0x90040000;     // stw r0,0(r4)
constexpr uint32_t kMrR4R3 = 0x7C641B78;      // mr r4,r3 (reads r3)
constexpr uint32_t kCmpR4 = 0x2C040000;       // cmpwi r4,0
}  // namespace

int main() {
  using namespace gecko_fn;
  const uint32_t at = 0x80100000;
  // A getter in the No Character Shadows shape: false on two early returns, true at the end. The
  // code turns the last li r3,1 into li r3,0: every return then gives 0.
  std::vector<uint32_t> getter = {kLoadObj, kLi0, kLoadFlag0, kTestBit, kBnelr, kLoadFlag1, kTestBit, kBeqlr, kLi1, kBlr};
  check(!analyse(at, getter, kBool).ok, "the unpatched getter returns 0 or 1");
  getter[8] = kLi0;
  Result r = analyse(at, getter, kBool);
  check(r.ok && r.value == 0, "the patched getter always returns 0");
  check(analyse(at, getter, 0x41).ok, "the same as a 4-byte integer");
  check(analyse(at, getter, kPointer).ok, "and as a pointer: null");

  // A branch around the final li: both paths must give the same number.
  std::vector<uint32_t> around = {kLi0, kCmpR4, 0x40820008 /* bne +8 */, 0x38600007, kBlr};
  check(!analyse(at, around, 0x41).ok, "0 on one path, 7 on the other");
  around[3] = kLi0;
  check(analyse(at, around, 0x41).ok, "0 on both paths");

  // blr at the start: a void function returns at once whatever follows; one that returns a value
  // would hand back its first argument, which is not a fixed answer.
  std::vector<uint32_t> body = {kBlr, kCall, kStoreR4, kBlr};
  check(analyse(at, body, kVoid).ok, "void: blr first");
  check(!analyse(at, body, 0x41).ok, "a value: blr first gives back r3 unchanged");

  // li r3,N + blr at the start: whatever the rest does never runs.
  std::vector<uint32_t> early = {0x38600005, kBlr, kCall, kStoreR4, kLi0, kBlr};
  r = analyse(at, early, 0x41);
  check(r.ok && r.value == 5, "li r3,5 + blr first returns 5");
  r = analyse(at, early, kBool);
  check(r.ok && r.value == 1, "a bool is 0 or 1");
  check(!analyse(at, early, kPointer).ok, "a pointer is never a fixed address");

  // Effects on the way refuse the code.
  std::vector<uint32_t> calls = {kCall, kLi0, kBlr};
  check(!analyse(at, calls, 0x41).ok, "a call on the way");
  std::vector<uint32_t> stores = {kStoreR4, kLi0, kBlr};
  check(!analyse(at, stores, 0x41).ok, "a store on the way");
  std::vector<uint32_t> frame = {0x9421FFE0 /* stwu r1,-32(r1) */, 0x93E1001C /* stw r31,28(r1) */, kLi1,
                                 0x83E1001C /* lwz r31,28(r1) */, 0x38210020 /* addi r1,r1,32 */, kBlr};
  r = analyse(at, frame, 0x41);
  check(r.ok && r.value == 1, "its own stack frame is fine");
  std::vector<uint32_t> reads = {kMrR4R3, kLi0, kBlr};
  check(analyse(at, reads, 0x41).ok, "reading r3 is not writing it");
  std::vector<uint32_t> table = {0x4E800420 /* bctr */, kLi0, kBlr};
  check(!analyse(at, table, 0x41).ok, "a jump through a table");
  std::vector<uint32_t> out = {0x48001000 /* b +0x1000 */, kBlr};
  check(!analyse(at, out, kVoid).ok, "a tail call");
  std::vector<uint32_t> off_end = {kLi0};
  check(!analyse(at, off_end, 0x41).ok, "running past the end");

  std::printf("gecko function patch: %s\n", failures ? "FAIL" : "all checks passed");
  return failures != 0;
}
