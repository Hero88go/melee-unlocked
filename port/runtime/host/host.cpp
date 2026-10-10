// Host services: memory, disc, boot, event delivery, time, MMIO, logging.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "audio.h"
#include "cosmetic_mods.h"
#include "mod_profile.h"
#include "user_gecko.h"
#include "memory_range.h"
#include "guest_heap_trace.h"
#include "disc_fst_paths.h"
#include <windows.h>
#include <bcrypt.h>
#include "guest_registry.h"
#include "guest_symbols.h"
#include "gx_core.h"
#include "gx_shader.h"
#include "authored_pose.h"
#include "window.h"
#include "ax_ucode.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "exi_slippi.h"
#include "jukebox.h"
#include "slippi_online.h"
#endif
#include "gecko_data.h"
#include "render_options.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <deque>
#include <thread>
#include <filesystem>
#include <fstream>

namespace hle { void audio_tick(bool force); uint64_t audio_next_due(); }

namespace hle { void dvd_poll(); void dvd_settle(); }
namespace host {

Options options;
uint8_t* ram = nullptr;
uint32_t ram_size = ppc::RAM_SIZE;
uint8_t* aram = nullptr;
ppc::Context* cpu = nullptr;

static FILE* g_disc = nullptr;
static FILE* g_state_trace = nullptr;
static FILE* g_state_digest = nullptr;
static uint32_t g_fst_offset, g_fst_size, g_fst_max;
static std::deque<Completion> g_completions;
static bool g_pe_finish_pending = false;
// Where boot put the disc's file table. The game's own lookups use its cached copy; the host's
// used to read the pointer back from low memory (80000038), which a stray guest store through a
// near-null pointer can overwrite (addresses are masked into RAM, so it lands there instead of
// faulting as on a console). The walk then ran off into garbage: "host access outside RAM".
static uint32_t g_fst_addr = 0;
static bool g_pe_token_pending = false;
static uint16_t g_pe_token = 0;
static uint32_t g_retraces = 0;
static std::atomic<uint32_t> g_profiler_frame{0};
static std::vector<uint32_t> g_slow_sim_frames;
static std::atomic<bool> g_exit{false};
static std::atomic<int> g_exit_code{0};
static std::chrono::steady_clock::time_point g_next_frame;
static uint8_t g_mmio[0x10000];      // 0xCC000000 - 0xCC00FFFF register file (big-endian bytes)
static bool g_in_interrupt = false;

static bool trace_ax_voice_window() {
  const char* range = std::getenv("MELEE_TRACE_AX_SFX");
  unsigned first = 0, last = 0;
  const uint32_t retrace = retrace_count();
  return range && std::sscanf(range, "%u:%u", &first, &last) == 2 &&
         first <= last && retrace >= first && retrace <= last;
}

static void trace_ax_voice(const ax::VoiceTrace& trace) {
  if (!trace_ax_voice_window()) return;
  log("[ax-voice-legacy] retrace=%u block=%llu ms=%u pb=%08X cur=%08X>%08X end=%08X ratio=%08X frac=%04X>%04X env=%04X/%04X input=%d/%d peak=%d output=%d/%d peak=%d",
      retrace_count(), (unsigned long long) trace.frame, trace.millisecond,
      trace.pb_addr, trace.cur_before, trace.cur_after, trace.end_addr,
      trace.ratio, trace.frac_before, trace.frac_after, trace.volume_before,
      trace.volume_delta, trace.input_first, trace.input_last,
      trace.input_peak, trace.output_first, trace.output_last,
      trace.output_peak);
}

// MELEE_TRACE_AX_VOICES (M4 diagnostic, off by default): every AX frame and every voice block the
// ucode renders, with hashes of the voice's own samples before the final mix (ax_ucode.h).
static void trace_ax_frame(const char* line) {
  log("[ax-vtrace] retrace=%u tb=%llu vi=%llu %s", retrace_count(), (unsigned long long)(cpu ? cpu->tb : 0),
      (unsigned long long)(next_retrace_tb() - TB_PER_FRAME), line);
}

// ---------------- logging ----------------
// Every line also goes to melee_port.log in the working directory (truncated at start), so a play
// session can be inspected afterwards without the console window.
static FILE* g_log_file = nullptr;
static void open_log_file() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  // Keep the previous session's log (a desync or crash report is often noticed only after relaunching).
  const std::string path = options.log_file.empty() ? "melee_port.log" : options.log_file;
  const std::string previous = path.size() > 4 && path.compare(path.size() - 4, 4, ".log") == 0
      ? path.substr(0, path.size() - 4) + ".prev.log" : path + ".prev";
  std::remove(previous.c_str());
  std::rename(path.c_str(), previous.c_str());
  g_log_file = std::fopen(path.c_str(), "w");
  // Which build wrote the log comes first, so a report always says it.
  if (g_log_file) {
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* name = std::strrchr(exe, '\\');
    std::fprintf(g_log_file, "Melee Unlocked %s, %s\n", MELEE_PORT_VERSION, name ? name + 1 : exe);
  }
}
// Lines are formatted by the caller and written (and flushed) by a background thread. Flushing on
// the caller made the simulation thread wait for the disk: with a busy disk (a build, a download)
// the once-a-second frame line alone stalled 60 Hz ticks by 20-200 ms. Order is kept: the writer
// takes g_log_io before it takes the pending text, and so does log_flush().
static std::mutex g_log_mutex;             // guards g_log_pending
static std::condition_variable g_log_cv;
static std::string g_log_pending;
static std::timed_mutex g_log_io;          // held while text goes to stdout and the file
static void log_drain(bool wait_for_io) {
  std::unique_lock<std::timed_mutex> io(g_log_io, std::defer_lock);
  // A crash on the writer thread itself must not wait on the lock it holds: give up after a while
  // and write anyway (a garbled line beats a hung crash report).
  if (wait_for_io) io.lock(); else if (!io.try_lock_for(std::chrono::milliseconds(500))) {}
  std::string batch;
  { std::lock_guard<std::mutex> lock(g_log_mutex); batch.swap(g_log_pending); }
  if (batch.empty()) return;
  std::fwrite(batch.data(), 1, batch.size(), stdout);
  std::fflush(stdout);
  if (g_log_file) { std::fwrite(batch.data(), 1, batch.size(), g_log_file); std::fflush(g_log_file); }
}
static void log_enqueue(const char* data, size_t len) {
  static std::once_flag started;
  std::call_once(started, [] {
    std::thread([] {
      for (;;) {
        { std::unique_lock<std::mutex> lock(g_log_mutex); g_log_cv.wait(lock, [] { return !g_log_pending.empty(); }); }
        log_drain(true);
      }
    }).detach();
    std::atexit([] { log_flush(); });
  });
  { std::lock_guard<std::mutex> lock(g_log_mutex); g_log_pending.append(data, len); }
  g_log_cv.notify_one();
}
void log_flush() { log_drain(false); }

void log(const char* fmt, ...) {
  if (options.quiet) return;
  open_log_file();
  char stack[1024];
  va_list ap; va_start(ap, fmt);
  const int n = std::vsnprintf(stack, sizeof stack - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n < sizeof stack - 1) {
    stack[n] = '\n';
    log_enqueue(stack, (size_t)n + 1);
  } else {
    std::string line((size_t)n + 1, '\0');
    va_list ap2; va_start(ap2, fmt);
    std::vsnprintf(line.data(), line.size(), fmt, ap2);
    va_end(ap2);
    line.back() = '\n';
    log_enqueue(line.data(), line.size());
  }
}

void log_guest_text(const char* data, size_t len) { log_enqueue(data, len); }

// Ends the process without running any DLL's unload code. The game has saved everything by the
// time this is called (replays, settings, shader cache, the log). The NVIDIA upscaler libraries
// release their D3D12 objects when their DLLs unload, which is after the device is gone, and on
// some machines that crashed: inside nvngx_dlss.dll, or in the graphics driver from
// nvngx_dlssd.dll. The player had only closed the game and was shown a crash report. Streamline's
// own shutdown is not an option either (it blocked inside the same unload, see
// streamline::shutdown_for_process_exit), so the unload is skipped: Windows reclaims the rest.
[[noreturn]] void end_process(int code) {
  log_flush();
  std::fflush(nullptr);
  TerminateProcess(GetCurrentProcess(), (UINT)code);
  std::abort();   // not reached
}

static void (*g_die_hook)(const char*) = nullptr;
void set_die_hook(void (*hook)(const char*)) { g_die_hook = hook; }

[[noreturn]] void die(const char* fmt, ...) {
  log_flush();
  va_list ap; va_start(ap, fmt);
  std::fprintf(stderr, "\nFATAL: ");
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  va_end(ap);
  if (g_log_file) {
    va_list ap2; va_start(ap2, fmt);
    std::fprintf(g_log_file, "\nFATAL: ");
    std::vfprintf(g_log_file, fmt, ap2);
    std::fprintf(g_log_file, "\n");
    va_end(ap2);
    std::fflush(g_log_file);
  }
  std::fflush(stderr);
  std::fflush(stdout);
  // MELEE_TEST_RAM_DUMP=<file> (tests): the guest memory as it was when the game stopped, so a stop
  // inside a mod's own code can be read afterwards (the code a mod writes at run time is in no file).
  if (const char* path = std::getenv("MELEE_TEST_RAM_DUMP"); path && *path && ram && ram_size) {
    if (FILE* f = std::fopen(path, "wb")) { std::fwrite(ram, 1, ram_size, f); std::fclose(f); }
  }
  if (void (*hook)(const char*) = g_die_hook) {
    g_die_hook = nullptr;   // once, even if the hook itself fails
    char message[512];
    va_list ap3; va_start(ap3, fmt);
    std::vsnprintf(message, sizeof message, fmt, ap3);
    va_end(ap3);
    hook(message);
  }
  // Not std::exit: a crash in a library's unload code would replace the report just written.
  end_process(3);
}

const char* symbol_name(uint32_t addr) {
  // Binary search the sorted function name table for the containing function.
  size_t lo = 0, hi = guest::name_table_count;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (guest::name_table[mid].addr <= addr) lo = mid + 1; else hi = mid;
  }
  if (lo == 0) return "?";
  return guest::name_table[lo - 1].name;
}

// ---------------- memory ----------------
uint8_t* game_image = nullptr;
uint32_t game_image_size = 0;
uint8_t* try_ptr(uint32_t addr, uint32_t bytes) {
  uint32_t off = addr & 0x3FFFFFFFu;
  if (valid_range(off, bytes, ram_size)) return ram + off;
  constexpr uint32_t IMAGE_PHYS = 0x10000000u;
  if (game_image && off >= IMAGE_PHYS && valid_range(off - IMAGE_PHYS, bytes, game_image_size)) return game_image + (off - IMAGE_PHYS);
  return nullptr;
}
uint8_t* ptr(uint32_t addr, uint32_t bytes) {
  uint8_t* p = try_ptr(addr, bytes);
  if (!p) die("host access outside RAM: %08X+%X", addr, bytes);
  return p;
}
uint32_t rd32(uint32_t a) { uint32_t v; std::memcpy(&v, ptr(a, 4), 4); return _byteswap_ulong(v); }
uint16_t rd16(uint32_t a) { uint16_t v; std::memcpy(&v, ptr(a, 2), 2); return _byteswap_ushort(v); }
uint8_t rd8(uint32_t a) { return *ptr(a); }
void mark_ram_write(uint32_t a, uint32_t bytes) { ppc::mark_ram_write(a, bytes); }
void wr32(uint32_t a, uint32_t v) { v = _byteswap_ulong(v); std::memcpy(ptr(a, 4), &v, 4); mark_ram_write(a, 4); }
void wr16(uint32_t a, uint16_t v) { v = _byteswap_ushort(v); std::memcpy(ptr(a, 2), &v, 2); mark_ram_write(a, 2); }
void wr8(uint32_t a, uint8_t v) { *ptr(a) = v; mark_ram_write(a, 1); }
// A guest string for a report. A pointer outside RAM (a panic raised with a bad argument) names
// itself instead of stopping the report that was about to say what went wrong.
std::string cstr(uint32_t addr, size_t max) {
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    const uint8_t* p = try_ptr(addr + (uint32_t)i, 1);
    if (!p) { char bad[40]; std::snprintf(bad, sizeof bad, "<bad pointer %08X>", addr); return s + bad; }
    if (!*p) break;
    s += (char)*p;
  }
  return s;
}

// ---------------- disc ----------------
// Reads the whole disc image once, in the background at the lowest I/O priority, so the file cache
// holds it. Every game read is synchronous on the simulation thread (as the console's drive
// interrupt only ever arrives between frames), so a read that has to seek a hard drive or wait for
// a busy disk stalled a frame: files loaded during a match (items, Pokemon, stage changes) showed up
// as stutter. Load timing never reaches the game state: replays and online are unaffected.
static std::atomic<bool> g_disc_prefetch_done{true};
static std::atomic<uint64_t> g_disc_prefetch_bytes{0};
static void prefetch_disc_image(const std::string& path) {
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  g_disc_prefetch_done = false;
  std::thread([path] {
    struct Completion { ~Completion() { g_disc_prefetch_done.store(true, std::memory_order_release); } } completion;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // low CPU and I/O priority
    const auto begin = std::chrono::steady_clock::now();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::vector<uint8_t> chunk(4u << 20);
    uint64_t total = 0;
    for (size_t got; (got = std::fread(chunk.data(), 1, chunk.size(), f)) > 0;) {
      total += got;
      g_disc_prefetch_bytes.store(total, std::memory_order_relaxed);
    }
    std::fclose(f);
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    log("disc: image prefetched into the file cache (%llu MB in %.1f s)", (unsigned long long)(total >> 20),
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
  }).detach();
}

void disc_prefetch_wait() {
  // Overlap file-cache warming with graphics initialization, but finish it before simulation.
  // In particular, a slow disk must not be competing with the first stage's synchronous reads.
  while (!g_disc_prefetch_done.load(std::memory_order_acquire)) {
    const size_t mb = (size_t)(g_disc_prefetch_bytes.load(std::memory_order_relaxed) >> 20);
    loading_show(L"Reading game assets into the file cache (MB)", mb, mb + 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  loading_close();
}

bool disc_open(const std::string& path) {
  g_disc = std::fopen(path.c_str(), "rb");
  if (!g_disc) return false;
  prefetch_disc_image(path);
  uint8_t hdr[0x440];
  if (!disc_read(0, hdr, sizeof hdr)) return false;
  auto be = [&](int o) { return ((uint32_t)hdr[o] << 24) | ((uint32_t)hdr[o + 1] << 16) | ((uint32_t)hdr[o + 2] << 8) | hdr[o + 3]; };
  g_fst_offset = be(0x424);
  g_fst_size = be(0x428);
  g_fst_max = be(0x42C);
  if (std::memcmp(hdr, "GALE01", 6) != 0) log("warning: disc id is not GALE01");
  return true;
}
uint64_t g_disc_reads = 0, g_disc_bytes = 0;
static std::mutex g_disc_mutex;   // the DVD worker and the simulation thread share the file
bool disc_read(uint32_t offset, void* dst, uint32_t size) {
  std::lock_guard<std::mutex> lk(g_disc_mutex);
  if (!g_disc) return false;
  if (_fseeki64(g_disc, offset, SEEK_SET) != 0) return false;
  ++g_disc_reads;
  g_disc_bytes += size;
  bool ok = std::fread(dst, 1, size, g_disc) == size;
  auto* output = (uint8_t*)dst;
  if (ok && ram && output >= ram && output <= ram + ppc::RAM_SIZE &&
      size <= (uint32_t)(ram + ppc::RAM_SIZE - output))
    ppc::mark_ram_write(ppc::RAM_BASE + (uint32_t)(output - ram), size);
  return ok;
}
bool disc_read_file(uint32_t vanilla_file_start, uint32_t file_offset, void* dst, uint32_t size) {
  switch (cosmetics::read(vanilla_file_start, file_offset, dst, size)) {
    case cosmetics::OverrideRead::Success:
      if (ram) {
        auto* output = (uint8_t*)dst;
        if (output >= ram && output <= ram + ppc::RAM_SIZE &&
            size <= (uint32_t)(ram + ppc::RAM_SIZE - output))
          ppc::mark_ram_write(ppc::RAM_BASE + (uint32_t)(output - ram), size);
      }
      return true;
    case cosmetics::OverrideRead::Failed: return false;
    case cosmetics::OverrideRead::NotOverridden: break;
  }
  uint64_t absolute = (uint64_t)vanilla_file_start + file_offset;
  if (absolute > UINT32_MAX) return false;
  return disc_read((uint32_t)absolute, dst, size);
}
uint32_t disc_fst_offset() { return g_fst_offset; }
uint32_t disc_fst_size() { return g_fst_size; }

// Looks a file up by name in the disc's FST (root and nested directories; exact match first,
// then case-insensitive). Used to serve ISO files to host-side loaders (Slippi game files).
bool disc_find_file(const std::string& name, uint32_t* offset, uint32_t* size) {
  static std::vector<uint8_t> fst;
  if (fst.empty()) {
    if (!g_fst_size) return false;
    fst.resize(g_fst_size);
    if (!disc_read(g_fst_offset, fst.data(), g_fst_size)) { fst.clear(); return false; }
  }
  auto be32 = [&](size_t o) { return o + 4 <= fst.size() ? ((uint32_t)fst[o] << 24) | ((uint32_t)fst[o + 1] << 16) | ((uint32_t)fst[o + 2] << 8) | fst[o + 3] : 0u; };
  uint32_t entries = be32(8);
  size_t strings = (size_t)entries * 12;
  if (strings > fst.size()) return false;
  for (int pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 1; i < entries; ++i) {
      uint32_t a = be32(i * 12);
      if (a >> 24) continue;   // directory
      size_t so = strings + (a & 0xFFFFFF);
      if (so >= fst.size()) continue;
      const char* n = (const char*)&fst[so];
      size_t maxlen = fst.size() - so;
      bool match = pass == 0 ? (std::strncmp(n, name.c_str(), maxlen) == 0) : (_strnicmp(n, name.c_str(), maxlen) == 0);
      if (match && std::strlen(n) == name.size()) {
        if (offset) *offset = be32(i * 12 + 4);
        if (size) *size = be32(i * 12 + 8);
        return true;
      }
    }
  }
  return false;
}

namespace {
const std::vector<uint8_t>* disc_path_fst() {
  static std::vector<uint8_t> fst;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  if (fst.empty() && g_fst_size) {
    fst.resize(g_fst_size);
    if (!disc_read(g_fst_offset, fst.data(), g_fst_size)) fst.clear();
  }
  return fst.empty() ? nullptr : &fst;
}
}  // namespace

bool disc_find_path_by_offset(uint32_t offset, std::string* path) {
  const auto* fst = disc_path_fst();
  return fst && disc_fst::path_by_offset(*fst, offset, path);
}

bool disc_music_paths(std::vector<std::string>* paths) {
  const auto* fst = disc_path_fst();
  return fst && disc_fst::music_paths(*fst, paths);
}
uint32_t disc_fst_max_size() { return g_fst_max; }

// ---------------- boot ----------------
static uint32_t disc_dol_offset() {
  uint8_t hdr[4];
  if (!disc_read(0x420, hdr, 4)) die("cannot read disc DOL offset");
  return ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
}
// Both engines are built for the retail main.dol: the recompiled one runs its code, the native one
// reads its data. A modded disc (a patched DOL, m-ex and friends) fails the SHA-1 of NTSC 1.02.
constexpr uint32_t kVanillaDolSize = 0x4385E0u;
// True for the retail NTSC 1.02 main.dol (kVanillaDolSize bytes).
static bool vanilla_dol_image(const std::vector<uint8_t>& image) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  uint8_t digest[20];
  const uint8_t expected[20] = {0x08,0xe0,0xbf,0x20,0x13,0x4d,0xfc,0xb2,0x60,0x69,0x96,0x71,0x00,0x45,0x27,0xb2,0xd6,0xbb,0x1a,0x45};
  if (image.size() != kVanillaDolSize) return false;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0)
    die("cannot initialize game-image verification");
  NTSTATUS hash_status = BCryptHash(algorithm, nullptr, 0, (PUCHAR)image.data(), kVanillaDolSize, digest, sizeof digest);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return hash_status >= 0 && std::memcmp(digest, expected, sizeof digest) == 0;
}
bool disc_has_vanilla_dol() {
  std::vector<uint8_t> image(kVanillaDolSize);
  if (!disc_read(disc_dol_offset(), image.data(), kVanillaDolSize))
    die("this file is not a full Melee disc image. Use a clean, uncompressed Melee NTSC 1.02 ISO (a trimmed or compressed image will not work)");
  return vanilla_dol_image(image);
}
// ---- mod discs (Static Recomp) ----
// The recompiled game is the vanilla 1.02 code. For a mod disc the vanilla game is booted first from
// the player's own vanilla disc (to build the reference image), then the mod's main.dol replaces it in
// RAM and every function whose bytes differ runs from RAM (ppc::redirect_changed_functions).
static bool g_mod_disc = false;
static std::vector<std::pair<uint32_t, uint32_t>> g_text_ranges;   // vanilla DOL text sections
static std::vector<uint8_t> g_mod_reference;                       // vanilla + Slippi code, per text range, concatenated
static std::vector<uint8_t> g_mod_reference_boot;                  // the same before Slippi's served table joined it
static std::vector<uint8_t> g_vanilla_text;                        // vanilla code alone, same layout
static std::vector<uint32_t> g_mod_block_versions;                  // write generations of the watched code blocks

