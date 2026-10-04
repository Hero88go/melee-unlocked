// Float and paired-single register forms: translated native code versus the reference in
// ppc_leaf_reference.h, bit for bit, over NaN payloads, infinities, denormals, the limits of float
// and int32, every host rounding mode and flush-to-zero.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "ppc_leaf_function.h"

namespace {
using namespace reference;
using worlds::Function;
using worlds::failures;
uint64_t known_total = 0;
// Round to nearest, toward zero, up, down, and nearest with flush-to-zero and denormals-are-zero.
const unsigned kModes[] = {0x1F80, 0x7F80, 0x5F80, 0x3F80, 0x9FC0};

struct Known {
  Function function;
  ppc::Context c{};
  bool run(std::vector<uint32_t> words) {
    ++known_total;
    words.push_back(kBlr);
    return function.translate(words) && function.run_native(c);
  }
};

// Results worked out by hand from the PowerPC definitions, ppc.h and emit.py.
void known_answers() {
  worlds::g_start_mxcsr = 0x1F80;
  { Known k; k.c.f[1].ps0 = 1.5; k.c.f[2].ps0 = 2.25; k.c.f[3].u1 = 0x77;
    CHECK(k.run({aform(63, 3, 1, 2, 0, 21)}) && k.c.f[3].ps0 == 3.75 && k.c.f[3].u1 == 0x77); }           // fadd leaves ps1
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[2].u0 = 0x3E10000000000000ull; // 2^-30
    CHECK(k.run({aform(59, 3, 1, 2, 0, 21)}) && k.c.f[3].ps0 == 1.0 && k.c.f[3].ps1 == 1.0); }           // fadds rounds, both halves
  { Known k; k.c.f[1].ps0 = 5.0; k.c.f[2].ps0 = 2.0;
    CHECK(k.run({aform(63, 3, 1, 2, 0, 20)}) && k.c.f[3].ps0 == 3.0); }                                  // fsub: a - b
  { Known k; k.c.f[1].ps0 = 3.0; k.c.f[2].ps0 = 4.0; k.c.f[0].ps0 = 100.0;
    CHECK(k.run({aform(63, 3, 1, 0, 2, 25)}) && k.c.f[3].ps0 == 12.0); }                                 // fmul uses fC, not fB
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[2].ps0 = 4.0;
    CHECK(k.run({aform(63, 3, 1, 2, 0, 18)}) && k.c.f[3].ps0 == 0.25); }                                 // fdiv
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[4].ps0 = 3.0;
    CHECK(k.run({aform(63, 3, 1, 2, 4, 29)}) && k.c.f[3].ps0 == 16.0); }                                 // fmadd: a*c + b
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[4].ps0 = 3.0;
    CHECK(k.run({aform(63, 3, 1, 2, 4, 28)}) && k.c.f[3].ps0 == -4.0); }                                 // fmsub: a*c - b
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[4].ps0 = 3.0;
    CHECK(k.run({aform(63, 3, 1, 2, 4, 31)}) && k.c.f[3].ps0 == -16.0); }                                // fnmadd
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[4].ps0 = 3.0;
    CHECK(k.run({aform(63, 3, 1, 2, 4, 30)}) && k.c.f[3].ps0 == 4.0); }                                  // fnmsub
  { Known k; k.c.f[2].u0 = 0x7FF4000000000123ull;
    CHECK(k.run({xoform(63, 3, 0, 2, 72)}) && k.c.f[3].u0 == 0x7FF4000000000123ull); }                   // fmr keeps a signalling NaN
  { Known k; k.c.f[2].u0 = 0x7FF4000000000123ull;
    CHECK(k.run({xoform(63, 3, 0, 2, 40)}) && k.c.f[3].u0 == 0xFFF4000000000123ull); }                   // fneg
  { Known k; k.c.f[2].u0 = 0xFFF4000000000123ull;
    CHECK(k.run({xoform(63, 3, 0, 2, 264)}) && k.c.f[3].u0 == 0x7FF4000000000123ull); }                  // fabs
  { Known k; k.c.f[2].ps0 = 2.0;
    CHECK(k.run({xoform(63, 3, 0, 2, 136)}) && k.c.f[3].ps0 == -2.0); }                                  // fnabs
  // fctiwz truncates and saturates; the upper half is 0xFFF80000.
  struct Convert { double value; uint32_t toward_zero, nearest; };
  for (const auto& item : {Convert{2.9, 2, 3}, Convert{-2.9, 0xFFFFFFFEu, 0xFFFFFFFDu}, Convert{2.5, 2, 2}, Convert{3.5, 3, 4},
                           Convert{3e9, 0x7FFFFFFFu, 0x7FFFFFFFu}, Convert{-3e9, 0x80000000u, 0x80000000u},
                           Convert{ppc::bits_to_double(0x7FF8000000000000ull), 0x80000000u, 0x80000000u}}) {
    Known z, n;
    z.c.f[2].ps0 = n.c.f[2].ps0 = item.value;
    CHECK(z.run({xoform(63, 3, 0, 2, 15)}) && z.c.f[3].u0 == (0xFFF8000000000000ull | item.toward_zero));
    CHECK(n.run({xoform(63, 3, 0, 2, 14)}) && n.c.f[3].u0 == (0xFFF8000000000000ull | item.nearest));
  }
  // fcmpu: LT 8, GT 4, EQ 2, unordered 1; XER.SO is not or'ed in.
  struct Compare { double a, b; uint8_t cr; };
  for (const auto& item : {Compare{1.0, 2.0, 8}, Compare{2.0, 1.0, 4}, Compare{2.0, 2.0, 2}, Compare{0.0, -0.0, 2},
                           Compare{ppc::bits_to_double(0x7FF8000000000000ull), 1.0, 1},
                           Compare{1.0, ppc::bits_to_double(0x7FF0000000000001ull), 1}}) {
    Known k; k.c.f[1].ps0 = item.a; k.c.f[2].ps0 = item.b; k.c.so = 1;
    CHECK(k.run({xoform(63, 6 << 2, 1, 2, 0)}) && k.c.cr[6] == item.cr && k.c.cr[0] == 0);
  }
  // fsel: fC when fA >= -0.0, else fB (a NaN selects fB).
  for (const auto& item : {Compare{-0.0, 0, 1}, Compare{0.0, 0, 1}, Compare{-1.0, 0, 0},
                           Compare{ppc::bits_to_double(0x7FF8000000000000ull), 0, 0}}) {
    Known k; k.c.f[1].ps0 = item.a; k.c.f[2].ps0 = 20.0; k.c.f[4].ps0 = 40.0;
    CHECK(k.run({aform(63, 3, 1, 2, 4, 23)}) && k.c.f[3].ps0 == (item.cr ? 40.0 : 20.0));
  }
  { Known k; k.c.fpscr = 0x12345678u;
    CHECK(k.run({xoform(63, 3, 0, 0, 583)}) && k.c.f[3].u0 == 0xFFF8000012345678ull); }                  // mffs
  { Known k; k.c.fpscr = 0x11111111u; k.c.f[2].u0 = 0xAAAAAAAA22222220ull;
    CHECK(k.run({(63u << 26) | (0x81u << 17) | (2u << 11) | (711u << 1)}) && k.c.fpscr == 0x21111110u); } // mtfsf fields 0 and 7
  { Known k; k.c.fpscr = 0;
    CHECK(k.run({xoform(63, 31, 0, 0, 38)}) && k.c.fpscr == 1); }                                        // mtfsb1 31
  { Known k; k.c.fpscr = 0xFFFFFFF8u;
    CHECK(k.run({xoform(63, 0, 0, 0, 70)}) && k.c.fpscr == 0x7FFFFFF8u); }                               // mtfsb0 0
  { Known k; k.c.fpscr = 0xFFFFFFF0u;
    CHECK(k.run({xoform(63, 6 << 2, 0, 5 << 1, 134)}) && k.c.fpscr == 0xFFFFFF50u); }                    // mtfsfi 6,5
  { Known k; k.c.fpscr = 0x00A00000u;
    CHECK(k.run({xoform(63, 5 << 2, 2 << 2, 0, 64)}) && k.c.cr[5] == 0xA); }                             // mcrfs cr5,2
  // The rounding mode a guest sets applies to what follows: 1/3 as a single, toward zero.
  { Known k; k.c.fpscr = 0; k.c.f[1].ps0 = 1.0; k.c.f[2].ps0 = 3.0;
    CHECK(k.run({xoform(63, 7 << 2, 0, 1 << 1, 134), aform(59, 3, 1, 2, 0, 18)}) && k.c.f[3].u0 == 0x3FD5555540000000ull); }
  { Known k; k.c.fpscr = 0; k.c.f[1].ps0 = 1.0; k.c.f[2].ps0 = 3.0;
    CHECK(k.run({aform(59, 3, 1, 2, 0, 18)}) && k.c.f[3].u0 == 0x3FD5555560000000ull && k.c.f[3].u1 == 0x3FD5555560000000ull); }
  { Known k; k.c.f[2].u0 = 0x3FF0000000000001ull;
    CHECK(k.run({xoform(63, 3, 0, 2, 12)}) && k.c.f[3].ps0 == 1.0 && k.c.f[3].ps1 == 1.0); }             // frsp
  // Paired singles work on both halves.
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[1].ps1 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[2].ps1 = 20.0;
    CHECK(k.run({aform(4, 3, 1, 2, 0, 21)}) && k.c.f[3].ps0 == 11.0 && k.c.f[3].ps1 == 22.0); }          // ps_add
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[1].ps1 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[2].ps1 = 20.0;
    CHECK(k.run({xoform(4, 3, 1, 2, 560)}) && k.c.f[3].ps0 == 1.0 && k.c.f[3].ps1 == 20.0); }            // ps_merge01
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[1].ps1 = 2.0; k.c.f[2].ps0 = 10.0; k.c.f[2].ps1 = 20.0;
    CHECK(k.run({xoform(4, 3, 1, 2, 592)}) && k.c.f[3].ps0 == 2.0 && k.c.f[3].ps1 == 10.0); }            // ps_merge10
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[2].ps1 = 20.0; k.c.f[4].ps1 = 7.0;
    CHECK(k.run({aform(4, 3, 1, 2, 4, 10)}) && k.c.f[3].ps0 == 21.0 && k.c.f[3].ps1 == 7.0); }           // ps_sum0
  { Known k; k.c.f[1].ps0 = 1.0; k.c.f[2].ps1 = 20.0; k.c.f[4].ps0 = 7.0;
    CHECK(k.run({aform(4, 3, 1, 2, 4, 11)}) && k.c.f[3].ps0 == 7.0 && k.c.f[3].ps1 == 21.0); }           // ps_sum1
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[1].ps1 = 3.0; k.c.f[4].ps0 = 5.0; k.c.f[4].ps1 = 100.0;
    CHECK(k.run({aform(4, 3, 1, 0, 4, 12)}) && k.c.f[3].ps0 == 10.0 && k.c.f[3].ps1 == 15.0); }          // ps_muls0
  { Known k; k.c.f[1].ps0 = 2.0; k.c.f[1].ps1 = 3.0; k.c.f[4].ps0 = 5.0; k.c.f[4].ps1 = 100.0;
    CHECK(k.run({aform(4, 3, 1, 0, 4, 13)}) && k.c.f[3].ps0 == 200.0 && k.c.f[3].ps1 == 300.0); }        // ps_muls1
  { Known k; k.c.f[1].ps1 = 1.0; k.c.f[2].ps1 = 2.0; k.c.f[1].ps0 = 9.0; k.c.f[2].ps0 = 1.0;
    CHECK(k.run({xoform(4, 2 << 2, 1, 2, 64)}) && k.c.cr[2] == 8); }                                     // ps_cmpu1 compares ps1
}

