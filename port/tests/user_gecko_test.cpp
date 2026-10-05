// The Source Port's Gecko code rules as the host itself runs them (runtime/host/user_gecko.cpp):
// what a code is compiled into and what its plan does in a frame. The vectors are those of
// gecko_layout_test.py, which runs the Python twin (tools/gecko_coverage.py), on the same made-up
// image: a function, a float array, a 16-bit array, a struct of two 16-bit fields and a float, a
// pointer table, a byte array, a 32-bit counter and a byte array in the second 16 megabytes.
// user_gecko.cpp is included, not linked, so the image below stands in for the generated table
// (USER_GECKO_TEST_TARGETS) and the game library is a map of console bytes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>

#define USER_GECKO_TEST_TARGETS
namespace user_gecko {
struct ConsoleSymbol { uint32_t addr, size; uint8_t kind; const char* name; };
// kind: 0 a variable a code may write and read, 1 code, 2 holds pointers (user_gecko.cpp).
static const ConsoleSymbol kConsoleSymbols[] = {
  {0x80100000u, 0x40u, 1, "ftCo_Attack_Example"},
  {0x80400000u, 0x10u, 0, "tunable_floats"},
  {0x80400010u, 0x08u, 0, "tunable_shorts"},
  {0x80400018u, 0x10u, 0, "mixed_fields"},
  {0x80400028u, 0x08u, 2, "callback_table"},
  {0x80400030u, 0x04u, 0, "tunable_bytes"},
  {0x80400034u, 0x04u, 0, "counter"},
  {0x81400000u, 0x10u, 0, "far_bytes"},
};
// Two patches a made-up switch carries: the write 04100020 38600001, and the injection
// C2100010 00000002 / 38600001 38800002 / 60000000 00000000. Sorted as the generated table is;
// the hashes are FNV-1a over the words (tools/gecko_targets.py, unit_key).
static const char* const kBuiltInLabels[] = {"Switch"};
struct BuiltInUnit { uint32_t word, lines; uint64_t hash; uint8_t label; };
static const BuiltInUnit kBuiltInUnits[] = {
  {0x04100020u, 1u, 0x02753CD4AC8530D6ull, 0},
  {0xC2100010u, 3u, 0x3E1F365EAE3C1F04ull, 0},
};
}  // namespace user_gecko

#include "../runtime/host/user_gecko.cpp"

#include <map>

// What user_gecko.cpp asks of the host. The Static Recomp's console memory is not under test.
namespace host {
void log(const char*, ...) {}
uint8_t rd8(uint32_t) { return 0; }
void wr8(uint32_t, uint8_t) {}
void wr16(uint32_t, uint16_t) {}
void wr32(uint32_t, uint32_t) {}
}  // namespace host
namespace ppc {
bool redirect_function_at(uint32_t) { return false; }
}  // namespace ppc

