// Copy compiler-produced instruction stencils and patch their COFF relocation holes.
// A call-capable stencil keeps its stack frame: every copy gets a RUNTIME_FUNCTION that points at
// the stencil's UNWIND_INFO, registered with RtlAddFunctionTable, so the system can unwind through
// translated code (crash reports, C++ exceptions thrown by a host function it called).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaf_translator.h"
#include "leaf_translation_plan.h"
#include "ram_translator.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <limits>
#include <utility>

namespace ppc::stencil {
namespace {
size_t align_up(size_t value, size_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }
size_t align16(size_t value) { return align_up(value, 16); }
using ByOperation = std::array<const Stencil*, kOperationCount>;
bool ends_with_return(const uint8_t* code, size_t size, size_t prefix) {
  const bool plain_return = size == prefix + 1 && code[prefix] == 0xC3;
  const bool zero_pop_return = size == prefix + 3 && code[prefix] == 0xC2 &&
                              code[prefix + 1] == 0 && code[prefix + 2] == 0;
  return plain_return || zero_pop_return;
}
// E9 rel32, or 0F 8x rel32 where a branch stencil may jump conditionally.
bool is_jump(const Stencil& stencil, uint32_t offset, bool conditional_allowed, bool& direct) {
  direct = offset >= 1 && stencil.code[offset - 1] == 0xE9;
  if (direct) return true;
  return conditional_allowed && offset >= 2 && stencil.code[offset - 2] == 0x0F &&
         stencil.code[offset - 1] >= 0x80 && stencil.code[offset - 1] <= 0x8F;
}
constexpr size_t kChainBytes = sizeof(RUNTIME_FUNCTION); // It ends a chained UNWIND_INFO.
// UNWIND_INFO as the extractor admits it: version 1, no handler, no frame register, and only the
// unwind codes that describe pushes, the stack allocation and saved registers. A chained record
// has UNW_FLAG_CHAININFO and nothing else. Anything else is refused rather than registered.
bool valid_unwind(const uint8_t* unwind, size_t size, size_t range_size, bool chained) {
  if (!unwind || size < 4 || (size & 3)) return false;
  if ((unwind[0] & 7) != 1 || (unwind[0] >> 3) != (chained ? 4 : 0)) return false;
  const size_t prolog = unwind[1], count = unwind[2];
  if (prolog > range_size || unwind[3] != 0) return false;
  if (size != 4 + 2 * ((count + 1) & ~size_t(1)) + (chained ? kChainBytes : 0)) return false;
  size_t i = 0;
  while (i < count) {
    const uint8_t offset = unwind[4 + 2 * i], code = unwind[5 + 2 * i] & 15, info = unwind[5 + 2 * i] >> 4;
    size_t slots = 0;
    switch (code) {
      case 0: case 2: slots = 1; break;                           // UWOP_PUSH_NONVOL, UWOP_ALLOC_SMALL
      case 1: slots = info == 0 ? 2 : info == 1 ? 3 : 0; break;   // UWOP_ALLOC_LARGE
      case 4: case 8: slots = 2; break;                           // UWOP_SAVE_NONVOL, UWOP_SAVE_XMM128
      case 5: case 9: slots = 3; break;                           // the FAR forms
      default: slots = 0; break;
    }
    if (!slots || offset > prolog) return false;
    i += slots;
  }
  return i == count;
}
bool validate_table(const Table& table, ByOperation& by_operation, std::string& error) {
  if (table.version != kFormatVersion || !table.stencils || table.count != by_operation.size()) {
    error = "unsupported stencil table"; return false;
  }
  if (table.external_count > 1024 || table.constant_count > 1024 ||
      (table.external_count && !table.externals) || (table.constant_count && !table.constants)) {
    error = "invalid stencil table externals"; return false;
  }
  for (size_t i = 0; i < table.external_count; ++i) {
    const External& external = table.externals[i];
    const bool known = external.kind == ExternalKind::Direct || external.kind == ExternalKind::Slot ||
                       external.kind == ExternalKind::ImageBase;
    if (!external.name || !known || (external.kind != ExternalKind::ImageBase && !external.address)) {
      error = "invalid stencil table externals"; return false;
    }
  }
  for (size_t i = 0; i < table.constant_count; ++i) {
    const Constant& constant = table.constants[i];
    if (!constant.bytes || !constant.size || constant.size > 64 || !constant.alignment ||
        constant.alignment > 64 || (constant.alignment & (constant.alignment - 1))) {
      error = "invalid stencil table constants"; return false;
    }
  }
  for (size_t i = 0; i < table.count; ++i) {
    const Stencil& stencil = table.stencils[i];
    const size_t operation = static_cast<size_t>(stencil.operation);
    if (operation >= by_operation.size() || by_operation[operation] || !stencil.code ||
        !stencil.size || stencil.size > 4096 || stencil.relocation_count > 64 ||
        (stencil.relocation_count && !stencil.relocations)) {
      error = "invalid or duplicate stencil"; return false;
    }
    by_operation[operation] = &stencil;
    const bool may_call = operation_may_call(stencil.operation);
    if (stencil.unwind || stencil.unwind_count) {
      if (!may_call) { error = "leaf stencil has unwind data"; return false; }
      if (!stencil.unwind || !stencil.unwind_count || stencil.unwind_count > 8) {
        error = "unsupported stencil unwind data"; return false;
      }
      // The records cover the stencil in order without a gap; a chained one names an earlier one.
      uint32_t covered = 0;
      for (size_t j = 0; j < stencil.unwind_count; ++j) {
        const UnwindRecord& record = stencil.unwind[j];
        const bool chained = record.parent >= 0;
        if (record.begin != covered || record.end <= record.begin || record.end > stencil.size ||
            record.parent < -1 || (chained && size_t(record.parent) >= j) ||
            !valid_unwind(record.info, record.info_size, record.end - record.begin, chained)) {
          error = "unsupported stencil unwind data"; return false;
        }
        covered = record.end;
      }
      if (covered != stencil.size) { error = "unsupported stencil unwind data"; return false; }
    }
    if (stencil.operation == Operation::Return) {
      // xor eax,eax then a ret with no stack pop: the chain's result 0.
      if (stencil.size < 2 || stencil.code[0] != 0x33 || stencil.code[1] != 0xC0 ||
          !ends_with_return(stencil.code, stencil.size, 2) || stencil.relocation_count) {
        error = "invalid return stencil"; return false;
      }
      continue;
    }
    const uint32_t expected = operation_holes(stencil.operation);
    const bool branch = (expected & holes::T) != 0;
    uint32_t present = 0, jump_count = 0;
    bool final_jump = false;
    for (size_t j = 0; j < stencil.relocation_count; ++j) {
      const Relocation& relocation = stencil.relocations[j];
      const uint32_t hole = static_cast<uint32_t>(relocation.hole);
      if (hole >= kHoleKindCount || relocation.bias > 5 ||
          relocation.offset > stencil.size || stencil.size - relocation.offset < 4) {
        error = "invalid stencil relocation"; return false;
      }
      for (size_t previous = 0; previous < j; ++previous) {
        const uint32_t other = stencil.relocations[previous].offset;
        if (relocation.offset < other + 4u && other < relocation.offset + 4u) {
          error = "overlapping stencil relocations"; return false;
        }
      }
      int32_t original = 0;
      std::memcpy(&original, stencil.code + relocation.offset, sizeof original);
      if (original) { error = "stencil hole has an unexpected addend"; return false; }
      if (hole >= kHoleCount) {
        // A reference to the host or to a constant: only in a call-capable stencil.
        if (!may_call) { error = "leaf stencil references the host"; return false; }
        if (relocation.hole == Hole::Constant) {
          if (relocation.index >= table.constant_count || relocation.addend < 0 ||
              size_t(relocation.addend) >= table.constants[relocation.index].size) {
            error = "invalid stencil constant reference"; return false;
          }
        } else {
          if (relocation.index >= table.external_count) { error = "invalid stencil host reference"; return false; }
          const ExternalKind kind = table.externals[relocation.index].kind;
          const bool rva = relocation.hole == Hole::ExternalRva;
          if ((rva && (kind != ExternalKind::Direct || relocation.bias)) || (!rva && relocation.addend)) {
            error = "invalid stencil host reference"; return false;
          }
        }
        continue;
      }
      if (relocation.addend) { error = "invalid stencil relocation"; return false; }
      present |= 1u << hole;
      if (relocation.hole == Hole::Next || relocation.hole == Hole::Taken) {
        bool direct = false;
        ++jump_count;
        if (relocation.bias || !is_jump(stencil, relocation.offset, branch, direct)) {
          error = "Next is not a final tail jump"; return false;
        }
        final_jump = final_jump || (relocation.offset + 4u == stencil.size && direct);
      }
    }
    if (present != expected) { error = "incomplete stencil operands"; return false; }
    if (stencil.operation == Operation::Exit) {
      // mov eax,[rip+Immediate] then a ret with no stack pop: a nonzero exit number.
      if (stencil.size < 6 || stencil.code[0] != 0x8B || stencil.code[1] != 0x05 ||
          !ends_with_return(stencil.code, stencil.size, 6) || stencil.relocation_count != 1 ||
          stencil.relocations[0].offset != 2 || stencil.relocations[0].bias) {
        error = "invalid exit stencil"; return false;
      }
      continue;
    }
    // A duplicated tail is several direct jumps; the last instruction is always one of them.
    if (!final_jump || !jump_count) {
      error = "Next is not a final tail jump"; return false;
    }
  }
  return true;
}
// The host image that holds every Direct external, so `__ImageBase` and ADDR32NB references
// resolve against the module that really defines them.
bool find_image(const Table& table, uintptr_t& base, uintptr_t& end, std::string& error) {
  base = end = 0;
  for (size_t i = 0; i < table.external_count; ++i) {
    const External& external = table.externals[i];
    if (external.kind != ExternalKind::Direct) continue;
    const auto address = reinterpret_cast<uintptr_t>(external.address);
    if (!base) {
      void* image = nullptr;
      if (!RtlPcToFileHeader(const_cast<void*>(external.address), &image) || !image) {
        error = "host symbol is not inside a loaded image"; return false;
      }
      const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(image);
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(static_cast<const uint8_t*>(image) + dos->e_lfanew);
      base = reinterpret_cast<uintptr_t>(image);
      end = base + nt->OptionalHeader.SizeOfImage;
    }
    if (address < base || address >= end) { error = "host symbols are in more than one image"; return false; }
  }
  return true;
}
// Committed read-write pages every byte of which is within REL32 reach of [low, high).
void* allocate_near(uintptr_t low, uintptr_t high, size_t size) {
  constexpr uintptr_t reach = 0x7FF00000u; // 2 GB less a margin for the bias and the patch itself.
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  const uintptr_t granularity = info.dwAllocationGranularity;
  const uintptr_t floor = high > reach ? high - reach : granularity;
  const uintptr_t ceiling = low + reach;
  if (size >= reach) return nullptr;
  // Above the image first, then below it.
  for (uintptr_t at = align_up(high, granularity); at + size <= ceiling;) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof region)) break;
    const uintptr_t region_end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (region.State == MEM_FREE && at + size <= region_end)
      if (void* got = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return got;
    at = align_up(region_end, granularity);
  }
  for (uintptr_t at = low & ~(granularity - 1); at > floor + size;) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<void*>(at - 1), &region, sizeof region)) break;
    const uintptr_t region_base = reinterpret_cast<uintptr_t>(region.BaseAddress);
    if (region.State == MEM_FREE && at - region_base >= size) {
      const uintptr_t candidate = (at - size) & ~(granularity - 1);
      if (candidate >= region_base && candidate >= floor)
        if (void* got = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return got;
    }
    if (region_base >= at) break;
    at = region_base & ~(granularity - 1);
    if (!at) break;
  }
  return nullptr;
}
// Translations are cut, page by page, from shared regions. One system allocation each put every
// translation on its own 64 KB boundary: the same offsets of all of them then compete for the
// same cache sets, each one costs TLB entries of its own, and finding a free range near the image
// walked past every earlier one. A page range is written, then made executable; it is never
// handed out again. Its pages are given back when the translation dies, the region when its last
// translation has (the region still being filled is kept). Used by one thread, as the cache is.
constexpr size_t kPage = 4096, kRegionBytes = size_t(1) << 20;
struct Region {
  uint8_t* base;
  size_t used, live;
  uintptr_t image;
};
Region* g_region = nullptr; // The region being filled.
void* region_allocate(uintptr_t image, uintptr_t image_end, size_t size, void*& owner) {
  owner = nullptr;
  const size_t pages = align_up(size, kPage);
  if (pages > kRegionBytes / 4) // A large one keeps an allocation of its own.
    return image ? allocate_near(image, image_end, size)
                 : VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!g_region || g_region->image != image || g_region->used + pages > kRegionBytes) {
    if (g_region && !g_region->live) {
      VirtualFree(g_region->base, 0, MEM_RELEASE);
      delete g_region;
    }
    g_region = nullptr;
    void* base = image ? allocate_near(image, image_end, kRegionBytes)
                       : VirtualAlloc(nullptr, kRegionBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return nullptr;
    g_region = new Region{static_cast<uint8_t*>(base), 0, 0, image};
  }
  void* result = g_region->base + g_region->used;
  g_region->used += pages;
  ++g_region->live;
  owner = g_region;
  return result;
}
void region_release(void* owner, void* allocation, size_t size) {
  auto* region = static_cast<Region*>(owner);
  VirtualFree(allocation, align_up(size, kPage), MEM_DECOMMIT);
  if (--region->live == 0 && region != g_region) {
    VirtualFree(region->base, 0, MEM_RELEASE);
    delete region;
  }
}
} // namespace

