// SPDX-License-Identifier: GPL-2.0-or-later
#include "user_gecko.h"
#include "host.h"
// port/tests/user_gecko_test.cpp includes this file with a small made-up table of its own in place
// of the generated one, so the rules below are tested on an image the test controls.
#ifndef USER_GECKO_TEST_TARGETS
#include "user_gecko_targets.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace user_gecko {
namespace {

std::string g_path;
std::vector<Code> g_codes;
bool g_code_patches_allowed = false;

// The DOL's code sections (main.dol: .init and .text). A write there changes instructions the
// translated code will never read.
bool in_code(uint32_t a) { return (a >= 0x80003100u && a < 0x80005520u) || (a >= 0x80005940u && a < 0x803B7240u); }

std::string hex8(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "%08X", v); return b; }

// ---- the Source Port: writes go to the native game's own variables ----
// The game library's entry for one write (shim/mu_gecko.c); null until the host has loaded it.
NativeWrite g_native_write = nullptr;
NativeRead g_native_read = nullptr;
// The 16 Gecko registers: shared by every code and kept from frame to frame, as the handler's are.
uint32_t g_registers[16] = {};

// What a write can do to a console symbol on the Source Port: ConsoleSymbol::kind, the numbers
// tools/gecko_targets.py writes into user_gecko_targets.h.
enum : uint8_t { kOk, kCode, kPointers, kPrivate, kConstant, kLiteral, kLayout, kAbsent };

// The console symbol holding `addr`, or null (the heap, the stack, a gap between objects).
const ConsoleSymbol* symbol_at(uint32_t addr) {
  const ConsoleSymbol* end = std::end(kConsoleSymbols);
  const ConsoleSymbol* it = std::upper_bound(std::begin(kConsoleSymbols), end, addr,
                                             [](uint32_t a, const ConsoleSymbol& s) { return a < s.addr; });
  if (it == std::begin(kConsoleSymbols)) return nullptr;
  --it;
  return addr - it->addr < it->size ? it : nullptr;
}

// Why a write of `len` bytes at console `addr` cannot run on the Source Port, or empty when the
// whole range is one variable the native game keeps with the console's layout. The wording is the
// same as check_write() in tools/gecko_targets.py, which the unit test runs.
std::string source_port_reason(uint32_t addr, uint32_t len) {
  const ConsoleSymbol* s = symbol_at(addr);
  if (!s) return "writes memory at " + hex8(addr) + ", which is not a game variable the Source Port can find";
  const std::string name = s->name;
  switch (s->kind) {
#ifdef MELEE_NO_SLIPPI   // no other engine to name in the build with one
    case kCode: return "writes game code at " + name + ", which is compiled C here";
#else
    case kCode: return "writes game code at " + name + ": needs the Static Recomp engine";
#endif
    case kPointers: return "writes " + name + ", which holds pointers";
    case kPrivate: return "writes " + name + ", which the Source Port keeps private to one source file";
    case kConstant: return "writes " + name + ", a constant the Source Port compiles into its code";
    case kLiteral: return "writes a constant at " + hex8(addr) + " that the Source Port compiles into its code";
    case kLayout: return "writes " + name + ", whose layout is not proven to match the console's";
    case kAbsent: return "writes " + name + ", which has no counterpart the Source Port can name";
    default: break;
  }
  // Two neighbours on the console are not neighbours here: a write may not run off the end.
  if (len > s->size - (addr - s->addr)) return "writes past the end of " + name;
  return "";
}

// Reads one code for the Source Port: every line becomes a step with its address worked out
// (Code::plan), or the code is refused with the reason. The same rules, in the form the unit test
// runs, are the Compiler of tools/gecko_coverage.py. Every rule is the console handler's own
// (port/slippi_sys/codehandler.bin, loaded at 80001800; the addresses below are its instructions).
//
// WHAT IS NOT THE HANDLER'S: ONE CODE AT A TIME
// The handler knows no codes, only one list of lines. Once per frame it starts at the top with the
// base address and the pointer at 80000000 and every condition clear (80001F64 to 80001F6C), and
// nothing but a terminator line (E0, E2) changes that before the list ends: a base, a pointer or an
// open if left behind by one code is still there for the next, and an F0 line ends the whole list,
// the codes after it included (80002618). Here each code is read and run on its own: it starts with
// both at 80000000 and no condition, and an F0 ends only that code. A code that ends with its
// terminator, as codes are written, does the same either way. One that leans on what the code
// before it left behind (a missing terminator) runs here as if it came first, and a code whose if
// is never closed does not switch off the codes listed after it. The 16 Gecko registers are the
// one thing shared by every code and kept from frame to frame, in both.
struct Compiler {
  Code& c;
  // The base address and the pointer, whether each is known here, and the if-depth each was last
  // set at (0: outside every if). A value set inside an if holds only until that if ends: after it
  // the register is one of two values, so it is not known.
  uint32_t base[2] = {0x80000000u, 0x80000000u};
  bool known[2] = {true, true};
  uint32_t set_at[2] = {0, 0};
  uint32_t depth = 0;
  std::string fail;

