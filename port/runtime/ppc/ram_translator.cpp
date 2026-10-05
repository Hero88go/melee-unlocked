// Opt-in copy-and-patch cache. Guest RAM and dispatch are owned by the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ram_translator.h"
#include "leaf_translator.h"
#include "ppc_leaf_stencils.generated.h"
#include "host.h"
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

extern "C" {
const std::atomic<uint32_t>* mu_ram_version0 = nullptr;
const std::atomic<uint32_t>* mu_ram_version1 = nullptr;
uint32_t mu_ram_expected0 = 0, mu_ram_expected1 = 0;
bool mu_ram_invalidated = false;
}

namespace ppc {
namespace {
using stencil::CompiledLeaf;
struct Cached {
  uint32_t address = 0;
  std::vector<uint8_t> source;
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
std::unordered_map<uint32_t, std::shared_ptr<Cached>> cache;
Invocation* active = nullptr;
RamTranslatorStats totals;
constexpr size_t kCacheBytes = 128u * 1024u * 1024u;
constexpr size_t kCacheEntries = 2048;

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
bool same_bytes(Cached& code, uint8_t* ram) {
  if (std::memcmp(ram + (code.address - RAM_BASE), code.source.data(), code.source.size())) return false;
  save_versions(code);
  return true;
}
void forget(const std::shared_ptr<Cached>& code) {
  auto at = cache.find(code->address);
  if (at == cache.end() || at->second != code) return;
  totals.native_bytes -= code->native.allocation_bytes();
  cache.erase(at);
  ++totals.invalidated;
  // The shared owner in try_translate_ram retains active code and unwind records until it returns.
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
void invalidate_range(Context& c) { mu_ram_translation_invalidate(c.r[3], c.r[4]); }
void invalidate_trk(Context& c) {
  if (c.r[4] > c.r[3]) mu_ram_translation_invalidate(c.r[3], c.r[4] - c.r[3]);
}
void invalidate_all(Context&) { mu_ram_translation_invalidate(RAM_BASE, RAM_SIZE); }
} // namespace

void reset_ram_translator() {
  for (auto& pair : cache) pair.second->stale = true;
  cache.clear(); totals = {};
  for (Invocation* i = active; i; i = i->parent) i->code.stale = true;
  select_invocation();
}
void configure_ram_translator(bool value) {
  reset_ram_translator(); enabled = value;
  if (enabled && !hooks_installed) {
    add_entry_hook(0x803448D4u, invalidate_range); // ICInvalidateRange
    add_entry_hook(0x80328F50u, invalidate_trk);   // TRK_flush_cache(start, end)
    add_entry_hook(0x8000543Cu, invalidate_range); // __flush_cache
    add_entry_hook(0x8034490Cu, invalidate_all);   // ICFlashInvalidate
    hooks_installed = true;
  }
  host::log("RAM translator: %s (Static engine, interpreter fallback)", enabled ? "on" : "off");
}
RamTranslatorStats ram_translator_stats() { auto result = totals; result.entries = cache.size(); return result; }

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
  if (auto at = cache.find(address); at != cache.end()) {
    code = at->second;
    if (code->stale || (!generations_match(*code) && !same_bytes(*code, ram))) { forget(code); code.reset(); }
    else ++totals.hits;
  }
  if (!code) {
    try {
      code = std::make_shared<Cached>(); code->address = address;
      const size_t bytes = extent(ram, address);
      code->source.assign(ram + (address - RAM_BASE), ram + (address - RAM_BASE) + bytes);
      save_versions(*code);
      std::string error;
      const bool ok = stencil::translate_leaf(code->source.data(), bytes, address,
        stencil::generated::table, code->native, error, nullptr, true);
      if (ok) ++totals.translated; else ++totals.refused;
      if (totals.translated + totals.refused <= 32)
        host::log("RAM translator: %08X %zu bytes %s%s%s", address, bytes, ok ? "native" : "fallback",
                  ok ? "" : ": ", ok ? "" : error.c_str());
      if (cache.size() >= kCacheEntries || totals.native_bytes + code->native.allocation_bytes() > kCacheBytes) {
        // Existing active owners retain their code. Clearing this index only evicts future entries.
        cache.clear(); totals.native_bytes = 0;
      }
      totals.native_bytes += code->native.allocation_bytes();
      cache.emplace(address, code);
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
    const stencil::RuntimeHooks hooks{branch, entry_lr, continuation, &next};
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
  if (code.stale || !same_bytes(code, active->ram)) {
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
  }
  for (Invocation* i = active; i; i = i->parent) if (overlaps(i->code)) i->code.stale = true;
  select_invocation();
}
