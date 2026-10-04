// Branches inside one function: b, bc on CR bits and CTR, bclr, loop back-edges and the poll.
// Translated native code versus the reference in ppc_leaf_reference.h, which executes the guest
// words one at a time and calls ppc::backedge exactly where emit.py writes it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "leaf_translation_plan.h"
#include "ppc_leaf_stencils.generated.h"
#include <array>

namespace {
using namespace reference;
using ppc::stencil::Operation;
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

constexpr uint32_t kNop = 0x60000000u;
uint32_t li(uint32_t rd, uint32_t value) { return dform(14, rd, 0, value); }
uint32_t addi(uint32_t rd, uint32_t ra, uint32_t value) { return dform(14, rd, ra, value); }

struct Polls { uint64_t count, digest; };
void reset_polls() { ppc::g_poll_count = 0; ppc::g_poll_digest = 1469598103934665603ull; }
Polls polls() { return {ppc::g_poll_count, ppc::g_poll_digest}; }

uint64_t comparisons = 0, polled_runs = 0, total_polls = 0, total_backedges = 0;
// One translated function in its own guest RAM.
struct Function {
  std::vector<uint32_t> words;
  std::array<uint8_t, 4096> ram{};
  ppc::stencil::CompiledLeaf leaf;
  uint32_t address = ppc::RAM_BASE;
  bool build(std::vector<uint32_t> guest, uint32_t at = ppc::RAM_BASE) {
    words = std::move(guest);
    address = at;
    const auto code = bytes(words);
    std::string error;
    if (address - ppc::RAM_BASE + code.size() > ram.size()) { std::printf("FAIL: function too large\n"); ++failures; return false; }
    std::memcpy(ram.data() + (address - ppc::RAM_BASE), code.data(), code.size());
    if (!ppc::stencil::translate_leaf(code.data(), code.size(), address, ppc::stencil::generated::table, leaf, error)) {
      std::printf("FAIL: function does not translate: %s\n", error.c_str());
      dump();
      ++failures;
      return false;
    }
    return true;
  }
  void dump() const { for (uint32_t word : words) std::printf("  %08X\n", word); }
  // Native and reference from one context: the whole context, every poll and guest RAM agree.
  bool compare(const ppc::Context& start, const char* what) {
    ppc::Context actual, expected;
    std::memcpy(&actual, &start, sizeof start);
    std::memcpy(&expected, &start, sizeof start);
    const auto before_ram = ram;
    reset_polls();
    const bool reference_ok = run(expected, words, address);
    const Polls expected_polls = polls();
    reset_polls();
    const bool native_ok = leaf.run(actual, ram.data());
    const Polls actual_polls = polls();
    ++comparisons;
    total_backedges += expected.backedges - start.backedges;
    if (expected_polls.count) { ++polled_runs; total_polls += expected_polls.count; }
    if (reference_ok && native_ok && same(actual, expected) && ram == before_ram &&
        actual_polls.count == expected_polls.count && actual_polls.digest == expected_polls.digest) return true;
    std::printf("FAIL: %s differs (reference %d native %d, polls %llu native %llu)\n", what, reference_ok, native_ok,
                (unsigned long long)expected_polls.count, (unsigned long long)actual_polls.count);
    dump();
    describe(actual, expected);
    ++failures;
    return false;
  }
};

// The planner's stencil graph for representative branches.
void plans() {
  using ppc::stencil::Instruction;
  std::vector<Instruction> plan;
  std::string error;
  const auto planned = [&](const std::vector<uint32_t>& words) {
    const auto code = bytes(words);
    return ppc::stencil::plan_leaf(code.data(), code.size(), ppc::RAM_BASE, plan, error);
  };
  // Forward b: one Jump straight to the target's stencil. The word it jumps over is not reached
  // and not planned.
  CHECK(planned({branch(8), kNop, kBlr}) && plan.size() == 2);
  CHECK(plan[0].operation == Operation::Jump && plan[0].next == 1 && plan[1].operation == Operation::Return);
  // With a branch into it the same word is planned.
  CHECK(planned({bc(12, 2, 8), branch(8), kNop, kBlr}) && plan.size() == 4 && plan[2].next == 3);
  // bdnz backward: CTR test, then the back-edge counter, then the exit taken every 1024th time.
  CHECK(planned({li(3, 5), mtspr(9, 3), addi(4, 4, 1), bc(16, 0, -4), kBlr}) && plan.size() == 7);
  CHECK(plan[3].operation == Operation::BranchCtrNonzero && plan[3].taken == 4 && plan[3].next == 6);
  CHECK(plan[4].operation == Operation::Backedge && plan[4].next == 2 && plan[4].taken == 5);
  CHECK(plan[5].operation == Operation::Exit && plan[5].immediate == 1 && plan[5].taken == 2);
  CHECK(plan[6].operation == Operation::Return);
  // bdnzt forward: the CTR test leads to the CR test; both fall through to the next instruction.
  CHECK(planned({dform(11, 0, 3, 0), bc(8, 2, 8), kNop, kBlr}) && plan.size() == 5);
  CHECK(plan[1].operation == Operation::BranchCtrNonzero && plan[1].taken == 2 && plan[1].next == 3);
  CHECK(plan[2].operation == Operation::BranchCrSet && plan[2].source == 0 && plan[2].immediate == 2 &&
        plan[2].taken == 4 && plan[2].next == 3);
  // bdz, and a CR bit of field 7 that must be clear.
  CHECK(planned({bc(18, 0, 8), kNop, bc(4, 31, 8), kNop, kBlr}) && plan.size() == 5);
  CHECK(plan[0].operation == Operation::BranchCtrZero && plan[0].taken == 2);
  CHECK(plan[2].operation == Operation::BranchCrClear && plan[2].source == 7 && plan[2].immediate == 1 && plan[2].taken == 4);
  // beqlr: taken reaches a Return of its own.
  CHECK(planned({bclr(12, 2), kBlr}) && plan.size() == 3);
  CHECK(plan[0].operation == Operation::BranchCrSet && plan[0].taken == 1 && plan[0].next == 2);
  CHECK(plan[1].operation == Operation::Return && plan[2].operation == Operation::Return);
  // A branch to itself is a back-edge. Two loops get exit numbers 1 and 2.
  CHECK(planned({branch(0)}) && plan.size() == 2 && plan[0].operation == Operation::Backedge &&
        plan[0].next == 0 && plan[0].taken == 1 && plan[1].taken == 0);
  CHECK(planned({bc(16, 0, 0), bc(16, 0, 0), kBlr}) && plan.size() == 7 && plan[2].immediate == 1 && plan[5].immediate == 2);
  // An unconditional backward b needs no Jump: the Backedge stencil is the branch.
  CHECK(planned({kNop, branch(-4)}) && plan.size() == 3 && plan[1].operation == Operation::Backedge && plan[1].next == 0);

  // Branches that leave the function are calls and tail calls now (ppc_leaf_call_test.cpp); what
  // is still refused here is a function that can run off its end, and the forms no stencil models.
  const std::vector<std::vector<uint32_t>> rejected = {
    {branch(8) | 1},                 // a call as the last instruction returns to nothing
    {bc(12, 2, 8)},                  // conditional tail call, then nothing
    {bclr(20, 0) | (1u << 11), kBlr},// bclr with a reserved field
    {xlform(16, 0, 0, 528), kBlr},   // bdnzctr
    {kNop, bc(12, 2, -4)},           // falls off the end when not taken
    {bc(16, 0, 0)},                  // bdnz to itself, then falls off
    {bclr(12, 2)},                   // conditional return with nothing after it
    {kNop},
    {branch(4), kNop},               // the jump lands on an instruction that falls off
  };
  for (const auto& words : rejected) {
    if (planned(words) || !plan.empty()) {
      std::printf("FAIL: function must be rejected:\n");
      for (uint32_t word : words) std::printf("  %08X\n", word);
      ++failures;
    }
  }
}

// Loops with results, back-edge counts and poll counts worked out by hand.
void known_loops() {
  struct Known { std::vector<uint32_t> words; uint32_t backedges_before, r3, backedge_count; uint64_t poll_count; };
  const Known cases[] = {
    // r3 += 3, ten times: nine taken back-edges.
    {{li(3, 0), li(4, 10), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr}, 0, 30, 9, 0},
    // The sixth back-edge makes the counter 0x400: exactly one poll.
    {{li(3, 0), li(4, 10), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr}, 0x3FA, 30, 9, 1},
    {{li(3, 0), li(4, 10), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr}, 0xFFFFFFFFu, 30, 9, 1}, // counter wraps to 0
    {{li(3, 0), li(4, 10), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr}, 0x3F6, 30, 9, 0},       // ends at 0x3FF
    // 5000 iterations: 4999 back-edges cross 1024, 2048, 3072 and 4096.
    {{li(3, 0), li(4, 5000), mtspr(9, 4), addi(3, 3, 1), bc(16, 0, -4), kBlr}, 0, 5000, 4999, 4},
    // Count up to 100 with cmpwi and blt.
    {{li(3, 0), addi(3, 3, 1), dform(11, 0, 3, 100), bc(12, 0, -8), kBlr}, 0, 100, 99, 0},
    // An unconditional backward b, left by beq: r3 counts 7 down to 0, six back-edges.
    {{li(3, 7), dform(13, 3, 3, 0xFFFF), bc(12, 2, 8), branch(-8), kBlr}, 0x3FE, 0, 6, 1},
    // One iteration: bdnz is not taken, so there is no back-edge at all.
    {{li(3, 0), li(4, 1), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr}, 0x3FF, 3, 0, 0},
    // Nested: 3 outer times 4 inner. Inner back-edges 3 * 3, outer 2.
    {{li(3, 0), li(28, 3), li(4, 4), mtspr(9, 4), addi(3, 3, 1), bc(16, 0, -4),
      dform(13, 28, 28, 0xFFFF), bc(4, 2, -20), kBlr}, 0, 12, 11, 0},
  };
  for (const auto& known : cases) {
    Function function;
    if (!function.build(known.words)) return;
    ppc::Context c{};
    c.backedges = known.backedges_before;
    const uint64_t tb = c.tb;
    reset_polls();
    CHECK(function.leaf.run(c, function.ram.data()));
    if (c.r[3] != known.r3 || c.backedges != known.backedges_before + known.backedge_count ||
        ppc::g_poll_count != known.poll_count || c.tb != tb + 977 * known.poll_count) {
      std::printf("FAIL: known loop: r3 %u (want %u), backedges %u (want %u), polls %llu (want %llu)\n", c.r[3], known.r3,
                  c.backedges - known.backedges_before, known.backedge_count,
                  (unsigned long long)ppc::g_poll_count, (unsigned long long)known.poll_count);
      function.dump();
      ++failures;
    }
    ppc::Context start{};
    start.backedges = known.backedges_before;
    function.compare(start, "known loop");
  }
}

// Every BO and BI through a forward bc, a backward bc and a bclr.
void every_condition(Random& random) {
  for (uint32_t bo = 0; bo < 32 && failures < 20; ++bo) {
    for (uint32_t bi = 0; bi < 32; ++bi) {
      Function forward, backward, returning;
      if (!forward.build({bc(bo, bi, 8), li(3, 1), kBlr})) return;
      // The backward branch at index 3 goes to index 1: taken once, never a loop.
      if (!backward.build({branch(12), li(3, 1), kBlr, bc(bo, bi, -8), li(4, 1), kBlr})) return;
      if (!returning.build({bclr(bo, bi), li(3, 1), kBlr})) return;
      for (int sample = 0; sample < 16; ++sample) {
        ppc::Context start;
        randomize(start, random);
        static const uint32_t counters[] = {0, 1, 2, 0xFFFFFFFFu};
        if (sample < 4) start.ctr = counters[sample];
        if (sample & 1) start.backedges = (random.u32() & ~0x3FFu) | 0x3FFu; // The next back-edge polls.
        if (sample & 2) for (auto& field : start.cr) field &= 15;
        if (!forward.compare(start, "forward bc") || !backward.compare(start, "backward bc") ||
            !returning.compare(start, "bclr")) return;
      }
    }
  }
}

// 10,000 random full contexts for each branch instruction on its own, CTR bounded per sample.
void each_branch(Random& random) {
  struct Case { const char* name; std::vector<uint32_t> words; uint32_t max_ctr; };
  const Case cases[] = {
    {"b forward", {branch(8), li(3, 1), kBlr}, 0},
    {"b backward", {xform(0, 4, 3, 8), dform(13, 28, 28, 0xFFFF), bc(12, 2, 8), branch(-12), kBlr}, 0},
    {"bdnz", {xform(3, 3, 4, 266), bc(16, 0, -4), kBlr}, 3000},
    {"bdz", {bc(18, 0, 8), li(3, 1), kBlr}, 0},
    {"bdnzt", {xform(2 << 2, 3, 4, 0), addi(3, 3, 1), bc(8, 9, -8), kBlr}, 200},
    {"bdnzf", {xform(2 << 2, 3, 4, 32), addi(3, 3, 1), bc(0, 10, -8), kBlr}, 200},
    {"bdzt", {bc(10, 5, 8), li(3, 1), kBlr}, 0},
    {"bne backward", {xform(5, 4, 3, 10), dform(13, 28, 28, 0xFFFF), bc(4, 2, -8), kBlr}, 0},
    {"beqlr", {dform(11, 3 << 2, 3, 0), bclr(12, 14), li(3, 1), kBlr}, 0},
    {"bdnzlr", {bclr(16, 0), li(3, 1), kBlr}, 0},
  };
  for (const auto& item : cases) {
    Function function;
    if (!function.build(item.words)) return;
    for (int sample = 0; sample < 10000; ++sample) {
      ppc::Context start;
      randomize(start, random);
      // Loops count r28 or CTR down; both get a small positive count, or CTR an edge value.
      start.r[28] = 1 + random.below(40);
      if (item.max_ctr) start.ctr = 1 + random.below(item.max_ctr);
      else if (random.below(4) == 0) start.ctr = random.below(3);
      if (sample & 1) start.backedges = (random.u32() & ~0x3FFu) | (0x3FFu - random.below(4));
      if (!function.compare(start, item.name)) return;
    }
  }
}

// ---- generated functions: every loop has a bounded trip count by construction ----
constexpr uint32_t kCounters = 0xF0000000u; // r28..r31 are loop counters; bodies never write them.
void straight(std::vector<uint32_t>& out, Random& random, uint32_t count) {
  while (count) {
    const Form& form = kForms[random.below(uint32_t(kFormCount))];
    if (form.kind == Kind::Mtspr && form.xo == 9) continue; // mtctr only where a loop sets it up
    const uint32_t word = random_word(form, random);
    const uint32_t written = written_register(form, word);
    if (written < 32 && ((kCounters >> written) & 1)) continue;
    out.push_back(word);
    --count;
  }
}
// A compare into a random field, and a BO/BI that tests one bit of that field without CTR.
void condition(std::vector<uint32_t>& out, Random& random, uint32_t& bo, uint32_t& bi) {
  static const char* const names[] = {"cmpw", "cmplw", "cmpwi", "cmplwi"};
  const char* name = names[random.below(4)];
  for (const Form& form : kForms) {
    if (std::strcmp(form.name, name)) continue;
    const uint32_t word = random_word(form, random);
    out.push_back(word);
    bo = (random.below(2) ? 12u : 4u) | random.below(2);
    bi = ((word >> 23) & 7) * 4 + random.below(4);
    return;
  }
}
void block(std::vector<uint32_t>& out, Random& random, uint32_t level, bool in_ctr_loop) {
  const uint32_t pieces = 1 + random.below(3);
  for (uint32_t piece = 0; piece < pieces; ++piece) {
    const uint32_t kind = random.below(level < 2 ? 8 : 3);
    const uint32_t counter = 28 + level;
    std::vector<uint32_t> body;
    uint32_t bo = 0, bi = 0;
    if (kind == 0) {
      straight(out, random, 1 + random.below(5));
    } else if (kind == 1) { // if: forward conditional branch over a block
      condition(out, random, bo, bi);
      block(body, random, level + 1, in_ctr_loop);
      out.push_back(bc(bo, bi, int32_t(4 * (body.size() + 1))));
      out.insert(out.end(), body.begin(), body.end());
    } else if (kind == 2) { // conditional early return
      condition(out, random, bo, bi);
      out.push_back(bclr(bo, bi));
    } else if (kind == 3) { // if and else: a forward b skips the else block
      std::vector<uint32_t> other;
      condition(out, random, bo, bi);
      block(body, random, level + 1, in_ctr_loop);
      block(other, random, level + 1, in_ctr_loop);
      out.push_back(bc(bo, bi, int32_t(4 * (body.size() + 2))));
      out.insert(out.end(), body.begin(), body.end());
      out.push_back(branch(int32_t(4 * (other.size() + 1))));
      out.insert(out.end(), other.begin(), other.end());
    } else if (kind <= 5 && !in_ctr_loop) { // CTR loop: bdnz, or bdnz combined with a CR bit
      block(body, random, level + 1, true);
      uint32_t loop_bo = 16;
      if (random.below(3) == 0) { condition(body, random, bo, bi); loop_bo = random.below(2) ? 8 : 0; }
      out.push_back(li(counter, 1 + random.below(40)));
      out.push_back(mtspr(9, counter));
      out.insert(out.end(), body.begin(), body.end());
      out.push_back(bc(loop_bo, bi, -int32_t(4 * body.size())));
    } else if (kind <= 6) { // register-counted loop closed by a conditional backward branch
      block(body, random, level + 1, in_ctr_loop);
      out.push_back(li(counter, 1 + random.below(20)));
      if (random.below(2)) {
        body.push_back(dform(13, counter, counter, 0xFFFF)); // addic. counter,counter,-1
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(bc(4, 2, -int32_t(4 * body.size())));  // bne
      } else {
        const uint32_t field = random.below(8);
        body.push_back(addi(counter, counter, 0xFFFF));
        body.push_back(dform(11, field << 2, counter, 0));   // cmpwi field,counter,0
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(bc(12, field * 4 + 1, -int32_t(4 * body.size()))); // bgt
      }
    } else { // loop closed by an unconditional backward b, left by a forward beq
      block(body, random, level + 1, in_ctr_loop);
      body.push_back(dform(13, counter, counter, 0xFFFF));
      body.push_back(bc(12, 2, 8));
      out.push_back(li(counter, 1 + random.below(20)));
      out.insert(out.end(), body.begin(), body.end());
      out.push_back(branch(-int32_t(4 * body.size())));
    }
  }
}
void generated(Random& random) {
  uint64_t longest = 0;
  for (int function_index = 0; function_index < 3000 && failures < 20; ++function_index) {
    std::vector<uint32_t> words;
    block(words, random, 0, false);
    if (random.below(3) == 0) { // at top level a forward branch may also decrement CTR
      std::vector<uint32_t> tail;
      static const uint32_t ctr_bo[] = {16, 18, 0, 2, 8, 10};
      straight(tail, random, 1 + random.below(3));
      words.push_back(bc(ctr_bo[random.below(6)], random.below(32), int32_t(4 * (tail.size() + 1))));
      words.insert(words.end(), tail.begin(), tail.end());
    }
    words.push_back(kBlr);
    if (words.size() > 900) continue;
    if (words.size() > longest) longest = words.size();
    Function function;
    if (!function.build(words, ppc::RAM_BASE + 4 * random.below(32))) return;
    for (int sample = 0; sample < 16; ++sample) {
      ppc::Context start;
      randomize(start, random);
      if (sample & 1) start.backedges = (random.u32() & ~0x3FFu) | (0x3FFu - random.below(8));
      if (!function.compare(start, "generated function")) return;
    }
  }
  std::printf("leaf branch: longest generated function %llu instructions\n", (unsigned long long)longest);
}

// The comparison sees a poll that is missing, extra or misplaced, and a lost back-edge.
void controls() {
  Function function;
  if (!function.build({li(3, 0), li(4, 10), mtspr(9, 4), addi(3, 3, 3), bc(16, 0, -4), kBlr})) return;
  ppc::Context native{}, expected{};
  native.backedges = 0x3FA; expected.backedges = 0x3F9; // One back-edge apart: the poll moves.
  reset_polls();
  CHECK(function.leaf.run(native, function.ram.data()));
  const Polls native_polls = polls();
  reset_polls();
  CHECK(run(expected, function.words, function.address));
  const Polls expected_polls = polls();
  CHECK(native_polls.count == 1 && expected_polls.count == 1 && native_polls.digest != expected_polls.digest);
  CHECK(!same(native, expected));
  // Stale code is still refused once branches exist.
  ppc::Context untouched{};
  function.ram[3] ^= 1;
  CHECK(!function.leaf.run(untouched, function.ram.data()));
}

// A table whose branch or exit stencil has another shape is refused before any code is copied.
void tables() {
  using namespace ppc::stencil;
  const auto code = bytes({li(4, 2), mtspr(9, 4), bc(16, 0, 0), kBlr});
  std::vector<Stencil> stencils(generated::table.stencils, generated::table.stencils + generated::table.count);
  auto table = generated::table;
  table.stencils = stencils.data();
  CompiledLeaf leaf;
  std::string error;
  CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) && leaf.ready());
  const auto index = [](Operation operation) { return static_cast<size_t>(operation); };
  const auto refused = [&](const char* what) {
    if (translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) || leaf.ready()) {
      std::printf("FAIL: table must be refused: %s\n", what);
      ++failures;
    }
  };
  // Exit: only mov eax,[rip+cell] and a ret with no stack pop.
  const Relocation exit_hole[] = {{2, Hole::Immediate, 0, 0}};
  const Relocation exit_biased[] = {{2, Hole::Immediate, 0, 1}};
  const Relocation exit_other[] = {{2, Hole::Source, 0, 0}};
  const uint8_t exit_plain[] = {0x8B,0x05,0,0,0,0,0xC3};
  const uint8_t exit_zero_pop[] = {0x8B,0x05,0,0,0,0,0xC2,0,0};
  const uint8_t exit_pop[] = {0x8B,0x05,0,0,0,0,0xC2,8,0};
  const uint8_t exit_trailing[] = {0x8B,0x05,0,0,0,0,0xC3,0x90};
  const uint8_t exit_other_register[] = {0x8B,0x0D,0,0,0,0,0xC3};
  const Stencil original_exit = stencils[index(Operation::Exit)];
  for (const auto& shape : {std::pair<const uint8_t*, size_t>{exit_plain, sizeof exit_plain},
                            {exit_zero_pop, sizeof exit_zero_pop}}) {
    stencils[index(Operation::Exit)] = {Operation::Exit, shape.first, shape.second, exit_hole, 1};
    CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) && leaf.ready());
  }
  for (const auto& shape : {std::pair<const uint8_t*, size_t>{exit_pop, sizeof exit_pop},
                            {exit_trailing, sizeof exit_trailing}, {exit_other_register, sizeof exit_other_register}}) {
    stencils[index(Operation::Exit)] = {Operation::Exit, shape.first, shape.second, exit_hole, 1};
    refused("exit shape");
  }
  stencils[index(Operation::Exit)] = {Operation::Exit, exit_plain, sizeof exit_plain, exit_biased, 1};
  refused("exit bias");
  stencils[index(Operation::Exit)] = {Operation::Exit, exit_plain, sizeof exit_plain, exit_other, 1};
  refused("exit hole");
  stencils[index(Operation::Exit)] = {Operation::Exit, exit_plain, sizeof exit_plain, nullptr, 0};
  refused("exit without its number");
  stencils[index(Operation::Exit)] = original_exit;

  // Branch: je over a jmp Taken, then jmp Next, and the conditional-jump layout.
  const uint8_t two_jumps[] = {0x74,0x05, 0xE9,0,0,0,0, 0xE9,0,0,0,0};
  const Relocation two_jump_holes[] = {{3, Hole::Taken, 0, 0}, {8, Hole::Next, 0, 0}};
  const uint8_t conditional[] = {0x0F,0x85,0,0,0,0, 0xE9,0,0,0,0};
  const Relocation conditional_holes[] = {{2, Hole::Taken, 0, 0}, {7, Hole::Next, 0, 0}};
  const uint8_t call_taken[] = {0x74,0x05, 0xE8,0,0,0,0, 0xE9,0,0,0,0};
  const uint8_t padded[] = {0x74,0x05, 0xE9,0,0,0,0, 0xE9,0,0,0,0, 0xCC};
  const uint8_t ends_conditional[] = {0xE9,0,0,0,0, 0x0F,0x85,0,0,0,0};
  const Relocation ends_conditional_holes[] = {{1, Hole::Taken, 0, 0}, {7, Hole::Next, 0, 0}};
  const Relocation only_next[] = {{3, Hole::Next, 0, 0}, {8, Hole::Next, 0, 0}};
  const Relocation with_operand[] = {{3, Hole::Taken, 0, 0}, {8, Hole::Next, 0, 0}, {0, Hole::Source, 0, 0}};
  const Stencil original_branch = stencils[index(Operation::BranchCtrNonzero)];
  const auto branch_stencil = [&](const uint8_t* bytes, size_t size, const Relocation* holes, size_t count) {
    stencils[index(Operation::BranchCtrNonzero)] = {Operation::BranchCtrNonzero, bytes, size, holes, count};
  };
  // These two only have to pass the table check: their bytes are synthetic and never run.
  branch_stencil(two_jumps, sizeof two_jumps, two_jump_holes, 2);
  CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) && leaf.ready());
  branch_stencil(conditional, sizeof conditional, conditional_holes, 2);
  CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) && leaf.ready());
  branch_stencil(call_taken, sizeof call_taken, two_jump_holes, 2);
  refused("Taken through a call");
  branch_stencil(padded, sizeof padded, two_jump_holes, 2);
  refused("padding after the final jump");
  branch_stencil(ends_conditional, sizeof ends_conditional, ends_conditional_holes, 2);
  refused("final jump is conditional");
  branch_stencil(two_jumps, sizeof two_jumps, only_next, 2);
  refused("branch without Taken");
  branch_stencil(two_jumps, sizeof two_jumps, with_operand, 3);
  refused("overlapping holes");
  stencils[index(Operation::BranchCtrNonzero)] = original_branch;
  // A linear stencil may not jump conditionally to Next, nor reference Taken.
  const Stencil original_jump = stencils[index(Operation::Jump)];
  stencils[index(Operation::Jump)] = {Operation::Jump, conditional, sizeof conditional, conditional_holes, 2};
  refused("linear stencil with Taken");
  const Relocation conditional_next[] = {{2, Hole::Next, 0, 0}, {7, Hole::Next, 0, 0}};
  stencils[index(Operation::Jump)] = {Operation::Jump, conditional, sizeof conditional, conditional_next, 2};
  refused("linear stencil with a conditional Next");
  stencils[index(Operation::Jump)] = original_jump;
  CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, leaf, error) && leaf.ready());
}
} // namespace

int main() {
  Random random(0x2026100100000004ull);
  plans();
  tables();
  known_loops();
  controls();
  const uint64_t fixed = comparisons;
  if (!failures) every_condition(random);
  const uint64_t condition_total = comparisons - fixed;
  if (!failures) each_branch(random);
  const uint64_t each_total = comparisons - fixed - condition_total;
  if (!failures) generated(random);
  const uint64_t generated_total = comparisons - fixed - condition_total - each_total;
  if (!failures)
    std::printf("leaf branch: plan and table checks, known loops, %llu comparisons over every BO and BI (forward bc, "
                "backward bc, bclr), %llu over 10 branch instructions (10000 random contexts each), %llu over 3000 "
                "generated functions with bounded loops; "
                "%llu back-edges taken, %llu runs polled, %llu polls matched in count and context\n",
                (unsigned long long)condition_total, (unsigned long long)each_total, (unsigned long long)generated_total,
                (unsigned long long)total_backedges, (unsigned long long)polled_runs, (unsigned long long)total_polls);
  return failures ? 1 : 0;
}
