// A Gecko code that rewrites a game function's PowerPC, judged for the Source Port.
//
// The Source Port runs the game compiled from C, so a code's instructions never run there. Some codes
// only make a function give back a fixed answer: a blr at its start ("do nothing"), li r3,N + blr at
// its start ("return N"), or li r3,N over the last value a getter computes (waxo's No Character
// Shadows, 040872F8 38600000). Each of those means exactly "return N at once" for the native
// function, which the host can do (user_gecko.cpp overwrites its entry), as long as nothing the
// function does on the way has an effect.
//
// analyse() proves that from the console's own instructions with the code's words in place: it
// follows every path from the function's entry, and accepts only when every instruction it reaches
// stays inside the function (no call, no tail call, no jump through a table), writes no memory but
// its own stack frame, changes no machine state, and every return it reaches gives back the same
// constant in r3 (any return at all for a void function). Anything else is refused with the reason.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gecko_fn {

// The native function's return kind (tools/gecko_functions.py): 0x00 void, 0x02 pointer, 0x11 /
// 0x21 / 0x41 an integer of 1, 2 or 4 bytes, 0x19 bool.
enum : uint8_t { kVoid = 0x00, kPointer = 0x02, kBool = 0x19 };

struct Result {
  bool ok = false;
  uint32_t value = 0;   // what the native function returns (meaningless for void)
  std::string why;      // when not ok
};