  explicit Compiler(Code& code) : c(code) {}

  bool refuse(const std::string& why) { fail = why; return false; }
  static const char* called(int which) { return which ? "pointer" : "base address"; }
  static int which_base(uint32_t w) { return ((w >> 24) & 0x10) ? 1 : 0; }

  // What a line counts from, the handler's r12 (80001FC0 to 80001FD4): the pointer in full for the
  // types with bit 0x10, else only the top seven bits of the base address (rlwinm r12,r6,0,0,6).
  // A base address anywhere in 80000000 to 81FFFFFF therefore counts as 80000000: the line's own
  // 25 bits of address do the rest. Only "add to the base address" (42 with the add bit) and the
  // value a store writes (44, 4C) see the base address in full.
  bool base_of(uint32_t w, uint32_t& out) {
    const int which = which_base(w);
    if (!known[which]) return refuse(std::string("uses the ") + called(which) + " after a condition that changed it has ended");
    out = which ? base[1] : base[0] & 0xFE000000u;
    return true;
  }
  bool address(uint32_t w, uint32_t& out) {
    uint32_t from = 0;
    if (!base_of(w, from)) return false;
    out = from + (w & 0x01FFFFFFu);
    return true;
  }
  bool check(uint32_t addr, uint32_t len, bool reading) {
    if (addr < 0x80003100u || addr > 0x81800000u || len > 0x81800000u - addr)
      return refuse(std::string(reading ? "reads" : "writes") + " outside game memory (" + hex8(addr) + ")");
    if (len == 0) return true;
    std::string why = source_port_reason(addr, len);
    if (why.empty()) return true;
    if (reading && why.compare(0, 6, "writes") == 0) why = "reads" + why.substr(6);
    return refuse(why);
  }
  bool write(uint32_t addr, const uint8_t* data, uint32_t len) {
    if (!check(addr, len, false)) return false;
    if (len == 0) return true;
    Op op;
    op.kind = Op::Write; op.addr = addr; op.value = (uint32_t)c.plan_bytes.size(); op.count = len;
    c.plan_bytes.insert(c.plan_bytes.end(), data, data + len);
    c.plan.push_back(op);
    return true;
  }
  bool write32(uint32_t addr, uint32_t v) {
    const uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return write(addr, b, 4);
  }
  bool read(uint32_t addr, uint32_t len) {
    if (!check(addr, len, true)) return false;
    c.reads = true;
    return true;
  }

  // Ifs end until `open` stay open (and, with `otherwise`, the innermost one turns into its else).
  // A base set inside a block that has ended is no longer known.
  void close_to(uint32_t open, bool otherwise) {
    for (int which = 0; which < 2; ++which)
      if (set_at[which] > open || (otherwise && set_at[which] >= std::max<uint32_t>(open, 1))) {
        known[which] = false;
        set_at[which] = 0;
      }
    depth = open;
  }
  // The second word of a terminator: a nonzero half sets the base address (high) or the pointer
  // (low) to that half times 0x10000, whatever the conditions were.
  void reset_bases(uint32_t v) {
    if (v >> 16) { base[0] = (v >> 16) << 16; known[0] = true; set_at[0] = 0; }
    if (v & 0xFFFFu) { base[1] = (v & 0xFFFFu) << 16; known[1] = true; set_at[1] = 0; }
  }
  void endif(uint32_t count, bool otherwise) {
    Op op;
    op.kind = Op::Endif; op.count = count; op.aux = (uint8_t)(otherwise ? 1 : 0);
    c.plan.push_back(op);
    close_to(depth > count ? depth - count : 0, otherwise);
  }

  // 00 to 08: the writes. `i` is left on the line's last word pair.
  bool write_line(size_t& i, uint32_t sub) {
    const uint32_t w = c.lines[i].first, v = c.lines[i].second;
    uint32_t addr = 0;
    if (!address(w, addr)) return false;
    std::vector<uint8_t> bytes;
    switch (sub) {
      case 0: bytes.assign((v >> 16) + 1, (uint8_t)v); break;
      case 1: for (uint32_t k = 0; k <= (v >> 16); ++k) { bytes.push_back((uint8_t)(v >> 8)); bytes.push_back((uint8_t)v); } break;
      case 2: return write32(addr & ~3u, v);   // the handler rounds a 32-bit write down to a word (80002050)
      case 3: {
        const size_t rows = ((size_t)v + 7) / 8;
        if (rows && rows >= c.lines.size() - i) return refuse("has a string write cut short");
        for (uint32_t k = 0; k < v; ++k) {
          const auto& l = c.lines[i + 1 + k / 8];
          const uint32_t word = (k % 8) < 4 ? l.first : l.second;
          bytes.push_back((uint8_t)(word >> (24 - 8 * (k % 4))));
        }
        i += rows;
        break;
      }
      case 4: {   // the serial write: the value, then on the next line size, count, step and increment
        if (i + 1 >= c.lines.size()) return refuse("has a serial write cut short");
        const uint32_t second = c.lines[i + 1].first, increment = c.lines[i + 1].second;
        const uint32_t size = second >> 28;
        if (size > 2) return refuse("has a serial write of an unknown size");
        const uint32_t width = 1u << size, times = ((second >> 16) & 0xFFFu) + 1, step = second & 0xFFFFu;
        for (uint32_t k = 0; k < times; ++k) {
          const uint32_t item = v + k * increment;
          uint8_t b[4];
          for (uint32_t n = 0; n < width; ++n) b[n] = (uint8_t)(item >> (8 * (width - 1 - n)));
          if (!write(addr + k * step, b, width)) return false;
        }
        ++i;
        return true;
      }
      default: return refuse("uses code type " + hex8(w).substr(0, 2) + ", which has no meaning here");
    }
    return write(addr, bytes.data(), (uint32_t)bytes.size());
  }

