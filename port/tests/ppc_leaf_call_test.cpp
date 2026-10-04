// Control flow beyond a leaf: bl, bcl, bctrl, tail branches and bctr through ppc::call, calls
// between translated functions, and the unwind data of call-capable stencils: the system can walk
// the stack from under translated code, and a C++ exception thrown by a host function (a guest
// longjmp) crosses translated frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "ppc_leaf_function.h"
#include "leaf_translation_plan.h"
#include <memory>

namespace {
using namespace reference;
using ppc::stencil::Operation;
using worlds::Function;
using worlds::failures;

constexpr uint32_t kNop = 0x60000000u;
constexpr uint32_t kHostBase = 0x80400000u;   // Anything up here is "a recompiled game function".
constexpr uint32_t kThrower = 0x80500000u;    // Throws ppc::GuestLongJmp.
constexpr uint32_t kWalker = 0x80500004u;     // Walks the host stack.
uint32_t li(uint32_t rd, uint32_t value) { return dform(14, rd, 0, value); }
uint32_t addi(uint32_t rd, uint32_t ra, uint32_t value) { return dform(14, rd, ra, value); }
uint32_t bl(int32_t offset) { return branch(offset) | 1; }
uint32_t bl_to(uint32_t from, uint32_t target) { return bl(int32_t(target - from)); }

// The functions a guest call can reach, and which executor is running.
std::vector<Function*> g_functions;
bool g_native = true, g_dispatch_failed = false;
// What the walker saw.
struct Walk { int frames = 0, translated = -1; bool entry_found = false, caller_in_image = false; int beyond = 0; };
Walk g_walk;
bool g_walk_enabled = false;

void walk_stack() {
  // The walk a crash reporter does: look up each frame's function entry (the static tables of the
  // images and the dynamic ones the translator registered) and unwind it. A frame without an
  // entry is a leaf, whose return address is at RSP.
  void* frames[48];
  USHORT count = 0;
  CONTEXT context;
  RtlCaptureContext(&context);
  while (count < 48 && context.Rip) {
    frames[count++] = reinterpret_cast<void*>(context.Rip);
    DWORD64 image = 0;
    if (PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr)) {
      void* handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler_data, &establisher, nullptr);
    } else {
      context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
      context.Rsp += 8;
    }
  }
  g_walk = Walk{};
  g_walk.frames = count;
  for (USHORT i = 0; i < count; ++i) {
    for (Function* function : g_functions) {
      if (!function->leaf.contains(frames[i])) continue;
      if (g_walk.translated < 0) g_walk.translated = i;
      DWORD64 base = 0;
      if (RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(frames[i]), &base, nullptr)) g_walk.entry_found = true;
    }
  }
  if (g_walk.translated >= 0 && g_walk.translated + 1 < count) {
    // The frame under the translated one is CompiledLeaf::run, in this program's image.
    void* image = nullptr;
    void* mine = nullptr;
    RtlPcToFileHeader(frames[g_walk.translated + 1], &image);
    RtlPcToFileHeader(reinterpret_cast<void*>(&walk_stack), &mine);
    g_walk.caller_in_image = image && image == mine;
    g_walk.beyond = count - g_walk.translated - 1;
  }
}
void io_hook(uint32_t ea) {
  if (!g_native) return;
  if (ea == 0xCC00F000u) walk_stack();
  if (ea == 0xCC00F004u) throw ppc::GuestLongJmp{ea, 7};
}
void dispatch(ppc::Context& c, uint8_t* m, uint32_t address) {
  if (address == kThrower) throw ppc::GuestLongJmp{c.r[3], c.r[4]};
  if (address == kWalker) { if (g_native && g_walk_enabled) walk_stack(); c.r[3] += 1; return; }
  for (Function* function : g_functions) {
    if (function->address != address) continue;
    // A store may have overwritten the callee's words, and a translation refuses to run over
    // changed code. Both executors put the words back, so their RAM stays the same.
    std::memcpy(m + (address - ppc::RAM_BASE), function->code.data(), function->code.size());
    const bool ok = g_native ? function->leaf.run(c, m) : run(c, m, function->words, function->address);
    if (!ok) g_dispatch_failed = true;
    return;
  }
  host::default_dispatch(c, m, address);
}
// Native in world 0 and the reference in world 1, both through the dispatch above.
bool compare(Function& function, const ppc::Context& start, const char* name) {
  worlds::install(function.address, function.code);
  for (Function* other : g_functions) worlds::install(other->address, other->code);
  g_dispatch_failed = false;
  g_native = true;
  worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return function.leaf.run(c, m); });
  g_native = false;
  g_nan_order_open = false;
  worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, function.words, function.address); });
  g_native = true;
  if (g_nan_order_open) { ++worlds::set_aside; worlds::resync(); return true; } // See ppc_leaf_reference.h.
  ++worlds::comparisons;
  const char* what = worlds::difference();
  if (!what && !g_dispatch_failed) return true;
  std::printf("FAIL: %s differs in %s\n", name, what ? what : "a callee that did not run");
  function.dump();
  describe(worlds::g_world[0].result.context, worlds::g_world[1].result.context);
  ++failures;
  return false;
}

