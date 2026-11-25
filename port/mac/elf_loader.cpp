// Load the GCC game built with -fno-stack-protector: the x86-64 one under Rosetta, or the AArch64 one
// on Apple silicon. Linux TLS canary accesses are rejected; executable code is never patched.
//
// The console window 0x7f000000..0x84000000 (MEM1, then this image at 0x82800000) sits at a
// 4 GB-aligned base: 0 under Rosetta, which lets the host free the low addresses, and the first free
// 4 GB block on Apple silicon, where the low 4 GB of a native process is never mapped. The game keeps
// the low 32 bits of its addresses and widens them against its own image (mu_window.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "elf_loader.h"
#include "elf_types.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__aarch64__)
#include <libkern/OSCacheControl.h>
#endif

namespace mac_elf {
namespace {
// Console addresses, as the ELF is linked; the host addresses are base plus these.
constexpr uintptr_t window_start=0x7f000000, window_end=0x84000000;
constexpr uintptr_t mem1=0x80000000, image_base=0x82800000;
#if defined(__aarch64__)
constexpr uint16_t machine=183;   // EM_AARCH64
constexpr size_t page=16384;
#else
constexpr uint16_t machine=62;    // EM_X86_64
constexpr size_t page=4096;
#endif
uintptr_t base=0;
bool reserved=false;
int reservation_error=0;
uintptr_t floor_page(uintptr_t p) { return p & ~(page-1); }
uintptr_t ceil_page(uintptr_t p) { return (p+page-1) & ~(page-1); }
uintptr_t host(uint64_t console) { return base+console; }
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error("Mac ELF: "+why); }
// A hint without MAP_FIXED cannot replace any existing region. The entire window stays reserved for
// the process's lifetime, including gaps between ELF segments.
bool reserve_at(uintptr_t at) {
  void* p=mmap(reinterpret_cast<void*>(at),window_end-window_start,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
  if (p==MAP_FAILED) { reservation_error=errno; return false; }
  if (uintptr_t(p)!=at) { munmap(p,window_end-window_start); reservation_error=EEXIST; return false; }
  return true;
}
void reserve_early() {
  if (reserved) return;
#if defined(__aarch64__)
  // The first 4 GB block, above the executable and dyld's shared region, whose window is free.
  for (uintptr_t block=2; block<0x800 && !reserved; ++block)
    if (reserve_at((block<<32)+window_start)) { base=block<<32; reserved=true; }
#else
  reserved=reserve_at(window_start);
#endif
}
__attribute__((constructor(101))) void earliest_reservation() { reserve_early(); }
void protect(uintptr_t at, size_t n, int prot) {
  require(mprotect(reinterpret_cast<void*>(at),n,prot)==0,
          "mprotect at "+std::to_string(at)+": "+std::strerror(errno));
}
// MAP_FIXED is used only inside our successfully acquired PROT_NONE reservation.
// Validate that a segment does not overlap an already mapped segment before this call.
void map_owned(uintptr_t console, size_t n) {
  require(reserved && console>=window_start && console<window_end && n<=window_end-console,"mapping outside owned window");
  void* p=mmap(reinterpret_cast<void*>(host(console)),n,PROT_READ|PROT_WRITE,
               MAP_FIXED|MAP_PRIVATE|MAP_ANON,-1,0);
  require(p!=MAP_FAILED && uintptr_t(p)==host(console),"fixed mapping failed: "+std::string(std::strerror(errno)));
}

#if defined(__aarch64__)
// ---- the printf family across the two AArch64 calling conventions ----
// AArch64 Linux passes variadic arguments like any other, in the next free x or v register and then
// on the stack; Apple's convention puts every one of them on the stack. A Linux va_list is a 32-byte
// struct, Apple's a pointer to 8-byte slots. The game's printf-family imports therefore come here:
// the format string gives each argument's class, it is fetched from where Linux put it, and the
// arguments are laid out as Apple's va_list for Darwin's v* function.
struct LinuxVaList { const uint8_t* stack; const uint8_t* gr_top; const uint8_t* vr_top; int32_t gr_offs, vr_offs; };
static_assert(sizeof(LinuxVaList)==32, "AAPCS64 va_list");
struct LinuxArgs { uint64_t x[8]; uint64_t d[8]; const uint64_t* stack; };   // saved by a thunk on entry
static_assert(std::is_same<va_list,char*>::value, "Darwin's AArch64 va_list is a pointer to 8-byte slots");

class LinuxVarargs {
 public:
  LinuxVarargs(const LinuxArgs& regs, int named_gp) : regs_(&regs), gp_(named_gp), stack_(regs.stack) {}
  explicit LinuxVarargs(const LinuxVaList& va) : va_(va) {}
  uint64_t gp() {
    if (regs_) return gp_<8 ? regs_->x[gp_++] : *stack_++;
    uint64_t v;
    if (va_.gr_offs<0) { std::memcpy(&v,va_.gr_top+va_.gr_offs,8); va_.gr_offs+=8; }
    else { std::memcpy(&v,va_.stack,8); va_.stack+=8; }
    return v;
  }
  uint64_t fp() {
    if (regs_) return fp_<8 ? regs_->d[fp_++] : *stack_++;
    uint64_t v;
    if (va_.vr_offs<0) { std::memcpy(&v,va_.vr_top+va_.vr_offs,8); va_.vr_offs+=16; }   // 16-byte register slots
    else { std::memcpy(&v,va_.stack,8); va_.stack+=8; }
    return v;
  }
 private:
  const LinuxArgs* regs_=nullptr;
  int gp_=0, fp_=0;
  const uint64_t* stack_=nullptr;
  LinuxVaList va_{};
};

// The arguments a format consumes, in order, one Apple slot each. Positional arguments and long
// double (binary128 on Linux, double on Apple) have no faithful translation: they stop the walk,
// so the conversion that needed them prints as a zero value or a null string.
struct Slots { uint64_t value[64]; int count=0; };
void collect(const char* fmt, LinuxVarargs& in, Slots& out) {
  static bool warned=false;
  auto stop=[&](const char* why) { if (!warned) { std::fprintf(stderr,"Mac ELF: printf %s in \"%s\" is unsupported\n",why,fmt); warned=true; } };
  auto push=[&](uint64_t v) { if (out.count<64) out.value[out.count++]=v; };
  for (const char* p=fmt; *p; ++p) {
    if (*p!='%') continue;
    if (*++p=='%') continue;
    while (*p && std::strchr("-+ #0'",*p)) ++p;
    if (*p=='*') { push(in.gp()); ++p; } else while (*p>='0' && *p<='9') ++p;
    if (*p=='$') { stop("positional argument"); return; }
    if (*p=='.') { ++p; if (*p=='*') { push(in.gp()); ++p; } else while (*p>='0' && *p<='9') ++p; }
    bool long_double=false;
    while (*p && std::strchr("hljztqL",*p)) { if (*p=='L') long_double=true; ++p; }
    if (!*p) return;
    if (std::strchr("fFeEgGaA",*p)) {
      if (long_double) { stop("long double"); return; }
      push(in.fp());
    } else if (std::strchr("diouxXcCsSpn",*p)) {
      push(in.gp());
    }
  }
}
va_list apple_va(Slots& s) { return reinterpret_cast<va_list>(s.value); }

enum PrintfKind { kPrintf, kFprintf, kSprintf, kSnprintf };
int printf_dispatch(int kind, const LinuxArgs* a) {
  static const int named[]={1,2,2,3};
  const char* fmt=reinterpret_cast<const char*>(a->x[named[kind]-1]);
  LinuxVarargs in(*a,named[kind]);
  Slots s; collect(fmt,in,s);
  switch (kind) {
    case kPrintf: return std::vprintf(fmt,apple_va(s));
    case kFprintf: return std::vfprintf(reinterpret_cast<FILE*>(a->x[0]),fmt,apple_va(s));
    case kSprintf: return std::vsprintf(reinterpret_cast<char*>(a->x[0]),fmt,apple_va(s));
    default: return std::vsnprintf(reinterpret_cast<char*>(a->x[0]),size_t(a->x[1]),fmt,apple_va(s));
  }
}
// Each thunk saves x0-x7 and d0-d7 and points at the caller's stacked arguments, then lets
// printf_dispatch (through mu_linux_printf, at the end of this file) read them as Linux placed them.
#define LINUX_PRINTF_THUNK(name, kind) \
  __attribute__((naked)) void name() { \
    __asm__ volatile( \
      "sub sp, sp, #160\n" "stp x29, x30, [sp, #144]\n" "add x29, sp, #144\n" \
      "stp x0, x1, [sp, #0]\n" "stp x2, x3, [sp, #16]\n" "stp x4, x5, [sp, #32]\n" "stp x6, x7, [sp, #48]\n" \
      "stp d0, d1, [sp, #64]\n" "stp d2, d3, [sp, #80]\n" "stp d4, d5, [sp, #96]\n" "stp d6, d7, [sp, #112]\n" \
      "add x9, sp, #160\n" "str x9, [sp, #128]\n" \
      "mov w0, #" #kind "\n" "mov x1, sp\n" "bl _mu_linux_printf\n" \
      "ldp x29, x30, [sp, #144]\n" "add sp, sp, #160\n" "ret\n"); \
  }
LINUX_PRINTF_THUNK(linux_printf, 0)
LINUX_PRINTF_THUNK(linux_fprintf, 1)
LINUX_PRINTF_THUNK(linux_sprintf, 2)
LINUX_PRINTF_THUNK(linux_snprintf, 3)
#undef LINUX_PRINTF_THUNK
// A Linux va_list is larger than 16 bytes, so it arrives by reference.
int linux_vsprintf(char* out, const char* fmt, const LinuxVaList* va) {
  LinuxVarargs in(*va); Slots s; collect(fmt,in,s); return std::vsprintf(out,fmt,apple_va(s));
}
int linux_vsnprintf(char* out, size_t n, const char* fmt, const LinuxVaList* va) {
  LinuxVarargs in(*va); Slots s; collect(fmt,in,s); return std::vsnprintf(out,n,fmt,apple_va(s));
}
#endif

struct Import { const char* name; uintptr_t address; };
#define IMPORT(f) {#f,reinterpret_cast<uintptr_t>(&::f)}
#define THUNK(f) {#f,reinterpret_cast<uintptr_t>(&linux_##f)}
uintptr_t import(const char* name) {
  require(std::strcmp(name,"__stack_chk_fail") != 0 && std::strcmp(name,"__stack_chk_guard") != 0,
          "Linux stack protector import; rebuild melee_game.so with -fno-stack-protector");
  // x86-64 Darwin and glibc share the SysV integer/pointer/float calling ABI.
  // va_list has the same register-save/overflow layout: v[s]printf can be called
  // directly. On AArch64 they differ for variadic calls only (the thunks above).
  // FILE* is opaque to this game: every fopen/fwrite/etc goes to Darwin,
  // so no Linux FILE layout is ever inspected (the structs themselves differ).
  // long/size_t are 64-bit on both; strtoul and printf widths therefore agree.
  static const Import table[]={
#if defined(__aarch64__)
    THUNK(printf), THUNK(sprintf), THUNK(vsprintf), THUNK(vsnprintf), THUNK(fprintf), THUNK(snprintf),
#else
    IMPORT(printf), IMPORT(sprintf), IMPORT(vsprintf), IMPORT(vsnprintf), IMPORT(fprintf), IMPORT(snprintf),
#endif
    IMPORT(strtoul),
    {"strchr",reinterpret_cast<uintptr_t>(static_cast<char*(*)(char*,int)>(&::strchr))},
    IMPORT(strlen), IMPORT(memcmp), IMPORT(strncmp), IMPORT(strncpy),
    IMPORT(memset), IMPORT(memcpy), IMPORT(strcpy), IMPORT(fclose),
    {"strrchr",reinterpret_cast<uintptr_t>(static_cast<char*(*)(char*,int)>(&::strrchr))},
    {"memchr",reinterpret_cast<uintptr_t>(static_cast<void*(*)(void*,int,size_t)>(&::memchr))},
    IMPORT(fflush), IMPORT(fopen), IMPORT(getenv), IMPORT(fabsf), IMPORT(strcmp),
    IMPORT(atoi),
    {"fabs",reinterpret_cast<uintptr_t>(static_cast<double(*)(double)>(&::fabs))},
    IMPORT(memmove),
    IMPORT(sqrtf), IMPORT(fwrite),
    // Newer game shims allocate with malloc/free; both sides use Darwin's one allocator.
    IMPORT(malloc), IMPORT(free), IMPORT(strcat)
  };
  // fabs[f] is exact sign-bit clearing; sqrtf is IEEE single precision on both.
  // Simulation's other math stays bound to its own ELF definitions (DT_SYMBOLIC).
  for (const auto& item:table) if (!std::strcmp(name,item.name)) return item.address;
  throw std::runtime_error(std::string("Mac ELF: unlisted strong import: ")+name);
}
#undef IMPORT
#undef THUNK
} // namespace

uintptr_t window_base() { reserve_early(); return base; }
bool reserve_window(std::string* error) {
  reserve_early();
  if (reserved) return true;
  if (error) *error="cannot reserve the console window 0x7f000000..0x84000000 without replacing mappings: "+std::string(std::strerror(reservation_error));
  return false;
}
bool activate_mem1(std::string* error) {
  if (!reserve_window(error)) return false;
  if (mprotect(reinterpret_cast<void*>(host(mem1)),image_base-mem1,PROT_READ|PROT_WRITE)) {
    if (error) *error="cannot activate MEM1: "+std::string(std::strerror(errno));
    return false;
  }
  std::fprintf(stderr,"Mac layout: owned window 0x%lx..0x%lx; MEM1 0x%lx..0x%lx\n",host(window_start),host(window_end),host(mem1),host(image_base));
  return true;
}
void deactivate_mem1() {
  if (reserved) { madvise(reinterpret_cast<void*>(host(mem1)),image_base-mem1,MADV_DONTNEED); mprotect(reinterpret_cast<void*>(host(mem1)),image_base-mem1,PROT_NONE); }
}

bool Image::load(const std::string& path, std::string* error) {
  try {
    require(mappings_.empty(),"image already loaded");
    require(reserve_window(error),error ? *error : "window reservation failed");
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    require(bool(input),"cannot open "+path);
    const auto length=input.tellg();
    require(length>=std::streamoff(sizeof(Ehdr)) && length<64*1024*1024,"invalid ELF file size");
    std::vector<uint8_t> file(static_cast<size_t>(length));
    input.seekg(0); input.read(reinterpret_cast<char*>(file.data()),file.size());
    require(bool(input),"short ELF read");
    Ehdr eh; std::memcpy(&eh,file.data(),sizeof eh);
    require(!std::memcmp(eh.ident,"\177ELF\2\1\1",7) && eh.type==3 && eh.machine==machine && eh.version==1,
            machine==62 ? "expected little-endian x86-64 ET_DYN" : "expected little-endian AArch64 ET_DYN");
    require(eh.ehsize==sizeof(Ehdr) && eh.phentsize==sizeof(Phdr) && eh.phnum>0 && eh.phnum<100 &&
            eh.phoff<=file.size() && uint64_t(eh.phnum)*sizeof(Phdr)<=file.size()-eh.phoff,"invalid program headers");
    std::vector<Phdr> headers(eh.phnum);
    std::memcpy(headers.data(),file.data()+eh.phoff,headers.size()*sizeof(Phdr));
    std::vector<std::pair<uintptr_t,uintptr_t>> pages;
    const Phdr* dynamic=nullptr;
    for (const auto& p:headers) {
      require(p.type!=PT_TLS,"TLS segments are unsupported");
      if (p.type==PT_DYNAMIC) { require(!dynamic,"multiple dynamic segments"); dynamic=&p; }
      if (p.type!=PT_LOAD) continue;
      require(p.memsz && p.filesz<=p.memsz && p.offset<=file.size() && p.filesz<=file.size()-p.offset &&
              p.vaddr>=image_base && p.vaddr<window_end && p.memsz<=window_end-p.vaddr &&
              !(p.flags&~7u) && !((p.flags&PF_X)&&(p.flags&PF_W)) &&
              p.align>=page && p.align%page==0 && p.vaddr%page==p.offset%page,"invalid/out-of-window PT_LOAD");
      const auto lo=floor_page(p.vaddr), hi=ceil_page(p.vaddr+p.memsz);
      for (const auto& old:pages) require(hi<=old.first || lo>=old.second,"overlapping load pages");
      pages.push_back({lo,hi});
      size_=std::max(size_,uint32_t(p.vaddr+p.memsz-image_base));
    }
    size_t canary_sites=0;
#if !defined(__aarch64__)
    for (const auto& p:headers) if (p.type==PT_LOAD && (p.flags&PF_X)) {
      const auto* code=file.data()+p.offset;
      for (size_t i=0;i+8<=p.filesz;++i) {
        if (code[i]!=0x64) continue;
        size_t op=i+1;
        if (code[op]>=0x40 && code[op]<=0x4f) ++op;
        if (op+7>p.filesz) continue;
        const auto opcode=code[op];
        if (opcode!=0x8b && opcode!=0x89 && opcode!=0x2b && opcode!=0x29 &&
            opcode!=0x33 && opcode!=0x31 && opcode!=0x3b && opcode!=0x39) continue;
        uint32_t displacement; std::memcpy(&displacement,code+op+3,4);
        if ((code[op+1]&0xc7)!=4 || code[op+2]!=0x25 || displacement!=0x28) continue;
        ++canary_sites; i=op+6;
      }
    }
#endif
    // AArch64 has no %fs canary: its stack protector would import __stack_chk_guard (import() refuses it).
    require(canary_sites==0,"found "+std::to_string(canary_sites)+
            " Linux %fs:0x28 stack canary sites; rebuild melee_game.so with -fno-stack-protector (no code patching)");
    require(dynamic && !pages.empty() && std::min_element(pages.begin(),pages.end())->first==image_base,"missing image base or PT_DYNAMIC");
    for (const auto& p:headers) if (p.type==PT_LOAD) {
      map_owned(floor_page(p.vaddr),ceil_page(p.vaddr+p.memsz)-floor_page(p.vaddr));
      mappings_.push_back({reinterpret_cast<void*>(host(p.vaddr)),uint32_t(p.memsz)});
      if (p.flags&PF_W) writable_.push_back(mappings_.back());
      std::memcpy(reinterpret_cast<void*>(host(p.vaddr)),file.data()+p.offset,p.filesz);
    }
    // Ranges are checked as linked (console addresses); reads and writes go to host(...).
    auto valid=[&](uintptr_t at, uint64_t n, uint32_t flags=0) {
      for (const auto& p:headers) if (p.type==PT_LOAD && at>=p.vaddr && at-p.vaddr<=p.memsz && n<=p.memsz-(at-p.vaddr) && (p.flags&flags)==flags) return true;
      return false;
    };
    require(valid(dynamic->vaddr,dynamic->memsz) && dynamic->memsz%sizeof(Dyn)==0,"invalid dynamic table");
    std::map<int64_t,uint64_t> dt;
    std::vector<uint64_t> needed;
    bool terminated=false;
    for (size_t i=0;i<dynamic->memsz/sizeof(Dyn);++i) {
      const auto d=reinterpret_cast<const Dyn*>(host(dynamic->vaddr))[i];
      if (d.tag==DT_NULL) { terminated=true; break; }
      switch(d.tag) {
        case DT_NEEDED: case DT_PLTRELSZ: case DT_PLTGOT: case DT_HASH: case DT_STRTAB:
        case DT_SYMTAB: case DT_RELA: case DT_RELASZ: case DT_RELAENT: case DT_STRSZ:
        case DT_SYMENT: case DT_INIT: case DT_FINI: case DT_SONAME: case DT_SYMBOLIC:
        case DT_PLTREL: case DT_JMPREL: case DT_INIT_ARRAY: case DT_FINI_ARRAY:
        case DT_INIT_ARRAYSZ: case DT_FINI_ARRAYSZ: case DT_RUNPATH: case DT_FLAGS:
        case DT_GNU_HASH: case DT_VERSYM: case DT_RELACOUNT: case DT_VERNEED: case DT_VERNEEDNUM: break;
        default: require(false,"unexpected dynamic tag "+std::to_string(d.tag));
      }
      if (d.tag==DT_NEEDED) needed.push_back(d.value);
      if (d.tag!=DT_NEEDED) require(!dt.count(d.tag),"duplicate dynamic tag");
      dt[d.tag]=d.value;
    }
    require(terminated && dt[DT_SYMENT]==sizeof(Sym) && (!dt[DT_RELASZ] || dt[DT_RELAENT]==sizeof(Rela)) &&
            (!dt[DT_PLTRELSZ] || dt[DT_PLTREL]==DT_RELA) && (dt[DT_FLAGS]&~2ull)==0,"unsupported dynamic format/flags");
    string_bytes_=dt[DT_STRSZ];
    require(string_bytes_ && valid(dt[DT_STRTAB],string_bytes_),"invalid string table");
    symtab_=host(dt[DT_SYMTAB]); strtab_=host(dt[DT_STRTAB]);
    for (auto offset:needed) {
      require(offset<string_bytes_,"invalid dependency name");
      const char* dep=reinterpret_cast<const char*>(strtab_+offset);
      require(std::memchr(dep,0,string_bytes_-offset),"unterminated dependency name");
      require(!std::strcmp(dep,"libc.so.6") || !std::strcmp(dep,"libm.so.6"),std::string("unexpected dependency: ")+dep);
    }
    if (dt.count(DT_HASH)) {
      require(valid(dt[DT_HASH],8),"invalid DT_HASH");
      symbol_count_=reinterpret_cast<const uint32_t*>(host(dt[DT_HASH]))[1];
    } else {
      const uintptr_t at=dt[DT_GNU_HASH];
      require(valid(at,16),"missing/invalid GNU hash");
      const auto* hash=reinterpret_cast<const uint32_t*>(host(at));
      const uint32_t buckets=hash[0], first=hash[1], bloom=hash[2];
      require(buckets && bloom && buckets<100000 && bloom<100000,"invalid GNU hash sizes");
      const uintptr_t bucket_at=at+16+uint64_t(bloom)*8;
      require(valid(bucket_at,uint64_t(buckets)*4),"invalid GNU hash buckets");
      const auto* bucket=reinterpret_cast<const uint32_t*>(host(bucket_at));
      const uintptr_t chains=bucket_at+uint64_t(buckets)*4;
      symbol_count_=first;
      for (uint32_t b=0;b<buckets;++b) if (bucket[b]) {
        uint32_t s=bucket[b]; require(s>=first,"invalid GNU hash index");
        for (;;) {
          require(s<100000 && valid(chains+uint64_t(s-first)*4,4),"unterminated GNU hash chain");
          const auto value=*reinterpret_cast<const uint32_t*>(host(chains+uint64_t(s-first)*4));
          symbol_count_=std::max(symbol_count_,size_t(s)+1);
          if (value&1) break;
          ++s;
        }
      }
    }
    require(symbol_count_ && symbol_count_<100000 && valid(dt[DT_SYMTAB],symbol_count_*sizeof(Sym)),"invalid symbol table");
    const auto* symbols=reinterpret_cast<const Sym*>(symtab_);
    auto name=[&](const Sym& s) {
      require(s.name<string_bytes_,"invalid symbol name");
      const char* n=reinterpret_cast<const char*>(strtab_+s.name);
      require(std::memchr(n,0,string_bytes_-s.name),"unterminated symbol name"); return n;
    };
    std::vector<uintptr_t> resolved(symbol_count_);
    size_t strong=0,weak=0;
    for (size_t i=1;i<symbol_count_;++i) {
      const auto& s=symbols[i]; const char* n=name(s);
      require((s.info&15)!=6 && (s.info&15)!=10,"TLS/IFUNC symbols unsupported");
      // Zero-size linker markers such as __bss_end__ may sit exactly at a segment's end.
      if (s.shndx) { require(s.shndx!=0xfff1 && valid(s.value,s.size),std::string("invalid defined symbol ")+n); resolved[i]=host(s.value); }
      else if ((s.info>>4)==2) { resolved[i]=0; ++weak; } // All undefined weak symbols, including __cxa_finalize, are absent.
      else { resolved[i]=import(n); ++strong; }
    }
#if defined(__aarch64__)
    enum : uint32_t { R_ABS64=257, R_GLOB_DAT=1025, R_JUMP_SLOT=1026, R_RELATIVE=1027 };
#else
    enum : uint32_t { R_ABS64=1, R_GLOB_DAT=6, R_JUMP_SLOT=7, R_RELATIVE=8 };
#endif
    size_t relocations=0;
    auto relocate=[&](uintptr_t at, uint64_t bytes) {
      if (!bytes) return;
      require(bytes%sizeof(Rela)==0 && valid(at,bytes),"invalid RELA range");
      for (size_t i=0;i<bytes/sizeof(Rela);++i) {
        const auto r=reinterpret_cast<const Rela*>(host(at))[i];
        const uint32_t type=uint32_t(r.info); const uint64_t sym=r.info>>32;
        require(valid(r.offset,8,PF_W) && sym<symbol_count_,"invalid relocation target/symbol");
        uint64_t value;
        switch(type) {
          case R_RELATIVE: require(sym==0,"RELATIVE with symbol"); value=host(uint64_t(r.addend)); break;
          case R_ABS64: value=resolved[sym]+r.addend; break;
          case R_GLOB_DAT: case R_JUMP_SLOT: require(r.addend==0,"GLOB_DAT/JUMP_SLOT addend"); value=resolved[sym]; break;
          default: require(false,"unsupported relocation type "+std::to_string(type)); value=0;
        }
        std::memcpy(reinterpret_cast<void*>(host(r.offset)),&value,8); ++relocations;
      }
    };
    relocate(dt[DT_RELA],dt[DT_RELASZ]); relocate(dt[DT_JMPREL],dt[DT_PLTRELSZ]);
    std::fprintf(stderr,"Mac ELF: %zu symbols (%zu strong imports, %zu weak -> 0), %zu relocations; canary sites %zu (no patching)\n",symbol_count_,strong,weak,relocations,canary_sites);
    for (const auto& p:headers) if (p.type==PT_LOAD) {
      const int prot=((p.flags&PF_R)?PROT_READ:0)|((p.flags&PF_W)?PROT_WRITE:0)|((p.flags&PF_X)?PROT_EXEC:0);
#if defined(__aarch64__)
      // The code was written through the data side; the instruction side must not see stale lines.
      if (p.flags&PF_X) sys_icache_invalidate(reinterpret_cast<void*>(host(p.vaddr)),p.filesz);
#endif
      protect(host(floor_page(p.vaddr)),ceil_page(p.vaddr+p.memsz)-floor_page(p.vaddr),prot);
      std::fprintf(stderr,"Mac layout: PT_LOAD 0x%llx..0x%llx %c%c%c\n",(unsigned long long)host(p.vaddr),(unsigned long long)host(p.vaddr+p.memsz),(p.flags&PF_R)?'r':'-',(p.flags&PF_W)?'w':'-',(p.flags&PF_X)?'x':'-');
    }
    std::fprintf(stderr,"Mac layout: image 0x%lx..0x%lx\n",host(image_base),host(image_base+size_));
    // Init/fini array entries hold relocated (host) addresses; DT_INIT and DT_FINI hold linked ones.
    auto array=[&](int64_t address_tag,int64_t size_tag) {
      const auto at=dt[address_tag], bytes=dt[size_tag];
      require(bytes%8==0 && (!bytes || valid(at,bytes)),"invalid init/fini array");
      std::vector<uintptr_t> result;
      for (size_t i=0;i<bytes/8;++i) {
        auto fn=reinterpret_cast<const uintptr_t*>(host(at))[i];
        require(fn>=base && valid(fn-base,1,PF_X),"init/fini entry outside executable segment"); result.push_back(fn);
      }
      return result;
    };
    auto init_array=array(DT_INIT_ARRAY,DT_INIT_ARRAYSZ);
    fini_array_=array(DT_FINI_ARRAY,DT_FINI_ARRAYSZ);
    require(!dt[DT_FINI] || valid(dt[DT_FINI],1,PF_X),"invalid DT_FINI");
    require(!dt[DT_INIT] || valid(dt[DT_INIT],1,PF_X),"invalid DT_INIT");
    fini_=dt[DT_FINI] ? host(dt[DT_FINI]) : 0;
    if (dt[DT_INIT]) reinterpret_cast<void(*)()>(host(dt[DT_INIT]))();
    for (auto fn:init_array) reinterpret_cast<void(*)()>(fn)();
    initialized_=true;
    return true;
  } catch (const std::exception& e) { if (error) *error=e.what(); unload(); return false; }
}
void* Image::symbol(const char* name) const {
  const auto* symbols=reinterpret_cast<const Sym*>(symtab_);
  for (size_t i=1;i<symbol_count_;++i) {
    const auto& s=symbols[i];
    if (s.shndx && s.name<string_bytes_ && !std::strcmp(name,reinterpret_cast<const char*>(strtab_+s.name))) return reinterpret_cast<void*>(host(s.value));
  }
  return nullptr;
}
void Image::unload() {
  if (initialized_) {
    for (auto it=fini_array_.rbegin();it!=fini_array_.rend();++it) reinterpret_cast<void(*)()>(*it)();
    if (fini_) reinterpret_cast<void(*)()>(fini_)();
  }
  for (const auto& r:mappings_) {
    const auto at=floor_page(uintptr_t(r.address)), end=ceil_page(uintptr_t(r.address)+r.size);
    // Retain the reservation while releasing the anonymous backing pages.
    mmap(reinterpret_cast<void*>(at),end-at,PROT_NONE,MAP_FIXED|MAP_PRIVATE|MAP_ANON,-1,0);
  }
  mappings_.clear(); writable_.clear(); fini_array_.clear();
  fini_=symtab_=strtab_=0; symbol_count_=string_bytes_=0; size_=0; initialized_=false;
}
} // namespace mac_elf
#if defined(__aarch64__)
// The thunks' target: a C name the assembly can call.
extern "C" __attribute__((used, visibility("hidden"))) int mu_linux_printf(int kind, const void* args) {
  return mac_elf::printf_dispatch(kind, static_cast<const mac_elf::LinuxArgs*>(args));
}
#endif
