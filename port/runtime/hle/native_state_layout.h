// Validation of the writable state regions the native game publishes for snapshots.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "../abi/mu_host.h"

namespace source_port {

// Region 0 is MEM1 at its fixed base and size. Regions 1 and 2 are the game image's writable
// sections: non-empty, outside MEM1 and disjoint from each other.
inline bool validate_state_regions(const MuStateRegion* regions, uint32_t count, uintptr_t mem1_base,
                                   uint32_t mem1_size, std::string* error) {
  auto fail = [&](const char* why) {
    if (error) *error = why;
    return false;
  };
  if (!regions || count != 3) return fail("expected three state regions");
  if ((uintptr_t)regions[0].address != mem1_base || regions[0].size != mem1_size)
    return fail("region 0 is not MEM1");
  const uintptr_t mem1_end = mem1_base + mem1_size;
  for (int i = 1; i < 3; ++i) {
    const uintptr_t start = (uintptr_t)regions[i].address;
    if (!start || !regions[i].size) return fail("an image region is empty");
    const uintptr_t end = start + regions[i].size;
    if (end < start) return fail("an image region wraps");
    if (start < mem1_end && end > mem1_base) return fail("an image region overlaps MEM1");
  }
  const uintptr_t a0 = (uintptr_t)regions[1].address, a1 = a0 + regions[1].size;
  const uintptr_t b0 = (uintptr_t)regions[2].address, b1 = b0 + regions[2].size;
  if (a0 < b1 && b0 < a1) return fail("the image regions overlap");
  return true;
}

// Every exclusion is non-empty, lies wholly inside one state region, and no two overlap.
inline bool validate_state_exclusions(const MuStateRegion* regions, uint32_t region_count,
                                      const MuStateRegion* exclusions, uint32_t count,
                                      std::string* error) {
  auto fail = [&](const char* why) {
    if (error) *error = why;
    return false;
  };
  if (count && !exclusions) return fail("missing exclusion list");
  std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
  for (uint32_t i = 0; i < count; ++i) {
    const uintptr_t start = (uintptr_t)exclusions[i].address;
    const uintptr_t end = start + exclusions[i].size;
    if (!start || !exclusions[i].size || end < start) return fail("an exclusion is empty");
    bool inside = false;
    for (uint32_t r = 0; r < region_count && !inside; ++r) {
      const uintptr_t r0 = (uintptr_t)regions[r].address, r1 = r0 + regions[r].size;
      inside = start >= r0 && end <= r1;
    }
    if (!inside) return fail("an exclusion lies outside the state regions");
    ranges.emplace_back(start, end);
  }
  std::sort(ranges.begin(), ranges.end());
  for (size_t i = 1; i < ranges.size(); ++i)
    if (ranges[i].first < ranges[i - 1].second) return fail("two exclusions overlap");
  return true;
}

}  // namespace source_port
