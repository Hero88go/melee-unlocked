// Host services: memory, disc, boot, event delivery, time, MMIO, logging.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "cosmetic_mods.h"
#include "memory_range.h"
#include <windows.h>
#include <bcrypt.h>
#include "guest_registry.h"
#include "guest_symbols.h"
#include "gx_core.h"
#include "authored_pose.h"
#include "window.h"
#include "ax_ucode.h"
#include "exi_slippi.h"
#include "slippi_online.h"
#include "gecko_data.h"
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <deque>
#include <thread>
#include <filesystem>
#include <fstream>

namespace hle { void audio_tick(bool force); }

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
  std::exit(3);
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
std::string cstr(uint32_t addr, size_t max) {
  std::string s;
  for (size_t i = 0; i < max; ++i) { char ch = (char)rd8(addr + (uint32_t)i); if (!ch) break; s += ch; }
  return s;
}

// ---------------- disc ----------------
bool disc_open(const std::string& path) {
  g_disc = std::fopen(path.c_str(), "rb");
  if (!g_disc) return false;
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
uint32_t disc_fst_max_size() { return g_fst_max; }

// ---------------- boot ----------------
static uint32_t disc_dol_offset() {
  uint8_t hdr[4];
  if (!disc_read(0x420, hdr, 4)) die("cannot read disc DOL offset");
  return ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
}
// Both engines are built for the retail main.dol: the recompiled one runs its code, the native one
// reads its data. A modded disc (a patched DOL, m-ex and friends) fails the SHA-1 of NTSC 1.02.
bool disc_has_vanilla_dol() {
  constexpr uint32_t dol_size = 0x4385E0u;
  std::vector<uint8_t> image(dol_size);
  if (!disc_read(disc_dol_offset(), image.data(), dol_size)) die("cannot read full Melee DOL");
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  uint8_t digest[20];
  const uint8_t expected[20] = {0x08,0xe0,0xbf,0x20,0x13,0x4d,0xfc,0xb2,0x60,0x69,0x96,0x71,0x00,0x45,0x27,0xb2,0xd6,0xbb,0x1a,0x45};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0)
    die("cannot initialize game-image verification");
  NTSTATUS hash_status = BCryptHash(algorithm, nullptr, 0, image.data(), dol_size, digest, sizeof digest);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return hash_status >= 0 && std::memcmp(digest, expected, sizeof digest) == 0;
}
static void load_dol_from_disc() {
  const uint32_t dol_offset = disc_dol_offset();
  if (!disc_has_vanilla_dol())
    die("ISO DOL does not match vanilla Melee NTSC 1.02; recompiled code cannot run this image");
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
static void install_gecko_boot() {
  if (!gecko::codehandler_bin_size) { log("boot: translated without Slippi code tables"); return; }
  std::memcpy(ptr(0x80001800u, (uint32_t)gecko::codehandler_bin_size), gecko::codehandler_bin, gecko::codehandler_bin_size);
  wr32(0x80001D6Cu, 0x4E800020u);   // USB Gecko I/O replaced by blr, as Slippi does
  wr32(0x80001800u, 0xD01F1BADu);   // handler magic
  std::memcpy(ptr(0x800028B8u, (uint32_t)gecko::bootloader_gct_size), gecko::bootloader_gct, gecko::bootloader_gct_size);
  wr8(0x80001807u, 1);              // codes on
  for (size_t i = 0; i < gecko::boot_writes_count; ++i) {
    const gecko::Write& w = gecko::boot_writes[i];
    std::memcpy(ptr(w.addr, w.size), w.data, w.size);
  }
  for (size_t i = 0; i < gecko::boot_hooks_count; ++i) {
    const gecko::HookInstall& h = gecko::boot_hooks[i];
    wr32(h.hook, 0x48000000u | ((h.cave_addr - h.hook) & 0x03FFFFFCu));
    uint32_t last = h.cave_addr + (h.words - 1) * 4;
    wr32(last, 0x48000000u | (((h.hook + 4) - last) & 0x03FFFFFCu));
  }
  slippi::init();
  log("boot: Slippi code tables installed (%zu boot writes, %zu boot hooks, main GCT %zu bytes served over EXI)",
      gecko::boot_writes_count, gecko::boot_hooks_count, gecko::slippi_gct_size);
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
    std::fputc('\n', g_state_digest);
  }
}