// The planner's stencil graph for each kind of call.
void plans() {
  using ppc::stencil::Instruction;
  std::vector<Instruction> plan;
  std::string error;
  const uint32_t base = ppc::RAM_BASE + 0x3000;
  const auto planned = [&](const std::vector<uint32_t>& words) {
    const auto code = bytes(words);
    return ppc::stencil::plan_leaf(code.data(), code.size(), base, plan, error);
  };
  // bl: LR is the next instruction, execution continues there.
  CHECK(planned({bl(0x100), kBlr}) && plan.size() == 2 && plan[0].operation == Operation::Call &&
        plan[0].immediate == base + 4 && plan[0].immediate2 == base + 0x100 && plan[0].next == 1);
  // bla: the target is absolute.
  CHECK(planned({(18u << 26) | 0x1000u | 3u, kBlr}) && plan[0].operation == Operation::Call && plan[0].immediate2 == 0x1000u);
  // b out of the function: the call, then a return of its own.
  CHECK(planned({branch(0x100)}) && plan.size() == 2 && plan[0].operation == Operation::TailCall &&
        plan[0].immediate == base + 0x100 && plan[0].next == 1 && plan[1].operation == Operation::Return);
  // b to before the function is a tail call too, not a back-edge.
  CHECK(planned({kNop, branch(-8)}) && plan.size() == 3 && plan[1].operation == Operation::TailCall && plan[1].immediate == base - 4);
  // beq out of the function: the test, the tail call and its return; not taken falls through.
  CHECK(planned({bc(12, 2, 0x100), kBlr}) && plan.size() == 4 && plan[0].operation == Operation::BranchCrSet &&
        plan[0].taken == 1 && plan[0].next == 3 && plan[1].operation == Operation::TailCall && plan[2].operation == Operation::Return);
  // beql: the test, then the call, which continues at the next instruction.
  CHECK(planned({bc(12, 2, 0x100) | 1, kBlr}) && plan.size() == 3 && plan[0].taken == 1 && plan[0].next == 2 &&
        plan[1].operation == Operation::Call && plan[1].next == 2);
  // bdnzl: CTR test, then the call.
  CHECK(planned({bc(16, 0, 0x100) | 1, kBlr}) && plan[0].operation == Operation::BranchCtrNonzero && plan[1].operation == Operation::Call);
  // bctrl and bctr.
  CHECK(planned({bcctr(20, 0, 1), kBlr}) && plan.size() == 2 && plan[0].operation == Operation::CallCtr && plan[0].immediate == base + 4);
  CHECK(planned({bcctr(20, 0)}) && plan.size() == 2 && plan[0].operation == Operation::TailCallCtr && plan[1].operation == Operation::Return);
  CHECK(planned({bcctr(12, 2), kBlr}) && plan.size() == 4 && plan[1].operation == Operation::TailCallCtr);
  // bl to one of the function's own addresses is a local call (ppc_leaf_local_test.cpp).
  CHECK(planned({kNop, bl(-4), kBlr}) && plan[1].operation == Operation::SetLr && plan[1].immediate == base + 8 &&
        plan[2].operation == Operation::Exit && plan[2].exit == ppc::stencil::ExitKind::LocalCall && plan[2].taken == 0);
}