void random_contexts(const WideForm& form, Random& random) {
  for (int encoding = 0; encoding < 150 && failures < 20; ++encoding) {
    Function function;
    if (!function.translate({random_word(form, random), kBlr})) return;
    worlds::g_start_mxcsr = kModes[encoding % 5];
    for (int sample = 0; sample < 40; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!function.compare(start, form.name)) return;
    }
  }
}

// Every pair of special operands for fA and fB with each special fC in turn, through distinct and
// repeated registers, in every rounding mode.
void specials(const WideForm& form, Random& random) {
  static const uint32_t registers[][4] = {{3, 4, 5, 6}, {3, 3, 5, 6}, {3, 4, 3, 6}, {3, 4, 5, 3}, {3, 4, 4, 4}, {3, 3, 3, 3}};
  size_t turn = 0;
  for (const auto& fields : registers) {
    Function function;
    if (!function.translate({encode(form, fields[0], fields[1], fields[2], fields[3], 0xA5), kBlr})) return;
    ppc::Context start;
    randomize_rich(start, random);
    for (size_t a = 0; a < kSpecialDoubleCount; ++a) {
      for (size_t b = 0; b < kSpecialDoubleCount; ++b) {
        const size_t c = turn++ % kSpecialDoubleCount;
        worlds::g_start_mxcsr = kModes[turn % 5];
        // Later writes win where a register repeats, as in the integer boundary sweep.
        start.f[fields[3]].u0 = kSpecialDoubles[c]; start.f[fields[3]].u1 = kSpecialDoubles[(c + 7) % kSpecialDoubleCount];
        start.f[fields[2]].u0 = kSpecialDoubles[b]; start.f[fields[2]].u1 = kSpecialDoubles[a];
        start.f[fields[1]].u0 = kSpecialDoubles[a]; start.f[fields[1]].u1 = kSpecialDoubles[b];
        start.fpscr = uint32_t(turn * 0x9E3779B1u);
        if (!function.compare(start, form.name)) return;
      }
    }
  }
}