void boot_setup() {
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
  uint64_t tb = options.time_base;
  if (!tb) {
    // Dolphin presets the timebase from the RTC (seconds since GC epoch 2000-01-01) * 40.5 MHz.
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t secs = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(now).count();
    const uint64_t GC_EPOCH = 946684800ull;
    tb = (secs - GC_EPOCH) * TB_HZ;
  }
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
  wr32(0x8000003C, g_fst_max);
  wr32(0x80000034, fst_addr);                        // arena hi
  log("boot: FST %u bytes at %08X (max %X), arena hi %08X", g_fst_size, fst_addr, g_fst_max, fst_addr);
  install_gecko_boot();

  cpu->msr = 0x00002030u | 0x8000u;                  // FP | DR | IR | EE
  cpu->fpscr = 0;
  ppc::update_mxcsr(*cpu);
  g_next_frame = std::chrono::steady_clock::now();
}

// ---------------- guest calls from host ----------------
void call_guest(uint32_t addr, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6) {
  ppc::Context& c = *cpu;
  uint32_t saved_lr = c.lr;
  c.r[3] = r3; c.r[4] = r4; c.r[5] = r5; c.r[6] = r6;
  c.lr = 0;
  ppc::call(c, ram, addr);
  c.lr = saved_lr;
}

// ---------------- events ----------------
void post_completion(Completion fn) { g_completions.push_back(std::move(fn)); }
void set_pe_finish_pending() { g_pe_finish_pending = true; }
void set_pe_token_pending(uint16_t token) { g_pe_token = token; g_pe_token_pending = true; }
bool exit_requested() { return g_exit; }
void request_exit(int code) { g_exit_code.store(code); g_exit.store(true); }
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
void pump_completions() {
  // Called from HLE entry points the guest polls. Virtual time flows a little so periodic
  // alarms (pad sampling) fire even in loops that never sleep. Nothing is delivered while the
  // guest has interrupts disabled; ppc::mtmsr flushes when they come back on.
  advance_time(2048);
  hle::dvd_poll();
  validate_alarm_queue("hle entry");
  if (!ppc::interrupts_on(*cpu)) return;
  fire_due_alarms(false);
  hle::audio_tick(false);
  deliver_completions(false);
  if (cpu->tb >= g_next_retrace_tb && !g_in_retrace) retrace();   // periodic VI interrupt during busy waits
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
  if (!cfg.lobby_status_file.empty()) {
    const auto path = std::filesystem::u8path(cfg.lobby_status_file);
    const auto temp = std::filesystem::u8path(cfg.lobby_status_file + ".tmp");
    { std::ofstream f(temp); f << "{\"status\":\"" << (in_match ? "In match" : "In game") << "\",\"stocks\":[" << stocks << "]}"; }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
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
void set_emulation_speed(double speed) { g_emulation_speed = speed < 0.5 ? 0.5 : speed > 2.0 ? 2.0 : speed; }
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
static const char* const g_sim_cost_names[SIM_COST_COUNT] = {"disc", "ax", "jukebox", "exi", "texsnap", "queue", "observe", "record", "gxdecode"};   // record includes texsnap and observe; gxdecode includes record and queue
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

// Every input waits for the next tick, so the tick wakes on time rather than on the millisecond
// sleep granularity (0.7 ms late on average, 1.2 ms at p95): a high resolution timer to just
// before the deadline, then a short spin. Without the high resolution timer (Windows before 1803)
// a millisecond sleep stands in for it.
static void wait_for_tick(std::chrono::steady_clock::time_point deadline) {
  static const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
  for (;;) {
    const double remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0.0) return;
    if (remaining > 0.0006) {
      LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining - 0.0004) * 1e7);
      if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
      else if (remaining > 0.002) Sleep(1);
      else YieldProcessor();
    } else {
      YieldProcessor();
    }
  }
}

void retrace() {
  struct Guard { Guard() { g_in_retrace = true; } ~Guard() { g_in_retrace = false; } } guard;
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
        log("sim frame %u took %.1f ms (ms:%s%s)", g_retraces, g_last_sim_ms, detail, n ? "" : " guest code");
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
  if (options.frames && g_retraces >= options.frames) request_exit(0);
  if (g_exit) {
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
    deliver_interrupt(19);  // __OS_INTERRUPT_PI_PE_FINISH
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
