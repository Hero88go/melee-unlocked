// The source-port host, first stage: loads the game library built from source, gives it the console's
// memory and a disc, and runs it. No window or sound yet; its job is to show how far the native game
// gets and where it stops (a crash is reported by function, a stall by where the game thread sits).
// The full host (renderer, audio, input, sub-frame) replaces the stand-ins here piece by piece.
// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "mu_host.h"

namespace {

constexpr uintptr_t MEM1_BASE = 0x80000000u;
constexpr uint32_t MEM1_SIZE = 64u << 20;          // the console's 24 MB, grown for 8-byte pointers
constexpr uintptr_t LOCKED_CACHE_BASE = 0xE0000000u;
constexpr uint32_t LOCKED_CACHE_SIZE = 16u << 10;
constexpr uintptr_t GAME_IMAGE_BASE = 0x50000000u;
constexpr uint32_t ARAM_SIZE = 16u << 20;

FILE* g_log = nullptr;
FILE* g_disc = nullptr;
std::vector<uint8_t> g_aram(ARAM_SIZE);
MuGameApi g_game{};
HMODULE g_module = nullptr;
DWORD g_game_thread_id = 0;
HANDLE g_game_thread = nullptr;
std::atomic<uint64_t> g_polls{0};
std::atomic<uint32_t> g_retraces{0};
double g_seconds_limit = 20.0;
std::chrono::steady_clock::time_point g_start;
uint64_t g_next_retrace_tick = 0;
uint64_t g_fifo_bytes = 0;
uint32_t g_disc_reads = 0;

void logf(const char* fmt, ...) {
  char line[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  std::fputs(line, stdout);
  if (g_log) { std::fputs(line, g_log); std::fflush(g_log); }
}

// ---- disc: the ISO and its filesystem table ----
struct FstFile { uint32_t offset, length; bool dir; };
std::vector<FstFile> g_fst;
std::unordered_map<std::string, int32_t> g_paths;   // lower-case "/dir/name" -> entry number

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool read_disc(uint32_t offset, void* dst, uint32_t size) {
  if (_fseeki64(g_disc, offset, SEEK_SET) != 0) return false;
  return std::fread(dst, 1, size, g_disc) == size;
}

bool open_disc(const char* path) {
  g_disc = std::fopen(path, "rb");
  if (!g_disc) return false;
  uint8_t header[0x440];
  if (!read_disc(0, header, sizeof header)) return false;
  const uint32_t fst_offset = be32(header + 0x424), fst_size = be32(header + 0x428);
  std::vector<uint8_t> fst(fst_size);
  if (!read_disc(fst_offset, fst.data(), fst_size)) return false;
  const uint32_t count = be32(fst.data() + 8);
  const char* names = (const char*)fst.data() + count * 12;
  g_fst.resize(count);
  // Walk the table keeping the directory stack, to build each entry's full path.
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const uint8_t* e = fst.data() + i * 12;
    const bool dir = e[0] != 0;
    std::string name = names + (be32(e) & 0xFFFFFFu);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    const std::string full = dirs.back().second + "/" + name;
    g_fst[i] = {be32(e + 4), be32(e + 8), dir};
    g_paths[full] = (int32_t)i;
    if (dir) dirs.push_back({be32(e + 8), full});
  }
  // The boot loader left the disc header at the start of MEM1, and the OS globals after it. The
  // native game reads those words as ordinary integers, so they are stored in host byte order.
  std::memcpy((void*)MEM1_BASE, header, 0x20);
  auto low = [](uint32_t offset, uint32_t value) { std::memcpy((void*)(MEM1_BASE + offset), &value, 4); };
  low(0x28, 24u << 20);      // physical memory size
  low(0xCC, 0);              // TV mode: NTSC
  low(0xF0, 24u << 20);      // simulated memory size
  low(0xF8, 162000000u);     // bus clock
  low(0xFC, 486000000u);     // core clock
  logf("disc: %u entries\n", count);
  return true;
}

// ---- time ----
uint64_t ticks_now() {
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  return (uint64_t)(s * (double)MU_TB_HZ);
}

// ---- the table the game calls ----
void h_log(const char* text) { logf("[game] %s%s", text, (*text && text[std::strlen(text) - 1] == '\n') ? "" : "\n"); }
void h_panic(const char* file, int32_t line, const char* message) {
  logf("PANIC %s:%d %s\n", file ? file : "?", line, message ? message : "");
  std::fflush(nullptr);
  ExitProcess(3);
}
uint64_t h_ticks() { return ticks_now(); }
uint64_t h_boot_time() { return 0; }

void h_poll() {
  ++g_polls;
  const uint64_t now = ticks_now();
  if (now >= g_next_retrace_tick) {
    g_next_retrace_tick = now + MU_TB_HZ / 60;
    ++g_retraces;
    g_game.retrace();
    if (g_retraces % 60 == 0)
      logf("retrace %u: %llu polls, %llu FIFO bytes, %u disc reads\n", g_retraces.load(), (unsigned long long)g_polls.load(),
           (unsigned long long)g_fifo_bytes, g_disc_reads);
  }
  g_game.fire_alarms(now);
  if (std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count() > g_seconds_limit) {
    logf("time limit reached at retrace %u\n", g_retraces.load());
    std::fflush(nullptr);
    ExitProcess(0);
  }
}

void h_gx_fifo(const uint8_t*, uint32_t size) { g_fifo_bytes += size; }
void h_vi_configure(uint32_t w, uint32_t h, uint32_t interlaced) { logf("VI: %ux%u%s\n", w, h, interlaced ? " interlaced" : ""); }
void h_vi_set_next_framebuffer(void*) {}
void h_vi_flush() {}
uint32_t h_vi_retrace_count() { return g_retraces; }
uint32_t h_vi_next_field() { return g_retraces & 1; }
void h_vi_set_black(int32_t) {}
void h_vi_wait_retrace() {
  const uint32_t start = g_retraces;
  while (g_retraces == start) { h_poll(); if (g_retraces == start) Sleep(1); }
}

void h_pad_read(MuPadStatus out[4]) {
  std::memset(out, 0, sizeof(MuPadStatus) * 4);
  for (int i = 1; i < 4; ++i) out[i].err = -1;   // PAD_ERR_NO_CONTROLLER
}
void h_pad_rumble(int32_t, int32_t) {}

int32_t h_disc_entrynum(const char* path) {
  std::string p = path ? path : "";
  std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  if (p.empty() || p[0] != '/') p = "/" + p;
  auto it = g_paths.find(p);
  if (it == g_paths.end()) { logf("disc: no entry for \"%s\"\n", path ? path : ""); return -1; }
  return it->second;
}
int32_t h_disc_file(int32_t entrynum, uint32_t* start, uint32_t* length) {
  if (entrynum <= 0 || entrynum >= (int32_t)g_fst.size() || g_fst[entrynum].dir) return 0;
  *start = g_fst[entrynum].offset;
  *length = g_fst[entrynum].length;
  return 1;
}
void h_disc_read(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user) {
  ++g_disc_reads;
  const bool ok = read_disc(offset, dst, size);
  if (!ok) logf("disc: read %08X+%X failed\n", offset, size);
  done(ok ? (int32_t)size : -1, user);
}
int32_t h_disc_status() { return 0; }
uint32_t h_disc_id(void* out, uint32_t size) { const uint32_t n = std::min<uint32_t>(size, 0x20); std::memcpy(out, (void*)MEM1_BASE, n); return n; }

// Memory card: none yet (CARD_RESULT_NOCARD).
int32_t h_card_probe(int32_t, int32_t*, int32_t*) { return -3; }
int32_t h_card_mount(int32_t) { return -3; }
int32_t h_card_unmount(int32_t) { return -3; }
int32_t h_card_open(int32_t, const char*, int32_t*, uint32_t*) { return -3; }
int32_t h_card_close(int32_t, int32_t) { return -3; }
int32_t h_card_create(int32_t, const char*, uint32_t, int32_t*) { return -3; }
int32_t h_card_delete(int32_t, const char*) { return -3; }
int32_t h_card_rename(int32_t, const char*, const char*) { return -3; }
int32_t h_card_read(int32_t, int32_t, void*, uint32_t, uint32_t) { return -3; }
int32_t h_card_write(int32_t, int32_t, const void*, uint32_t, uint32_t) { return -3; }
int32_t h_card_stat(int32_t, int32_t, void*, uint32_t) { return -3; }
int32_t h_card_set_stat(int32_t, int32_t, const void*, uint32_t) { return -3; }
int32_t h_card_free_blocks(int32_t, int32_t*, int32_t*) { return -3; }
int32_t h_card_format(int32_t) { return -3; }

// Audio: silent for now. The buffers are accepted and nothing is played.
void h_ai_init_dma(void*, uint32_t) {}
void h_ai_start_dma(int32_t) {}
void h_ai_set_sample_rate(uint32_t) {}
void h_ai_set_stream_volume(int32_t, int32_t) {}
void h_dsp_mail(uint32_t) {}
uint32_t h_dsp_mail_pending() { return 0; }
void* h_aram_base() { return g_aram.data(); }
uint32_t h_aram_size() { return ARAM_SIZE; }
void h_aram_dma(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length) {
  if ((uint64_t)aram_offset + length > ARAM_SIZE) { logf("ARAM DMA out of range %08X+%X\n", aram_offset, length); return; }
  if (to_aram) std::memcpy(g_aram.data() + aram_offset, mainmem, length);
  else std::memcpy(mainmem, g_aram.data() + aram_offset, length);
}

uint32_t h_mem1_size() { return MEM1_SIZE; }
int32_t h_sound_mode() { return 1; }
void h_set_sound_mode(int32_t) {}
int32_t h_progressive_mode() { return 0; }
void h_set_progressive_mode(int32_t) {}
int32_t h_reset_code() { return 0; }
int32_t h_reset_switch() { return 0; }
void h_stop(int32_t reason, int32_t code) {
  logf("game stopped: reason %d code %d\n", reason, code);
  std::fflush(nullptr);
  ExitProcess(0);
}

MuHostApi make_host() {
  MuHostApi h{};
  h.version = MU_HOST_API_VERSION;
  h.log = h_log; h.panic = h_panic; h.ticks = h_ticks; h.boot_time = h_boot_time; h.poll = h_poll;
  h.gx_fifo = h_gx_fifo; h.vi_configure = h_vi_configure; h.vi_set_next_framebuffer = h_vi_set_next_framebuffer;
  h.vi_flush = h_vi_flush; h.vi_retrace_count = h_vi_retrace_count; h.vi_next_field = h_vi_next_field;
  h.vi_set_black = h_vi_set_black; h.vi_wait_retrace = h_vi_wait_retrace;
  h.pad_read = h_pad_read; h.pad_rumble = h_pad_rumble;
  h.disc_entrynum = h_disc_entrynum; h.disc_file = h_disc_file; h.disc_read = h_disc_read;
  h.disc_status = h_disc_status; h.disc_id = h_disc_id;
  h.card_probe = h_card_probe; h.card_mount = h_card_mount; h.card_unmount = h_card_unmount; h.card_open = h_card_open;
  h.card_close = h_card_close; h.card_create = h_card_create; h.card_delete = h_card_delete; h.card_rename = h_card_rename;
  h.card_read = h_card_read; h.card_write = h_card_write; h.card_stat = h_card_stat; h.card_set_stat = h_card_set_stat;
  h.card_free_blocks = h_card_free_blocks; h.card_format = h_card_format;
  h.ai_init_dma = h_ai_init_dma; h.ai_start_dma = h_ai_start_dma; h.ai_set_sample_rate = h_ai_set_sample_rate;
  h.ai_set_stream_volume = h_ai_set_stream_volume; h.dsp_mail = h_dsp_mail; h.dsp_mail_pending = h_dsp_mail_pending;
  h.aram_base = h_aram_base; h.aram_size = h_aram_size; h.aram_dma = h_aram_dma;
  h.mem1_size = h_mem1_size; h.sound_mode = h_sound_mode; h.set_sound_mode = h_set_sound_mode;
  h.progressive_mode = h_progressive_mode; h.set_progressive_mode = h_set_progressive_mode;
  h.reset_code = h_reset_code; h.reset_switch = h_reset_switch; h.stop = h_stop;
  return h;
}

// ---- diagnostics: where the game is, as an offset into its image (resolve with addr2line) ----
void describe_address(const char* what, uint64_t rip) {
  if (rip >= GAME_IMAGE_BASE && rip < GAME_IMAGE_BASE + 0x10000000u)
    logf("%s: game+0x%llX (%llX)\n", what, (unsigned long long)(rip - GAME_IMAGE_BASE), (unsigned long long)rip);
  else
    logf("%s: %llX (outside the game image)\n", what, (unsigned long long)rip);
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code == DBG_PRINTEXCEPTION_C || code == 0x406D1388u /* thread name */ || code < 0x80000000u) return EXCEPTION_CONTINUE_SEARCH;
  logf("exception %08lX", code);
  if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
    logf(" (%s %llX)", info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
         (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
  logf(" at retrace %u\n", g_retraces.load());
  describe_address("  rip", info->ContextRecord->Rip);
  // A few return addresses from the stack, the ones inside the game image.
  const uint64_t* sp = (const uint64_t*)info->ContextRecord->Rsp;
  int shown = 0;
  for (int i = 0; i < 512 && shown < 16; ++i) {
    uint64_t v = 0;
    __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
    if (v >= GAME_IMAGE_BASE + 0x1000 && v < GAME_IMAGE_BASE + 0x400000) { describe_address("  stack", v); ++shown; }
  }
  std::fflush(nullptr);
  ExitProcess(4);
}

void watchdog() {
  uint64_t last = 0;
  int still = 0;
  for (;;) {
    Sleep(1000);
    const uint64_t polls = g_polls.load();
    still = polls == last ? still + 1 : 0;
    last = polls;
    if (still >= 3) {
      SuspendThread(g_game_thread);
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(g_game_thread, &ctx)) {
        logf("stall: no poll for %d s at retrace %u\n", still, g_retraces.load());
        describe_address("  rip", ctx.Rip);
      }
      ResumeThread(g_game_thread);
      if (still >= 6) { logf("giving up\n"); std::fflush(nullptr); ExitProcess(5); }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string iso, dll = "melee_game.dll";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--iso") iso = next();
    else if (a == "--game") dll = next();
    else if (a == "--seconds") g_seconds_limit = std::atof(next());
  }
  g_log = std::fopen("melee_source.log", "w");
  g_start = std::chrono::steady_clock::now();

  // The console's memory, where the game's 32-bit disc pointers expect it. First, before anything
  // else in the process can take the range.
  void* mem1 = VirtualAlloc((void*)MEM1_BASE, MEM1_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* lc = VirtualAlloc((void*)LOCKED_CACHE_BASE, LOCKED_CACHE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (mem1 != (void*)MEM1_BASE || lc != (void*)LOCKED_CACHE_BASE) {
    logf("could not map MEM1 at %llX (%p) or the locked cache at %llX (%p)\n", (unsigned long long)MEM1_BASE, mem1,
         (unsigned long long)LOCKED_CACHE_BASE, lc);
    return 1;
  }
  if (iso.empty() || !open_disc(iso.c_str())) { logf("cannot open disc \"%s\" (--iso)\n", iso.c_str()); return 1; }

  g_module = LoadLibraryA(dll.c_str());
  if (!g_module) { logf("cannot load %s (error %lu)\n", dll.c_str(), GetLastError()); return 1; }
  if ((uintptr_t)g_module != GAME_IMAGE_BASE) { logf("%s loaded at %p, not %llX\n", dll.c_str(), (void*)g_module, (unsigned long long)GAME_IMAGE_BASE); return 1; }
  auto entry = (MuGameEntry)GetProcAddress(g_module, "mu_game_entry");
  if (!entry) { logf("%s has no mu_game_entry\n", dll.c_str()); return 1; }
  static MuHostApi host = make_host();
  if (entry(&host, &g_game) != 0) { logf("game refused host API version %u\n", host.version); return 1; }

  AddVectoredExceptionHandler(1, on_exception);
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
  g_game_thread_id = GetCurrentThreadId();
  std::thread(watchdog).detach();

  logf("running the game\n");
  const int32_t code = g_game.run();
  logf("game returned %d\n", code);
  return code;
}