bool mod_disc_active() { return g_mod_disc; }

// Clean mode: a mod disc that was built for the plain game and cannot share RAM with Slippi's code
// (the 20XX Hack Pack calls code inside its character select file at fixed heap addresses, and
// Slippi's heap table moves that file). Such a disc runs without Slippi's codes: only the disc's own
// code is in RAM, and every function Slippi's codes touch runs from RAM too, because the compiled
// game has those codes built in. No online play and no replays in this mode.
static bool g_mod_clean = false;
bool mod_clean_mode() { return g_mod_clean; }

bool mod_reference_from_table() {
  static const bool boot_only = [] { const char* v = std::getenv("MELEE_MOD_REFERENCE"); return v && std::strcmp(v, "boot") == 0; }();
  return !boot_only;
}

// The reference byte for a guest address inside the text ranges (the vectors concatenate them).
static uint8_t* reference_byte(std::vector<uint8_t>& image, uint32_t addr) {
  size_t at = 0;
  for (const auto& r : g_text_ranges) {
    if (addr >= r.first && addr - r.first < r.second) return image.data() + at + (addr - r.first);
    at += r.second;
  }
  return nullptr;
}

// Makes the reference differ from RAM at these bytes, so the function that owns them runs from RAM.
// A function the port implements on the host keeps that implementation: Slippi's change to it never
// ran in the compiled game either.
static void mod_reference_force(uint32_t addr, uint32_t n) {
  uint32_t lo = 0, hi = 0;
  if (ppc::function_bounds(addr, &lo, &hi) && ppc::compiled_as_host_function(lo)) return;
  for (uint32_t i = 0; i < n; ++i)
    if (uint8_t* p = reference_byte(g_mod_reference, addr + i)) *p = (uint8_t)~rd8(addr + i);
}

bool mod_reference_set(uint32_t addr, const uint8_t* bytes, uint32_t n) {
  if (!g_mod_disc || g_mod_reference.empty()) return false;
  bool any = false;
  for (uint32_t i = 0; i < n; ++i)
    if (uint8_t* p = reference_byte(g_mod_reference, addr + i)) { *p = bytes[i]; any = true; }
  return any;
}

static void load_dol_from_file(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) die("cannot open the vanilla disc %s", path.c_str());
  auto read_at = [&](uint64_t off, void* dst, size_t n) {
    return _fseeki64(f, (long long)off, SEEK_SET) == 0 && std::fread(dst, 1, n, f) == n;
  };
  uint8_t hdr[4];
  if (!read_at(0x420, hdr, 4)) die("cannot read the vanilla disc header");
  const uint32_t dol_offset = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
  // The base has to be the retail game: the recompiled code is that game, and the mod's own code is
  // found by comparing against it. A modified disc here (the mod disc itself set as the Melee ISO,
  // for one) made the comparison find nothing, so the retail code ran on the mod's files and the
  // game stopped at boot on the first thing that no longer fit (the mod's larger sound bank).
  {
    std::vector<uint8_t> image(kVanillaDolSize);
    if (!read_at(dol_offset, image.data(), kVanillaDolSize) || !vanilla_dol_image(image)) {
      std::fclose(f);
      die("the Melee ISO (%s) is a modified disc. A mod runs on top of an unmodified Melee NTSC 1.02 ISO: "
          "choose your clean disc as the Melee ISO on the Play page and keep the mod's ISO in the Mods folder",
          path.c_str());
    }
  }
  uint8_t dh[0x100];
  if (!read_at(dol_offset, dh, sizeof dh)) die("cannot read the vanilla DOL header");
  auto be = [&](int o) { return ((uint32_t)dh[o] << 24) | ((uint32_t)dh[o + 1] << 16) | ((uint32_t)dh[o + 2] << 8) | dh[o + 3]; };
  g_text_ranges.clear();
  std::vector<std::pair<uint32_t, uint32_t>> data_sections;
  for (int i = 0; i < 18; ++i) {
    uint32_t off = be(i * 4), addr = be(0x48 + i * 4), size = be(0x90 + i * 4);
    if (!size) continue;
    if (!read_at(dol_offset + off, ptr(addr, size), size)) die("cannot read vanilla DOL section %d", i);
    if (i < 7) g_text_ranges.push_back({addr, size});
    else data_sections.push_back({addr, size});
  }
  std::fclose(f);
  // Slippi's helper branch slots (800055EC..800056BC, inside a DOL data section): the recompiled
  // game compiled each one as a function that jumps to Slippi's helper. Mods put their own helpers
  // in the same slots (Akaneia replaces 8 of them, one is its sound bank request), so the slots are
  // watched as code too: a slot the mod rewrites runs the mod's branch, not the compiled one.
  for (const auto& d : data_sections) {
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (size_t i = 0; i < guest::name_table_count; ++i) {
      const uint32_t a = guest::name_table[i].addr;
      if (a < d.first || a - d.first >= d.second) continue;
      if (std::strncmp(guest::name_table[i].name, "gecko_hook_", 11) != 0) continue;
      lo = std::min(lo, a);
      hi = std::max(hi, a + 4);
    }
    if (lo < hi && hi - lo <= 0x1000) g_text_ranges.push_back({lo, hi - lo});
  }
  g_vanilla_text.clear();
  for (const auto& r : g_text_ranges) { const uint8_t* p = ptr(r.first, r.second); g_vanilla_text.insert(g_vanilla_text.end(), p, p + r.second); }
}

// Recognised by what the disc carries, never by its file name: the pack's two code and table files,
// its numbered character select file and its renamed HUD file, with the retail names gone.
static bool disc_is_fixed_address_pack() {
  return disc_find_file("AI_Engine.bin", nullptr, nullptr) && disc_find_file("StageSwapTable.bin", nullptr, nullptr) &&
         disc_find_file("MnSlChr.0sd", nullptr, nullptr) && disc_find_file("IfAl0.usd", nullptr, nullptr) &&
         !disc_find_file("MnSlChr.usd", nullptr, nullptr) && !disc_find_file("IfAll.usd", nullptr, nullptr);
}

static void load_dol_from_disc() {
  const uint32_t dol_offset = disc_dol_offset();
  if (!disc_has_vanilla_dol()) {
    if (options.mod_base_iso.empty())
      die("ISO DOL does not match vanilla Melee NTSC 1.02; recompiled code cannot run this image");
    // A mod disc: boot the vanilla code first; the mod's code is laid over it after the Slippi
    // tables are installed (apply_mod_code, boot_setup).
    load_dol_from_file(options.mod_base_iso);
    g_mod_disc = true;
    install_mod_disc_guards();
    log("boot: mod disc; vanilla code from %s, the mod's code follows", options.mod_base_iso.c_str());
    if (disc_is_fixed_address_pack() && !std::getenv("MELEE_MOD_NO_CLEAN")) {
      g_mod_clean = true;
      install_clean_mode_music();
#ifdef MELEE_NO_SLIPPI   // the same lines of this file's boot path, worded for the build without that layer
      log("mods: this disc runs in clean mode (its own code only); online play and replays are off for it");
#else
      log("mods: this disc runs in clean mode (its own code only, without Slippi's codes); online play and replays are off for it");
#endif
    }
    return;
  }
  uint8_t dh[0x100];
  if (!disc_read(dol_offset, dh, sizeof dh)) die("cannot read DOL header");
  auto be = [&](int o) { return ((uint32_t)dh[o] << 24) | ((uint32_t)dh[o + 1] << 16) | ((uint32_t)dh[o + 2] << 8) | dh[o + 3]; };
  for (int i = 0; i < 18; ++i) {
    uint32_t off = be(i * 4), addr = be(0x48 + i * 4), size = be(0x90 + i * 4);
    if (!size) continue;
    if (!disc_read(dol_offset + off, ptr(addr, size), size)) die("cannot read DOL section %d", i);
  }
  // The DOL header's bss range overlaps the loaded .sdata section; the guest's own
  // __init_data zeroes .bss/.sbss precisely and RAM starts zeroed, so do not memset here.
  log("boot: DOL loaded from disc offset %08X, bss %08X+%X, entry %08X", dol_offset, be(0xD8), be(0xDC), be(0xE0));
}

// Reproduces Slippi Dolphin's boot-time Gecko installation in guest RAM: codehandler.bin at
// 0x80001800, bootloader.gct at 0x800028B8, then the effect of running the handler once (its
// 32-bit writes and the C2 hook branches into the caves inside the table). The recompiled code
// already contains these patches; this keeps RAM identical to what the game expects to read.
static void apply_gecko_boot_ram() {
  std::memcpy(ptr(0x80001800u, (uint32_t)gecko::codehandler_bin_size), gecko::codehandler_bin, gecko::codehandler_bin_size);
  mark_ram_write(0x80001800u, (uint32_t)gecko::codehandler_bin_size);
  wr32(0x80001D6Cu, 0x4E800020u);   // USB Gecko I/O replaced by blr, as Slippi does
  wr32(0x80001800u, 0xD01F1BADu);   // handler magic
  std::memcpy(ptr(0x800028B8u, (uint32_t)gecko::bootloader_gct_size), gecko::bootloader_gct, gecko::bootloader_gct_size);
  mark_ram_write(0x800028B8u, (uint32_t)gecko::bootloader_gct_size);
  wr8(0x80001807u, 1);              // codes on
  for (size_t i = 0; i < gecko::boot_writes_count; ++i) {
    const gecko::Write& w = gecko::boot_writes[i];
    std::memcpy(ptr(w.addr, w.size), w.data, w.size);
    mark_ram_write(w.addr, w.size);
  }
  for (size_t i = 0; i < gecko::boot_hooks_count; ++i) {
    const gecko::HookInstall& h = gecko::boot_hooks[i];
    wr32(h.hook, 0x48000000u | ((h.cave_addr - h.hook) & 0x03FFFFFCu));
    uint32_t last = h.cave_addr + (h.words - 1) * 4;
    wr32(last, 0x48000000u | (((h.hook + 4) - last) & 0x03FFFFFCu));
  }
}
static void install_gecko_boot() {
#ifdef MELEE_NO_SLIPPI
#define BOOT_TABLES_NAME "code tables"
#else
#define BOOT_TABLES_NAME "Slippi code tables"
#endif
  if (!gecko::codehandler_bin_size) { log("boot: translated without " BOOT_TABLES_NAME); return; }
  apply_gecko_boot_ram();
  slippi::init();
  log("boot: " BOOT_TABLES_NAME " installed (%zu boot writes, %zu boot hooks, main GCT %zu bytes served over EXI)",
      gecko::boot_writes_count, gecko::boot_hooks_count, gecko::slippi_gct_size);
#undef BOOT_TABLES_NAME
}

// The mod's main.dol over the vanilla code in RAM, then the Slippi tables again (as Dolphin applies
// them to whatever disc it boots), then every function whose code differs from vanilla + Slippi runs
// from RAM. The code ranges stay watched so code the mod writes later (its own code lists, a memory
// card payload) is picked up at the next retrace.
static void apply_mod_code() {
  g_mod_reference.clear();
  for (const auto& r : g_text_ranges) {
    const uint8_t* p = ptr(r.first, r.second);
    g_mod_reference.insert(g_mod_reference.end(), p, p + r.second);
  }
  g_mod_reference_boot = g_mod_reference;
  // Clean mode: Slippi's handler and boot table leave RAM before the mod's code goes in (the mod may
  // use that low memory itself), and nothing may reach their compiled twins by address any more. The
  // same holds for the heap addresses the compiled copies of Slippi's main list were made for: the
  // mod's own files load there now.
  if (g_mod_clean && gecko::codehandler_bin_size) {
    std::memset(ptr(0x80001800u, (uint32_t)gecko::codehandler_bin_size), 0, gecko::codehandler_bin_size);
    std::memset(ptr(0x800028B8u, (uint32_t)gecko::bootloader_gct_size), 0, gecko::bootloader_gct_size);
    mark_ram_write(0x80001800u, (uint32_t)gecko::codehandler_bin_size);
    mark_ram_write(0x800028B8u, (uint32_t)gecko::bootloader_gct_size);
    const uint32_t text_start = g_text_ranges.empty() ? 0x80003100u : g_text_ranges.front().first;
    ppc::disable_dispatch_range(0x80001800u, std::min(text_start, 0x80001800u + (uint32_t)gecko::codehandler_bin_size));
    ppc::disable_dispatch_range(0x800028B8u, std::min(text_start, 0x800028B8u + (uint32_t)gecko::bootloader_gct_size));
    if (gecko::gct_base_used)
      ppc::disable_dispatch_range(gecko::gct_base_used, gecko::gct_base_used + (uint32_t)gecko::slippi_gct_size + 0x1000u);
  }
  const uint32_t dol_offset = disc_dol_offset();
  uint8_t dh[0x100];
  if (!disc_read(dol_offset, dh, sizeof dh)) die("cannot read the mod's DOL header");
  auto be = [&](int o) { return ((uint32_t)dh[o] << 24) | ((uint32_t)dh[o + 1] << 16) | ((uint32_t)dh[o + 2] << 8) | dh[o + 3]; };
  for (int i = 0; i < 18; ++i) {
    uint32_t off = be(i * 4), addr = be(0x48 + i * 4), size = be(0x90 + i * 4);
    if (!size) continue;
    if (!try_ptr(addr, size) || !disc_read(dol_offset + off, ptr(addr, size), size)) die("cannot read the mod's DOL section %d", i);
  }
  // Slippi's boot patches again over the mod's code, as Slippi Dolphin applies them to any disc.
  if (g_mod_clean) {
    // The reference so far is retail plus Slippi's boot codes. Every word Slippi's main list would
    // install is compiled in as well, so each of those is forced to differ: the functions that own
    // them run the disc's code from RAM, whatever the optional codes are set to.
    uint32_t forced = 0;
    slippi::for_each_served_code_write([&](uint32_t addr, uint32_t n) { mod_reference_force(addr, n); ++forced; });
#ifdef MELEE_NO_SLIPPI
    log("mods: clean mode; %u places the main code list changes run the disc's code", forced);
#else
    log("mods: clean mode; %u places Slippi's main list changes run the disc's code", forced);
#endif
  } else if (gecko::codehandler_bin_size) {
    apply_gecko_boot_ram();
    ppc::add_ram_code_range(0x80001800u, 0x80001800u + (uint32_t)gecko::codehandler_bin_size);
    ppc::add_ram_code_range(0x800028B8u, 0x800028B8u + (uint32_t)gecko::bootloader_gct_size);
  }
  size_t total = 0, at = 0;
  for (const auto& r : g_text_ranges) {
    total += ppc::redirect_changed_functions(g_mod_reference.data() + at, ram, r.first, r.second);
    at += r.second;
  }
  // A function the mod changed may be small enough to have been built into its callers, and such a
  // caller still carries the original. Every mod disc is checked, not only clean mode (where the
  // functions Slippi's codes touch go to RAM too): a mod that rewrote the four-instruction lookup of
  // Kirby's Yoshi egg model kept reading the original, empty table from the one caller that had it
  // built in, and the game stopped on a missing model when that Kirby caught someone.
  total += ppc::redirect_inlined_callers(ram);
  g_mod_block_versions.assign(ppc::RAM_WATCH_COUNT, 0);
  for (const auto& r : g_text_ranges) ppc::watch_ram_range(r.first & 0x3FFFFFFFu, r.second);
  for (uint32_t b = 0; b < ppc::RAM_WATCH_COUNT; ++b) g_mod_block_versions[b] = ppc::g_ram_versions[b].load();
  log("mods: the mod's main.dol is in; %zu game functions run its code", total);

  // Clean mode has no Slippi code in the game, so there is no online menu and nothing to pair: the
  // build is marked as a mod without an identity, which every matchmaking mode refuses.
  if (g_mod_clean) {
    char title[0x41] = {};
    disc_read(0x20, title, 0x40);
    slippi::online::LocalBuild build;
    build.mod_view = true;
    build.name = title[0] ? std::string(title) : std::string("a modded disc");
    slippi::online::set_local_build(build);
    slippi::online::set_native_gameplay_profile(slippi::online::NativeGameplayProfile::OtherMod);
    return;
  }
  // Online: a mod disc plays Direct and Teams, against the same mod on every side (Unranked and Party refuse).
  // Hash the entire disc, including every file payload and DOL data section. The patched text
  // also binds the identity to Slippi's boot code. No path/mtime cache can reuse a stale digest.
  {
    std::string hex;
    const std::string disc_hash = source_port::mods::sha256_file(std::filesystem::u8path(options.iso));
    if (slippi::online::native_mod_fingerprint_valid(disc_hash)) {
      source_port::mods::Sha256 identity;
      identity.update("mu-static-mod-disc-v2\n");
      identity.update(disc_hash);
      for (const auto& r : g_text_ranges) identity.update(ptr(r.first, r.second), r.second);
      hex = identity.hex();
    }
    if (!slippi::online::native_mod_fingerprint_valid(hex))
      log("mods: cannot verify the disc's complete content; Direct matchmaking will be refused");
    char title[0x41] = {};
    disc_read(0x20, title, 0x40);
    slippi::online::LocalBuild build;
    build.mod_view = true;
    build.extended_content = true;   // the Static engine executes the mod's own code
    build.fingerprint = hex;
    build.name = title[0] ? std::string(title) : std::string("a modded disc");
    slippi::online::set_local_build(build);
    slippi::online::set_native_gameplay_profile(slippi::online::NativeGameplayProfile::OtherMod);
    log("mods: online plays Direct and Teams against the same mod (%s, %s)", build.name.c_str(), hex.substr(0, 16).c_str());
  }
}
// At each retrace: code blocks written since the last look are compared again. A 64 KB block can
// hold the end of one text range and the start of the next (block 0 holds both vanilla ranges), so
// the changed blocks are collected first, every range part in them is compared, and only then are
// their versions stored (storing inside the range loop skipped the second range's part).
static void check_mod_code_writes() {
  if (!g_mod_disc || g_mod_block_versions.empty()) return;
  std::vector<std::pair<uint32_t, uint32_t>> changed;   // (block, the version read now)
  for (const auto& r : g_text_ranges) {
    const uint32_t first = (r.first & 0x3FFFFFFFu) >> ppc::RAM_WATCH_SHIFT, last = ((r.first & 0x3FFFFFFFu) + r.second - 1) >> ppc::RAM_WATCH_SHIFT;
    for (uint32_t b = first; b <= last; ++b) {
      const uint32_t v = ppc::g_ram_versions[b].load(std::memory_order_relaxed);
      if (v == g_mod_block_versions[b]) continue;
      if (std::find_if(changed.begin(), changed.end(), [&](const auto& c) { return c.first == b; }) == changed.end()) changed.push_back({b, v});
    }
  }
  if (changed.empty()) return;
  size_t at = 0;
  for (const auto& r : g_text_ranges) {
    for (const auto& c : changed) {
      const uint32_t b = c.first;
      const uint32_t lo = std::max(r.first, ppc::RAM_BASE + (b << ppc::RAM_WATCH_SHIFT));
      const uint32_t hi = std::min(r.first + r.second, ppc::RAM_BASE + ((b + 1) << ppc::RAM_WATCH_SHIFT));
      if (hi <= lo) continue;
      const size_t off = at + (lo - r.first);
      const size_t n = ppc::redirect_changed_functions(g_mod_reference.data() + off, ram, lo, hi - lo);
      if (n) log("mods: %zu more functions run code written at run time (block %08X)", n, lo);
      if (n) ppc::redirect_inlined_callers(ram);
      ppc::report_kept_compiled(g_mod_reference_boot.data() + off, g_mod_reference.data() + off, ram, lo, hi - lo);
    }
    at += r.second;
  }
  for (const auto& c : changed) g_mod_block_versions[c.first] = c.second;
}

