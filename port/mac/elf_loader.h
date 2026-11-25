// Headless macOS ELF bridge. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_host.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mac_elf {
// Called by an early constructor, before host allocations. Never overwrites mappings.
bool reserve_window(std::string* error);
// Where the console window sits: 0 under Rosetta, a 4 GB-aligned block on Apple silicon. A console
// address A is at window_base() + A in this process (sourceport/game/include/mu_window.h).
uintptr_t window_base();
bool activate_mem1(std::string* error);
void deactivate_mem1();
class Image {
 public:
  bool load(const std::string& path, std::string* error);
  void unload();
  void* symbol(const char* name) const;
  const std::vector<MuStateRegion>& mappings() const { return mappings_; }
  const std::vector<MuStateRegion>& writable() const { return writable_; }
  uint32_t size() const { return size_; }
 private:
  std::vector<MuStateRegion> mappings_, writable_;
  std::vector<uintptr_t> fini_array_;
  uintptr_t fini_ = 0, symtab_ = 0, strtab_ = 0;
  size_t symbol_count_ = 0, string_bytes_ = 0;
  uint32_t size_ = 0;
  bool initialized_ = false;
};
} // namespace mac_elf
