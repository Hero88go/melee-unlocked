// ELF64 on-disk structures; macOS SDK has no elf.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace mac_elf {
struct Ehdr {
  uint8_t ident[16]; uint16_t type, machine; uint32_t version;
  uint64_t entry, phoff, shoff; uint32_t flags;
  uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Phdr {
  uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct Dyn { int64_t tag; uint64_t value; };
struct Sym {
  uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size;
};
struct Rela { uint64_t offset, info; int64_t addend; };
static_assert(sizeof(Ehdr)==64 && sizeof(Phdr)==56 && sizeof(Dyn)==16 &&
              sizeof(Sym)==24 && sizeof(Rela)==24, "ELF64 disk layout");
constexpr uint32_t PT_LOAD=1, PT_DYNAMIC=2, PT_TLS=7, PF_X=1, PF_W=2, PF_R=4;
constexpr int64_t DT_NULL=0, DT_NEEDED=1, DT_PLTRELSZ=2, DT_PLTGOT=3,
  DT_HASH=4, DT_STRTAB=5, DT_SYMTAB=6, DT_RELA=7, DT_RELASZ=8, DT_RELAENT=9,
  DT_STRSZ=10, DT_SYMENT=11, DT_INIT=12, DT_FINI=13, DT_SONAME=14,
  DT_SYMBOLIC=16, DT_PLTREL=20, DT_JMPREL=23, DT_INIT_ARRAY=25,
  DT_FINI_ARRAY=26, DT_INIT_ARRAYSZ=27, DT_FINI_ARRAYSZ=28, DT_RUNPATH=29,
  DT_FLAGS=30, DT_GNU_HASH=0x6ffffef5, DT_VERSYM=0x6ffffff0,
  DT_RELACOUNT=0x6ffffff9, DT_VERNEED=0x6ffffffe, DT_VERNEEDNUM=0x6fffffff;
} // namespace mac_elf
