// Native Melee port entry point.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include "host.h"
#include "gecko_data.h"
#include "slippi_playback.h"
#include "render_observer.h"
#include "exi_slippi.h"
#include "slippi_online.h"
#include "slippi_net.h"
#include "jukebox.h"
#include "audio.h"
#include "functions.h"
#include "guest_symbols.h"
#include "gx_backend.h"
#include "gx_core.h"
#include "gx_d3d12.h"
#include "pc_settings.h"
#include "texture_pack.h"
#include "threaded_backend.h"
#include "window.h"
#include "lcancel.h"
#include "user_gecko.h"
#include "updater.h"
#include "discord_presence.h"
namespace app { int run_settings_window(gx::D3D12Options& options); }
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ppc { void init_dispatch(); }
namespace guest {
struct NameEntry { uint32_t addr; const char* name; };
extern const NameEntry name_table[];
extern const size_t name_table_count;
}

static void usage() {
  std::printf("melee_port --iso <path> [--frames N] [--fast] [--headless] [--scale N|auto] [--window WxH] [--vsync]\n"
              "           [--aspect auto|73:60|4:3|16:9|stretch] [--widescreen|--true-widescreen]\n"
              "           [--fps N|monitor|unlocked] [--frame-mode extrapolate|interpolate|authored|off] [--threaded-renderer]\n"
              "           [--fullscreen] [--backend d3d12|d3d11] [--dlss off|dlaa|quality|balanced|performance|ultra] [--frame-times out.csv] [--music 0-100|--no-music] [--volume 0-100] [--audio-dump out.wav]\n"
              "           [--capture out.ppm --capture-frame N] [--trace-calls] [--quiet]\n");
}

// Windows hands out ~15.6 ms timer granularity by default, so every pacing sleep (the 60 Hz
// retrace, the presentation deadline, the audio device wait) overshoots by up to a frame. One
// millisecond is what games ask for, and it is what makes 60 Hz land on 60 Hz.
struct TimerResolution {
  bool raised = timeBeginPeriod(1) == TIMERR_NOERROR;
  ~TimerResolution() { if (raised) timeEndPeriod(1); }
};

#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif

// Crash report: a native crash used to close the console with nothing on screen. Any unhandled
// exception now leaves melee_port_crash.txt (exception, module offset, last guest functions), a
// minidump, the same lines in melee_port.log, and a dialog pointing at them when a player launched it.
static bool g_crash_dialog = true;

