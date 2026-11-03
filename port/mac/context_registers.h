// Register access for the POSIX crash handlers (both hosts are x86-64).
#pragma once
#include <cstdint>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
inline uintptr_t mac_or_linux_rip(const ucontext_t* uc) {
#ifdef __APPLE__
  return uc->uc_mcontext->__ss.__rip;
#else
  return uc->uc_mcontext.gregs[REG_RIP];
#endif
}