// Test runs (MELEE_DIGEST_RAM=N): every N retraces the state digest also carries a hash of all of main
// RAM, so two runs that must behave the same (a function compiled or run from RAM) are compared on every
// byte the game keeps, not only on the fighters. MELEE_DIGEST_RAM_DUMP=<retrace>:<file> writes RAM at
// that retrace, to find what differs when two hashes do not match.
static uint32_t digest_ram_interval() {
  static const uint32_t n = [] { const char* v = std::getenv("MELEE_DIGEST_RAM"); return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 0u; }();
  return n;
}
// MELEE_DIGEST_RAM_SKIP=<addr>+<bytes>[,...] leaves words out of the hash: the few that hold host time
// (two identical runs differ only there, e.g. 800030D8+8 and the OS time words in .sbss).
static uint64_t ram_hash() {
  static const std::vector<std::pair<uint32_t, uint32_t>> skip = [] {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    if (const char* v = std::getenv("MELEE_DIGEST_RAM_SKIP"))
      for (const char* p = v; *p;) {
        char* end = nullptr;
        const uint32_t a = (uint32_t)std::strtoul(p, &end, 16);
        if (end == p || *end != '+') break;
        const uint32_t n = (uint32_t)std::strtoul(end + 1, &end, 0);
        out.push_back({(a & 0x3FFFFFFFu) & ~7u, ((a & 7u) + n + 7) / 8});   // whole 8-byte words
        p = (*end == ',') ? end + 1 : end;
      }
    return out;
  }();
  const uint64_t* p = reinterpret_cast<const uint64_t*>(ram);
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (size_t i = 0; i < ppc::RAM_SIZE / 8; ++i) {
    uint64_t w = p[i];
    for (const auto& s : skip) if (i >= s.first / 8 && i < s.first / 8 + s.second) { w = 0; break; }
    h = (h ^ w) * 0x100000001B3ull; h ^= h >> 29;
  }
  return h;
}
// MELEE_TEST_PEEK=<addr>[,<addr>...]@<retrace>: logs those RAM words at that retrace (test runs).
static void test_peek_and_dump() {
  static const auto peek = [] {
    std::pair<std::vector<uint32_t>, uint32_t> out{{}, 0};
    const char* v = std::getenv("MELEE_TEST_PEEK");
    if (!v) return out;
    const char* at = std::strchr(v, '@');
    if (!at) return out;
    out.second = (uint32_t)std::strtoul(at + 1, nullptr, 0);
    for (const char* p = v; p < at;) {
      char* end = nullptr;
      const uint32_t a = (uint32_t)std::strtoul(p, &end, 16);
      if (end == p) break;
      out.first.push_back(a);
      p = (*end == ',') ? end + 1 : end;
    }
    return out;
  }();
  if (!peek.first.empty() && g_retraces == peek.second)
    for (uint32_t a : peek.first) log("peek %08X = %08X", a, try_ptr(a, 4) ? rd32(a) : 0u);
  static const auto dump = [] {
    std::pair<uint32_t, std::string> out{0, {}};
    const char* v = std::getenv("MELEE_DIGEST_RAM_DUMP");
    if (!v) return out;
    const char* colon = std::strchr(v, ':');
    if (!colon) return out;
    out.first = (uint32_t)std::strtoul(v, nullptr, 0);
    out.second = colon + 1;
    return out;
  }();
  // MELEE_TEST_WIDESCREEN_TOGGLES=<retrace>[,<retrace>...]: flips Slippi widescreen at those retraces,
  // as the settings panel would (a switch during a session, in hidden test runs).
  static const std::vector<uint32_t> toggles = [] {
    std::vector<uint32_t> out;
    if (const char* v = std::getenv("MELEE_TEST_WIDESCREEN_TOGGLES"))
      for (const char* p = v; *p;) { char* end = nullptr; out.push_back((uint32_t)std::strtoul(p, &end, 0)); if (end == p) break; p = (*end == ',') ? end + 1 : end; }
    return out;
  }();
  if (std::find(toggles.begin(), toggles.end(), g_retraces) != toggles.end()) {
    slippi::request_widescreen(!slippi::widescreen());
    log("test: widescreen switched %s at retrace %u", slippi::widescreen() ? "off" : "on", g_retraces);
  }
  if (!dump.second.empty() && g_retraces == dump.first) {
    if (FILE* f = std::fopen(dump.second.c_str(), "wb")) {
      std::fwrite(ram, 1, ppc::RAM_SIZE, f);
      std::fclose(f);
      log("ram: written to %s at retrace %u", dump.second.c_str(), g_retraces);
    }
  }
}

void init_state_digest() {
  if (!options.state_digest.empty()) {
    g_state_digest = std::fopen(options.state_digest.c_str(), "w");
    if (!g_state_digest) die("cannot open state digest");
    std::fprintf(g_state_digest, "frame,rng,scene");
    for (unsigned slot = 0; slot < 6; ++slot)
      for (const char* field : {"present", "stocks", "action", "anim_frame", "pos_x", "pos_y", "pos_z",
                                "vel_x", "vel_y", "vel_z", "percent", "facing"})
        std::fprintf(g_state_digest, ",p%u_%s", slot, field);
    std::fprintf(g_state_digest, ",scene_major,match_frame");
    if (digest_ram_interval()) std::fprintf(g_state_digest, ",ram");
    std::fputc('\n', g_state_digest);
  }
}

namespace {
struct GuestHeapCall { uint32_t handle = 0, requested = 0, caller = 0, stack = 0; bool available = false; };
thread_local GuestHeapCall g_guest_heap_call;
void record_guest_heap_call(ppc::Context& c) {
  g_guest_heap_call = {c.r[3], c.r[4], c.lr, c.r[1], true};
}
bool heap_trace_read32(uint32_t address, uint32_t& value) {
  if (!try_ptr(address, 4)) return false;
  value = rd32(address);
  return true;
}
bool heap_trace_literal(uint32_t address, const char* literal) {
  const size_t bytes = std::strlen(literal) + 1;
  const uint8_t* data = try_ptr(address, static_cast<uint32_t>(bytes));
  return data && std::memcmp(data, literal, bytes) == 0;
}
void trace_guest_heap_assert(ppc::Context& c) {
  // guest_002.cpp: before its __assert call, r24=Handle*, r30=32-byte-rounded request,
  // and the allocator's caller LR is at r1+60. The __assert entry hook runs BEFORE its
  // prologue overwrites r30 (guest_130.cpp:6532-6538), including direct C++ callers.
  if (c.lr != 0x80015098u || c.r[4] != 233u || !heap_trace_literal(c.r[3], "lbmemory.c")) return;
  const uint32_t handle = c.r[24], rounded = c.r[30];
  uint32_t saved_caller = 0;
  const bool have_saved_caller = c.r[1] <= 0xFFFFFFC3u && heap_trace_read32(c.r[1]+60u, saved_caller);
  const bool have_original = g_guest_heap_call.available && g_guest_heap_call.handle == handle &&
    ((g_guest_heap_call.requested + 31u) & ~31u) == rounded && g_guest_heap_call.stack >= 56u &&
    g_guest_heap_call.stack - 56u == c.r[1];
  log("[guest-heap] failure retrace=%u assert=lbmemory.c:233 assert_lr=%08X heap=%08X rounded=%08X "
      "caller=%08X caller_available=%u original_requested=%08X original_available=%u", retrace_count(),
      c.lr, handle, rounded, have_saved_caller ? saved_caller : have_original ? g_guest_heap_call.caller : 0u,
      (unsigned)(have_saved_caller || have_original), have_original ? g_guest_heap_call.requested : 0u,
      (unsigned)have_original);
  log("[guest-heap] registers sp=%08X r24=%08X r25=%08X r26=%08X r27=%08X r29=%08X r30=%08X r31=%08X cr=%08X",
      c.r[1], c.r[24], c.r[25], c.r[26], c.r[27], c.r[29], c.r[30], c.r[31], ppc::mfcr(c));
  const auto snapshot = guest_heap_trace::inspect(handle, heap_trace_read32);
  log("[guest-heap] bounds=%08X..%08X first=%08X blocks=%u free=%08X largest_gap=%08X "
      "complete=%u error=%s", snapshot.lo, snapshot.hi, snapshot.first, (unsigned)snapshot.count,
      snapshot.free, snapshot.largest_gap, (unsigned)snapshot.complete, snapshot.error ? snapshot.error : "none");
  for (size_t i = 0; i < snapshot.count; ++i) {
    const auto& block = snapshot.blocks[i];
    log("[guest-heap] block=%u descriptor=%08X next=%08X address=%08X size=%08X",
        (unsigned)i, block.descriptor, block.next, block.address, block.size);
  }
  // Exact retail Allocator offsets from guest_002.cpp, not a host-native struct cast.
  for (uint32_t offset : {0u, 4u, 0x62Cu, 0x630u, 0x634u, 0x698u, 0x69Cu}) {
    uint32_t value = 0;
    const uint32_t address = 0x804318B0u + offset;
    if (heap_trace_read32(address, value)) log("[guest-heap] allocator+%03X=%08X", offset, value);
    else log("[guest-heap] allocator+%03X unreadable", offset);
  }
  log_flush();
}
} // namespace
void install_guest_heap_trace() {
  static bool installed = false;
  const char* requested = std::getenv("MELEE_TRACE_GUEST_HEAP");
  if (installed || !requested || std::strcmp(requested, "1") || !options.no_gc_adapter) return;
  installed = true;
  // enter() is present in compiled functions and interpret() entries, so this catches direct
  // compiled __assert calls. Entirely RAM-local branches can bypass entry hooks; the assert-site
  // register capture remains available if __assert itself reaches its known compiled entry.
  ppc::add_entry_hook(0x80014FC8u, record_guest_heap_call);
  ppc::add_entry_hook(0x80388220u, trace_guest_heap_assert);
  log("[guest-heap] opt-in retail allocator failure hooks installed; no allocation behavior changes");
}

void boot_setup() {
  install_guest_heap_trace();
  init_state_digest();
  if (!options.state_trace.empty()) {
    g_state_trace = std::fopen(options.state_trace.c_str(), "w");
    if (!g_state_trace) die("cannot open state trace");
    std::fprintf(g_state_trace, "retrace,cpu,ram,aram,events\n");
  }
  ram = (uint8_t*)std::calloc(ppc::RAM_SIZE + 64, 1);
  aram = (uint8_t*)std::calloc(0x01000000, 1);
  ax::set_memory({rd16, rd32, wr16, wr32, aram, 0x01000000});
  ax::set_voice_trace(std::getenv("MELEE_TRACE_AX_SFX") ? trace_ax_voice
                                                        : nullptr);
  ax::set_frame_trace(std::getenv("MELEE_TRACE_AX_VOICES") ? trace_ax_frame : nullptr);
  ax::reset();
  cpu = new ppc::Context();
  std::memset(cpu, 0, sizeof *cpu);
  if (!ram || !aram) die("out of memory");
  std::memset(g_mmio, 0, sizeof g_mmio);

  load_dol_from_disc();

  // Low memory, mirroring Dolphin's Boot_BS2Emu.cpp (GC path) plus what the apploader leaves.
  disc_read(0, ptr(0x80000000, 0x20), 0x20);              // disc id
  wr32(0x80000020, 0x0D15EA5E);                      // booted from bootrom
  wr32(0x80000028, ppc::RAM_SIZE);                   // physical memory size
  wr32(0x8000002C, 0x10000006);                      // console type (Dolphin reports devkit)
  wr32(0x80000030, 0);                               // arena lo (0 = use linker default)
  wr32(0x800000CC, 0);                               // NTSC
  wr32(0x800000D0, 0x01000000);                      // ARAM size
  wr32(0x800000F0, ppc::RAM_SIZE);                   // simulated memory size
  wr32(0x800000F8, 0x09A7EC80);                      // bus clock
  wr32(0x800000FC, 0x1CF7C580);                      // cpu clock
  wr32(0x80000300, 0x4C000064);                      // rfi stubs
  wr32(0x80000800, 0x4C000064);
  wr32(0x80000C00, 0x4C000064);
  const uint64_t tb = console_epoch_ticks();
  // Like Dolphin: the timebase register starts near zero; 0x800030D8 holds the epoch adjust
  // that __OSGetSystemTime adds to mftb.
  wr32(0x800030D8, (uint32_t)(tb >> 32));
  wr32(0x800030DC, (uint32_t)tb);
  cpu->tb = 0;

  // Apploader: FST at the top of RAM, arena hi below it.
  if (!valid_range(0, g_fst_max, ppc::RAM_SIZE) || g_fst_size > g_fst_max) die("invalid FST size");
  uint32_t fst_addr = (0x81800000u - g_fst_max) & ~31u;
  if (!disc_read(g_fst_offset, ptr(fst_addr, g_fst_size), g_fst_size)) die("cannot read FST");
  // Resolve logical asset paths against this exact ISO before the guest initializes DVD. Selected
  // entries keep their vanilla starts but receive their replacement lengths; DVDFileInfo reads are
  // then served by file identity, so a larger mod never aliases the next physical ISO file.
  cosmetics::apply_to_fst(ptr(fst_addr, g_fst_size), g_fst_size);
  cosmetics::set_online_probe([] { return ram != nullptr && rd8(0x80479D30) == 8; });
  wr32(0x80000038, fst_addr);
  g_fst_addr = fst_addr;
  wr32(0x8000003C, g_fst_max);
  const uint32_t gecko_bytes = user_gecko::static_memory_required();
  const uint32_t gecko_address = gecko_bytes ? (fst_addr - gecko_bytes) & ~0xFFFFu : 0;
  wr32(0x80000034, gecko_bytes ? gecko_address : fst_addr); // arena hi
  if (gecko_bytes) user_gecko::static_memory_start(gecko_address, fst_addr - gecko_address);
  log("boot: FST %u bytes at %08X (max %X), arena hi %08X", g_fst_size, fst_addr, g_fst_max,
      gecko_bytes ? gecko_address : fst_addr);
  install_gecko_boot();
  if (g_mod_disc) apply_mod_code();
  // MELEE_TEST_INTERPRET=<addr>[,<addr>...]: run those functions in the interpreter on any disc, so
  // interpreted and compiled behaviour can be compared on the vanilla game. Test runs only (hidden
  // and headless runs never open the GameCube adapter, which is what marks them here).
  if (const char* v = std::getenv("MELEE_TEST_INTERPRET"); v && options.no_gc_adapter) {
    for (const char* p = v; *p;) {
      char* end = nullptr;
      const uint32_t a = (uint32_t)std::strtoul(p, &end, 16);
      if (end == p) break;
      log("test: %08X %s %s", a, symbol_name(a), ppc::redirect_to_interpreter(a) ? "runs in the interpreter" : "could not be redirected");
      p = (*end == ',') ? end + 1 : end;
    }
  }

  cpu->msr = 0x00002030u | 0x8000u;                  // FP | DR | IR | EE
  cpu->fpscr = 0;
  ppc::update_mxcsr(*cpu);
  g_next_frame = std::chrono::steady_clock::now();
}