  // 20 to 2E: the ifs. An address ending in 1 ends one if first. The handler adds the line's 25
  // bits, the 1 included, and then rounds down to what it reads: a word (80002104) or a half
  // (80002110).
  bool if_line(uint32_t w, uint32_t v, uint32_t sub) {
    if (w & 1) endif(1, false);
    uint32_t from = 0;
    if (!base_of(w, from)) return false;
    Op op;
    op.kind = Op::If; op.aux = (uint8_t)(sub & 3);
    op.width = (uint8_t)(sub < 4 ? 4 : 2);
    op.addr = (from + (w & 0x01FFFFFFu)) & ~(uint32_t)(op.width - 1);
    if (!read(op.addr, op.width)) return false;
    if (op.width == 4) { op.value = v; op.count = 0xFFFFFFFFu; }
    else { op.value = v & 0xFFFFu; op.count = ~(v >> 16) & 0xFFFFu; }
    c.plan.push_back(op);
    ++depth;
    return true;
  }

  // 40 to 4E: the base address and the pointer. The handler tests one bit of each flag digit
  // (80002198 to 800021F0): add (bit 20), relative (bit 16), plus a register (bit 12).
  bool base_line(uint32_t w, uint32_t v, uint32_t sub) {
    const int target = (sub & 4) ? 1 : 0;
    const std::string type = hex8(w).substr(0, 2);
    const uint32_t add = (w >> 20) & 1, relative = (w >> 16) & 1, by_register = (w >> 12) & 1;
    if ((sub & 3) == 0)
      return refuse(std::string("loads the ") + called(target) + " from game memory (code type " + type + "): a pointer there is a PC address here");
    if ((sub & 3) == 3) return refuse("takes the address of its own lines (code type " + type + ")");
    if (by_register) return refuse("adds a register to an address (code type " + type + ")");
    const std::string lost = std::string("uses the ") + called(target) + " after a condition that changed it has ended";
    if ((sub & 3) == 1) {   // set
      uint32_t operand = v;
      if (relative) {   // plus what a line counts from (base_of: the base address's top bits only)
        uint32_t from = 0;
        if (!base_of(w, from)) return false;
        operand += from;
      }
      if (add) {        // plus the register being set, in full (800021F8, 80002200)
        if (!known[target]) return refuse(lost);
        operand += base[target];
      }
      base[target] = operand; known[target] = true; set_at[target] = depth;
      return true;
    }
    // store: the register's value in full, a console address, written as a number. The handler
    // counts the address from the base whether or not the relative bit is set (stwx r6,r12,r4 at
    // 8000221C): 44000000 00400034 writes at 80400034, and 44000000 80400034 at 00400034.
    uint32_t from = 0;
    if (!base_of(w, from)) return false;
    if (!known[target]) return refuse(lost);
    return write32(from + v, base[target]);
  }

