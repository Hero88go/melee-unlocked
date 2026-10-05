// Opt-in copy-and-patch cache. Guest RAM and dispatch are owned by the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ram_translator.h"
#include "leaf_translator.h"
#include "ppc_leaf_stencils.generated.h"
#include "host.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
const std::atomic<uint32_t>* mu_ram_version0 = nullptr;
const std::atomic<uint32_t>* mu_ram_version1 = nullptr;
uint32_t mu_ram_expected0 = 0, mu_ram_expected1 = 0;
bool mu_ram_invalidated = false;
}

namespace ppc {
namespace {
using stencil::CompiledLeaf;
struct Cached : std::enable_shared_from_this<Cached> {
  uint32_t address = 0;
  std::vector<uint8_t> source;
  // (offset, size) ranges of the source the translation depends on: all of it for refused code,
  // everything but the words no path reaches (inline data) for a translation.
  std::vector<std::pair<uint32_t, uint32_t>> compared;
  // One byte per word: a branch may land there natively (see plan_attempt). Empty: none planned.
  std::vector<uint8_t> direct;
  uint32_t dispatch_epoch = 0;
  std::array<uint32_t, 2> versions{};
  CompiledLeaf native;
  bool stale = false;
};
struct Invocation {
  Cached& code;
  uint8_t* ram;
  Invocation* parent;
};
bool enabled = false, hooks_installed = false;
// Ordered, so a continuation can find the translation that already contains its address.
std::map<uint32_t, std::shared_ptr<Cached>> cache;
Invocation* active = nullptr;
RamTranslatorStats totals;
constexpr size_t kCacheBytes = 128u * 1024u * 1024u;
constexpr size_t kCacheEntries = 2048;

// The last answer for an address, in front of the map. `inside`: the address is not the entry of
// `code` but an instruction inside it (continuations only). A slot is valid while index_epoch is
// the one it was filled at; every removal from the map moves the epoch, so no slot outlives its
// entry.
struct Slot {
  uint32_t address = 0, epoch = 0;
  Cached* code = nullptr;
  bool inside = false;
};
constexpr size_t kSlots = 4096;
Slot slots[kSlots];
uint32_t index_epoch = 1;

// Calls seen per address without a translation. Translating costs far more than interpreting a
// routine a few times, so code is translated only once it has been asked for `need` times, and
// each translation at the same address raises `need`: code that keeps changing (or keeps being
// refused) settles in the interpreter instead of being planned again every frame. Which path
// runs never changes what the guest computes, so none of this is visible to the simulation.
struct Heat {
  uint32_t address = 0, calls = 0, need = 0;
};
constexpr size_t kHeat = 4096;
constexpr uint32_t kDefaultHotCalls = 16, kMaxHotCalls = 1u << 16;
Heat heat[kHeat];
uint32_t hot_calls = kDefaultHotCalls;
bool plain_branches = false; // Every branch through the driver, every instruction guarded.
std::array<uint8_t, RAM_WATCH_COUNT> watched_here{}; // Blocks this cache asked to be watched.

uint32_t word_at(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint32_t first_block(const Cached& code) { return (code.address - RAM_BASE) >> RAM_WATCH_SHIFT; }
uint32_t last_block(const Cached& code) {
  return uint32_t((code.address - RAM_BASE + code.source.size() - 1) >> RAM_WATCH_SHIFT);
}
void save_versions(Cached& code) {
  const uint32_t first = first_block(code), last = last_block(code);
  for (uint32_t b = first; b <= last; ++b) {
    g_ram_watched[b].store(1, std::memory_order_relaxed);
    watched_here[b] = 1;
    code.versions[b - first] = g_ram_versions[b].load(std::memory_order_relaxed);
  }
}
bool generations_match(const Cached& code) {
  const uint32_t first = first_block(code), last = last_block(code);
  for (uint32_t b = first; b <= last; ++b)
    if (code.versions[b - first] != g_ram_versions[b].load(std::memory_order_relaxed)) return false;
  return true;
}
void select_invocation() {
  mu_ram_version0 = mu_ram_version1 = nullptr;
  mu_ram_invalidated = false;
  if (!active) return;
  const Cached& code = active->code;
  const uint32_t first = first_block(code), last = last_block(code);
  mu_ram_version0 = &g_ram_versions[first]; mu_ram_expected0 = code.versions[0];
  if (last != first) { mu_ram_version1 = &g_ram_versions[last]; mu_ram_expected1 = code.versions[1]; }
  mu_ram_invalidated = code.stale;
}
struct Scope {
  Invocation current;
  Scope(Cached& code, uint8_t* ram) : current{code, ram, active} { active = &current; select_invocation(); }
  ~Scope() { active = current.parent; select_invocation(); }
};
// The interpreter's transfer() continues in RAM at `address`: it is not a compiled target.
bool allows_direct(uint32_t address) { return !(lookup(address) && !runs_from_ram(address)); }
// The native branches of a translation were planned against the dispatch as it was. Every change
// of the dispatch moves g_ram_dispatch_epoch and the write generation of the blocks it touches
// (ppc_runtime.cpp dispatch_changed), so this runs wherever the source bytes are compared.
bool direct_same(Cached& code) {
  if (code.dispatch_epoch == g_ram_dispatch_epoch) return true;
  for (size_t i = 0; i < code.direct.size(); ++i)
    if (code.direct[i] != uint8_t(allows_direct(code.address + uint32_t(i) * 4))) return false;
  code.dispatch_epoch = g_ram_dispatch_epoch;
  return true;
}
// Nothing the translation was made from can have changed since it was last checked.
bool unchanged(const Cached& code) {
  return code.dispatch_epoch == g_ram_dispatch_epoch && generations_match(code);
}
// The slow check after a write generation or the dispatch moved. A data write in the same 64 KB
// block, or into inline data no path reaches, leaves the translation valid.
bool still_valid(Cached& code, uint8_t* ram) {
  const uint8_t* now = ram + (code.address - RAM_BASE);
  for (const auto& part : code.compared)
    if (std::memcmp(now + part.first, code.source.data() + part.first, part.second)) return false;
  if (!direct_same(code)) return false;
  save_versions(code);
  return true;
}
void set_compared(Cached& code) {
  code.compared.clear();
  const uint32_t size = uint32_t(code.source.size());
  uint32_t at = 0;
  if (code.native.ready()) {
    // No stencil was planned from an unreachable word and no entry leads into one: a transfer
    // there leaves through the driver and is looked up, or translated, from the bytes of then.
    for (const auto& run : code.native.unreachable_runs()) {
      const uint32_t lo = run.first * 4, hi = (run.second + 1) * 4;
      if (lo > at) code.compared.push_back({at, lo - at});
      at = std::max(at, hi);
    }
  }
  if (size > at) code.compared.push_back({at, size - at});
}
void forget(const std::shared_ptr<Cached>& code) {
  auto at = cache.find(code->address);
  if (at == cache.end() || at->second != code) return;
  totals.native_bytes -= code->native.allocation_bytes();
  cache.erase(at);
  ++index_epoch;
  ++totals.invalidated;
  // The shared owner in try_translate_ram retains active code and unwind records until it returns.
}
void remember(Slot& slot, uint32_t address, Cached& code, bool inside) {
  slot.address = address; slot.epoch = index_epoch; slot.code = &code; slot.inside = inside;
}
// A translation that already holds the instruction at `address` and can be entered there. The
// stencils of that instruction were planned from the same bytes a new translation would read,
// and every way out of them goes through the same driver, so continuing inside it computes what
// a translation starting at `address` would. Without this every return address became a segment
// of its own that repeated the rest of its function.
Cached* containing(uint32_t address, uint8_t* ram) {
  auto at = cache.upper_bound(address);
  for (int tries = 0; tries < 8 && at != cache.begin(); ++tries) {
    --at;
    Cached& outer = *at->second;
    if (address - outer.address >= stencil::kMaxFunctionBytes) break;
    if (outer.stale || !outer.native.enters_at(address)) continue;
    if (!unchanged(outer) && !still_valid(outer, ram)) continue; // Retired when it is asked for itself.
    return &outer;
  }
  return nullptr;
}
bool hot(uint32_t address) {
  if (hot_calls <= 1) return true;
  Heat& entry = heat[(address >> 2) & (kHeat - 1)];
  if (entry.address != address) { entry.address = address; entry.calls = 0; entry.need = hot_calls; }
  return ++entry.calls >= entry.need;
}
void planned(uint32_t address, bool native) {
  if (hot_calls <= 1) return;
  Heat& entry = heat[(address >> 2) & (kHeat - 1)];
  if (entry.address != address) return;
  entry.calls = 0;
  entry.need = std::min<uint32_t>(entry.need * (native ? 4u : 8u), kMaxHotCalls);
}

// A known redirected retail body has explicit bounds. Loaded code has none: scan only up to
// a terminal transfer that earlier branches do not jump over. Extents are an optimization hint;
// a transfer beyond them resumes the same interpreter invocation, never guesses a return.
size_t extent(const uint8_t* ram, uint32_t address) {
  uint32_t lo = 0, hi = 0;
  if (runs_from_ram(address) && function_bounds(address, &lo, &hi) && lo == address &&
      hi > address && hi - address <= stencil::kMaxFunctionBytes && hi - RAM_BASE <= RAM_SIZE)
    return hi - address;
  const size_t limit = std::min<size_t>(stencil::kMaxFunctionBytes, RAM_SIZE - (address - RAM_BASE));
  uint32_t needed = address;
  for (size_t off = 0; off + 4 <= limit; off += 4) {
    const uint32_t pc = address + uint32_t(off), w = word_at(ram + (pc - RAM_BASE));
    const uint32_t op = w >> 26, xo = (w >> 1) & 0x3FF, bo = (w >> 21) & 31;
    if (op == 16 || (op == 18 && (w & 1))) {
      uint32_t d = op == 18 ? w & 0x03FFFFFCu : w & 0xFFFCu;
      if (op == 18 ? d & 0x02000000u : d & 0x8000u) d |= op == 18 ? 0xFC000000u : 0xFFFF0000u;
      const uint32_t target = w & 2 ? d : pc + d;
      if (target > pc && target - address < limit && !(lookup(target) && !runs_from_ram(target)))
        needed = std::max(needed, target);
    }
    const bool terminal = (op == 18 && !(w & 1)) ||
      (op == 19 && (xo == 16 || xo == 528 || xo == 50) && !(w & 1) && (bo & 20) == 20);
    if (terminal && pc >= needed) return off + 4;
  }
  return limit & ~size_t(3);
}

uint32_t branch(Context& c, uint8_t* ram, uint32_t pc, uint32_t w, uint32_t entry_lr) {
  const uint32_t op = w >> 26, xo = (w >> 1) & 0x3FF;
  const bool linked = (w & 1) != 0, is_lr = op == 19 && xo == 16;
  uint32_t target;
  if (op == 19) target = is_lr ? c.lr : c.ctr;
  else {
    uint32_t d = op == 18 ? w & 0x03FFFFFCu : w & 0xFFFCu;
    if (op == 18 ? d & 0x02000000u : d & 0x8000u) d |= op == 18 ? 0xFC000000u : 0xFFFF0000u;
    target = w & 2 ? d : pc + d;
  }
  // A bclr into a compiled entry is an interpreter error. Hand over before changing CTR or LR.
  const bool compiled = lookup(target) && !runs_from_ram(target);
  if (is_lr && !linked && compiled && target != entry_lr) throw RamTranslationResume{pc};
  if (op != 18) {
    uint32_t bo = (w >> 21) & 31, bi = (w >> 16) & 31;
    if (xo == 528 && op == 19) bo |= 4;
    bool take = true;
    if (!(bo & 4)) { --c.ctr; take = bo & 2 ? c.ctr == 0 : c.ctr != 0; }
    if (!(bo & 16)) { const uint32_t bit = crbit(c, bi); take = take && (bo & 8 ? bit != 0 : bit == 0); }
    if (!take) return pc + 4;
  }
  if (linked) c.lr = pc + 4;
  if (is_lr && target == entry_lr) return 0;
  if (op == 16 || op == 18) ram_branch_poll(c, pc, target, linked);
  while (lookup(target) && !runs_from_ram(target)) {
    ppc::call(c, ram, target);
    if (linked) return pc + 4;
    target = c.lr;
    if (target == entry_lr) return 0;
  }
  return target;
}
// The end of branch() alone, for a taken native branch without link whose poll has just run:
// compiled targets are called and the transfer goes on at the address they return to.
uint32_t transfer(Context& c, uint8_t* ram, uint32_t target, uint32_t entry_lr) {
  while (lookup(target) && !runs_from_ram(target)) {
    ppc::call(c, ram, target);
    target = c.lr;
    if (target == entry_lr) return 0;
  }
  return target;
}
void invalidate_range(Context& c) { mu_ram_translation_invalidate(c.r[3], c.r[4]); }
void invalidate_trk(Context& c) {
  if (c.r[4] > c.r[3]) mu_ram_translation_invalidate(c.r[3], c.r[4] - c.r[3]);
}
void invalidate_all(Context&) { mu_ram_translation_invalidate(RAM_BASE, RAM_SIZE); }
uint32_t env_number(const char* name, uint32_t fallback) {
  const char* value = std::getenv(name);
  if (!value || !*value) return fallback;
  const unsigned long parsed = std::strtoul(value, nullptr, 10);
  return parsed ? uint32_t(std::min<unsigned long>(parsed, kMaxHotCalls)) : fallback;
}
bool env_is(const char* name, const char* wanted) {
  const char* value = std::getenv(name);
  return value && std::string(value) == wanted;
}
} // namespace

void reset_ram_translator() {
  for (auto& pair : cache) pair.second->stale = true;
  cache.clear(); totals = {};
  ++index_epoch;
  for (Heat& entry : heat) entry = Heat{};
  for (Invocation* i = active; i; i = i->parent) i->code.stale = true;
  select_invocation();
}
void configure_ram_translator(bool value) {
  reset_ram_translator(); enabled = value;
  // The dispatch reports its changes only while native branches can depend on them.
  g_ram_dispatch_watch = value;
  // Diagnostics. MELEE_RAM_TRANSLATOR_HOT=1 translates on the first call, as before the threshold.
  // MELEE_RAM_TRANSLATOR_PLAIN=1 plans as before native branches and guard placement. The
  // interpreter's MELEE_INTERP_POLL=compiled polls by other rules than a native back-edge.
  hot_calls = env_number("MELEE_RAM_TRANSLATOR_HOT", kDefaultHotCalls);
  plain_branches = env_is("MELEE_RAM_TRANSLATOR_PLAIN", "1") || env_is("MELEE_INTERP_POLL", "compiled");
  if (enabled && !hooks_installed) {
    add_entry_hook(0x803448D4u, invalidate_range); // ICInvalidateRange
    add_entry_hook(0x80328F50u, invalidate_trk);   // TRK_flush_cache(start, end)
    add_entry_hook(0x8000543Cu, invalidate_range); // __flush_cache
    add_entry_hook(0x8034490Cu, invalidate_all);   // ICFlashInvalidate
    hooks_installed = true;
  }
  host::log("RAM translator: %s (Static engine, interpreter fallback)", enabled ? "on" : "off");
}
void set_ram_translator_hot_calls(uint32_t calls) {
  hot_calls = std::min<uint32_t>(std::max<uint32_t>(calls, 1u), kMaxHotCalls);
  for (Heat& entry : heat) entry = Heat{};
}
RamTranslatorStats ram_translator_stats() {
  auto result = totals; result.entries = cache.size();
  if ((totals.translated || totals.refused) && env_is("MELEE_RAM_TRANSLATOR_STATS", "1")) {
    // What the write watch of the code blocks costs the rest of the game: every guest store into
    // a watched block is one more counted store.
    uint64_t blocks = 0, bumps = 0;
    for (uint32_t b = 0; b < RAM_WATCH_COUNT; ++b)
      if (watched_here[b]) { ++blocks; bumps += g_ram_versions[b].load(std::memory_order_relaxed); }
    host::log("RAM translator detail: cold=%llu joined=%llu hot=%u plain=%d watched_blocks=%llu watched_block_writes=%llu",
              (unsigned long long)totals.cold, (unsigned long long)totals.joined, hot_calls, int(plain_branches),
              (unsigned long long)blocks, (unsigned long long)bumps);
  }
  return result;
}

bool try_translate_ram(Context& c, uint8_t* ram, uint32_t address) {
  if (!enabled || c.entry || !ram || (address & 3) || address < RAM_BASE || address - RAM_BASE >= RAM_SIZE) return false;
  const uint32_t entry_lr = c.lr;
  bool continuation = false;
  for (;;) {
  if ((address & 3) || address < RAM_BASE || address - RAM_BASE >= RAM_SIZE) {
    resume_interpret(c, ram, address, entry_lr);
    return true;
  }
  std::shared_ptr<Cached> code;
  uint32_t start = 0; // Nonzero: continue at this instruction inside `code`.
  Slot& slot = slots[(address >> 2) & (kSlots - 1)];
  if (slot.address == address && slot.epoch == index_epoch && (continuation || !slot.inside)) {
    Cached& found = *slot.code;
    if (!found.stale && (unchanged(found) || still_valid(found, ram))) {
      code = found.shared_from_this();
      if (slot.inside) start = address;
      ++totals.hits;
    }
  }
  if (!code) {
    if (auto at = cache.find(address); at != cache.end()) {
      code = at->second;
      if (code->stale || (!unchanged(*code) && !still_valid(*code, ram))) { forget(code); code.reset(); }
      else { ++totals.hits; remember(slot, address, *code, false); }
    }
  }
  if (!code && continuation) {
    if (Cached* outer = containing(address, ram)) {
      code = outer->shared_from_this();
      start = address;
      ++totals.hits; ++totals.joined;
      remember(slot, address, *outer, true);
    }
  }
  if (!code) {
    if (!hot(address)) {
      // Not worth a translation yet: the interpreter runs it, exactly as with the switch off.
      ++totals.cold;
      if (!continuation) return false;
      ++totals.resumed; resume_interpret(c, ram, address, entry_lr); return true;
    }
    try {
      code = std::make_shared<Cached>(); code->address = address;
      const size_t bytes = extent(ram, address);
      code->source.assign(ram + (address - RAM_BASE), ram + (address - RAM_BASE) + bytes);
      if (!plain_branches) {
        code->direct.resize(bytes / 4);
        for (size_t i = 0; i < code->direct.size(); ++i)
          code->direct[i] = uint8_t(allows_direct(address + uint32_t(i) * 4));
      }
      code->dispatch_epoch = g_ram_dispatch_epoch;
      save_versions(*code);
      std::string error;
      const bool ok = stencil::translate_leaf(code->source.data(), bytes, address,
        stencil::generated::table, code->native, error, nullptr, true,
        code->direct.empty() ? nullptr : &code->direct);
      set_compared(*code);
      planned(address, ok);
      if (ok) ++totals.translated; else ++totals.refused;
      if (totals.translated + totals.refused <= 32)
        host::log("RAM translator: %08X %zu bytes %s%s%s", address, bytes, ok ? "native" : "fallback",
                  ok ? "" : ": ", ok ? "" : error.c_str());
      if (cache.size() >= kCacheEntries || totals.native_bytes + code->native.allocation_bytes() > kCacheBytes) {
        // Existing active owners retain their code. Clearing this index only evicts future entries.
        cache.clear(); totals.native_bytes = 0;
        ++index_epoch;
      }
      totals.native_bytes += code->native.allocation_bytes();
      cache.emplace(address, code);
      remember(slot, address, *code, false);
    } catch (const std::bad_alloc&) {
      ++totals.refused;
      if (!continuation) return false;
      ++totals.resumed; resume_interpret(c, ram, address, entry_lr); return true;
    }
  }
  if (!code->native.ready()) {
    if (!continuation) return false;
    ++totals.resumed; resume_interpret(c, ram, address, entry_lr); return true;
  }
  uint32_t next = 0;
  bool deopt = false;
  {
    Scope scope(*code, ram);
    stencil::RuntimeHooks hooks{};
    hooks.branch = branch; hooks.entry_lr = entry_lr; hooks.continuation = continuation;
    hooks.resume_pc = &next; hooks.start = start; hooks.transfer = transfer;
    try {
      if (!code->native.run(c, ram, &hooks, true)) {
        fatal(c, "runtime translation driver failed", address);
        return true;
      }
    } catch (const RamTranslationResume& resume) {
      next = resume.pc; deopt = true;
    }
  }
  if (deopt) {
    ++totals.resumed;
    resume_interpret(c, ram, next, entry_lr);
    return true;
  }
  if (!next) return true;
  // RAM calls and computed transfers are parts of this same invocation, as in Interp::transfer.
  // Continue through cached native segments without another host frame or a synthetic enter().
  address = next; continuation = true;
  }
}
} // namespace ppc

extern "C" void mu_ram_translation_guard(uint32_t pc) {
  using namespace ppc;
  if (!active) return;
  auto& code = active->code;
  if (code.stale || !still_valid(code, active->ram)) {
    code.stale = true; mu_ram_invalidated = true;
    throw RamTranslationResume{pc};
  }
  select_invocation(); // A data-only write in the same 64 KB block does not invalidate code.
}
extern "C" void mu_ram_translation_invalidate(uint32_t address, uint32_t bytes) {
  using namespace ppc;
  if (!enabled || !bytes) return;
  const uint32_t off = address & 0x3FFFFFFFu;
  if (off >= RAM_SIZE) return;
  const uint32_t lo = RAM_BASE + off, hi = lo + std::min(bytes, RAM_SIZE - off);
  const auto overlaps = [&](const Cached& code) { return lo < code.address + code.source.size() && code.address < hi; };
  for (auto it = cache.begin(); it != cache.end();) {
    if (!overlaps(*it->second)) { ++it; continue; }
    it->second->stale = true;
    totals.native_bytes -= it->second->native.allocation_bytes(); ++totals.invalidated;
    it = cache.erase(it);
    ++index_epoch;
  }
  for (Invocation* i = active; i; i = i->parent) if (overlaps(i->code)) i->code.stale = true;
  select_invocation();
}