// Results worked out by hand.
void known_answers() {
  const uint32_t a = ppc::RAM_BASE + 0x3000, b = ppc::RAM_BASE + 0x3400;
  { // A translated function calls another translated function and continues.
    Function caller, callee;
    if (!caller.translate({li(3, 5), bl_to(a + 4, b), addi(3, 3, 1), kBlr}, a) ||
        !callee.translate({addi(3, 3, 10), kBlr}, b)) return;
    g_functions = {&callee};
    ppc::Context c{};
    c.lr = 0x1234;
    worlds::install(a, caller.code); worlds::install(b, callee.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return caller.leaf.run(context, m); });
    const auto& result = worlds::g_world[0].result;
    CHECK(result.ok && result.context.r[3] == 16 && result.context.lr == a + 8 && result.context.call_depth == 0 &&
          result.events.count == 1 && result.context.last_pc == b);
    compare(caller, c, "call and continue");
  }
  { // A function that calls its own entry: local calls, 300 deep, and 300 local returns. Only the
    // last 32 return addresses are kept, and they are all the same one.
    Function f;
    if (!f.translate({dform(11, 0, 3, 0), bclr(12, 2), addi(3, 3, 0xFFFF), bl(-12), addi(4, 4, 1), kBlr}, a)) return;
    g_functions = {&f};
    ppc::Context c{};
    c.r[3] = 300;
    worlds::install(a, f.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return f.leaf.run(context, m); });
    const auto& result = worlds::g_world[0].result;
    CHECK(result.ok && result.context.r[3] == 0 && result.context.r[4] == 300 && result.events.count == 0 &&
          result.context.call_depth == 0);
    compare(f, c, "recursion");
  }
  { // A tail branch: the callee runs and the caller returns without running what follows.
    Function caller, callee;
    if (!caller.translate({li(3, 1), branch(int32_t(b - (a + 4))), li(3, 99), kBlr}, a) ||
        !callee.translate({addi(3, 3, 10), kBlr}, b)) return;
    g_functions = {&callee};
    ppc::Context c{};
    c.lr = 0x1234;
    worlds::install(a, caller.code); worlds::install(b, callee.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return caller.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.context.r[3] == 11 && worlds::g_world[0].result.context.lr == 0x1234);
    compare(caller, c, "tail call");
  }
  { // bctrl reads the target before it writes LR; bctr leaves LR alone.
    Function through, tail, callee;
    if (!through.translate({bcctr(20, 0, 1), addi(3, 3, 1), kBlr}, a) || !tail.translate({bcctr(20, 0)}, a + 0x100) ||
        !callee.translate({addi(3, 3, 10), kBlr}, b)) return;
    g_functions = {&callee};
    ppc::Context c{};
    c.ctr = b; c.lr = 0x1234;
    worlds::install(a, through.code); worlds::install(a + 0x100, tail.code); worlds::install(b, callee.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return through.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.context.r[3] == 11 && worlds::g_world[0].result.context.lr == a + 4);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return tail.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.context.r[3] == 10 && worlds::g_world[0].result.context.lr == 0x1234);
    compare(through, c, "bctrl");
    compare(tail, c, "bctr");
  }
  { // A call inside a loop: five calls of a host function, four back-edges.
    Function loop;
    if (!loop.translate({li(28, 5), bl_to(a + 4, kHostBase), dform(13, 28, 28, 0xFFFF), bc(4, 2, -8), kBlr}, a)) return;
    g_functions = {};
    ppc::Context c{};
    worlds::install(a, loop.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return loop.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.events.count == 5 && worlds::g_world[0].result.context.backedges == 4);
    c.backedges = 0x3FD;
    compare(loop, c, "call in a loop");
  }
  { // A conditional call that is not taken makes no call and leaves LR alone.
    Function f;
    if (!f.translate({bc(12, 2, 0x100) | 1, kBlr}, a)) return;
    ppc::Context c{};
    c.lr = 0x1234;
    worlds::install(a, f.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return f.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.events.count == 0 && worlds::g_world[0].result.context.lr == 0x1234);
    c.cr[0] = 2;
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return f.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.events.count == 1 && worlds::g_world[0].result.context.lr == a + 4);
    compare(f, c, "conditional call");
  }
}