  // 80 to 8E: the Gecko registers.
  bool register_line(uint32_t w, uint32_t v, uint32_t sub) {
    const uint32_t high = (w >> 20) & 0xF, relative = (w >> 16) & 0xF;
    const std::string type = hex8(w).substr(0, 2);
    Op op;
    op.reg = (uint8_t)(w & 0xF);
    if (sub <= 2) {
      uint32_t operand = v;
      if (relative & 1) {   // the handler tests bit 16 alone (800022F8, 80002364)
        uint32_t from = 0;
        if (!base_of(w, from)) return false;
        operand += from;
      }
      if (sub == 0) {   // 80: set or add a number (add: bit 20 alone, 80002370)
        op.kind = Op::GrSet; op.value = operand; op.aux = (uint8_t)(high & 1);
        c.plan.push_back(op);
        return true;
      }
      if (high > 2) return refuse(sub == 1 ? "has a register load of an unknown size" : "has a register store of an unknown size");
      op.width = (uint8_t)(1u << high); op.addr = operand;
      if (sub == 1) {   // 82: load
        if (!read(op.addr, op.width)) return false;
        op.kind = Op::GrLoad;
      } else {          // 84: store, one or more times in a row
        op.count = ((w >> 4) & 0xFFFu) + 1;
        if (!check(op.addr, op.width * op.count, false)) return false;
        op.kind = Op::GrStore;
      }
      c.plan.push_back(op);
      return true;
    }
    if (sub == 3 || sub == 4) {   // 86: register and number or memory; 88: two registers
      if (high > 0xA) return refuse("has a register operation of an unknown kind");
      op.aux = (uint8_t)high;
      if (relative == 0) {
        op.kind = sub == 3 ? Op::GrOp : Op::GrOpGr;
        op.value = sub == 3 ? v : (v & 0xF);
      } else if (relative == 2 && sub == 3) {
        // The number is an address, which the handler always counts from the base (add r19,r12,r19
        // at 800023B8): 86020002 00400034 reads 80400034.
        uint32_t from = 0;
        if (!base_of(w, from)) return false;
        op.kind = Op::GrOpMem; op.addr = from + v; op.width = 4;
        if (!read(op.addr, 4)) return false;
      } else {
        return refuse("uses a register as an address (code type " + type + ")");
      }
      c.plan.push_back(op);
      return true;
    }
    return refuse("copies memory between addresses held in registers (code type " + type + ")");
  }

  // C0 to CE: PowerPC of the code's own, which nothing here can run.
  bool code_line(uint32_t w, uint32_t sub) {
    const std::string type = hex8(w).substr(0, 2);
    if (sub == 1) {
      uint32_t from = 0;
      const ConsoleSymbol* at = known[which_base(w)] && base_of(w, from) ? symbol_at((from + (w & 0x01FFFFFFu)) & ~3u) : nullptr;
      if (at && at->kind == kCode) return refuse(std::string("injects PowerPC code into ") + at->name + " (C2), which this build cannot run");
      return refuse("injects PowerPC code (C2), which this build cannot run");
    }
    if (sub == 0 || sub == 3) return refuse("runs PowerPC code of its own (code type " + type + ")");
    return refuse("uses code type " + type + ", which has no meaning here");
  }

  bool compile() {
    if (c.lines.empty()) return refuse("has no code lines");
    for (size_t i = 0; i < c.lines.size(); ++i) {
      const uint32_t w = c.lines[i].first, v = c.lines[i].second;
      const uint32_t type = w >> 24, family = type >> 5, sub = (type >> 1) & 7;
      const std::string name = hex8(w).substr(0, 2);
      if (type == 0xF0) break;   // the end of a code list
      bool ok = true;
      switch (family) {
        case 0: ok = write_line(i, sub); break;
        case 1: ok = if_line(w, v, sub); break;
        case 2: ok = base_line(w, v, sub); break;
        case 3: ok = refuse("uses a loop or a jump (code type " + name + ")"); break;
        case 4: ok = register_line(w, v, sub); break;
        case 5: ok = refuse("compares registers or counts frames (code type " + name + ")"); break;
        case 6: ok = code_line(w, sub); break;
        default:
          if (type == 0xE0) {
            Op op;
            op.kind = Op::End;
            c.plan.push_back(op);
            close_to(0, false);
            reset_bases(v);
          } else if (type == 0xE2) {
            // How many ifs end: five bits (clrlwi. r9,r3,27 at 80002740), then else (bit 20).
            endif(w & 0x1Fu, ((w >> 20) & 1) != 0);
            reset_bases(v);
          } else {
            ok = refuse("uses code type " + name + ", which has no meaning here");
          }
          break;
      }
      if (!ok) return false;
    }
    return true;
  }
};

uint32_t be(const uint8_t* b, uint32_t width) {
  uint32_t v = 0;
  for (uint32_t k = 0; k < width; ++k) v = (v << 8) | b[k];
  return v;
}