// --profile: samples where the simulation thread is ~1000 times a second and logs the hottest
// functions at exit. Translated game functions are named through the dispatch table, everything
// else through the executable's debug symbols. The sample buffer is reserved up front: the sampler
// must not allocate while the simulation thread (which may hold the heap lock) is suspended.
struct SimProfiler {
  HANDLE target = nullptr;
  std::atomic<bool> running{false};
  std::thread sampler;
  bool render_thread = false;   // --profile-render: after warm-up, sample the busiest other thread instead
  DWORD main_thread_id = 0;
  // CPU time (100 ns units) of every other thread of this process, by thread id.
  std::unordered_map<DWORD, uint64_t> thread_times() {
    std::unordered_map<DWORD, uint64_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == main_thread_id || te.th32ThreadID == GetCurrentThreadId()) continue;
      HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
      if (!h) continue;
      FILETIME c, e, k, u;
      if (GetThreadTimes(h, &c, &e, &k, &u))
        out[te.th32ThreadID] = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
      CloseHandle(h);
    }
    CloseHandle(snap);
    return out;
  }
  std::vector<uint64_t> samples;
  std::vector<uint64_t> returns;   // [rsp] at the sample: the caller when the sample is in a leaf system routine
  void start() {
    main_thread_id = GetCurrentThreadId();
    if (!render_thread) DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
    samples.reserve(1u << 22);
    returns.reserve(1u << 22);
    running = true;
    sampler = std::thread([this] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
      if (render_thread) {
        Sleep(8000);   // past boot and menus into the match
        auto before = thread_times();
        Sleep(1000);
        auto after = thread_times();
        DWORD busiest = 0; uint64_t most = 0;
        for (auto& kv : after) { auto b = before.find(kv.first); uint64_t d = kv.second - (b == before.end() ? 0 : b->second); if (d > most) { most = d; busiest = kv.first; } }
        if (busiest) target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, busiest);
        host::log("profile: sampling thread %lu (%.0f%% of a core over 1 s)", busiest, most / 1e5);
        if (!target) return;
      }
      while (running.load(std::memory_order_relaxed) && samples.size() < samples.capacity()) {
        if (SuspendThread(target) != (DWORD)-1) {
          CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_CONTROL;
          if (GetThreadContext(target, &ctx)) { samples.push_back(ctx.Rip); returns.push_back(*(uint64_t*)ctx.Rsp); }
          ResumeThread(target);
        }
        Sleep(1);
      }
    });
  }
  void report() {
    if (!running) return;
    running = false;
    if (sampler.joinable()) sampler.join();
    std::vector<std::pair<uintptr_t, uint32_t>> fns;
    fns.reserve(guest::fn_table_count);
    for (size_t i = 0; i < guest::fn_table_count; ++i) fns.push_back({(uintptr_t)guest::fn_table[i].fn, guest::fn_table[i].addr});
    std::sort(fns.begin(), fns.end());
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    std::unordered_map<std::string, uint64_t> hits, system_callers;
    const uintptr_t exe_base = (uintptr_t)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(exe_base + ((IMAGE_DOS_HEADER*)exe_base)->e_lfanew);
    const uintptr_t exe_end = exe_base + nt->OptionalHeader.SizeOfImage;
    std::unordered_map<uint64_t, std::string> host_names;
    auto host_name = [&](uint64_t addr) -> std::string {
      auto cached = host_names.find(addr);
      if (cached != host_names.end()) return cached->second;
      char buf[sizeof(SYMBOL_INFO) + 256]{}; SYMBOL_INFO* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255;
      DWORD64 disp = 0;
      std::string name = SymFromAddr(process, addr, &disp, sym) ? std::string(sym->Name) : "?";
      return host_names.emplace(addr, name).first->second;
    };
    uint64_t guest_hits = 0;
    for (size_t si = 0; si < samples.size(); ++si) {
      const uint64_t rip = samples[si];
      auto it = std::upper_bound(fns.begin(), fns.end(), std::make_pair((uintptr_t)rip, UINT32_MAX));
      if (it != fns.begin() && rip < fns.back().first + 0x10000) {
        --it;
        ++guest_hits;
        ++hits[std::string("game ") + host::symbol_name(it->second)];
        continue;
      }
      const bool in_exe = rip >= exe_base && rip < exe_end;
      ++hits[std::string(in_exe ? "host " : "system ") + host_name(rip)];
      // A system DLL has no symbols here, so its exported names are only nearest guesses; the
      // return address shows which of our functions called into it.
      if (!in_exe && returns[si] >= exe_base && returns[si] < exe_end) ++system_callers[host_name(returns[si])];
    }
    std::vector<std::pair<uint64_t, std::string>> top;
    for (auto& kv : hits) top.push_back({kv.second, kv.first});
    std::sort(top.rbegin(), top.rend());
    const double total = (double)std::max<size_t>(1, samples.size());
    host::log("profile: %zu samples of the %s thread, game code %.1f%%, runtime %.1f%%", samples.size(), render_thread ? "busiest non-simulation" : "simulation",
              100.0 * guest_hits / total, 100.0 * (samples.size() - guest_hits) / total);
    for (size_t i = 0; i < top.size() && i < 40; ++i) host::log("profile: %5.1f%%  %s", 100.0 * top[i].first / total, top[i].second.c_str());
    std::vector<std::pair<uint64_t, std::string>> callers;
    for (auto& kv : system_callers) callers.push_back({kv.second, kv.first});
    std::sort(callers.rbegin(), callers.rend());
    for (size_t i = 0; i < callers.size() && i < 15; ++i) host::log("profile: %5.1f%%  system call from %s", 100.0 * callers[i].first / total, callers[i].second.c_str());
  }
};
static SimProfiler g_profiler;
static bool g_profile = false;
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
  static volatile LONG entered = 0;
  if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* er = info->ExceptionRecord;
  HMODULE module = nullptr; char module_name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)er->ExceptionAddress, &module))
    GetModuleFileNameA(module, module_name, MAX_PATH);
  const uintptr_t offset = (uintptr_t)er->ExceptionAddress - (uintptr_t)module;
  char head[512];
  std::snprintf(head, sizeof head, "CRASH: exception %08lX at %p (%s+0x%llX), version %s", er->ExceptionCode, er->ExceptionAddress,
                std::strrchr(module_name, '\\') ? std::strrchr(module_name, '\\') + 1 : module_name, (unsigned long long)offset, MELEE_PORT_VERSION);
  host::log("%s", head);
  if (FILE* f = std::fopen("melee_port_crash.txt", "w")) {
    std::fprintf(f, "%s\n", head);
    if (host::cpu) {
      std::fprintf(f, "last guest function %08X %s, lr %08X\nrecent guest functions (oldest first):\n", host::cpu->last_pc, host::symbol_name(host::cpu->last_pc), host::cpu->lr);
      for (uint32_t i = 0; i < 64; ++i) { uint32_t pc = host::cpu->trace[(host::cpu->trace_pos + i) & 63]; if (pc) std::fprintf(f, "  %08X %s\n", pc, host::symbol_name(pc)); }
    }
    std::fclose(f);
  }
  HANDLE dump = CreateFileA("melee_port_crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, &mei, nullptr, nullptr);
    CloseHandle(dump);
  }
  if (g_crash_dialog) {
    std::string text = std::string(head) + "\n\nMelee Unlocked crashed. Please send melee_port.log, melee_port_crash.txt and melee_port_crash.dmp "
                       "from the game folder with your bug report (https://github.com/hero88go/melee-unlocked/issues).";
    MessageBoxA(nullptr, text.c_str(), "Melee Unlocked", MB_ICONERROR | MB_OK);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// Records the disc this run used, next to the launcher's own settings. However the game was
// started (a batch file, a shortcut, the launcher, a development command line), the launcher can
// then offer that disc instead of leaving Play greyed out with an empty box.
static void remember_iso(const std::string& iso) {
  char full[MAX_PATH];
  if (!GetFullPathNameA(iso.c_str(), MAX_PATH, full, nullptr)) return;
  char* local = nullptr; size_t n = 0;
  if (_dupenv_s(&local, &n, "LOCALAPPDATA") != 0 || !local) return;
  std::string dir = std::string(local) + "\\MeleeUnlocked";
  free(local);
  CreateDirectoryA(dir.c_str(), nullptr);
  FILE* f = std::fopen((dir + "\\launcher.ini").c_str(), "w");
  if (!f) return;
  std::fprintf(f, "iso=%s\n", full);
  std::fclose(f);
}

// The runtime and the translated game are built for AVX2. Without it the first AVX2 instruction
// kills the process before anything is logged, so say so plainly instead (this file is not AVX2).
static bool cpu_has_avx2() {
  int r[4];
  __cpuid(r, 0); if (r[0] < 7) return false;
  __cpuid(r, 1); const bool osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
  if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
  __cpuidex(r, 7, 0); return (r[1] >> 5) & 1;
}

// ...and the check in melee_main is too late to catch it. The runtime and guest libraries are built
// for AVX2, and their C++ static initialisers run before main does, so a CPU without AVX2 died
// during CRT startup: exit code 0xC000001D, no log file written, and the message below never shown.
// A player reported exactly that, and the empty folder they were asked to find the log in is what
// gave it away.
//
// The initialiser slots run in this order: XCC (compiler), XCL (library), XCU (user). Every static
// constructor in the runtime and guest libraries is XCU, so XCC is ahead of all of them and is
// still after the C runtime has set itself up.
//
// The first attempt used .CRT$XIB, which is a slot the CRT uses for its own early initialisation,
// and it ran before the CRT was ready. That is a plausible way to get a wrong answer out of a
// check that is correct everywhere else, which is why the message below prints what the processor
// actually reported rather than only the conclusion.
static int __cdecl check_avx2_before_anything_else() {
#ifndef MELEE_NEEDS_AVX2
  return 0;   // built for an older baseline: nothing here to require
#else
  if (cpu_has_avx2()) return 0;
  // Say which processor and which of the four conditions failed. A player who is told "your CPU is
  // too old" and believes otherwise has no way to settle it, and neither do we: this makes the
  // screenshot itself the answer, instead of a round of guessing about what machine it is.
  char brand[64] = "unknown";
  int r[4];
  __cpuid(r, 0x80000000);
  if ((unsigned)r[0] >= 0x80000004u) {
    for (int i = 0; i < 3; ++i) { __cpuid(r, 0x80000002 + i); std::memcpy(brand + i * 16, r, 16); }
    brand[48] = '\0';
  }
  __cpuid(r, 0);
  const int max_leaf = r[0];
  int leaf1[4] = {};
  if (max_leaf >= 1) __cpuid(leaf1, 1);
  const bool osxsave = (leaf1[2] >> 27) & 1, avx = (leaf1[2] >> 28) & 1;
  const unsigned long long xcr0 = osxsave ? _xgetbv(0) : 0;
  int leaf7[4] = {};
  if (max_leaf >= 7) __cpuidex(leaf7, 7, 0);
  const bool avx2 = max_leaf >= 7 && ((leaf7[1] >> 5) & 1);

  // wsprintfA rather than snprintf: this runs from a CRT initialiser slot, before the C runtime
  // has finished setting itself up, and wsprintfA lives in user32 with no such dependency. The
  // last thing this should do is fault inside the code explaining a fault.
  char msg[768];
  wsprintfA(msg,
                "Melee Unlocked needs a processor with AVX2, and this one reports that it does not "
                "have it.\n\nAVX2 means Intel Core 4th generation (Haswell, 2013) or newer, or AMD "
                "Ryzen or newer. Every version of Melee Unlocked has been built for it; older "
                "versions crashed here without a message instead of showing this one.\n\n"
                "Processor: %s\n"
                "AVX: %s   AVX2: %s   OS support: %s\n\n"
                "If you believe this is wrong, send this window to the developer: these four values "
                "say exactly what the processor reported.",
                brand, avx ? "yes" : "no", avx2 ? "yes" : "no",
                (osxsave && (xcr0 & 6) == 6) ? "yes" : "no");
  MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
  ExitProcess(3);
  return 0;
#endif
}
#pragma section(".CRT$XCC", long, read)
__declspec(allocate(".CRT$XCC")) static int (__cdecl* g_avx2_guard)() = check_avx2_before_anything_else;

// Built for the Windows subsystem so double-clicking the game does not open a terminal alongside it.
// Anything started from a command line still prints there: this reattaches to the parent console when
// one exists, so `melee_port.exe --help` and scripted runs behave exactly as before.
static void attach_parent_console() {
  // Never take over output that is already going somewhere. A script running `--version` hands us a
  // pipe, and reopening CONOUT$ over it sends the answer to the terminal instead of back to the
  // caller, which is how this first broke the release packaging.
  const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
  if (existing && existing != INVALID_HANDLE_VALUE && GetFileType(existing) != FILE_TYPE_UNKNOWN) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE* f = nullptr;
  freopen_s(&f, "CONOUT$", "w", stdout);
  freopen_s(&f, "CONOUT$", "w", stderr);
  freopen_s(&f, "CONIN$", "r", stdin);
}

static int melee_main(int argc, char** argv);

// Both entry points exist so the executable links whichever subsystem it is built for: WinMain for the
// windowed build (no terminal alongside the game), main if it is ever built as a console program.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  attach_parent_console();
  return melee_main(__argc, __argv);
}
int main(int argc, char** argv) { return melee_main(argc, argv); }

