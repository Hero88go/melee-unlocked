// An isolated translator prototype; no dispatch or game boot path uses it yet.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc.h"
#include "stencil_format.h"
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
private:
  friend bool translate_leaf(const uint8_t*, size_t, uint32_t, const Table&, CompiledLeaf&, std::string&);
  void release();
  void* allocation_ = nullptr;
  Entry entry_ = nullptr;
  uint32_t address_ = 0;
  std::vector<uint8_t> source_;
  std::vector<Entry> resume_;
};

// On failure result stays empty. Accepted instructions are listed in tools/ppc_stencils/README.md.
// It emits copied native code and literal cells, never an interpreter loop.
bool translate_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                    const Table& table, CompiledLeaf& result, std::string& error);
} // namespace ppc::stencil
