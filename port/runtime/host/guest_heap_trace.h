// Read-only inspection of retail Handle and HSD_AllocEntry layouts in guest memory.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace host::guest_heap_trace {
constexpr size_t kMaxBlocks = 0x83; // lbmemory.c Allocator.mem, retail descriptor pool.
struct Block { uint32_t descriptor = 0, next = 0, address = 0, size = 0; };
struct Snapshot {
  uint32_t handle = 0, lo = 0, hi = 0, first = 0;
  uint32_t free = 0, largest_gap = 0;
  size_t count = 0;
  bool complete = false;
  const char* error = nullptr;
  std::array<Block,kMaxBlocks> blocks{};
};
// read32(address, value) must validate the complete four-byte span and read big-endian data.
// Errors return partial metadata with complete=false; no guest pointer is directly dereferenced.
template<class Read32> Snapshot inspect(uint32_t handle, Read32 read32) {
  Snapshot result;
  result.handle = handle;
  if (!handle || (handle & 3u) || handle > 0xFFFFFFF0u ||
      !read32(handle+4, result.lo) || !read32(handle+8, result.hi) ||
      !read32(handle+12, result.first)) {
    result.error = "unreadable heap handle"; return result;
  }
  if (result.hi < result.lo) { result.error = "inverted heap bounds"; return result; }
  uint32_t cursor = result.lo, node = result.first;
  while (node) {
    if (result.count == kMaxBlocks) { result.error = "descriptor limit reached"; return result; }
    for (size_t i=0; i<result.count; ++i) if (result.blocks[i].descriptor == node) {
      result.error = "cyclic block list"; return result;
    }
    Block block;
    block.descriptor = node;
    if ((node & 3u) || node > 0xFFFFFFF4u || !read32(node, block.next) ||
        !read32(node+4, block.address) || !read32(node+8, block.size)) {
      result.error = "unreadable block descriptor"; return result;
    }
    if (block.address < cursor || block.address > result.hi || block.size > result.hi - block.address) {
      result.error = "unsorted, overlapping or out-of-bounds block"; return result;
    }
    const uint32_t gap = block.address - cursor;
    result.free += gap;
    if (gap > result.largest_gap) result.largest_gap = gap;
    result.blocks[result.count++] = block;
    cursor = block.address + block.size;
    node = block.next;
  }
  const uint32_t tail = result.hi - cursor;
  result.free += tail;
  if (tail > result.largest_gap) result.largest_gap = tail;
  result.complete = true;
  return result;
}
} // namespace host::guest_heap_trace