static int melee_main(int argc, char** argv) {
  if (!cpu_has_avx2()) {
    const char* msg = "Melee Unlocked needs a CPU with AVX2 (Intel Haswell 2013 or newer, AMD Ryzen or newer). This CPU does not support it.";
    std::fprintf(stderr, "%s\n", msg);
    MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
    return 3;
  }
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--version") { std::printf("%s\n", MELEE_PORT_VERSION); return 0; }
  TimerResolution timer_resolution;
  host::Options& o = host::options;
  bool headless = false, hidden = false, threaded = false, fps_requested = false;
  gx::D3D12Options gfx;
  bool automated = false, explicit_frame_mode = false, settings_window_only = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--hidden" || arg == "--headless") automated = true;
    if (arg == "--settings-path" && i+1 < argc) gfx.settings_path = argv[++i];
    if (arg == "--frame-mode") explicit_frame_mode = true;
    if (arg == "--settings-window") settings_window_only = true;
  }
  gfx.pc_settings = !automated;
  g_crash_dialog = !automated;
  o.no_gc_adapter = automated;   // a hidden test run must not take the adapter from a game the player is running
  if (std::getenv("MELEE_NO_GC_ADAPTER")) o.no_gc_adapter = true;   // same, for a visible test window
  SetUnhandledExceptionFilter(crash_filter);
  if (!automated) {
    // Interpolate by default: it never overshoots a stop, so menus, cursors and stage geometry stay
    // on one timeline. Predict avoids its one tick of delay but can overshoot and snap back.
    // Set before the settings file is read, so a saved "subframe" (Off in the Low spec preset) wins
    // over this default and an explicit --frame-mode, parsed below, still wins over both.
    if (!explicit_frame_mode) gfx.subframe = gx::SubFrameMode::Authored;   // Predict (the default since 0.5.5)
    // A person launching the game wants to hear it. The zero default is there for automated runs,
    // which never reach this branch, and it used to be hidden by the launcher passing --volume 70 on
    // every start; that override was removed because it also overwrote the player's saved settings,
    // which left anyone without a saved volume silent. Set before the file is read, so a saved
    // volume still wins, and an explicit --volume below wins over both.
    o.volume = 70;
    // Unlocked is the whole point of the port, so a fresh install must not present at 60. The
    // struct default is 60 for automated runs, which never reach this branch. The launcher used to
    // pass --fps unlocked on every start, which also overrode what the player had saved (0.2.1 and
    // 0.2.2), so it was removed from the launcher; without a default here a new player got a 60 Hz
    // build with sub-frame animation on and no way to tell why it felt wrong. Set before the
    // settings file is read, so a saved cap (60 in the Low spec preset) still wins, and an explicit
    // --fps below wins over both.
    //
    // Monitor rate, not uncapped. 0.3.0 defaulted this to uncapped, which renders as fast as the
    // hardware can and does it hardest on the menus, where there is almost nothing to draw: a
    // player reported the fans winding up to a jet engine a minute after reaching the main menu and
    // settling once a match loaded. Frames beyond the refresh rate are never shown, so all of that
    // heat and power bought nothing, and free-running also paces worse than following the display.
    // Following the monitor is still unlocked in the sense that matters: a 144 Hz display gets 144.
    gfx.fps_cap = -1;   // -1 = follow the monitor, 0 = uncapped
    gx::load_pc_settings(gfx, o.volume);
    // Opt-in, and only ever from a saved setting: an automated or headless run never gets here, so
    // it can never publish. With the setting off no thread is started and no pipe is opened.
    if (gfx.discord_presence) { host::discord::configure(gfx.discord_app_id); host::discord::enable(true); }
    threaded = true;
  }
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
    if (a == "--iso") o.iso = next();
    else if (a == "--state-trace") o.state_trace = next();
    else if (a == "--frames") o.frames = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--fast") o.fast = true;
    else if (a == "--headless") headless = true;
    else if (a == "--hidden") hidden = true;
    else if (a == "--threaded-renderer") threaded = true;
    else if (a == "--fps") {   // display rate: N or "unlocked"; enables the render thread
      std::string v = next(); gfx.fps_cap = v == "unlocked" ? 0 : v == "monitor" ? -1 : std::atoi(v.c_str());
      if (v != "unlocked" && v != "monitor" && (gfx.fps_cap < 1 || v.find_first_not_of("0123456789") != std::string::npos)) { usage(); return 2; }
      threaded = true;
      fps_requested = true;
    }
    else if (a == "--frame-mode") {
      std::string v = next();
      if (v == "extrapolate") gfx.subframe = gx::SubFrameMode::Extrapolate;
      else if (v == "interpolate") gfx.subframe = gx::SubFrameMode::Interpolate;
      else if (v == "authored") gfx.subframe = gx::SubFrameMode::Authored;
      else if (v == "authored-interpolate") gfx.subframe = gx::SubFrameMode::AuthoredInterpolate;
      else if (v == "off") gfx.subframe = gx::SubFrameMode::Off;
      else { usage(); return 2; }
      threaded = true;
    }
    else if (a == "--scale") { std::string v = next(); gfx.efb_scale = v == "auto" ? 0 : std::atoi(v.c_str()); if (v != "auto" && gfx.efb_scale < 1) { usage(); return 2; } }
    else if (a == "--window") { if (std::sscanf(next(), "%dx%d", &gfx.window_w, &gfx.window_h) != 2 || gfx.window_w < 320 || gfx.window_h < 240) { usage(); return 2; } gfx.window_pinned = true; }
    // Presentation only, so it cannot desync and the two players in a match may differ.
    else if (a == "--aspect") { std::string v = next();
      gfx.aspect = v == "auto" ? gx::AspectMode::Auto : v == "73:60" || v == "native" ? gx::AspectMode::Native
                 : v == "4:3" ? gx::AspectMode::Force4_3 : v == "16:9" ? gx::AspectMode::Force16_9
                 : v == "stretch" ? gx::AspectMode::Stretch : (gx::AspectMode)-1;
      if ((int)gfx.aspect < 0) { std::fprintf(stderr, "--aspect auto|73:60|4:3|16:9|stretch\n"); return 2; } }
    else if (a == "--settings-path") gfx.settings_path = next();
    else if (a == "--pc-settings-open") { gfx.pc_settings = true; gfx.settings_open = true; }
    // The overlay layer without the panel. An automated run turns the UI off entirely, which also
    // takes the on-screen overlays with it; this brings them back without capturing the pad, so a
    // scripted run can screenshot an overlay.
    else if (a == "--pc-settings") gfx.pc_settings = true;
    else if (a == "--fullscreen") gfx.fullscreen = true;
    else if (a == "--backend") { std::string v = next();
      if (v == "d3d11" || v == "dx11" || v == "11") gfx.api = gx::RenderApi::D3D11;
      else if (v == "d3d12" || v == "dx12" || v == "12") gfx.api = gx::RenderApi::D3D12;
      else { std::fprintf(stderr, "--backend d3d12|d3d11\n"); return 2; } }
