// Loads elf_abi_probe.so with the Mac ELF loader and checks it against Darwin's own printf: the
// console window placement, relocations, and (on Apple silicon) the printf-family bridge between
// the Linux and Apple AArch64 calling conventions.
//   elf_loader_test path/to/elf_abi_probe.so
// SPDX-License-Identifier: GPL-2.0-or-later
#include "elf_loader.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: %s elf_abi_probe.so\n", argv[0]); return 2; }
  std::string error;
  if (!mac_elf::activate_mem1(&error)) { std::fprintf(stderr, "FAIL window: %s\n", error.c_str()); return 1; }
  mac_elf::Image image;
  if (!image.load(argv[1], &error)) { std::fprintf(stderr, "FAIL load: %s\n", error.c_str()); return 1; }
  int failures = 0;
  const uintptr_t base = mac_elf::window_base();
  auto data = reinterpret_cast<const void* (*)()>(image.symbol("mu_probe_data"));
  auto probe = reinterpret_cast<int (*)(char*, int)>(image.symbol("mu_probe"));
  if (!data || !probe) { std::fprintf(stderr, "FAIL: probe symbols missing\n"); return 1; }
  const uintptr_t at = reinterpret_cast<uintptr_t>(data());
  std::printf("window base 0x%llx, probe data at 0x%llx\n", (unsigned long long)base, (unsigned long long)at);
  if ((base & 0xFFFFFFFFu) || at < base + 0x82800000u || at >= base + 0x84000000u) {
    std::fprintf(stderr, "FAIL: image data outside the console window\n"); ++failures;
  }
  char got[512] = {}, want[512] = {};
  const int k = probe(got, sizeof got);
  int w = std::snprintf(want, sizeof want, "%d %s %.3f %ld %c %x %g %d %d %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f|",
                        1, "two", 3.25, 4L, 'f', 0xab, 1e10, 7, 8, 9, 10, 11,
                        1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5);
  w += std::snprintf(want + w, sizeof want - w, "%s=%d,%f,%*d,%lld,%.2e|", "one", 41, 0.5, 5, 9, -3LL, 12345.678);
  std::printf("game printed: %s\n", got);
  if (k != w || std::strcmp(got, want)) { std::fprintf(stderr, "FAIL: expected %s (%d), got %d\n", want, w, k); ++failures; }
  image.unload();
  std::puts(failures ? "elf_loader_test: FAILED" : "elf_loader_test: ok");
  return failures ? 1 : 0;
}