// One Gecko register operation (code types 86 and 88), as the handler's PowerPC does it. `a` is the
// register the result goes to, `b` the number, the memory or the other register. The handler loads
// a into r4 and b into r9 (80002440) and its shifts and its rotate are "slw r4,r9,r4" and the like
// (800023F4 to 8000240C): the OPERAND is shifted, BY the register, not the other way round as the
// written descriptions of these codes say. A shift by 32 or more gives 0 (or the sign, for the
// arithmetic one); the float ones work on the bit patterns.
uint32_t operate(uint32_t a, uint32_t b, uint8_t operation) {
  switch (operation) {
    case 0: return a + b;
    case 1: return a * b;
    case 2: return a | b;
    case 3: return a & b;
    case 4: return a ^ b;
    case 5: return (a & 0x20u) ? 0u : b << (a & 0x1Fu);
    case 6: return (a & 0x20u) ? 0u : b >> (a & 0x1Fu);
    case 7: { const uint32_t n = a & 0x1Fu; return n ? (b << n) | (b >> (32u - n)) : b; }
    case 8: {
      const uint32_t n = std::min<uint32_t>(a & 0x3Fu, 31u);
      const uint32_t fill = (b & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u;
      return (b >> n) | fill;
    }
    default: {
      float fa, fb;
      std::memcpy(&fa, &a, 4); std::memcpy(&fb, &b, 4);
      const float r = operation == 9 ? fa + fb : fa * fb;
      uint32_t out;
      std::memcpy(&out, &r, 4);
      return out;
    }
  }
}

// Runs one code's plan for this frame, by the Gecko handler's rules. 1 when done, -1 while a
// network match is running (the game takes no codes), 0 when the game library does not offer an
// address the plan uses (left in `bad`).
int32_t run_plan(const Code& c, uint32_t& bad) {
  uint32_t skipping = 0;   // 0: running; else how many ifs deep, counted from the first that failed
  uint8_t b[4];
  static std::vector<uint8_t> repeated;   // game thread only
  for (const Op& op : c.plan) {
    bad = op.addr;
    if (op.kind == Op::If) {
      if (skipping) { ++skipping; continue; }
      const int32_t got = g_native_read ? g_native_read(op.addr, b, op.width) : 0;
      if (got <= 0) return got;
      const uint32_t memory = be(b, op.width) & op.count;
      const bool passed = op.aux == 0 ? memory == op.value : op.aux == 1 ? memory != op.value
                          : op.aux == 2 ? memory > op.value : memory < op.value;
      if (!passed) skipping = 1;
      continue;
    }
    if (op.kind == Op::Endif) {
      // The handler keeps one bit per open if, the innermost lowest, and an if opened inside a
      // block that is being skipped copies that block's bit (rlwimi r8,r8,1,0,30 at 800020E4): the
      // set bits are always the lowest ones, which is what this count is. An endif shifts the bits
      // out (srw r8,r8,r9 at 80002748). An else then flips the innermost bit only when the bit
      // above it is clear (8000274C to 80002754): inside an outer block that is being skipped it
      // changes nothing, which is the last case below.
      skipping = skipping > op.count ? skipping - op.count : 0;
      if (op.aux) skipping = skipping == 0 ? 1 : skipping == 1 ? 0 : skipping;
      continue;
    }
    if (op.kind == Op::End) { skipping = 0; continue; }
    if (skipping) continue;
    uint32_t& reg = g_registers[op.reg & 15];
    int32_t done = 1;
    switch (op.kind) {
      case Op::Write: done = g_native_write(op.addr, c.plan_bytes.data() + op.value, op.count); break;
      case Op::GrSet: reg = op.value + (op.aux ? reg : 0u); break;
      case Op::GrLoad:
        done = g_native_read ? g_native_read(op.addr, b, op.width) : 0;
        if (done > 0) reg = be(b, op.width);
        break;
      case Op::GrStore:
        repeated.clear();
        for (uint32_t k = 0; k < op.count; ++k)
          for (uint32_t n = 0; n < op.width; ++n) repeated.push_back((uint8_t)(reg >> (8 * (op.width - 1u - n))));
        done = g_native_write(op.addr, repeated.data(), (uint32_t)repeated.size());
        break;
      case Op::GrOp: reg = operate(reg, op.value, op.aux); break;
      case Op::GrOpGr: reg = operate(reg, g_registers[op.value & 15], op.aux); break;
      case Op::GrOpMem:
        done = g_native_read ? g_native_read(op.addr, b, 4) : 0;
        if (done > 0) reg = operate(reg, be(b, 4), op.aux);
        break;
      default: break;
    }
    if (done <= 0) return done;
  }
  return 1;
}

// Checks every line and says why a code cannot run here, or leaves reason empty.
void classify(Code& c) {
  c.supported = false;
  c.reason.clear();
  c.plan.clear();
  c.plan_bytes.clear();
  c.reads = false;
  if (c.lines.empty()) { c.reason = "has no code lines"; return; }
  // No code patches means the Source Port (set_code_patches_allowed): every read and write is then
  // held to the table of variables the native game shares with the console, and the code becomes
  // a plan (Compiler above).
  const bool source_port = !g_code_patches_allowed;
  if (source_port) {
    Compiler compiler(c);
    c.supported = compiler.compile();
    if (!c.supported) { c.reason = compiler.fail; c.plan.clear(); c.plan_bytes.clear(); c.reads = false; }
    return;
  }
  for (size_t i = 0; i < c.lines.size(); ++i) {
    const uint32_t w = c.lines[i].first, v = c.lines[i].second;
    const uint32_t type = w >> 24;
    if (type == 0xE0 || type == 0xF0) continue;   // terminators: nothing to do
    if (type & 0x10) { c.reason = "writes through the pointer register (code type " + hex8(w).substr(0, 2) + "), which needs the Gecko handler"; return; }
    const uint32_t kind = type & 0x0E;
    if (type > 0x07 || kind > 0x06) {
      c.reason = type == 0xC2 || type == 0xC3 ? "injects PowerPC code (C2), which this build cannot run"
                                              : "uses code type " + hex8(w).substr(0, 2) + ", which needs the Gecko handler";
      return;
    }
    const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
    uint32_t len = kind == 0x00 ? ((v >> 16) + 1) : kind == 0x02 ? ((v >> 16) + 1) * 2 : kind == 0x04 ? 4 : v;
    if (kind == 0x06) i += (v + 7) / 8;           // the string's bytes follow on the next lines
    if (i >= c.lines.size() && kind == 0x06) { c.reason = "has a string write cut short"; return; }
    if (addr < 0x80003100u || addr + len > 0x81800000u) { c.reason = "writes outside game memory (" + hex8(addr) + ")"; return; }
    if (in_code(addr) || in_code(addr + len - 1)) c.patches_code = true;
  }
  c.supported = true;
}

// The Source Port's apply(): each enabled code's plan, run against the game library, which keeps
// the variables in host byte order and converts on the way in and out.
void apply_native() {
  if (!g_native_write) return;
  for (Code& c : g_codes) {
    if (!c.enabled || !c.supported) continue;
    if (c.reads && !g_native_read) {
      // An older game library: it stores but cannot hand a variable back.
      c.supported = false;
      c.enabled = false;
      c.reason = "reads game variables, which this game library cannot hand back";
      host::log("gecko: \"%s\" is off: the game library has no mu_user_gecko_read", c.name.c_str());
      continue;
    }
    uint32_t bad = 0;
    const int32_t result = run_plan(c, bad);
    if (result < 0) return;   // a network match is running: the game takes no codes
    if (result == 0) {
      // The game library does not know a variable this host's table lists: the two were built
      // from different trees. The code goes off instead of being half applied every frame.
      c.supported = false;
      c.enabled = false;
      c.reason = "uses " + hex8(bad) + ", which this game library does not offer";
      host::log("gecko: \"%s\" is off: the game library refused the address %08X", c.name.c_str(), bad);
    }
  }
}

// A write of `len` bytes at `addr` by a code that patches the game's code. The first time, the
// bytes it replaces inside the code are kept, so switching the code off can put them back.
void keep_original(Code& c, uint32_t addr, uint32_t len) {
  if (c.patch_live) return;
  for (uint32_t k = 0; k < len; ++k)
    if (in_code(addr + k)) c.original.push_back({addr + k, host::rd8(addr + k)});
}

// The patched instructions are in RAM: the functions holding them run from there from now on.
void run_patched_functions(Code& c) {
  uint32_t last = 0;
  int words = 0, missing = 0;
  for (const auto& b : c.original) {
    const uint32_t word = b.first & ~3u;
    if (word == last) continue;
    last = word;
    if (ppc::redirect_function_at(word)) ++words; else ++missing;
  }
  host::log("gecko: code \"%s\" patches the game's code: %d instruction words now run from memory%s", c.name.c_str(), words,
            missing ? " (some are not inside a function and stay as they were)" : "");
}

// The code was switched off: the game's own instructions go back (the functions keep running from
// RAM, which holds the original bytes again).
void restore_original(Code& c) {
  for (const auto& b : c.original) host::wr8(b.first, b.second);
  c.original.clear();
  c.patch_live = false;
  host::log("gecko: code \"%s\" is off: the game's own code is back", c.name.c_str());
}

}  // namespace

