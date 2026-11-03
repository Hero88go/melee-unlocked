// Exercise the actual host adapter without a disc, renderer or PPC runtime.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "yougame_memcard.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
bool active = false;
std::array<uint8_t, 0x20> data{};
std::map<uint32_t, ppc::Fn> dispatch;
std::vector<uint32_t> interpreted;
unsigned inputs = 0, messages = 0;
constexpr uint32_t base = 0x80480DA8, input_addr = 0x801AEDC8, message_addr = 0x801AEE6C;
void original_input(ppc::Context& c, uint8_t*) { ++inputs; c.r[3] = 0x80002; }
void original_message(ppc::Context&, uint8_t*) { ++messages; }
void set_decision(unsigned decision) { data[0x17] = decision; }
}
namespace yougame { bool active() { return ::active; } }
namespace ppc {
Fn lookup(uint32_t addr) { const auto it = dispatch.find(addr); return it == dispatch.end() ? nullptr : it->second; }
Fn set_hook(uint32_t addr, Fn fn) { Fn old = lookup(addr); dispatch[addr] = fn; return old; }
bool redirect_to_interpreter(uint32_t addr) { interpreted.push_back(addr); return true; }
}
namespace host {
uint8_t rd8(uint32_t addr) { CHECK(addr >= base && addr - base < data.size()); return data[addr - base]; }
uint32_t rd32(uint32_t addr) {
  return (uint32_t(rd8(addr)) << 24) | (uint32_t(rd8(addr + 1)) << 16) |
         (uint32_t(rd8(addr + 2)) << 8) | rd8(addr + 3);
}
void wr8(uint32_t addr, uint8_t v) { CHECK(addr == base + 0x1C); data[addr - base] = v; }
void log(const char*, ...) {}
}

int main() {
  using yougame::memcard::install_static_boot;
  install_static_boot();
  CHECK(dispatch.empty() && interpreted.empty()); // Standalone installs nothing.
  active = true;
  install_static_boot();
  CHECK(dispatch.empty() && interpreted.empty()); // Missing symbols leave the menu alone.
  dispatch[input_addr] = original_input;
  dispatch[message_addr] = original_message;
  install_static_boot();
  CHECK(interpreted == (std::vector<uint32_t>{0x801AF568, 0x801AF0D4, 0x801AF250}));
  install_static_boot();
  CHECK(interpreted.size() == 3); // One installation.

  ppc::Context c;
  for (unsigned enabled = 0; enabled != 2; ++enabled) {
    active = enabled;
    for (unsigned chan = 0; chan != 2; ++chan) {
      data[4] = chan;
      for (unsigned decision = 0; decision != 21; ++decision) {
        set_decision(decision);
        for (uint32_t lr : {0x801AF100u, 0x801AF900u, 0x801B0264u, 0x80200000u}) {
          data[0x1C] = 1; // A held physical direction must not choose No.
          c.lr = lr;
          const unsigned before = inputs;
          dispatch[input_addr](c, nullptr);
          const bool answer = enabled && chan == 0 && (decision == 5 || decision == 7) && lr < 0x801B0264;
          CHECK(c.r[3] == (answer ? 0x100u : 0x80002u));
          CHECK(inputs == before + (answer ? 0 : 1));
          CHECK(data[0x1C] == (answer ? 0 : 1));
        }
      }
      for (unsigned msg = 0; msg != 25; ++msg) {
        c.r[4] = msg;
        const unsigned before = messages;
        dispatch[message_addr](c, nullptr);
        CHECK(messages == before + ((enabled && chan == 0 && (msg == 10 || msg == 12)) ? 0 : 1));
      }
    }
  }
  std::puts("YouGame Static Recomp memory-card adapter passed");
}
