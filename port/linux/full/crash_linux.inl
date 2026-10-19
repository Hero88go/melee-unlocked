// Linux build of main.cpp's crash reporting: the same melee_port_crash.txt the launcher sends (the
// fault, its module and offset, the host's call stack, the recent guest functions) and the same
// dialog, from signal handlers instead of Windows' exception filters. Included from main.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <ucontext.h>
#include <unistd.h>

// --profile is a Windows sampling profiler (it suspends the simulation thread and reads its
// registers). On Linux, use perf: `perf record -g ./melee_source ...` gives the same picture.
struct SimProfiler {
  bool render_thread = false;
  void start() { host::log("profile: --profile samples on Windows only; on Linux run the game under perf record -g"); }
  void report() {}
};
static SimProfiler g_profiler;
static bool g_profile = false;

// One "stack N: module+0xOFFSET" line per frame, as Windows writes them.
static void linux_stack_lines(void* const* frames, int count, FILE* f, int skip) {
  for (int i = skip; i < count; ++i) {
    Dl_info info{};
    const char* leaf = "?";
    uintptr_t offset = (uintptr_t)frames[i];
    if (dladdr(frames[i], &info) && info.dli_fname) {
      leaf = std::strrchr(info.dli_fname, '/') ? std::strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
      offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_fbase;
    }
    char line[400];
    std::snprintf(line, sizeof line, "stack %2d: %s+0x%llX", i - skip, leaf, (unsigned long long)offset);
    host::log("%s", line);
    if (f) std::fprintf(f, "%s\n", line);
  }
}

static void linux_guest_trace(FILE* f) {
  if (!host::cpu || !f) return;
  std::fprintf(f, "last guest function %08X %s, lr %08X\nrecent guest functions (oldest first):\n", host::cpu->last_pc,
               host::symbol_name(host::cpu->last_pc), host::cpu->lr);
  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t pc = host::cpu->trace[(host::cpu->trace_pos + i) & 63];
    if (pc) std::fprintf(f, "  %08X %s\n", pc, host::symbol_name(pc));
  }
}

static void linux_crash_dialog(const char* head, bool stopped) {
  if (!g_crash_dialog) return;
  std::string text = std::string(head) + (stopped
      ? "\n\nMelee Unlocked stopped with an error. The launcher can send this report: it asks once you close this message. "
      : "\n\nMelee Unlocked crashed. The launcher can send this report: it asks once you close this message. ") +
      "If you started the game without the launcher, please send melee_port.log and melee_port_crash.txt from the game folder "
      "with your bug report (https://github.com/hero88go/melee-unlocked/issues).";
  MessageBoxA(nullptr, text.c_str(), "Melee Unlocked", MB_ICONERROR | MB_OK);
}

static volatile sig_atomic_t g_linux_crash_entered = 0;
static void linux_crash_signal(int sig, siginfo_t* si, void* uc_void) {
  if (g_linux_crash_entered) { signal(sig, SIG_DFL); raise(sig); return; }
  g_linux_crash_entered = 1;
  const ucontext_t* uc = static_cast<const ucontext_t*>(uc_void);
  const uintptr_t rip = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RIP] : 0;
  Dl_info info{};
  const char* module = "?";
  uintptr_t offset = rip;
  if (rip && dladdr((void*)rip, &info) && info.dli_fname) {
    module = std::strrchr(info.dli_fname, '/') ? std::strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
    offset = rip - (uintptr_t)info.dli_fbase;
  }
  char head[512];
  std::snprintf(head, sizeof head, "CRASH: signal %d (%s) at %p (%s+0x%llX), address %p, version %s", sig, strsignal(sig),
                (void*)rip, module, (unsigned long long)offset, si ? si->si_addr : nullptr, MELEE_PORT_VERSION);
  host::log("%s", head);
  if (FILE* f = std::fopen("melee_port_crash.txt", "w")) {
    std::fprintf(f, "%s\n", head);
    void* frames[48];
    const int count = backtrace(frames, 48);
    linux_stack_lines(frames, count, f, 0);
    host::log_flush();
    linux_guest_trace(f);
    std::fclose(f);
  }
  host::log_flush();
  linux_crash_dialog(head, false);
  signal(sig, SIG_DFL);
  raise(sig);
}

static void linux_install_crash_handlers() {
  // An alternate stack, so a stack overflow can still be reported.
  static std::vector<uint8_t> alt(1 << 16);
  stack_t ss{};
  ss.ss_sp = alt.data(); ss.ss_size = alt.size();
  sigaltstack(&ss, nullptr);
  struct sigaction sa{};
  sa.sa_sigaction = linux_crash_signal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) sigaction(sig, &sa, nullptr);
}

static void throw_stack_lines(FILE*) {}
static volatile LONG g_throw_frozen = 0;

// host::die: the same report without a signal.
static void die_report(const char* message) {
  char head[640];
  std::snprintf(head, sizeof head, "FATAL: %s, version %s", message, MELEE_PORT_VERSION);
  if (FILE* f = std::fopen("melee_port_crash.txt", "w")) {
    std::fprintf(f, "%s\n", head);
    void* frames[48];
    const int count = backtrace(frames, 48);
    linux_stack_lines(frames, count, f, 1);
    linux_guest_trace(f);
    std::fclose(f);
  }
  linux_crash_dialog(head, true);
}