void set_code_patches_allowed(bool allowed) { g_code_patches_allowed = allowed; }
void set_native_writer(NativeWrite write) { g_native_write = write; }
void set_native_reader(NativeRead read) { g_native_read = read; }

void load(const std::string& path, const std::vector<std::string>& enabled_names, bool chosen) {
  g_path = path;
  g_codes.clear();
  std::ifstream f(path);
  if (!f) { host::log("gecko: no user codes (%s not found)", path.c_str()); return; }
  std::vector<std::string> ini_enabled;
  std::string line, section;
  static const std::regex code_line(R"(^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8}))");
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    line = line.substr(first);
    if (line[0] == '[') { section = line; continue; }
    if (section == "[Gecko_Enabled]") { if (line[0] == '$') ini_enabled.push_back(line.substr(1)); continue; }
    if (section != "[Gecko]") continue;
    if (line[0] == '$') {
      Code c; c.name = line.substr(1);
      const size_t bracket = c.name.find('[');   // Dolphin: "$Name [Author]"
      if (bracket != std::string::npos) c.name = c.name.substr(0, bracket);
      while (!c.name.empty() && c.name.back() == ' ') c.name.pop_back();
      g_codes.push_back(c);
      continue;
    }
    if (g_codes.empty()) continue;
    if (line[0] == '*') { g_codes.back().notes.push_back(line.substr(1)); continue; }
    std::smatch m;
    if (std::regex_search(line, m, code_line))
      g_codes.back().lines.push_back({(uint32_t)std::stoul(m[1].str(), nullptr, 16), (uint32_t)std::stoul(m[2].str(), nullptr, 16)});
  }
  const std::vector<std::string>& on = chosen ? enabled_names : ini_enabled;
  int supported = 0, enabled = 0;
  for (Code& c : g_codes) {
    classify(c);
    c.enabled = c.supported && std::find(on.begin(), on.end(), c.name) != on.end();
    supported += c.supported; enabled += c.enabled;
    if (!c.supported) host::log("gecko: \"%s\" cannot run here: %s", c.name.c_str(), c.reason.c_str());
  }
  host::log("gecko: %zu user codes in %s (%d usable, %d on)", g_codes.size(), path.c_str(), supported, enabled);
}

