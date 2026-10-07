// Run the shipped console handler rather than approximating individual Gecko types.
// Source has no reference to this translation unit and links no PPC runner.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "static_gecko.h"
#include "user_gecko.h"
#include "host.h"
#include "gecko_data.h"
#include "slippi_online.h"
#include "slippi_playback.h"
#include "ppc.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace user_gecko {
namespace {
constexpr uint32_t kHandlerOffset = 0x1800, kStackBytes = 0x4000;
uint32_t region = 0, region_bytes = 0, entry = 0, list = 0, stack = 0;
uint32_t last_tick = ~0u;
bool running = false, live = false, warned = false, failed = false;
std::vector<uint32_t> selection;
struct Changed { uint32_t original, installed; bool restore; };
std::map<uint32_t, Changed> changes;

bool code_address(uint32_t address) {
  return (address >= 0x80003100u && address < 0x80005520u) ||
         (address >= 0x80005940u && address < 0x803B7240u);
}
void restore() {
  for (const auto& item : changes)
    if (item.second.restore && host::rd32(item.first) == item.second.installed)
      host::wr32(item.first, item.second.original);
  changes.clear(); live = false;
}
void observe(ppc::Context&, uint32_t address, uint32_t bytes, uint64_t value) {
  const uint32_t offset = address & 0x3FFFFFFFu;
  if (offset >= ppc::RAM_SIZE || bytes > ppc::RAM_SIZE - offset) return;
  address = ppc::RAM_BASE | offset;
  if (address >= region && address < region + region_bytes) return;
  // Restore fixed game variables and instructions, plus installed RAM branches.
  // Transient fighter/heap data is allowed to change naturally with the game.
  const bool restorable = address < 0x80500000u || (bytes == 4 && (uint32_t(value) >> 26) == 18);
  for (uint32_t i = 0; i < bytes; ++i) {
    const uint8_t next = uint8_t(value >> (8 * (bytes - i - 1)));
    const uint32_t word = (address + i) & ~3u, shift = (3u - ((address + i) & 3u)) * 8;
    auto at = changes.find(word);
    if (at == changes.end()) {
      if (changes.size() >= 256 * 1024) continue;
      const uint32_t original = host::rd32(word);
      at = changes.emplace(word, Changed{original, original, restorable}).first;
    }
    at->second.installed = (at->second.installed & ~(0xFFu << shift)) | (uint32_t(next) << shift);
    at->second.restore |= restorable;
  }
  if (code_address(address)) ppc::redirect_function_at(address);
}

uint32_t reserve() {
  // A launch with no code switched on keeps exactly the original guest memory map: the reservation
  // comes out of the game's heap, and large mod discs (ACE) were running out of it with codes
  // imported but all off.
  if (codes().empty() || !any_enabled()) return 0;
  size_t bytes = 0;
  for (const auto& code : codes()) bytes += code.lines.size() * 8;
  const uint64_t need = kHandlerOffset + gecko::codehandler_bin_size + bytes + kStackBytes + 32;
  if (need > 1024 * 1024) return 0;
  return uint32_t((need + 0xFFFF) & ~uint64_t(0xFFFF));
}
void start(uint32_t address, uint32_t bytes) {
  region = address; region_bytes = bytes;
  stack = address + bytes - 0x100;
  entry = list = 0; selection.clear(); changes.clear(); live = false;
  if (!address || gecko::codehandler_bin_size < 0xB0) return;
  const uint32_t original_list = 0x80001800u + uint32_t(gecko::codehandler_bin_size) - 8;
  list = address + kHandlerOffset + uint32_t(gecko::codehandler_bin_size) - 8;
  std::memcpy(host::ptr(address + kHandlerOffset, uint32_t(gecko::codehandler_bin_size)),
              gecko::codehandler_bin, gecko::codehandler_bin_size);
  // The handler's core begins with mflr r29; lis/ori r15,codelist. Running
  // this core avoids the USB debugger while retaining all standard code types.
  for (uint32_t off = 0; off + 12 <= gecko::codehandler_bin_size; off += 4) {
    const uint32_t at = address + kHandlerOffset + off;
    if (host::rd32(at) == 0x7FA802A6u &&
        host::rd32(at + 4) == (0x3DE00000u | (original_list >> 16)) &&
        host::rd32(at + 8) == (0x61EF0000u | (original_list & 0xFFFFu))) {
      if (entry) host::die("Gecko handler core is ambiguous");
      entry = at;
      host::wr32(at + 4, 0x3DE00000u | (list >> 16));
      host::wr32(at + 8, 0x61EF0000u | (list & 0xFFFFu));
    }
  }
  if (!entry) host::die("Gecko handler core could not be located");
  host::log("gecko: console handler at %08X, table %08X, reserved %u bytes", entry, list, bytes);
  ppc::add_ram_code_range(address, address + bytes);
  host::mark_ram_write(address, bytes);
}
void run(const std::vector<Code>& codes) {
  if (running || !host::cpu || !host::ram) return;
  const uint32_t tick = host::retrace_count();
  if (tick == last_tick) return;
  last_tick = tick;
  if (slippi::online::session_mode() >= 0 || slippi::playback::enabled()) {
    if (live) { restore(); selection.clear(); }
    return;
  }
  std::vector<uint32_t> next;
  for (const auto& code : codes) if (code.enabled && code.supported) {
    for (const auto& line : code.lines) { next.push_back(line.first); next.push_back(line.second); }
    // Codes in one list retain the console's shared base, pointer and registers.
  }
  if (next.empty()) { if (live) restore(); selection.clear(); return; }
  if (!entry || uint64_t(list) + 16 + next.size() * 4 > uint64_t(stack) - kStackBytes) {
    if (!warned) { host::log("gecko: restart after adding codes so their handler/table memory can be reserved"); warned = true; }
    return;
  }
  if (next != selection) {
    if (live) restore();
    selection = next;
    failed = false;
    host::wr32(list, 0x00D0C0DEu); host::wr32(list + 4, 0x00D0C0DEu);
    for (size_t i = 0; i < next.size(); ++i) host::wr32(list + 8 + uint32_t(i * 4), next[i]);
    host::wr32(list + 8 + uint32_t(next.size() * 4), 0xF0000000u);
    host::wr32(list + 12 + uint32_t(next.size() * 4), 0);
    live = true;
  }
  if (failed) return;
  struct Scope {
    ppc::Context saved;
    ppc::StoreObserver previous;
    ppc::InterpreterBudget budget;
    Scope() : saved(*host::cpu), previous(ppc::store_observer()), budget(ppc::execution_budget) {
      running = true; ppc::store_observer() = observe; ppc::execution_budget = {true, 128000000};
    }
    ~Scope() { ppc::store_observer() = previous; ppc::execution_budget = budget;
      *host::cpu = saved; ppc::update_mxcsr(*host::cpu); running = false; }
  } scope;
  host::cpu->r[1] = stack;
  host::cpu->r[31] = region & 0xFFFF0000u;
  host::cpu->msr &= ~0x8000u; // The console handler runs with external interrupts disabled.
  host::cpu->lr = 0; host::cpu->entry = 0;
  try { ppc::interpret(*host::cpu, host::ram, entry); }
  catch (const ppc::ExecutionBudgetExceeded&) {
    failed = true; restore(); host::log("gecko: code execution did not return within its instruction budget; codes suspended");
  }
}
}  // namespace
void install_static_runtime() { set_static_runtime(run, reserve, start); }
}  // namespace user_gecko
