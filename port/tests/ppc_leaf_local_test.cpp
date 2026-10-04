// Local calls, inline data, blrl and computed returns: the statements of emit.py that keep state
// per invocation (`lrs`, `lrn`, `entry_lr`) and the resume test after a call of a callee that
// can return past its call. Translated native code versus the reference in ppc_leaf_reference.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "ppc_leaf_function.h"

namespace {
using namespace reference;
using ppc::stencil::ExitKind;
using ppc::stencil::Operation;
using worlds::Function;
using worlds::failures;

constexpr uint32_t kNop = 0x60000000u;
constexpr uint32_t kHost = 0x80400000u;
constexpr uint32_t kA = ppc::RAM_BASE + 0x3000;
uint32_t li(uint32_t rd, uint32_t value) { return dform(14, rd, 0, value); }
uint32_t addi(uint32_t rd, uint32_t ra, uint32_t value) { return dform(14, rd, ra, value); }
uint32_t bl(int32_t offset) { return branch(offset) | 1; }
uint32_t lwz(uint32_t rd, uint32_t ra, uint32_t d) { return dform(32, rd, ra, d); }
uint32_t mflr(uint32_t rd) { return mfspr(rd, 8); }
uint32_t mtlr(uint32_t rs) { return mtspr(8, rs); }
uint64_t known_total = 0;

const worlds::Result& native(Function& function, const ppc::Context& start) {
  worlds::install(function.address, function.code);
  worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return function.leaf.run(c, m); });
  ++known_total;
  return worlds::g_world[0].result;
}