// Every BO and BI through bcl, a conditional tail bc, bcctrl and bcctr.
void every_condition(Random& random) {
  const uint32_t a = ppc::RAM_BASE + 0x3000;
  g_functions = {};
  for (uint32_t bo = 0; bo < 32 && failures < 20; ++bo) {
    for (uint32_t bi = 0; bi < 32; ++bi) {
      Function call, tail, through, jump;
      if (!call.translate({bc(bo, bi, 0x200) | 1, li(3, 1), kBlr}, a) || !tail.translate({bc(bo, bi, 0x200), li(3, 1), kBlr}, a)) return;
      const bool counter = (bo & 4) != 0; // bcctr may not decrement CTR.
      if (counter && (!through.translate({bcctr(bo, bi, 1), li(3, 1), kBlr}, a) || !jump.translate({bcctr(bo, bi), li(3, 1), kBlr}, a))) return;
      for (int sample = 0; sample < 8; ++sample) {
        ppc::Context start;
        randomize_rich(start, random);
        static const uint32_t counters[] = {0, 1, 2, 0xFFFFFFFFu};
        if (sample < 4) start.ctr = counters[sample];
        if (!compare(call, start, "bcl") || !compare(tail, start, "bc tail")) return;
        if (counter && (!compare(through, start, "bcctrl") || !compare(jump, start, "bcctr"))) return;
      }
    }
  }
}

// Generated call graphs: four functions of integer, memory and float forms; each calls only
// functions after it, host functions, or through CTR, so every run ends.
void graphs(Random& random) {
  for (int graph = 0; graph < 600 && failures < 20; ++graph) {
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::vector<uint32_t>> words(4);
    uint32_t addresses[4];
    for (int i = 0; i < 4; ++i) addresses[i] = ppc::RAM_BASE + 0xA03000 + 0x400 * uint32_t(i) + 4 * random.below(16);
    const uint32_t near_host = ppc::RAM_BASE + 0xA04400; // Past the last function, within reach of bc.
    for (int i = 0; i < 4; ++i) {
      auto& out = words[i];
      const uint32_t length = 2 + random.below(24);
      for (uint32_t n = 0; n < length; ++n) {
        const uint32_t here = addresses[i] + 4 * uint32_t(out.size());
        const uint32_t pick = random.below(16);
        const uint32_t target = (i < 3 && random.below(2)) ? addresses[i + 1 + random.below(3 - i)] : kHostBase + 4 * random.below(64);
        if (pick == 0) out.push_back(bl_to(here, target));
        else if (pick == 1) {
          const uint32_t close_by = (i < 3 && random.below(2)) ? addresses[i + 1 + random.below(3 - i)] : near_host + 4 * random.below(64);
          out.push_back(bc(random.below(2) ? 12 : 4, random.below(32), int32_t(close_by - here)) | 1);
        }
        else if (pick == 2 || pick == 3) {
          // lis, ori, mtctr, then bctrl or a conditional bcctrl. CTR is always set here: a CTR
          // inherited from the caller could name the function itself and never end.
          out.push_back(dform(15, 12, 0, target >> 16)); out.push_back(dform(24, 12, 12, target & 0xFFFF));
          out.push_back(mtspr(9, 12));
          out.push_back(bcctr(pick == 3 ? 20 : (random.below(2) ? 12 : 4), random.below(32), 1));
        }
        else if (pick < 7) out.push_back(random_word(kMemoryForms[random.below(uint32_t(kMemoryFormCount))], random));
        else if (pick < 9) out.push_back(random_word(kFloatForms[random.below(uint32_t(kFloatFormCount))], random));
        else {
          const Form& form = kForms[random.below(uint32_t(kFormCount))];
          if (form.kind == Kind::Mtspr && form.xo == 9) continue;
          out.push_back(random_word(form, random));
        }
      }
      const uint32_t here = addresses[i] + 4 * uint32_t(out.size());
      const uint32_t ending = random.below(6);
      const uint32_t target = (i < 3 && random.below(2)) ? addresses[i + 1 + random.below(3 - i)] : kHostBase + 4 * random.below(64);
      if (ending == 0) out.push_back(branch(int32_t(target - here)));          // tail call
      else if (ending == 1) {                                                   // lis, ori, mtctr, bctr
        out.push_back(dform(15, 12, 0, target >> 16)); out.push_back(dform(24, 12, 12, target & 0xFFFF));
        out.push_back(mtspr(9, 12)); out.push_back(bcctr(20, 0));
      }
      else out.push_back(kBlr);
    }
    bool built = true;
    for (int i = 0; i < 4 && built; ++i) {
      functions.push_back(std::make_unique<Function>());
      built = functions.back()->translate(words[i], addresses[i]);
    }
    if (!built) return;
    g_functions.clear();
    for (auto& function : functions) g_functions.push_back(function.get());
    if ((graph & 15) == 0) worlds::watch(random, 3);
    for (int sample = 0; sample < 16; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!compare(*functions[0], start, "call graph")) return;
    }
  }
  g_functions.clear();
}

