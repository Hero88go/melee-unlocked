// Every reason the planner can give for refusing a function, each reached by at least one word,
// with the reason checked, and nothing planned or translated when it refuses.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "leaf_translation_plan.h"
#include "ppc_leaf_stencils.generated.h"

namespace {
using namespace reference;
using ppc::stencil::Refusal;
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
constexpr uint32_t kNop = 0x60000000u;
struct Case { Refusal reason; std::vector<uint32_t> words; const char* what; };
} // namespace

int main() {
  const std::vector<Case> cases = {
    // ---- no instruction the recompiler decodes and emits ----
    {Refusal::NotDecoded, {0x00000000u, kBlr}, "primary opcode 0"},
    {Refusal::NotDecoded, {1u << 26, kBlr}, "primary opcode 1"},
    {Refusal::NotDecoded, {2u << 26, kBlr}, "primary opcode 2 (tdi)"},
    {Refusal::NotDecoded, {5u << 26, kBlr}, "primary opcode 5"},
    {Refusal::NotDecoded, {6u << 26, kBlr}, "primary opcode 6"},
    {Refusal::NotDecoded, {9u << 26, kBlr}, "primary opcode 9"},
    {Refusal::NotDecoded, {22u << 26, kBlr}, "primary opcode 22"},
    {Refusal::NotDecoded, {30u << 26, kBlr}, "primary opcode 30 (64-bit rotates)"},
    {Refusal::NotDecoded, {58u << 26, kBlr}, "primary opcode 58 (ld)"},
    {Refusal::NotDecoded, {62u << 26, kBlr}, "primary opcode 62 (std)"},
    {Refusal::NotDecoded, {xform(3, 4, 5, 9), kBlr}, "op 31 with no such extended opcode"},
    {Refusal::NotDecoded, {xlform(3, 4, 5, 1), kBlr}, "op 19 with no such extended opcode"},
    {Refusal::NotDecoded, {aform(59, 3, 4, 5, 0, 22), kBlr}, "op 59 with no such extended opcode"},
    {Refusal::NotDecoded, {aform(59, 3, 0, 5, 0, 26), kBlr}, "frsqrtes: not on this CPU"},
    {Refusal::NotDecoded, {aform(63, 3, 0, 5, 0, 22), kBlr}, "fsqrt: emit.py has no statement for it"},
    {Refusal::NotDecoded, {aform(63, 3, 0, 5, 0, 24), kBlr}, "fre: not on this CPU"},
    {Refusal::NotDecoded, {xoform(63, 3, 0, 5, 846), kBlr}, "fcfid"},
    {Refusal::NotDecoded, {xoform(4, 3, 4, 5, 100), kBlr}, "op 4 with no such extended opcode"},
    // ---- emit.py models it, the prototype has no stencil ----
    {Refusal::NoStencil, {0x44000002u, kBlr}, "sc"},
    {Refusal::NoStencil, {xlform(0, 0, 0, 50), kBlr}, "rfi"},
    {Refusal::NoStencil, {xform(3, 4, 5, 20), kBlr}, "lwarx"},
    {Refusal::NoStencil, {xform(3, 4, 5, 150, 1), kBlr}, "stwcx."},
    {Refusal::NoStencil, {xform(3, 0, 0, 83), kBlr}, "mfmsr"},
    {Refusal::NoStencil, {xform(3, 0, 0, 146), kBlr}, "mtmsr"},
    {Refusal::NoStencil, {(31u << 26) | (3u << 21) | (spr_field(268) << 11) | (371u << 1), kBlr}, "mftb"},
    {Refusal::NoStencil, {xform(3, 0, 0, 595), kBlr}, "mfsr"},
    {Refusal::NoStencil, {xform(3, 0, 5, 659), kBlr}, "mfsrin"},
    {Refusal::NoStencil, {xform(3, 0, 0, 210), kBlr}, "mtsr"},
    {Refusal::NoStencil, {xform(3, 0, 5, 242), kBlr}, "mtsrin"},
    {Refusal::NoStencil, {xform(0, 0, 5, 306), kBlr}, "tlbie"},
    {Refusal::NoStencil, {xform(0, 0, 0, 566), kBlr}, "tlbsync"},
    {Refusal::NoStencil, {xform(3, 4, 8, 597), kBlr}, "lswi"},
    {Refusal::NoStencil, {xform(3, 4, 8, 725), kBlr}, "stswi"},
    {Refusal::NoStencil, {xform(3, 4, 5, 533), kBlr}, "lswx"},
    {Refusal::NoStencil, {xform(3, 4, 5, 661), kBlr}, "stswx"},
    {Refusal::NoStencil, {xform(0, 4, 5, 1014), kBlr}, "dcbz"},
    {Refusal::NoStencil, {xoform(4, 0, 4, 5, 1014), kBlr}, "dcbz_l"},
    // ---- reserved fields, noncanonical encodings ----
    {Refusal::ReservedBits, {xform(3, 4, 5, 104), kBlr}, "neg with RB"},
    {Refusal::ReservedBits, {xform(3, 4, 5, 202), kBlr}, "addze with RB"},
    {Refusal::ReservedBits, {xform(4, 3, 5, 954), kBlr}, "extsb with RB"},
    {Refusal::ReservedBits, {xform(4, 3, 5, 26), kBlr}, "cntlzw with RB"},
    {Refusal::ReservedBits, {dform(11, (7 << 2) | 1, 4, 0), kBlr}, "cmpwi with L=1"},
    {Refusal::ReservedBits, {dform(10, (7 << 2) | 2, 4, 0), kBlr}, "cmplwi with the reserved bit"},
    {Refusal::ReservedBits, {xform((7 << 2) | 1, 4, 5, 0), kBlr}, "cmpw with L=1"},
    {Refusal::ReservedBits, {xform(7 << 2, 4, 5, 0, 1), kBlr}, "cmpw with Rc"},
    {Refusal::ReservedBits, {xform(3, 1, 0, 19), kBlr}, "mfcr with a reserved field"},
    {Refusal::ReservedBits, {mtcrf(0xFF, 3) | (1u << 20), kBlr}, "mtocrf"},
    {Refusal::ReservedBits, {xlform(1, 4, 0, 0), kBlr}, "mcrf with reserved bits"},
    {Refusal::ReservedBits, {xlform(1 << 2, 2 << 2, 0, 0) | 1, kBlr}, "mcrf with LK"},
    {Refusal::ReservedBits, {xlform(3, 4, 5, 257) | 1, kBlr}, "crand with LK"},
    {Refusal::ReservedBits, {xform(1, 0, 0, 512), kBlr}, "mcrxr with reserved bits"},
    {Refusal::ReservedBits, {mfspr(3, 8) | 1, kBlr}, "mflr with Rc"},
    {Refusal::ReservedBits, {xform(0, 0, 0, 598) | 1, kBlr}, "sync with Rc"},
    {Refusal::ReservedBits, {xform(1, 0, 0, 598), kBlr}, "sync with a field"},
    {Refusal::ReservedBits, {xform(1, 4, 5, 86), kBlr}, "dcbf with a reserved field"},
    {Refusal::ReservedBits, {xlform(0, 1, 0, 150), kBlr}, "isync with a field"},
    {Refusal::ReservedBits, {xform(3, 4, 5, 23, 1), kBlr}, "lwzx with Rc"},
    {Refusal::ReservedBits, {xform(3, 4, 5, 151, 1), kBlr}, "stwx with Rc"},
    {Refusal::ReservedBits, {bclr(20, 0) | (1u << 11), kBlr}, "bclr with a reserved field"},
    {Refusal::ReservedBits, {bcctr(20, 0) | (1u << 11), kBlr}, "bcctr with a reserved field"},
    {Refusal::ReservedBits, {aform(63, 3, 4, 5, 6, 21), kBlr}, "fadd with fC"},
    {Refusal::ReservedBits, {aform(63, 3, 4, 5, 6, 25), kBlr}, "fmul with fB"},
    {Refusal::ReservedBits, {aform(59, 3, 4, 5, 0, 24), kBlr}, "fres with fA"},
    {Refusal::ReservedBits, {xoform(63, 3, 4, 5, 72), kBlr}, "fmr with fA"},
    {Refusal::ReservedBits, {xoform(63, 1, 4, 5, 0), kBlr}, "fcmpu with reserved bits"},
    {Refusal::ReservedBits, {xoform(63, 4, 4, 5, 32, 1), kBlr}, "fcmpo with Rc"},
    {Refusal::ReservedBits, {xoform(63, 3, 1, 0, 583), kBlr}, "mffs with a field"},
    {Refusal::ReservedBits, {(63u << 26) | (1u << 25) | (0xFFu << 17) | (2u << 11) | (711u << 1), kBlr}, "mtfsf with L"},
    {Refusal::ReservedBits, {xoform(63, 3, 1, 0, 70), kBlr}, "mtfsb0 with a field"},
    {Refusal::ReservedBits, {xoform(63, 4, 0, 3, 134), kBlr}, "mtfsfi with the reserved bit"},
    {Refusal::ReservedBits, {xoform(63, 4, 4, 1, 64), kBlr}, "mcrfs with a field"},
    {Refusal::ReservedBits, {aform(4, 3, 4, 5, 6, 21), kBlr}, "ps_add with fC"},
    {Refusal::ReservedBits, {xoform(4, 3, 4, 5, 72), kBlr}, "ps_mr with fA"},
    {Refusal::ReservedBits, {xoform(4, 1, 4, 5, 0), kBlr}, "ps_cmpu0 with reserved bits"},
    {Refusal::ReservedBits, {psq_x(3, 4, 5, 1, 2, 6) | 1, kBlr}, "psq_lx with Rc"},
    // ---- OE=1 ----
    {Refusal::OverflowEnable, {xform(3, 4, 5, 266 + 512), kBlr}, "addo"},
    {Refusal::OverflowEnable, {xform(3, 4, 5, 10 + 512, 1), kBlr}, "addco."},
    {Refusal::OverflowEnable, {xform(3, 4, 5, 136 + 512), kBlr}, "subfeo"},
    {Refusal::OverflowEnable, {xform(3, 4, 0, 104 + 512), kBlr}, "nego"},
    // ---- Rc=1 on float forms ----
    {Refusal::FloatRecord, {aform(63, 3, 4, 5, 0, 21, 1), kBlr}, "fadd."},
    {Refusal::FloatRecord, {aform(59, 3, 4, 5, 6, 29, 1), kBlr}, "fmadds."},
    {Refusal::FloatRecord, {xoform(63, 3, 0, 5, 72, 1), kBlr}, "fmr."},
    {Refusal::FloatRecord, {xoform(63, 3, 0, 5, 15, 1), kBlr}, "fctiwz."},
    {Refusal::FloatRecord, {xoform(63, 3, 0, 0, 583, 1), kBlr}, "mffs."},
    {Refusal::FloatRecord, {(63u << 26) | (0xFFu << 17) | (2u << 11) | (711u << 1) | 1, kBlr}, "mtfsf."},
    {Refusal::FloatRecord, {xoform(63, 3, 0, 0, 38, 1), kBlr}, "mtfsb1."},
    {Refusal::FloatRecord, {xoform(63, 4, 0, 2, 134, 1), kBlr}, "mtfsfi."},
    {Refusal::FloatRecord, {aform(4, 3, 4, 5, 0, 21, 1), kBlr}, "ps_add."},
    {Refusal::FloatRecord, {xoform(4, 3, 4, 5, 528, 1), kBlr}, "ps_merge00."},
    // ---- calls and branches the planner does not model ----
    {Refusal::CounterBranch, {bcctr(16, 0), kBlr}, "bdnzctr"},
    {Refusal::CounterBranch, {bcctr(0, 2, 1), kBlr}, "bdnzfctrl"},
    // ---- invalid forms ----
    {Refusal::InvalidUpdate, {dform(33, 3, 0, 4), kBlr}, "lwzu with RA=0"},
    {Refusal::InvalidUpdate, {dform(33, 3, 3, 4), kBlr}, "lwzu with RA=RD"},
    {Refusal::InvalidUpdate, {dform(37, 3, 0, 4), kBlr}, "stwu with RA=0"},
    {Refusal::InvalidUpdate, {dform(35, 4, 4, 0), kBlr}, "lbzu with RA=RD"},
    {Refusal::InvalidUpdate, {dform(43, 4, 4, 0), kBlr}, "lhau with RA=RD"},
    {Refusal::InvalidUpdate, {xform(3, 0, 5, 55), kBlr}, "lwzux with RA=0"},
    {Refusal::InvalidUpdate, {xform(3, 3, 5, 119), kBlr}, "lbzux with RA=RD"},
    {Refusal::InvalidUpdate, {xform(3, 0, 5, 183), kBlr}, "stwux with RA=0"},
    {Refusal::InvalidUpdate, {dform(49, 3, 0, 4), kBlr}, "lfsu with RA=0"},
    {Refusal::InvalidUpdate, {dform(55, 3, 0, 4), kBlr}, "stfdu with RA=0"},
    {Refusal::InvalidUpdate, {xform(3, 0, 5, 631), kBlr}, "lfdux with RA=0"},
    {Refusal::InvalidUpdate, {psq_d(57, 3, 0, 0, 0, 8), kBlr}, "psq_lu with RA=0"},
    {Refusal::InvalidUpdate, {psq_x(3, 0, 5, 0, 0, 39), kBlr}, "psq_stux with RA=0"},
    {Refusal::InvalidMultiple, {dform(46, 29, 30, 0), kBlr}, "lmw with RA in the range"},
    {Refusal::InvalidMultiple, {dform(46, 0, 0, 0), kBlr}, "lmw r0 with RA=0"},
    // ---- special registers ----
    {Refusal::SpecialRegister, {mfspr(3, 268), kBlr}, "mfspr of the time base"},
    {Refusal::SpecialRegister, {mtspr(22, 3), kBlr}, "mtdec"},
    {Refusal::SpecialRegister, {mfspr(3, 912), kBlr}, "mfspr GQR0"},
    {Refusal::SpecialRegister, {mtspr(1008, 3), kBlr}, "mtspr HID0"},
    // ---- traps ----
    {Refusal::DroppedByEmit, {dform(3, 31, 3, 0), kBlr}, "twi"},
    {Refusal::DroppedByEmit, {xform(31, 0, 0, 4), kBlr}, "trap"},
    // ---- the whole function ----
    {Refusal::FallsOffEnd, {kNop}, "no return"},
    {Refusal::FallsOffEnd, {kNop, bc(12, 2, -4)}, "falls off when not taken"},
    {Refusal::FallsOffEnd, {bclr(12, 2)}, "conditional return with nothing after it"},
    {Refusal::FallsOffEnd, {bc(12, 2, 0x100)}, "conditional tail call with nothing after it"},
    {Refusal::FallsOffEnd, {branch(8) | 1}, "a call as the last instruction"},
    {Refusal::FallsOffEnd, {bcctr(20, 0, 1)}, "bctrl as the last instruction"},
    {Refusal::FallsOffEnd, {xform(0, 0, 0, 598)}, "only an instruction that plans as nothing"},
    {Refusal::FallsOffEnd, {branch(4), kNop}, "the jump lands on an instruction that falls off"},
  };
  std::vector<ppc::stencil::Instruction> plan;
  std::vector<ppc::stencil::Refused> all;
  std::string error;
  bool reached[ppc::stencil::kRefusalCount] = {};
  const uint32_t base = ppc::RAM_BASE + 0x3000;
  uint64_t checked = 0;
  for (const auto& item : cases) {
    const auto code = bytes(item.words);
    ++checked;
    const bool planned = ppc::stencil::plan_leaf(code.data(), code.size(), base, plan, error);
    const bool reason_named = error.find(ppc::stencil::refusal_text(item.reason)) != std::string::npos;
    ppc::stencil::CompiledLeaf leaf;
    std::string translate_error;
    const bool translated = ppc::stencil::translate_leaf(code.data(), code.size(), base, ppc::stencil::generated::table, leaf, translate_error);
    // The coverage mode names the same reason and still refuses.
    const bool collected = ppc::stencil::plan_leaf(code.data(), code.size(), base, plan, error, &all);
    bool listed = false;
    for (const auto& refused : all) listed = listed || refused.reason == item.reason;
    if (planned || !plan.empty() || !reason_named || translated || leaf.ready() || collected || !listed) {
      std::printf("FAIL: %s must be refused as %s; planner said \"%s\"\n", item.what,
                  ppc::stencil::refusal_name(item.reason), error.c_str());
      ++failures;
    }
    reached[static_cast<size_t>(item.reason)] = true;
  }
  // Words no path from the entry reaches are not planned, so they cannot refuse a function:
  // data and dead code after the last return, and the inline data a `bl` jumps over.
  for (const auto& words : std::vector<std::vector<uint32_t>>{
           {kBlr, kNop}, {kBlr, 0x00000000u, 0x44000002u, 0xFFFFFFFFu}, {branch(8), 0x00000000u, kBlr},
           {branch(12) | 1, 0x00000000u, 0x3F800000u, mfspr(3, 8), kBlr}}) {
    const auto code = bytes(words);
    ++checked;
    CHECK(ppc::stencil::plan_leaf(code.data(), code.size(), base, plan, error) && !plan.empty());
  }
  // The range of the function itself.
  const auto good = bytes({kNop, kBlr});
  const auto range = [&](const uint8_t* code, size_t size, uint32_t address) {
    ++checked;
    const bool planned = ppc::stencil::plan_leaf(code, size, address, plan, error);
    if (planned || !plan.empty() || error != ppc::stencil::refusal_text(Refusal::InvalidRange)) {
      std::printf("FAIL: range %08X + %zu must be refused as InvalidRange\n", address, size);
      ++failures;
    }
    reached[static_cast<size_t>(Refusal::InvalidRange)] = true;
  };
  CHECK(ppc::stencil::plan_leaf(good.data(), good.size(), base, plan, error) && error.empty());
  range(nullptr, 8, base);
  range(good.data(), 0, base);
  range(good.data(), 6, base);
  range(good.data(), 8, base + 2);
  range(good.data(), 8, 0x7FFFFFFCu);
  range(good.data(), 8, ppc::RAM_BASE + ppc::RAM_SIZE);
  range(good.data(), 8, ppc::RAM_BASE + ppc::RAM_SIZE - 4);
  range(good.data(), 8, 0xC0003000u);
  { std::vector<uint8_t> large(ppc::stencil::kMaxFunctionBytes + 4, 0x60);
    range(large.data(), large.size(), base); }
  // Every reason the planner has is reached above.
  for (uint32_t i = 0; i < ppc::stencil::kRefusalCount; ++i) {
    if (reached[i]) continue;
    std::printf("FAIL: no test reaches refusal %s\n", ppc::stencil::refusal_name(static_cast<Refusal>(i)));
    ++failures;
  }
  // Coverage mode keeps going: three refused words in one function are all listed, in order.
  { const auto code = bytes({0x44000002u, kNop, xform(3, 4, 5, 266 + 512), mfspr(3, 912), kBlr});
    CHECK(!ppc::stencil::plan_leaf(code.data(), code.size(), base, plan, error, &all) && plan.empty());
    CHECK(all.size() == 3 && all[0].index == 0 && all[0].reason == Refusal::NoStencil && all[1].index == 2 &&
          all[1].reason == Refusal::OverflowEnable && all[2].index == 3 && all[2].reason == Refusal::SpecialRegister);
    CHECK(error.find("byte 0") != std::string::npos); }
  // The largest function the planner takes is planned and translated.
  { std::vector<uint32_t> words(ppc::stencil::kMaxFunctionBytes / 4 - 1, kNop);
    words.push_back(kBlr);
    const auto code = bytes(words);
    ppc::stencil::CompiledLeaf leaf;
    CHECK(ppc::stencil::translate_leaf(code.data(), code.size(), base, ppc::stencil::generated::table, leaf, error) && leaf.ready()); }
  if (!failures)
    std::printf("leaf refusal: %llu refused functions, every one of the %u refusal reasons reached and named\n",
                (unsigned long long)checked, ppc::stencil::kRefusalCount);
  return failures ? 1 : 0;
}
