// Guest heap failure diagnostics must tolerate corrupted guest metadata without touching it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_heap_trace.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
constexpr uint32_t base = 0x80000000u;
std::array<uint8_t,4096> ram{};
void put(uint32_t offset, uint32_t value) {
  for (uint32_t i=0; i<4; ++i) ram[offset+i] = uint8_t(value >> (24u-i*8u));
}
bool read(uint32_t address, uint32_t& value) {
  if (address < base || address-base > ram.size()-4) return false;
  const auto* bytes = ram.data()+(address-base);
  value = (uint32_t(bytes[0])<<24) | (uint32_t(bytes[1])<<16) | (uint32_t(bytes[2])<<8) | bytes[3];
  return true;
}
void seed() {
  ram.fill(0);
  put(4,0x4000); put(8,0x4100); put(12,base+32);
  put(32,base+44); put(36,0x4020); put(40,0x20);
  put(44,0); put(48,0x4080); put(52,0x10);
}
}
int main() {
  using host::guest_heap_trace::inspect;
  seed(); const auto before=ram;
  auto result=inspect(base,read);
  CHECK(result.complete && result.count==2 && result.free==0xD0 && result.largest_gap==0x70);
  CHECK(ram==before);
  put(12,0); result=inspect(base,read);
  CHECK(result.complete && result.count==0 && result.free==0x100 && result.largest_gap==0x100);
  CHECK(!inspect(0,read).complete);
  CHECK(!inspect(base+1,read).complete);
  CHECK(!inspect(base+4092,read).complete);
  CHECK(!inspect(0xFFFFFFFCu,read).complete);
  seed(); put(8,0x3000); CHECK(!inspect(base,read).complete);
  seed(); put(44,base+32); result=inspect(base,read);
  CHECK(!result.complete && std::strcmp(result.error,"cyclic block list")==0);
  seed(); put(48,0x4030); CHECK(!inspect(base,read).complete);
  seed(); put(52,0x100); CHECK(!inspect(base,read).complete);
  seed(); put(32,base+4095); CHECK(!inspect(base,read).complete);
  seed(); put(32,0xFFFFFFFCu); CHECK(!inspect(base,read).complete);
  seed(); put(36,0x4101); CHECK(!inspect(base,read).complete);
  seed(); put(4,0xFFFF0000u); put(8,0xFFFFFFFFu); put(36,0xFFFFFFF0u); put(40,32);
  CHECK(!inspect(base,read).complete); // size addition would overflow.
  ram.fill(0); put(4,0x4000); put(8,0x4100); put(12,base+32);
  for(uint32_t i=0;i<132;++i){
    const uint32_t offset=32+i*12;
    put(offset,i==131?0:base+offset+12); put(offset+4,0x4000); put(offset+8,0);
  }
  result=inspect(base,read);
  CHECK(!result.complete && result.count==host::guest_heap_trace::kMaxBlocks);
  if(!failures)std::printf("guest heap trace: bounds, gaps, cycles, overflow and read-only checks passed\n");
  return failures?1:0;
}