// ---------------- guest calls from host ----------------
static void (*g_after_guest_call)(uint32_t) = nullptr;
void call_guest(uint32_t addr, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6) {
  ppc::Context& c = *cpu;
  uint32_t saved_lr = c.lr;
  // A callback delivered at a poll (an interrupt on the console) may find the interrupted function
  // between saving its LR into its caller's frame (stw r0,4(r1)) and making its own frame (stwu).
  // The callback's prologue writes 4(r1) as well, so on the same r1 it overwrote that saved LR with 0
  // and the interrupted function returned to address 0 (the ACE sound crash). The console's interrupt
  // path gives handlers a frame of their own; so does this: r1 moves below a back-chained frame.
  const uint32_t saved_r1 = c.r[1];
  if (saved_r1 >= 0x80000100u && saved_r1 < 0x81800000u) {
    c.r[1] = (saved_r1 - 0x40u) & ~7u;
    wr32(c.r[1], saved_r1);
  }
  c.r[3] = r3; c.r[4] = r4; c.r[5] = r5; c.r[6] = r6;
  c.lr = 0;
  ppc::call(c, ram, addr);
  c.lr = saved_lr;
  c.r[1] = saved_r1;
  if (g_after_guest_call) g_after_guest_call(addr);
}
void set_after_guest_call(void (*hook)(uint32_t addr)) { g_after_guest_call = hook; }

// ---------------- events ----------------
void post_completion(Completion fn) { g_completions.push_back(std::move(fn)); }
void set_pe_finish_pending() { g_pe_finish_pending = true; }
// The file table's address for the host's own lookups. Says so once, and puts the pointer back,
// when low memory no longer holds it.
uint32_t disc_fst_addr() {
  if (!g_fst_addr) return rd32(0x80000038u);
  const uint32_t now = rd32(0x80000038u);
  if (now != g_fst_addr) {
    static bool told = false;
    if (!told) {
      told = true;
      log("low memory: the file table pointer at 80000038 was overwritten (now %08X, was %08X); put back", now, g_fst_addr);
    }
    wr32(0x80000038u, g_fst_addr);
  }
  return g_fst_addr;
}
void set_pe_token_pending(uint16_t token) { g_pe_token = token; g_pe_token_pending = true; }
bool exit_requested() { return g_exit; }
void request_exit(int code) { g_exit_code.store(code); g_exit.store(true); }
static std::atomic<bool> g_replay_viewing{false};
void set_replay_viewing(bool on) { g_replay_viewing.store(on); }
void request_user_exit() { request_exit(g_replay_viewing.load() ? kViewerClosedExit : 0); }
void request_restart() {
  // Start the replacement first and only then ask for a clean shutdown: if the launch fails there
  // is nothing to recover to, so the running game is left alone rather than closed into nothing.
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) { log("restart: cannot find this executable"); return; }
  std::wstring cmd = GetCommandLineW();
  STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
  // The new process must not inherit the window or the adapter, so it waits for this one to go.
  if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    log("restart: CreateProcess failed, error %lu", (unsigned long)GetLastError());
    return;
  }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  log("restart: relaunched, shutting down this instance");
  request_exit(0);
}
int exit_code() { return g_exit_code.load(); }
uint32_t retrace_count() { return g_retraces; }
uint32_t profiler_frame_id() { return g_profiler_frame.load(std::memory_order_relaxed); }
const std::vector<uint32_t>& slow_sim_frames() { return g_slow_sim_frames; }
// The VI retrace is periodic in virtual time, like the hardware interrupt: `g_next_retrace_tb`
// is the timebase value of the next retrace. A sleeping guest (wait_event) jumps time straight
// to that boundary; a guest that busy-waits with interrupts enabled advances time in small steps
// at HLE entry points and loop polls and takes the retrace when it crosses the boundary (Slippi's
// lag-reduction code waits for pad data that the retrace path produces).
static uint64_t g_next_retrace_tb = TB_PER_FRAME;
static bool g_in_retrace = false;
void (*native_retrace)() = nullptr;
void (*native_state_snapshot)(MuStatePod*) = nullptr;
bool retrace_due() { return cpu->tb >= g_next_retrace_tb && !g_in_retrace; }
uint64_t next_retrace_tb() { return g_next_retrace_tb; }
void advance_time(uint64_t ticks) { cpu->tb += ticks; }
static void advance_frame() {
  if (cpu->tb < g_next_retrace_tb) cpu->tb = g_next_retrace_tb;   // idle: jump to the boundary
  g_next_retrace_tb += TB_PER_FRAME;
}

void deliver_interrupt(uint32_t number) {
  // __OSInterruptHandlerTable lives at 0x80003040 (OS_INTERRUPTTABLE_ADDR).
  uint32_t handler = rd32(0x80003040u + number * 4);
  if (!handler) return;
  uint32_t context = rd32(0x800000D4u);  // OS current context (virtual address)
  ppc::Context& c = *cpu;
  ppc::Context saved = c;                // handlers clobber registers; restore like an rfi would
  bool was = g_in_interrupt;
  g_in_interrupt = true;
  try {
    call_guest(handler, number, context);
  } catch (const LoadContextUnwind&) {
  }
  g_in_interrupt = was;
  uint64_t tb = c.tb;
  c = saved;
  c.tb = tb;
  ppc::update_mxcsr(c);
}

static void fire_due_alarms(bool force);
static bool deliver_completions(bool force);

static void validate_alarm_queue(const char* where);
// Set when the game hands over the frame's picture (the display copy): from then until the retrace it
// is only waiting, so its polls may wait for real time without delaying any game work.
static bool g_frame_submitted = false;
void note_frame_submitted() { g_frame_submitted = true; }

// The game's own stack (64 KB on the console) running past its end. Calls that deep also run this
// program's stack out, so the reports of it are all the same exception C00000FD with thirty-two host
// frames and no word on what called itself: by then the game has written over its own data below
// the stack, and the last thing seen is a stray write. This notes the game's call chain the first
// time the stack pointer is found past the end (and again 128 KB further), which is what a later
// crash line needs above it. It changes nothing: the console overruns the same memory the same way.
static void note_guest_stack_overrun() {
  constexpr uint32_t kCurrentThread = 0x800000E4u, kStackBase = 0x304u, kStackEnd = 0x308u;
  static uint32_t noted_below = 0;   // how far past the end the last note was, +1
  if (native_retrace || !ram || !cpu) return;   // the Source Port's game runs on this program's stack
  const uint32_t thread = rd32(kCurrentThread);
  if (thread - ppc::RAM_BASE >= ram_size - 0x310u) return;
  // MELEE_TEST_GUEST_STACK_KB=<n> (tests): the stack counts as n KB, so ordinary calls run past it.
  static const uint32_t test_kb = [] { const char* v = std::getenv("MELEE_TEST_GUEST_STACK_KB"); return v ? (uint32_t)std::atoi(v) : 0u; }();
  const uint32_t base = rd32(thread + kStackBase), sp = cpu->r[1];
  const uint32_t end = test_kb ? base - test_kb * 1024u : rd32(thread + kStackEnd);
  if (end - ppc::RAM_BASE >= ram_size || base <= end || sp >= end || sp - ppc::RAM_BASE >= ram_size) return;
  const uint32_t past = end - sp;
  if (noted_below && past < noted_below - 1 + 128u * 1024u) return;
  noted_below = past + 1;
  // Walk the back chain: each frame holds the caller's frame address, and the caller's return address beside it.
  struct Count { uint32_t function; uint32_t calls; };
  std::vector<Count> counts;
  std::string inner, outer;
  std::vector<uint32_t> chain;
  for (uint32_t at = sp; chain.size() < 100000;) {
    const uint32_t up = rd32(at);
    if (up <= at || up - ppc::RAM_BASE >= ram_size - 8u) break;
    chain.push_back(rd32(up + 4));
    at = up;
  }
  auto function_of = [](uint32_t lr) {
    size_t lo = 0, hi = guest::name_table_count;
    while (lo < hi) { const size_t mid = (lo + hi) / 2; if (guest::name_table[mid].addr <= lr) lo = mid + 1; else hi = mid; }
    return lo ? guest::name_table[lo - 1].addr : 0u;
  };
  for (uint32_t lr : chain) {
    const uint32_t f = function_of(lr);
    auto it = std::find_if(counts.begin(), counts.end(), [&](const Count& c) { return c.function == f; });
    if (it == counts.end()) counts.push_back({f, 1}); else ++it->calls;
  }
  std::sort(counts.begin(), counts.end(), [](const Count& a, const Count& b) { return a.calls > b.calls; });
  std::string most;
  for (size_t i = 0; i < counts.size() && i < 6; ++i) { char t[96]; std::snprintf(t, sizeof t, "%s%s x%u", i ? ", " : "", symbol_name(counts[i].function), counts[i].calls); most += t; }
  for (size_t i = 0; i < chain.size() && i < 10; ++i) { inner += i ? " < " : ""; inner += symbol_name(chain[i]); }
  for (size_t i = chain.size() > 6 ? chain.size() - 6 : 0; i < chain.size(); ++i) { outer += outer.empty() ? "" : " < "; outer += symbol_name(chain[i]); }
  uint32_t major = 0, minor = 0, match_frame = 0;
  current_scene(&major, &minor, &match_frame);
  log("stack: the game's own stack is %u KB past its end (%u KB of stack, %zu calls deep, scene %02X:%02X, retrace %u). Most repeated: %s",
      past / 1024, (base - end) / 1024, chain.size(), major, minor, g_retraces, most.c_str());
  log("stack: innermost calls: %s | outermost: %s", inner.c_str(), outer.c_str());
  log_flush();
}

void pump_completions() {
  // Called from HLE entry points the guest polls. Virtual time flows a little so periodic
  // alarms (pad sampling) fire even in loops that never sleep. Nothing is delivered while the
  // guest has interrupts disabled; ppc::mtmsr flushes when they come back on.
  note_guest_stack_overrun();
  advance_time(2048);
  hle::dvd_poll();
  validate_alarm_queue("hle entry");
  if (!ppc::interrupts_on(*cpu)) return;
  fire_due_alarms(false);
  // Slippi's lag reduction busy-waits for the retrace with interrupts on, and each poll moves
  // console time on: without this, time raced ahead of the clock and a frame's audio blocks went out
  // together. Once the frame is submitted, a block that is due waits for its real time (the AI
  // interrupt's 5 ms rhythm on the console). Game work before the display copy is never delayed.
  if (g_frame_submitted && !options.fast) {
    static const bool pace = [] { const char* v = std::getenv("MELEE_AUDIO_PACING"); return !(v && *v == '0'); }();
    const uint64_t due = hle::audio_next_due();
    if (pace && due && cpu->tb >= due && due < g_next_retrace_tb) wait_until_console_time(due);
  }
  hle::audio_tick(false);
  deliver_completions(false);
  if (cpu->tb >= g_next_retrace_tb && !g_in_retrace) retrace();   // periodic VI interrupt during busy waits
}