// The system's own unwinder finds every call-capable stencil copy and walks through it.
void unwinding() {
  const uint32_t a = ppc::RAM_BASE + 0x3000, b = ppc::RAM_BASE + 0x3400;
  // The function table: one entry per call-capable copy, three for the chained stmw, none for leaves.
  { Function leaf_only, one, three;
    if (!leaf_only.translate({li(3, 1), xform(3, 4, 5, 266), kBlr}, a) || !one.translate({dform(36, 3, 4, 0), kBlr}, a) ||
        !three.translate({dform(47, 29, 4, 0), bl(0x100), kBlr}, a)) return;
    CHECK(leaf_only.leaf.unwind_entries() == 0 && one.leaf.unwind_entries() == 1 && three.leaf.unwind_entries() == 4);
  }
  // A stack walk from a host function called by translated code, two translated frames deep.
  { Function outer, inner;
    if (!outer.translate({li(3, 0), bl_to(a + 4, b), addi(3, 3, 100), kBlr}, a) ||
        !inner.translate({addi(3, 3, 10), bl_to(b + 4, kWalker), kBlr}, b)) return;
    g_functions = {&outer, &inner};
    g_walk_enabled = true;
    ppc::Context c{};
    worlds::install(a, outer.code); worlds::install(b, inner.code);
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return outer.leaf.run(context, m); });
    g_walk_enabled = false;
    CHECK(worlds::g_world[0].result.ok && worlds::g_world[0].result.context.r[3] == 111);
    // Frames: walker, dispatch, ppc::call, inner's Call stencil, run, dispatch, ppc::call, outer's
    // Call stencil, run, the test, main, the C runtime.
    CHECK(g_walk.translated >= 2 && g_walk.entry_found && g_walk.caller_in_image && g_walk.beyond >= 6);
    if (failures) std::printf("  walk: %d frames, translated at %d, %d beyond it\n", g_walk.frames, g_walk.translated, g_walk.beyond);
  }
  // The same from under a load, a store and the chained stmw, through the I/O stub.
  host::g_io_hook = io_hook;
  for (uint32_t word : {dform(32, 3, 4, 0), dform(36, 3, 4, 0), dform(47, 30, 4, 0xFFFC), dform(54, 3, 4, 0)}) {
    Function f;
    if (!f.translate({word, kBlr}, a)) return;
    g_functions = {&f};
    ppc::Context c{};
    c.r[4] = 0xCC00F000u;
    worlds::install(a, f.code);
    g_walk = Walk{};
    worlds::run(0, c, [&](ppc::Context& context, uint8_t* m) { return f.leaf.run(context, m); });
    CHECK(worlds::g_world[0].result.ok && g_walk.translated >= 1 && g_walk.entry_found && g_walk.caller_in_image && g_walk.beyond >= 3);
  }
  // A guest longjmp: a C++ exception thrown by the host crosses two translated functions, their
  // drivers and the dispatch, and the call depth comes back to where it was.
  { Function outer, inner;
    if (!outer.translate({li(3, 0x55), li(4, 9), bl_to(a + 8, b), li(3, 1), kBlr}, a) ||
        !inner.translate({dform(47, 29, 5, 0), bl_to(b + 4, kThrower), li(3, 2), kBlr}, b)) return;
    g_functions = {&outer, &inner};
    worlds::install(a, outer.code); worlds::install(b, inner.code);
    uint32_t caught = 0;
    for (int round = 0; round < 20000; ++round) {
      ppc::Context c{};
      c.r[5] = 0x80008000u;
      c.call_depth = 3;
      host::g_locked_cache = worlds::g_world[0].lc;
      try {
        outer.leaf.run(c, worlds::g_world[0].ram);
        std::printf("FAIL: the exception did not propagate\n"); ++failures; break;
      } catch (const ppc::GuestLongJmp& jump) {
        if (jump.buf == 0x55 && jump.val == 9 && c.call_depth == 3 && c.r[3] == 0x55) ++caught;
      }
    }
    CHECK(caught == 20000);
    // These runs wrote world 0 only (the stmw); bring world 1 back in step.
    std::memcpy(worlds::g_world[1].ram, worlds::g_world[0].ram, 0x10000);
    worlds::ram_same();
  }
  // The same thrown from under each kind of memory stencil, the chained stmw among them: its
  // second and third I/O writes are inside the ranges with chained unwind data.
  for (uint32_t word : {dform(32, 3, 4, 0), dform(36, 3, 4, 0), dform(47, 29, 4, 0xFFFC), dform(47, 31, 4, 0), dform(50, 3, 4, 0),
                        dform(52, 3, 4, 0), xform(3, 4, 0, 23)}) {
    Function f;
    if (!f.translate({li(6, 1), word, li(6, 2), kBlr}, a)) return;
    g_functions = {&f};
    worlds::install(a, f.code);
    uint32_t caught = 0;
    for (int round = 0; round < 2000; ++round) {
      ppc::Context c{};
      c.r[4] = 0xCC00F004u;
      host::g_locked_cache = worlds::g_world[0].lc;
      try { f.leaf.run(c, worlds::g_world[0].ram); }
      catch (const ppc::GuestLongJmp& jump) { if (jump.val == 7 && c.r[6] == 1) ++caught; }
    }
    CHECK(caught == 2000);
  }
  host::g_io_hook = nullptr;
  // Freed translations leave no function table behind.
  { void* inside = nullptr;
    { Function f;
      if (!f.translate({bl(0x100), kBlr}, a)) return;
      // The first byte of the copied code is the Call stencil, which has an entry.
      inside = const_cast<void*>(f.leaf.base());
      DWORD64 base = 0;
      CHECK(inside && RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(inside), &base, nullptr) != nullptr &&
            base == reinterpret_cast<DWORD64>(inside));
    }
    DWORD64 base = 0;
    CHECK(inside && RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(inside), &base, nullptr) == nullptr);
  }
  g_functions.clear();
}

