// Execute the shipped handler and its injections against actual guest RAM.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime/host/static_gecko.cpp"
#include "../runtime/hle/offline_results_code_policy.h"
#include <cstdio>

namespace ppc { void init_dispatch(); }
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
constexpr uint32_t data = 0x80410000u, hook = 0x80100000u;
void execute() {
  const ppc::Context before = *host::cpu;
  const auto tick = host::retrace_count();
  user_gecko::last_tick = ~0u;
  user_gecko::apply();
  CHECK(std::memcmp(&before, host::cpu, sizeof before) == 0);
  CHECK(host::retrace_count() == tick);
  CHECK(!ppc::execution_budget.active && !ppc::store_observer());
}
void choose(const char* text) {
  user_gecko::codes().clear(); execute();
  const auto error = user_gecko::add("fixture", text);
  CHECK(error.empty() && user_gecko::codes().size() == 1);
  if (user_gecko::codes().empty()) return;
  CHECK(user_gecko::codes()[0].supported);
  user_gecko::codes()[0].enabled = true;
  execute();
}
}
int main() {
  std::vector<uint8_t> memory(ppc::RAM_SIZE + 64);
  ppc::Context context{};
  context.r[1] = 0x81500000; context.lr = 0x81234560; context.tb = 123456; context.msr = 0x8000;
  host::ram = memory.data(); host::cpu = &context; host::options.quiet = true;
  ppc::init_dispatch();
  // Results hooks can reach a leaf through RAM and return to a generated
  // continuation. The continuation restores the outer LR; it is a valid bclr
  // target even though this interpreter invocation started with another LR.
  constexpr uint32_t continuation = 0x81710000u;
  ppc::set_hook(continuation, [](ppc::Context& c, uint8_t*) {
    c.r[3] += 7;
    c.lr = c.r[12];
  });
  host::wr32(data, 0x3C008171);       // lis r0,0x8171
  host::wr32(data + 4, 0x7C0803A6);   // mtlr r0
  host::wr32(data + 8, 0x4E800020);   // blr into generated continuation
  ppc::Context returned{};
  returned.lr = returned.r[12] = 0xDEAD0000u;
  returned.r[3] = 5;
  ppc::interpret(returned, memory.data(), data);
  CHECK(returned.r[3] == 12 && returned.lr == 0xDEAD0000u && returned.call_depth == 0);
  ppc::set_hook(continuation, nullptr);
  std::memset(memory.data() + (data - ppc::RAM_BASE), 0, 12);
  user_gecko::install_static_runtime(); user_gecko::set_code_patches_allowed(true);
  CHECK(user_gecko::static_memory_required() == 0);
  CHECK(user_gecko::add("reserved", "04410000 11223344").empty());
  CHECK(user_gecko::static_memory_required() == 0x10000u);
  user_gecko::static_memory_start(0x81600000, 0x10000);
  CHECK(user_gecko::entry != 0 && user_gecko::list > user_gecko::entry);

  // The old results cave used a caller argument slot. Simulate a callee/hook
  // reusing that slot and prove the saved next state and sudden-death byte survive.
  for (unsigned rematch : {0u, 2u}) for (bool save_hook : {true, false}) {
    char body[512];
    std::snprintf(body, sizeof body,
      "C0000000 00000009\n38800004 38A00003\n%08X %08X\n"
      "3B850000 3800007F\n90010008 3B60000%X\n2C1B0000 40820008\n"
      "%08X 3C608041\n93630300 578C063E\n91830304 38800048\n4E800020 60000000",
      save_hook ? 0x3B640000u : 0x3B600000u, save_hook ? slippi::results_codes::kSaveNext : 0x60000000u,
      rematch, slippi::results_codes::kRestoreNext);
    choose(body);
    CHECK(host::rd32(data + 0x300) == (rematch ? rematch : save_hook ? 4 : 0));
    CHECK(host::rd32(data + 0x304) == 3);
  }
  std::vector<uint8_t> old_results(8 + 24 + 32);
  const uint32_t words[] = {0xC21A5B00, 2, 0x3B640000, 0x90810008, 0x60000000, 0,
                           0xC21A5B18, 3, 0x2C1B0000, 0x40820008, 0x83610008, 0x881F0064, 0x60000000, 0};
  for (size_t i = 0; i < std::size(words); ++i) slippi::unlock_codes::write_word(old_results.data() + 8 + i*4, words[i]);
  CHECK(slippi::results_codes::upgrade(old_results.data(), old_results.size()));
  CHECK(slippi::unlock_codes::read_word(old_results.data() + 20) == slippi::results_codes::kSaveNext);
  CHECK(slippi::unlock_codes::read_word(old_results.data() + 48) == slippi::results_codes::kRestoreNext);
  CHECK(!slippi::results_codes::upgrade(old_results.data(), old_results.size()));

  choose("00410000 000200AB\n02410004 0001CDEF\n04410008 11223344\n"
         "06410010 0000000B\n01020304 05060708\n090A0B00 00000000\n"
         "08410020 00000003\n20020004 00000002");
  CHECK(host::rd8(data) == 0xAB && host::rd8(data + 2) == 0xAB && host::rd8(data + 3) == 0);
  CHECK(host::rd16(data + 4) == 0xCDEF && host::rd16(data + 6) == 0xCDEF);
  CHECK(host::rd32(data + 8) == 0x11223344);
  CHECK(host::rd32(data + 0x10) == 0x01020304 && host::rd8(data + 0x1A) == 0x0B);
  CHECK(host::rd32(data + 0x20) == 3 && host::rd32(data + 0x24) == 5 && host::rd32(data + 0x28) == 7);

  host::wr32(data, 0x80410080);
  choose("48000000 80410000\n14000000 AABBCCDD\nE0000000 80008000\n"
         "20410080 AABBCCDD\n04410084 55667788\nE0000000 80008000\n"
         "80000000 12345678\n84200000 80410088");
  CHECK(host::rd32(data + 0x80) == 0xAABBCCDD);
  CHECK(host::rd32(data + 0x84) == 0x55667788);
  CHECK(host::rd32(data + 0x88) == 0x12345678);

  choose("80000000 00000000\n60000003 00000000\n86000000 00000001\n"
         "62000000 00000000\n84200000 80410300");
  CHECK(host::rd32(data + 0x300) == 4);
  host::wr32(data + 0x200, 0x13572468); host::wr32(data + 0x204, 0xABCDEF01);
  choose("F6000001 80418042\n13572468 ABCDEF01\n14000008 55EEFAAB\nE0000000 80008000");
  CHECK(host::rd32(data + 0x208) == 0x55EEFAAB);

  // C0 calls a PPC body, while C2 installs a hook, runs it and returns to game code.
  choose("C0000000 00000002\n3C608041 3980002A\n91830100 4E800020");
  CHECK(host::rd32(data + 0x100) == 42);
  host::wr32(hook, 0x38600007); host::wr32(hook + 4, 0x4E800020);
  choose("C2100000 00000002\n3860000B 60000000\n60000000 00000000");
  CHECK((host::rd32(hook) >> 26) == 18);
  ppc::Context call{}; call.lr = 0xDEAD0000; call.r[1] = 0x81500000;
  ppc::interpret(call, memory.data(), hook);
  CHECK(call.r[3] == 11);
  user_gecko::codes()[0].enabled = false; execute();
  CHECK(host::rd32(hook) == 0x38600007);
  call = {}; call.lr = 0xDEAD0000;
  ppc::interpret(call, memory.data(), hook); CHECK(call.r[3] == 7);

  // A later mod's replacement must survive switching our hook off.
  user_gecko::codes()[0].enabled = true; execute();
  host::wr32(hook, 0x38600063);
  user_gecko::codes()[0].enabled = false; execute();
  CHECK(host::rd32(hook) == 0x38600063);

  // A malformed body is refused before execution. A loop is bounded and restores context.
  user_gecko::codes().clear();
  CHECK(user_gecko::add("truncated", "C2100000 00000002\n60000000 00000000").empty());
  CHECK(!user_gecko::codes()[0].supported);
  choose("C0000000 00000001\n48000000 00000000");
  CHECK(user_gecko::failed && !user_gecko::live);
  user_gecko::codes().clear(); execute();
  host::ram = nullptr; host::cpu = nullptr;
  std::printf("%s\n", failures ? "Static Gecko FAILED" : "Static Gecko passed");
  return failures ? 1 : 0;
}
