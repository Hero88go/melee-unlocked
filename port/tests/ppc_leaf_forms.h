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
} // namespace reference