// A table whose call-capable stencils are not what the extractor admits is refused before any
// code is copied.
void tables() {
  using namespace ppc::stencil;
  const auto code = bytes({dform(36, 3, 4, 0), dform(47, 29, 4, 0), bl(0x100), xoform(63, 3, 0, 2, 15), kBlr});
  std::vector<Stencil> stencils(generated::table.stencils, generated::table.stencils + generated::table.count);
  std::vector<External> externals(generated::table.externals, generated::table.externals + generated::table.external_count);
  std::vector<Constant> constants(generated::table.constants, generated::table.constants + generated::table.constant_count);
  auto table = generated::table;
  table.stencils = stencils.data(); table.externals = externals.data(); table.constants = constants.data();
  CompiledLeaf leaf;
  std::string error;
  const uint32_t at = ppc::RAM_BASE + 0x3000;
  CHECK(translate_leaf(code.data(), code.size(), at, table, leaf, error) && leaf.ready());
  const auto index = [](Operation operation) { return static_cast<size_t>(operation); };
  const auto refused = [&](const char* what) {
    if (translate_leaf(code.data(), code.size(), at, table, leaf, error) || leaf.ready()) {
      std::printf("FAIL: table must be refused: %s\n", what);
      ++failures;
    }
  };
  // Unwind data on a leaf stencil.
  const Stencil stw = stencils[index(Operation::Stw)], add = stencils[index(Operation::Add)];
  CHECK(stw.unwind && stw.unwind_count == 1 && !add.unwind);
  stencils[index(Operation::Add)].unwind = stw.unwind; stencils[index(Operation::Add)].unwind_count = 1;
  refused("unwind data on a leaf stencil");
  stencils[index(Operation::Add)] = add;
  // Unwind data the translator does not know how to register.
  std::vector<uint8_t> info(stw.unwind[0].info, stw.unwind[0].info + stw.unwind[0].info_size);
  UnwindRecord record = stw.unwind[0];
  record.info = info.data();
  stencils[index(Operation::Stw)].unwind = &record;
  CHECK(translate_leaf(code.data(), code.size(), at, table, leaf, error));
  info[0] = 2; refused("unwind version 2"); info[0] = 1;
  info[0] = 1 | (1 << 3); refused("exception handler flag"); info[0] = 1;
  info[0] = 1 | (4 << 3); refused("chained flag without a parent"); info[0] = 1;
  info[3] = 5; refused("frame register"); info[3] = 0;
  { const uint8_t saved = info[5]; info[5] = uint8_t((saved & 0xF0) | 3); refused("UWOP_SET_FPREG"); info[5] = saved; }
  { const uint8_t saved = info[5]; info[5] = uint8_t((saved & 0xF0) | 10); refused("UWOP_PUSH_MACHFRAME"); info[5] = saved; }
  info[2] = uint8_t(info[2] + 2); refused("more unwind codes than bytes"); info[2] = uint8_t(info[2] - 2);
  info[1] = 0xFF; refused("prolog longer than the stencil"); info[1] = stw.unwind[0].info[1];
  record.end -= 1; refused("record that does not cover the stencil"); record.end += 1;
  record.begin = 1; refused("record that starts late"); record.begin = 0;
  record.parent = 0; refused("record chained to itself"); record.parent = -1;
  record.info_size -= 4; refused("truncated unwind data"); record.info_size += 4;
  stencils[index(Operation::Stw)].unwind_count = 0; refused("unwind pointer without records");
  stencils[index(Operation::Stw)] = stw;
  // The chained stmw: its later records need their parent.
  const Stencil stmw = stencils[index(Operation::Stmw)];
  CHECK(stmw.unwind_count >= 2 && stmw.unwind[1].parent == 0);
  std::vector<UnwindRecord> records(stmw.unwind, stmw.unwind + stmw.unwind_count);
  stencils[index(Operation::Stmw)].unwind = records.data();
  CHECK(translate_leaf(code.data(), code.size(), at, table, leaf, error));
  records[1].parent = -1; refused("chained data without a parent"); records[1].parent = 0;
  records[1].parent = 1; refused("chained to itself"); records[1].parent = 0;
  records[1].begin += 1; refused("gap between records"); records[1].begin -= 1;
  stencils[index(Operation::Stmw)].unwind_count = 1; refused("records that stop early");
  stencils[index(Operation::Stmw)] = stmw;
  // Host references.
  const Stencil call = stencils[index(Operation::Call)];
  std::vector<Relocation> holes(call.relocations, call.relocations + call.relocation_count);
  stencils[index(Operation::Call)].relocations = holes.data();
  size_t host_hole = holes.size();
  for (size_t i = 0; i < holes.size(); ++i) if (holes[i].hole == Hole::External) host_hole = i;
  CHECK(host_hole < holes.size());
  if (host_hole < holes.size()) {
    const Relocation original = holes[host_hole];
    holes[host_hole].index = uint16_t(externals.size()); refused("host symbol index out of range"); holes[host_hole] = original;
    holes[host_hole].addend = 4; refused("host reference with an addend"); holes[host_hole] = original;
    holes[host_hole].hole = Hole::ExternalRva; holes[host_hole].bias = 1; refused("image-relative reference with a bias"); holes[host_hole] = original;
    holes[host_hole].hole = Hole::Constant; holes[host_hole].index = uint16_t(constants.size()); refused("constant index out of range"); holes[host_hole] = original;
    // The same reference in a stencil that is not call-capable.
    const Stencil jump = stencils[index(Operation::Jump)];
    stencils[index(Operation::Jump)] = {Operation::Jump, call.code, call.size, call.relocations, call.relocation_count};
    refused("host reference in a leaf stencil");
    stencils[index(Operation::Jump)] = jump;
    const External saved = externals[original.index];
    externals[original.index].address = nullptr; refused("host symbol without an address");
    externals[original.index] = saved;
    externals[original.index].address = &saved; refused("host symbol outside the host image");
    externals[original.index] = saved;
    externals[original.index].kind = static_cast<ExternalKind>(9); refused("unknown host symbol kind");
    externals[original.index] = saved;
  }
  stencils[index(Operation::Call)] = call;
  if (!constants.empty()) {
    const Constant saved = constants[0];
    constants[0].alignment = 3; refused("constant alignment"); constants[0] = saved;
    constants[0].size = 0; refused("empty constant"); constants[0] = saved;
  }
  CHECK(translate_leaf(code.data(), code.size(), at, table, leaf, error) && leaf.ready());
}