inline Result analyse(uint32_t addr, const std::vector<uint32_t>& words, uint8_t kind) {
  Result r;
  const size_t n = words.size();
  // r3 at the start of each instruction: 0 not reached, 1 a known constant (r3v), 2 unknown.
  std::vector<uint8_t> state(n, 0);
  std::vector<uint32_t> r3v(n, 0);
  std::vector<size_t> work;
  auto flow = [&](size_t to, uint8_t s, uint32_t v) -> bool {
    if (to >= n) return false;
    if (state[to] == 0) { state[to] = s; r3v[to] = v; work.push_back(to); }
    else if (state[to] == 1 && (s == 2 || r3v[to] != v)) { state[to] = 2; work.push_back(to); }
    return true;
  };
  auto fail = [&](const char* why) { r.ok = false; r.why = why; return r; };
  auto target = [&](size_t i, uint32_t w, uint32_t field, uint32_t sign_bit, uint32_t span, int64_t& to) {
    int64_t d = (int64_t)(w & field);
    if (d & sign_bit) d -= span;
    const int64_t at = (w & 2) ? d : (int64_t)addr + 4 * (int64_t)i + d;
    to = (at - (int64_t)addr) / 4;
    return at >= (int64_t)addr && at < (int64_t)addr + 4 * (int64_t)n && ((at - (int64_t)addr) & 3) == 0;
  };
  bool returned = false, known_all = true, have_value = false;
  uint32_t value = 0;
  flow(0, 2, 0);
  size_t steps = 0;
  while (!work.empty()) {
    if (++steps > 200000) return fail("is too complex to follow");
    const size_t i = work.back();
    work.pop_back();
    const uint32_t w = words[i], op = w >> 26, rd = (w >> 21) & 31, ra = (w >> 16) & 31;
    const uint8_t s = state[i];
    const uint32_t v = r3v[i];
    auto next = [&](uint8_t ns, uint32_t nv) { return flow(i + 1, ns, nv); };
    switch (op) {
      case 18: {   // b, bl
        if (w & 1) return fail("calls another function");
        int64_t to = 0;
        if (!target(i, w, 0x03FFFFFC, 0x02000000, 0x04000000, to)) return fail("jumps out of the function");
        flow((size_t)to, s, v);
        continue;
      }
      case 16: {   // bc
        if (w & 1) return fail("calls another function");
        int64_t to = 0;
        if (!target(i, w, 0xFFFC, 0x8000, 0x10000, to)) return fail("jumps out of the function");
        flow((size_t)to, s, v);
        if ((rd & 0x14) != 0x14 && !next(s, v)) return fail("runs past its end");
        continue;
      }
      case 19: {
        const uint32_t xo = (w >> 1) & 1023;
        if (xo == 16) {   // bclr: a return
          if (w & 1) return fail("calls through the link register");
          returned = true;
          if (s != 1) known_all = false;
          else if (!have_value) { have_value = true; value = v; }
          else if (value != v) known_all = false;
          if ((rd & 0x14) != 0x14 && !next(s, v)) return fail("runs past its end");
          continue;
        }
        if (xo == 528) return fail("jumps through a table or a function pointer");
        if (xo == 0 || xo == 33 || xo == 129 || xo == 150 || xo == 193 || xo == 225 || xo == 257 ||
            xo == 289 || xo == 417 || xo == 449) {   // mcrf, condition register logic, isync
          if (!next(s, v)) return fail("runs past its end");
          continue;
        }
        return fail("changes machine state");
      }
      case 36: case 37: case 38: case 39: case 44: case 45: case 47:
      case 52: case 53: case 54: case 55: case 60: case 61:   // stores
        if (ra != 1) return fail("writes memory");
        if (!next(s, v)) return fail("runs past its end");
        continue;
      case 2: case 3: case 17: return fail("traps or calls the system");
      case 59: case 63:
        if (op == 63) {
          const uint32_t xo = (w >> 1) & 1023;
          if (xo == 38 || xo == 70 || xo == 134 || xo == 711) return fail("changes the floating-point state");
        }
        if (!next(s, v)) return fail("runs past its end");
        continue;
      case 4: {
        const uint32_t x6 = (w >> 1) & 63, x10 = (w >> 1) & 1023;
        if (x6 == 7 || x6 == 39 || x10 == 1014) return fail("writes memory");
        if (!next(s, v)) return fail("runs past its end");
        continue;
      }
      case 10: case 11:   // compares: the condition register only
        if (!next(s, v)) return fail("runs past its end");
        continue;
      case 31: {
        const uint32_t xo = (w >> 1) & 1023;
        static const uint16_t stores[] = {54, 86, 150, 151, 183, 215, 246, 247, 407, 439, 470, 661, 662, 663,
                                          695, 725, 727, 759, 918, 983, 1014};
        static const uint16_t effects[] = {4, 146, 210, 242, 306, 370, 566, 595, 598, 659, 854, 978, 982};
        for (uint16_t x : stores) if (xo == x) return fail("writes memory");
        for (uint16_t x : effects) if (xo == x) return fail("changes machine state");
        if (xo == 467 && ((w >> 11) & 0x3FF) != 0x100 && ((w >> 11) & 0x3FF) != 0x120) return fail("changes machine state");
        if (xo == 0 || xo == 32) { if (!next(s, v)) return fail("runs past its end"); continue; }   // cmp, cmpl
        // The logic and shift group writes rA (from rS in the rD field); loads with update write
        // both; the rest write rD.
        static const uint16_t to_ra[] = {24, 26, 28, 60, 124, 284, 316, 412, 444, 476, 536, 792, 824, 922, 954};
        static const uint16_t update[] = {55, 119, 311, 375, 567, 631};
        bool writes_r3 = rd == 3;
        for (uint16_t x : to_ra) if (xo == x) writes_r3 = ra == 3;
        for (uint16_t x : update) if (xo == x) writes_r3 = (xo < 500 && rd == 3) || ra == 3;
        if (!next(writes_r3 ? 2 : s, writes_r3 ? 0 : v)) return fail("runs past its end");
        continue;
      }
      case 14: case 15: {   // addi, addis (li, lis with rA 0)
        uint8_t ns = s; uint32_t nv = v;
        if (rd == 3) {
          const uint32_t imm = (uint32_t)(int32_t)(int16_t)(w & 0xFFFF);
          if (ra == 0) { ns = 1; nv = op == 14 ? imm : (w & 0xFFFF) << 16; }
          else if (ra == 3 && s == 1) { nv = v + (op == 14 ? imm : (w & 0xFFFF) << 16); }
          else ns = 2;
        }
        if (!next(ns, nv)) return fail("runs past its end");
        continue;
      }
      case 24: {   // ori: rA = rS | imm (the second half of lis + ori)
        uint8_t ns = s; uint32_t nv = v;
        if (ra == 3) {
          if (rd == 3 && s == 1) nv = v | (w & 0xFFFF);
          else ns = 2;
        }
        if (!next(ns, nv)) return fail("runs past its end");
        continue;
      }
      case 7: case 8: case 12: case 13:                       // mulli, subfic, addic(.)
      case 20: case 21: case 23: case 25: case 26: case 27: case 28: case 29:   // rotates, logic
      case 32: case 33: case 34: case 35: case 40: case 41: case 42: case 43:   // integer loads
      case 46:                                                // lmw
      case 48: case 49: case 50: case 51: case 56: case 57: { // float loads (rA changes with update)
        bool writes_r3 = false;
        switch (op) {
          case 7: case 8: case 12: case 13: case 32: case 34: case 40: case 42: writes_r3 = rd == 3; break;
          case 33: case 35: case 41: case 43: writes_r3 = rd == 3 || ra == 3; break;   // with update
          case 46: writes_r3 = rd <= 3; break;                                       // lmw rN..r31
          case 49: case 51: case 57: writes_r3 = ra == 3; break;                     // float, with update
          case 48: case 50: case 56: writes_r3 = false; break;                       // a float register only
          default: writes_r3 = ra == 3; break;                                       // rotates and logic: rA
        }
        if (!next(writes_r3 ? 2 : s, writes_r3 ? 0 : v)) return fail("runs past its end");
        continue;
      }
      default:
        return fail("uses an instruction the Source Port does not follow");
    }
  }
  if (!returned) return fail("never returns");
  if (kind == kVoid) { r.ok = true; return r; }
  if (!known_all || !have_value) return fail("does not always return the same number");
  if (kind == kPointer && value != 0) return fail("returns a fixed address");
  r.ok = true;
  r.value = kind == kBool ? (value & 0xFFu ? 1u : 0u) : value;
  return r;
}

}  // namespace gecko_fn