// ---- audio blocks at their 5 ms times during the frame wait (Static Recomp) ----
// The scene loop waits for the next frame by asking lb_80019894 how many pad samples are queued and
// polling until there is one. On the console the audio interface interrupt fires every 5 ms during
// that wait and the DSP renders one block each time; here console time used to run straight to the
// retrace in small poll steps, so a frame's three or four blocks went out together (a ~17 ms burst
// the output buffer had to absorb, the main cost in sound latency). While the answer is "none yet",
// wait for the next block's real time and play it; the retrace, the pad sample and frame timing are
// unchanged. The Source Port does the same in its scene loop (mu_audio_idle).
}  // namespace host
namespace hle { uint64_t audio_next_due(); }
namespace host {
namespace {
ppc::Fn g_pad_queue_count = nullptr;
void pad_queue_count_paced(ppc::Context& c, uint8_t* m) {
  g_pad_queue_count(c, m);
  static const bool enabled = [] { const char* v = std::getenv("MELEE_AUDIO_PACING"); return !(v && *v == '0'); }();
  static const bool trace = [] { const char* v = std::getenv("MELEE_AUDIO_PACING_TRACE"); return v && *v == '1'; }();
  static uint64_t calls, has_pad, irq_off, no_due, past, steps;
  if (trace && ++calls % 600 == 0)
    log("audio pacing (recomp): %llu calls, %llu steps; pad queued %llu, interrupts off %llu, not due/none %llu, past retrace %llu",
        (unsigned long long)calls, (unsigned long long)steps, (unsigned long long)has_pad, (unsigned long long)irq_off,
        (unsigned long long)no_due, (unsigned long long)past);
  if (c.r[3] != 0) { ++has_pad; return; }
  if (!enabled || options.fast) return;
  if (!ppc::interrupts_on(c)) { ++irq_off; return; }
  const uint64_t due = hle::audio_next_due();
  if (!due || c.tb >= due) { ++no_due; return; }
  if (!wait_until_console_time(due)) { ++past; return; }   // due after the retrace: the retrace wait covers it
  hle::audio_tick(false);
  ++steps;
}
}  // namespace
ppc::Fn g_idle_poll = nullptr;
// lb_800195D0: the scene loop's idle poll. With Slippi's codes the wait loop around it is theirs, so
// pace here: when no pad sample is queued yet (the game is waiting), play the next block at its time.
void idle_poll_paced(ppc::Context& c, uint8_t* m) {
  static const bool trace = [] { const char* v = std::getenv("MELEE_AUDIO_PACING_TRACE"); return v && *v == '1'; }();
  static uint64_t calls;
  if (trace && ++calls % 600 == 0) log("audio pacing (recomp): idle poll %llu calls", (unsigned long long)calls);
  if (g_pad_queue_count) {
    const ppc::Context saved = c;
    g_pad_queue_count(c, m);          // how many pad samples are queued (reads only)
    const uint32_t queued = c.r[3];
    const uint64_t tb = c.tb;
    c = saved; c.tb = tb;
    if (queued == 0) {
      static const bool enabled = [] { const char* v = std::getenv("MELEE_AUDIO_PACING"); return !(v && *v == '0'); }();
      const uint64_t due = hle::audio_next_due();
      if (enabled && !options.fast && ppc::interrupts_on(c) && due && c.tb < due && wait_until_console_time(due))
        hle::audio_tick(false);
    }
  }
  g_idle_poll(c, m);
}
// OSGetTime is the console's time since 2000-01-01: the boot ROM loads the timebase register from
// the clock before a game starts. Here the timebase starts at zero and the date sits in the OS
// adjust at 0x800030D8, which only __OSGetSystemTime adds. So everything that turned OSGetTime
// into a date read January 1, 2000 at every start: the date in the save file's comment, and a mod
// stage that shows a clock or changes with the season.
// Every caller gets the dated time now, except __OSGetSystemTime (8034C410..8034C474), which adds
// the adjust itself. The same three timebase reads as the game's function, so emulated time
// advances exactly as it did; differences between two readings are unchanged.
static void os_get_time_dated(ppc::Context& c, uint8_t*) {
  ppc::enter(c, 0x8034C3F0u);
  uint32_t hi, lo, again;
  do {
    hi = (uint32_t)(ppc::read_tb(c) >> 32);
    lo = (uint32_t)ppc::read_tb(c);
    again = (uint32_t)(ppc::read_tb(c) >> 32);
  } while (hi != again);
  ppc::cr_set_s(c, 0, (int32_t)hi, (int32_t)again);
  uint64_t time = ((uint64_t)hi << 32) | lo;
  if (c.lr < 0x8034C410u || c.lr >= 0x8034C474u)
    time += ((uint64_t)rd32(0x800030D8u) << 32) | rd32(0x800030DCu);
  c.r[3] = (uint32_t)(time >> 32); c.r[4] = (uint32_t)time; c.r[5] = again;
}
// The console clock when the game starts, in timebase ticks since 2000-01-01 (--time-base presets
// it, which is what keeps a test run repeatable). Dolphin presets the timebase from the RTC the
// same way. The console's clock holds local time, so this is the local wall-clock reading taken as
// if it were UTC: a stage that shows the time of day, or a save's date, reads it from here.
uint64_t console_epoch_ticks() {
  static const uint64_t value = [] {
    if (options.time_base) return options.time_base;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    const long long local_secs = _mkgmtime64(&local);
    const long long GC_EPOCH = 946684800ll;
    return (uint64_t)(local_secs > GC_EPOCH ? local_secs - GC_EPOCH : 0) * TB_HZ;
  }();
  return value;
}
void install_console_clock() {
  if (!ppc::redirect_to_host(0x8034C3F0u, os_get_time_dated)) log("clock: OSGetTime keeps the time since start (not replaced)");
}

// ---- the game's language as a PC setting ----
std::atomic<int> g_game_language{0};
std::atomic<bool> g_cpu_20xx{false};   // "20XX CPUs", see hackpack_ai.cpp
int game_language_override() {
  const int choice = g_game_language.load(std::memory_order_relaxed);
  return choice == 1 ? 0 : choice == 2 ? 1 : -1;
}
// Static Recomp. The game reads its saved language through three small functions (lblanguage.c:
// lbLang_GetSavedLanguage, lbLang_IsSavedLanguageJP, lbLang_IsSavedLanguageUS). Each is replaced by a
// host function that answers with the player's choice, or reads the save exactly as the original
// does: GamePrefs.saved_language, the byte at +0x16 of the preferences at +0x1CB0 of the save data
// (r13 - 0x77C0 holds its address).
static uint32_t saved_language(ppc::Context& c) {
  const int forced = game_language_override();
  if (forced >= 0) return (uint32_t)forced;
  const uint32_t save = rd32(c.r[13] - 0x77C0u);
  if (!try_ptr(save + 0x1CB0u + 0x16u, 1)) return 1;   // before the save data exists: English, as the disc sets it
  return rd8(save + 0x1CB0u + 0x16u);
}
static void lang_get_saved(ppc::Context& c, uint8_t*) { c.r[3] = saved_language(c); }
static void lang_is_saved_jp(ppc::Context& c, uint8_t*) { c.r[3] = saved_language(c) == 0 ? 1u : 0u; }
static void lang_is_saved_us(ppc::Context& c, uint8_t*) { c.r[3] = saved_language(c) == 1 ? 1u : 0u; }
// The title demo preloads four fighters and a stage into a heap of a fixed size (heap 4,
// 0x64B400 bytes), sized for the original files: the largest original set leaves about 113 KB.
// A stage skin a megabyte or more larger than the file it replaces no longer fits with most
// fighter draws, and the game stops with lbmemory.c:233 ("memp_kouho") on the title screen.
// gm_801BF684 (801BF684) is where gm_SetupTitleDemo stores the stage it drew (a stage kind, u16 at
// 8049E554), after the fighters. When that stage's file cannot fit beside the largest original
// fighter set plus the growth of the installed fighter skins, another stage of the game's own
// demo list (803B7808, 0x1D entries) that does fit is stored in its place. With original files
// every stage fits, so nothing changes for a game without large skins.
static int32_t fst_find_path(const char* path);
static uint32_t demo_stage_file_size(uint32_t stkind) {
  if (stkind >= 0x11Eu) return 0;                              // stage_id_map has 0x11E entries of 12 bytes
  const uint32_t grkind = rd32(0x803E9960u + stkind * 12);
  if (grkind >= 0x6Fu) return 0;                               // stage_datas: 0x1BC bytes of pointers
  const uint32_t data = rd32(0x803DFEDCu + grkind * 4);
  if (!try_ptr(data, 12)) return 0;
  const uint32_t name = rd32(data + 8);
  if (!try_ptr(name, 1)) return 0;
  const std::string path = cstr(name, 64);
  const int32_t entry = fst_find_path(path.c_str());
  if (entry <= 0) return 0;
  return rd32(disc_fst_addr() + (uint32_t)entry * 12 + 8);
}
static void title_demo_store_stage(ppc::Context& c, uint8_t*) {
  constexpr uint32_t kHeap = 0x64B400u, kOriginalFighters = 4170176u, kStageSlot = 0x8049E554u;
  uint32_t stage = c.r[3] & 0xFFFFu;
  // MELEE_TEST_DEMO_GROWTH=<bytes>: test runs only (hidden runs never open the GameCube adapter),
  // stands in for installed skins that much larger than their originals.
  static const uint32_t test_growth = [] {
    const char* v = std::getenv("MELEE_TEST_DEMO_GROWTH");
    return v && options.no_gc_adapter ? (uint32_t)std::strtoul(v, nullptr, 0) : 0u;
  }();
  const uint64_t fighters = (uint64_t)kOriginalFighters + cosmetics::largest_fighter_growth() + test_growth;
  const auto fits = [&](uint32_t kind) {
    const uint32_t size = demo_stage_file_size(kind);
    return size != 0 && ((uint64_t)((size + 31u) & ~31u) + 0x60u + fighters) <= kHeap;
  };
  // A mod disc has its own stage tables (and its own guard): the stage is stored as drawn.
  if (!g_mod_disc && demo_stage_file_size(stage) != 0 && !fits(stage)) {
    for (uint32_t i = 0; i < 0x1Du; ++i) {
      const uint32_t other = rd16(0x803B7808u + i * 2);
      if (other == stage || !fits(other)) continue;
      static bool told = false;
      if (!told) {
        told = true;
        log("mods: the title demo drew a stage whose skin does not fit its memory with the fighters; another stage plays in the demo");
      }
      stage = other;
      break;
    }
  }
  wr16(kStageSlot, (uint16_t)stage);
}
void install_language_override() {
  if (!ppc::redirect_to_host(0x801BF684u, title_demo_store_stage))
    log("mods: the title demo's stage store could not be replaced; large stage skins can stop the title screen");
  const bool get = ppc::redirect_to_host(0x8000ADF4u, lang_get_saved);
  const bool jp = ppc::redirect_to_host(0x8000AE58u, lang_is_saved_jp);
  const bool us = ppc::redirect_to_host(0x8000AE90u, lang_is_saved_us);
  if (!get || !jp || !us) log("language: the saved-language functions could not be replaced; the game's own choice applies");
}
// True 16:9 on the Static Recomp. The renderer shows more to each side, but the game's draw test
// (ftLib_80086A8C) still answers for its own 73:60 camera, so a fighter in the added sides was not
// drawn at all. Slippi's widescreen code changes one instruction of that test ("Draw High Poly
// Models", 80086B24: the branch that returns "do not draw" becomes a nop) and nothing else in it:
// the on-screen flag, the bubble and everything else the game reads are set before that branch.
// That instruction is compiled to follow the Slippi option, which also widens the cameras, so here
// the same change is made in RAM and the function runs from there (a few calls per frame).
// Only when the instruction is the game's own: a mod that changed this function is left alone.
void clean_mode_music_frame();
// The two switches over lines of Slippi's code set that the game was translated both ways for
// (recomp/gecko.py: TWO_WAY_TEXT and the "Results Screen Offline" port codes). A network session,
// from matchmaking to the end of the match, always plays as Slippi does: everything unlocked and no
// results screen. Offline the player's "Unlock everything" setting decides the first, and the
// results screen is shown unless the player turned "Show results screen" off.
static void apply_code_switches() {
  const bool online = slippi::online::session_mode() >= 0;
  gecko::option_unlock_all = online || gx::RenderOptions::live_unlock_all();
  gecko::option_offline_results = !online && gx::RenderOptions::live_results_screen();
}
void apply_wide_fighter_draw() {
  apply_code_switches();
  clean_mode_music_frame();
  constexpr uint32_t kSite = 0x80086B24u, kBranch = 0x4182000Cu, kNop = 0x60000000u;
  static bool live = false;
  const bool want = gx::true_widescreen_active() && !gecko::option_widescreen;
  if (want == live || !ram) return;
  if (want) {
    if (rd32(kSite) != kBranch) return;   // not the game's own code here
    wr32(kSite, kNop);
    if (!ppc::redirect_function_at(kSite)) { wr32(kSite, kBranch); return; }
    live = true;
    log("widescreen: fighters in the added sides are drawn (True 16:9)");
  } else {
    if (rd32(kSite) == kNop) wr32(kSite, kBranch);
    live = false;
  }
}
// "Low poly fighters" on the Static Recomp. The game's fighter draw (ftDrawCommon_800805C8) picks
// the model with four calls to ftParts_800750C8(fp, table, show) at 80080A54..80080ACC: table 1
// (the far, low-polygon model) hidden, table 4 hidden, then table 2 (metal) hidden and table 0 (the
// full model) shown, or 0 hidden and 2 shown for a metal fighter. The decision is in those immediate
// operands, not in data, so the Source Port swaps tables 0 and 1 in C (ftdrawcommon.c) and here the
// same swap is made in front of the parts function: a dispatch-table hook that, for a call returning
// into that range (the link register), with the option on and the fighter owning a far model
// (x5AC.xC[1], fp + 0x5BC), exchanges 0 and 1 and then runs the compiled original. Compiled code
// calls its functions directly and never sees the table, so while the option is on the draw function
// runs from its own unchanged RAM bytes (the widescreen pattern above), whose calls do. Every other
// caller (the reflection, shadow and magnifier passes, which draw the far model already) is passed
// through untouched, and so is everything while the option is off. No RAM word is written. The hook
// changes DObj hidden flags only, which nothing but the draw reads: no simulation state, replay
// digest or online state moves.
static ppc::Fn g_parts_show_original = nullptr;
static void parts_show_low_poly(ppc::Context& c, uint8_t* m) {
  constexpr uint32_t kFirstReturn = 0x80080A58u, kLastReturn = 0x80080AD0u;
  if (gx::low_poly_fighters_active() && c.lr >= kFirstReturn && c.lr <= kLastReturn && c.r[4] <= 1u) {
    const uint32_t fp = c.r[3];
    if (try_ptr(fp + 0x5BCu, 4) && rd32(fp + 0x5BCu) != 0) c.r[4] ^= 1u;
  }
  g_parts_show_original(c, m);
}
void install_low_poly_fighters() {
  constexpr uint32_t kPartsShow = 0x800750C8u;
  if (g_parts_show_original) return;
  g_parts_show_original = ppc::set_hook(kPartsShow, parts_show_low_poly);
  if (!g_parts_show_original) {
    ppc::set_hook(kPartsShow, nullptr);   // never leave a hook with nothing to call behind it
    log("low poly: the fighter parts function has no dispatch entry; the option has no effect");
  }
}
// MELEE_TEST_ADVENTURE_SCENE=<decimal scene id> (tests only, Static Recomp): Adventure starts at
// that scene (stage * 8 + part: 25 is the Zelda fight, 33 the Kirby team), as the Source Port's hook
// of the same name does, so a hidden run can reach a later stage. The game's own start function
// (gm_801B4350) is run from RAM so that its call to gm_SetNextGameModeStateId goes through the
// dispatch table, and the id is replaced there. Nothing is installed without the variable.
static ppc::Fn g_set_next_scene_original = nullptr;
static int g_test_adventure_scene = -1;
static void set_next_scene_test(ppc::Context& c, uint8_t* m) {
  constexpr uint32_t kAdventureStart = 0x801B4350u, kAdventureStartEnd = 0x801B4408u;
  if (g_test_adventure_scene >= 0 && c.lr >= kAdventureStart && c.lr < kAdventureStartEnd) c.r[3] = (uint32_t)g_test_adventure_scene;
  g_set_next_scene_original(c, m);
}
static void apply_test_adventure_scene() {
  constexpr uint32_t kSetNextScene = 0x801A42A0u, kAdventureStart = 0x801B4350u;
  static bool tried = false;
  if (tried || !ram) return;
  tried = true;
  const char* v = std::getenv("MELEE_TEST_ADVENTURE_SCENE");
  if (!v || !*v) return;
  g_set_next_scene_original = ppc::set_hook(kSetNextScene, set_next_scene_test);
  if (!g_set_next_scene_original) { ppc::set_hook(kSetNextScene, nullptr); log("test: the scene function has no dispatch entry"); return; }
  if (!ppc::redirect_to_interpreter(kAdventureStart)) { log("test: the Adventure start could not be run from RAM"); return; }
  g_test_adventure_scene = std::atoi(v);
  log("test: Adventure starts at scene %d", g_test_adventure_scene);
}
// "Always show player tags": the game's own "show every tag" bytes (if/ifnametag.c un_804D6D70, one
// per player, which un_802FD404 sets for the modes that always show them) are held at 1 while the
// option is on, and given back as 0 once when it goes off. Display only: nothing in the match reads
// them. Retail game only, a mod disc has its own memory layout.
void apply_player_tags_always() {
  constexpr uint32_t kShowAllTags = 0x804D6D70u;
  static bool held = false;
  if (!ram || mod_disc_active()) return;
  const bool on = gx::player_tags_always_active();
  if (!on && !held) return;
  for (uint32_t i = 0; i < 6; ++i) wr8(kShowAllTags + i, on ? 1 : 0);
  held = on;
}
void apply_low_poly_fighters() {
  apply_test_adventure_scene();
  constexpr uint32_t kFighterDraw = 0x800805C8u;
  static bool live = false;
  if (live || !ram || !g_parts_show_original || !gx::low_poly_fighters_active()) return;
  // Once from RAM, it stays there (a few hundred instructions per fighter draw): switching the option
  // off makes the hook pass every call through, which is the game's own picture again.
  if (!ppc::redirect_to_interpreter(kFighterDraw)) { log("low poly: the fighter draw could not be run from RAM; the option has no effect"); gx::set_low_poly_fighters(false); return; }
  live = true;
  log("low poly: fighters draw with the game's far models");
}
// A mod disc can ask for a file it does not ship (ACE's results screen asks for /audio/ff_step1.hps
// for one of its fighters). The game then opens entry -1, DVDFastOpen refuses and leaves the file
// information of whatever was opened last, and the music stream reads its header out of that file:
// junk, which stops the game. Only the music stream opens a file without checking its name first, so
// in a mod session an entry outside the table opens a short victory tune the disc does have
// (ff_good.hps, else the first ff_*.hps): the wrong tune instead of a stop.
void dvd_fast_open_checked(ppc::Context& c, uint8_t*) {
  uint32_t entry = c.r[3];
  const uint32_t info = c.r[4];
  const uint32_t fst = disc_fst_addr();
  const uint32_t count = fst ? rd32(fst + 8) : 0;
  auto is_file = [&](uint32_t e) { return e < count && rd8(fst + e * 12) == 0; };
  if (!is_file(entry) && (int32_t)entry < 0) {
    static uint32_t fallback = 0;
    if (!fallback) {
      const uint32_t strings = fst + count * 12;
      for (uint32_t e = 1; e < count; ++e) {
        if (rd8(fst + e * 12)) continue;
        const char* name = (const char*)try_ptr(strings + (rd32(fst + e * 12) & 0x00FFFFFFu), 16);
        if (!name || std::strncmp(name, "ff_", 3) != 0 || !std::strstr(name, ".hps")) continue;
        if (!fallback) fallback = e;
        if (!std::strcmp(name, "ff_good.hps")) { fallback = e; break; }
      }
      log("mods: the game asked for a music file this disc does not have; %s", fallback ? "another tune plays in its place" : "no tune to play instead");
    }
    if (fallback) entry = fallback;
  }
  if (!is_file(entry)) { c.r[3] = 0; return; }   // as the game's own code: refused, information untouched
  wr32(info + 0x30, rd32(fst + entry * 12 + 4));   // startAddr
  wr32(info + 0x34, rd32(fst + entry * 12 + 8));   // length
  wr32(info + 0x38, 0);                            // callback
  wr32(info + 0x0C, 0);                            // cb.state
  c.r[3] = 1;
}
// The game's own name search (DVDConvertPathToEntrynum), rule for rule, without the 8.3 name check:
// from the root or the given directory, one path part at a time, letters compared without case.
static int32_t fst_find_path(const char* path) {
  const uint32_t fst = disc_fst_addr();
  const uint32_t count = fst ? rd32(fst + 8) : 0;
  if (!count) return -1;
  const uint32_t strings = fst + count * 12;
  auto is_dir = [&](uint32_t e) { return rd8(fst + e * 12) != 0; };
  auto next_of = [&](uint32_t e) { return rd32(fst + e * 12 + 8); };
  uint32_t dir = 0;
  for (;;) {
    if (*path == '\0') return (int32_t)dir;
    if (*path == '/') { dir = 0; ++path; continue; }
    if (*path == '.') {
      if (path[1] == '.') {
        if (path[2] == '/') { dir = rd32(fst + dir * 12 + 4); path += 3; continue; }
        if (path[2] == '\0') return (int32_t)rd32(fst + dir * 12 + 4);
      } else if (path[1] == '/') { path += 2; continue; }
      else if (path[1] == '\0') return (int32_t)dir;
    }
    const char* end = path;
    while (*end != '\0' && *end != '/') ++end;
    const bool want_dir = *end != '\0';
    uint32_t found = 0;
    const uint32_t stop = std::min(next_of(dir), count);
    for (uint32_t e = dir + 1; e < stop; e = is_dir(e) ? std::max(next_of(e), e + 1) : e + 1) {
      if (!is_dir(e) && want_dir) continue;
      const char* name = (const char*)try_ptr(strings + (rd32(fst + e * 12) & 0x00FFFFFFu), 1);
      if (!name) continue;
      const char* p = path;
      while (*name != '\0' && std::tolower((unsigned char)*p) == std::tolower((unsigned char)*name)) { ++p; ++name; }
      if (*name == '\0' && (*p == '/' || *p == '\0')) { found = e; break; }
    }
    if (!found) return -1;
    if (!want_dir) return (int32_t)found;
    dir = found;
    path = end + 1;
  }
}
// Where a code list's branch at `at` leads (the code it put in place of that instruction), or 0 when
// the instruction there is not a plain branch.
static uint32_t mod_code_behind(uint32_t at) {
  const uint32_t first = rd32(at);
  if ((first & 0xFC000003u) != 0x48000000u) return 0;
  const uint32_t code = at + (uint32_t)((int32_t)(first << 6) >> 6);
  return code >= 0x80000000u && code < 0x81800000u ? code : 0;
}
// m-ex replaces gm_80160438 (the results animation file of a fighter) with a read of its own table:
//   lwz r4,N(r2) ; mulli r3,r3,4 ; lwzx r3,r3,r4 ; blr
// A row can name a file the disc does not have (ACE 2.0.0: Crazy Hand's row names GmRstMGk.dat). The game
// itself has no file for such a fighter and its results screen then shows no model for it (a NULL row).
// Rows without a file are cleared when the results screen opens its own file, before any fighter is
// asked for. Nothing is read or written unless that exact code is behind the function's entry, so the
// retail game and a mod without this table are left alone. A cleared row is skipped from then on.
//
// A row can also name a file the disc has for a fighter that has no results animation in any file
// (ACE 2.0.0: Master Hand's row names Mario's file). m-ex then looks up the fighter's symbol name, an
// empty string, and stops ("fighter N has no symbol"). Such a row is cleared too, but only when both of
// m-ex's reads are confirmed the same way: the fighter id table (Player_80036E20: lwz r3,8(r2)) and the
// symbol name table with its stop (ftDemo_SetArchiveData: lwz r4,0x78(r2)).
static void clear_result_rows_without_file(ppc::Context& c) {
  auto in_ram = [](uint32_t a, uint32_t n) { return a >= 0x80000000u && a < 0x81800000u && try_ptr(a, n) != nullptr; };
  const uint32_t code = mod_code_behind(0x80160438u);
  if (!code || !in_ram(code, 16) || (rd32(code) & 0xFFFF0000u) != 0x80820000u || rd32(code + 4) != 0x1C630004u ||
      rd32(code + 8) != 0x7C63202Eu || rd32(code + 12) != 0x4E800020u) return;
  const uint32_t holder = c.r[2] + (uint32_t)(int32_t)(int16_t)(rd32(code) & 0xFFFFu);
  if (!in_ram(holder, 4)) return;
  const uint32_t table = rd32(holder);
  // The fighter id of a row (3 bytes a row, the id first) and the symbol names of a fighter (4 a fighter,
  // the results one first), both 0 unless m-ex's code for them is in place.
  uint32_t ids = 0, symbols = 0;
  const uint32_t id_code = mod_code_behind(0x80036E34u), symbol_code = mod_code_behind(0x800BEBC8u);
  if (id_code && in_ram(id_code, 8) && rd32(id_code) == 0x80620008u && rd32(id_code + 4) == 0x3803FFE0u &&
      rd32(0x80036E24u) == 0x1CC30003u && rd32(0x80036E3Cu) == 0x7FE03214u && rd32(0x80036E50u) == 0x887F0020u &&
      symbol_code && in_ram(symbol_code, 0x48) && rd32(symbol_code + 0x10) == 0x7C7D1B78u &&
      rd32(symbol_code + 0x18) == 0x80820078u && rd32(symbol_code + 0x1C) == 0x1C1D0004u &&
      rd32(symbol_code + 0x20) == 0x7C84002Eu && rd32(symbol_code + 0x24) == 0x1C050004u &&
      rd32(symbol_code + 0x28) == 0x7F84002Eu && rd32(symbol_code + 0x40) == 0x2C030000u &&
      rd32(symbol_code + 0x44) == 0x4182000Cu && in_ram(c.r[2] + 8u, 4) && in_ram(c.r[2] + 0x78u, 4)) {
    ids = rd32(c.r[2] + 8u);
    symbols = rd32(c.r[2] + 0x78u);
  }
  for (uint32_t row = 0; row < 256; ++row) {
    if (!in_ram(table + row * 4, 4)) break;
    const uint32_t at = rd32(table + row * 4);
    if (!at) continue;
    if (!in_ram(at, 16)) break;
    const char* name = (const char*)try_ptr(at, 16);
    if (!*name) continue;                                                    // a fighter without an entry
    if (_strnicmp(name, "GmRstM", 6) != 0 || !std::memchr(name, '\0', 16)) break;   // past the table
    if (fst_find_path(name) < 0) {
      log("mods: results table row for character %u named a file this disc does not have (%s); cleared", row, name);
      wr32(table + row * 4, 0);
      continue;
    }
    if (!ids || !symbols || !in_ram(ids + row * 3, 1)) continue;
    const uint32_t fighter = rd8(ids + row * 3);
    if (fighter >= 0x80u || !in_ram(symbols + fighter * 4, 4)) continue;
    const uint32_t names = rd32(symbols + fighter * 4);
    if (!in_ram(names, 4) || !in_ram(rd32(names), 1) || rd8(rd32(names)) != 0) continue;
    log("mods: results table row for character %u named a file (%s) for fighter %u, which has no results animation in any file; cleared", row, name, fighter);
    wr32(table + row * 4, 0);
  }
}
// A mod can send the results screen for a fighter that has no results animation on any disc (Master
// Hand, picked from a mod's debug menu, wins a match: GmRstMMh.dat). The game stops on the missing
// file. In a mod session a results animation the disc does not have opens as Mario's instead.
void dvd_convert_path_checked(ppc::Context& c, uint8_t*) {
  const char* path = (const char*)try_ptr(c.r[3], 1);
  if (!path) { c.r[3] = 0xFFFFFFFFu; return; }
  {
    // The results screen asks for its own file before any fighter's (see clear_result_rows_without_file).
    const char* slash0 = std::strrchr(path, '/');
    const char* base0 = slash0 ? slash0 + 1 : path;
    if (!_stricmp(base0, "GmRst.usd") || !_stricmp(base0, "GmRst.dat")) clear_result_rows_without_file(c);
  }
  int32_t entry = fst_find_path(path);
  if (entry < 0) {
    const char* slash = std::strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    if (std::strlen(base) == 12 && !_strnicmp(base, "GmRstM", 6) && !_stricmp(base + 8, ".dat")) {
      entry = fst_find_path("GmRstMMr.dat");
      static bool told = false;
      if (!told && entry >= 0) { told = true; log("mods: the game asked for a results animation this disc does not have (%s); another fighter's plays in its place", base); }
    }
  }
  c.r[3] = (uint32_t)entry;
}
// Clean mode (a mod disc that runs without Slippi's codes) has none of the code that hands the music
// to the host's player, and the game's own disc stream does not advance here: every song repeated
// its first 1.8 seconds. The pack replaces the function that starts a stream with its own code, so
// nothing is hooked. Once per game frame the host reads the game's own bookkeeping: the id of the
// music stream (HSD_Synth_804D7760) and the disc entry it opened (HSD_Synth_804D7764). A new stream
// starts that file in the player Slippi's codes use, the game's own stream voice is kept silent
// (its volumes in the voice table, hsd_SynthSFXNodes), and the player stops when the stream is gone.
// The level follows the game's music level, the value Slippi's volume code sends
// (lbl_804D38AC * lbl_804D3884 * 2).
static std::atomic<bool> g_clean_music = false;
void install_clean_mode_music() { g_clean_music.store(true); }
void clean_mode_music_frame() {
  if (!g_clean_music.load(std::memory_order_relaxed)) return;
  static uint32_t playing_id = 0;
  const uint32_t id = rd32(0x804D7760u);
  const uint32_t node = 0x804C2C64u + (id & 0x3Fu) * 0x50u;
  const bool live = (int32_t)id > 0 && rd32(node) == id;
  if (!live) {
    if (playing_id) { slippi::jukebox::stop(); playing_id = 0; }
    return;
  }
  // MELEE_TRACE_CLEAN_MUSIC (tests only): what became of the previous stream's voice slot two
  // seconds after the game replaced it. A slot still holding the old id was never freed.
  static const bool trace = std::getenv("MELEE_TRACE_CLEAN_MUSIC") != nullptr;
  static uint32_t watched_id = 0, watched_frames = 0;
  if (trace && watched_id && ++watched_frames == 120) {
    const uint32_t old_node = 0x804C2C64u + (watched_id & 0x3Fu) * 0x50u;
    log("mods: music stream %08X two seconds after it was replaced: slot holds %08X, flags %02X, pending %u (%s)", watched_id,
        rd32(old_node), rd8(old_node + 0x09), rd8(old_node + 0x26), rd32(old_node) == watched_id ? "NOT freed" : "freed");
    watched_id = 0;
  }
  if (id != playing_id) {
    if (trace && playing_id) { watched_id = playing_id; watched_frames = 0; }
    const uint32_t entry = rd32(0x804D7764u), fst = disc_fst_addr();
    const uint32_t count = fst ? rd32(fst + 8) : 0;
    if (entry > 0 && entry < count && rd8(fst + entry * 12) == 0) {
      slippi::jukebox::start_song(rd32(fst + entry * 12 + 4), rd32(fst + entry * 12 + 8));
      static bool told = false;
      if (!told) { told = true; log("mods: this disc's music plays through the host's player"); }
      const char* name = (const char*)try_ptr(fst + count * 12 + (rd32(fst + entry * 12) & 0x00FFFFFFu), 1);
      log("mods: music stream %08X starts %s (entry %u, %u bytes)", id, name ? name : "?", entry, rd32(fst + entry * 12 + 8));
    }
    playing_id = id;
  }
  // The stream voice itself stays silent: its three volume factors, then the node is queued for the
  // sound driver's volume pass exactly as the game queues one (HSD_SynthSFXUpdateVolume): linked
  // into the list at HSD_Synth_804D774C with its pending byte set. The byte means "already on the
  // list". Setting it alone, as this did, kept the node off the list for good, and the driver's
  // key-off of that voice goes through the same list: the slot was never faded or freed, and a
  // sound that later landed in it could not be stopped until the game closed (reported as one
  // sound repeating after a few matches). A stream still loading (flag 8) queues itself when its
  // first data arrives and reads the zeroed factors then.
  if (rd32(node + 0x28) | rd32(node + 0x2C) | rd32(node + 0x34)) {
    constexpr uint32_t kVolumeList = 0x804D774Cu;
    wr32(node + 0x28, 0); wr32(node + 0x2C, 0); wr32(node + 0x34, 0);
    // MELEE_TEST_CLEAN_MUSIC_OLD (tests only): the byte alone, as before, to show the slot leak.
    static const bool old_way = std::getenv("MELEE_TEST_CLEAN_MUSIC_OLD") != nullptr;
    if (old_way) wr8(node + 0x26, 1);
    else if (rd8(node + 0x26) == 0 && (rd8(node + 0x09) & 8) == 0) {
      wr32(node + 0x20, rd32(kVolumeList)); wr32(kVolumeList, node); wr8(node + 0x26, 1);
    }
  }
  const uint32_t bits = rd32(0x804D38ACu);
  float level; std::memcpy(&level, &bits, 4);
  if (!std::isfinite(level)) return;
  static int last = -1;
  const int volume = (int)std::clamp(level * (float)(rd32(0x804D3884u) * 2u), 0.0f, 254.0f);
  if (volume != last) { last = volume; slippi::jukebox::set_melee_volume((uint8_t)volume); }
}
// The title demo preloads four fighters and a stage into two heaps of a fixed size, sized for the 26
// fighters of the retail disc. A disc that draws the demo from a larger roster and keeps those sizes
// (ACE 2.0.0) stops with `assertion "memp_kouho"` in lbmemory.c when the draw is too large. At the entry
// of gm_PreloadTitleDemo (801BF3F8) the four picks are at 8049E548 (costumes at +4). A pick that is not
// one of the 26 becomes one of them, worked out from the pick itself (no random number is drawn); those
// always fit. 0x21 is the game's "no fighter" and loads nothing, so it stays. The retail game never has
// another value here, and this hook is only installed for a mod disc.
static void title_demo_fit(ppc::Context&) {
  constexpr uint32_t kPicks = 0x8049E548u;
  constexpr uint8_t kRetailFighters = 0x1Au, kNoFighter = 0x21u, kZelda = 0x12u, kSheik = 0x13u;
  uint8_t pick[4];
  for (int i = 0; i < 4; ++i) pick[i] = rd8(kPicks + i);
  for (int i = 0; i < 4; ++i) {
    if (pick[i] < kRetailFighters || pick[i] == kNoFighter) continue;
    // As the game's own draw: no fighter twice, and never Zelda and Sheik together.
    auto taken = [&](uint8_t v) {
      for (int j = 0; j < 4; ++j)
        if (j != i && (pick[j] == v || (v == kZelda && pick[j] == kSheik) || (v == kSheik && pick[j] == kZelda))) return true;
      return false;
    };
    uint8_t v = (uint8_t)(pick[i] % kRetailFighters);
    while (taken(v)) v = (uint8_t)((v + 1u) % kRetailFighters);
    log("mods: the title demo drew fighter %u, which its memory was not sized for; fighter %u of the original game plays in its place", (unsigned)pick[i], (unsigned)v);
    pick[i] = v;
    wr8(kPicks + i, v);
    wr8(kPicks + 4 + i, 0);
  }
}
// The same stop, should it still happen (a disc with larger files of the 26, a stage too large): one
// line with the heap that had no room and the title demo's draw, so a report names the cause. At the
// entry of __assert (80388220) called from lbMemory_80014FC8 for line 233, r24 is the heap's handle and
// r30 the size asked, rounded up to 32 bytes (see trace_guest_heap_assert, the test switch's full trace).
static void report_heap_stop_lines(uint32_t handle, uint32_t rounded, uint32_t caller) {
  static bool told = false;   // the entry hook and the panic itself can both come here for one stop
  if (told) return;
  told = true;
  const auto heap = guest_heap_trace::inspect(handle, heap_trace_read32);
  log("heap: no room in a fixed heap: handle %08X, %08X..%08X (%u bytes), asked %u, free %u, largest gap %u, %u blocks%s, asked by %08X",
      handle, heap.lo, heap.hi, heap.hi - heap.lo, rounded, heap.free, heap.largest_gap, (unsigned)heap.count,
      heap.complete ? "" : " (list not read to its end)", caller);
  constexpr uint32_t kPicks = 0x8049E548u;
  if (try_ptr(kPicks, 0x10))
    log("heap: title demo draw: fighters %02X %02X %02X %02X, costumes %02X %02X %02X %02X, stage %04X", rd8(kPicks), rd8(kPicks + 1),
        rd8(kPicks + 2), rd8(kPicks + 3), rd8(kPicks + 4), rd8(kPicks + 5), rd8(kPicks + 6), rd8(kPicks + 7), rd16(kPicks + 0xC));
  log_flush();
}
static void report_heap_stop(ppc::Context& c) {
  if (c.lr != 0x80015098u || c.r[4] != 233u || !heap_trace_literal(c.r[3], "lbmemory.c")) return;
  uint32_t caller = 0;
  if (c.r[1] > 0xFFFFFFC3u || !heap_trace_read32(c.r[1] + 60u, caller)) caller = 0;
  report_heap_stop_lines(c.r[24], c.r[30], caller);
}
// The same two lines for any disc, from the panic itself (no entry hook is installed for the retail
// disc). At the entry of OSPanic the stack holds the frame of the panic helper (80388278), then
// __assert's, where the size asked was saved (+0x18), then the allocator's, which holds __assert's
// return address (+4) and, above its 0x38 bytes, its own caller's. r24 is still the heap's handle:
// neither function in between uses it. Anything that does not match this exactly prints nothing.
void report_heap_panic(ppc::Context& c) {
  uint32_t helper = c.r[1], assert_frame = 0, allocator = 0, back = 0, rounded = 0, caller = 0;
  if (c.lr != 0x80388300u || !heap_trace_read32(helper, assert_frame) || !heap_trace_read32(assert_frame, allocator) ||
      allocator > 0xFFFFFFC3u || !heap_trace_read32(allocator + 4u, back) || back != 0x80015098u ||
      assert_frame > 0xFFFFFFE7u || !heap_trace_read32(assert_frame + 0x18u, rounded)) return;
  if (!heap_trace_read32(allocator + 60u, caller)) caller = 0;
  report_heap_stop_lines(c.r[24], rounded, caller);
}
void install_mod_disc_guards() {
  if (!mod_disc_active()) return;
  if (!ppc::redirect_to_host(0x80337C60u, dvd_fast_open_checked)) log("mods: DVDFastOpen keeps the game's own code");
  if (!ppc::redirect_to_host(0x8033796Cu, dvd_convert_path_checked)) log("mods: the file name search keeps the game's own code");
  // MELEE_TEST_NO_DEMO_FIT (tests only, hidden and headless runs): the draw is left as the disc made
  // it, to show the stop and its report.
  if (!(std::getenv("MELEE_TEST_NO_DEMO_FIT") && options.no_gc_adapter)) ppc::add_entry_hook(0x801BF3F8u, title_demo_fit);
  ppc::add_entry_hook(0x80388220u, report_heap_stop);
}
void install_audio_pacing() {
  if (ppc::Fn previous = ppc::set_hook(0x80019894u, pad_queue_count_paced)) g_pad_queue_count = previous;
  else log("audio: frame-wait pacing not installed (lb_80019894 not found)");
  if (ppc::Fn previous = ppc::set_hook(0x800195D0u, idle_poll_paced)) g_idle_poll = previous;
  else log("audio: frame-wait pacing not installed (lb_800195D0 not found)");
}

// Diagnostic: the OSAlarm queue must only ever link alarms whose handlers are code. A corrupt
// link is reported at the first HLE entry after it appears so the call trace points at the writer.
static void validate_alarm_queue(const char* where) {
  static bool reported = false;
  if (reported) return;
  uint32_t a = rd32(gs::AlarmQueue), prev = 0;
  for (int guard = 0; a && guard < 64; ++guard) {
    bool bad = a < 0x80003000u || a >= 0x81800000u;
    uint32_t handler = bad ? 0 : rd32(a);
    // Handlers live in the DOL's text or in the Slippi code table caves; nothing else is code.
    bool code = (handler >= 0x80003100u && handler < 0x803B7240u) || (handler >= 0x8065C000u && handler < 0x8071B000u);
    if (!bad) bad = !code || (handler & 3) || rd32(a + 16) != prev;
    if (bad) {
      reported = true;
      log("ALARM QUEUE CORRUPT (%s): entry %08X handler %08X prev %08X (expected %08X) next %08X head %08X tail %08X retrace %u",
          where, a, handler, bad && a >= 0x80003000u && a < 0x81800000u ? rd32(a + 16) : 0, prev, a >= 0x80003000u && a < 0x81800000u ? rd32(a + 20) : 0,
          rd32(gs::AlarmQueue), rd32(gs::AlarmQueue + 4), g_retraces);
      ppc::fatal(*cpu, "alarm queue corrupt", a);
      return;
    }
    prev = a; a = rd32(a + 20);
  }
}

static void fire_due_alarms(bool force) {
  // OSAlarm queue head lives in the SDK's static AlarmQueue; fire through the installed
  // decrementer exception handler (OSExceptionTable[8] at 0x80003000 + 8*4) so the guest's
  // own callback logic runs. The handler expects an exception frame; the asm wrapper just
  // saves GPRs into the context and tail-calls DecrementerExceptionCallback, which processes
  // one alarm and re-arms periodic ones, so loop while the head is due.
  static bool firing = false;
  if (firing) return;
  if (!force && !ppc::interrupts_on(*cpu)) return;
  firing = true;
  for (int guard = 0; guard < 16; ++guard) {
    validate_alarm_queue(guard ? "after previous alarm handler" : "entry");
    uint32_t head = rd32(gs::AlarmQueue);
    if (!head) break;
    uint64_t fire = ((uint64_t)rd32(head + 8) << 32) | rd32(head + 12);
    // Alarm times are OS system time: timebase + the adjust at 0x800030D8.
    uint64_t adjust = ((uint64_t)rd32(0x800030D8u) << 32) | rd32(0x800030DCu);
    if ((int64_t)fire > (int64_t)(cpu->tb + adjust)) break;
    uint32_t handler = rd32(0x80003000u + 8 * 4);
    if (!handler) break;
    uint32_t context = rd32(0x800000D4u);
    static int reported = 0;
    if (options.trace_calls && reported++ < 40)
      log("[alarm] head=%08X fire=%llu tb=%llu handler=%08X cb=%08X period=%llu", head, fire, cpu->tb,
          handler, rd32(head), ((uint64_t)rd32(head + 24) << 32) | rd32(head + 28));
    ppc::Context& c = *cpu;
    ppc::Context saved = c;
    try {
      call_guest(handler, 8, context);
    } catch (const LoadContextUnwind&) {
    }
    uint64_t tb = c.tb;
    c = saved;
    c.tb = tb;
    ppc::update_mxcsr(c);
    static char where[64];
    std::snprintf(where, sizeof where, "after alarm %08X handler %08X", head, rd32(head));
    validate_alarm_queue(where);
  }
  firing = false;
}

bool g_has_window = false;

// Field-wise CPU hash excludes C++ padding and diagnostic counters/trace history.
static void trace_state() {
  if (!g_state_trace) return;
  hle::dvd_settle();   // a disc read still being copied in would make the RAM hash depend on the machine's load
  uint64_t h = 0;
  auto add = [&](const auto& v) { h = (h ^ gx::hash_bytes(&v, sizeof v)) * 0x100000001b3ull; };
  const auto& c = *cpu;
  add(c.r); add(c.f); add(c.cr); add(c.lr); add(c.ctr);
  add(c.ca); add(c.so); add(c.ov); add(c.fpscr); add(c.gqr);
  add(c.msr); add(c.hid0); add(c.hid2); add(c.dec); add(c.tb); add(c.spr);
  uint64_t events = (uint64_t)g_completions.size() << 32 |
      (uint64_t)g_pe_token << 8 | (g_pe_finish_pending ? 1 : 0) | (g_pe_token_pending ? 2 : 0);
  std::fprintf(g_state_trace, "%u,%016llX,%016llX,%016llX,%016llX\n", g_retraces,
      h, gx::hash_bytes(ram, ppc::RAM_SIZE), gx::hash_bytes(aram, 0x01000000), events);
  std::fflush(g_state_trace);
}

// Just the scene fields, cheap enough to call every retrace when an @scene script needs to know
// when the game reaches a particular mode/state (window.cpp). Shares the same addresses
// digest_state() uses for the full snapshot so the two never disagree about what "scene" means.
void current_scene(uint32_t* major, uint32_t* minor, uint32_t* match_frame) {
  if (native_state_snapshot) {
    MuStatePod state{};
    native_state_snapshot(&state);
    *major = state.scene_major;
    *minor = state.scene;
    *match_frame = state.match_frame;
  } else {
    *major = rd8(0x80479D30u);   // GameRouting::curr_mode (state_machine + 0)
    *minor = rd8(0x80479D33u);   // GameRouting::curr_state_id (state_machine + 3)
    *match_frame = rd32(0x8046B6C4u); // VsSceneController state frame count
  }
}

static void digest_state() {
  if (!g_state_digest) return;
  MuStatePod state{};
  if (native_state_snapshot) {
    native_state_snapshot(&state);
  } else {
    const uint32_t seed = rd32(0x804D5F94u);
    if (seed && try_ptr(seed, 4)) state.rng = rd32(seed);
    state.scene = rd8(0x80479D33u);         // GameRouting::curr_state_id (state_machine + 3)
    state.scene_major = rd8(0x80479D30u);   // GameRouting::curr_mode (state_machine + 0)
    state.match_frame = rd32(0x8046B6C4u);  // VsSceneController(0x8046B6A0)->state.frame_count (+0x24)
    for (uint32_t slot = 0; slot < 6; ++slot) {
      const uint32_t player = 0x80453080u + slot * 0xE90u;
      if (rd32(player) != 2) continue;
      const uint32_t active = rd8(player + 0xCu);
      const uint32_t gobj = rd32(player + 0xB0u + (active & 1u) * 4u);
      if (!gobj || !try_ptr(gobj, 0x30)) continue;
      const uint32_t fp = rd32(gobj + 0x2Cu);
      if (!fp || !try_ptr(fp, 0x1834)) continue;
      if (rd32(fp) != gobj) continue;
      MuFighterState& f = state.player[slot];
      f.present = 1;
      f.stocks = static_cast<int8_t>(rd8(player + 0x8Eu));
      f.action = rd32(fp + 0x10u);
      f.anim_frame = rd32(fp + 0x894u);
      f.pos_x = rd32(fp + 0xB0u); f.pos_y = rd32(fp + 0xB4u); f.pos_z = rd32(fp + 0xB8u);
      f.vel_x = rd32(fp + 0x80u); f.vel_y = rd32(fp + 0x84u); f.vel_z = rd32(fp + 0x88u);
      f.percent = rd32(fp + 0x1830u);
      f.facing = rd32(fp + 0x2Cu);
    }
  }
  std::fprintf(g_state_digest, "%u,%08X,%08X", g_retraces, state.rng, state.scene);
  for (const auto& f : state.player) {
    const uint32_t* words = &f.present;
    for (unsigned index = 0; index < 12; ++index) std::fprintf(g_state_digest, ",%08X", words[index]);
  }
  std::fprintf(g_state_digest, ",%08X,%08X", state.scene_major, state.match_frame);
  if (const uint32_t every = digest_ram_interval()) {
    if (g_retraces % every == 0 && !native_state_snapshot) {
      hle::dvd_settle();   // as the state trace does: a read still being copied in is not the game's state yet
      std::fprintf(g_state_digest, ",%016llX", (unsigned long long)ram_hash());
    }
    else std::fprintf(g_state_digest, ",-");
  }
  std::fputc('\n', g_state_digest);
  std::fflush(g_state_digest);
}

// A local mailbox only. The launcher owns network presence and authentication; the simulation
// writes at most once per second, using each engine's real player state (never a guessed timer).
static void publish_lobby_status() {
  const auto& cfg = slippi::online::config();
  if (cfg.lobby_status_file.empty() && cfg.lobby_code.empty()) return;
  static auto last = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(1)) return;
  last = now;
  const bool in_match = slippi::online::is_online_match() && !slippi::online::in_online_menus();
  std::string stocks;
  if (in_match) {
    const int local = slippi::online::local_player_index();
    if (local >= 0 && local < 4) {
      int count = 0;
      if (native_state_snapshot) {
        MuStatePod state{}; native_state_snapshot(&state);
        count = (int)state.player[local].stocks;
      } else {
        count = (int8_t)rd8(0x80453080u + (uint32_t)local * 0xE90u + 0x8Eu);
      }
      if (count >= 0 && count <= 99) stocks = std::to_string(count);
    }
  }
  // The file is written by its own thread, and only when the text changes. Written here, a slow
  // disk (a sleeping hard drive, a virus scan of the new file) held the simulation for that long
  // once a second, and a write over a second long repeated on every frame: 1 fps until the disk
  // recovered. The launcher adds this file, so games started from it stuttered and bare runs did not.
  if (!cfg.lobby_status_file.empty()) {
    static std::mutex mutex;
    static std::condition_variable wake;
    static std::string pending, written;
    std::string text = std::string("{\"status\":\"") + (in_match ? "In match" : "In game") + "\",\"stocks\":[" + stocks + "]}";
    static std::once_flag started;
    std::call_once(started, [path = cfg.lobby_status_file] {
      std::thread([path] {
        const auto target = std::filesystem::u8path(path), temp = std::filesystem::u8path(path + ".tmp");
        for (;;) {
          std::string next;
          { std::unique_lock<std::mutex> lock(mutex); wake.wait(lock, [] { return pending != written; }); next = written = pending; }
          { std::ofstream f(temp); f << next; }
          MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
      }).detach();
    });
    { std::lock_guard<std::mutex> lock(mutex); pending = std::move(text); }
    wake.notify_one();
  }
  // Native lobby boot is a single negotiated match. Return to the launcher after it ends.
  // A failed negotiation must not leave either player stuck in the loading scene indefinitely.
  if (!cfg.lobby_code.empty()) {
    static const auto started = now;
    static bool played = false;
    if (in_match) played = true;
    if ((game_image && played && !in_match) || (!played && now - started > std::chrono::seconds(90))) request_exit(played ? 0 : 2);
  }
}

void publish_lobby_result(int winner_index, int end_method) {
  const auto& cfg = slippi::online::config();
  if (cfg.lobby_code.empty() || cfg.lobby_status_file.empty()) return;
  const int local = slippi::online::local_player_index();
  const char* outcome = "incomplete";
  if (end_method == 2 && winner_index >= 0 && winner_index < 4 && local >= 0 && local < 4)
    outcome = winner_index == local ? "win" : "loss";
  const auto path = std::filesystem::u8path(cfg.lobby_status_file + ".results");
  std::ofstream file(path, std::ios::app);
  file << "{\"result\":\"" << outcome << "\",\"winner\":"
       << winner_index << ",\"end_method\":" << end_method << "}\n";
}

static double g_frame_time = 0.0;
static double g_emulation_speed = 1.0;
void set_emulation_speed(double speed) {
  g_emulation_speed = speed < 0.5 ? 0.5 : speed > 2.0 ? 2.0 : speed;
  // The game makes its sound at this speed too (Slippi's time sync nudges it by up to 1% online):
  // the output follows it directly, since its own drift correction is capped far below that.
  audio_set_speed(g_emulation_speed);
}
double emulation_speed() { return g_emulation_speed; }
double now_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
double frame_time() { return g_frame_time; }
bool latency_trace_enabled() {
  static const bool enabled = [] { const char* v = std::getenv("MELEE_TRACE_LATENCY"); return v && *v && *v != '0'; }();
  return enabled;
}
static TickTiming g_tick_timing;
TickTiming& tick_timing() { return g_tick_timing; }

// Simulation-thread cost accounting: HLE entry points add their time to a slot; at the next
// retrace the frame's work time (sleep excluded) is logged when it exceeds 20 ms, with the
// slots that explain it, so a hitch is attributed instead of guessed.
// Seconds per time stamp counter tick, measured against the performance counter over 20 ms at startup.
const double tsc_seconds = [] {
  LARGE_INTEGER freq, q0, q1; QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&q0); const uint64_t t0 = __rdtsc();
  Sleep(20);
  QueryPerformanceCounter(&q1); const uint64_t t1 = __rdtsc();
  return t1 > t0 ? (double)(q1.QuadPart - q0.QuadPart) / (double)freq.QuadPart / (double)(t1 - t0) : 0.0;
}();
static double g_sim_costs[SIM_COST_COUNT];
static double g_sim_costs_window[SIM_COST_COUNT];   // accumulated over the 60-frame log interval
static double g_sim_ms_window = 0, g_sim_ms_worst = 0;
static ULONG64 g_slow_frame_cycles = 0;   // this thread's cycle count at the start of the frame, for the slow-frame line
static const char* const g_sim_cost_names[SIM_COST_COUNT] = {"disc", "ax", "jukebox", "exi", "texsnap", "queue", "observe", "record", "gxdecode", "input", "wait", "late"};   // record includes texsnap and observe; gxdecode includes record and queue
static double g_sim_frame_start = 0.0, g_last_sim_ms = 0.0;
static ULONG64 g_sim_frame_start_cycles = 0;   // this thread's cycle count at g_sim_frame_start (MELEE_SIM_TIMES)
void sim_cost_add(int slot, double seconds) { if (slot >= 0 && slot < SIM_COST_COUNT) { g_sim_costs[slot] += seconds; g_sim_costs_window[slot] += seconds; } }
// "sim: 3.1 ms/frame (worst 12.4) | observe 0.9 texsnap 0.4" for the periodic frame log.
static std::string sim_cost_line(uint32_t frames) {
  char buf[320];
  static uint64_t last_enters = 0;
  const uint64_t enters = ppc::g_enter_count - last_enters; last_enters = ppc::g_enter_count;
  size_t n = (size_t)std::snprintf(buf, sizeof buf, "sim: %.1f ms/frame (worst %.1f), %llu guest calls/frame", g_sim_ms_window / std::max(1u, frames), g_sim_ms_worst,
                                   (unsigned long long)(enters / std::max(1u, frames)));
  // Code run from RAM (mod discs, RAM-resident routines): the interpreter's share of the window.
  static uint64_t last_insns = 0;
  uint64_t calls = 0, insns = 0;
  ppc::interpreter_counts(&calls, &insns);
  if (insns != last_insns)
    n += (size_t)std::snprintf(buf + n, sizeof buf - n, ", %llu interpreted insns/frame", (unsigned long long)((insns - last_insns) / std::max(1u, frames)));
  last_insns = insns;
  bool first = true;
  for (int i = 0; i < SIM_COST_COUNT; ++i) {
    double ms = g_sim_costs_window[i] * 1000.0 / std::max(1u, frames);
    if (ms < 0.05) continue;
    n += (size_t)std::snprintf(buf + n, sizeof buf - n, "%s %s %.2f", first ? " |" : "", g_sim_cost_names[i], ms);
    first = false;
  }
  std::memset(g_sim_costs_window, 0, sizeof g_sim_costs_window);
  g_sim_ms_window = 0; g_sim_ms_worst = 0;
  return buf;
}
double last_sim_frame_ms() { return g_last_sim_ms; }

