// Renderer texture invalidation must observe guest writes without taxing unrelated RAM stores.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ppc {
std::atomic<uint8_t> g_ram_watched[RAM_WATCH_COUNT]{};
std::atomic<uint32_t> g_ram_versions[RAM_WATCH_COUNT]{};
void mmio_write(Context&, uint32_t, uint32_t, int) { std::abort(); }
void mmio_write64(Context&, uint32_t, uint64_t) { std::abort(); }
uint8_t* locked_cache() { std::abort(); }
}  // namespace ppc

static void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "ram watch test failed: %s\n", message);
    std::exit(1);
  }
}

int main() {
  std::vector<uint8_t> ram(ppc::RAM_SIZE);
  ppc::Context cpu{};

  constexpr uint32_t texture = 0x80120000;
  const uint64_t initial = ppc::watch_ram_range(texture - ppc::RAM_BASE, 0x18000);
  ppc::st32(cpu, ram.data(), texture + 16, 0x12345678);
  const uint64_t changed = ppc::watch_ram_range(texture - ppc::RAM_BASE, 0x18000);
  check(initial != changed, "watched texture write did not change its token");

  ppc::st32(cpu, ram.data(), 0x80010000, 0x87654321);
  check(changed == ppc::watch_ram_range(texture - ppc::RAM_BASE, 0x18000),
        "unrelated RAM write changed the texture token");

  constexpr uint32_t boundary = 0x8012fffc;
  const uint64_t before_boundary = ppc::watch_ram_range(boundary - ppc::RAM_BASE, 8);
  ppc::st64(cpu, ram.data(), boundary, 0x1122334455667788ull);
  check(before_boundary != ppc::watch_ram_range(boundary - ppc::RAM_BASE, 8),
        "cross-block write did not invalidate the watched range");

  std::puts("ram watch tests passed");
  return 0;
}