// Random straight-line functions of float forms, float loads and stores and integer forms.
void sequences(Random& random) {
  for (int index = 0; index < 1500 && failures < 20; ++index) {
    std::vector<uint32_t> words;
    const uint32_t length = 1 + random.below(40);
    for (uint32_t i = 0; i < length; ++i) {
      const uint32_t pick = random.below(8);
      if (pick < 5) words.push_back(random_word(kFloatForms[random.below(uint32_t(kFloatFormCount))], random));
      else if (pick < 7) words.push_back(random_word(kMemoryForms[random.below(uint32_t(kMemoryFormCount))], random));
      else words.push_back(random_word(kForms[random.below(uint32_t(kFormCount))], random));
    }
    words.push_back(kBlr);
    Function function;
    if (!function.translate(words, ppc::RAM_BASE + 0x3000 + 4 * random.below(64))) return;
    worlds::g_start_mxcsr = kModes[index % 5];
    for (int sample = 0; sample < 12; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!function.compare(start, "float sequence", true)) return;
    }
  }
}

// The comparison can fail: one bit of a NaN payload, the other half of a pair, a rounding mode.
void controls(Random& random) {
  Function fmr;
  if (!fmr.translate({xoform(63, 3, 0, 2, 72), kBlr})) return;
  const auto differs = [&](Function& native, const std::vector<uint32_t>& other, const ppc::Context& start, const char* expect) {
    worlds::install(native.address, native.code);
    worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return native.leaf.run(c, m); });
    worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, other, native.address); });
    const char* what = worlds::difference();
    if (!what || std::strcmp(what, expect)) {
      std::printf("FAIL: control expected a difference in %s, saw %s\n", expect, what ? what : "none");
      ++failures;
    }
  };
  ppc::Context start;
  randomize(start, random);
  start.f[2].u0 = 0x7FF4000000000123ull; start.f[2].u1 = 5; start.f[3].u0 = 0; start.f[3].u1 = 6; start.fpscr = 0;
  differs(fmr, {xoform(63, 3, 0, 2, 40), kBlr}, start, "context");   // fneg: one bit of the NaN
  differs(fmr, {xoform(4, 3, 0, 2, 72), kBlr}, start, "context");    // ps_mr: the other half
  differs(fmr, {xoform(63, 3, 0, 2, 72), xoform(63, 7 << 2, 0, 1 << 1, 134), kBlr}, start, "context"); // fpscr
  Function quiet;
  if (!quiet.translate({xoform(63, 3, 0, 2, 72), kBlr})) return;
  start.fpscr = 1; // The reference sets the same FPSCR bits it already has: only the host mode moves.
  differs(quiet, {xoform(63, 3, 0, 2, 72), xoform(63, 7 << 2, 0, 1 << 1, 134), kBlr}, start, "host calls");
}
} // namespace