// MELEE_TEST_EARLY_RNG_SEED="<seed>@<retrace>" (M4 diagnostic, off by default): the recompiled
// game's seed is written through HSD_RandSeedPtr at the start of that retrace, before the VI
// interrupt, so both engines reach the pre-match scenes with the same RNG. The native game writes
// the same seed at the same point (mu_entry.c mu_early_rng_seed).
static void apply_early_rng_seed() {
  static bool parsed = false, enabled = false;
  static uint32_t seed = 0, at = 0;
  if (!parsed) {
    parsed = true;
    if (const char* text = std::getenv("MELEE_TEST_EARLY_RNG_SEED")) {
      char* end = nullptr;
      seed = (uint32_t)std::strtoul(text, &end, 0);
      if (end && *end == '@') {
        at = (uint32_t)std::strtoul(end + 1, &end, 0);
        enabled = end && *end == '\0' && at != 0;
      }
    }
  }
  if (!enabled || g_retraces != at) return;
  const uint32_t seed_addr = rd32(0x804D5F94u);   // HSD_RandSeedPtr, as install_rng_seed_hook writes it
  if (seed_addr && try_ptr(seed_addr, 4)) wr32(seed_addr, seed);
  log("rng-seed: early %08X at retrace %u", seed, at);
}

// ---- sleeps that can be trusted ----
// A report: the game ran at a steady 48 Hz, every frame 20 to 23 ms long, with almost none of that
// time spent on game work. 48 Hz is three frames per four ticks of Windows' default 15.6 ms timer:
// on that system the frame wait's short sleeps were ending on the coarse tick instead of on time
// (a power saving state can do this to a process whatever resolution it asked for). The frame wait
// has a short sleep before each 5 ms audio step, and one that overshoots past the frame's end makes
// the whole frame late.
// So sleeping is trusted only as far as it is measured: a helper thread times a 1 ms sleep four
// times a second, with the same call the frame wait uses, and publishes how late it wakes. The
// frame wait sleeps only when there is room for that lateness and spins the rest. On a healthy
// system the lateness is a few hundred microseconds and nothing changes.
// MELEE_TEST_COARSE_SLEEP=1 makes every such sleep end on the next 15.625 ms boundary, to stand in
// for such a system; =2 does the same with the measurement ignored, which is the old behaviour.
static std::atomic<double> g_sleep_slack{0.0};   // seconds a timed sleep wakes late by, as last measured
static int coarse_sleep_test() {
  static const int mode = [] { const char* v = std::getenv("MELEE_TEST_COARSE_SLEEP"); return v ? std::atoi(v) : 0; }();
  return mode;
}
// One timed sleep of about `seconds`: the high resolution timer, or a millisecond sleep before
// Windows 1803. False when there is no timer and the time is too short to hand to Sleep.
static bool timed_sleep(HANDLE timer, double seconds) {
  if (coarse_sleep_test()) {
    const double tick = 0.015625;
    const double until = std::ceil((now_seconds() + seconds) / tick) * tick;
    for (double left = until - now_seconds(); left > 0.0; left = until - now_seconds()) {
      LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(std::max(left - 0.0004, 0.0001) * 1e7);
      if (left > 0.0008 && timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
      else YieldProcessor();
    }
    return true;
  }
  LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(seconds * 1e7);
  if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) { WaitForSingleObject(timer, INFINITE); return true; }
  if (seconds > 0.0016) { Sleep(1); return true; }
  return false;
}
static void sleep_probe_start() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);   // measured as the simulation thread sleeps
      const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
      double late[8] = {};
      bool coarse = false;
      for (unsigned n = 0;; ++n) {
        const double before = now_seconds();
        timed_sleep(timer, 0.001);
        late[n % 8] = std::max(0.0, now_seconds() - before - 0.001);
        // The median of the last eight: one sleep cut into by another program is not the system's timer.
        double sorted[8]; std::copy(std::begin(late), std::end(late), sorted); std::sort(std::begin(sorted), std::end(sorted));
        const double typical = n < 7 ? sorted[7] : sorted[4];
        g_sleep_slack.store(typical > 0.0007 ? typical : 0.0, std::memory_order_relaxed);
        const bool now_coarse = typical > 0.002;
        if (now_coarse != coarse && n >= 7) {
          coarse = now_coarse;
          if (coarse) log("timing: sleeps on this system wake %.1f ms late (a power saving state does this); the frame wait spins instead of sleeping so the game keeps 60 Hz", typical * 1000.0);
          else log("timing: sleeps wake on time again; the frame wait sleeps as usual");
        }
        Sleep(250);
      }
    }).detach();
  });
}
double sleep_slack() {
  sleep_probe_start();
  return coarse_sleep_test() == 2 ? 0.0 : g_sleep_slack.load(std::memory_order_relaxed);
}