const char* native_equivalent(const Code& c) {
  // Slippi's optional codes (GALE01r2.ini) that the Source Port carries as C, by the code's first
  // line. Never by its name: a code is what its lines do, and one that only shares a name with a
  // built-in switch is judged like any other.
  struct Known { uint32_t w, v; const char* label; };
  static const Known known[] = {
      {0x043BB05Cu, 0x3EB00000u, "Widescreen 16:9"},
      {0x04030E44u, 0x4E800020u, "Disable Screen Shake"},
      {0xC20C0148u, 0x0000000Cu, "Flash Red on Failed L-Cancel"},
      {0xC21CBB90u, 0x00000005u, "Lagless FoD"},
  };
  for (const Known& k : known)
    if (!c.lines.empty() && c.lines[0].first == k.w && c.lines[0].second == k.v) return k.label;
  // Any other code: built in when every one of its patches is one the Source Port carries as C,
  // by address and exact words (kBuiltInUnits; tools/gecko_targets.py, built_in_label). A patch is
  // one write, one string or serial write with its data lines, or one block of PowerPC with its
  // instructions; terminator lines are not patches.
  const char* found = nullptr;
  const size_t n = c.lines.size();
  for (size_t i = 0; i < n;) {
    const uint32_t w = c.lines[i].first, v = c.lines[i].second, type = w >> 24, kind = type & 0xEEu;
    size_t count = 1;
    if (kind == 0xC0 || kind == 0xC2) count = 1 + (size_t)v;
    else if (kind == 0x06) count = 1 + ((size_t)v + 7) / 8;
    else if (kind == 0x08) count = 2;
    if (count > n - i) count = n - i;
    if (count == 1 && (type == 0xE0 || type == 0xF0)) { ++i; continue; }
    // An injection of one instruction that is not a relative branch does exactly what a 32-bit
    // write of it does: it is looked up as that write.
    uint32_t words[2] = {w, v};
    size_t hashed = count;
    const bool single = (type == 0xC2 || type == 0xC3) && v == 1 && count == 2 && c.lines[i + 1].second == 0 &&
                        (c.lines[i + 1].first >> 26) != 16 && (c.lines[i + 1].first >> 26) != 18;
    if (single) { words[0] = 0x04000000u | (w & 0x01FFFFFFu); words[1] = c.lines[i + 1].first; hashed = 1; }
    uint64_t hash = 0xCBF29CE484222325ull;   // FNV-1a over the words as big-endian bytes
    for (size_t k = 0; k < hashed; ++k) {
      const uint32_t pair[2] = {k == 0 ? words[0] : c.lines[i + k].first, k == 0 ? words[1] : c.lines[i + k].second};
      for (uint32_t word : pair)
        for (int shift = 24; shift >= 0; shift -= 8) hash = (hash ^ ((word >> shift) & 0xFFu)) * 0x100000001B3ull;
    }
    const BuiltInUnit want = {words[0], (uint32_t)hashed, hash, 0};
    const BuiltInUnit* last = std::end(kBuiltInUnits);
    const BuiltInUnit* it = std::lower_bound(std::begin(kBuiltInUnits), last, want, [](const BuiltInUnit& a, const BuiltInUnit& b) {
      return a.word != b.word ? a.word < b.word : a.lines != b.lines ? a.lines < b.lines : a.hash < b.hash;
    });
    if (it == last || it->word != want.word || it->lines != want.lines || it->hash != want.hash) return nullptr;
    if (!found) found = kBuiltInLabels[it->label];
    i += count;
  }
  return found;
}

const std::string& path() { return g_path; }
std::vector<Code>& codes() { return g_codes; }
bool any_enabled() { return std::any_of(g_codes.begin(), g_codes.end(), [](const Code& c) { return c.enabled; }); }

