// Holds native routines to the translated game, bit for bit.
//
// The translated game is a faithful copy of every routine in the console binary, and each one can be
// called on its own. So for a native routine that must give the console's results, the check is
// direct: run both on the same inputs, many thousands of times, and compare the output bits. A
// disagreement names the routine, the inputs and the differing bits, which is what a replay that
// drifts on frame 4000 never tells you.
//
//   port_native_parity --iso <disc> --dll build-sourceport-gcc/mu_parity.dll [--iterations N] [--only name] [--edge]
//
// The library exports a table describing each routine (port/runtime/abi/mu_parity.h). Pointer
// arguments are float arrays copied into guest RAM in big-endian order for the translated call and
// handed to the native call as they are; every array is compared afterwards, written or not, so a
// stray write shows too. Routines that write an output that may alias an input are run that way as
// well, because the console versions special-case it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "ppc.h"
#include "abi/mu_parity.h"
#include "exi_slippi.h"
#include "slippi_online.h"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ppc { void init_dispatch(); void call(Context& c, uint8_t* m, uint32_t addr); }
namespace guest {
struct NameEntry { uint32_t addr; const char* name; };
extern const NameEntry name_table[];
extern const size_t name_table_count;
}

namespace {

constexpr uint32_t R2 = 0x804DF9E0u, R13 = 0x804DB6A0u;   // small data bases set by __init_registers
constexpr uint32_t GUEST_STACK = 0x81700000u, GUEST_SCRATCH = 0x81600000u;

struct Rng {
  uint64_t s;
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
  double range(double lo, double hi) { return lo + (hi - lo) * unit(); }
};

bool g_edge = false;

// A spread of magnitudes the game actually produces: positions and matrices near unity, distances
// in the hundreds, tiny velocities, exact zeros and ones. Edge mode adds denormals and infinities.
float gen_float(Rng& rng) {
  uint64_t pick = rng.next() % 100;
  double v;
  if (pick < 55) v = rng.range(-2.0, 2.0);
  else if (pick < 72) v = rng.range(-1000.0, 1000.0);
  else if (pick < 82) v = rng.range(-1e-5, 1e-5);
  else if (pick < 90) v = (double)(int)(rng.next() % 5) - 2.0;            // -2..2 exactly
  else if (pick < 96) v = rng.range(-1e9, 1e9);
  else if (!g_edge) v = rng.range(-1e-30, 1e-30);
  else {
    switch (rng.next() % 4) {
      case 0: { uint32_t bits = (uint32_t)(rng.next() & 0x807FFFFFu); float f; std::memcpy(&f, &bits, 4); return f; }   // denormal
      case 1: return (rng.next() & 1) ? INFINITY : -INFINITY;
      case 2: return -0.0f;
      default: return std::nanf("");
    }
  }
  return (float)v;
}

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void put_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
uint64_t dbits(double d) { uint64_t u; std::memcpy(&u, &d, 8); return u; }

uint32_t guest_address(const char* name) {
  for (size_t i = 0; i < guest::name_table_count; ++i)
    if (std::strcmp(guest::name_table[i].name, name) == 0) return guest::name_table[i].addr;
  return 0;
}

struct Call {
  const MuParityEntry& e;
  std::vector<std::vector<float>> arrays;   // one per pointer argument, after the call
  int alias[3] = {-1, -1, -1};             // alias[i] = j: pointer i is the same buffer as pointer j
  uint8_t chr = 0;
  double scalar[3] = {0, 0, 0};
  uint32_t ret_u32 = 0;
  double ret_real = 0;
};

// Native call through the shape the table describes.
bool call_native(Call& c, std::vector<std::vector<float>>& bufs) {
  const MuParityEntry& e = c.e;
  float* p[3] = {nullptr, nullptr, nullptr};
  for (int i = 0; i < e.n_ptr; ++i) p[i] = bufs[c.alias[i] >= 0 ? c.alias[i] : i].data();
  void* fn = e.fn;
  const float s0 = (float)c.scalar[0], s1 = (float)c.scalar[1], s2 = (float)c.scalar[2];
#define SHAPE(np, hc, ns, dbl, r) (e.n_ptr == (np) && e.has_char == (hc) && e.n_scalar == (ns) && e.is_double == (dbl) && e.ret == (r))
  if (SHAPE(1, 0, 0, 0, MU_RET_NONE)) ((void (*)(float*))fn)(p[0]);
  else if (SHAPE(2, 0, 0, 0, MU_RET_NONE)) ((void (*)(float*, float*))fn)(p[0], p[1]);
  else if (SHAPE(3, 0, 0, 0, MU_RET_NONE)) ((void (*)(float*, float*, float*))fn)(p[0], p[1], p[2]);
  else if (SHAPE(2, 0, 0, 0, MU_RET_U32)) c.ret_u32 = ((uint32_t (*)(float*, float*))fn)(p[0], p[1]);
  else if (SHAPE(1, 0, 3, 0, MU_RET_NONE)) ((void (*)(float*, float, float, float))fn)(p[0], s0, s1, s2);
  else if (SHAPE(1, 1, 2, 0, MU_RET_NONE)) ((void (*)(float*, char, float, float))fn)(p[0], (char)c.chr, s0, s1);
  else if (SHAPE(2, 0, 1, 0, MU_RET_NONE)) ((void (*)(float*, float*, float))fn)(p[0], p[1], s0);
  else if (SHAPE(1, 0, 0, 0, MU_RET_REAL)) c.ret_real = ((float (*)(float*))fn)(p[0]);
  else if (SHAPE(2, 0, 0, 0, MU_RET_REAL)) c.ret_real = ((float (*)(float*, float*))fn)(p[0], p[1]);
  else if (SHAPE(0, 0, 1, 0, MU_RET_REAL)) c.ret_real = ((float (*)(float))fn)(s0);
  else if (SHAPE(0, 0, 2, 0, MU_RET_REAL)) c.ret_real = ((float (*)(float, float))fn)(s0, s1);
  else if (SHAPE(0, 0, 1, 1, MU_RET_REAL)) c.ret_real = ((double (*)(double))fn)(c.scalar[0]);
  else if (SHAPE(0, 0, 2, 1, MU_RET_REAL)) c.ret_real = ((double (*)(double, double))fn)(c.scalar[0], c.scalar[1]);
  else return false;
#undef SHAPE
  for (int i = 0; i < e.n_ptr; ++i) c.arrays[i] = bufs[c.alias[i] >= 0 ? c.alias[i] : i];
  return true;
}

// The same call through the translated routine, with the arrays in guest RAM.
void call_guest(Call& c, uint32_t addr, const std::vector<std::vector<float>>& bufs) {
  const MuParityEntry& e = c.e;
  ppc::Context& cpu = *host::cpu;
  uint32_t guest_ptr[3] = {0, 0, 0};
  for (int i = 0; i < e.n_ptr; ++i) {
    int owner = c.alias[i] >= 0 ? c.alias[i] : i;
    guest_ptr[i] = GUEST_SCRATCH + (uint32_t)owner * 0x100u;
    if (owner == i) {
      uint8_t* dst = host::ptr(guest_ptr[i], (uint32_t)bufs[i].size() * 4);
      for (size_t k = 0; k < bufs[i].size(); ++k) put_be32(dst + 4 * k, fbits(bufs[i][k]));
    }
  }
  std::memset(cpu.r, 0, sizeof cpu.r);
  cpu.r[1] = GUEST_STACK; cpu.r[2] = R2; cpu.r[13] = R13;
  int gpr = 3;
  for (int i = 0; i < e.n_ptr; ++i) cpu.r[gpr++] = guest_ptr[i];
  if (e.has_char) cpu.r[gpr++] = c.chr;
  for (int i = 0; i < e.n_scalar; ++i) cpu.f[1 + i].ps0 = cpu.f[1 + i].ps1 = e.is_double ? c.scalar[i] : (double)(float)c.scalar[i];
  cpu.lr = 0; cpu.cr[0] = 0;
  ppc::call(cpu, host::ram, addr);
  if (e.ret == MU_RET_U32) c.ret_u32 = cpu.r[3];
  if (e.ret == MU_RET_REAL) c.ret_real = e.is_double ? cpu.f[1].ps0 : (double)(float)cpu.f[1].ps0;
  for (int i = 0; i < e.n_ptr; ++i) {
    int owner = c.alias[i] >= 0 ? c.alias[i] : i;
    const uint8_t* src = host::ptr(guest_ptr[i], (uint32_t)bufs[owner].size() * 4);
    c.arrays[i].resize(bufs[owner].size());
    for (size_t k = 0; k < bufs[owner].size(); ++k) { uint32_t u = be32(src + 4 * k); std::memcpy(&c.arrays[i][k], &u, 4); }
  }
}

std::string describe(const Call& c, const std::vector<std::vector<float>>& inputs) {
  std::string s;
  char buf[64];
  for (int i = 0; i < c.e.n_ptr; ++i) {
    s += " p" + std::to_string(i) + (c.alias[i] >= 0 ? "=p" + std::to_string(c.alias[i]) : "") + "[";
    int owner = c.alias[i] >= 0 ? c.alias[i] : i;
    for (size_t k = 0; k < inputs[owner].size(); ++k) { std::snprintf(buf, sizeof buf, "%s%08X", k ? " " : "", fbits(inputs[owner][k])); s += buf; }
    s += "]";
  }
  if (c.e.has_char) { std::snprintf(buf, sizeof buf, " char=%c", c.chr); s += buf; }
  for (int i = 0; i < c.e.n_scalar; ++i) {
    if (c.e.is_double) std::snprintf(buf, sizeof buf, " f%d=%016llX", i + 1, (unsigned long long)dbits(c.scalar[i]));
    else std::snprintf(buf, sizeof buf, " f%d=%08X", i + 1, fbits((float)c.scalar[i]));
    s += buf;
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  std::string iso, dll = "build-sourceport-gcc/mu_parity.dll", only;
  int iterations = 20000;
  uint64_t seed = 0x9E3779B97F4A7C15ull;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&] { if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); } return std::string(argv[++i]); };
    if (a == "--iso") iso = next();
    else if (a == "--dll") dll = next();
    else if (a == "--iterations") iterations = std::atoi(next().c_str());
    else if (a == "--only") only = next();
    else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 0);
    else if (a == "--edge") g_edge = true;
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (iso.empty()) { std::fprintf(stderr, "usage: port_native_parity --iso <disc> [--dll <mu_parity.dll>] [--iterations N] [--only name] [--edge]\n"); return 2; }

  HMODULE lib = LoadLibraryA(dll.c_str());
  if (!lib) { std::fprintf(stderr, "cannot load %s (error %lu)\n", dll.c_str(), GetLastError()); return 2; }
  auto table_fn = (MuParityTableFn)GetProcAddress(lib, "mu_parity_table");
  if (!table_fn) { std::fprintf(stderr, "%s exports no mu_parity_table\n", dll.c_str()); return 2; }
  int count = 0;
  const MuParityEntry* table = table_fn(&count);

  host::options.iso = iso;
  host::options.quiet = true;
  host::options.log_file = "native_parity.log";
  host::options.time_base = 1;
  if (!host::disc_open(iso)) { std::fprintf(stderr, "cannot open %s\n", iso.c_str()); return 2; }
  ppc::init_dispatch();
  host::boot_setup();
  // The console ran the C runtime's static initialisers from a table before main; the maths library
  // fills one of its tables that way (trigf.c). The parity library does the same on its side.
  for (size_t i = 0; i < guest::name_table_count; ++i)
    if (std::strncmp(guest::name_table[i].name, "__sinit_", 8) == 0) host::call_guest(guest::name_table[i].addr);

  int failed_routines = 0, skipped = 0;
  std::setvbuf(stdout, nullptr, _IONBF, 0);   // results must survive whatever the runtime does at exit
  std::printf("%-20s %10s %10s %s\n", "routine", "calls", "mismatches", "first mismatch");
  for (int t = 0; t < count; ++t) {
    const MuParityEntry& e = table[t];
    if (!only.empty() && only != e.name) continue;
    uint32_t addr = guest_address(e.name);
    if (!addr) { std::printf("%-20s %10s %10s not in the translated game\n", e.name, "-", "-"); ++skipped; continue; }
    // Alias variants: none, then each written output over each input of the same length.
    std::vector<std::array<int, 3>> variants = {{-1, -1, -1}};
    for (int o = 0; o < e.n_ptr; ++o) if (e.ptr_written[o])
      for (int in = 0; in < e.n_ptr; ++in) if (in != o && e.ptr_floats[in] == e.ptr_floats[o]) { std::array<int, 3> v = {-1, -1, -1}; v[o] = in; variants.push_back(v); }
    Rng rng{seed ^ (uint64_t)t * 0x2545F4914F6CDD1Dull};
    long long calls = 0, mismatches = 0;
    std::string first;
    bool shape_ok = true;
    for (const auto& v : variants) {
      for (int it = 0; it < iterations && shape_ok; ++it) {
        Call native{e}, guest{e};
        std::vector<std::vector<float>> inputs(e.n_ptr);
        for (int i = 0; i < e.n_ptr; ++i) { inputs[i].resize(e.ptr_floats[i]); for (float& f : inputs[i]) f = gen_float(rng); }
        for (auto* c : {&native, &guest}) {
          c->arrays.resize(e.n_ptr);
          for (int i = 0; i < 3; ++i) c->alias[i] = v[i];
        }
        static const char axes[] = {'x', 'y', 'z', 'X', 'Y', 'Z'};
        native.chr = guest.chr = axes[rng.next() % 6];
        for (int i = 0; i < e.n_scalar; ++i) {
          double v = e.is_double ? rng.range(-8.0, 8.0) : (double)gen_float(rng);
          if (e.scalar_min[i] != 0.0f || e.scalar_max[i] != 0.0f) v = (double)(float)rng.range(e.scalar_min[i], e.scalar_max[i]);
          native.scalar[i] = guest.scalar[i] = v;
        }
        std::vector<std::vector<float>> native_bufs = inputs;
        if (!call_native(native, native_bufs)) { shape_ok = false; break; }
        call_guest(guest, addr, inputs);
        ++calls;
        bool same = native.ret_u32 == guest.ret_u32 && dbits(native.ret_real) == dbits(guest.ret_real);
        for (int i = 0; same && i < e.n_ptr; ++i)
          for (size_t k = 0; k < native.arrays[i].size(); ++k)
            if (fbits(native.arrays[i][k]) != fbits(guest.arrays[i][k])) { same = false; break; }
        if (same) continue;
        ++mismatches;
        if (first.empty()) {
          first = describe(native, inputs);
          char buf[96];
          if (e.ret == MU_RET_REAL) { std::snprintf(buf, sizeof buf, " -> native %016llX guest %016llX", (unsigned long long)dbits(native.ret_real), (unsigned long long)dbits(guest.ret_real)); first += buf; }
          if (e.ret == MU_RET_U32) { std::snprintf(buf, sizeof buf, " -> native %u guest %u", native.ret_u32, guest.ret_u32); first += buf; }
          for (int i = 0; i < e.n_ptr; ++i)
            for (size_t k = 0; k < native.arrays[i].size(); ++k)
              if (fbits(native.arrays[i][k]) != fbits(guest.arrays[i][k])) { std::snprintf(buf, sizeof buf, " p%d[%zu] native %08X guest %08X", i, k, fbits(native.arrays[i][k]), fbits(guest.arrays[i][k])); first += buf; }
        }
      }
    }
    if (!shape_ok) { std::printf("%-20s %10s %10s the harness has no call shape for this signature\n", e.name, "-", "-"); ++skipped; continue; }
    std::printf("%-20s %10lld %10lld %s\n", e.name, calls, mismatches, first.c_str());
    if (mismatches) ++failed_routines;
  }
  std::printf("%d routine(s) disagree, %d skipped\n", failed_routines, skipped);
  // The runtime keeps worker threads that the game joins on its way out; a joinable thread left to
  // static destruction takes the process down with it.
  host::gcadapter_shutdown();
  host::switchpro_shutdown();
  slippi::shutdown();
  return failed_routines ? 1 : 0;
}
