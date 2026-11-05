// Lookup tables that are emptied every simulation frame and refilled with about as many keys. Where
// the standard library has polymorphic memory resources, their nodes come from a pool that keeps the
// freed nodes for the next frame instead of one heap allocation per key; elsewhere they are plain
// standard containers. Simulation thread only: the pool is not thread-safe.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <unordered_map>
#include <vector>
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif

namespace gx {
#if defined(__cpp_lib_memory_resource)
using FramePool = std::pmr::unsynchronized_pool_resource;
template <class K, class V> using FrameTable = std::pmr::unordered_map<K, V>;
template <class T> using FrameVector = std::pmr::vector<T>;
// A table drawing its nodes from `pool` (which must outlive it: declare the pool first).
template <class Table> Table frame_table(FramePool& pool) { return Table(&pool); }
#else
struct FramePool {};
template <class K, class V> using FrameTable = std::unordered_map<K, V>;
template <class T> using FrameVector = std::vector<T>;
template <class Table> Table frame_table(FramePool&) { return Table(); }
#endif
}  // namespace gx
