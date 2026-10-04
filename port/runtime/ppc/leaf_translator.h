// An isolated translator prototype; no dispatch or game boot path uses it yet.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc.h"
#include "stencil_format.h"
#include "leaf_translation_plan.h"
#include <string>
#include <vector>

namespace ppc::stencil {
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
  bool run(Context& context, uint8_t* ram) const;
  bool ready() const { return allocation_ != nullptr; }
  uint32_t address() const { return address_; }
  // The copied code and its data: for tests and crash reports that ask whose address this is.
  bool contains(const void* pointer) const {
    const auto* p = static_cast<const uint8_t*>(pointer);
    const auto* base = static_cast<const uint8_t*>(allocation_);
    return base && p >= base && p < base + size_;
  }
  const void* base() const { return allocation_; }
  // Unwind records of the call-capable stencil copies in this translation, all registered.
  size_t unwind_entries() const { return unwind_entries_; }
private:
  friend bool translate_leaf(const uint8_t*, size_t, uint32_t, const Table&, CompiledLeaf&, std::string&,
                             const std::vector<Callee>*);
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
};

// On failure result stays empty. Accepted instructions are listed in tools/ppc_stencils/README.md.
// It emits copied native code and literal cells, never an interpreter loop. When the table names
// host symbols the code is placed within REL32 reach of the host image, and the unwind data of
// every call-capable stencil copy is registered with the system until the translation is freed.
// `callees`: what is known about the functions this one calls (their computed returns), or null.
bool translate_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                    const Table& table, CompiledLeaf& result, std::string& error,
                    const std::vector<Callee>* callees = nullptr);
} // namespace ppc::stencil