// Every input waits for the next tick, so the tick wakes on time rather than on the millisecond
// sleep granularity (0.7 ms late on average, 1.2 ms at p95): a high resolution timer to just
// before the deadline, then a short spin. Without the high resolution timer (Windows before 1803)
// a millisecond sleep stands in for it. The sleep is as long as the measured lateness leaves room
// for (sleep_slack): on a system whose sleeps overshoot, the wait spins instead.
static void wait_for_tick(std::chrono::steady_clock::time_point deadline) {
  static const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
  for (;;) {
    const double remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0.0) return;
    const double slack = sleep_slack();
    if (remaining > 0.0006 + slack) {
      if (!timed_sleep(timer, remaining - 0.0004 - slack)) YieldProcessor();
    } else {
      YieldProcessor();
    }
  }
}

// How deep the game thread's stack has been: the lowest page Windows has had to commit for it. The
// game's model loaders call themselves once per joint and per drawn part, each such call is a host
// call here, and a retrace interrupt that arrives mid-load runs on top of them. With the default
// 1 MB that depth ran out after an online match (exception C00000FD while the next screen loaded),
// though ordinary play uses about a tenth of it. This line says how much a session really used.
static void log_stack_peak(bool at_exit) {
  static size_t logged = 0;
  const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
  const size_t used = (size_t)((uintptr_t)tib->StackBase - (uintptr_t)tib->StackLimit);
  if (!at_exit && used < logged + 128 * 1024) return;   // a new high by 128 KB, so a session logs a handful
  ULONG_PTR low = 0, high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  uint32_t major = 0, minor = 0, match_frame = 0;
  current_scene(&major, &minor, &match_frame);
  logged = std::max(logged, used);
  log("stack: the game thread has used %zu KB of %zu KB at most (scene %02X:%02X)", logged / 1024, (size_t)(high - low) / 1024, major, minor);
}

