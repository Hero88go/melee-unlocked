// Integer, compare and condition-register stencils versus the reference in ppc_leaf_reference.h.
// Per instruction form: 10,000 random full contexts over 200 random canonical encodings, then an
// explicit boundary sweep; known answers; random straight-line functions.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "leaf_translation_plan.h"
#include "ppc_leaf_stencils.generated.h"
#include <array>

namespace {
using namespace reference;
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

uint64_t comparisons = 0, known_total = 0, rejected_total = 0;
std::array<uint8_t, 64> ram{};
// Translates {word, blr}, then runs it natively and through the reference from one context.
struct Single {
  ppc::stencil::CompiledLeaf leaf;
  std::vector<uint32_t> words;
  bool translate(uint32_t word) {
    words = {word, kBlr};
    const auto code = bytes(words);
    std::string error;
    std::memcpy(ram.data(), code.data(), code.size());
    if (!ppc::stencil::translate_leaf(code.data(), code.size(), ppc::RAM_BASE,
                                      ppc::stencil::generated::table, leaf, error)) {
      std::printf("FAIL: %08X does not translate: %s\n", word, error.c_str());
      ++failures;
      return false;
    }
    return true;
  }
  bool compare(const ppc::Context& start, const char* name) {
    ppc::Context actual, expected;
    std::memcpy(&actual, &start, sizeof start);
    std::memcpy(&expected, &start, sizeof start);
    const auto before_ram = ram;
    const bool reference_ok = run(expected, words, ppc::RAM_BASE);
    const bool native_ok = leaf.run(actual, ram.data());
    ++comparisons;
    if (reference_ok && native_ok && same(actual, expected) && ram == before_ram) return true;
    std::printf("FAIL: %s %08X differs (reference %d native %d)\n", name, words[0], reference_ok, native_ok);
    describe(actual, expected);
    ++failures;
    return false;
  }
};

void random_contexts(const Form& form, Random& random) {
  for (int encoding = 0; encoding < 200 && failures < 20; ++encoding) {
    Single single;
    if (!single.translate(random_word(form, random))) return;
    for (int sample = 0; sample < 50; ++sample) {
      ppc::Context start;
      randomize(start, random);
      if (!single.compare(start, form.name)) return;
    }
  }
}

// Explicit boundaries: every pair of boundary operand values with carry and summary overflow
// both ways, through distinct and repeated registers, with boundary immediates, every shift
// count and both ordinary and wrapped rotate masks.
void boundaries(const Form& form, Random& random) {
  static const uint32_t registers[][3] = {{3,4,5}, {3,3,5}, {3,4,3}, {4,3,3}, {3,3,3}, {0,0,0}, {0,4,5}, {3,0,5}, {31,30,0}};
  static const uint32_t immediates[] = {0, 1, 2, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF, 0x00FF, 0x0100};
  static const uint32_t mask_bits[] = {0, 1, 7, 8, 15, 16, 24, 30, 31};
  std::vector<uint32_t> words;
  for (const auto& triple : registers) {
    switch (form.kind) {
      case Kind::SignedImmediate: case Kind::UnsignedImmediate: case Kind::CompareImmediate: case Kind::Mtcrf:
        for (uint32_t immediate : immediates) words.push_back(encode(form, triple[0], triple[1], triple[2], immediate, 0, 0));
        if (form.kind == Kind::Mtcrf)
          for (uint32_t crm = 0; crm < 256; ++crm) words.push_back(encode(form, triple[0], 0, 0, crm, 0, 0));
        break;
      case Kind::ShiftImmediate:
        for (uint32_t sh = 0; sh < 32; ++sh) words.push_back(encode(form, triple[0], triple[1], sh, 0, 0, 0));
        break;
      case Kind::Rotate:
        for (uint32_t sh : {0u, 1u, 4u, 15u, 16u, 31u})
          for (uint32_t mb : mask_bits) for (uint32_t me : mask_bits)
            words.push_back(encode(form, triple[0], triple[1], sh, 0, mb, me));
        break;
      case Kind::RotateRegister:
        for (uint32_t mb : mask_bits) for (uint32_t me : mask_bits)
          words.push_back(encode(form, triple[0], triple[1], triple[2], 0, mb, me));
        break;
      default:
        words.push_back(encode(form, triple[0], triple[1], triple[2], 0, 0, 0));
        break;
    }
  }
  if (form.kind == Kind::Compare || form.kind == Kind::CompareImmediate || form.kind == Kind::Mcrf)
    for (uint32_t field = 0; field < 8; ++field)
      for (uint32_t other = 0; other < 8; ++other)
        words.push_back(encode(form, field, form.kind == Kind::Mcrf ? other : 4, 5, other * 0x1111u, 0, 0));
  for (uint32_t word : words) {
    if (failures >= 20) return;
    Single single;
    if (!single.translate(word)) return;
    ppc::Context start;
    randomize(start, random);
    const uint32_t first = (word >> 21) & 31, second = (word >> 16) & 31, third = (word >> 11) & 31;
    // Forms with many encodings pair each boundary value with its complement only.
    const bool every_pair = words.size() <= 200;
    for (uint32_t x : kBoundary) for (uint32_t y : kBoundary) {
      if (!every_pair && y != kBoundary[0]) break;
      const uint32_t other = every_pair ? y : ~x;
      // Arithmetic forms read the second and third fields; logical, shift and rotate forms read
      // the first and third, and rlwimi also the old value of the second. Later writes win
      // where an instruction repeats a register.
      const bool source_first = form.kind == Kind::Logical || form.kind == Kind::LogicalUnary ||
          form.kind == Kind::ShiftImmediate || form.kind == Kind::Rotate || form.kind == Kind::RotateRegister ||
          form.kind == Kind::UnsignedImmediate || form.kind == Kind::Mtcrf || form.kind == Kind::Mtspr;
      start.r[third] = other;
      if (source_first) { start.r[second] = other; start.r[first] = x; }
      else { start.r[first] = x ^ other; start.r[second] = x; }
      for (uint32_t flags = 0; flags < 4; ++flags) {
        start.ca = flags & 1; start.so = flags >> 1;
        if (!single.compare(start, form.name)) return;
      }
    }
    // The CR and XER readers see every 4-bit field value and every flag combination.
    if (form.kind == Kind::Mfcr || form.kind == Kind::Mcrf || form.kind == Kind::Mfspr)
      for (uint32_t value = 0; value < 16; ++value) {
        for (int field = 0; field < 8; ++field) start.cr[field] = uint8_t((value + field * 5) & 15);
        start.so = value & 1; start.ca = (value >> 1) & 1; start.ov = (value >> 2) & 1;
        if (!single.compare(start, form.name)) return;
      }
  }
}

// Known answers worked out by hand from the PowerPC definitions and emit.py: guards against one
// misreading shared by the stencils and the reference. Inputs r4, r5, CA, SO; result r3 and CA.
struct Known { uint32_t word, r4, r5, ca, so, r3, ca_out; };
const uint32_t kUnchanged = 2; // ca_out: the instruction must leave CA as it was.
void known_answers() {
  const Known cases[] = {
    {dform(14, 3, 0, 0xFFFF), 0, 0, 0, 0, 0xFFFFFFFFu, kUnchanged},                    // li r3,-1
    {dform(15, 3, 0, 0x8000), 0, 0, 1, 0, 0x80000000u, kUnchanged},                    // lis r3,0x8000
    {xform(4, 3, 4, 444), 0x12345678u, 0, 0, 0, 0x12345678u, kUnchanged},              // mr r3,r4
    {xform(3, 4, 5, 10), 0xFFFFFFFFu, 1, 0, 0, 0, 1},                                  // addc carries out
    {xform(3, 4, 5, 10), 0x7FFFFFFFu, 1, 1, 0, 0x80000000u, 0},                        // addc, signed overflow only
    {xform(3, 4, 5, 138), 0xFFFFFFFFu, 0, 1, 0, 0, 1},                                 // adde carry in and out
    {xform(3, 4, 5, 138), 0xFFFFFFFEu, 0, 1, 0, 0xFFFFFFFFu, 0},
    {xform(3, 4, 5, 138), 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 0, 0xFFFFFFFFu, 1},
    {xform(3, 4, 5, 40), 1, 0, 1, 0, 0xFFFFFFFFu, kUnchanged},                         // subf: rb - ra
    {xform(3, 4, 5, 8), 1, 0, 1, 0, 0xFFFFFFFFu, 0},                                   // subfc borrows
    {xform(3, 4, 5, 8), 0, 0, 0, 0, 0, 1},                                             // subfc 0 - 0
    {xform(3, 4, 5, 8), 5, 7, 0, 0, 2, 1},
    {dform(8, 3, 4, 0), 0, 0, 0, 0, 0, 1},                                             // subfic r3,r4,0
    {dform(8, 3, 4, 0), 1, 0, 1, 0, 0xFFFFFFFFu, 0},
    {dform(8, 3, 4, 0xFFFF), 1, 0, 0, 0, 0xFFFFFFFEu, 1},                              // -1 - 1
    {dform(12, 3, 4, 0xFFFF), 1, 0, 0, 0, 0, 1},                                       // addic r3,r4,-1
    {dform(12, 3, 4, 1), 0xFFFFFFFEu, 0, 1, 0, 0xFFFFFFFFu, 0},
    {xform(3, 4, 0, 104), 0x80000000u, 0, 0, 0, 0x80000000u, kUnchanged},              // neg
    {xform(3, 4, 0, 104), 1, 0, 0, 0, 0xFFFFFFFFu, kUnchanged},
    {xform(3, 4, 0, 202), 0xFFFFFFFFu, 0, 1, 0, 0, 1},                                 // addze
    {xform(4, 3, 5, 24), 1, 31, 0, 0, 0x80000000u, kUnchanged},                        // slw
    {xform(4, 3, 5, 24), 1, 32, 0, 0, 0, kUnchanged},
    {xform(4, 3, 5, 24), 1, 63, 0, 0, 0, kUnchanged},
    {xform(4, 3, 5, 24), 1, 64, 0, 0, 1, kUnchanged},                                  // only six count bits
    {xform(4, 3, 5, 536), 0x80000000u, 31, 0, 0, 1, kUnchanged},                       // srw
    {xform(4, 3, 5, 536), 0x80000000u, 32, 0, 0, 0, kUnchanged},
    {xform(4, 3, 5, 792), 0x80000000u, 31, 1, 0, 0xFFFFFFFFu, 0},                      // sraw, no ones lost
    {xform(4, 3, 5, 792), 0x80000001u, 1, 0, 0, 0xC0000000u, 1},
    {xform(4, 3, 5, 792), 0x80000000u, 32, 0, 0, 0xFFFFFFFFu, 1},
    {xform(4, 3, 5, 792), 0x7FFFFFFFu, 32, 1, 0, 0, 0},
    {xform(4, 3, 5, 792), 0x80000001u, 0, 1, 0, 0x80000001u, 0},
    {xform(4, 3, 31, 824), 0xFFFFFFFFu, 0, 0, 0, 0xFFFFFFFFu, 1},                      // srawi 31
    {xform(4, 3, 0, 824), 0xFFFFFFFFu, 0, 1, 0, 0xFFFFFFFFu, 0},                       // srawi 0
    {xform(4, 3, 4, 824), 0x7FFFFFF1u, 0, 1, 0, 0x07FFFFFFu, 0},
    {mform(21, 4, 3, 4, 28, 3), 0x12345678u, 0, 0, 0, 0x20000001u, kUnchanged},        // rlwinm, wrapped mask
    {mform(21, 4, 3, 0, 0, 31), 0xDEADBEEFu, 0, 0, 0, 0xDEADBEEFu, kUnchanged},
    {mform(21, 4, 3, 8, 24, 31), 0xAB000000u, 0, 0, 0, 0xABu, kUnchanged},
    {mform(21, 4, 3, 31, 0, 0), 0x00000001u, 0, 0, 0, 0x80000000u, kUnchanged},
    {mform(23, 4, 3, 5, 16, 31), 0x12345678u, 36, 0, 0, 0x00006781u, kUnchanged},       // rlwnm by 36 & 31
    {xform(4, 3, 0, 954), 0x00000080u, 0, 0, 0, 0xFFFFFF80u, kUnchanged},              // extsb
    {xform(4, 3, 0, 954), 0x0000017Fu, 0, 0, 0, 0x0000007Fu, kUnchanged},
    {xform(4, 3, 0, 922), 0x00008000u, 0, 0, 0, 0xFFFF8000u, kUnchanged},              // extsh
    {xform(4, 3, 0, 922), 0x00017FFFu, 0, 0, 0, 0x00007FFFu, kUnchanged},
    {xform(4, 3, 0, 26), 0, 0, 0, 0, 32, kUnchanged},                                  // cntlzw
    {xform(4, 3, 0, 26), 1, 0, 0, 0, 31, kUnchanged},
    {xform(4, 3, 0, 26), 0x80000000u, 0, 0, 0, 0, kUnchanged},
    {dform(7, 3, 4, 0xFFFF), 0x80000000u, 0, 0, 0, 0x80000000u, kUnchanged},           // mulli by -1
    {dform(7, 3, 4, 0xFFFE), 3, 0, 0, 0, 0xFFFFFFFAu, kUnchanged},
    {xform(3, 4, 5, 235), 0x10000u, 0x10000u, 0, 0, 0, kUnchanged},                    // mullw wraps
    {xform(3, 4, 5, 75), 0x80000000u, 0x80000000u, 0, 0, 0x40000000u, kUnchanged},     // mulhw
    {xform(3, 4, 5, 75), 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0, kUnchanged},
    {xform(3, 4, 5, 11), 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0xFFFFFFFEu, kUnchanged},     // mulhwu
    {xform(3, 4, 5, 491), 7, 0, 0, 0, 0, kUnchanged},                                  // divw by zero
    {xform(3, 4, 5, 491), 0xFFFFFFFFu, 0, 0, 0, 0xFFFFFFFFu, kUnchanged},
    {xform(3, 4, 5, 491), 0x80000000u, 0xFFFFFFFFu, 0, 0, 0, kUnchanged},              // INT_MIN / -1
    {xform(3, 4, 5, 491), 0xFFFFFFF9u, 2, 0, 0, 0xFFFFFFFDu, kUnchanged},              // -7 / 2 truncates
    {xform(3, 4, 5, 459), 7, 0, 0, 0, 0, kUnchanged},                                  // divwu by zero
    {xform(4, 3, 5, 60), 0xFF00FF00u, 0x0F0F0F0Fu, 0, 0, 0xF000F000u, kUnchanged},     // andc
    {xform(4, 3, 5, 124), 0, 0, 0, 0, 0xFFFFFFFFu, kUnchanged},                        // nor
    {xform(4, 3, 5, 412), 0x0000FFFFu, 0x00FFFFFFu, 0, 0, 0xFF00FFFFu, kUnchanged},    // orc
    {xform(4, 3, 5, 284), 0x0F0F0F0Fu, 0x00FF00FFu, 0, 0, 0xF00FF00Fu, kUnchanged},    // eqv
  };
  for (const auto& known : cases) {
    Single single;
    if (!single.translate(known.word)) return;
    ++known_total;
    ppc::Context c{};
    c.r[4] = known.r4; c.r[5] = known.r5; c.ca = known.ca; c.so = known.so;
    const auto before_ram = ram;
    CHECK(single.leaf.run(c, ram.data()));
    const uint32_t ca = known.ca_out == kUnchanged ? known.ca : known.ca_out;
    if (c.r[3] != known.r3 || c.ca != ca || ram != before_ram) {
      std::printf("FAIL: known answer %08X: r3 %08X (want %08X) ca %u (want %u)\n", known.word, c.r[3], known.r3, c.ca, ca);
      ++failures;
    }
  }
  // rlwimi keeps the bits of RA outside the mask.
  { Single single; ppc::Context c{};
    if (single.translate(mform(20, 4, 3, 8, 16, 23))) {
      c.r[3] = 0xAAAAAAAAu; c.r[4] = 0x000000CDu;
      CHECK(single.leaf.run(c, ram.data()) && c.r[3] == 0xAAAACDAAu); } }
  // Record forms: CR0 is LT, GT or EQ of the signed result, or'ed with XER.SO.
  struct Recorded { uint32_t r4, r5, so; uint8_t cr0; };
  for (const auto& recorded : {Recorded{1, 0xFFFFFFFFu, 0, 2}, Recorded{1, 0xFFFFFFFFu, 1, 3},
                               Recorded{0x7FFFFFFFu, 1, 0, 8}, Recorded{1, 1, 1, 5}}) {
    Single single; ppc::Context c{};
    if (!single.translate(xform(3, 4, 5, 266, 1))) return;
    c.r[4] = recorded.r4; c.r[5] = recorded.r5; c.so = recorded.so; c.cr[0] = 0xFF;
    CHECK(single.leaf.run(c, ram.data()) && c.cr[0] == recorded.cr0 && c.r[3] == recorded.r4 + recorded.r5);
  }
  struct Compared { uint32_t word, r4, r5, so; int field; uint8_t value; };
  for (const auto& compared : {
         Compared{dform(11, 7 << 2, 4, 0xFFFF), 0xFFFFFFFFu, 0, 1, 7, 3},      // cmpwi cr7,r4,-1: EQ|SO
         Compared{dform(11, 0, 4, 0), 0x80000000u, 0, 0, 0, 8},                // negative < 0
         Compared{dform(10, 1 << 2, 4, 0xFFFF), 0, 0, 0, 1, 8},                // cmplwi cr1: 0 < 0xFFFF
         Compared{dform(10, 1 << 2, 4, 0xFFFF), 0x10000u, 0, 1, 1, 5},         // uimm is not sign-extended
         Compared{xform(6 << 2, 4, 5, 0), 0x80000000u, 0x7FFFFFFFu, 0, 6, 8},  // cmpw: signed less
         Compared{xform(6 << 2, 4, 5, 32), 0x80000000u, 0x7FFFFFFFu, 0, 6, 4}, // cmplw: unsigned greater
       }) {
    Single single; ppc::Context c{};
    if (!single.translate(compared.word)) return;
    c.r[4] = compared.r4; c.r[5] = compared.r5; c.so = compared.so;
    CHECK(single.leaf.run(c, ram.data()) && c.cr[compared.field] == compared.value);
    for (int field = 0; field < 8; ++field) CHECK(field == compared.field || c.cr[field] == 0);
  }
  { Single single; ppc::Context c{};
    const uint8_t fields[8] = {8, 4, 2, 1, 0xF, 0, 0xA, 5};
    std::memcpy(c.cr, fields, 8);
    if (single.translate(xform(3, 0, 0, 19))) CHECK(single.leaf.run(c, ram.data()) && c.r[3] == 0x8421F0A5u);
    if (single.translate(mtcrf(0x81, 4))) {
      c.r[4] = 0x12345678u;
      CHECK(single.leaf.run(c, ram.data()) && c.cr[0] == 1 && c.cr[7] == 8 && c.cr[1] == 4 && c.cr[6] == 0xA);
    }
    if (single.translate(mtspr(1, 4))) {
      c.r[4] = 0xA0000000u;
      CHECK(single.leaf.run(c, ram.data()) && c.so == 1 && c.ov == 0 && c.ca == 1);
    }
    if (single.translate(mfspr(3, 1))) CHECK(single.leaf.run(c, ram.data()) && c.r[3] == 0xA0000000u);
  }
}

// Random straight-line functions of 1 to 48 instructions from every form, 16 contexts each.
void sequences(Random& random) {
  const size_t form_count = sizeof kForms / sizeof kForms[0];
  std::array<uint8_t, 256> wide{};
  for (int function = 0; function < 2000 && failures < 20; ++function) {
    std::vector<uint32_t> words;
    const uint32_t length = 1 + random.below(48);
    for (uint32_t i = 0; i < length; ++i) words.push_back(random_word(kForms[random.below(uint32_t(form_count))], random));
    words.push_back(kBlr);
    const auto code = bytes(words);
    const uint32_t address = ppc::RAM_BASE + 4 * random.below(8);
    std::memcpy(wide.data() + (address - ppc::RAM_BASE), code.data(), code.size());
    ppc::stencil::CompiledLeaf leaf;
    std::string error;
    CHECK(ppc::stencil::translate_leaf(code.data(), code.size(), address, ppc::stencil::generated::table, leaf, error));
    if (!leaf.ready()) { std::printf("  %s\n", error.c_str()); return; }
    for (int sample = 0; sample < 16; ++sample) {
      ppc::Context actual, expected;
      randomize(actual, random);
      std::memcpy(&expected, &actual, sizeof actual);
      const auto before_ram = wide;
      const bool reference_ok = run(expected, words, address);
      const bool native_ok = leaf.run(actual, wide.data());
      ++comparisons;
      if (!reference_ok || !native_ok || !same(actual, expected) || wide != before_ram) {
        std::printf("FAIL: sequence %d of %u instructions differs\n", function, length);
        for (uint32_t word : words) std::printf("  %08X\n", word);
        describe(actual, expected);
        ++failures;
        return;
      }
    }
  }
}

// The comparison can fail: native add against the reference for subf, and one flipped byte
// anywhere in the context, must both be seen as differences.
void controls(Random& random) {
  Single single;
  if (!single.translate(xform(3, 4, 5, 266))) return;
  ppc::Context actual, expected;
  randomize(actual, random);
  actual.r[4] = 1; actual.r[5] = 2;
  std::memcpy(&expected, &actual, sizeof actual);
  CHECK(run(expected, {xform(3, 4, 5, 40), kBlr}, ppc::RAM_BASE));
  CHECK(single.leaf.run(actual, ram.data()));
  CHECK(!same(actual, expected) && actual.r[3] == 3 && expected.r[3] == 1);
  for (size_t at : {size_t(0), sizeof actual / 2, sizeof actual - 1}) {
    std::memcpy(&expected, &actual, sizeof actual);
    reinterpret_cast<uint8_t*>(&expected)[at] ^= 1;
    CHECK(!same(actual, expected));
  }
}

// Words the planner must refuse: emit.py has no faithful or no canonical form for them.
void rejections() {
  std::vector<ppc::stencil::Instruction> plan;
  std::string error;
  const uint32_t rejected[] = {
    xform(3, 4, 5, 266 + 512),      // addo: emit.py models no overflow flag
    xform(3, 4, 5, 10 + 512, 1),    // addco.
    xform(3, 4, 5, 104),            // neg with a reserved RB field
    xform(3, 4, 5, 202),            // addze with a reserved RB field
    xform(4, 3, 5, 954),            // extsb with a reserved RB field
    xform(4, 3, 5, 26),             // cntlzw with a reserved RB field
    xform(3, 4, 5, 9),              // no such extended opcode
    xform(3, 4, 5, 339),            // mfspr of an SPR outside XER, LR and CTR
    0x44000002u,                    // sc
    dform(11, (7 << 2) | 1, 4, 0),  // cmpwi with L=1
    dform(10, (7 << 2) | 2, 4, 0),  // cmplwi with the reserved bit
    xform((7 << 2) | 1, 4, 5, 0),   // cmpw with L=1
    xform(7 << 2, 4, 5, 0, 1),      // cmpw with Rc
    xform(3, 1, 0, 19),             // mfcr with a reserved field
    mtcrf(0xFF, 3) | (1u << 20),    // mtocrf form
    xlform(1, 4, 0, 0),             // mcrf with reserved bits
    xlform(1 << 2, 2 << 2, 0, 0) | 1, // mcrf with LK
    mfspr(3, 268),                  // mftb through mfspr numbering
    mtspr(22, 3),                   // mtdec
  };
  for (uint32_t word : rejected) {
    const auto code = bytes({word, kBlr});
    ++rejected_total;
    if (plan_leaf(code.data(), code.size(), ppc::RAM_BASE, plan, error) || !plan.empty()) {
      std::printf("FAIL: %08X must be rejected\n", word);
      ++failures;
    }
  }
}
} // namespace

int main() {
  Random random(0x2026100100000002ull);
  const size_t form_count = sizeof kForms / sizeof kForms[0];
  known_answers();
  rejections();
  controls(random);
  const uint64_t fixed = comparisons;
  for (const auto& form : kForms) if (!failures) random_contexts(form, random);
  const uint64_t random_total = comparisons - fixed;
  for (const auto& form : kForms) if (!failures) boundaries(form, random);
  const uint64_t boundary_total = comparisons - fixed - random_total;
  if (!failures) sequences(random);
  const uint64_t sequence_total = comparisons - fixed - random_total - boundary_total;
  if (!failures)
    std::printf("leaf integer: %zu forms, %llu random full-context comparisons (10000 per form), "
                "%llu boundary comparisons, %llu register known answers plus the CR and SPR ones, %llu "
                "rejected words and %llu comparisons over 2000 random straight-line functions passed\n", form_count,
                (unsigned long long)random_total, (unsigned long long)boundary_total,
                (unsigned long long)known_total, (unsigned long long)rejected_total,
                (unsigned long long)sequence_total);
  return failures ? 1 : 0;
}