#ifdef GX_DLSS5
    else if (a == "--dlss5") gfx.dlss5 = true;                  // EXPERIMENTAL (gx_dlss5.h); needs --dlss
#endif
    else if (a == "--dlss") { std::string v = next(); gfx.dlss_mode = v == "off" ? 0 : v == "dlaa" ? 1 : v == "quality" ? 2 : v == "balanced" ? 3 : v == "performance" ? 4 : v == "ultra" ? 5 : v == "xess-aa" ? 6 : v == "xess-ultra" ? 7 : v == "xess-quality" ? 8 : v == "xess-balanced" ? 9 : v == "xess-performance" ? 10 : -1;
      if (gfx.dlss_mode < 0) { std::fprintf(stderr, "--dlss off|dlaa|quality|balanced|performance|ultra\n"); return 2; } }
    else if (a == "--frame-generation") gfx.frame_generation_mode = 1;   // 2x
    else if (a == "--reflex") gfx.reflex_mode = 2;
    else if (a == "--dlss-jitter-sign") gfx.dlss_jitter_sign = (float)std::atof(next());
    else if (a == "--frame-times") gfx.frame_times = next();
    else if (a == "--vsync") gfx.vsync = true;
    else if (a == "--flicker-scan") gfx.flicker_scan = true;
    else if (a == "--pin-phase") gfx.pin_phase = std::atof(next());
    else if (a == "--capture") gfx.capture_path = next();
    else if (a == "--capture-frame") gfx.capture_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-every") gfx.capture_every = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-burst") gfx.capture_burst = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-sim-frame") gfx.capture_sim_frame = std::strtoull(next(), nullptr, 0);
    else if (a == "--script") { if (!host::input_load_script(next())) { std::fprintf(stderr, "cannot load input script\n"); return 1; } }
    else if (a == "--dump") gfx.dump_path = next();
    else if (a == "--shader-cache") gfx.shader_cache = next();
    else if (a == "--trace-func") { const char* spec = next(); uint32_t addr = (uint32_t)std::strtoul(spec, nullptr, 16); uint32_t limit = 40;
      if (const char* colon = std::strchr(spec, ':')) limit = (uint32_t)std::strtoul(colon + 1, nullptr, 10);
      if (!addr) { std::string name(spec, std::strchr(spec, ':') ? std::strchr(spec, ':') - spec : std::strlen(spec));
        for (size_t i = 0; i < guest::name_table_count; ++i) if (name == guest::name_table[i].name) { addr = guest::name_table[i].addr; break; } }
      if (!addr) { std::fprintf(stderr, "unknown function %s\n", spec); return 2; }
      ppc::add_trace_func(addr, limit); }
    else if (a == "--sys-dir") o.sys_dir = next();
    else if (a == "--replay-dir") o.replay_dir = next();
    else if (a == "--card-dir") o.card_dir = next();
    else if (a == "--log-file") o.log_file = next();
    else if (a == "--replay") slippi::playback::set_replay(next());   // playback build: play this .slp
    else if (a == "--user-dir") slippi::online::config().user_dir = next();
    else if (a == "--online-delay") slippi::online::config().delay = std::atoi(next());
    else if (a == "--chat") { std::string v = next(); slippi::online::config().chat = v == "off" ? 2 : v == "direct" ? 1 : 0; }
    else if (a == "--netplay-port") slippi::Matchmaking::forced_port = (uint16_t)std::atoi(next());
    else if (a == "--local-peer") {
      // idx:local_port:remote_ip:remote_port; two instances peer directly without the matchmaking server.
      std::string v = next(); auto& lp = slippi::Matchmaking::local_peer;
      size_t a1 = v.find(':'), a2 = v.find(':', a1 + 1), a3 = v.find(':', a2 + 1);
      if (a1 == std::string::npos || a2 == std::string::npos || a3 == std::string::npos) { std::fprintf(stderr, "--local-peer idx:port:ip:port"); return 2; }
      lp.enabled = true; lp.local_index = std::atoi(v.substr(0, a1).c_str()); lp.local_port = (uint16_t)std::atoi(v.substr(a1 + 1, a2 - a1 - 1).c_str());
      lp.remote_ip = v.substr(a2 + 1, a3 - a2 - 1); lp.remote_port = (uint16_t)std::atoi(v.substr(a3 + 1).c_str()); }
    else if (a == "--dump-frame") gfx.dump_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--trace-calls") o.trace_calls = true;
    else if (a == "--quiet") o.quiet = true;
    else if (a == "--time-base") o.time_base = std::strtoull(next(), nullptr, 0);
    else if (a == "--volume") o.volume = std::atoi(next());
    else if (a == "--music") slippi::jukebox::set_user_volume(std::clamp(std::atoi(next()), 0, 100));
    else if (a == "--no-music") slippi::jukebox::set_user_volume(0);
    else if (a == "--widescreen") { gfx.widescreen = true; gfx.true_widescreen = false; }
    else if (a == "--pal-stock-icons") gecko::option_pal_stock_icons = true;
    else if (a == "--no-screen-shake") gecko::option_no_screen_shake = true;
    else if (a == "--gecko-codes") user_gecko::load(next(), {}, false);   // scripted runs: that file's enabled codes
    // Experimental true 16:9: widens the frustum in the renderer, no game code. Mutually exclusive
    // with --widescreen, so whichever comes last on the command line wins rather than both applying.
    else if (a == "--true-widescreen") { gfx.true_widescreen = true; gfx.widescreen = false; }
    // Texture packs are a settings-panel feature; these exist so automated runs, which start with
    // the panel disabled, can exercise the same paths.
    else if (a == "--custom-textures") gfx.custom_textures = true;
    else if (a == "--dump-textures") gfx.dump_textures = true;
    else if (a == "--sharpness") gfx.sharpness = std::clamp((float)std::atof(next()), 0.0f, 1.0f);
    else if (a == "--ssaa") gfx.ssaa = std::atoi(next()) >= 2 ? 2 : 1;
    else if (a == "--anisotropy") gfx.anisotropy = std::clamp(std::atoi(next()), 1, 16);
    // Hidden runs never load the settings file, so the quality level needs a flag to be measurable.
    else if (a == "--effects") gfx.effects_level = std::clamp(std::atoi(next()), 0, 2);
    else if (a == "--hang-watch") o.hang_watch = std::atof(next());
    else if (a == "--audio-dump") o.audio_dump = next();
    else if (a == "--profile") g_profile = true;
    else if (a == "--input-log") o.input_log = next();
    // L-cancel helpers, both off unless asked for. --lcancel-log writes a per-frame CSV of the
    // local fighters' action state and trigger timer, which is how the landing lag is measured.
    else if (a == "--auto-lcancel") lcancel::set_automatic(true);
    else if (a == "--lcancel-indicator") lcancel::set_indicator(true);
    else if (a == "--lcancel-log") lcancel::set_log_path(next());
    else if (a == "--profile-render") { g_profile = true; g_profiler.render_thread = true; }
    // Recognised in the pre-scan above; listed here so it is not rejected as unknown.
    else if (a == "--settings-window") {}
    else { usage(); return 2; }
  }
  gecko::option_widescreen = gfx.widescreen;   // before the game loads the code table
  if (fps_requested && gfx.subframe == gx::SubFrameMode::Off) {
    std::fprintf(stderr, "--fps requires explicit experimental --frame-mode interpolate, extrapolate or authored\n");
    return 2;
  }
  // The settings panel with no game behind it: the launcher opens this instead of booting the
  // whole game to change a setting. Before the disc check, because it needs no disc.
  if (settings_window_only) {
    gfx.pc_settings = true;
    gx::load_pc_settings(gfx, o.volume);
    const int rc = app::run_settings_window(gfx);
    // The panel polls every controller so its live readouts work, which starts the adapter and
    // Switch Pro threads, and it can start an update check. Returning straight from here left those
    // threads running into static destruction, where a joinable std::thread ends the process: every
    // close of this window was a crash (0xC0000409), and with Windows Error Reporting collecting it,
    // the window took a long time to go away. Same shutdown as the game's, below.
    host::updater::shutdown();
    host::discord::shutdown();
    host::gcadapter_shutdown();
    host::switchpro_shutdown();
    slippi::shutdown();
    return rc;
  }
  if (o.iso.empty()) { usage(); return 2; }
  if (!host::disc_open(o.iso)) { std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str()); return 1; }
  remember_iso(o.iso);   // so the launcher can offer this disc without being told again
  // Controllers do not count as activity to Windows, so a session played only on a pad let the
  // display power off after the idle timeout (monitors going black mid-game until the mouse moved).
  // Held by this thread for as long as the game runs; Windows drops it when the process exits.
  if (!hidden) SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

  std::unique_ptr<gx::Backend> backend;
  if (!headless && threaded) {
    backend = gx::create_threaded_backend(gfx, !hidden);
  } else if (!headless) {
    void* hwnd = host::window_create(gfx.window_w, gfx.window_h, L"Melee Unlocked (development)", !hidden);
    if (gfx.fullscreen && !gfx.exclusive_fullscreen) host::window_set_fullscreen(true);
    backend.reset(gx::create_render_backend(hwnd, gfx.window_w, gfx.window_h, gfx));
    host::window_set_resize_callback([renderer = backend.get()](int w, int h) { gx::render_resize(renderer, w, h); });
    host::g_has_window = true;
  }
  // Texture packs: scan before the game starts so the settings list is right, and decode up front
  // when the player asked for that, with the wait shown in the title bar rather than as a silent
  // half minute. Decoding runs on its own thread, so the game keeps booting while it works.
  gx::texpack::configure(gfx.custom_textures, gfx.dump_textures);
  gx::texpack::refresh_packs();
  if (gfx.custom_textures && gfx.prefetch_textures) {
    gx::texpack::prefetch_begin();
    while (gx::texpack::prefetching()) {
      uint64_t done = 0, total = 0;
      gx::texpack::prefetch_progress(&done, &total);
      wchar_t title[128];
      swprintf_s(title, L"Melee Unlocked  |  loading textures %llu / %llu",
                 (unsigned long long)done, (unsigned long long)total);
      host::window_set_title(title);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    host::window_set_title(L"Melee Unlocked");
  }
  gx::set_authored_capture(gfx.subframe == gx::SubFrameMode::Authored || gfx.subframe == gx::SubFrameMode::AuthoredInterpolate);
  gx::init(backend.get());
  host::audio_open(o.volume, o.audio_dump.c_str(), !headless);

  ppc::init_dispatch();
  host::boot_setup();
  host::log("boot: entering __start at %08X", 0x8000522Cu);
  int code = 0;
  if (g_profile) g_profiler.start();
  try {
    ppc::call(*host::cpu, host::ram, 0x8000522Cu);
    host::log("guest returned from __start after %u retraces", host::retrace_count());
  } catch (const ExitRequested& stop) {
    backend.reset();
    code = stop.code;
  } catch (const LoadContextUnwind&) {
    host::log("OSLoadContext reached top level");
  }
  g_profiler.report();
  { uint64_t silent_ms = 0, underruns = host::audio_underruns(&silent_ms);
    double rate_low = 1.0, rate_high = 1.0; host::audio_rate_range(&rate_low, &rate_high);
    host::log("audio: %llu frames played, %llu blocks dropped, %llu gaps (%llu ms held), clock tracking %+.3f%% to %+.3f%%",
              (unsigned long long)host::audio_pushed_frames(), (unsigned long long)host::audio_dropped_blocks(),
              (unsigned long long)underruns, (unsigned long long)silent_ms, (rate_low - 1.0) * 100.0, (rate_high - 1.0) * 100.0); }
  host::audio_close();
  host::updater::shutdown();   // the settings panel may have started an update check; join it before exit
  host::discord::shutdown();   // clears the presence and joins its thread; a no-op when never enabled
  host::gcadapter_shutdown();
  host::switchpro_shutdown();   // joins the init thread and hands any Switch pad back to the system
  slippi::shutdown();
  { uint64_t calls = 0, insns = 0; ppc::interpreter_stats(&calls, &insns);
    if (calls) host::log("interpreter: %llu calls into RAM-resident code, %llu instructions", (unsigned long long)calls, (unsigned long long)insns); }
  if (ppc::g_resumed_returns)
    host::log("gecko: %llu code-cave returns resumed past the call (UCF Shield Drop and the like)", (unsigned long long)ppc::g_resumed_returns);
  host::log("slippi: %llu EXI commands, %llu replays written, GCT at %08X", (unsigned long long)slippi::commands_seen(),
            (unsigned long long)slippi::replays_written(), slippi::gct_load_address());
  return code;
}
