// Validation of the native game's published state regions.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_state_layout.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using source_port::validate_state_regions;

namespace {
constexpr uintptr_t MEM1 = 0x80000000u;
constexpr uint32_t MEM1_SIZE = 40u << 20;
void* at(uintptr_t a) { return (void*)a; }
}  // namespace

int main() {
  // The shape the game reports today: MEM1, then the image's .data and .bss after it.
  MuStateRegion good[3] = {{at(MEM1), MEM1_SIZE}, {at(0x82B65000u), 353936}, {at(0x82C6F000u), 2413392}};
  std::string error;
  CHECK(validate_state_regions(good, 3, MEM1, MEM1_SIZE, &error));

  CHECK(!validate_state_regions(nullptr, 3, MEM1, MEM1_SIZE, &error));
  CHECK(!validate_state_regions(good, 2, MEM1, MEM1_SIZE, &error));
  CHECK(!validate_state_regions(good, 4, MEM1, MEM1_SIZE, &error));

  MuStateRegion r[3];
  auto reset = [&] { for (int i = 0; i < 3; ++i) r[i] = good[i]; };

  reset(); r[0].size = MEM1_SIZE - 1;
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error) && error == "region 0 is not MEM1");
  reset(); r[0].address = at(MEM1 + 0x1000);
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error));

  reset(); r[1].size = 0;
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error) && error == "an image region is empty");
  reset(); r[2].address = nullptr;
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error));

  // Inside MEM1, or straddling its end.
  reset(); r[1].address = at(MEM1 + 0x100000);
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error) && error == "an image region overlaps MEM1");
  reset(); r[2].address = at(MEM1 + MEM1_SIZE - 16);
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error));

  // .data and .bss must not overlap; touching is fine.
  reset(); r[2].address = at(0x82B65000u + 100);
  CHECK(!validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error) && error == "the image regions overlap");
  reset(); r[2].address = at(0x82B65000u + 353936);
  CHECK(validate_state_regions(r, 3, MEM1, MEM1_SIZE, &error));

  // Exclusions: inside a region, non-empty, disjoint.
  using source_port::validate_state_exclusions;
  MuStateRegion ex[3] = {{at(0x82C6F000u + 64), 16}, {at(0x82B65000u), 8}, {at(MEM1 + 0x1000), 32}};
  CHECK(validate_state_exclusions(good, 3, ex, 3, &error));
  CHECK(validate_state_exclusions(good, 3, nullptr, 0, &error));
  CHECK(!validate_state_exclusions(good, 3, nullptr, 1, &error));
  MuStateRegion bad = {at(0x82C6F000u + 2413392 - 4), 8};   // runs past the end of .bss
  CHECK(!validate_state_exclusions(good, 3, &bad, 1, &error) && error == "an exclusion lies outside the state regions");
  bad = {at(0x90000000u), 4};
  CHECK(!validate_state_exclusions(good, 3, &bad, 1, &error));
  bad = {at(0x82B65000u), 0};
  CHECK(!validate_state_exclusions(good, 3, &bad, 1, &error) && error == "an exclusion is empty");
  MuStateRegion overlap[2] = {{at(0x82C6F000u + 64), 16}, {at(0x82C6F000u + 72), 16}};
  CHECK(!validate_state_exclusions(good, 3, overlap, 2, &error) && error == "two exclusions overlap");
  overlap[1].address = at(0x82C6F000u + 80);   // touching is fine
  CHECK(validate_state_exclusions(good, 3, overlap, 2, &error));
  std::printf("native state layout: ok\n");
  return 0;
}