// The planner's view: what is reached, what is data, where a local call returns.
void plans() {
  ppc::stencil::Plan plan;
  std::string error;
  const auto planned = [&](const std::vector<uint32_t>& words, const std::vector<ppc::stencil::Callee>* callees = nullptr) {
    const auto code = bytes(words);
    return ppc::stencil::plan_function(code.data(), code.size(), kA, plan, error, nullptr, callees);
  };
  // bl over two data words, then mflr: the data is not planned, and nothing returns into it.
  CHECK(planned({mflr(0), bl(12), 0x11223344u, 0x00000000u, mflr(3), mtlr(0), kBlr}));
  CHECK(plan.has_local_calls && plan.unreachable == 2 && plan.local_returns.size() == 1 &&
        plan.local_returns[0].address == kA + 8 && plan.local_returns[0].stencil == ppc::stencil::kNoStencil);
  CHECK(plan.stencils.size() == 6 && plan.stencils[1].operation == Operation::SetLr && plan.stencils[1].immediate == kA + 8 &&
        plan.stencils[2].operation == Operation::Exit && plan.stencils[2].exit == ExitKind::LocalCall &&
        plan.stencils[2].exit_value == kA + 8 && plan.stencils[2].taken == 3 && plan.stencils[3].operation == Operation::Mflr);
  // In a function with local calls every return leaves through the driver.
  CHECK(plan.stencils[5].operation == Operation::Exit && plan.stencils[5].exit == ExitKind::LocalReturn &&
        plan.stencils[5].taken == ppc::stencil::kNoStencil);
  // The data may be anything: words that decode, words that do not, words the planner refuses.
  CHECK(planned({mflr(0), bl(20), 0x44000002u, 0xFFFFFFFFu, 0x7C0004ACu | 1, 0x4E800021u, mflr(3), mtlr(0), kBlr}) && plan.unreachable == 4);
  // bl +4, mflr: the return address is the next instruction, which is code.
  CHECK(planned({bl(4), mflr(3), kBlr}) && plan.unreachable == 0 && plan.local_returns.size() == 1 &&
        plan.local_returns[0].stencil == 2);
  // A local subroutine: the words after the call are code and the return goes there.
  CHECK(planned({bl(12), addi(3, 3, 1), kBlr, addi(3, 3, 10), kBlr}) && plan.unreachable == 0 &&
        plan.local_returns.size() == 1 && plan.local_returns[0].address == kA + 4 && plan.local_returns[0].stencil == 2);
  // Without a leading mflr at the target the words after a forward bl are code and must plan.
  CHECK(!planned({bl(12), 0x00000000u, 0x00000000u, addi(3, 3, 10), kBlr}));
  CHECK(error.find(ppc::stencil::refusal_text(ppc::stencil::Refusal::NotDecoded)) != std::string::npos);
  // A conditional bcl over data: the fall-through is the data, which is reached.
  CHECK(!planned({bc(12, 2, 12) | 1, 0x00000000u, 0x00000000u, mflr(3), kBlr}));
  // blrl without local calls: the driver compares with the entry LR; a plain blr stays a Return.
  CHECK(planned({0x4E800021u, kBlr}) && !plan.has_local_calls && plan.stencils.size() == 2 &&
        plan.stencils[0].operation == Operation::Exit && plan.stencils[0].exit == ExitKind::LinkReturn &&
        plan.stencils[0].exit_value == kA + 4 && plan.stencils[0].taken == 1 && plan.stencils[1].operation == Operation::Return);
  // beqlrl: the test, then the exit.
  CHECK(planned({bclr(12, 2) | 1, kBlr}) && plan.stencils[0].operation == Operation::BranchCrSet &&
        plan.stencils[0].taken == 1 && plan.stencils[0].next == 2 && plan.stencils[1].exit == ExitKind::LinkReturn);
  // A tail call in a function with local calls is the driver's too.
  CHECK(planned({bl(8), branch(0x1000), kBlr}) && plan.stencils[2].operation == Operation::Exit &&
        plan.stencils[2].exit == ExitKind::TailCall && plan.stencils[2].exit_value == kA + 0x1004);
  CHECK(planned({bl(8), bcctr(20, 0), kBlr}) && plan.stencils[2].exit == ExitKind::TailCallCtr);
  // A local call as the last word would return past the end.
  CHECK(!planned({kNop, bl(-4)}) && error == ppc::stencil::refusal_text(ppc::stencil::Refusal::FallsOffEnd));
  // Computed returns: the counted call and one test per delta that lands inside the function.
  const std::vector<ppc::stencil::Callee> callees = {{kHost, {8, 4, 0x100}}, {kHost + 4, {}}, {kHost + 8, {8}}};
  CHECK(planned({bl(int32_t(kHost - kA)), kNop, kNop, kNop, kBlr}, &callees) && plan.stencils.size() == 7);
  CHECK(plan.stencils[0].operation == Operation::CallChecked && plan.stencils[0].immediate == kA + 4 &&
        plan.stencils[0].immediate2 == kHost && plan.stencils[0].next == 1);
  CHECK(plan.stencils[1].operation == Operation::ResumeTest && plan.stencils[1].immediate == kA + 8 &&
        plan.stencils[1].taken == 4 && plan.stencils[1].next == 2);
  CHECK(plan.stencils[2].operation == Operation::ResumeTest && plan.stencils[2].immediate == kA + 12 &&
        plan.stencils[2].taken == 5 && plan.stencils[2].next == 3);
  // No delta inside the function, or a callee without any: the plain call, uncounted.
  CHECK(planned({bl(int32_t(kHost - kA)), kBlr}, &callees) && plan.stencils[0].operation == Operation::Call);
  CHECK(planned({bl(int32_t(kHost + 4 - kA)), kNop, kNop, kBlr}, &callees) && plan.stencils[0].operation == Operation::Call);
  // The address a callee may resume at is reached even when nothing else reaches it.
  CHECK(planned({bl(int32_t(kHost + 8 - kA)), kBlr, 0x00000000u, kBlr}, &callees) && plan.unreachable == 1 &&
        plan.stencils[1].operation == Operation::ResumeTest && plan.stencils[1].taken == 3);
  // analyze.py's rule for a computed return, on the words of a callee.
  const auto deltas = [](const std::vector<uint32_t>& words) {
    const auto code = bytes(words);
    return ppc::stencil::computed_returns(code.data(), code.size());
  };
  CHECK(deltas({lwz(12, 1, 4), addi(12, 12, 8), mtlr(12), kBlr}) == std::vector<uint32_t>{8});
  CHECK(deltas({lwz(0, 1, 36), addi(0, 0, 4), addi(1, 1, 32), addi(0, 0, 4), mtlr(0), kBlr}) == std::vector<uint32_t>{8});
  CHECK(deltas({lwz(12, 1, 4), mtlr(12), kBlr}).empty());                      // an ordinary return
  CHECK(deltas({lwz(12, 1, 4), addi(12, 12, 8), mtlr(12), kNop, bc(12, 2, 8), kNop, kBlr}).empty()); // a branch in between
  CHECK(deltas({lwz(12, 3, 4), addi(12, 12, 8), mtlr(12), kBlr}).empty());     // not loaded off the stack
  CHECK(deltas({lwz(12, 1, 4), addi(12, 12, 0xFFF8), mtlr(12), kBlr}).empty()); // backward
  CHECK(deltas({lwz(12, 1, 4), addi(12, 12, 6), mtlr(12), kBlr}).empty());     // not a whole instruction
  CHECK(deltas({lwz(12, 1, 4), addi(12, 12, 8), mtlr(12), bclr(12, 2), lwz(12, 1, 4), addi(12, 12, 16), mtlr(12), kBlr}) ==
        (std::vector<uint32_t>{8, 16}));
}