int main() {
  Random random(0x2026100300000002ull);
  if (!worlds::init(2, random)) return 1;
  known_answers();
  controls(random);
  const uint64_t fixed = worlds::comparisons;
  for (const auto& form : kFloatForms) if (!failures) random_contexts(form, random);
  const uint64_t random_total = worlds::comparisons - fixed;
  for (const auto& form : kFloatForms) if (!failures) specials(form, random);
  const uint64_t special_total = worlds::comparisons - fixed - random_total;
  if (!failures) sequences(random);
  const uint64_t sequence_total = worlds::comparisons - fixed - random_total - special_total;
  if (!failures)
    std::printf("leaf float: %zu forms, %llu known answers, %llu random full-context comparisons (6000 per form), "
                "%llu special-operand comparisons (%zu special doubles, every pair, 5 host float modes), "
                "%llu comparisons over 1500 random functions; where two NaN operands leave the result to the "
                "compiler, either NaN was accepted in %llu single-instruction comparisons and %llu function "
                "samples were set aside\n",
                kFloatFormCount, (unsigned long long)known_total, (unsigned long long)random_total,
                (unsigned long long)special_total, kSpecialDoubleCount, (unsigned long long)sequence_total,
                (unsigned long long)worlds::g_either_nan_accepted, (unsigned long long)worlds::set_aside);
  return failures ? 1 : 0;
}
