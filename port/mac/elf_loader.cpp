// Load the GCC x86-64 game built with -fno-stack-protector.
// Linux TLS canary accesses are rejected; executable code is never patched.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "elf_loader.h"
#include "elf_types.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace mac_elf {
namespace {
constexpr uintptr_t window_start=0x7f000000, window_end=0x84000000;
constexpr uintptr_t mem1=0x80000000, image_base=0x82800000;
constexpr size_t page=4096;
bool reserved=false;
int reservation_error=0;
uintptr_t floor_page(uintptr_t p) { return p & ~(page-1); }
uintptr_t ceil_page(uintptr_t p) { return (p+page-1) & ~(page-1); }
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error("Mac ELF: "+why); }
void reserve_early() {
  if (reserved) return;
  // A hint without MAP_FIXED cannot replace any existing region. Keep the entire
  // window reserved for process lifetime, including gaps between ELF segments.
  void* p=mmap(reinterpret_cast<void*>(window_start),window_end-window_start,
               PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
  if (p==MAP_FAILED) { reservation_error=errno; return; }
  if (uintptr_t(p)!=window_start) { munmap(p,window_end-window_start); reservation_error=EEXIST; return; }
  reserved=true;
}
__attribute__((constructor(101))) void earliest_reservation() { reserve_early(); }
void protect(uintptr_t at, size_t n, int prot) {
  require(mprotect(reinterpret_cast<void*>(at),n,prot)==0,
          "mprotect at "+std::to_string(at)+": "+std::strerror(errno));
}
// MAP_FIXED is used only inside our successfully acquired PROT_NONE reservation.
// Validate that a segment does not overlap an already mapped segment before this call.
void map_owned(uintptr_t at, size_t n) {
  require(reserved && at>=window_start && at<window_end && n<=window_end-at,"mapping outside owned window");
  void* p=mmap(reinterpret_cast<void*>(at),n,PROT_READ|PROT_WRITE,
               MAP_FIXED|MAP_PRIVATE|MAP_ANON,-1,0);
  require(p!=MAP_FAILED && uintptr_t(p)==at,"fixed mapping failed: "+std::string(std::strerror(errno)));
}
struct Import { const char* name; uintptr_t address; };
#define IMPORT(f) {#f,reinterpret_cast<uintptr_t>(&::f)}
uintptr_t import(const char* name) {
  require(std::strcmp(name,"__stack_chk_fail") != 0,
          "Linux stack protector import; rebuild melee_game.so with -fno-stack-protector");
  // x86-64 Darwin and glibc share the SysV integer/pointer/float calling ABI.
  // va_list has the same register-save/overflow layout: v[s]printf can be called
  // directly. FILE* is opaque to this game: every fopen/fwrite/etc goes to Darwin,
  // so no Linux FILE layout is ever inspected (the structs themselves differ).
  // long/size_t are 64-bit on both; strtoul and printf widths therefore agree.
  static const Import table[]={
    IMPORT(printf), IMPORT(sprintf), IMPORT(strtoul),
    {"strchr",reinterpret_cast<uintptr_t>(static_cast<char*(*)(char*,int)>(&::strchr))},
    IMPORT(strlen), IMPORT(memcmp), IMPORT(vsprintf), IMPORT(strncmp), IMPORT(strncpy),
    IMPORT(memset), IMPORT(vsnprintf), IMPORT(memcpy), IMPORT(strcpy), IMPORT(fclose),
    {"strrchr",reinterpret_cast<uintptr_t>(static_cast<char*(*)(char*,int)>(&::strrchr))},
    {"memchr",reinterpret_cast<uintptr_t>(static_cast<void*(*)(void*,int,size_t)>(&::memchr))},
    IMPORT(fflush), IMPORT(fopen), IMPORT(getenv), IMPORT(fabsf), IMPORT(strcmp),
    IMPORT(fprintf), IMPORT(atoi),
    {"fabs",reinterpret_cast<uintptr_t>(static_cast<double(*)(double)>(&::fabs))},
    IMPORT(snprintf), IMPORT(memmove),
    IMPORT(sqrtf), IMPORT(fwrite)
  };
  // fabs[f] is exact sign-bit clearing; sqrtf is IEEE single precision on x86.
  // Simulation's other math stays bound to its own ELF definitions (DT_SYMBOLIC).
  for (const auto& item:table) if (!std::strcmp(name,item.name)) return item.address;
  throw std::runtime_error(std::string("Mac ELF: unlisted strong import: ")+name);
}
#undef IMPORT
} // namespace