bool wait_until_console_time(uint64_t tb) {
  if (options.fast || tb >= g_next_retrace_tb || g_next_retrace_tb - tb > TB_PER_FRAME) return false;
  // Position of `tb` inside the frame, as a fraction, mapped onto the frame's real-time period.
  const double into_frame = 1.0 - (double)(g_next_retrace_tb - tb) / (double)TB_PER_FRAME;
  const auto deadline = g_next_frame + std::chrono::microseconds((long long)(into_frame * 16667.0 / g_emulation_speed));
  const auto before = std::chrono::steady_clock::now();
  if (deadline > before) {
    wait_for_tick(deadline);
    const auto after = std::chrono::steady_clock::now();
    sim_cost_add(SIM_WAIT, std::chrono::duration<double>(after - before).count());
    sim_cost_add(SIM_LATE, std::chrono::duration<double>(after - deadline).count());
  }
  if (cpu->tb < tb) cpu->tb = tb;
  return true;
}

void retrace() {
  struct Guard { Guard() { g_in_retrace = true; } ~Guard() { g_in_retrace = false; } } guard;
  g_frame_submitted = false;
  ++g_retraces;
  if (!native_retrace) apply_early_rng_seed();
  // Work sampled since the prior retrace belongs to the prior frame id. Publish
  // the new id only after that interval is complete so a slow-frame report can
  // select the samples that actually occurred inside it.
  const uint32_t completed_frame = g_retraces - 1;
  g_profiler_frame.store(g_retraces, std::memory_order_relaxed);
  {
    double now = now_seconds();
    if (g_sim_frame_start > 0.0) {
      g_last_sim_ms = (now - g_sim_frame_start) * 1000.0;
      g_sim_ms_window += g_last_sim_ms;
      if (g_last_sim_ms > g_sim_ms_worst) g_sim_ms_worst = g_last_sim_ms;
      if (g_last_sim_ms > 20.0) {
        g_slow_sim_frames.push_back(completed_frame);
        char detail[256] = ""; size_t n = 0;
        for (int i = 0; i < SIM_COST_COUNT; ++i) if (g_sim_costs[i] * 1000.0 >= 0.5) n += (size_t)std::snprintf(detail + n, sizeof detail - n, " %s %.1f", g_sim_cost_names[i], g_sim_costs[i] * 1000.0);
        // How long this thread actually ran during the frame: far below the frame's length means it
        // was waiting or pushed aside, close to it means the work itself was slow.
        ULONG64 cycles = 0; QueryThreadCycleTime(GetCurrentThread(), &cycles);
        const double ran_ms = g_slow_frame_cycles ? (double)(cycles - g_slow_frame_cycles) * tsc_seconds * 1000.0 : 0.0;
        log("sim frame %u took %.1f ms, thread ran %.1f ms (ms:%s%s)", g_retraces, g_last_sim_ms, ran_ms, detail, n ? "" : " guest code");
        // This is end-of-frame context rather than attribution. With --profile, the sampling
        // report below also names routines sampled during this exact slow frame.
        if (cpu) {
          constexpr uint32_t kRecentFunctions = 16;
          const uint32_t count = std::min<uint32_t>(cpu->trace_pos, kRecentFunctions);
          for (uint32_t i = 0; i < count; ++i) {
            const uint32_t pc = cpu->trace[(cpu->trace_pos - count + i) & 63u];
            if (pc) log("  end-of-frame guest %08X %s", pc, symbol_name(pc));
          }
        }
      }
      // MELEE_SIM_TIMES=<csv>: every simulation frame's work time and its cost slots, for
      // percentile comparisons between the engines (the log above only names frames over 20 ms).
      static FILE* sim_times = [] {
        const char* path = std::getenv("MELEE_SIM_TIMES");
        FILE* f = path && *path ? std::fopen(path, "w") : nullptr;
        if (f) {
          std::setvbuf(f, nullptr, _IOFBF, 1 << 20);
          std::fputs("retrace,scene_major,scene_minor,match_frame,sim_ms,cpu_ms", f);
          for (int i = 0; i < SIM_COST_COUNT; ++i) std::fprintf(f, ",%s", g_sim_cost_names[i]);
          std::fputc('\n', f);
        }
        return f;
      }();
      if (sim_times) {
        uint32_t major = 0, minor = 0, match_frame = 0;
        current_scene(&major, &minor, &match_frame);
        // Cycles this thread actually ran during the frame (waits and preemption by other processes
        // excluded): separates the frame's own cost from contention.
        ULONG64 cycles = 0; QueryThreadCycleTime(GetCurrentThread(), &cycles);
        const double cpu_ms = g_sim_frame_start_cycles ? (double)(cycles - g_sim_frame_start_cycles) * tsc_seconds * 1000.0 : 0.0;
        std::fprintf(sim_times, "%u,%u,%u,%u,%.3f,%.3f", completed_frame, major, minor, match_frame, g_last_sim_ms, cpu_ms);
        for (int i = 0; i < SIM_COST_COUNT; ++i) std::fprintf(sim_times, ",%.3f", g_sim_costs[i] * 1000.0);
        std::fputc('\n', sim_times);
        if (g_retraces % 600 == 0) std::fflush(sim_times);
      }
    }
    std::memset(g_sim_costs, 0, sizeof g_sim_costs);
  }
  slippi::poll_options();
  check_mod_code_writes();
  {
    // Gameplay for the audio output's Auto mode: a match whose frame counter moves, past its first
    // second (the match start itself can hitch).
    static uint32_t last_match_frame = 0;
    uint32_t major = 0, minor = 0, match_frame = 0;
    current_scene(&major, &minor, &match_frame);
    audio_set_gameplay(match_frame != last_match_frame && match_frame > 60);
    last_match_frame = match_frame;
  }
  // Static Recomp: the guest sleeps until the retrace, and time used to jump straight to it, so the
  // blocks that fell due during the sleep went out together once the frame's real-time wait ended.
  // Deliver each at its own real time first (as the console's AI interrupt does every 5 ms); the
  // frame boundary does not move. The Source Port paces in its scene loop instead (mu_audio_idle).
  if (!native_retrace && !options.fast) {
    static const bool pace = [] { const char* v = std::getenv("MELEE_AUDIO_PACING"); return !(v && *v == '0'); }();
    for (int guard = 0; pace && guard < 8; ++guard) {
      const uint64_t due = hle::audio_next_due();
      if (!due || due >= g_next_retrace_tb) break;
      if (cpu->tb < due && !wait_until_console_time(due)) break;
      if (cpu->tb < due) cpu->tb = due;
      hle::audio_tick(true);
    }
  }
  advance_frame();
  if (g_has_window) window_pump();
  if (!options.fast) {
    g_next_frame += std::chrono::microseconds((long long)(16667.0 / g_emulation_speed));
    auto now = std::chrono::steady_clock::now();
    if (g_next_frame > now) wait_for_tick(g_next_frame);
    else if (now - g_next_frame > std::chrono::milliseconds(34)) g_next_frame = now;   // after a stall, resume at 60 Hz instead of sprinting to catch up (audio would crackle)
    g_frame_time = std::chrono::duration<double>(g_next_frame.time_since_epoch()).count();
  } else {
    g_frame_time = now_seconds();
  }
  g_sim_frame_start = now_seconds();
  QueryThreadCycleTime(GetCurrentThread(), &g_slow_frame_cycles);
  static const bool sim_times_wanted = [] { const char* v = std::getenv("MELEE_SIM_TIMES"); return v && *v; }();
  if (sim_times_wanted) QueryThreadCycleTime(GetCurrentThread(), &g_sim_frame_start_cycles);
  g_tick_timing = {g_frame_time, g_sim_frame_start, 0, -1};
  if (native_retrace) {
    native_retrace();
  } else {
    fire_due_alarms(true);
    hle::audio_tick(true);
    // VI: mark display-interrupt 0 as pending (bit 15 of DI0 status, VI reg index 0x18).
    uint16_t di0 = ((uint16_t)g_mmio[0x2030] << 8) | g_mmio[0x2031];
    di0 |= 0x8000;
    g_mmio[0x2030] = (uint8_t)(di0 >> 8); g_mmio[0x2031] = (uint8_t)di0;
    deliver_interrupt(24);  // __OS_INTERRUPT_PI_VI
    trace_state();
  }
  digest_state();
  test_peek_and_dump();
  publish_lobby_status();
  if (g_retraces % 60 == 0 || (options.frames && g_retraces >= options.frames)) {
    uint64_t commands, draws, vertices; uint32_t copies;
    gx_stats(&commands, &draws, &vertices, &copies);
    log("[frame %u] gx: %llu cmds %llu draws %llu verts %u efb-copies | disc: %llu reads %.1f MB | %s",
        g_retraces, commands, draws, vertices, copies, g_disc_reads, g_disc_bytes / 1048576.0, sim_cost_line(60).c_str());
    // Same-thread, fixed-frame reading of authored coverage: comparable between the two engines.
    if (const auto& a = gx::authored_stats(); a.posed_draws)
      log("[frame %u] authored coverage: posed draws %u (envelope %u), skinned draws %u",
          g_retraces, (unsigned)a.posed_draws, (unsigned)a.posed_draws_envelope, (unsigned)a.skinned_draws);
    if (gx::native_draw_audit_enabled()) {
      const auto audit = gx::native_draw_audit_stats();
      log("native PObj scope audit: %llu submitted, %llu streamed, %llu matched, %llu mismatched, %llu sequence errors, %llu scope errors, %llu/%llu scopes, %llu scoped / %llu unscoped draws, depth %llu, %llu pending, %llu open",
          (unsigned long long)audit.submitted_events, (unsigned long long)audit.streamed_events,
          (unsigned long long)audit.matched_events, (unsigned long long)audit.mismatched_events,
          (unsigned long long)audit.sequence_errors, (unsigned long long)audit.invalid_scope_events,
          (unsigned long long)audit.scopes_ended, (unsigned long long)audit.scopes_started,
          (unsigned long long)audit.scoped_draws, (unsigned long long)audit.unscoped_draws,
          (unsigned long long)audit.max_scope_depth, (unsigned long long)audit.pending_events,
          (unsigned long long)audit.open_scopes);
    }
  }
  // MELEE_TEST_RESIZE_AT=<retrace>:<w>x<h> (tests): the window changes size at that retrace, the
  // way a maximized or fullscreen window does shortly after start.
  static const struct ResizeAt {
    uint32_t at = 0; int w = 0, h = 0;
    ResizeAt() { if (const char* v = std::getenv("MELEE_TEST_RESIZE_AT")) std::sscanf(v, "%u:%dx%d", &at, &w, &h); }
  } resize_at;
  if (resize_at.at && g_retraces == resize_at.at) window_set_client_size(resize_at.w, resize_at.h);
  if (options.frames && g_retraces >= options.frames) request_exit(0);
  log_stack_peak(false);
  if (g_exit) {
    log_stack_peak(true);
    log("exit requested after %u retraces", g_retraces);
    std::fflush(stdout);
    throw ExitRequested{g_exit_code.load()};
  }
}

// Nesting: a callback that sleeps (OSSleepThread inside a DVD/ARQ chain) is a wait point where
// hardware would run further completions, so forced delivery may nest; polled entry points
// (force = false) never nest so callback order stays as posted.
static int g_pump_depth = 0;
static bool deliver_completions(bool force) {
  if (g_completions.empty()) return false;
  if (!force && (g_pump_depth > 0 || !ppc::interrupts_on(*cpu))) return false;
  if (g_pump_depth >= 16) return false;
  ++g_pump_depth;
  size_t n = g_completions.size();
  ppc::Context saved = *cpu;
  for (size_t i = 0; i < n && !g_completions.empty(); ++i) {
    Completion fn = std::move(g_completions.front());
    g_completions.pop_front();
    fn();
  }
  uint64_t tb = cpu->tb;
  *cpu = saved;
  cpu->tb = tb;
  ppc::update_mxcsr(*cpu);
  --g_pump_depth;
  return true;
}

void wait_event() {
  if (g_pe_finish_pending) {
    g_pe_finish_pending = false;
    // PE_ISR (0xCC00100A): finish interrupt status bit 3.
    g_mmio[0x100B] |= 0x08;
    // The game's draw-done callback (HSD_VIDrawDoneXFB, 803762C4) stops with video.c:722 when the
    // frame buffer it is told about is not waiting for it. A console delivers this interrupt right
    // after the frame; here it waits for the next sleep, and with Slippi's lag reduction lines (in
    // the replay viewer they ride with the widescreen code) the buffer index is -1 and two frames'
    // signals can meet the same slot: the viewer stopped at the end of every replay. Then the
    // callback is skipped for this one delivery: the wait flag still clears and GX still sees its
    // frame done, only the status change and the assert are left out, as Slippi's own
    // ForceNoVideoAssert does online. Display state only (outside the rollback snapshot).
    constexpr uint32_t kCallbackSlot = 0x804C1F68u, kArgSlot = 0x804C1F64u, kStatus0 = 0x804C1DDCu;
    constexpr uint32_t kDrawDoneXfb = 0x803762C4u, kWaitDone = 4u;
    uint32_t callback = 0;
    bool skip = false;
    if (!game_image && ram && rd32(kCallbackSlot) == kDrawDoneXfb) {
      const int32_t arg = (int32_t)rd32(kArgSlot);
      if (arg >= -1 && arg <= 2 && rd32(kStatus0 + (uint32_t)(arg * 0x60)) != kWaitDone) {
        skip = true;
        callback = rd32(kCallbackSlot);
        static bool told = false;
        if (!told) { told = true; log("video: a draw-done signal arrived for frame buffer %d, which was not waiting for it; skipped", arg); }
        wr32(kCallbackSlot, 0);
      }
    }
    deliver_interrupt(19);  // __OS_INTERRUPT_PI_PE_FINISH
    if (skip) wr32(kCallbackSlot, callback);
    return;
  }
  if (g_pe_token_pending) {
    g_pe_token_pending = false;
    g_mmio[0x100B] |= 0x04;
    g_mmio[0x100E] = (uint8_t)(g_pe_token >> 8); g_mmio[0x100F] = (uint8_t)g_pe_token;
    deliver_interrupt(18);  // __OS_INTERRUPT_PI_PE_TOKEN
    return;
  }
  // The sleeping thread yields: interrupts are effectively enabled during the switch, so pending
  // completions run now (nested if this sleep happens inside another callback). Otherwise time moves on.
  hle::dvd_poll();
  if (deliver_completions(true)) return;
  retrace();
}

}  // namespace host

namespace ppc {
void loop_poll(Context& c) {
  (void)c;
  host::pump_completions();   // advances time; fires alarms / audio frames / completions when EE is set
}
void interrupts_enabled(Context& c) {
  // Called from mtmsr when EE goes 0 -> 1: flush events that arrived while masked.
  if (host::g_pump_depth == 0) host::deliver_completions(true);   // never nest from inside a callback here
}
}  // namespace ppc

namespace host {

// ---------------- MMIO ----------------
static uint32_t mmio_get(uint32_t off, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | g_mmio[(off + i) & 0xFFFF];
  return v;
}
static void mmio_put(uint32_t off, uint32_t value, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) { g_mmio[(off + i) & 0xFFFF] = (uint8_t)value; value >>= 8; }
}

uint32_t mmio_read(uint32_t addr, int bytes) {
  if ((addr & 0xFFFF0000u) == 0xCC000000u) {
    uint32_t off = addr & 0xFFFF;
    switch (off & 0xFFFE) {
      case 0x2002: return 0;                 // VI: vertical position (VIGetCurrentLine)
      case 0x2000: return 0;
      case 0x0000: return 0;                 // CP status: fifo idle, not overflowed
      case 0x0004: return 0;                 // CP control
      case 0x0034: case 0x0036: return mmio_get(off, bytes);   // CP fifo rw distance (we keep 0)
      case 0x3000: return 0;                 // PI INTSR
      case 0x5004: return 0;                 // DSP mailbox from DSP: nothing pending
      case 0x5000: return 0;                 // DSP mailbox to DSP: not busy
      case 0x500A: return mmio_get(off, bytes) & ~0x0001u;  // DSP CSR: DSP not "reset in progress"
      default: return mmio_get(off, bytes);
    }
  }
  if ((addr & 0xF8000000u) == 0xC8000000u) return 0;  // EFB peek
  static int reported = 0;
  if (reported++ < 20) {
    log("mmio read %08X (%d) from %s lr=%08X", addr, bytes, symbol_name(cpu->last_pc), cpu->lr);
    if (reported <= 2) {
      log("  recent entries:");
      for (uint32_t i = 48; i < 64; ++i) { uint32_t pc = cpu->trace[(cpu->trace_pos + i) & 63]; if (pc) log("    %08X %s", pc, symbol_name(pc)); }
    }
  }
  return 0;
}

void mmio_write(uint32_t addr, uint32_t value, int bytes) {
  if ((addr & 0xFFFFC000u) == 0xCC008000u) { gx_write(value, bytes); return; }
  if ((addr & 0xFFFF0000u) == 0xCC000000u) {
    uint32_t off = addr & 0xFFFF;
    mmio_put(off, value, bytes);
    if (off == 0x3000 || off == 0x3004) return;
    return;
  }
  if ((addr & 0xF8000000u) == 0xC8000000u) return;  // EFB poke
  static int reported = 0;
  if (reported++ < 20) log("mmio write %08X = %08X (%d) from %s", addr, value, bytes, symbol_name(cpu->last_pc));
}

// ---------------- GX glue ----------------
void gx_write(uint32_t value, int bytes) { gx::write_fifo(value, bytes); }
void gx_frame_present(uint32_t) {}
void gx_stats(uint64_t* commands, uint64_t* draws, uint64_t* vertices, uint32_t* efb_copies) {
  gx::stats(commands, draws, vertices, efb_copies);
}

}  // namespace host