namespace user_gecko {
namespace {

int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define CHECK_EQ(got, want) do { \
    const unsigned long long got_ = (unsigned long long)(got), want_ = (unsigned long long)(want); \
    if (got_ != want_) { std::printf("FAIL %s:%d  %s is %llX, not %llX\n", __FILE__, __LINE__, #got, got_, want_); ++g_failures; } \
  } while (0)

using Lines = std::vector<std::pair<uint32_t, uint32_t>>;

// ---- the game library: console bytes, big-endian, of the variables above ----
std::map<uint32_t, uint8_t> g_memory;
int g_refused_calls = 0;        // reads and writes that were not inside one variable
bool g_network_match = false;   // the library takes no codes during one
uint32_t g_missing = 0;         // an address this library does not offer (an older build of it)

bool variable(uint32_t addr, uint32_t len) {
  for (const ConsoleSymbol& s : kConsoleSymbols)
    if (s.kind == 0 && addr >= s.addr && addr - s.addr < s.size && len <= s.size - (addr - s.addr)) return true;
  return false;
}

int32_t fake_write(uint32_t addr, const uint8_t* bytes, uint32_t len) {
  if (g_network_match) return -1;
  if (g_missing && addr == g_missing) return 0;
  if (!variable(addr, len)) { ++g_refused_calls; return 0; }
  for (uint32_t k = 0; k < len; ++k) g_memory[addr + k] = bytes[k];
  return 1;
}

int32_t fake_read(uint32_t addr, uint8_t* bytes, uint32_t len) {
  if (g_network_match) return -1;
  if (g_missing && addr == g_missing) return 0;
  if (!variable(addr, len)) { ++g_refused_calls; return 0; }
  for (uint32_t k = 0; k < len; ++k) {
    const auto it = g_memory.find(addr + k);
    bytes[k] = it == g_memory.end() ? (uint8_t)0 : it->second;
  }
  return 1;
}

uint32_t peek(uint32_t addr, uint32_t width) {
  uint32_t v = 0;
  for (uint32_t k = 0; k < width; ++k) {
    const auto it = g_memory.find(addr + k);
    v = (v << 8) | (it == g_memory.end() ? 0u : (uint32_t)it->second);
  }
  return v;
}

void poke(uint32_t addr, uint32_t width, uint32_t value) {
  for (uint32_t k = 0; k < width; ++k) g_memory[addr + k] = (uint8_t)(value >> (8 * (width - 1 - k)));
}

// The image and the Gecko registers as at boot.
void fresh() {
  g_memory.clear();
  std::fill(std::begin(g_registers), std::end(g_registers), 0u);
  codes().clear();
  g_refused_calls = 0;
  g_network_match = false;
  g_missing = 0;
}

Code compiled(const Lines& lines) {
  Code c;
  c.name = "test";
  c.lines = lines;
  classify(c);
  return c;
}

// Reads one code and runs it for `frames` frames through apply(), as the game does. False when
// the code was refused or the game library did not take an address.
bool run(const Lines& lines, int frames = 1) {
  codes().clear();
  codes().push_back(compiled(lines));
  if (!codes().back().supported) { std::printf("  refused: %s\n", codes().back().reason.c_str()); return false; }
  codes().back().enabled = true;
  for (int f = 0; f < frames; ++f) apply();
  if (g_refused_calls) { std::printf("  %d reads or writes outside one variable\n", g_refused_calls); g_refused_calls = 0; return false; }
  if (!codes().back().enabled) { std::printf("  switched off: %s\n", codes().back().reason.c_str()); return false; }
  return true;
}
#define RUN(...) do { if (!run(__VA_ARGS__)) { std::printf("FAIL %s:%d  the code did not run\n", __FILE__, __LINE__); ++g_failures; } } while (0)

// The code is refused and its reason starts with `text`.
void check_refused(int line, const char* text, const Lines& lines) {
  const Code c = compiled(lines);
  if (c.supported) { std::printf("FAIL %s:%d  not refused (expected: %s)\n", __FILE__, line, text); ++g_failures; return; }
  if (c.reason.compare(0, std::strlen(text), text) != 0) {
    std::printf("FAIL %s:%d  refused with \"%s\", expected \"%s\"\n", __FILE__, line, c.reason.c_str(), text);
    ++g_failures;
  }
  CHECK(c.plan.empty() && c.plan_bytes.empty() && !c.reads);
}
#define REFUSED(text, ...) check_refused(__LINE__, text, __VA_ARGS__)

const uint32_t kFloats = 0x80400000u, kShorts = 0x80400010u, kMixed = 0x80400018u, kBytes = 0x80400030u,
               kCounter = 0x80400034u, kFar = 0x81400000u;

// ---- writes ----

void test_writes() {
  fresh();
  RUN({{0x00400030u, 0x000300ABu}});                       // 8-bit, three more
  CHECK_EQ(peek(kBytes, 4), 0xABABABABu);
  RUN({{0x02400012u, 0x00021234u}});                       // 16-bit, two more, from the second half
  CHECK_EQ(peek(kShorts, 4), 0x00001234u);
  CHECK_EQ(peek(kShorts + 4, 4), 0x12341234u);
  RUN({{0x04400004u, 0x3FC00000u}});                       // 1.5f into the second float
  CHECK_EQ(peek(kFloats, 4), 0u);
  CHECK_EQ(peek(kFloats + 4, 4), 0x3FC00000u);

  fresh();
  RUN({{0x06400030u, 3u}, {0x11223344u, 0x55667788u}});    // a string of three bytes
  CHECK_EQ(peek(kBytes, 4), 0x11223300u);
  REFUSED("has a string write cut short", {{0x06400030u, 9u}, {0x11223344u, 0x55667788u}});

  // The serial write: 16-bit, three more writes, 2 bytes apart, the value up by 0x10 each time.
  fresh();
  RUN({{0x08400010u, 0x00000100u}, {0x10030002u, 0x00000010u}});
  CHECK_EQ(peek(kShorts, 4), 0x01000110u);
  CHECK_EQ(peek(kShorts + 4, 4), 0x01200130u);
  // 32-bit, one more write 2 bytes on: the second starts inside the variable and runs past it.
  REFUSED("writes past the end of tunable_bytes", {{0x08400030u, 0x00000001u}, {0x20010002u, 0x00000000u}});
  // Each write is judged on its own: a serial write may step from one variable into the next.
  RUN({{0x08400010u, 0x00000100u}, {0x10040002u, 0x00000010u}});
  CHECK_EQ(peek(kMixed, 2), 0x0140u);
  REFUSED("has a serial write cut short", {{0x08400010u, 1u}});

  fresh();
  RUN({{0x01400002u, 0x000000ABu}});                       // address bit 24: 81400002
  CHECK_EQ(peek(kFar + 2, 1), 0xABu);

  // A 32-bit write is rounded down to a word (the handler, 80002050); the others are not.
  fresh();
  RUN({{0x04400006u, 0x3FC00000u}});
  CHECK_EQ(peek(kFloats + 4, 4), 0x3FC00000u);
  CHECK_EQ(peek(kFloats + 8, 4), 0u);
  RUN({{0x02400031u, 0x00001234u}});
  CHECK_EQ(peek(kBytes, 4), 0x00123400u);

  // Two neighbours on the console are not neighbours here.
  REFUSED("writes past the end of tunable_floats", {{0x0240000Eu, 0x00010001u}});
  REFUSED("writes callback_table, which holds pointers", {{0x04400028u, 0x80100000u}});
  REFUSED("writes memory at 80C00000, which is not a game variable", {{0x04C00000u, 1u}});
}

// ---- the base address and the pointer ----

void test_base_and_pointer() {
  fresh();
  RUN({{0x4A000000u, 0x80400000u},     // po = 80400000
       {0x14000004u, 0x3FC00000u},     // 32-bit at po + 4
       {0x12000010u, 0x00001234u},     // 16-bit at po + 0x10
       {0x10000030u, 0x000000CDu}});   // 8-bit at po + 0x30
  CHECK_EQ(peek(kFloats + 4, 4), 0x3FC00000u);
  CHECK_EQ(peek(kShorts, 2), 0x1234u);
  CHECK_EQ(peek(kBytes, 1), 0xCDu);

  fresh();
  RUN({{0x4A000000u, 0x80400000u},     // po = 80400000
       {0x4A100000u, 0x00000010u},     // po += 0x10
       {0x12000000u, 0x00000001u},     // 16-bit at po
       {0xE0000000u, 0x80008000u},     // ba = po = 80000000
       {0x12400012u, 0x00000002u}});
  CHECK_EQ(peek(kShorts, 4), 0x00010002u);

  // The base address counts by its top seven bits only (rlwinm r12,r6,0,0,6 at 80001FCC): set to
  // the non-aligned 80400010 it still counts as 80000000, and the line's own address decides.
  fresh();
  RUN({{0x42000000u, 0x80400010u}, {0x02400012u, 0x00000002u}});
  CHECK_EQ(peek(kShorts, 4), 0x00000002u);
  CHECK_EQ(peek(kShorts + 4, 4), 0u);
  REFUSED("writes outside game memory (80000000)", {{0x42000000u, 0x80400010u}, {0x02000000u, 0x00000001u}});
  // The same for an if, for a register load and store, and for a number made relative.
  fresh();
  poke(kCounter, 4, 5);
  RUN({{0x42000000u, 0x80400010u}, {0x20400034u, 5u}, {0x00400030u, 0x000000EEu}, {0xE2000001u, 0u},
       {0x82210001u, 0x00400034u},     // gr1 = [ba + 400034]
       {0x84010001u, 0x00400031u},     // [ba + 400031] = gr1, 8-bit
       {0x4A010000u, 0x00400032u},     // po = ba + 400032
       {0x10000000u, 0x000000CDu}});
  CHECK_EQ(peek(kBytes, 4), 0xEE05CD00u);
  // A base address outside 80000000 to 81FFFFFF does move the line.
  REFUSED("writes outside game memory (90400012)", {{0x42000000u, 0x90000000u}, {0x02400012u, 0x00000002u}});

  // The flags are one bit each: 4A02 adds nothing, 4A20 sets.
  fresh();
  RUN({{0x4A020000u, 0x80400030u}, {0x10000000u, 0x00000011u}});
  CHECK_EQ(peek(kBytes, 1), 0x11u);
  RUN({{0x4A000000u, 0x80400000u}, {0x4A200000u, 0x80400031u}, {0x10000000u, 0x00000022u}});
  CHECK_EQ(peek(kBytes + 1, 1), 0x22u);

  // A store writes the register in full, as a number, at an address always counted from the base
  // (stwx r6,r12,r4 at 8000221C).
  fresh();
  RUN({{0x42000000u, 0x80400000u}, {0x42100000u, 0x00000010u}, {0x44000000u, 0x00400034u}});
  CHECK_EQ(peek(kCounter, 4), 0x80400010u);
  REFUSED("writes outside game memory (00400034)", {{0x42000000u, 0x80400010u}, {0x44000000u, 0x80400034u}});
  RUN({{0x4A000000u, 0x80400000u}, {0x5C000000u, 0x00000034u}});   // the pointer, at pointer + 0x34
  CHECK_EQ(peek(kCounter, 4), 0x80400000u);

  REFUSED("loads the pointer from game memory (code type 48)", {{0x48000000u, 0x80400034u}, {0x14000000u, 1u}});
  REFUSED("loads the base address from game memory (code type 40)", {{0x40000000u, 0x80400034u}});
  REFUSED("takes the address of its own lines (code type 4E)", {{0x4E000000u, 0u}});
  REFUSED("adds a register to an address (code type 42)", {{0x42001003u, 0x80400000u}});

  // A pointer set inside an if is known inside it and not after it.
  fresh();
  const Lines pointer_in_if = {{0x20400034u, 0u}, {0x4A000000u, 0x80400000u}, {0x14000000u, 0x3F800000u}, {0xE2000001u, 0u}};
  RUN(pointer_in_if);
  CHECK_EQ(peek(kFloats, 4), 0x3F800000u);
  Lines after = pointer_in_if;
  after.push_back({0x14000004u, 0u});
  REFUSED("uses the pointer after a condition that changed it has ended", after);
  // The endif that resets the pointer makes it known again.
  RUN({pointer_in_if[0], pointer_in_if[1], pointer_in_if[2], {0xE2000001u, 0x00008000u}, {0x14400004u, 0x40000000u}});
  CHECK_EQ(peek(kFloats + 4, 4), 0x40000000u);
  // The base address the same way, and in the else of the if that set it.
  REFUSED("uses the base address after a condition that changed it has ended",
          {{0x20400034u, 0u}, {0x42000000u, 0x80400000u}, {0x04400000u, 0x40400000u}, {0xE2000001u, 0u}, {0x04400004u, 0u}});
  RUN({{0x20400034u, 0u}, {0x42000000u, 0x80400000u}, {0x04400000u, 0x40400000u}, {0xE2000001u, 0x80000000u},
       {0x04400004u, 0x40800000u}});
  CHECK_EQ(peek(kFloats, 4), 0x40400000u);
  CHECK_EQ(peek(kFloats + 4, 4), 0x40800000u);
  REFUSED("uses the base address after a condition that changed it has ended",
          {{0x20400034u, 0u}, {0x42000000u, 0x80400000u}, {0xE2100000u, 0u}, {0x04000004u, 0u}});
}

// ---- ifs ----

void test_ifs() {
  struct If32 { uint32_t word, value; bool passes; };
  const If32 ifs32[] = {{0x20400034u, 5, true}, {0x20400034u, 6, false}, {0x22400034u, 6, true}, {0x24400034u, 4, true},
                        {0x24400034u, 5, false}, {0x26400034u, 6, true}, {0x26400034u, 5, false}};
  fresh();
  poke(kCounter, 4, 5);
  for (const If32& t : ifs32) {
    poke(kBytes, 1, 0);
    RUN({{t.word, t.value}, {0x00400030u, 0x000000EEu}, {0xE2000001u, 0u}});
    CHECK(codes().back().reads);
    if ((peek(kBytes, 1) == 0xEEu) != t.passes) { std::printf("FAIL if %08X %08X\n", t.word, t.value); ++g_failures; }
  }

  // A 16-bit if masks before comparing.
  struct If16 { uint32_t value; bool passes; };
  const If16 ifs16[] = {{0x00001234u, true}, {0xFF000034u, true}, {0xFF000035u, false}, {0x00001235u, false}};
  fresh();
  poke(kShorts, 2, 0x1234);
  for (const If16& t : ifs16) {
    poke(kBytes, 1, 0);
    RUN({{0x28400010u, t.value}, {0x00400030u, 0x000000EEu}, {0xE2000001u, 0u}});
    if ((peek(kBytes, 1) == 0xEEu) != t.passes) { std::printf("FAIL if 28400010 %08X\n", t.value); ++g_failures; }
  }

  // The address is rounded down to what is read: a word (80002104) or a half (80002110).
  fresh();
  poke(kCounter, 4, 5);
  poke(kShorts + 2, 2, 0x1234);
  RUN({{0x20400036u, 5u}, {0x00400030u, 0x000000EEu}, {0xE2000001u, 0u},             // reads 80400034
       {0x20400034u, 6u},                                                            // false
       {0x20400037u, 5u}, {0x00400031u, 0x000000DDu}, {0xE2000001u, 0u},             // endif, reads 80400034
       {0x28400034u, 9u},                                                            // false
       {0x28400013u, 0x00001234u}, {0x00400032u, 0x000000CCu}, {0xE2000001u, 0u}});  // endif, reads 80400012
  CHECK_EQ(peek(kBytes, 4), 0xEEDDCC00u);

  // Nested ifs and endif counts.
  fresh();
  poke(kCounter, 4, 5);
  RUN({{0x20400034u, 6u},              // false
       {0x20400034u, 5u},              // skipped: one deeper
       {0x00400030u, 0x000000AAu},     // skipped
       {0xE2000001u, 0u},              // ends the inner if: still skipping
       {0x00400031u, 0x000000BBu},     // skipped
       {0xE2000001u, 0u},              // ends the outer if
       {0x00400032u, 0x000000CCu}});   // runs
  CHECK_EQ(peek(kBytes, 4), 0x0000CC00u);

  // The count of an endif is five bits (clrlwi. r9,r3,27 at 80002740): E2000021 ends one if.
  fresh();
  RUN({{0x20400034u, 1u},              // false
       {0x20400034u, 1u},              // skipped: one deeper
       {0xE2000021u, 0u},              // ends the inner if only
       {0x00400030u, 0x000000AAu},     // still skipped
       {0xE2000001u, 0u},
       {0x00400031u, 0x000000BBu}});   // runs
  CHECK_EQ(peek(kBytes, 4), 0x00BB0000u);

  // Else.
  fresh();
  poke(kCounter, 4, 5);
  RUN({{0x20400034u, 6u}, {0x00400030u, 0x000000AAu}, {0xE2100000u, 0u}, {0x00400031u, 0x000000BBu}, {0xE2000001u, 0u}});
  CHECK_EQ(peek(kBytes, 4), 0x00BB0000u);
  fresh();
  poke(kCounter, 4, 5);
  RUN({{0x20400034u, 5u}, {0x00400030u, 0x000000AAu}, {0xE2100000u, 0u}, {0x00400031u, 0x000000BBu}, {0xE2000001u, 0u},
       {0x00400032u, 0x000000CCu}});
  CHECK_EQ(peek(kBytes, 4), 0xAA00CC00u);
  // An else inside a block that is being skipped changes nothing: the handler flips the innermost
  // if only when the one around it is running (8000274C to 80002754).
  fresh();
  RUN({{0x20400034u, 1u},              // false
       {0x20400034u, 0u},              // skipped (it would be true)
       {0x00400030u, 0x000000AAu},     // skipped
       {0xE2100000u, 0u},              // else of the inner if: the outer one is still false
       {0x00400031u, 0x000000BBu},     // skipped
       {0xE2000001u, 0u},
       {0x00400032u, 0x000000CCu},     // skipped: the outer if
       {0xE2100000u, 0u},              // else of the outer if
       {0x00400033u, 0x000000DDu},     // runs
       {0xE2000001u, 0u}});
  CHECK_EQ(peek(kBytes, 4), 0x000000DDu);
  // An endif and an else in one line: one if ends, then the one around it turns into its else.
  fresh();
  RUN({{0x20400034u, 0u},              // true
       {0x20400034u, 1u},              // false
       {0x00400030u, 0x000000AAu},     // skipped
       {0xE2100001u, 0u},              // ends the inner if; the outer one, true, turns false
       {0x00400031u, 0x000000BBu},     // skipped
       {0xE2000001u, 0u},
       {0x00400032u, 0x000000CCu}});   // runs
  CHECK_EQ(peek(kBytes, 4), 0x0000CC00u);

  // An address ending in 1 ends the previous if.
  fresh();
  poke(kCounter, 4, 5);
  RUN({{0x20400034u, 6u}, {0x00400030u, 0x000000AAu},
       {0x20400035u, 5u}, {0x00400031u, 0x000000BBu}, {0xE0000000u, 0x80008000u}});
  CHECK_EQ(peek(kBytes, 4), 0x00BB0000u);

  // The terminator ends every if.
  fresh();
  RUN({{0x20400034u, 1u}, {0x20400034u, 1u}, {0xE0000000u, 0u}, {0x00400030u, 0x00000077u}});
  CHECK_EQ(peek(kBytes, 1), 0x77u);

  REFUSED("reads memory at 80C00000", {{0x28C00000u, 0x00000100u}, {0x00400030u, 1u}});
  REFUSED("reads callback_table, which holds pointers", {{0x20400028u, 0u}});
}

// ---- registers ----

void test_registers() {
  fresh();
  RUN({{0x80000003u, 0x00001234u}, {0x84100023u, 0x80400010u}});   // gr3, 16-bit, three in a row
  CHECK_EQ(peek(kShorts, 4), 0x12341234u);
  CHECK_EQ(peek(kShorts + 4, 4), 0x12340000u);

  // Load, operate, store: the registers and the game's memory carry from frame to frame.
  fresh();
  RUN({{0x82200001u, 0x80400034u},     // gr1 = counter
       {0x86000001u, 0x00000001u},     // gr1 += 1
       {0x84200001u, 0x80400034u}},    // counter = gr1
      3);
  CHECK(codes().back().reads);
  CHECK_EQ(peek(kCounter, 4), 3u);

  CHECK_EQ(operate(0xFFFFFFFFu, 2u, 0), 1u);
  CHECK_EQ(operate(0x10000u, 0x10000u, 1), 0u);
  CHECK_EQ(operate(0xF0u, 0x0Fu, 2), 0xFFu);
  CHECK_EQ(operate(0xF0u, 0x3Cu, 3), 0x30u);
  CHECK_EQ(operate(0xF0u, 0x3Cu, 4), 0xCCu);
  // The shifts and the rotate: the operand shifted by the register (slw r4,r9,r4 at 800023F4, the
  // register in r4 and the operand in r9).
  CHECK_EQ(operate(31u, 1u, 5), 0x80000000u);
  CHECK_EQ(operate(32u, 1u, 5), 0u);
  CHECK_EQ(operate(31u, 0x80000000u, 6), 1u);
  CHECK_EQ(operate(32u, 0x80000000u, 6), 0u);
  CHECK_EQ(operate(1u, 0x80000001u, 7), 3u);
  CHECK_EQ(operate(0u, 5u, 7), 5u);
  CHECK_EQ(operate(4u, 0x80000000u, 8), 0xF8000000u);
  CHECK_EQ(operate(40u, 0x80000000u, 8), 0xFFFFFFFFu);
  CHECK_EQ(operate(4u, 0x40000000u, 8), 0x04000000u);
  CHECK_EQ(operate(0x3F800000u, 0x40000000u, 9), 0x40400000u);    // 1.0 + 2.0
  CHECK_EQ(operate(0x40000000u, 0x40400000u, 10), 0x40C00000u);   // 2.0 * 3.0

  // A register and memory, then two registers. The memory operand's address is always counted
  // from the base (add r19,r12,r19 at 800023B8).
  fresh();
  poke(kCounter, 4, 0x30);
  RUN({{0x80000002u, 0x0000000Cu}, {0x86020002u, 0x00400034u},     // gr2 += [counter]
       {0x80000005u, 0x00000003u}, {0x88100002u, 0x00000005u},     // gr2 *= gr5
       {0x84000002u, 0x80400030u}});
  CHECK_EQ(peek(kBytes, 1), 0xB4u);
  REFUSED("reads outside game memory (00400034)", {{0x86020002u, 0x80400034u}});
  RUN({{0x4A000000u, 0x80400000u}, {0x80000002u, 0x00000001u}, {0x96020002u, 0x00000034u},   // through the pointer
       {0x84000002u, 0x80400031u}});
  CHECK_EQ(peek(kBytes + 1, 1), 0x31u);

  fresh();
  RUN({{0x80000004u, 0x00000004u}, {0x86500004u, 0x00000003u},     // gr4 = 3 << gr4
       {0x84000004u, 0x80400030u}});
  CHECK_EQ(peek(kBytes, 1), 0x30u);

  // The flags are one bit each: 8020 sets, 8010 adds.
  fresh();
  RUN({{0x80000003u, 0x00000007u}, {0x80200003u, 0x00000005u}, {0x84000003u, 0x80400030u},
       {0x80100003u, 0x00000001u}, {0x84000003u, 0x80400031u}});
  CHECK_EQ(peek(kBytes, 2), 0x0506u);

  REFUSED("uses a register as an address (code type 86)", {{0x86010002u, 1u}});
  REFUSED("uses a register as an address (code type 88)", {{0x88020002u, 3u}});
  REFUSED("copies memory between addresses held in registers (code type 8A)", {{0x8A000412u, 0u}});
  REFUSED("writes past the end of tunable_bytes", {{0x84200011u, 0x80400030u}});
}

// ---- what stays refused ----

void test_refused() {
  REFUSED("injects PowerPC code into ftCo_Attack_Example (C2), which this build cannot run", {{0xC2100010u, 1u}, {0x60000000u, 0u}});
  REFUSED("runs PowerPC code of its own (code type C0)", {{0xC0000000u, 1u}, {0x4E800020u, 0u}});
  REFUSED("runs PowerPC code of its own (code type C6)", {{0xC6100010u, 0x80100020u}});
  REFUSED("writes game code at ftCo_Attack_Example", {{0x04100010u, 0x60000000u}});
  REFUSED("uses a loop or a jump (code type 60)", {{0x60000003u, 0u}});
  for (uint32_t word : {0x62000000u, 0x64000000u, 0x66000001u, 0x68000001u, 0xA0400034u, 0xA8000000u, 0xCC000000u,
                        0xCE000000u, 0xF6000001u, 0x0A400030u}) {
    const Code c = compiled({{word, 0u}});
    if (c.supported || c.reason.empty()) { std::printf("FAIL %08X is not refused\n", word); ++g_failures; }
  }
  REFUSED("writes outside game memory (80000000)", {{0x04000000u, 1u}});
  REFUSED("has no code lines", Lines{});

  fresh();
  RUN({{0x00400030u, 0x00000011u}, {0xF0000000u, 0u}, {0x00400031u, 0x00000022u}});   // the end of a code list
  CHECK_EQ(peek(kBytes, 2), 0x1100u);
}

// ---- several codes, and the game library's three answers ----

void test_codes_and_library() {
  // Each code starts with the base address and the pointer at 80000000 and no condition open (the
  // note above Compiler in user_gecko.cpp): the first code here leaves its pointer moved and an if
  // open and false. The Gecko registers are shared.
  fresh();
  codes().push_back(compiled({{0x80000005u, 0x00000009u}, {0x4A000000u, 0x80400000u}, {0x20400034u, 1u}}));
  codes().push_back(compiled({{0x10400031u, 0x00000077u}, {0x84000005u, 0x80400032u}}));
  for (Code& c : codes()) { CHECK(c.supported); c.enabled = c.supported; }
  apply();
  CHECK_EQ(peek(kBytes, 4), 0x00770900u);
  CHECK_EQ(g_refused_calls, 0);

  // A code that is off does nothing.
  fresh();
  codes().push_back(compiled({{0x00400030u, 0x00000011u}}));
  apply();
  CHECK_EQ(peek(kBytes, 1), 0u);

  // A network match: nothing is written and the code stays on for afterwards.
  fresh();
  g_network_match = true;
  codes().push_back(compiled({{0x00400030u, 0x00000011u}}));
  codes().back().enabled = true;
  apply();
  CHECK_EQ(peek(kBytes, 1), 0u);
  CHECK(codes().back().enabled && codes().back().supported);
  g_network_match = false;
  apply();
  CHECK_EQ(peek(kBytes, 1), 0x11u);

  // A game library that does not offer an address: the code goes off with the reason.
  fresh();
  g_missing = kBytes;
  codes().push_back(compiled({{0x00400030u, 0x00000011u}}));
  codes().back().enabled = true;
  apply();
  CHECK(!codes().back().enabled && !codes().back().supported);
  CHECK(codes().back().reason == "uses 80400030, which this game library does not offer");

  // A game library that cannot hand a variable back: a code that reads goes off, one that only
  // writes still runs.
  fresh();
  set_native_reader(nullptr);
  codes().push_back(compiled({{0x20400034u, 0u}, {0x00400030u, 0x00000011u}, {0xE2000001u, 0u}}));
  codes().push_back(compiled({{0x00400031u, 0x00000022u}}));
  for (Code& c : codes()) c.enabled = true;
  apply();
  CHECK(!codes()[0].enabled && !codes()[0].supported);
  CHECK(codes()[1].enabled);
  CHECK_EQ(peek(kBytes, 2), 0x0022u);
  set_native_reader(fake_read);
}

// ---- built in ----

void test_built_in() {
  auto label = [](const char* name, const Lines& lines) {
    Code c;
    c.name = name;
    c.lines = lines;
    const char* found = native_equivalent(c);
    return std::string(found ? found : "");
  };
  // A name alone is nothing: these are the names of the four optional switches, on other lines.
  for (const char* name : {"Optional: Widescreen 16:9", "Optional: Disable Screen Shake",
                           "Optional: Flash Red on Failed L-Cancel", "Optional: Lagless FoD"})
    CHECK(label(name, {{0x04400004u, 0x3FC00000u}}).empty());
  // Their first lines are something, whatever the code is called.
  CHECK(label("mine", {{0x043BB05Cu, 0x3EB00000u}}) == "Widescreen 16:9");
  CHECK(label("mine", {{0x04030E44u, 0x4E800020u}}) == "Disable Screen Shake");
  CHECK(label("mine", {{0xC20C0148u, 0x0000000Cu}}) == "Flash Red on Failed L-Cancel");
  CHECK(label("mine", {{0xC21CBB90u, 0x00000005u}}) == "Lagless FoD");
  CHECK(label("Optional: Disable Screen Shake", {{0x04030E44u, 0x60000000u}}).empty());

  // Any other code: every patch has to be one the table carries, word for word.
  const Lines hook = {{0xC2100010u, 2u}, {0x38600001u, 0x38800002u}, {0x60000000u, 0u}};
  CHECK(label("hook", hook) == "Switch");
  CHECK(label("hook", {hook[0], hook[1], hook[2], {0x04100020u, 0x38600001u}, {0xE0000000u, 0x80008000u}}) == "Switch");
  CHECK(label("hook", {{0x04100020u, 0x38600002u}}).empty());
  CHECK(label("hook", {hook[0], {0x38600001u, 0x38800003u}, hook[2]}).empty());
  CHECK(label("hook", {hook[0], hook[1], hook[2], {0x04100024u, 0x60000000u}}).empty());   // one patch more
  CHECK(label("hook", {hook[0], hook[1]}).empty());                                        // cut short
  // An injection of one instruction is the write of it.
  CHECK(label("hook", {{0xC2100020u, 1u}, {0x38600001u, 0u}}) == "Switch");
}

int run_all() {
  set_code_patches_allowed(false);   // the Source Port
  set_native_writer(fake_write);
  set_native_reader(fake_read);
  test_writes();
  test_base_and_pointer();
  test_ifs();
  test_registers();
  test_refused();
  test_codes_and_library();
  test_built_in();
  if (g_failures) std::printf("%d checks failed\n", g_failures);
  else std::printf("user gecko: all checks passed\n");
  return g_failures ? 1 : 0;
}

}  // namespace
}  // namespace user_gecko

int main() { return user_gecko::run_all(); }