std::string add(const std::string& name, const std::string& body) {
  Code c; c.name = name;
  std::istringstream in(body);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) { first = false; continue; }
    line = line.substr(start);
    // A pasted whole code (Dolphin's own "$Name" line included) names itself; the typed Name field
    // is then only what is offered until the paste says otherwise.
    if (first && line[0] == '$') { c.name = line.substr(1); first = false; continue; }
    first = false;
    if (line[0] == '$') continue;   // a second header inside one paste: only the first code is kept
    if (line[0] == '*') { c.notes.push_back(line.substr(1)); continue; }
    std::smatch m;
    static const std::regex code_line(R"(^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8}))");
    if (std::regex_search(line, m, code_line))
      c.lines.push_back({(uint32_t)std::stoul(m[1].str(), nullptr, 16), (uint32_t)std::stoul(m[2].str(), nullptr, 16)});
  }
  while (!c.name.empty() && c.name.back() == ' ') c.name.pop_back();
  if (c.name.empty()) return "needs a name";
  if (std::any_of(g_codes.begin(), g_codes.end(), [&](const Code& e) { return e.name == c.name; }))
    return "a code named \"" + c.name + "\" already exists";
  if (c.lines.empty()) return "no XXXXXXXX YYYYYYYY code lines found";
  classify(c);
  g_codes.push_back(c);
  return "";
}

void remove(const std::string& name) {
  g_codes.erase(std::remove_if(g_codes.begin(), g_codes.end(), [&](const Code& c) { return c.name == name; }), g_codes.end());
}

bool save() {
  if (g_path.empty()) return false;
  std::ofstream f(g_path, std::ios::trunc);
  if (!f) return false;
  f << "[Gecko]\n";
  for (const Code& c : g_codes) {
    f << "$" << c.name << "\n";
    for (const std::string& n : c.notes) f << "*" << n << "\n";
    for (const auto& l : c.lines) { char b[20]; std::snprintf(b, sizeof b, "%08X %08X", l.first, l.second); f << b << "\n"; }
  }
  // Kept for Dolphin and other tools that read this file directly; this app tracks enabled codes in
  // port-settings.ini instead (a code's on/off state should not depend on which game folder it is
  // playing from), so this section is written but never read back by load() once `chosen` is true.
  f << "\n[Gecko_Enabled]\n";
  for (const Code& c : g_codes) if (c.enabled) f << "$" << c.name << "\n";
  return f.good();
}

void apply() {
  if (!g_code_patches_allowed) { apply_native(); return; }   // the Source Port has no console memory to write
  for (Code& c : g_codes) {
    if (!c.enabled) { if (c.patch_live) restore_original(c); continue; }
    if (!c.patches_code) {
      for (size_t i = 0; i < c.lines.size(); ++i) {
        const uint32_t w = c.lines[i].first, v = c.lines[i].second, type = w >> 24;
        if (type == 0xE0 || type == 0xF0) continue;
        const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
        switch (type & 0x0E) {
          case 0x00: for (uint32_t k = 0; k <= (v >> 16); ++k) host::wr8(addr + k, (uint8_t)v); break;
          case 0x02: for (uint32_t k = 0; k <= (v >> 16); ++k) host::wr16(addr + 2 * k, (uint16_t)v); break;
          case 0x04: host::wr32(addr, v); break;
          case 0x06: {
            for (uint32_t k = 0; k < v; ++k) {
              const auto& l = c.lines[i + 1 + k / 8];
              const uint32_t word = (k % 8) < 4 ? l.first : l.second;
              host::wr8(addr + k, (uint8_t)(word >> (24 - 8 * (k % 4))));
            }
            i += (v + 7) / 8;
            break;
          }
        }
      }
      continue;
    }
    // A code that writes into the game's code. A byte is written only when it differs: a mod
    // session watches its code blocks for changes, and the same values every frame would have a
    // block compared again every frame.
    auto put8 = [](uint32_t a, uint8_t b) { if (host::rd8(a) != b) host::wr8(a, b); };
    for (size_t i = 0; i < c.lines.size(); ++i) {
      const uint32_t w = c.lines[i].first, v = c.lines[i].second, type = w >> 24;
      if (type == 0xE0 || type == 0xF0) continue;
      const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
      switch (type & 0x0E) {
        case 0x00:
          keep_original(c, addr, (v >> 16) + 1);
          for (uint32_t k = 0; k <= (v >> 16); ++k) put8(addr + k, (uint8_t)v);
          break;
        case 0x02:
          keep_original(c, addr, ((v >> 16) + 1) * 2);
          for (uint32_t k = 0; k <= (v >> 16); ++k) { put8(addr + 2 * k, (uint8_t)(v >> 8)); put8(addr + 2 * k + 1, (uint8_t)v); }
          break;
        case 0x04:
          keep_original(c, addr, 4);
          for (uint32_t k = 0; k < 4; ++k) put8(addr + k, (uint8_t)(v >> (24 - 8 * k)));
          break;
        case 0x06: {
          keep_original(c, addr, v);
          for (uint32_t k = 0; k < v; ++k) {
            const auto& l = c.lines[i + 1 + k / 8];
            const uint32_t word = (k % 8) < 4 ? l.first : l.second;
            put8(addr + k, (uint8_t)(word >> (24 - 8 * (k % 4))));
          }
          i += (v + 7) / 8;
          break;
        }
      }
    }
    if (!c.patch_live) { c.patch_live = true; run_patched_functions(c); }
  }
}

}  // namespace user_gecko