bool reserve_window(std::string* error) {
  if (reserved) return true;
  if (error) *error="cannot reserve 0x7f000000..0x84000000 without replacing mappings: "+std::string(std::strerror(reservation_error));
  return false;
}
bool activate_mem1(std::string* error) {
  if (!reserve_window(error)) return false;
  if (mprotect(reinterpret_cast<void*>(mem1),image_base-mem1,PROT_READ|PROT_WRITE)) {
    if (error) *error="cannot activate MEM1: "+std::string(std::strerror(errno));
    return false;
  }
  std::fprintf(stderr,"Mac layout: owned window 0x%lx..0x%lx; MEM1 0x%lx..0x%lx\n",window_start,window_end,mem1,image_base);
  return true;
}
void deactivate_mem1() {
  if (reserved) { madvise(reinterpret_cast<void*>(mem1),image_base-mem1,MADV_DONTNEED); mprotect(reinterpret_cast<void*>(mem1),image_base-mem1,PROT_NONE); }
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
    require(!std::memcmp(eh.ident,"\177ELF\2\1\1",7) && eh.type==3 && eh.machine==62 && eh.version==1,
            "expected little-endian x86-64 ET_DYN");
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
              p.align==page && p.vaddr%page==p.offset%page,"invalid/out-of-window PT_LOAD");
      const auto lo=floor_page(p.vaddr), hi=ceil_page(p.vaddr+p.memsz);
      for (const auto& old:pages) require(hi<=old.first || lo>=old.second,"overlapping load pages");
      pages.push_back({lo,hi});
      size_=std::max(size_,uint32_t(p.vaddr+p.memsz-image_base));
    }
    size_t canary_sites=0;
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
    require(canary_sites==0,"found "+std::to_string(canary_sites)+
            " Linux %fs:0x28 stack canary sites; rebuild melee_game.so with -fno-stack-protector (no code patching)");
    require(dynamic && !pages.empty() && std::min_element(pages.begin(),pages.end())->first==image_base,"missing image base or PT_DYNAMIC");
    for (const auto& p:headers) if (p.type==PT_LOAD) {
      map_owned(floor_page(p.vaddr),ceil_page(p.vaddr+p.memsz)-floor_page(p.vaddr));
      mappings_.push_back({reinterpret_cast<void*>(p.vaddr),uint32_t(p.memsz)});
      if (p.flags&PF_W) writable_.push_back(mappings_.back());
      std::memcpy(reinterpret_cast<void*>(p.vaddr),file.data()+p.offset,p.filesz);
    }
    auto valid=[&](uintptr_t at, uint64_t n, uint32_t flags=0) {
      for (const auto& p:headers) if (p.type==PT_LOAD && at>=p.vaddr && at-p.vaddr<=p.memsz && n<=p.memsz-(at-p.vaddr) && (p.flags&flags)==flags) return true;
      return false;
    };
    require(valid(dynamic->vaddr,dynamic->memsz) && dynamic->memsz%sizeof(Dyn)==0,"invalid dynamic table");
    std::map<int64_t,uint64_t> dt;
    std::vector<uint64_t> needed;
    bool terminated=false;
    for (size_t i=0;i<dynamic->memsz/sizeof(Dyn);++i) {
      const auto d=reinterpret_cast<const Dyn*>(dynamic->vaddr)[i];
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
    symtab_=dt[DT_SYMTAB]; strtab_=dt[DT_STRTAB]; string_bytes_=dt[DT_STRSZ];
    require(string_bytes_ && valid(strtab_,string_bytes_),"invalid string table");
    for (auto offset:needed) {
      require(offset<string_bytes_,"invalid dependency name");
      const char* dep=reinterpret_cast<const char*>(strtab_+offset);
      require(std::memchr(dep,0,string_bytes_-offset),"unterminated dependency name");
      require(!std::strcmp(dep,"libc.so.6") || !std::strcmp(dep,"libm.so.6"),std::string("unexpected dependency: ")+dep);
    }
    if (dt.count(DT_HASH)) {
      require(valid(dt[DT_HASH],8),"invalid DT_HASH");
      symbol_count_=reinterpret_cast<const uint32_t*>(dt[DT_HASH])[1];
    } else {
      const uintptr_t at=dt[DT_GNU_HASH];
      require(valid(at,16),"missing/invalid GNU hash");
      const auto* hash=reinterpret_cast<const uint32_t*>(at);
      const uint32_t buckets=hash[0], first=hash[1], bloom=hash[2];
      require(buckets && bloom && buckets<100000 && bloom<100000,"invalid GNU hash sizes");
      const uintptr_t bucket_at=at+16+uint64_t(bloom)*8;
      require(valid(bucket_at,uint64_t(buckets)*4),"invalid GNU hash buckets");
      const auto* bucket=reinterpret_cast<const uint32_t*>(bucket_at);
      const uintptr_t chains=bucket_at+uint64_t(buckets)*4;
      symbol_count_=first;
      for (uint32_t b=0;b<buckets;++b) if (bucket[b]) {
        uint32_t s=bucket[b]; require(s>=first,"invalid GNU hash index");
        for (;;) {
          require(s<100000 && valid(chains+uint64_t(s-first)*4,4),"unterminated GNU hash chain");
          const auto value=*reinterpret_cast<const uint32_t*>(chains+uint64_t(s-first)*4);
          symbol_count_=std::max(symbol_count_,size_t(s)+1);
          if (value&1) break;
          ++s;
        }
      }
    }
    require(symbol_count_ && symbol_count_<100000 && valid(symtab_,symbol_count_*sizeof(Sym)),"invalid symbol table");
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
      if (s.shndx) { require(s.shndx!=0xfff1 && valid(s.value,s.size),std::string("invalid defined symbol ")+n); resolved[i]=s.value; }
      else if ((s.info>>4)==2) { resolved[i]=0; ++weak; } // All undefined weak symbols, including __cxa_finalize, are absent.
      else { resolved[i]=import(n); ++strong; }
    }
    size_t relocations=0;
    auto relocate=[&](uintptr_t at, uint64_t bytes) {
      if (!bytes) return;
      require(bytes%sizeof(Rela)==0 && valid(at,bytes),"invalid RELA range");
      for (size_t i=0;i<bytes/sizeof(Rela);++i) {
        const auto r=reinterpret_cast<const Rela*>(at)[i];
        const uint32_t type=uint32_t(r.info); const uint64_t sym=r.info>>32;
        require(valid(r.offset,8,PF_W) && sym<symbol_count_,"invalid relocation target/symbol");
        uint64_t value;
        switch(type) {
          case 8: require(sym==0,"RELATIVE with symbol"); value=uint64_t(r.addend); break; // load bias is zero
          case 1: value=resolved[sym]+r.addend; break;
          case 6: case 7: require(r.addend==0,"GLOB_DAT/JUMP_SLOT addend"); value=resolved[sym]; break;
          default: require(false,"unsupported relocation type "+std::to_string(type)); value=0;
        }
        std::memcpy(reinterpret_cast<void*>(r.offset),&value,8); ++relocations;
      }
    };
    relocate(dt[DT_RELA],dt[DT_RELASZ]); relocate(dt[DT_JMPREL],dt[DT_PLTRELSZ]);
    std::fprintf(stderr,"Mac ELF: %zu symbols (%zu strong imports, %zu weak -> 0), %zu relocations; canary sites %zu (no patching)\n",symbol_count_,strong,weak,relocations,canary_sites);
    for (const auto& p:headers) if (p.type==PT_LOAD) {
      const int prot=((p.flags&PF_R)?PROT_READ:0)|((p.flags&PF_W)?PROT_WRITE:0)|((p.flags&PF_X)?PROT_EXEC:0);
      protect(floor_page(p.vaddr),ceil_page(p.vaddr+p.memsz)-floor_page(p.vaddr),prot);
      std::fprintf(stderr,"Mac layout: PT_LOAD 0x%llx..0x%llx %c%c%c\n",p.vaddr,p.vaddr+p.memsz,(p.flags&PF_R)?'r':'-',(p.flags&PF_W)?'w':'-',(p.flags&PF_X)?'x':'-');
    }
    std::fprintf(stderr,"Mac layout: image 0x%lx..0x%lx\n",image_base,image_base+size_);
    auto array=[&](int64_t address_tag,int64_t size_tag) {
      const auto at=dt[address_tag], bytes=dt[size_tag];
      require(bytes%8==0 && (!bytes || valid(at,bytes)),"invalid init/fini array");
      std::vector<uintptr_t> result;
      for (size_t i=0;i<bytes/8;++i) {
        auto fn=reinterpret_cast<const uintptr_t*>(at)[i];
        require(valid(fn,1,PF_X),"init/fini entry outside executable segment"); result.push_back(fn);
      }
      return result;
    };
    auto init_array=array(DT_INIT_ARRAY,DT_INIT_ARRAYSZ);
    fini_array_=array(DT_FINI_ARRAY,DT_FINI_ARRAYSZ); fini_=dt[DT_FINI];
    require(!fini_ || valid(fini_,1,PF_X),"invalid DT_FINI");
    require(!dt[DT_INIT] || valid(dt[DT_INIT],1,PF_X),"invalid DT_INIT");
    if (dt[DT_INIT]) reinterpret_cast<void(*)()>(dt[DT_INIT])();
    for (auto fn:init_array) reinterpret_cast<void(*)()>(fn)();
    initialized_=true;
    return true;
  } catch (const std::exception& e) { if (error) *error=e.what(); unload(); return false; }
}
void* Image::symbol(const char* name) const {
  const auto* symbols=reinterpret_cast<const Sym*>(symtab_);
  for (size_t i=1;i<symbol_count_;++i) {
    const auto& s=symbols[i];
    if (s.shndx && s.name<string_bytes_ && !std::strcmp(name,reinterpret_cast<const char*>(strtab_+s.name))) return reinterpret_cast<void*>(s.value);
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