// The comparison can fail: a call to another address, a missing call and a wrong LR are seen.
void controls(Random& random) {
  const uint32_t a = ppc::RAM_BASE + 0x3000;
  g_functions = {};
  Function f;
  if (!f.translate({bl_to(a, kHostBase), kBlr}, a)) return;
  const auto differs = [&](const std::vector<uint32_t>& other, const char* expect) {
    ppc::Context start;
    randomize(start, random);
    worlds::install(a, f.code);
    worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return f.leaf.run(c, m); });
    g_native = false;
    worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, other, a); });
    g_native = true;
    const char* what = worlds::difference();
    if (!what || std::strcmp(what, expect)) {
      std::printf("FAIL: control expected a difference in %s, saw %s\n", expect, what ? what : "none");
      ++failures;
    }
  };
  differs({bl_to(a, kHostBase + 4), kBlr}, "context");          // another target
  differs({kNop, kBlr}, "context");                              // no call at all
  differs({branch(int32_t(kHostBase - a)), kBlr}, "context");    // a tail call leaves LR alone
}
} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0); // A crash in copied code must not swallow the report.
  Random random(0x2026100300000003ull);
  if (!worlds::init(2, random)) return 1;
  host::g_dispatch = dispatch;
  plans();
  tables();
  known_answers();
  controls(random);
  unwinding();
  const uint64_t fixed = worlds::comparisons;
  if (!failures) every_condition(random);
  const uint64_t condition_total = worlds::comparisons - fixed;
  if (!failures) graphs(random);
  const uint64_t graph_total = worlds::comparisons - fixed - condition_total;
  if (!failures)
    std::printf("leaf call: plan and table checks, known calls, stack walks through translated frames, 34000 C++ "
                "exceptions across translated frames, %llu comparisons over every BO and BI (bcl, tail bc, bcctrl, "
                "bcctr), %llu over 600 generated call graphs of four functions (%llu more samples set aside "
                "for a NaN result the compiler chooses); %llu host calls compared\n",
                (unsigned long long)condition_total, (unsigned long long)graph_total,
                (unsigned long long)worlds::set_aside, (unsigned long long)worlds::g_host_events);
  return failures ? 1 : 0;
}
