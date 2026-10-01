// Decode once at translation time. Unsupported functions are rejected before allocating code.
// Only canonical encodings are accepted: a set reserved bit, OE, LK or AA rejects the function.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stencil_format.h"
#include <string>
#include <utility>
#include <vector>

namespace ppc::stencil {
// One stencil instance. A guest instruction becomes one or more of these (a record form is its
// operation followed by Record; a branch is its tests, then Backedge and Exit when it goes
// backward). next and taken are indices into the plan. An Exit keeps its number in immediate
// and, in taken, the stencil where execution resumes after the poll.
constexpr uint32_t kNoStencil = 0xFFFFFFFFu;
struct Instruction {
  Operation operation;
  uint32_t destination = 0, source = 0, immediate = 0, source2 = 0, immediate2 = 0;
  uint32_t next = kNoStencil, taken = kNoStencil;
};
// The mask emit.py computes for rlwinm, rlwnm and rlwimi (Emitter._mask).
constexpr uint32_t rotate_mask(uint32_t mb, uint32_t me) {
  const uint32_t begin = 0xFFFFFFFFu >> mb, end = 0x7FFFFFFFu >> me, m = begin ^ end;
  return me < mb ? ~m : m;
}
inline bool plan_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                      std::vector<Instruction>& output, std::string& error) {
  output.clear();
  error.clear();
  constexpr uint32_t ram_base = 0x80000000u, ram_size = 0x01800000u;
  if (!code || !bytes || (bytes & 3) || (address & 3) || bytes > 4096 ||
      address < ram_base || address - ram_base >= ram_size ||
      bytes > ram_size - (address - ram_base)) {
    error = "invalid or oversized leaf function range";
    return false;
  }
  const uint32_t count = uint32_t(bytes / 4);
  // While planning, a link at or above kGuest names the first stencil of a guest instruction.
  constexpr uint32_t kGuest = 0x80000000u;
  std::vector<Instruction> plan;
  std::vector<uint32_t> first(count + 1, kNoStencil);
  uint32_t exits = 0;
  plan.reserve(count * 2);
  for (uint32_t index = 0; index < count; ++index) {
    const size_t at = size_t(index) * 4;
    const uint32_t word = (uint32_t(code[at]) << 24) | (uint32_t(code[at+1]) << 16) |
                          (uint32_t(code[at+2]) << 8) | uint32_t(code[at+3]);
    const auto unsupported = [&] {
      error = "unsupported opcode at byte " + std::to_string(at);
      return false;
    };
    first[index] = uint32_t(plan.size());
    const uint32_t op = word >> 26, first_field = (word >> 21) & 31, second = (word >> 16) & 31;
    const uint32_t third = (word >> 11) & 31;
    const bool record = word & 1;
    uint32_t immediate = word & 0xFFFFu;
    const uint32_t simm = (immediate & 0x8000u) ? immediate | 0xFFFF0000u : immediate;
    const uint32_t fall = kGuest | (index + 1);
    // Appends one stencil; a record form is followed by Record of the destination register.
    const auto emit = [&](Operation operation, uint32_t destination, uint32_t source,
                          uint32_t value, uint32_t source2, uint32_t value2, bool with_record) {
      Instruction instruction{};
      instruction.operation = operation;
      instruction.destination = destination; instruction.source = source;
      instruction.immediate = value; instruction.source2 = source2; instruction.immediate2 = value2;
      instruction.next = with_record ? uint32_t(plan.size()) + 1 : fall;
      plan.push_back(instruction);
      if (with_record) {
        Instruction cr0{};
        cr0.operation = Operation::Record; cr0.source = destination; cr0.next = fall;
        plan.push_back(cr0);
      }
    };
    // A branch as emit.py writes it: `if (cond_expr) { [ppc::backedge(c);] goto L; }` or
    // `return;`. cond_expr decrements and tests CTR first unless BO bit 4 is set, then tests the
    // CR bit unless BO bit 16 is set, short-circuit. A target at or before the branch is a loop
    // back-edge.
    const auto emit_branch = [&](uint32_t bo, uint32_t bi, bool to_return, uint32_t target) {
      const bool test_ctr = !(bo & 4), test_cr = !(bo & 16);
      const uint32_t tests = uint32_t(test_ctr) + uint32_t(test_cr);
      const uint32_t base = uint32_t(plan.size());
      const bool back = !to_return && target <= index;
      const uint32_t destination = (to_return || back) ? base + tests : (kGuest | target);
      if (test_ctr) {
        Instruction test{};
        test.operation = (bo & 2) ? Operation::BranchCtrZero : Operation::BranchCtrNonzero;
        test.taken = test_cr ? base + 1 : destination;
        test.next = fall;
        plan.push_back(test);
      }
      if (test_cr) {
        Instruction test{};
        test.operation = (bo & 8) ? Operation::BranchCrSet : Operation::BranchCrClear;
        test.source = bi >> 2; test.immediate = 8u >> (bi & 3);
        test.taken = destination;
        test.next = fall;
        plan.push_back(test);
      }
      if (to_return) {
        plan.push_back({Operation::Return});
      } else if (back) {
        Instruction poll{}, leave{};
        poll.operation = Operation::Backedge;
        poll.next = kGuest | target; poll.taken = base + tests + 1;
        leave.operation = Operation::Exit;
        leave.immediate = ++exits; leave.taken = kGuest | target;
        plan.push_back(poll);
        plan.push_back(leave);
      } else if (!tests) {
        Instruction jump{};
        jump.operation = Operation::Jump; jump.next = kGuest | target;
        plan.push_back(jump);
      }
    };
    // Only branches that stay inside this function, relative, without link.
    const auto emit_relative = [&](uint32_t bo, uint32_t bi, int32_t displacement) {
      const int64_t target = int64_t(index) + displacement / 4;
      if ((word & 3) || target < 0 || target >= int64_t(count)) return false;
      emit_branch(bo, bi, false, uint32_t(target));
      return true;
    };
    if (op == 18) { // b
      const uint32_t li = word & 0x03FFFFFCu;
      if (!emit_relative(20, 0, int32_t((li & 0x02000000u) ? li | 0xFC000000u : li))) return unsupported();
      continue;
    }
    if (op == 16) { // bc
      const uint32_t bd = word & 0xFFFCu;
      if (!emit_relative(first_field, second, int32_t((bd & 0x8000u) ? bd | 0xFFFF0000u : bd))) return unsupported();
      continue;
    }
    if (op == 19 && ((word >> 1) & 0x3FFu) == 16) { // bclr: no link, reserved field clear
      if (record || third) return unsupported();
      emit_branch(first_field, second, true, 0);
      continue;
    }
    if (op == 14 || op == 15) { // RA=0 means zero only for addi/addis.
      emit(Operation::Add, first_field, second, op == 15 ? simm << 16 : simm, 0, 0, false);
    } else if (op >= 24 && op <= 29) { // RS=0 reads r0 normally for logical instructions.
      emit(op < 26 ? Operation::Or : op < 28 ? Operation::Xor : Operation::AndRecord,
           second, first_field, (op & 1) ? immediate << 16 : immediate, 0, 0, false);
    } else if (op == 7) {
      emit(Operation::Mulli, first_field, second, simm, 0, 0, false);
    } else if (op == 8) {
      emit(Operation::Subfic, first_field, second, simm, 0, 0, false);
    } else if (op == 12 || op == 13) {
      emit(Operation::Addic, first_field, second, simm, 0, 0, op == 13);
    } else if (op == 20 || op == 21) { // rlwimi, rlwinm: RA = second, RS = first, SH = third.
      emit(op == 20 ? Operation::Rlwimi : Operation::Rlwinm, second, first_field, third, 0,
           rotate_mask((word >> 6) & 31, (word >> 1) & 31), record);
    } else if (op == 23) {
      emit(Operation::Rlwnm, second, first_field, 0, third,
           rotate_mask((word >> 6) & 31, (word >> 1) & 31), record);
    } else if (op == 31) {
      const uint32_t xo = (word >> 1) & 0x3FFu;
      // Arithmetic forms write RD = first from RA = second and RB = third. emit.py models no
      // overflow flag, so an OE=1 word (xo + 512) stays unsupported.
      Operation operation{};
      bool arithmetic = true, unary = false;
      switch (xo) {
        case 266: operation = Operation::AddReg; break;
        case 40: operation = Operation::Subf; break;
        case 235: operation = Operation::Mullw; break;
        case 75: operation = Operation::Mulhw; break;
        case 11: operation = Operation::Mulhwu; break;
        case 491: operation = Operation::Divw; break;
        case 459: operation = Operation::Divwu; break;
        case 10: operation = Operation::Addc; break;
        case 138: operation = Operation::Adde; break;
        case 8: operation = Operation::Subfc; break;
        case 104: operation = Operation::Neg; unary = true; break;
        case 202: operation = Operation::Addze; unary = true; break;
        case 234: operation = Operation::Addme; unary = true; break;
        case 200: operation = Operation::Subfze; unary = true; break;
        case 232: operation = Operation::Subfme; unary = true; break;
        default: arithmetic = false; break;
      }
      if (arithmetic) {
        if (unary && third) return unsupported();
        emit(operation, first_field, second, 0, unary ? 0 : third, 0, record);
        continue;
      }
      // Logical and shift forms write RA = second from RS = first and RB = third.
      bool logical = true;
      switch (xo) {
        case 28: operation = Operation::AndReg; break;
        case 444: operation = Operation::OrReg; break;
        case 316: operation = Operation::XorReg; break;
        case 476: operation = Operation::Nand; break;
        case 124: operation = Operation::Nor; break;
        case 284: operation = Operation::Eqv; break;
        case 60: operation = Operation::Andc; break;
        case 412: operation = Operation::Orc; break;
        case 24: operation = Operation::Slw; break;
        case 536: operation = Operation::Srw; break;
        case 792: operation = Operation::Sraw; break;
        case 954: operation = Operation::Extsb; unary = true; break;
        case 922: operation = Operation::Extsh; unary = true; break;
        case 26: operation = Operation::Cntlzw; unary = true; break;
        default: logical = false; break;
      }
      if (logical) {
        if (unary && third) return unsupported();
        emit(operation, second, first_field, 0, unary ? 0 : third, 0, record);
      } else if (xo == 824) {
        emit(Operation::Srawi, second, first_field, third, 0, 0, record);
      } else if (xo == 0 || xo == 32) { // cmpw, cmplw: no Rc, L=0 and the reserved bit clear.
        if (record || (first_field & 3)) return unsupported();
        emit(xo ? Operation::Cmplw : Operation::Cmpw, first_field >> 2, second, 0, third, 0, false);
      } else if (xo == 19) { // mfcr
        if (record || second || third) return unsupported();
        emit(Operation::Mfcr, first_field, 0, 0, 0, 0, false);
      } else if (xo == 144) { // mtcrf: the bits around CRM are reserved (bit 11 set is mtocrf).
        if (record || (word & 0x00100800u)) return unsupported();
        emit(Operation::Mtcrf, 0, first_field, (word >> 12) & 0xFFu, 0, 0, false);
      } else if (xo == 339 || xo == 467) { // mfspr, mtspr: XER, LR and CTR only.
        const uint32_t spr = (third << 5) | second;
        if (record || (spr != 1 && spr != 8 && spr != 9)) return unsupported();
        if (xo == 339) {
          emit(spr == 1 ? Operation::Mfxer : spr == 8 ? Operation::Mflr : Operation::Mfctr,
               first_field, 0, 0, 0, 0, false);
        } else {
          emit(spr == 1 ? Operation::Mtxer : spr == 8 ? Operation::Mtlr : Operation::Mtctr,
               0, first_field, 0, 0, 0, false);
        }
      } else {
        return unsupported();
      }
    } else if (op == 10 || op == 11) { // cmplwi, cmpwi: L=0 and the reserved bit clear.
      if (first_field & 3) return unsupported();
      emit(op == 11 ? Operation::Cmpwi : Operation::Cmplwi, first_field >> 2, second,
           op == 11 ? simm : immediate, 0, 0, false);
    } else if (op == 19) {
      const uint32_t xo = (word >> 1) & 0x3FFu;
      if (record) return unsupported(); // LK
      // mcrf only: the CR logical forms (crand, cror, ...) have no stencil yet.
      if (xo != 0 || (first_field & 3) || (second & 3) || third) return unsupported();
      emit(Operation::Mcrf, first_field >> 2, second >> 2, 0, 0, 0, false);
    } else {
      return unsupported();
    }
  }
  first[count] = kNoStencil; // Nothing follows the last instruction.
  for (auto& instruction : plan) {
    for (uint32_t* link : {&instruction.next, &instruction.taken}) {
      if (*link == kNoStencil || *link < kGuest) continue;
      const uint32_t target = *link & ~kGuest;
      if (target > count || first[target] == kNoStencil) {
        error = "leaf function can run past its last instruction";
        return false;
      }
      *link = first[target];
    }
  }
  output = std::move(plan);
  return true;
}
} // namespace ppc::stencil