void CompiledLeaf::release() {
  if (function_table_) RtlDeleteFunctionTable(static_cast<PRUNTIME_FUNCTION>(function_table_));
  if (allocation_) {
    if (region_) region_release(region_, allocation_, size_);
    else VirtualFree(allocation_, 0, MEM_RELEASE);
  }
  allocation_ = nullptr; size_ = 0; function_table_ = nullptr; unwind_entries_ = 0;
  region_ = nullptr;
  entry_ = nullptr; address_ = 0; source_.clear(); exits_.clear(); local_returns_.clear();
  has_local_calls_ = false;
  guest_entries_.clear();
  unreachable_runs_.clear();
}
CompiledLeaf::~CompiledLeaf() { release(); }
CompiledLeaf::CompiledLeaf(CompiledLeaf&& other) noexcept { *this = std::move(other); }
CompiledLeaf& CompiledLeaf::operator=(CompiledLeaf&& other) noexcept {
  if (this != &other) {
    release();
    allocation_ = std::exchange(other.allocation_, nullptr);
    size_ = std::exchange(other.size_, size_t(0));
    function_table_ = std::exchange(other.function_table_, nullptr);
    unwind_entries_ = std::exchange(other.unwind_entries_, size_t(0));
    entry_ = std::exchange(other.entry_, nullptr);
    address_ = std::exchange(other.address_, 0u);
    source_ = std::move(other.source_);
    exits_ = std::move(other.exits_);
    local_returns_ = std::move(other.local_returns_);
    has_local_calls_ = std::exchange(other.has_local_calls_, false);
    guest_entries_ = std::move(other.guest_entries_);
    unreachable_runs_ = std::move(other.unreachable_runs_);
    region_ = std::exchange(other.region_, nullptr);
  }
  return *this;
}
bool CompiledLeaf::run(Context& context, uint8_t* ram, const RuntimeHooks* runtime, bool code_verified) const {
  if (!entry_ || !ram || context.entry ||
      (!code_verified && std::memcmp(ram + (address_ - RAM_BASE), source_.data(), source_.size()))) return false;
  // This frame is the invocation: the `uint32_t lrs[32]; uint32_t lrn = 0;` and the
  // `const uint32_t entry_lr = c.lr;` that emit.py writes at the top of a function live here, and
  // every Exit of the chain is one of the statements that use them (or the back-edge poll).
  uint32_t lrs[32];
  uint32_t lrn = 0;
  const uint32_t entry_lr = runtime ? runtime->entry_lr : context.lr;
  if (!runtime || !runtime->continuation) ppc::enter(context, address_);
  // `if (ppc::local_return(lrs, lrn, t)) { if (t == r) goto L_r; ... }`: where to resume, or null
  // when t is not a local return of this invocation. `data` is set when it is one whose words are
  // inline data: nothing may return there.
  const auto local_return = [&](uint32_t t, bool& data) -> Entry {
    data = false;
    if (!has_local_calls_ || !ppc::local_return(lrs, lrn, t)) return nullptr;
    for (const auto& item : local_returns_) {
      if (item.first != t) continue;
      data = item.second == nullptr;
      return item.second;
    }
    return nullptr;
  };
  // Runtime mode: the entry of the guest instruction at `next`, or null when the driver has to
  // find other code for it.
  const auto guest_entry = [&](uint32_t next) -> Entry {
    if ((next & 3) || next < address_) return nullptr;
    const size_t index = (next - address_) / 4;
    return index < guest_entries_.size() ? guest_entries_[index] : nullptr;
  };
  Entry first = entry_;
  if (runtime && runtime->start && runtime->start != address_) {
    first = guest_entry(runtime->start);
    if (!first) return false;
  }
  uint32_t exit = first(context, ram);
  while (exit) {
    if (exit > exits_.size()) return false;
    const ExitAction& action = exits_[exit - 1];
    Entry resume = action.resume;
    bool data = false;
    switch (action.kind) {
      case ExitKind::RuntimeBranch: {
        if (!runtime || !runtime->branch) {
          ppc::fatal(context, "runtime translation has no branch driver", action.value);
          return false;
        }
        const size_t off = action.value - address_;
        const uint8_t* p = source_.data() + off;
        const uint32_t word = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                              (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        const uint32_t next = runtime->branch(context, ram, action.value, word, entry_lr);
        if (!next) return true;
        resume = guest_entry(next);
        if (!resume) {
          if (!runtime->resume_pc) throw ppc::RamTranslationResume{next};
          *runtime->resume_pc = next;
          return true;
        }
        break;
      }
      case ExitKind::Poll:
        // A back-edge whose counter reached the poll interval: ppc::backedge split at its call.
        ppc::loop_poll(context);
        if (runtime && runtime->transfer && action.value) {
          // A native runtime back-edge (action.value is its target). The interpreter polls and
          // then transfers, so what the poll ran decides where the branch lands.
          const uint32_t next = runtime->transfer(context, ram, action.value, entry_lr);
          if (!next) return true;
          if (next != action.value) {
            resume = guest_entry(next);
            if (!resume) {
              if (!runtime->resume_pc) throw ppc::RamTranslationResume{next};
              *runtime->resume_pc = next;
              return true;
            }
          }
        }
        break;
      case ExitKind::LocalCall:
        lrs[lrn++ & 31u] = action.value;
        break;
      case ExitKind::LinkReturn: {
        const uint32_t t = context.lr;
        context.lr = action.value;
        if (Entry local = local_return(t, data)) { resume = local; break; }
        if (!data) {
          if (t == entry_lr) return true;
          ppc::call(context, ram, t);
        }
        break;
      }
      case ExitKind::TailCall: case ExitKind::TailCallCtr: case ExitKind::LocalReturn: {
        const uint32_t t = context.lr; // Read before a tail call, as emit.py does.
        if (action.kind == ExitKind::TailCall) ppc::call(context, ram, action.value);
        else if (action.kind == ExitKind::TailCallCtr) ppc::call(context, ram, context.ctr);
        resume = local_return(t, data);
        if (!resume && !data) return true;
        break;
      }
      default: return false;
    }
    if (data || !resume) {
      // The recompiled code would go on to execute data words as instructions. A translation has
      // none for them, so it stops here and says so.
      ppc::fatal(context, "translated code returned into inline data", context.lr);
      return false;
    }
    exit = resume(context, ram);
  }
  return true;
}

bool translate_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                    const Table& table, CompiledLeaf& result, std::string& error,
                    const std::vector<Callee>* callees, bool runtime,
                    const std::vector<uint8_t>* direct) {
  result.release();
  error.clear();
#if !defined(_M_X64)
  error = "leaf stencils require the Windows AMD64 ABI";
  return false;
#else
  Plan whole;
  if (!plan_function(code, bytes, address, whole, error, nullptr, callees, runtime,
                     runtime ? direct : nullptr)) return false;
  const std::vector<Instruction>& plan = whole.stencils;
  ByOperation by_operation{};
  if (!validate_table(table, by_operation, error)) return false;
  uintptr_t image = 0, image_end = 0;
  if (!find_image(table, image, image_end, error)) return false;
  // Layout: code, then the literal cells, the pointer slots, the constants, the unwind data of
  // every call-capable copy, and the function table.
  std::vector<size_t> offsets;
  offsets.reserve(plan.size());
  size_t code_size = 0, unwind_count = 0, unwind_bytes = 0;
  for (const auto& instruction : plan) {
    if (static_cast<size_t>(instruction.operation) >= by_operation.size()) {
      error = "plan names an unknown stencil"; return false;
    }
    const Stencil& stencil = *by_operation[static_cast<size_t>(instruction.operation)];
    offsets.push_back(code_size);
    code_size = align16(code_size + stencil.size);
    unwind_count += stencil.unwind_count;
    for (size_t j = 0; j < stencil.unwind_count; ++j) unwind_bytes += align_up(stencil.unwind[j].info_size, 4);
  }
  constexpr size_t cell_bytes = kCellCount * sizeof(uint32_t);
  size_t total = code_size + plan.size() * cell_bytes;
  std::vector<size_t> slot_at(table.external_count, 0), constant_at(table.constant_count, 0);
  total = align_up(total, 8);
  for (size_t i = 0; i < table.external_count; ++i)
    if (table.externals[i].kind == ExternalKind::Slot) { slot_at[i] = total; total += 8; }
  for (size_t i = 0; i < table.constant_count; ++i) {
    total = align_up(total, table.constants[i].alignment);
    constant_at[i] = total;
    total += table.constants[i].size;
  }
  total = align_up(total, 4);
  size_t unwind_at = total;
  total += unwind_bytes;
  const size_t functions_at = total;
  total += unwind_count * sizeof(RUNTIME_FUNCTION);
  const size_t allocation_size = total;
  if (allocation_size > 16u * 1024u * 1024u) { error = "translation exceeds code limit"; return false; }
  CompiledLeaf candidate;
  candidate.source_.assign(code, code + bytes);
  candidate.address_ = address;
  candidate.allocation_ = region_allocate(image, image_end, allocation_size, candidate.region_);
  if (!candidate.allocation_) {
    error = image ? "no free address range within reach of the host image" : "cannot allocate translation";
    return false;
  }
  candidate.size_ = allocation_size;
  auto* base = static_cast<uint8_t*>(candidate.allocation_);
  std::memset(base, 0xCC, code_size);
  for (size_t i = 0; i < table.external_count; ++i)
    if (table.externals[i].kind == ExternalKind::Slot)
      std::memcpy(base + slot_at[i], &table.externals[i].address, 8);
  for (size_t i = 0; i < table.constant_count; ++i)
    std::memcpy(base + constant_at[i], table.constants[i].bytes, table.constants[i].size);
  auto* functions = reinterpret_cast<RUNTIME_FUNCTION*>(base + functions_at);
  size_t function_index = 0;
  for (size_t i = 0; i < plan.size(); ++i) {
    const auto& instruction = plan[i];
    const size_t operation = static_cast<size_t>(instruction.operation);
    const auto& stencil = *by_operation[operation];
    uint8_t* start = base + offsets[i];
    std::memcpy(start, stencil.code, stencil.size);
    uint8_t* literals = base + code_size + i * cell_bytes;
    uint32_t operands[kCellCount] = {};
    operands[hole_cell(Hole::Destination)] = instruction.destination;
    operands[hole_cell(Hole::Source)] = instruction.source;
    operands[hole_cell(Hole::Immediate)] = instruction.immediate;
    operands[hole_cell(Hole::Source2)] = instruction.source2;
    operands[hole_cell(Hole::Immediate2)] = instruction.immediate2;
    std::memcpy(literals, operands, sizeof operands);
    for (size_t j = 0; j < stencil.relocation_count; ++j) {
      const auto& relocation = stencil.relocations[j];
      uint8_t* patch = start + relocation.offset;
      const uint8_t* target = nullptr;
      if (relocation.hole == Hole::ExternalRva) {
        // The stencil adds this to the image base it took from `__ImageBase`.
        const int64_t rva = int64_t(reinterpret_cast<uintptr_t>(table.externals[relocation.index].address) - image) + relocation.addend;
        if (rva < 0 || rva > std::numeric_limits<int32_t>::max()) {
          error = "host symbol is outside the image-relative range"; return false;
        }
        const uint32_t value = uint32_t(rva);
        std::memcpy(patch, &value, sizeof value);
        continue;
      }
      if (relocation.hole == Hole::Next || relocation.hole == Hole::Taken) {
        const uint32_t link = relocation.hole == Hole::Next ? instruction.next : instruction.taken;
        if (link >= plan.size()) { error = "tail jump has no following stencil"; return false; }
        target = base + offsets[link];
      } else if (relocation.hole == Hole::External) {
        const External& external = table.externals[relocation.index];
        target = external.kind == ExternalKind::Slot ? base + slot_at[relocation.index]
               : external.kind == ExternalKind::ImageBase ? reinterpret_cast<const uint8_t*>(image)
               : static_cast<const uint8_t*>(external.address);
      } else if (relocation.hole == Hole::Constant) {
        target = base + constant_at[relocation.index] + relocation.addend;
      } else {
        target = literals + hole_cell(relocation.hole) * sizeof(uint32_t);
      }
      const int64_t delta = target - (patch + 4 + relocation.bias);
      if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) {
        error = "stencil relocation is outside rel32 range"; return false;
      }
      const int32_t relative = static_cast<int32_t>(delta);
      std::memcpy(patch, &relative, sizeof relative);
    }
    // One RUNTIME_FUNCTION and one UNWIND_INFO per record of this copy. A chained UNWIND_INFO
    // ends in the RUNTIME_FUNCTION of the record it continues, which is this copy's own.
    const size_t first_function = function_index;
    for (size_t j = 0; j < stencil.unwind_count; ++j) {
      const UnwindRecord& record = stencil.unwind[j];
      std::memcpy(base + unwind_at, record.info, record.info_size);
      RUNTIME_FUNCTION& function = functions[function_index++];
      function.BeginAddress = DWORD(offsets[i] + record.begin);
      function.EndAddress = DWORD(offsets[i] + record.end);
      function.UnwindData = DWORD(unwind_at);
      if (record.parent >= 0)
        std::memcpy(base + unwind_at + record.info_size - kChainBytes, &functions[first_function + record.parent], kChainBytes);
      unwind_at += align_up(record.info_size, 4);
    }
    if (instruction.operation == Operation::Exit) {
      // Exit numbers are 1-based and dense. A return or a tail call has nowhere to resume.
      const bool resumes = instruction.exit == ExitKind::Poll || instruction.exit == ExitKind::LocalCall ||
                           instruction.exit == ExitKind::LinkReturn;
      if (instruction.immediate != candidate.exits_.size() + 1 ||
          (resumes ? instruction.taken >= plan.size() : instruction.taken != kNoStencil)) {
        error = "invalid exit numbering"; return false;
      }
      candidate.exits_.push_back({instruction.exit, instruction.exit_value,
                                  resumes ? reinterpret_cast<Entry>(base + offsets[instruction.taken]) : nullptr});
    }
  }
  DWORD old = 0;
  if (!VirtualProtect(base, allocation_size, PAGE_EXECUTE_READ, &old)) {
    error = "cannot protect translation"; return false;
  }
  if (!FlushInstructionCache(GetCurrentProcess(), base, code_size)) {
    error = "cannot flush translation instruction cache"; return false;
  }
  if (unwind_count) {
    // Entries are in address order because the plan is laid out in order.
    if (!RtlAddFunctionTable(functions, DWORD(unwind_count), reinterpret_cast<DWORD64>(base))) {
      error = "cannot register translation unwind data"; return false;
    }
    candidate.function_table_ = functions;
    candidate.unwind_entries_ = unwind_count;
  }
  candidate.has_local_calls_ = whole.has_local_calls;
  for (const LocalReturn& item : whole.local_returns) {
    if (item.stencil != kNoStencil && item.stencil >= plan.size()) { error = "invalid local return"; return false; }
    candidate.local_returns_.push_back({item.address, item.stencil == kNoStencil ? nullptr
                                        : reinterpret_cast<Entry>(base + offsets[item.stencil])});
  }
  for (uint32_t index : whole.guest_entries)
    candidate.guest_entries_.push_back(index == kNoStencil ? nullptr : reinterpret_cast<Entry>(base + offsets[index]));
  candidate.unreachable_runs_ = whole.unreachable_runs;
  candidate.entry_ = reinterpret_cast<Entry>(base);
  result = std::move(candidate);
  return true;
#endif
}
} // namespace ppc::stencil
