// Copy-and-patch translator, also used by the optional Static RAM-code cache.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc.h"
#include "stencil_format.h"
#include "leaf_translation_plan.h"
#include <string>
#include <vector>

namespace ppc::stencil {
struct RuntimeHooks {
  // 0 = invocation returned; otherwise the guest address where execution continues.
  uint32_t (*branch)(Context&, uint8_t*, uint32_t pc, uint32_t word, uint32_t entry_lr);
  uint32_t entry_lr = 0;
  bool continuation = false;
  uint32_t* resume_pc = nullptr;
  // A continuation may begin at any guest instruction the translation has an entry for
  // (CompiledLeaf::enters_at). 0 = the function entry.
  uint32_t start = 0;
  // After the poll of a native back-edge: where a transfer without link to `target` lands under
  // the interpreter's rules (the target itself while it runs from RAM), or 0 when the invocation
  // returned. Null: the target is taken as it is.
  uint32_t (*transfer)(Context&, uint8_t*, uint32_t target, uint32_t entry_lr) = nullptr;
};
// A stencil chain's entry. The result is 0 when the guest function returned.
using Entry = uint32_t (*)(Context&, uint8_t*);
class CompiledLeaf {
public:
  CompiledLeaf() = default;
  ~CompiledLeaf();
  CompiledLeaf(const CompiledLeaf&) = delete;
  CompiledLeaf& operator=(const CompiledLeaf&) = delete;
  CompiledLeaf(CompiledLeaf&& other) noexcept;
  CompiledLeaf& operator=(CompiledLeaf&& other) noexcept;
  // Reject mid-function entries and changed code rather than executing stale bytes.
  // This byte comparison is deliberately only a prototype validation path. Runtime caching and
  // generation invalidation must be measured before this can be installed into game dispatch.
  bool run(Context& context, uint8_t* ram, const RuntimeHooks* runtime = nullptr,
           bool code_verified = false) const;
  bool ready() const { return allocation_ != nullptr; }
  uint32_t address() const { return address_; }
  // The copied code and its data: for tests and crash reports that ask whose address this is.
  bool contains(const void* pointer) const {
    const auto* p = static_cast<const uint8_t*>(pointer);
    const auto* base = static_cast<const uint8_t*>(allocation_);
    return base && p >= base && p < base + size_;
  }
  const void* base() const { return allocation_; }
  size_t allocation_bytes() const { return size_; }
  // Unwind records of the call-capable stencil copies in this translation, all registered.
  size_t unwind_entries() const { return unwind_entries_; }
  // Runtime mode: the driver may continue at this guest address inside the translation.
  bool enters_at(uint32_t address) const {
    if ((address & 3) || address < address_) return false;
    const size_t index = (address - address_) / 4;
    return index < guest_entries_.size() && guest_entries_[index] != nullptr;
  }
  // Word ranges [first, last] of the source no path from the entry reaches (inline data, dead
  // code). No stencil was planned from them and no entry leads into them.
  const std::vector<std::pair<uint32_t, uint32_t>>& unreachable_runs() const { return unreachable_runs_; }
private:
  friend bool translate_leaf(const uint8_t*, size_t, uint32_t, const Table&, CompiledLeaf&, std::string&,
                             const std::vector<Callee>*, bool, const std::vector<uint8_t>*);
  // What the driver does at each Exit stencil, by exit number less one.
  struct ExitAction { ExitKind kind; uint32_t value; Entry resume; };
  void release();
  void* allocation_ = nullptr;
  size_t size_ = 0;
  void* function_table_ = nullptr; // RUNTIME_FUNCTION array inside the allocation, when registered.
  size_t unwind_entries_ = 0;
  Entry entry_ = nullptr;
  uint32_t address_ = 0;
  std::vector<uint8_t> source_;
  std::vector<ExitAction> exits_;
  // Return addresses of local calls and where each resumes; null for inline data.
  std::vector<std::pair<uint32_t, Entry>> local_returns_;
  bool has_local_calls_ = false;
  std::vector<Entry> guest_entries_;
  std::vector<std::pair<uint32_t, uint32_t>> unreachable_runs_;
  void* region_ = nullptr; // The shared code region the allocation was cut from, or null.
};

// On failure result stays empty. Accepted instructions are listed in tools/ppc_stencils/README.md.
// It emits copied native code and literal cells, never an interpreter loop. When the table names
// host symbols the code is placed within REL32 reach of the host image, and the unwind data of
// every call-capable stencil copy is registered with the system until the translation is freed.
// `callees`: what is known about the functions this one calls (their computed returns), or null.
// `direct`: runtime mode only, see plan_attempt. Null keeps every branch in the driver.
bool translate_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                    const Table& table, CompiledLeaf& result, std::string& error,
                    const std::vector<Callee>* callees = nullptr, bool runtime = false,
                    const std::vector<uint8_t>* direct = nullptr);
} // namespace ppc::stencil