// Results worked out by hand from the PowerPC definitions and emit.py.
void known_answers() {
  { // The inline data idiom: mflr gives the guest address of the data, and the data is read from RAM.
    Function f;
    if (!f.translate({mflr(0), bl(12), 0x11223344u, 0x3F800000u, mflr(3), lwz(4, 3, 0), dform(48, 1, 3, 4), mtlr(0), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[3] == kA + 8 && r.context.r[4] == 0x11223344u && r.context.f[1].ps0 == 1.0 &&
          r.context.lr == 0x80001234u && r.events.count == 0 && r.fatals == 0);
    f.compare(c, "inline data");
  }
  { // bl +4 and bcl 20,31,+4 read the address of the next instruction.
    Function f, g;
    if (!f.translate({mflr(0), bl(4), mflr(3), mtlr(0), kBlr}, kA) || !g.translate({mflr(0), bc(20, 31, 4) | 1, mflr(3), mtlr(0), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    CHECK(native(f, c).context.r[3] == kA + 8 && worlds::g_world[0].result.ok);
    CHECK(native(g, c).context.r[3] == kA + 8 && worlds::g_world[0].result.ok);
    f.compare(c, "bl +4"); g.compare(c, "bcl +4");
  }
  { // Returning into inline data is not something a translation can do: it stops and says so.
    Function f;
    if (!f.translate({bl(12), 0x11223344u, 0x00000000u, mflr(3), kBlr}, kA)) return;
    ppc::Context c{};
    const auto& r = native(f, c);
    CHECK(!r.ok && r.fatals == 1 && r.context.r[3] == kA + 4 &&
          std::strcmp(host::g_fatal_what, "translated code returned into inline data") == 0);
  }
  { // A local subroutine and its return.
    Function f;
    if (!f.translate({mflr(27), li(3, 1), bl(16), addi(3, 3, 100), mtlr(27), kBlr, addi(3, 3, 10), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[3] == 111 && r.context.lr == 0x80001234u && r.events.count == 0 && r.context.call_depth == 0);
    f.compare(c, "local subroutine");
  }
  { // A subroutine that calls another and saves its own return address.
    Function f;
    if (!f.translate({mflr(27), li(3, 0), bl(20), bl(36), addi(3, 3, 1000), mtlr(27), kBlr,
                      /* sub1 */ mflr(26), addi(3, 3, 1), bl(12), mtlr(26), kBlr,
                      /* sub2 */ addi(3, 3, 10), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    // sub1 (1), sub2 from sub1 (10), sub2 from the main body (10), then 1000.
    CHECK(r.ok && r.context.r[3] == 1021 && r.context.lr == 0x80001234u);
    f.compare(c, "nested local subroutines");
  }
  { // A return whose LR is not a local return address leaves the function, even inside a subroutine.
    Function f;
    if (!f.translate({li(3, 1), bl(12), li(3, 99), kBlr, mtlr(5), kBlr}, kA)) return;
    ppc::Context c{};
    c.r[5] = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[3] == 1 && r.context.lr == 0x80001234u);
    f.compare(c, "return out of a subroutine");
  }
  { // A conditional local call: not taken leaves LR alone and remembers nothing.
    Function f;
    if (!f.translate({mflr(27), bc(12, 2, 16) | 1, addi(3, 3, 100), mtlr(27), kBlr, addi(3, 3, 10), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    CHECK(native(f, c).context.r[3] == 100 && worlds::g_world[0].result.ok);
    c.cr[0] = 2;
    CHECK(native(f, c).context.r[3] == 110 && worlds::g_world[0].result.ok);
    f.compare(c, "conditional local call");
  }
  { // A subroutine that ends in a tail call returns to its local caller after the callee.
    Function f;
    if (!f.translate({mflr(27), bl(16), li(5, 7), mtlr(27), kBlr, branch(int32_t(kHost - (kA + 20)))}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[5] == 7 && r.events.count == 1 && r.context.lr == 0x80001234u);
    f.compare(c, "tail call in a subroutine");
  }
  { // The local return is decided on the LR from before the tail call: here the callee moves LR
    // (r3 is odd, so the stand-in adds 8), and the subroutine still returns to its local caller.
    Function f;
    if (!f.translate({mflr(27), bl(16), li(5, 7), mtlr(27), kBlr, branch(int32_t(host::kResumeBase - (kA + 20)))}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u; c.r[3] = 1;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[5] == 7 && r.events.count == 1 && r.context.lr == 0x80001234u);
    f.compare(c, "tail call to a callee that moves LR");
  }
  { // The same tail call from the main body leaves the function.
    Function f;
    if (!f.translate({mflr(27), bl(16), mtlr(27), branch(int32_t(kHost - (kA + 12))), li(5, 7), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[5] == 0 && r.events.count == 1);
    f.compare(c, "tail call after a local call");
  }
  { // blrl with the LR the function was entered with: a return that leaves a new LR.
    Function f;
    if (!f.translate({0x4E800021u, li(3, 1), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[3] == 0 && r.context.lr == kA + 4 && r.events.count == 0);
    f.compare(c, "blrl as a return");
  }
  { // blrl with any other LR: a call through the dispatch, then on.
    Function f;
    if (!f.translate({mtlr(5), 0x4E800021u, li(6, 1), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u; c.r[5] = kHost;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[6] == 1 && r.context.lr == kA + 8 && r.events.count == 1);
    f.compare(c, "blrl as a call");
  }
  { // bl x; x: blrl. The blrl first returns locally to itself, then calls what follows it through
    // the dispatch with LR pointing there, and execution continues there: the helper-table idiom.
    Function f;
    if (!f.translate({mflr(27), bl(4), 0x4E800021u, li(6, 1), mtlr(27), kBlr}, kA)) return;
    ppc::Context c{};
    c.lr = 0x80001234u;
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[6] == 1 && r.events.count == 1 && r.context.lr == 0x80001234u);
    f.compare(c, "bl to a blrl");
  }
  { // Only the last 32 local return addresses are remembered (ppc::local_return). 34 local calls
    // that never return, then a return to the first one's address: forgotten, so the function
    // returns. A return to the third one's address is still known: execution goes back there, makes
    // 31 more calls, and the second time that address has been popped, so the function returns.
    std::vector<uint32_t> words(34, bl(4));
    words.insert(words.end(), {addi(6, 6, 1), mtlr(5), kBlr});
    Function f;
    if (!f.translate(words, kA)) return;
    ppc::Context c{};
    c.r[5] = kA + 4;
    CHECK(native(f, c).context.r[6] == 1 && worlds::g_world[0].result.ok);
    f.compare(c, "forgotten local return");
    c.r[5] = kA + 12;
    CHECK(native(f, c).context.r[6] == 2 && worlds::g_world[0].result.ok);
    f.compare(c, "remembered local return");
  }
  { // A callee that returns past its call: the caller resumes where the callee asked.
    Function f;
    f.resumes = {{host::kResumeBase, 8}};
    if (!f.translate({bl(int32_t(host::kResumeBase - kA)), li(5, 1), li(6, 1), li(7, 1), kBlr}, kA)) return;
    ppc::Context c{};
    c.r[3] = 1; // Odd: the stand-in callee adds 8 to LR.
    const auto& r = native(f, c);
    CHECK(r.ok && r.context.r[5] == 0 && r.context.r[6] == 0 && r.context.r[7] == 1 && r.resume_checks == 1 && r.resumed == 1);
    f.compare(c, "resumed return");
    c.r[3] = 2;
    const auto& plain = native(f, c);
    CHECK(plain.ok && plain.context.r[5] == 1 && plain.context.r[6] == 1 && plain.resume_checks == 1 && plain.resumed == 0);
    f.compare(c, "ordinary return of a callee with computed returns");
  }
}

// ---- generated functions with local structure; every one ends ----
constexpr uint32_t kSaved = 0x0F000000u; // r24..r27 hold saved return addresses.
void straight(std::vector<uint32_t>& out, Random& random, uint32_t count) {
  while (count) {
    const uint32_t pick = random.below(6);
    uint32_t word = 0, written = 32, written2 = 32;
    if (pick == 0) {
      const WideForm& form = kMemoryForms[random.below(uint32_t(kMemoryFormCount))];
      word = random_word(form, random);
      // Loads write their first field, update forms their second; lmw writes a run of registers.
      if (form.kind == Wide::Lmw) continue;
      if ((word >> 26) != 4 && (word >> 26) < 48) written = (word >> 21) & 31;
      written2 = (word >> 16) & 31;
    } else if (pick == 1) {
      word = random_word(kFloatForms[random.below(uint32_t(kFloatFormCount))], random);
    } else {
      const Form& form = kForms[random.below(uint32_t(kFormCount))];
      if (form.kind == Kind::Mtspr) continue; // LR and CTR are set only where the generator means to
      word = random_word(form, random);
      written = written_register(form, word);
    }
    if ((written < 32 && ((kSaved >> written) & 1)) || (written2 < 32 && ((kSaved >> written2) & 1))) continue;
    out.push_back(word);
    --count;
  }
}
uint32_t data_word(Random& random) {
  static const uint32_t fixed[] = {0x00000000u, 0x00000001u, 0x3F800000u, 0xC0000000u, 0xFFFFFFFFu, 0x00FF00FFu};
  return random.below(2) ? fixed[random.below(6)] : (random.u32() & 0x03FFFFFFu); // primary opcode 0: never an instruction
}
struct Generated { std::vector<uint32_t> words; std::vector<std::pair<uint32_t, uint32_t>> resumes; };
Generated generate(Random& random, uint32_t address) {
  // Three subroutines after the main body. The first may call the others. The main body saves LR
  // in r27 and the subroutines in r26, r25 and r24, as code that uses LR for anything else must.
  // Calls are patched once the addresses are known.
  struct Patch { size_t at; uint32_t sub; uint32_t bo, bi; bool conditional; };
  Generated out;
  auto& words = out.words;
  std::vector<Patch> patches;
  const auto host_target = [&] { return kHost + 4 * random.below(64); };
  const auto local_call = [&](uint32_t first_sub) {
    patches.push_back({words.size(), first_sub + random.below(3 - first_sub), random.below(2) ? 12u : 4u, random.below(32), random.below(4) == 0});
    words.push_back(0);
  };
  const auto piece = [&](uint32_t first_sub) {
    const uint32_t here = address + 4 * uint32_t(words.size());
    switch (random.below(10)) {
      case 0: case 1: if (first_sub < 3) local_call(first_sub); break;
      case 2: { // bl over inline data, mflr, and usually a read of the data
        const uint32_t count = 1 + random.below(4), reg = 3 + random.below(8);
        words.push_back(bl(int32_t(4 * (count + 1))));
        for (uint32_t i = 0; i < count; ++i) words.push_back(data_word(random));
        words.push_back(mflr(reg));
        if (random.below(3)) words.push_back(lwz(3 + random.below(8), reg, 4 * random.below(count)));
        break;
      }
      case 3: words.push_back(random.below(2) ? bl(4) : (bc(20, 31, 4) | 1)); words.push_back(mflr(3 + random.below(8))); break;
      case 4: words.push_back(bl(int32_t(host_target() - here))); break;
      case 5: { // a callee with a computed return; the two words after the call can be skipped
        const uint32_t target = host::kResumeBase + 4 * random.below(host::kResumeCount);
        out.resumes.push_back({target, host::resume_delta(target)});
        words.push_back(bl(int32_t(target - here)));
        straight(words, random, 3);
        break;
      }
      case 6: { // blrl to a host function
        const uint32_t target = host_target();
        words.push_back(dform(15, 12, 0, target >> 16)); words.push_back(dform(24, 12, 12, target & 0xFFFF));
        words.push_back(mtlr(12)); words.push_back(random.below(3) ? 0x4E800021u : (bclr(random.below(2) ? 12 : 4, random.below(32)) | 1));
        break;
      }
      default: straight(words, random, 1 + random.below(4)); break;
    }
  };
  words.push_back(mflr(27));
  for (uint32_t n = 1 + random.below(8); n; --n) piece(0);
  words.push_back(mtlr(27));
  const uint32_t ending = random.below(6);
  if (ending == 0) words.push_back(branch(int32_t(host_target() - (address + 4 * uint32_t(words.size())))));
  else if (ending == 1) {
    const uint32_t target = host_target();
    words.push_back(dform(15, 12, 0, target >> 16)); words.push_back(dform(24, 12, 12, target & 0xFFFF));
    words.push_back(mtspr(9, 12)); words.push_back(bcctr(20, 0));
  } else if (ending == 2) { words.push_back(0x4E800021u); words.push_back(kBlr); } // blrl with the entry LR: a return
  else words.push_back(kBlr);
  if (random.below(3) == 0) for (uint32_t n = 1 + random.below(3); n; --n) words.push_back(data_word(random)); // a data pool
  uint32_t subs[3];
  for (uint32_t sub = 0; sub < 3; ++sub) {
    subs[sub] = uint32_t(words.size());
    const bool nests = sub == 0 && random.below(2);
    words.push_back(mflr(26 - sub));
    for (uint32_t n = random.below(4); n; --n) piece(nests ? 1 : 3);
    straight(words, random, 1 + random.below(3));
    words.push_back(mtlr(26 - sub));
    // A subroutine returns, or tail-calls a host function (and so returns to its local caller).
    // Some of those callees move LR, which must not change where the subroutine returns.
    if (random.below(4) == 0) {
      const uint32_t target = random.below(2) ? host::kResumeBase + 4 * random.below(host::kResumeCount) : host_target();
      words.push_back(branch(int32_t(target - (address + 4 * uint32_t(words.size())))));
    }
    else words.push_back(kBlr);
  }
  for (const Patch& patch : patches) {
    const int32_t offset = int32_t(4 * (subs[patch.sub] - uint32_t(patch.at)));
    words[patch.at] = patch.conditional ? (bc(patch.bo, patch.bi, offset) | 1) : bl(offset);
  }
  // The planner is told every delta of a callee; keep one entry per callee.
  std::sort(out.resumes.begin(), out.resumes.end());
  out.resumes.erase(std::unique(out.resumes.begin(), out.resumes.end()), out.resumes.end());
  return out;
}
uint64_t local_calls_made = 0, resumed_total = 0, checks_total = 0;
void generated(Random& random) {
  for (int index = 0; index < 4000 && failures < 20; ++index) {
    const uint32_t address = kA + 4 * random.below(64);
    Generated made = generate(random, address);
    Function function;
    function.resumes = made.resumes;
    if (!function.translate(made.words, address)) return;
    if ((index & 31) == 0) worlds::watch(random, 3);
    for (int sample = 0; sample < 12; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!function.compare(start, "generated local function", true)) return;
      resumed_total += worlds::g_world[0].result.resumed;
      checks_total += worlds::g_world[0].result.resume_checks;
      if (worlds::g_world[1].result.fatals || worlds::g_world[0].result.fatals) {
        std::printf("FAIL: a generated function returned into its data\n"); function.dump(); ++failures; return;
      }
    }
    for (uint32_t word : made.words) local_calls_made += ((word >> 26) == 18 || (word >> 26) == 16) && (word & 1);
  }
}

// The comparison can fail: a forgotten local return, a wrong LR and a missed resume are seen.
void controls(Random& random) {
  Function f;
  if (!f.translate({mflr(27), li(3, 1), bl(16), addi(3, 3, 100), mtlr(27), kBlr, addi(3, 3, 10), kBlr}, kA)) return;
  const auto differs = [&](Function& native_function, const std::vector<uint32_t>& other,
                           const std::vector<std::pair<uint32_t, uint32_t>>* resumes, const char* expect) {
    ppc::Context start;
    randomize(start, random);
    start.r[3] = 1;
    worlds::install(native_function.address, native_function.code);
    worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return native_function.leaf.run(c, m); });
    worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, other, kA, 1u << 22, resumes); });
    const char* what = worlds::difference();
    if (!what || std::strcmp(what, expect)) {
      std::printf("FAIL: control expected a difference in %s, saw %s\n", expect, what ? what : "none");
      ++failures;
    }
  };
  // No call at all: the subroutine's work is missing.
  differs(f, {mflr(27), li(3, 1), kNop, addi(3, 3, 100), mtlr(27), kBlr, addi(3, 3, 10), kBlr}, nullptr, "context");
  // A jump in place of the local call: the subroutine's blr leaves the function.
  differs(f, {mflr(27), li(3, 1), branch(16), addi(3, 3, 100), mtlr(27), kBlr, addi(3, 3, 10), kBlr}, nullptr, "context");
  // A resume that is not taken: the native code was planned without the callee's deltas.
  const std::vector<std::pair<uint32_t, uint32_t>> resumes = {{host::kResumeBase, 8}};
  Function plain;
  if (!plain.translate({bl(int32_t(host::kResumeBase - kA)), li(5, 1), li(6, 1), li(7, 1), kBlr}, kA)) return;
  differs(plain, plain.words, &resumes, "context");
}
} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Random random(0x2026100400000001ull);
  if (!worlds::init(2, random)) return 1;
  plans();
  known_answers();
  controls(random);
  const uint64_t fixed = worlds::comparisons;
  if (!failures) generated(random);
  if (!failures)
    std::printf("leaf local: plan checks, %llu known answers, %llu comparisons over 4000 generated functions with local "
                "subroutines, inline data, blrl and callees with computed returns (%llu local call sites, %llu resume "
                "tests run, %llu resumed past the call; %llu samples set aside for a NaN result the compiler chooses)\n",
                (unsigned long long)known_total, (unsigned long long)(worlds::comparisons - fixed),
                (unsigned long long)local_calls_made, (unsigned long long)checks_total, (unsigned long long)resumed_total,
                (unsigned long long)worlds::set_aside);
  return failures ? 1 : 0;
}
