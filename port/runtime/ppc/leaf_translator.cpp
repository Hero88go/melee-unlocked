// Copy compiler-produced instruction stencils and patch their COFF relocation holes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaf_translator.h"
#include "leaf_translation_plan.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <limits>
#include <utility>

namespace ppc::stencil {
namespace {
size_t align16(size_t value) { return (value + 15u) & ~size_t(15u); }
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
bool validate_table(const Table& table, ByOperation& by_operation, std::string& error) {
  if (table.version != kFormatVersion || !table.stencils || table.count != by_operation.size()) {
    error = "unsupported stencil table"; return false;
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
      if (hole >= kHoleCount || relocation.bias > 5 || relocation.addend ||
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
} // namespace

void CompiledLeaf::release() {
  if (allocation_) VirtualFree(allocation_, 0, MEM_RELEASE);
  allocation_ = nullptr; entry_ = nullptr; address_ = 0; source_.clear(); resume_.clear();
}
CompiledLeaf::~CompiledLeaf() { release(); }
CompiledLeaf::CompiledLeaf(CompiledLeaf&& other) noexcept { *this = std::move(other); }
CompiledLeaf& CompiledLeaf::operator=(CompiledLeaf&& other) noexcept {
  if (this != &other) {
    release();
    allocation_ = std::exchange(other.allocation_, nullptr);
    entry_ = std::exchange(other.entry_, nullptr);
    address_ = std::exchange(other.address_, 0u);
    source_ = std::move(other.source_);
    resume_ = std::move(other.resume_);
  }
  return *this;
}
bool CompiledLeaf::run(Context& context, uint8_t* ram) const {
  if (!entry_ || !ram || context.entry ||
      std::memcmp(ram + (address_ - RAM_BASE), source_.data(), source_.size())) return false;
  ppc::enter(context, address_);
  // A nonzero result is a back-edge whose counter reached the poll interval: the chain left
  // without a call, the poll runs here in ordinary compiled code, and the loop resumes at the
  // branch target. This is ppc::backedge split at its call.
  uint32_t exit = entry_(context, ram);
  while (exit) {
    if (exit > resume_.size()) return false;
    ppc::loop_poll(context);
    exit = resume_[exit - 1](context, ram);
  }
  return true;
}

bool translate_leaf(const uint8_t* code, size_t bytes, uint32_t address,
                    const Table& table, CompiledLeaf& result, std::string& error) {
  result.release();
  error.clear();
#if !defined(_M_X64)
  error = "leaf stencils require the Windows AMD64 ABI";
  return false;
#else
  std::vector<Instruction> plan;
  if (!plan_leaf(code, bytes, address, plan, error)) return false;
  ByOperation by_operation{};
  if (!validate_table(table, by_operation, error)) return false;
  std::vector<size_t> offsets;
  offsets.reserve(plan.size());
  size_t code_size = 0;
  for (const auto& instruction : plan) {
    if (static_cast<size_t>(instruction.operation) >= by_operation.size()) {
      error = "plan names an unknown stencil"; return false;
    }
    offsets.push_back(code_size);
    code_size = align16(code_size + by_operation[static_cast<size_t>(instruction.operation)]->size);
  }
  constexpr size_t cell_bytes = kCellCount * sizeof(uint32_t);
  const size_t allocation_size = code_size + plan.size() * cell_bytes;
  if (allocation_size > 5u * 1024u * 1024u) { error = "translation exceeds code limit"; return false; }
  CompiledLeaf candidate;
  candidate.source_.assign(code, code + bytes);
  candidate.address_ = address;
  candidate.allocation_ = VirtualAlloc(nullptr, allocation_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!candidate.allocation_) { error = "cannot allocate translation"; return false; }
  auto* base = static_cast<uint8_t*>(candidate.allocation_);
  std::memset(base, 0xCC, code_size);
  for (size_t i = 0; i < plan.size(); ++i) {
    const auto& instruction = plan[i];
    const auto& stencil = *by_operation[static_cast<size_t>(instruction.operation)];
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
      if (relocation.hole == Hole::Next || relocation.hole == Hole::Taken) {
        const uint32_t link = relocation.hole == Hole::Next ? instruction.next : instruction.taken;
        if (link >= plan.size()) { error = "tail jump has no following stencil"; return false; }
        target = base + offsets[link];
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
    if (instruction.operation == Operation::Exit) {
      // Exit numbers are 1-based and dense; the entry to resume at is the back-edge's target.
      if (instruction.immediate != candidate.resume_.size() + 1 || instruction.taken >= plan.size()) {
        error = "invalid exit numbering"; return false;
      }
      candidate.resume_.push_back(reinterpret_cast<Entry>(base + offsets[instruction.taken]));
    }
  }
  DWORD old = 0;
  if (!VirtualProtect(base, allocation_size, PAGE_EXECUTE_READ, &old)) {
    error = "cannot protect translation"; return false;
  }
  if (!FlushInstructionCache(GetCurrentProcess(), base, code_size)) {
    error = "cannot flush translation instruction cache"; return false;
  }
  candidate.entry_ = reinterpret_cast<Entry>(base);
  result = std::move(candidate);
  return true;
#endif
}
} // namespace ppc::stencil
