// Decode once at translation time. Unsupported functions are rejected before allocating code.
// Only canonical encodings are accepted, and only forms whose emit.py statement a stencil
// reproduces exactly. Every refusal has a reason; port/tests/ppc_leaf_refusal_test.cpp covers each.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stencil_format.h"
#include <string>
#include <utility>
#include <vector>

namespace ppc::stencil {
// What the driver (CompiledLeaf::run) does when the chain leaves through an Exit stencil. These
// are the statements of emit.py that need the state it keeps per invocation of a function: the
// ring of local return addresses (`lrs`, `lrn`) and the LR the function was entered with
// (`entry_lr`). The stencils stay stateless; the driver's stack frame is the invocation.
enum class ExitKind : uint8_t {
  Poll,        // ppc::loop_poll(c), then resume: the back-edge poll.
  LocalCall,   // `lrs[lrn++ & 31u] = ret; goto L_target;` (c.lr = ret was a SetLr stencil).
  LocalReturn, // `{ uint32_t t = c.lr; if (ppc::local_return(lrs, lrn, t)) { goto L_t; } return; }`
  LinkReturn,  // blrl: `{ uint32_t t = c.lr; c.lr = ret; [local return on t]
               //          if (t == entry_lr) return; ppc::call(c, m, t); }`, then resume.
  TailCall,    // `{ const uint32_t tl = c.lr; ppc::call(c, m, target); [local return on tl] return; }`
  TailCallCtr, // the same with `ppc::call(c, m, c.ctr);`
  RuntimeBranch, // Runtime mode uses the interpreter's RAM transfer and return rules.
};
// One stencil instance. A guest instruction becomes zero or more of these (a record form is its
// operation followed by Record; a branch is its tests, then its body: Backedge and Exit when it
// goes backward, Call, TailCall and Return, ...). next and taken are indices into the plan. An
// Exit keeps its number in immediate, what the driver does in exit and exit_value, and in taken
// the stencil where execution resumes afterwards (kNoStencil when it never does).
constexpr uint32_t kNoStencil = 0xFFFFFFFFu;
constexpr size_t kMaxFunctionBytes = 0x10000; // 16384 guest instructions
struct Instruction {
  Operation operation;
  uint32_t destination = 0, source = 0, immediate = 0, source2 = 0, immediate2 = 0;
  uint32_t next = kNoStencil, taken = kNoStencil;
  ExitKind exit = ExitKind::Poll;
  uint32_t exit_value = 0;
};
// Runtime mode: operations that cannot write guest RAM, change the dispatch, call guest or host
// code that could, or leave the translation. Register work, compares, native branch tests and
// loads (a load never has a side effect: see mmio_read). After an instruction made only of these
// the code guard of the next instruction has nothing new to detect, so the fall-through skips it.
// Anything not listed keeps its guard: stores, quantized forms, calls, exits, cache invalidation.
constexpr bool runtime_pure(Operation operation) {
  switch (operation) {
    case Operation::Add: case Operation::Or: case Operation::Xor: case Operation::AndRecord:
    case Operation::AddReg: case Operation::Subf: case Operation::Mullw: case Operation::Mulhw:
    case Operation::Mulhwu: case Operation::Divw: case Operation::Divwu: case Operation::Neg:
    case Operation::Addc: case Operation::Adde: case Operation::Addze: case Operation::Addme:
    case Operation::Subfc: case Operation::Subfze: case Operation::Subfme: case Operation::Subfe:
    case Operation::Addic: case Operation::Subfic: case Operation::Mulli:
    case Operation::AndReg: case Operation::OrReg: case Operation::XorReg: case Operation::Nand:
    case Operation::Nor: case Operation::Eqv: case Operation::Andc: case Operation::Orc:
    case Operation::Extsb: case Operation::Extsh: case Operation::Cntlzw:
    case Operation::Slw: case Operation::Srw: case Operation::Sraw: case Operation::Srawi:
    case Operation::Rlwinm: case Operation::Rlwnm: case Operation::Rlwimi: case Operation::Record:
    case Operation::Cmpw: case Operation::Cmplw: case Operation::Cmpwi: case Operation::Cmplwi:
    case Operation::Mfcr: case Operation::Mtcrf: case Operation::Mcrf: case Operation::Mcrxr:
    case Operation::Mflr: case Operation::Mtlr: case Operation::Mfctr: case Operation::Mtctr:
    case Operation::Mfxer: case Operation::Mtxer:
    case Operation::Jump: case Operation::BranchCrSet: case Operation::BranchCrClear:
    case Operation::BranchCtrNonzero: case Operation::BranchCtrZero:
    case Operation::Crand: case Operation::Cror: case Operation::Crxor: case Operation::Crnand:
    case Operation::Crnor: case Operation::Creqv: case Operation::Crandc: case Operation::Crorc:
    case Operation::Lbz: case Operation::Lhz: case Operation::Lha: case Operation::Lwz:
    case Operation::Lbzu: case Operation::Lhzu: case Operation::Lhau: case Operation::Lwzu:
    case Operation::Lbzx: case Operation::Lhzx: case Operation::Lhax: case Operation::Lwzx:
    case Operation::Lbzux: case Operation::Lhzux: case Operation::Lhaux: case Operation::Lwzux:
    case Operation::Lmw: case Operation::Lwbrx: case Operation::Lhbrx:
    case Operation::Lfs: case Operation::Lfsu: case Operation::Lfsx: case Operation::Lfsux:
    case Operation::Lfd: case Operation::Lfdu: case Operation::Lfdx: case Operation::Lfdux:
    case Operation::Fadd: case Operation::Fsub: case Operation::Fmul: case Operation::Fdiv:
    case Operation::Fmadd: case Operation::Fmsub: case Operation::Fnmadd: case Operation::Fnmsub:
    case Operation::Fadds: case Operation::Fsubs: case Operation::Fmuls: case Operation::Fdivs:
    case Operation::Fmadds: case Operation::Fmsubs: case Operation::Fnmadds: case Operation::Fnmsubs:
    case Operation::Fres: case Operation::Frsqrte: case Operation::Frsp:
    case Operation::Fmr: case Operation::Fneg: case Operation::Fabs: case Operation::Fnabs:
    case Operation::Fsel: case Operation::Fcmp: case Operation::Fctiw: case Operation::Fctiwz:
    case Operation::Mffs: case Operation::Mtfsf: case Operation::Mtfsb0: case Operation::Mtfsb1:
    case Operation::Mtfsfi: case Operation::Mcrfs:
    case Operation::PsAdd: case Operation::PsSub: case Operation::PsMul: case Operation::PsDiv:
    case Operation::PsMuls0: case Operation::PsMuls1:
    case Operation::PsMadd: case Operation::PsMsub: case Operation::PsNmadd: case Operation::PsNmsub:
    case Operation::PsMadds0: case Operation::PsMadds1: case Operation::PsSum0: case Operation::PsSum1:
    case Operation::PsRes: case Operation::PsRsqrte: case Operation::PsSel:
    case Operation::PsMr: case Operation::PsNeg: case Operation::PsAbs: case Operation::PsNabs:
    case Operation::PsMerge00: case Operation::PsMerge01: case Operation::PsMerge10:
    case Operation::PsMerge11: case Operation::FcmpPs1:
      return true;
    default:
      return false;
  }
}
// A return address of a local call (emit.py's info.local_returns) and the stencil the return goes
// to. kNoStencil: the words after that call are inline data, which nothing may return into.
struct LocalReturn { uint32_t address; uint32_t stencil; };
// What the planner may be told about a function this one calls: the deltas K for which the callee
// can return to its caller's LR + K (analyze.py _computed_return_delta; see computed_returns()).
struct Callee { uint32_t address; std::vector<uint32_t> computed_returns; };
struct Plan {
  std::vector<Instruction> stencils;
  std::vector<LocalReturn> local_returns;
  bool has_local_calls = false;
  uint32_t unreachable = 0; // Words no path from the entry reaches: inline data, dead code.
  std::vector<std::pair<uint32_t, uint32_t>> unreachable_runs; // [first, last] word indices of each run
  std::vector<uint32_t> guest_entries; // Runtime mode: guest word -> first stencil, or kNoStencil.
};
// Why the planner refused. The first two are about the whole function.
#define MU_REFUSALS(X) \
  X(InvalidRange, "invalid or oversized function range") \
  X(FallsOffEnd, "the function can run past its last instruction") \
  X(NotDecoded, "no instruction the recompiler decodes and emits") \
  X(NoStencil, "emit.py models it, this prototype has no stencil for it") \
  X(ReservedBits, "reserved field set or noncanonical encoding") \
  X(OverflowEnable, "OE=1: emit.py models no overflow flag") \
  X(FloatRecord, "Rc=1 on a float form: emit.py writes no CR1 update") \
  X(CounterBranch, "bcctr that decrements CTR is an invalid form") \
  X(InvalidUpdate, "update form with RA=0, or a load with RA=RD") \
  X(InvalidMultiple, "lmw with RA in the loaded range") \
  X(SpecialRegister, "special register other than XER, LR and CTR") \
  X(DroppedByEmit, "emit.py drops this instruction (trap): not translated as nothing")
enum class Refusal : uint8_t {
#define MU_REFUSAL_ENUM(name, text) name,
  MU_REFUSALS(MU_REFUSAL_ENUM)
#undef MU_REFUSAL_ENUM
};
#define MU_REFUSAL_COUNT(name, text) +1u
constexpr uint32_t kRefusalCount = 0u MU_REFUSALS(MU_REFUSAL_COUNT);
#undef MU_REFUSAL_COUNT
constexpr const char* refusal_name(Refusal refusal) {
  switch (refusal) {
#define MU_REFUSAL_NAME(name, text) case Refusal::name: return #name;
    MU_REFUSALS(MU_REFUSAL_NAME)
#undef MU_REFUSAL_NAME
  }
  return "?";
}
constexpr const char* refusal_text(Refusal refusal) {
  switch (refusal) {
#define MU_REFUSAL_TEXT(name, text) case Refusal::name: return text;
    MU_REFUSALS(MU_REFUSAL_TEXT)
#undef MU_REFUSAL_TEXT
  }
  return "?";
}
// index is the guest instruction, or the instruction count for a refusal of the whole function.
struct Refused { uint32_t index; Refusal reason; };
// The mask emit.py computes for rlwinm, rlwnm and rlwimi (Emitter._mask).
constexpr uint32_t rotate_mask(uint32_t mb, uint32_t me) {
  const uint32_t begin = 0xFFFFFFFFu >> mb, end = 0x7FFFFFFFu >> me, m = begin ^ end;
  return me < mb ? ~m : m;
}
// analyze.py _computed_return_delta for the non-link bclr at word `index`: a function can end by
// reloading its caller's saved LR off the stack, adding a constant and returning there. Returns
// that constant, or 0 for an ordinary return.
inline uint32_t computed_return_delta(const std::vector<uint32_t>& words, size_t index) {
  int reg = -1;
  int32_t delta = 0;
  for (size_t back = 1; back <= 11 && back <= index; ++back) {
    const uint32_t w = words[index - back], op = w >> 26, rd = (w >> 21) & 31, ra = (w >> 16) & 31;
    const uint32_t xo = (w >> 1) & 0x3FFu;
    if (op == 16 || op == 18 || (op == 19 && (xo == 16 || xo == 528))) return 0; // any branch ends the search
    const bool mtspr = op == 31 && xo == 467;
    if (reg < 0) {
      if (mtspr && (((w >> 11) & 31) | (((w >> 16) & 31) << 5)) == 0x100) reg = int(rd); // mtlr: SPR 8
      continue;
    }
    // gekko.py gives every decoded word an "rd" field; only a word that writes `reg` matters.
    if (mtspr || int(rd) != reg) continue;
    if (op == 14 && int(ra) == reg) { delta += int16_t(w & 0xFFFFu); continue; }
    if (op == 32 && ra == 1) return (delta > 0 && delta % 4 == 0 && delta < 256) ? uint32_t(delta) : 0;
    return 0;
  }
  return 0;
}
// Every computed-return delta of a function's code, as analyze.py collects info.computed_returns.
inline std::vector<uint32_t> computed_returns(const uint8_t* code, size_t bytes) {
  std::vector<uint32_t> words, out;
  for (size_t at = 0; at + 4 <= bytes; at += 4)
    words.push_back((uint32_t(code[at]) << 24) | (uint32_t(code[at+1]) << 16) | (uint32_t(code[at+2]) << 8) | uint32_t(code[at+3]));
  for (size_t i = 0; i < words.size(); ++i) {
    if ((words[i] >> 26) != 19 || ((words[i] >> 1) & 0x3FFu) != 16 || (words[i] & 1)) continue;
    const uint32_t k = computed_return_delta(words, i);
    bool seen = false;
    for (uint32_t have : out) seen = seen || have == k;
    if (k && !seen) out.push_back(k);
  }
  return out;
}
// With `all` the planner does not stop at the first refusal: it records every refused instruction
// (a refused instruction then plans as nothing) and still returns false. tools/ppc_stencils uses
// this to measure coverage; translation never passes it.
//
// Only words a path from the entry reaches are planned. A call to a target inside the function is
// a local call, as analyze.py decides for cave code (a `bl` whose target is one of the function's
// own addresses): LR is set, the return address is remembered for this invocation and control
// jumps. `data_calls` marks, by word index, the local calls whose return address is inline data
// (see plan_function): the words after such a call are not reached by it.
//
// `direct` (runtime mode only, one byte per guest word, or null): nonzero where a branch without
// link may land without asking the driver, because the interpreter would simply continue there
// (the word is not a compiled dispatch target). The caller owns that promise and retires the
// translation when it stops holding. With it, a b or bc to such a word of this function is native
// (the standalone branch stencils, the back-edge poll included) and code guards are kept only
// where something could have changed since the last one. Without it every branch is a
// RuntimeBranch exit and every instruction starts with its guard.
inline bool plan_attempt(const uint8_t* code, size_t bytes, uint32_t address,
                         Plan& result, std::string& error,
                         std::vector<Refused>* all, const std::vector<Callee>* callees,
                         const std::vector<uint8_t>& data_calls, bool runtime = false,
                         const std::vector<uint8_t>* direct = nullptr) {
  result = Plan{};
  error.clear();
  if (all) all->clear();
  constexpr uint32_t ram_base = 0x80000000u, ram_size = 0x01800000u;
  if (!code || !bytes || (bytes & 3) || (address & 3) || bytes > kMaxFunctionBytes ||
      address < ram_base || address - ram_base >= ram_size ||
      bytes > ram_size - (address - ram_base)) {
    error = refusal_text(Refusal::InvalidRange);
    if (all) all->push_back({0, Refusal::InvalidRange});
    return false;
  }
  const uint32_t count = uint32_t(bytes / 4);
  // While planning, a link at or above kGuest names the first stencil of a guest instruction.
  // With kBody as well it names the first stencil after that instruction's code guard.
  constexpr uint32_t kGuest = 0x80000000u, kBody = 0x40000000u;
  std::vector<Instruction> plan;
  std::vector<uint32_t> first(count + 1, kNoStencil);
  // Runtime guard placement (only with `direct`). body: the first stencil after the guard.
  // pure: the instruction is made of runtime_pure stencils and native branch tests only.
  // cold: instructions whose guard no fall-through needs; it is appended after the hot code and
  // serves only as the driver's entry (guest_entries).
  const bool elide = runtime && direct != nullptr;
  std::vector<uint32_t> body(count + 1, kNoStencil), cold;
  std::vector<uint8_t> pure(count, 0);
  uint32_t open = kNoStencil; // The last planned instruction, not yet classified.
  size_t open_body = 0;
  std::vector<uint32_t> words(count);
  for (uint32_t index = 0; index < count; ++index) {
    const size_t at = size_t(index) * 4;
    words[index] = (uint32_t(code[at]) << 24) | (uint32_t(code[at+1]) << 16) |
                   (uint32_t(code[at+2]) << 8) | uint32_t(code[at+3]);
  }
  const auto inside_function = [&](uint32_t target) { return target >= address && target - address < bytes; };
  // The address a b or bc goes to.
  const auto direct_target = [&](uint32_t index) {
    const uint32_t w = words[index], here = address + index * 4;
    uint32_t displacement = 0;
    if ((w >> 26) == 18) { displacement = w & 0x03FFFFFCu; if (displacement & 0x02000000u) displacement |= 0xFC000000u; }
    else { displacement = w & 0xFFFCu; if (displacement & 0x8000u) displacement |= 0xFFFF0000u; }
    return (w & 2) ? displacement : here + displacement;
  };
  // The computed-return deltas of a direct call's callee that land on this function's own words.
  const auto resume_deltas = [&](uint32_t index, uint32_t target) {
    std::vector<uint32_t> out;
    if (!callees) return out;
    for (const Callee& callee : *callees) {
      if (callee.address != target) continue;
      for (uint32_t k : callee.computed_returns)
        if (!(k & 3) && inside_function(address + index * 4 + 4 + k)) out.push_back(k);
    }
    for (size_t i = 1; i < out.size(); ++i) // sorted, as emit.py walks them
      for (size_t j = i; j > 0 && out[j - 1] > out[j]; --j) std::swap(out[j - 1], out[j]);
    return out;
  };
  // Which words a path from the entry reaches, and whether the function makes local calls.
  std::vector<uint8_t> reachable(count, 0), nothing(count, 0);
  bool has_local = false;
  {
    std::vector<uint32_t> work{0};
    reachable[0] = 1;
    const auto reach = [&](uint32_t index) {
      if (index < count && !reachable[index]) { reachable[index] = 1; work.push_back(index); }
    };
    while (!work.empty()) {
      const uint32_t index = work.back();
      work.pop_back();
      const uint32_t w = words[index], op = w >> 26, xo = (w >> 1) & 0x3FFu;
      const bool always = op == 18 || ((w >> 21) & 20) == 20; // BO: no CTR test and no CR test
      if (op == 18 || op == 16) {
        const uint32_t target = direct_target(index);
        if (w & 1) {
          if (inside_function(target)) {
            has_local = true;
            const uint32_t to = (target - address) / 4;
            reach(to);
            if (!(index < data_calls.size() && data_calls[index])) reach(index + 1);
          } else {
            reach(index + 1);
            for (uint32_t k : resume_deltas(index, target)) reach(index + 1 + k / 4);
          }
        } else {
          if (inside_function(target)) reach((target - address) / 4);
          if (!always) reach(index + 1);
        }
      } else if (op == 19 && (xo == 16 || xo == 528)) {
        if ((w & 1) || !always) reach(index + 1); // A call continues; a conditional return falls through.
      } else {
        reach(index + 1);
      }
    }
  }
  result.has_local_calls = has_local;
  std::vector<std::pair<uint32_t, uint32_t>> local_returns; // (return address, guest index)
  uint32_t exits = 0;
  bool refused_any = false;
  plan.reserve(count * 2);
  // Classifies the instruction planned last, once all its stencils are known. A pure one passes
  // control to the stencil after the next instruction's guard, and that guard becomes cold.
  const auto settle = [&] {
    if (!elide || open == kNoStencil) return;
    const uint32_t after = kGuest | (open + 1);
    if (plan.size() == open_body) {
      if (first[open] == kNoStencil) {
        // A cold guard and no stencil of its own (a cache hint): something has to stand here.
        Instruction pass{};
        pass.operation = Operation::Jump; pass.next = after;
        plan.push_back(pass);
        nothing[open] = 0;
      } else {
        body[open] = first[open]; // Only its guard: that is what a native arrival runs.
        if (plan[first[open]].next == after) plan[first[open]].next = after | kBody;
        pure[open] = 1;
        open = kNoStencil;
        return;
      }
    }
    bool clean = true;
    for (size_t i = open_body; i < plan.size(); ++i) clean = clean && runtime_pure(plan[i].operation);
    if (clean) {
      for (size_t i = open_body; i < plan.size(); ++i)
        if (plan[i].next == after) plan[i].next = after | kBody;
      pure[open] = 1;
    }
    open = kNoStencil;
  };
  for (uint32_t index = 0; index < count; ++index) {
    const size_t at = size_t(index) * 4;
    const uint32_t word = words[index];
    if (!reachable[index]) {
      ++result.unreachable;
      if (!result.unreachable_runs.empty() && result.unreachable_runs.back().second + 1 == index) result.unreachable_runs.back().second = index;
      else result.unreachable_runs.push_back({index, index});
      continue;
    }
    settle();
    const size_t planned_before = plan.size();
    struct Nothing { // Marks an instruction that planned no stencil, on every way out of the loop body.
      std::vector<uint8_t>& flags; const std::vector<Instruction>& plan; size_t before; uint32_t index;
      ~Nothing() { flags[index] = plan.size() == before; }
    } mark_nothing{nothing, plan, planned_before, index};
    // False stops the planner; true means "recorded, keep going" (coverage mode).
    const auto refuse = [&](Refusal reason) {
      refused_any = true;
      if (error.empty())
        error = "unsupported opcode at byte " + std::to_string(at) + ": " + refusal_text(reason);
      if (all) { all->push_back({index, reason}); return true; }
      return false;
    };
    first[index] = uint32_t(plan.size());
    const uint32_t op = word >> 26, first_field = (word >> 21) & 31, second = (word >> 16) & 31;
    const uint32_t third = (word >> 11) & 31, fourth = (word >> 6) & 31;
    const bool record = word & 1;
    uint32_t immediate = word & 0xFFFFu;
    const uint32_t simm = (immediate & 0x8000u) ? immediate | 0xFFFF0000u : immediate;
    const uint32_t fall = kGuest | (index + 1);
    const uint32_t here = address + uint32_t(at);
    bool guard_inline = false;
    if (runtime) {
      // The guard stays in line at the entry and wherever the instruction before could have
      // written RAM, changed the dispatch or come back from the driver.
      guard_inline = !elide || index == 0 || !reachable[index - 1] || !pure[index - 1];
      if (guard_inline) {
        Instruction guard{};
        guard.operation = Operation::CodeGuard; guard.immediate = here;
        guard.next = uint32_t(plan.size()) + 1;
        plan.push_back(guard);
      } else {
        first[index] = kNoStencil; // Set when the cold guards are appended.
        cold.push_back(index);
      }
      body[index] = uint32_t(plan.size());
      open = index; open_body = plan.size();
      const uint32_t xo = (word >> 1) & 0x3FF;
      if (op == 16 || op == 18 || (op == 19 && (xo == 16 || xo == 528))) {
        if (op == 19 && (third || (xo == 528 && !(first_field & 4)))) {
          if (!refuse(third ? Refusal::ReservedBits : Refusal::CounterBranch)) return false;
          continue;
        }
        if (elide && op != 19 && !(word & 1)) {
          // b or bc without link. Interp::step: cond() (CTR first, then the CR bit), then for a
          // taken branch ram_branch_poll (a target at or before the branch counts a back-edge)
          // and transfer(), which continues in RAM at a word that is not a compiled target.
          uint32_t displacement = op == 18 ? word & 0x03FFFFFCu : word & 0xFFFCu;
          if (op == 18 ? displacement & 0x02000000u : displacement & 0x8000u)
            displacement |= op == 18 ? 0xFC000000u : 0xFFFF0000u;
          const uint32_t target = (word & 2) ? displacement : here + displacement;
          const uint32_t bo = op == 18 ? 20u : first_field;
          const bool test_ctr = !(bo & 4), test_cr = !(bo & 16);
          const uint32_t to = (target - address) / 4;
          if (target >= address && target - address < bytes && to < direct->size() && (*direct)[to] &&
              reachable[to] && ((!test_ctr && !test_cr) || index + 1 < count)) {
            const uint32_t tests = uint32_t(test_ctr) + uint32_t(test_cr);
            const uint32_t start = uint32_t(plan.size());
            const bool back = to <= index;
            const uint32_t skip = fall | kBody, land = kGuest | kBody | to;
            const uint32_t taken = back ? start + tests : land;
            if (test_ctr) {
              Instruction test{};
              test.operation = (bo & 2) ? Operation::BranchCtrZero : Operation::BranchCtrNonzero;
              test.taken = test_cr ? start + 1 : taken;
              test.next = skip;
              plan.push_back(test);
            }
            if (test_cr) {
              Instruction test{};
              test.operation = (bo & 8) ? Operation::BranchCrSet : Operation::BranchCrClear;
              test.source = second >> 2; test.immediate = 8u >> (second & 3);
              test.taken = taken;
              test.next = skip;
              plan.push_back(test);
            }
            if (back) {
              // ppc::backedge split at its call, as in a standalone plan. The Exit carries the
              // target: after the poll the driver asks where the transfer lands now, and it
              // resumes at the target's guard, because the poll may have run anything.
              Instruction poll{}, leave{};
              poll.operation = Operation::Backedge;
              poll.next = land; poll.taken = uint32_t(plan.size()) + 1;
              plan.push_back(poll);
              leave.operation = Operation::Exit; leave.immediate = ++exits;
              leave.exit = ExitKind::Poll; leave.exit_value = target; leave.taken = kGuest | to;
              plan.push_back(leave);
            } else if (!tests) {
              Instruction jump{};
              jump.operation = Operation::Jump; jump.next = land;
              plan.push_back(jump);
            }
            pure[index] = 1;
            continue;
          }
        }
        Instruction branch{};
        branch.operation = Operation::Exit; branch.immediate = ++exits;
        branch.exit = ExitKind::RuntimeBranch; branch.exit_value = here;
        plan.push_back(branch);
        continue;
      }
      if (op == 31 && xo == 982) { // icbi
        if (first_field || record) { if (!refuse(Refusal::ReservedBits)) return false; continue; }
        Instruction invalidate{};
        invalidate.operation = Operation::InvalidateCode; invalidate.source = second;
        invalidate.source2 = third; invalidate.next = fall;
        plan.push_back(invalidate);
        continue;
      }
    }
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
    // A branch as emit.py writes it: `if (cond_expr) { body }`. cond_expr decrements and tests
    // CTR first unless BO bit 4 is set, then tests the CR bit unless BO bit 16 is set,
    // short-circuit. The body is one of:
    //   Local     `[ppc::backedge(c);] goto L;` (a target at or before the branch is a back-edge)
    //   Return    `return;`
    //   Call      `c.lr = ret; ppc::call(c, m, target);` and execution continues
    //   CallCtr   `{ uint32_t t = c.ctr; c.lr = ret; ppc::call(c, m, t); }`
    //   Tail      `ppc::call(c, m, target); return;`
    //   TailCtr   `ppc::call(c, m, c.ctr); return;`
    //   LocalCall `c.lr = ret; lrs[lrn++ & 31u] = ret; goto L_target;`
    //   LinkReturn the blrl statement (see ExitKind)
    // In a function with local calls every return and tail call also checks the local returns,
    // which is the driver's work, so those leave through an Exit.
    enum class Body { Local, Return, Call, CallCtr, Tail, TailCtr, LocalCall, LinkReturn };
    const auto push_exit = [&](ExitKind kind, uint32_t value, uint32_t resume) {
      Instruction leave{};
      leave.operation = Operation::Exit;
      leave.immediate = ++exits; leave.exit = kind; leave.exit_value = value; leave.taken = resume;
      plan.push_back(leave);
    };
    const auto emit_branch = [&](uint32_t bo, uint32_t bi, Body body, uint32_t target) {
      const bool test_ctr = !(bo & 4), test_cr = !(bo & 16);
      const uint32_t tests = uint32_t(test_ctr) + uint32_t(test_cr);
      const uint32_t base = uint32_t(plan.size());
      const bool back = body == Body::Local && target <= index;
      const uint32_t destination = (body == Body::Local && !back) ? (kGuest | target) : base + tests;
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
      const uint32_t link = here + 4; // The return address a call leaves in LR.
      switch (body) {
        case Body::Return:
          if (has_local) push_exit(ExitKind::LocalReturn, 0, kNoStencil);
          else plan.push_back({Operation::Return});
          break;
        case Body::LocalCall: {
          Instruction set{};
          set.operation = Operation::SetLr;
          set.immediate = link; set.next = uint32_t(plan.size()) + 1;
          plan.push_back(set);
          push_exit(ExitKind::LocalCall, link, kGuest | target);
          local_returns.push_back({link, index + 1});
          break;
        }
        case Body::LinkReturn:
          push_exit(ExitKind::LinkReturn, link, fall);
          break;
        case Body::Local:
          if (back) {
            Instruction poll{}, leave{};
            poll.operation = Operation::Backedge;
            poll.next = kGuest | target; poll.taken = base + tests + 1;
            (void)leave;
            plan.push_back(poll);
            push_exit(ExitKind::Poll, 0, kGuest | target);
          } else if (!tests) {
            Instruction jump{};
            jump.operation = Operation::Jump; jump.next = kGuest | target;
            plan.push_back(jump);
          }
          break;
        case Body::Call: case Body::CallCtr: {
          // A direct call of a callee with computed returns: emit.py counts the check, makes the
          // call and then compares LR with each address the callee may have asked to resume at.
          const std::vector<uint32_t> deltas = body == Body::Call ? resume_deltas(index, target) : std::vector<uint32_t>{};
          Instruction call{};
          call.operation = body == Body::CallCtr ? Operation::CallCtr : deltas.empty() ? Operation::Call : Operation::CallChecked;
          call.immediate = link; call.immediate2 = target;
          call.next = deltas.empty() ? fall : uint32_t(plan.size()) + 1;
          plan.push_back(call);
          for (size_t i = 0; i < deltas.size(); ++i) {
            Instruction test{};
            test.operation = Operation::ResumeTest;
            test.immediate = link + deltas[i];
            test.taken = kGuest | (index + 1 + deltas[i] / 4);
            test.next = i + 1 < deltas.size() ? uint32_t(plan.size()) + 1 : fall;
            plan.push_back(test);
          }
          break;
        }
        case Body::Tail: case Body::TailCtr: {
          if (has_local) {
            push_exit(body == Body::Tail ? ExitKind::TailCall : ExitKind::TailCallCtr, target, kNoStencil);
            break;
          }
          Instruction call{};
          call.operation = body == Body::Tail ? Operation::TailCall : Operation::TailCallCtr;
          call.immediate = target; call.next = uint32_t(plan.size()) + 1;
          plan.push_back(call);
          plan.push_back({Operation::Return});
          break;
        }
      }
    };
    // b and bc. The target is an address; AA makes the displacement absolute.
    const auto emit_direct = [&](uint32_t bo, uint32_t bi, uint32_t displacement) {
      const uint32_t target = (word & 2) ? displacement : here + displacement;
      const bool inside = target >= address && target - address < bytes;
      if (word & 1) { // LK
        if (inside) emit_branch(bo, bi, Body::LocalCall, (target - address) / 4);
        else emit_branch(bo, bi, Body::Call, target);
      } else if (inside) {
        emit_branch(bo, bi, Body::Local, (target - address) / 4);
      } else {
        emit_branch(bo, bi, Body::Tail, target);
      }
      return true;
    };
    // Loads, stores and their update forms. Integer loads may not update the register they load.
    const auto emit_memory = [&](Operation operation, bool update, bool integer_load, bool indexed) {
      if (update && (second == 0 || (integer_load && second == first_field))) return refuse(Refusal::InvalidUpdate);
      emit(operation, first_field, second, indexed ? 0 : simm, indexed ? third : 0, 0, false);
      return true;
    };
    if (op == 18) { // b
      const uint32_t li = word & 0x03FFFFFCu;
      if (!emit_direct(20, 0, (li & 0x02000000u) ? li | 0xFC000000u : li)) return false;
      continue;
    }
    if (op == 16) { // bc
      const uint32_t bd = word & 0xFFFCu;
      if (!emit_direct(first_field, second, (bd & 0x8000u) ? bd | 0xFFFF0000u : bd)) return false;
      continue;
    }
    if (op == 19 && ((word >> 1) & 0x3FFu) == 16) { // bclr
      if (third) { if (!refuse(Refusal::ReservedBits)) return false; continue; }
      emit_branch(first_field, second, record ? Body::LinkReturn : Body::Return, 0);
      continue;
    }
    if (op == 19 && ((word >> 1) & 0x3FFu) == 528) { // bcctr
      if (third) { if (!refuse(Refusal::ReservedBits)) return false; continue; }
      if (!(first_field & 4)) { if (!refuse(Refusal::CounterBranch)) return false; continue; }
      emit_branch(first_field, second, record ? Body::CallCtr : Body::TailCtr, 0);
      continue;
    }
    Refusal reason = Refusal::NotDecoded;
    bool accepted = true;
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
    } else if (op >= 32 && op <= 47) { // D-form integer loads and stores; odd opcodes update.
      static constexpr Operation kPlain[] = {Operation::Lwz, Operation::Lbz, Operation::Stw, Operation::Stb,
                                             Operation::Lhz, Operation::Lha, Operation::Sth};
      static constexpr Operation kUpdate[] = {Operation::Lwzu, Operation::Lbzu, Operation::Stwu, Operation::Stbu,
                                              Operation::Lhzu, Operation::Lhau, Operation::Sthu};
      if (op == 46) { // lmw: RA inside the loaded range is an invalid form.
        if (second >= first_field) { accepted = false; reason = Refusal::InvalidMultiple; }
        else emit(Operation::Lmw, first_field, second, simm, 0, 0, false);
      } else if (op == 47) {
        emit(Operation::Stmw, first_field, second, simm, 0, 0, false);
      } else {
        const uint32_t row = (op - 32) / 2;
        const bool load = row == 0 || row == 1 || row == 4 || row == 5;
        if (!emit_memory((op & 1) ? kUpdate[row] : kPlain[row], op & 1, load, false)) return false;
      }
    } else if (op >= 48 && op <= 55) { // lfs, lfsu, lfd, lfdu, stfs, stfsu, stfd, stfdu
      static constexpr Operation kFloat[] = {Operation::Lfs, Operation::Lfsu, Operation::Lfd, Operation::Lfdu,
                                             Operation::Stfs, Operation::Stfsu, Operation::Stfd, Operation::Stfdu};
      if (!emit_memory(kFloat[op - 48], op & 1, false, false)) return false;
    } else if (op == 56 || op == 57 || op == 60 || op == 61) { // psq_l, psq_lu, psq_st, psq_stu
      const uint32_t d12 = word & 0xFFFu, w = (word >> 15) & 1, i = (word >> 12) & 7;
      const bool update = op & 1;
      if (update && second == 0) { accepted = false; reason = Refusal::InvalidUpdate; }
      else emit(op == 56 ? Operation::PsqL : op == 57 ? Operation::PsqLu : op == 60 ? Operation::PsqSt : Operation::PsqStu,
                first_field, second, (d12 & 0x800u) ? d12 | 0xFFFFF000u : d12, 0, w | (i << 1), false);
    } else if (op == 31) {
      const uint32_t xo = (word >> 1) & 0x3FFu;
      // Arithmetic forms write RD = first from RA = second and RB = third. emit.py models no
      // overflow flag, so an OE=1 word (xo + 512) is refused.
      Operation operation{};
      bool arithmetic = true, unary = false;
      switch (xo & 0x1FFu) {
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
        case 136: operation = Operation::Subfe; break;
        case 104: operation = Operation::Neg; unary = true; break;
        case 202: operation = Operation::Addze; unary = true; break;
        case 234: operation = Operation::Addme; unary = true; break;
        case 200: operation = Operation::Subfze; unary = true; break;
        case 232: operation = Operation::Subfme; unary = true; break;
        default: arithmetic = false; break;
      }
      if (arithmetic) {
        if (xo & 0x200u) { if (!refuse(Refusal::OverflowEnable)) return false; continue; }
        if (unary && third) { if (!refuse(Refusal::ReservedBits)) return false; continue; }
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
      // Indexed loads and stores: RD or RS = first, RA = second, RB = third. No Rc.
      struct Indexed { uint32_t xo; Operation operation; bool update, integer_load; };
      static constexpr Indexed kIndexed[] = {
        {23, Operation::Lwzx, false, true}, {55, Operation::Lwzux, true, true},
        {87, Operation::Lbzx, false, true}, {119, Operation::Lbzux, true, true},
        {279, Operation::Lhzx, false, true}, {311, Operation::Lhzux, true, true},
        {343, Operation::Lhax, false, true}, {375, Operation::Lhaux, true, true},
        {151, Operation::Stwx, false, false}, {183, Operation::Stwux, true, false},
        {215, Operation::Stbx, false, false}, {247, Operation::Stbux, true, false},
        {407, Operation::Sthx, false, false}, {439, Operation::Sthux, true, false},
        {534, Operation::Lwbrx, false, true}, {790, Operation::Lhbrx, false, true},
        {662, Operation::Stwbrx, false, false}, {918, Operation::Sthbrx, false, false},
        {535, Operation::Lfsx, false, false}, {567, Operation::Lfsux, true, false},
        {599, Operation::Lfdx, false, false}, {631, Operation::Lfdux, true, false},
        {663, Operation::Stfsx, false, false}, {695, Operation::Stfsux, true, false},
        {727, Operation::Stfdx, false, false}, {759, Operation::Stfdux, true, false},
        {983, Operation::Stfiwx, false, false},
      };
      const Indexed* indexed = nullptr;
      for (const auto& row : kIndexed) if (row.xo == xo) indexed = &row;
      if (logical) {
        if (unary && third) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(operation, second, first_field, 0, unary ? 0 : third, 0, record);
      } else if (indexed) {
        if (record) { accepted = false; reason = Refusal::ReservedBits; }
        else if (!emit_memory(indexed->operation, indexed->update, indexed->integer_load, true)) return false;
      } else if (xo == 824) {
        emit(Operation::Srawi, second, first_field, third, 0, 0, record);
      } else if (xo == 0 || xo == 32) { // cmpw, cmplw: no Rc, L=0 and the reserved bit clear.
        if (record || (first_field & 3)) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(xo ? Operation::Cmplw : Operation::Cmpw, first_field >> 2, second, 0, third, 0, false);
      } else if (xo == 19) { // mfcr
        if (record || second || third) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(Operation::Mfcr, first_field, 0, 0, 0, 0, false);
      } else if (xo == 144) { // mtcrf: the bits around CRM are reserved (bit 11 set is mtocrf).
        if (record || (word & 0x00100800u)) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(Operation::Mtcrf, 0, first_field, (word >> 12) & 0xFFu, 0, 0, false);
      } else if (xo == 512) { // mcrxr
        if (record || (first_field & 3) || second || third) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(Operation::Mcrxr, first_field >> 2, 0, 0, 0, 0, false);
      } else if (xo == 339 || xo == 467) { // mfspr, mtspr: XER, LR and CTR only.
        const uint32_t spr = (third << 5) | second;
        if (record) { accepted = false; reason = Refusal::ReservedBits; }
        else if (spr != 1 && spr != 8 && spr != 9) { accepted = false; reason = Refusal::SpecialRegister; }
        else if (xo == 339) {
          emit(spr == 1 ? Operation::Mfxer : spr == 8 ? Operation::Mflr : Operation::Mfctr,
               first_field, 0, 0, 0, 0, false);
        } else {
          emit(spr == 1 ? Operation::Mtxer : spr == 8 ? Operation::Mtlr : Operation::Mtctr,
               0, first_field, 0, 0, 0, false);
        }
      } else if (xo == 598 || xo == 854) { // sync, eieio: emit.py writes nothing.
        if (record || first_field || second || third) { accepted = false; reason = Refusal::ReservedBits; }
      } else if (xo == 54 || xo == 86 || xo == 246 || xo == 278 || xo == 470 || xo == 982) {
        // dcbst, dcbf, dcbtst, dcbt, dcbi, icbi: emit.py writes nothing.
        if (record || first_field) { accepted = false; reason = Refusal::ReservedBits; }
      } else if (xo == 4) { // tw
        accepted = false; reason = Refusal::DroppedByEmit;
      } else if (xo == 20 || xo == 150 || xo == 83 || xo == 146 || xo == 371 || xo == 595 || xo == 659 ||
                 xo == 210 || xo == 242 || xo == 306 || xo == 566 || xo == 597 || xo == 725 ||
                 xo == 533 || xo == 661 || xo == 1014) {
        // lwarx, stwcx., mfmsr, mtmsr, mftb, mfsr, mfsrin, mtsr, mtsrin, tlbie, tlbsync, lswi,
        // stswi, lswx, stswx, dcbz.
        accepted = false; reason = Refusal::NoStencil;
      } else {
        accepted = false; reason = Refusal::NotDecoded;
      }
    } else if (op == 10 || op == 11) { // cmplwi, cmpwi: L=0 and the reserved bit clear.
      if (first_field & 3) { accepted = false; reason = Refusal::ReservedBits; }
      else emit(op == 11 ? Operation::Cmpwi : Operation::Cmplwi, first_field >> 2, second,
                op == 11 ? simm : immediate, 0, 0, false);
    } else if (op == 19) {
      const uint32_t xo = (word >> 1) & 0x3FFu;
      Operation operation{};
      bool logical = true;
      switch (xo) { // CR logical forms: crbD = first, crbA = second, crbB = third.
        case 257: operation = Operation::Crand; break;
        case 449: operation = Operation::Cror; break;
        case 193: operation = Operation::Crxor; break;
        case 225: operation = Operation::Crnand; break;
        case 33: operation = Operation::Crnor; break;
        case 289: operation = Operation::Creqv; break;
        case 129: operation = Operation::Crandc; break;
        case 417: operation = Operation::Crorc; break;
        default: logical = false; break;
      }
      if (logical) {
        if (record) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(operation, first_field, second, 0, third, 0, false);
      } else if (xo == 0) { // mcrf
        if (record || (first_field & 3) || (second & 3) || third) { accepted = false; reason = Refusal::ReservedBits; }
        else emit(Operation::Mcrf, first_field >> 2, second >> 2, 0, 0, 0, false);
      } else if (xo == 150) { // isync: emit.py writes nothing.
        if (record || first_field || second || third) { accepted = false; reason = Refusal::ReservedBits; }
      } else if (xo == 50) { // rfi
        accepted = false; reason = Refusal::NoStencil;
      } else {
        accepted = false; reason = Refusal::NotDecoded;
      }
    } else if (op == 59 || op == 63) {
      // Float register forms: fD = first, fA = second, fB = third, fC = fourth. emit.py ignores
      // Rc, so a record form is refused. A-form words are decoded by their five-bit opcode.
      const uint32_t xo5 = (word >> 1) & 31, xo = (word >> 1) & 0x3FFu;
      const bool single = op == 59;
      Operation operation{};
      enum class Shape { None, AB, AC, ACB, B, Select } shape = Shape::None;
      switch (xo5) {
        case 21: operation = single ? Operation::Fadds : Operation::Fadd; shape = Shape::AB; break;
        case 20: operation = single ? Operation::Fsubs : Operation::Fsub; shape = Shape::AB; break;
        case 18: operation = single ? Operation::Fdivs : Operation::Fdiv; shape = Shape::AB; break;
        case 25: operation = single ? Operation::Fmuls : Operation::Fmul; shape = Shape::AC; break;
        case 29: operation = single ? Operation::Fmadds : Operation::Fmadd; shape = Shape::ACB; break;
        case 28: operation = single ? Operation::Fmsubs : Operation::Fmsub; shape = Shape::ACB; break;
        case 31: operation = single ? Operation::Fnmadds : Operation::Fnmadd; shape = Shape::ACB; break;
        case 30: operation = single ? Operation::Fnmsubs : Operation::Fnmsub; shape = Shape::ACB; break;
        case 24: if (single) { operation = Operation::Fres; shape = Shape::B; } break;
        case 26: if (!single) { operation = Operation::Frsqrte; shape = Shape::B; } break;
        case 23: if (!single) { operation = Operation::Fsel; shape = Shape::Select; } break;
        default: break;
      }
      if (shape != Shape::None) {
        const bool reserved = (shape == Shape::AB && fourth) || (shape == Shape::AC && third) ||
                              (shape == Shape::B && (second || fourth));
        if (reserved) { accepted = false; reason = Refusal::ReservedBits; }
        else if (record) { accepted = false; reason = Refusal::FloatRecord; }
        else emit(operation, first_field, second, fourth, third, 0, false);
      } else if (single) {
        accepted = false; reason = Refusal::NotDecoded;
      } else if (xo5 == 22) { // fsqrt: gekko.py decodes it, emit.py has no statement for it.
        accepted = false; reason = Refusal::NotDecoded;
      } else {
        bool unary = true;
        switch (xo) { // fD = first, fB = third; the fA field is reserved.
          case 12: operation = Operation::Frsp; break;
          case 14: operation = Operation::Fctiw; break;
          case 15: operation = Operation::Fctiwz; break;
          case 40: operation = Operation::Fneg; break;
          case 72: operation = Operation::Fmr; break;
          case 136: operation = Operation::Fnabs; break;
          case 264: operation = Operation::Fabs; break;
          default: unary = false; break;
        }
        if (unary) {
          if (second) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(operation, first_field, 0, 0, third, 0, false);
        } else if (xo == 0 || xo == 32) { // fcmpu, fcmpo
          if (record || (first_field & 3)) { accepted = false; reason = Refusal::ReservedBits; }
          else emit(Operation::Fcmp, first_field >> 2, second, 0, third, 0, false);
        } else if (xo == 583) { // mffs
          if (second || third) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(Operation::Mffs, first_field, 0, 0, 0, 0, false);
        } else if (xo == 711) { // mtfsf: FM is bits 7..14; the bits beside it are reserved.
          const uint32_t fm = (word >> 17) & 0xFFu;
          uint32_t mask = 0;
          for (uint32_t i = 0; i < 8; ++i) if (fm & (0x80u >> i)) mask |= 0xFu << (28 - 4 * i);
          if (word & 0x02010000u) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(Operation::Mtfsf, 0, 0, ~mask, third, mask, false);
        } else if (xo == 70 || xo == 38) { // mtfsb0, mtfsb1
          if (second || third) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(xo == 70 ? Operation::Mtfsb0 : Operation::Mtfsb1, 0, 0, 0x80000000u >> first_field, 0, 0, false);
        } else if (xo == 134) { // mtfsfi
          const uint32_t shift = 28 - 4 * (first_field >> 2);
          if ((first_field & 3) || second || (third & 1)) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(Operation::Mtfsfi, 0, 0, 0xFu << shift, 0, (third >> 1) << shift, false);
        } else if (xo == 64) { // mcrfs
          if (record || (first_field & 3) || (second & 3) || third) { accepted = false; reason = Refusal::ReservedBits; }
          else emit(Operation::Mcrfs, first_field >> 2, 0, 28 - 4 * (second >> 2), 0, 0, false);
        } else {
          accepted = false; reason = Refusal::NotDecoded;
        }
      }
    } else if (op == 4) {
      // Paired singles. gekko.py tries the six-bit indexed quantized forms first, then the
      // five-bit forms, then the ten-bit ones.
      const uint32_t xo6 = (word >> 1) & 63, xo5 = (word >> 1) & 31, xo = (word >> 1) & 0x3FFu;
      Operation operation{};
      if (xo6 == 6 || xo6 == 7 || xo6 == 38 || xo6 == 39) { // psq_lx, psq_stx, psq_lux, psq_stux
        const uint32_t w = (word >> 10) & 1, i = (word >> 7) & 7;
        const bool update = xo6 >= 38;
        if (record) { accepted = false; reason = Refusal::ReservedBits; }
        else if (update && second == 0) { accepted = false; reason = Refusal::InvalidUpdate; }
        else emit(xo6 == 6 ? Operation::PsqLx : xo6 == 7 ? Operation::PsqStx : xo6 == 38 ? Operation::PsqLux : Operation::PsqStux,
                  first_field, second, 0, third, w | (i << 1), false);
      } else {
        enum class Shape { None, AB, AC, ACB, B } shape = Shape::None;
        switch (xo5) {
          case 21: operation = Operation::PsAdd; shape = Shape::AB; break;
          case 20: operation = Operation::PsSub; shape = Shape::AB; break;
          case 18: operation = Operation::PsDiv; shape = Shape::AB; break;
          case 25: operation = Operation::PsMul; shape = Shape::AC; break;
          case 12: operation = Operation::PsMuls0; shape = Shape::AC; break;
          case 13: operation = Operation::PsMuls1; shape = Shape::AC; break;
          case 29: operation = Operation::PsMadd; shape = Shape::ACB; break;
          case 28: operation = Operation::PsMsub; shape = Shape::ACB; break;
          case 31: operation = Operation::PsNmadd; shape = Shape::ACB; break;
          case 30: operation = Operation::PsNmsub; shape = Shape::ACB; break;
          case 14: operation = Operation::PsMadds0; shape = Shape::ACB; break;
          case 15: operation = Operation::PsMadds1; shape = Shape::ACB; break;
          case 10: operation = Operation::PsSum0; shape = Shape::ACB; break;
          case 11: operation = Operation::PsSum1; shape = Shape::ACB; break;
          case 23: operation = Operation::PsSel; shape = Shape::ACB; break;
          case 24: operation = Operation::PsRes; shape = Shape::B; break;
          case 26: operation = Operation::PsRsqrte; shape = Shape::B; break;
          default: break;
        }
        if (shape != Shape::None) {
          const bool reserved = (shape == Shape::AB && fourth) || (shape == Shape::AC && third) ||
                                (shape == Shape::B && (second || fourth));
          if (reserved) { accepted = false; reason = Refusal::ReservedBits; }
          else if (record) { accepted = false; reason = Refusal::FloatRecord; }
          else emit(operation, first_field, second, fourth, third, 0, false);
        } else {
          enum class Ten { None, Unary, Merge, Compare } ten = Ten::None;
          switch (xo) {
            case 40: operation = Operation::PsNeg; ten = Ten::Unary; break;
            case 72: operation = Operation::PsMr; ten = Ten::Unary; break;
            case 136: operation = Operation::PsNabs; ten = Ten::Unary; break;
            case 264: operation = Operation::PsAbs; ten = Ten::Unary; break;
            case 528: operation = Operation::PsMerge00; ten = Ten::Merge; break;
            case 560: operation = Operation::PsMerge01; ten = Ten::Merge; break;
            case 592: operation = Operation::PsMerge10; ten = Ten::Merge; break;
            case 624: operation = Operation::PsMerge11; ten = Ten::Merge; break;
            case 0: case 32: operation = Operation::Fcmp; ten = Ten::Compare; break;       // ps_cmpu0, ps_cmpo0
            case 64: case 96: operation = Operation::FcmpPs1; ten = Ten::Compare; break;   // ps_cmpu1, ps_cmpo1
            default: break;
          }
          if (ten == Ten::Compare) {
            if (record || (first_field & 3)) { accepted = false; reason = Refusal::ReservedBits; }
            else emit(operation, first_field >> 2, second, 0, third, 0, false);
          } else if (ten != Ten::None) {
            if (ten == Ten::Unary && second) { accepted = false; reason = Refusal::ReservedBits; }
            else if (record) { accepted = false; reason = Refusal::FloatRecord; }
            else emit(operation, first_field, ten == Ten::Unary ? 0 : second, 0, third, 0, false);
          } else if (xo == 1014) { // dcbz_l
            accepted = false; reason = Refusal::NoStencil;
          } else {
            accepted = false; reason = Refusal::NotDecoded;
          }
        }
      }
    } else if (op == 3) { // twi
      accepted = false; reason = Refusal::DroppedByEmit;
    } else if (op == 17) { // sc
      accepted = false; reason = Refusal::NoStencil;
    } else {
      accepted = false; reason = Refusal::NotDecoded;
    }
    if (!accepted && !refuse(reason)) return false;
    // Runtime cache hints still need to reach the next guard.
    if (runtime && guard_inline && plan.size() == planned_before + 1) plan.back().next = fall;
  }
  settle();
  // The guards no fall-through runs, after all the hot code. A refused plan is discarded anyway.
  if (elide && !refused_any) {
    for (uint32_t index : cold) {
      Instruction guard{};
      guard.operation = Operation::CodeGuard; guard.immediate = address + index * 4;
      guard.next = body[index];
      first[index] = uint32_t(plan.size());
      plan.push_back(guard);
    }
  }
  // The stencil a guest instruction starts with. An instruction that plans as nothing (sync, a
  // cache hint) stands for the one after it; past the last word, or into a word that was not
  // planned, there is nothing. `inner` asks for the stencil after the instruction's runtime guard.
  const auto resolve = [&](uint32_t index, bool inner = false) {
    while (index < count && reachable[index] && nothing[index]) ++index;
    if (index >= count || !reachable[index]) return kNoStencil;
    return inner ? body[index] : first[index];
  };
  bool falls = false;
  for (auto& instruction : plan) {
    for (uint32_t* link : {&instruction.next, &instruction.taken}) {
      if (*link == kNoStencil || *link < kGuest) continue;
      *link = resolve(*link & ~(kGuest | kBody), (*link & kBody) != 0);
      if (*link == kNoStencil) falls = true;
    }
  }
  if (plan.empty()) falls = true;
  for (const auto& item : local_returns) {
    // Reached only by returning: code when it was planned, inline data when it was not.
    const uint32_t stencil = item.second < count && reachable[item.second] ? resolve(item.second) : kNoStencil;
    if (item.second >= count || (reachable[item.second] && stencil == kNoStencil)) falls = true;
    result.local_returns.push_back({item.first, stencil});
  }
  if (falls && !refused_any) {
    error = refusal_text(Refusal::FallsOffEnd);
    if (all) all->push_back({count, Refusal::FallsOffEnd});
  }
  if (falls || refused_any) { result.local_returns.clear(); return false; }
  result.stencils = std::move(plan);
  if (runtime) {
    result.guest_entries.resize(count, kNoStencil);
    for (uint32_t i = 0; i < count; ++i)
      if (reachable[i]) result.guest_entries[i] = resolve(i);
  }
  return true;
}
// The whole plan of a function, or the reason it is refused.
//
// Inline data. Mod code keeps data between its instructions and reaches it with an unconditional
// `bl` over the data to an mflr, which then holds the guest address of the data. The same shape is
// also an ordinary call of a local subroutine that saves LR first. The two are told apart by what
// the words after the `bl` are: they are first planned as the code a subroutine would return to,
// and only when one of the words between the `bl` and its target cannot be planned are they taken
// as data, never planned as instructions, and a return into them stops the translation
// (CompiledLeaf::run). Data that happens to be all plannable instructions is planned as the
// instructions emit.py would write for it, which nothing executes either way.
inline bool plan_function(const uint8_t* code, size_t bytes, uint32_t address,
                          Plan& result, std::string& error,
                          std::vector<Refused>* all = nullptr, const std::vector<Callee>* callees = nullptr,
                          bool runtime = false, const std::vector<uint8_t>* direct = nullptr) {
  std::vector<uint8_t> data_calls(bytes / 4 + 1, 0);
  std::vector<Refused> refused;
  for (;;) {
    if (plan_attempt(code, bytes, address, result, error, &refused, callees, data_calls, runtime, direct)) {
      if (all) all->clear();
      return true;
    }
    bool grew = false;
    const uint32_t count = uint32_t(bytes / 4);
    const auto word_at = [&](uint32_t index) {
      const size_t at = size_t(index) * 4;
      return (uint32_t(code[at]) << 24) | (uint32_t(code[at+1]) << 16) | (uint32_t(code[at+2]) << 8) | uint32_t(code[at+3]);
    };
    for (const Refused& item : refused) {
      if (item.reason == Refusal::InvalidRange || item.reason == Refusal::FallsOffEnd || item.index >= count) continue;
      // The nearest unconditional relative `bl` before the refused word that jumps over it to an mflr.
      for (uint32_t index = item.index, scanned = 0; index-- > 0 && scanned < 2048; ++scanned) {
        const uint32_t w = word_at(index);
        const bool bl = (w >> 26) == 18 && (w & 3) == 1;
        const bool bcl = (w >> 26) == 16 && (w & 3) == 1 && ((w >> 21) & 20) == 20;
        if (!bl && !bcl) continue;
        uint32_t displacement = bl ? (w & 0x03FFFFFCu) : (w & 0xFFFCu);
        if (bl ? (displacement & 0x02000000u) : (displacement & 0x8000u)) continue; // backward
        const uint32_t to = index + displacement / 4;
        if (to <= item.index || to >= count) continue;
        if ((word_at(to) & 0xFC1FFFFFu) != 0x7C0802A6u) continue; // mfspr rX, LR
        if (!data_calls[index]) { data_calls[index] = 1; grew = true; }
        break;
      }
    }
    if (!grew) {
      if (all) *all = refused;
      return false;
    }
  }
}
// The stencil list alone, for callers that do not run the plan.
inline bool plan_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                      std::vector<Instruction>& output, std::string& error,
                      std::vector<Refused>* all = nullptr) {
  Plan plan;
  const bool ok = plan_function(code, bytes, address, plan, error, all);
  output = std::move(plan.stencils);
  return ok;
}
} // namespace ppc::stencil
