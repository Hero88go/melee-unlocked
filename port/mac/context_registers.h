// Register access for the POSIX crash handlers: x86-64 (Linux, and the Mac under Rosetta) and the
// native Apple silicon Mac.
#pragma once
#include <cstdint>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
// The faulting instruction.
inline uintptr_t mac_or_linux_rip(const ucontext_t* uc) {
#if defined(__APPLE__) && defined(__aarch64__)
  return uc->uc_mcontext->__ss.__pc;
#elif defined(__APPLE__)
  return uc->uc_mcontext->__ss.__rip;
#else
  return uc->uc_mcontext.gregs[REG_RIP];
#endif
}
// The stack pointer at the fault.
inline uintptr_t mac_or_linux_sp(const ucontext_t* uc) {
#if defined(__APPLE__) && defined(__aarch64__)
  return uc->uc_mcontext->__ss.__sp;
#elif defined(__APPLE__)
  return uc->uc_mcontext->__ss.__rsp;
#else
  return uc->uc_mcontext.gregs[REG_RSP];
#endif
}
