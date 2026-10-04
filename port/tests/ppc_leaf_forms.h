// The instruction forms the translator accepts outside branches, with canonical encoders and
// random encodings. Shared by the native tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc_leaf_reference.h"

namespace reference {
// How a form's three 5-bit fields and its immediate are used.
enum class Kind {
  SignedImmediate,   // op rd, ra, simm
  UnsignedImmediate, // op rs, ra, uimm
  Arithmetic,        // 31: rd, ra, rb
  ArithmeticUnary,   // 31: rd, ra
  Logical,           // 31: rs, ra, rb
  LogicalUnary,      // 31: rs, ra
  ShiftImmediate,    // 31: rs, ra, sh
  Rotate,            // rs, ra, sh, mb, me
  RotateRegister,    // rs, ra, rb, mb, me
  Compare,           // 31: crfd, ra, rb
  CompareImmediate,  // op crfd, ra, imm
  Mfcr, Mtcrf, Mcrf, Mfspr, Mtspr,
  CrLogical,         // 19: crbD, crbA, crbB
  Mcrxr,             // 31: crfd
  Fixed,             // sync, eieio, isync: no fields
  CacheHint,         // 31: ra, rb (emit.py writes nothing)
};
struct Form { const char* name; Kind kind; uint32_t op, xo, rc, slice; }; // slice: where it was added
inline const Form kForms[] = {
  {"addi", Kind::SignedImmediate, 14, 0, 0, 1}, {"addis", Kind::SignedImmediate, 15, 0, 0, 1},
  {"addic", Kind::SignedImmediate, 12, 0, 0, 1}, {"addic.", Kind::SignedImmediate, 13, 0, 0, 1},
  {"subfic", Kind::SignedImmediate, 8, 0, 0, 1}, {"mulli", Kind::SignedImmediate, 7, 0, 0, 1},
  {"ori", Kind::UnsignedImmediate, 24, 0, 0, 1}, {"oris", Kind::UnsignedImmediate, 25, 0, 0, 1},
  {"xori", Kind::UnsignedImmediate, 26, 0, 0, 1}, {"xoris", Kind::UnsignedImmediate, 27, 0, 0, 1},
  {"andi.", Kind::UnsignedImmediate, 28, 0, 0, 1}, {"andis.", Kind::UnsignedImmediate, 29, 0, 0, 1},
#define MU_BOTH(name, kind, op, xo) {name, kind, op, xo, 0, 1}, {name ".", kind, op, xo, 1, 1}
  MU_BOTH("add", Kind::Arithmetic, 31, 266), MU_BOTH("subf", Kind::Arithmetic, 31, 40),
  MU_BOTH("mullw", Kind::Arithmetic, 31, 235), MU_BOTH("mulhw", Kind::Arithmetic, 31, 75),
  MU_BOTH("mulhwu", Kind::Arithmetic, 31, 11), MU_BOTH("divw", Kind::Arithmetic, 31, 491),
  MU_BOTH("divwu", Kind::Arithmetic, 31, 459), MU_BOTH("addc", Kind::Arithmetic, 31, 10),
  MU_BOTH("adde", Kind::Arithmetic, 31, 138), MU_BOTH("subfc", Kind::Arithmetic, 31, 8),
  MU_BOTH("subfe", Kind::Arithmetic, 31, 136),
  MU_BOTH("neg", Kind::ArithmeticUnary, 31, 104),
  MU_BOTH("addze", Kind::ArithmeticUnary, 31, 202), MU_BOTH("addme", Kind::ArithmeticUnary, 31, 234),
  MU_BOTH("subfze", Kind::ArithmeticUnary, 31, 200), MU_BOTH("subfme", Kind::ArithmeticUnary, 31, 232),
  MU_BOTH("and", Kind::Logical, 31, 28), MU_BOTH("or", Kind::Logical, 31, 444),
  MU_BOTH("xor", Kind::Logical, 31, 316), MU_BOTH("nand", Kind::Logical, 31, 476),
  MU_BOTH("nor", Kind::Logical, 31, 124), MU_BOTH("eqv", Kind::Logical, 31, 284),
  MU_BOTH("andc", Kind::Logical, 31, 60), MU_BOTH("orc", Kind::Logical, 31, 412),
  MU_BOTH("slw", Kind::Logical, 31, 24), MU_BOTH("srw", Kind::Logical, 31, 536),
  MU_BOTH("sraw", Kind::Logical, 31, 792), MU_BOTH("extsb", Kind::LogicalUnary, 31, 954),
  MU_BOTH("extsh", Kind::LogicalUnary, 31, 922), MU_BOTH("cntlzw", Kind::LogicalUnary, 31, 26),
  MU_BOTH("srawi", Kind::ShiftImmediate, 31, 824), MU_BOTH("rlwinm", Kind::Rotate, 21, 0),
  MU_BOTH("rlwimi", Kind::Rotate, 20, 0), MU_BOTH("rlwnm", Kind::RotateRegister, 23, 0),
#undef MU_BOTH
  {"cmpw", Kind::Compare, 31, 0, 0, 2}, {"cmplw", Kind::Compare, 31, 32, 0, 2},
  {"cmpwi", Kind::CompareImmediate, 11, 0, 0, 2}, {"cmplwi", Kind::CompareImmediate, 10, 0, 0, 2},
  {"mfcr", Kind::Mfcr, 31, 19, 0, 2}, {"mtcrf", Kind::Mtcrf, 31, 144, 0, 2},
  {"mcrf", Kind::Mcrf, 19, 0, 0, 2},
  {"mflr", Kind::Mfspr, 31, 8, 0, 2}, {"mfctr", Kind::Mfspr, 31, 9, 0, 2},
  {"mfxer", Kind::Mfspr, 31, 1, 0, 2}, {"mtlr", Kind::Mtspr, 31, 8, 0, 2},
  {"mtctr", Kind::Mtspr, 31, 9, 0, 2}, {"mtxer", Kind::Mtspr, 31, 1, 0, 2},
  // Third set: the forms that were refused by compiled shape, and the ones emit.py drops.
  {"crand", Kind::CrLogical, 19, 257, 0, 5}, {"cror", Kind::CrLogical, 19, 449, 0, 5},
  {"crxor", Kind::CrLogical, 19, 193, 0, 5}, {"crnand", Kind::CrLogical, 19, 225, 0, 5},
  {"crnor", Kind::CrLogical, 19, 33, 0, 5}, {"creqv", Kind::CrLogical, 19, 289, 0, 5},
  {"crandc", Kind::CrLogical, 19, 129, 0, 5}, {"crorc", Kind::CrLogical, 19, 417, 0, 5},
  {"mcrxr", Kind::Mcrxr, 31, 512, 0, 5},
  {"sync", Kind::Fixed, 31, 598, 0, 5}, {"eieio", Kind::Fixed, 31, 854, 0, 5}, {"isync", Kind::Fixed, 19, 150, 0, 5},
  {"dcbst", Kind::CacheHint, 31, 54, 0, 5}, {"dcbf", Kind::CacheHint, 31, 86, 0, 5},
  {"dcbtst", Kind::CacheHint, 31, 246, 0, 5}, {"dcbt", Kind::CacheHint, 31, 278, 0, 5},
  {"dcbi", Kind::CacheHint, 31, 470, 0, 5}, {"icbi", Kind::CacheHint, 31, 982, 0, 5},
};
// One canonical word of a form from three register-sized fields, an immediate and a mask.
inline uint32_t encode(const Form& form, uint32_t a, uint32_t b, uint32_t c, uint32_t immediate, uint32_t mb, uint32_t me) {
  switch (form.kind) {
    case Kind::SignedImmediate: case Kind::UnsignedImmediate: return dform(form.op, a, b, immediate);
    case Kind::Arithmetic: case Kind::Logical: return xform(a, b, c, form.xo, form.rc);
    case Kind::ArithmeticUnary: case Kind::LogicalUnary: return xform(a, b, 0, form.xo, form.rc);
    case Kind::ShiftImmediate: return xform(a, b, c, form.xo, form.rc);
    case Kind::Rotate: case Kind::RotateRegister: return mform(form.op, a, b, c, mb, me, form.rc);
    case Kind::Compare: return xform((a & 7) << 2, b, c, form.xo);
    case Kind::CompareImmediate: return dform(form.op, (a & 7) << 2, b, immediate);
    case Kind::Mfcr: return xform(a, 0, 0, 19);
    case Kind::Mtcrf: return mtcrf(immediate & 0xFF, a);
    case Kind::Mcrf: return xlform((a & 7) << 2, (b & 7) << 2, 0, 0);
    case Kind::Mfspr: return mfspr(a, form.xo);
    case Kind::Mtspr: return mtspr(form.xo, a);
    case Kind::CrLogical: return xlform(a, b, c, form.xo);
    case Kind::Mcrxr: return xform((a & 7) << 2, 0, 0, form.xo);
    case Kind::Fixed: return (form.op << 26) | (form.xo << 1);
    case Kind::CacheHint: return xform(0, b, c, form.xo);
  }
  return 0;
}
// Registers repeat often, so an instruction naming one register two or three times is common.
inline uint32_t random_register(Random& random) {
  static const uint32_t favourites[] = {0, 3, 31};
  return random.below(3) == 0 ? favourites[random.below(3)] : random.below(32);
}
inline uint32_t random_word(const Form& form, Random& random) {
  const bool count = form.kind == Kind::ShiftImmediate || form.kind == Kind::Rotate; // third field is SH
  const uint32_t a = random_register(random), b = random_register(random);
  const uint32_t c = count ? random.below(32) : random_register(random);
  uint32_t immediate = random.u32() & 0xFFFFu;
  if (random.below(4) == 0) {
    static const uint32_t edges[] = {0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x00FF, 0xFF00, 0x8001};
    immediate = edges[random.below(8)];
  }
  return encode(form, a, b, c, immediate, random.below(32), random.below(32));
}
constexpr size_t kFormCount = sizeof kForms / sizeof kForms[0];
// The GPR a word of this form writes, or 32 when it writes none.
inline uint32_t written_register(const Form& form, uint32_t word) {
  switch (form.kind) {
    case Kind::SignedImmediate: case Kind::Arithmetic: case Kind::ArithmeticUnary: case Kind::Mfcr: case Kind::Mfspr:
      return (word >> 21) & 31;
    case Kind::UnsignedImmediate: case Kind::Logical: case Kind::LogicalUnary: case Kind::ShiftImmediate:
    case Kind::Rotate: case Kind::RotateRegister:
      return (word >> 16) & 31;
    default: return 32;
  }
}

// ---- memory, float and paired-single forms (third set) ----
enum class Wide {
  D, DUpdate, DUpdateLoad,   // op rD|rS|fD, d(rA); update needs rA != 0, an integer load also rA != rD
  X, XUpdate, XUpdateLoad,   // 31: indexed
  Lmw, Stmw,
  PsqD, PsqDUpdate, PsqX, PsqXUpdate,
  FloatAB, FloatAC, FloatACB, FloatB, FloatUnary, FloatCompare, // op 59, 63 or 4
  Mffs, Mtfsf, Mtfsb, Mtfsfi, Mcrfs,
};
struct WideForm { const char* name; Wide kind; uint32_t op, xo; bool memory; };
inline const WideForm kMemoryForms[] = {
  {"lwz", Wide::D, 32, 0, true}, {"lwzu", Wide::DUpdateLoad, 33, 0, true}, {"lbz", Wide::D, 34, 0, true},
  {"lbzu", Wide::DUpdateLoad, 35, 0, true}, {"stw", Wide::D, 36, 0, true}, {"stwu", Wide::DUpdate, 37, 0, true},
  {"stb", Wide::D, 38, 0, true}, {"stbu", Wide::DUpdate, 39, 0, true}, {"lhz", Wide::D, 40, 0, true},
  {"lhzu", Wide::DUpdateLoad, 41, 0, true}, {"lha", Wide::D, 42, 0, true}, {"lhau", Wide::DUpdateLoad, 43, 0, true},
  {"sth", Wide::D, 44, 0, true}, {"sthu", Wide::DUpdate, 45, 0, true},
  {"lmw", Wide::Lmw, 46, 0, true}, {"stmw", Wide::Stmw, 47, 0, true},
  {"lwzx", Wide::X, 31, 23, true}, {"lwzux", Wide::XUpdateLoad, 31, 55, true}, {"lbzx", Wide::X, 31, 87, true},
  {"lbzux", Wide::XUpdateLoad, 31, 119, true}, {"lhzx", Wide::X, 31, 279, true}, {"lhzux", Wide::XUpdateLoad, 31, 311, true},
  {"lhax", Wide::X, 31, 343, true}, {"lhaux", Wide::XUpdateLoad, 31, 375, true},
  {"stwx", Wide::X, 31, 151, true}, {"stwux", Wide::XUpdate, 31, 183, true}, {"stbx", Wide::X, 31, 215, true},
  {"stbux", Wide::XUpdate, 31, 247, true}, {"sthx", Wide::X, 31, 407, true}, {"sthux", Wide::XUpdate, 31, 439, true},
  {"lwbrx", Wide::X, 31, 534, true}, {"lhbrx", Wide::X, 31, 790, true}, {"stwbrx", Wide::X, 31, 662, true},
  {"sthbrx", Wide::X, 31, 918, true},
  {"lfs", Wide::D, 48, 0, true}, {"lfsu", Wide::DUpdate, 49, 0, true}, {"lfd", Wide::D, 50, 0, true},
  {"lfdu", Wide::DUpdate, 51, 0, true}, {"stfs", Wide::D, 52, 0, true}, {"stfsu", Wide::DUpdate, 53, 0, true},
  {"stfd", Wide::D, 54, 0, true}, {"stfdu", Wide::DUpdate, 55, 0, true},
  {"lfsx", Wide::X, 31, 535, true}, {"lfsux", Wide::XUpdate, 31, 567, true}, {"lfdx", Wide::X, 31, 599, true},
  {"lfdux", Wide::XUpdate, 31, 631, true}, {"stfsx", Wide::X, 31, 663, true}, {"stfsux", Wide::XUpdate, 31, 695, true},
  {"stfdx", Wide::X, 31, 727, true}, {"stfdux", Wide::XUpdate, 31, 759, true}, {"stfiwx", Wide::X, 31, 983, true},
  {"psq_l", Wide::PsqD, 56, 0, true}, {"psq_lu", Wide::PsqDUpdate, 57, 0, true},
  {"psq_st", Wide::PsqD, 60, 0, true}, {"psq_stu", Wide::PsqDUpdate, 61, 0, true},
  {"psq_lx", Wide::PsqX, 4, 6, true}, {"psq_stx", Wide::PsqX, 4, 7, true},
  {"psq_lux", Wide::PsqXUpdate, 4, 38, true}, {"psq_stux", Wide::PsqXUpdate, 4, 39, true},
};
inline const WideForm kFloatForms[] = {
  {"fadd", Wide::FloatAB, 63, 21}, {"fsub", Wide::FloatAB, 63, 20}, {"fmul", Wide::FloatAC, 63, 25},
  {"fdiv", Wide::FloatAB, 63, 18}, {"fmadd", Wide::FloatACB, 63, 29}, {"fmsub", Wide::FloatACB, 63, 28},
  {"fnmadd", Wide::FloatACB, 63, 31}, {"fnmsub", Wide::FloatACB, 63, 30},
  {"fadds", Wide::FloatAB, 59, 21}, {"fsubs", Wide::FloatAB, 59, 20}, {"fmuls", Wide::FloatAC, 59, 25},
  {"fdivs", Wide::FloatAB, 59, 18}, {"fmadds", Wide::FloatACB, 59, 29}, {"fmsubs", Wide::FloatACB, 59, 28},
  {"fnmadds", Wide::FloatACB, 59, 31}, {"fnmsubs", Wide::FloatACB, 59, 30},
  {"fres", Wide::FloatB, 59, 24}, {"frsqrte", Wide::FloatB, 63, 26}, {"fsel", Wide::FloatACB, 63, 23},
  {"frsp", Wide::FloatUnary, 63, 12}, {"fmr", Wide::FloatUnary, 63, 72}, {"fneg", Wide::FloatUnary, 63, 40},
  {"fabs", Wide::FloatUnary, 63, 264}, {"fnabs", Wide::FloatUnary, 63, 136},
  {"fctiw", Wide::FloatUnary, 63, 14}, {"fctiwz", Wide::FloatUnary, 63, 15},
  {"fcmpu", Wide::FloatCompare, 63, 0}, {"fcmpo", Wide::FloatCompare, 63, 32},
  {"mffs", Wide::Mffs, 63, 583}, {"mtfsf", Wide::Mtfsf, 63, 711}, {"mtfsb0", Wide::Mtfsb, 63, 70},
  {"mtfsb1", Wide::Mtfsb, 63, 38}, {"mtfsfi", Wide::Mtfsfi, 63, 134}, {"mcrfs", Wide::Mcrfs, 63, 64},
  {"ps_add", Wide::FloatAB, 4, 21}, {"ps_sub", Wide::FloatAB, 4, 20}, {"ps_mul", Wide::FloatAC, 4, 25},
  {"ps_div", Wide::FloatAB, 4, 18}, {"ps_muls0", Wide::FloatAC, 4, 12}, {"ps_muls1", Wide::FloatAC, 4, 13},
  {"ps_madd", Wide::FloatACB, 4, 29}, {"ps_msub", Wide::FloatACB, 4, 28}, {"ps_nmadd", Wide::FloatACB, 4, 31},
  {"ps_nmsub", Wide::FloatACB, 4, 30}, {"ps_madds0", Wide::FloatACB, 4, 14}, {"ps_madds1", Wide::FloatACB, 4, 15},
  {"ps_sum0", Wide::FloatACB, 4, 10}, {"ps_sum1", Wide::FloatACB, 4, 11}, {"ps_sel", Wide::FloatACB, 4, 23},
  {"ps_res", Wide::FloatB, 4, 24}, {"ps_rsqrte", Wide::FloatB, 4, 26},
  {"ps_mr", Wide::FloatUnary, 4, 72}, {"ps_neg", Wide::FloatUnary, 4, 40}, {"ps_abs", Wide::FloatUnary, 4, 264},
  {"ps_nabs", Wide::FloatUnary, 4, 136},
  {"ps_merge00", Wide::FloatAB, 4, 528}, {"ps_merge01", Wide::FloatAB, 4, 560},
  {"ps_merge10", Wide::FloatAB, 4, 592}, {"ps_merge11", Wide::FloatAB, 4, 624},
  {"ps_cmpu0", Wide::FloatCompare, 4, 0}, {"ps_cmpo0", Wide::FloatCompare, 4, 32},
  {"ps_cmpu1", Wide::FloatCompare, 4, 64}, {"ps_cmpo1", Wide::FloatCompare, 4, 96},
};
// One canonical word from explicit fields: a = first, b = second, c = third, k = fourth (fC).
inline uint32_t encode(const WideForm& form, uint32_t a, uint32_t b, uint32_t c, uint32_t k, uint32_t immediate) {
  switch (form.kind) {
    case Wide::D: case Wide::DUpdate: case Wide::DUpdateLoad: case Wide::Lmw: case Wide::Stmw:
      return dform(form.op, a, b, immediate);
    case Wide::X: case Wide::XUpdate: case Wide::XUpdateLoad: return xform(a, b, c, form.xo);
    case Wide::PsqD: case Wide::PsqDUpdate: return psq_d(form.op, a, b, k & 1, (k >> 1) & 7, immediate);
    case Wide::PsqX: case Wide::PsqXUpdate: return psq_x(a, b, c, k & 1, (k >> 1) & 7, form.xo);
    case Wide::FloatAB: return form.xo > 31 ? xoform(form.op, a, b, c, form.xo) : aform(form.op, a, b, c, 0, form.xo);
    case Wide::FloatAC: return aform(form.op, a, b, 0, k, form.xo);
    case Wide::FloatACB: return aform(form.op, a, b, c, k, form.xo);
    case Wide::FloatB: return aform(form.op, a, 0, c, 0, form.xo);
    case Wide::FloatUnary: return xoform(form.op, a, 0, c, form.xo);
    case Wide::FloatCompare: return xoform(form.op, (a & 7) << 2, b, c, form.xo);
    case Wide::Mffs: return xoform(63, a, 0, 0, form.xo);
    case Wide::Mtfsf: return (63u << 26) | ((immediate & 0xFFu) << 17) | (c << 11) | (form.xo << 1);
    case Wide::Mtfsb: return xoform(63, a, 0, 0, form.xo);
    case Wide::Mtfsfi: return xoform(63, (a & 7) << 2, 0, (immediate & 15) << 1, form.xo);
    case Wide::Mcrfs: return xoform(63, (a & 7) << 2, (b & 7) << 2, 0, form.xo);
  }
  return 0;
}
// A random canonical word the planner accepts: update forms never name r0 as the base, integer
// load-updates never load their own base, lmw never loads its base.
inline uint32_t random_word(const WideForm& form, Random& random) {
  uint32_t a = random_register(random), b = random_register(random);
  const uint32_t c = random_register(random), k = random_register(random);
  uint32_t immediate = random.u32() & 0xFFFFu;
  if (random.below(4) == 0) {
    static const uint32_t edges[] = {0, 1, 4, 0x7FFF, 0x8000, 0xFFFF, 0xFFFC, 0xFFF8};
    immediate = edges[random.below(8)];
  }
  const bool update = form.kind == Wide::DUpdate || form.kind == Wide::DUpdateLoad || form.kind == Wide::XUpdate ||
                      form.kind == Wide::XUpdateLoad || form.kind == Wide::PsqDUpdate || form.kind == Wide::PsqXUpdate;
  if (update) while (b == 0) b = 1 + random.below(31);
  if (form.kind == Wide::DUpdateLoad || form.kind == Wide::XUpdateLoad) while (a == b) a = random.below(32);
  if (form.kind == Wide::Lmw) { a = 1 + random.below(31); b = random.below(a); }
  if (form.kind == Wide::Stmw && random.below(2)) a = 24 + random.below(8); // Short runs are the common use.
  return encode(form, a, b, c, k, immediate);
}
constexpr size_t kMemoryFormCount = sizeof kMemoryForms / sizeof kMemoryForms[0];
constexpr size_t kFloatFormCount = sizeof kFloatForms / sizeof kFloatForms[0];
} // namespace reference
