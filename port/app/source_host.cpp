// The source port inside the full application: the same window, renderers (D3D12 with DLSS, D3D11),
// sub-frame presentation, input, settings panel and pacing as the recompiled build, with the game
// itself coming from melee_game.dll (the decompiled sources built natively, see sourceport/game)
// instead of the translated guest. main.cpp calls reserve_memory() first thing and run() where the
// recompiled build would enter __start.
//
// What still differs from the recompiled build is listed where it happens: no memory card yet
// (M10), Slippi's game side not present (M12).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_host.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "host.h"
#include "audio_core.h"
#include "ax_ucode.h"
#include "gx_core.h"
#include "lcancel.h"
#include "mu_host.h"
#include "ppc.h"
#include "window.h"

namespace source_port {
namespace {

constexpr uintptr_t MEM1_BASE = 0x80000000u;
constexpr uint32_t MEM1_SIZE = 40u << 20;   // the console's 24 MB, grown for 8-byte pointers; the game image follows
constexpr uintptr_t LOCKED_CACHE_BASE = 0xE0000000u;
constexpr uint32_t LOCKED_CACHE_SIZE = 16u << 10;
// Right after MEM1, so the game's own statics (the font atlas, static textures) have a physical
// address the GX texture and display-list registers can hold: 26 bits, the first 64 MB.
constexpr uintptr_t GAME_IMAGE_BASE = 0x82800000u;

MuGameApi g_game{};
std::string g_dll = "melee_game.dll";
audio_core::State g_audio;

uint16_t ax_native_rd16(uint32_t addr) {
  uint16_t value;
  std::memcpy(&value, host::ptr(addr, sizeof value), sizeof value);
  return value;
}
uint32_t ax_native_rd32(uint32_t addr) {
  return ((uint32_t) ax_native_rd16(addr) << 16) | ax_native_rd16(addr + 2);
}
void ax_native_wr16(uint32_t addr, uint16_t value) {
  std::memcpy(host::ptr(addr, sizeof value), &value, sizeof value);
}
void ax_native_wr32(uint32_t addr, uint32_t value) {
  ax_native_wr16(addr, (uint16_t) (value >> 16));
  ax_native_wr16(addr + 2, (uint16_t) value);
}

// ---- the disc's filesystem table, for the game's entry numbers ----
struct FstFile { uint32_t offset, length; bool dir; };
std::vector<FstFile> g_fst;
std::unordered_map<std::string, int32_t> g_paths;   // lower-case "/dir/name" -> entry number

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool read_fst() {
  uint8_t header[0x440];
  if (!host::disc_read(0, header, sizeof header)) return false;
  const uint32_t fst_offset = be32(header + 0x424), fst_size = be32(header + 0x428);
  std::vector<uint8_t> fst(fst_size);
  if (!host::disc_read(fst_offset, fst.data(), fst_size)) return false;
  const uint32_t count = be32(fst.data() + 8);
  const char* names = (const char*)fst.data() + count * 12;
  g_fst.assign(count, FstFile{});
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};   // (end entry, path) of open directories
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const uint8_t* e = fst.data() + i * 12;
    std::string name = names + (be32(e) & 0xFFFFFFu);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    const std::string full = dirs.back().second + "/" + name;
    g_fst[i] = {be32(e + 4), be32(e + 8), e[0] != 0};
    g_paths[full] = (int32_t)i;
    if (e[0]) dirs.push_back({be32(e + 8), full});
  }
  // The boot loader left the disc header at the start of MEM1, then the OS globals. The native game
  // reads those words as ordinary integers, so they are in host byte order.
  std::memcpy((void*)MEM1_BASE, header, 0x20);
  auto low = [](uint32_t offset, uint32_t value) { std::memcpy((void*)(MEM1_BASE + offset), &value, 4); };
  low(0x28, 24u << 20);      // physical memory size
  low(0xCC, 0);              // TV mode: NTSC
  low(0xF0, 24u << 20);      // simulated memory size
  low(0xF8, 162000000u);     // bus clock
  low(0xFC, 486000000u);     // core clock
  return true;
}

// ---- stopping ----
// host::retrace() ends the game by throwing ExitRequested. That must not unwind through the game's
// frames (another compiler's code), so it is caught at the boundary and the process finishes here.
int g_exit_code = 0;
void (*g_shutdown)(int) = nullptr;
template <class F> void guarded(F&& f) {
  try { f(); } catch (const ExitRequested& stop) {
    g_exit_code = stop.code;
    if (g_shutdown) g_shutdown(stop.code);
    ExitProcess((UINT)stop.code);
  }
}

// ---- the table the game calls ----
void h_log(const char* text) {
  std::string line = text ? text : "";
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  if (!line.empty()) host::log("[game] %s", line.c_str());
}
void h_panic(const char* file, int32_t line, const char* message) {
  host::log("game panic at %s:%d", file ? file : "?", line);
  void* frames[32];
  const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    const uintptr_t pc = reinterpret_cast<uintptr_t>(frames[i]);
    if (pc > GAME_IMAGE_BASE && pc < GAME_IMAGE_BASE + host::game_image_size)
      host::log("  stack: melee_game.dll+0x%llX", (unsigned long long)(pc - 1 - GAME_IMAGE_BASE));
  }
  host::die("game stopped at %s:%d: %s", file ? file : "?", line, message ? message : "");
}
uint64_t h_ticks() { return host::cpu->tb; }
uint64_t h_boot_time() { return 0; }

void native_audio_done(void*) {
  if (g_game.ai_dma_done) g_game.ai_dma_done();
}

void native_audio_tick() {
  audio_core::tick(g_audio, host::cpu->tb, host::TB_HZ, native_audio_done, nullptr);
}

void h_poll() {
  guarded([] {
    host::advance_time(2048);   // time flows in busy waits, as it does for the recompiled build
    native_audio_tick();
    if (host::retrace_due()) host::retrace();
    else g_game.fire_alarms(host::cpu->tb);
  });
}

void native_retrace() {
  native_audio_tick();
  g_game.fire_alarms(host::cpu->tb);
  g_game.retrace();
}

void h_gx_fifo(const uint8_t* data, uint32_t size) { guarded([&] { gx::write_fifo_bytes(data, size); }); }
void h_vi_configure(uint32_t w, uint32_t h, uint32_t interlaced) { host::log("VI: %ux%u%s", w, h, interlaced ? " interlaced" : ""); }
void h_vi_set_next_framebuffer(void*) {}   // presentation follows the EFB copy, as in the recompiled build
void h_vi_flush() {}
uint32_t h_vi_retrace_count() { return host::retrace_count(); }
uint32_t h_vi_next_field() { return host::retrace_count() & 1; }
void h_vi_set_black(int32_t) {}
void h_vi_wait_retrace() { guarded([] { host::retrace(); }); }

void h_pad_read(MuPadStatus out[4]) {
  host::PadState pads[4];
  host::input_poll(pads);
  lcancel::apply(pads);   // auto L-cancel, upstream of the game exactly as in the recompiled build
  for (int i = 0; i < 4; ++i) {
    out[i].button = pads[i].button;
    out[i].stick_x = pads[i].stick_x; out[i].stick_y = pads[i].stick_y;
    out[i].sub_x = pads[i].sub_x; out[i].sub_y = pads[i].sub_y;
    out[i].trigger_l = pads[i].trig_l; out[i].trigger_r = pads[i].trig_r;
    out[i].analog_a = pads[i].analog_a; out[i].analog_b = pads[i].analog_b;
    out[i].err = pads[i].err;
  }
}
void h_pad_rumble(int32_t port, int32_t on) { host::gcadapter_rumble(port, on != 0); }

int32_t h_disc_entrynum(const char* path) {
  std::string p = path ? path : "";
  std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  if (p.empty() || p[0] != '/') p = "/" + p;
  auto it = g_paths.find(p);
  return it == g_paths.end() ? -1 : it->second;
}
int32_t h_disc_file(int32_t entrynum, uint32_t* start, uint32_t* length) {
  if (entrynum <= 0 || entrynum >= (int32_t)g_fst.size() || g_fst[entrynum].dir) return 0;
  *start = g_fst[entrynum].offset;
  *length = g_fst[entrynum].length;
  return 1;
}
void h_disc_read(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user) {
  const bool ok = host::disc_read(offset, dst, size);
  if (!ok) host::log("disc: read %08X+%X failed", offset, size);
  ++host::g_disc_reads;
  host::g_disc_bytes += size;
  done(ok ? (int32_t)size : -1, user);   // the game queues it and runs it as the drive interrupt
}
int32_t h_disc_status() { return 0; }
uint32_t h_disc_id(void* out, uint32_t size) { const uint32_t n = std::min<uint32_t>(size, 0x20); std::memcpy(out, (void*)MEM1_BASE, n); return n; }

// Memory card: slot A is a Dolphin-compatible folder of .gci files. The game-side shim passes
// native pointers here, so directory entries are converted explicitly instead of exposing their
// big-endian on-card representation as a host struct.
namespace {
constexpr int32_t CARD_READY = 0, CARD_NOCARD = -3, CARD_NOFILE = -4, CARD_EXIST = -7,
                  CARD_NOENT = -8, CARD_INSSPACE = -9, CARD_NOPERM = -10,
                  CARD_LIMIT = -11, CARD_NAMETOOLONG = -12, CARD_FATAL = -128;
constexpr uint32_t CARD_SECTOR = 0x2000, CARD_MAX_FILES = 127, CARD_TOTAL_BLOCKS = 2043,
                   CARD_MEM_SIZE_MBIT = 128;

struct SourceCardFile {
  uint8_t dir[64]{};
  std::vector<uint8_t> data;
  std::filesystem::path path;

  std::string name() const {
    return std::string(reinterpret_cast<const char*>(dir + 8), strnlen(reinterpret_cast<const char*>(dir + 8), 32));
  }
  uint16_t blocks() const { return (uint16_t)(((uint16_t)dir[0x38] << 8) | dir[0x39]); }
};

struct SourceCardStat {
  char fileName[32];
  uint32_t length, time;
  uint8_t gameName[4], company[2], bannerFormat;
  uint32_t iconAddr;
  uint16_t iconFormat, iconSpeed;
  uint32_t commentAddr, offsetBanner, offsetBannerTlut, offsetIcon[8], offsetIconTlut, offsetData;
};
static_assert(sizeof(SourceCardStat) == 0x6C, "native CARDStat layout");

std::vector<SourceCardFile*> g_card_files;
std::filesystem::path g_card_dir;
bool g_card_mounted = false;

uint16_t card_be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
uint32_t card_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void card_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void card_put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

const uint8_t* card_disk_id() { return reinterpret_cast<const uint8_t*>(MEM1_BASE); }
bool card_same_game(const SourceCardFile& file) { return std::memcmp(file.dir, card_disk_id(), 6) == 0; }
uint32_t card_used_blocks() {
  uint32_t blocks = 0;
  for (const SourceCardFile* file : g_card_files) if (file) blocks += file->blocks();
  return blocks;
}
int32_t card_find(const std::string& name) {
  for (size_t i = 0; i < g_card_files.size(); ++i) {
    if (g_card_files[i] && card_same_game(*g_card_files[i]) && g_card_files[i]->name() == name)
      return (int32_t)i;
  }
  return -1;
}
std::string card_safe_name(const SourceCardFile& file) {
  std::string name(reinterpret_cast<const char*>(file.dir), 6);
  name += "-";
  for (char ch : file.name())
    name += (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.') ? ch : '_';
  return name + ".gci";
}
void card_save(SourceCardFile& file) {
  if (file.path.empty()) file.path = g_card_dir / card_safe_name(file);
  std::filesystem::path temporary = file.path.string() + ".tmp";
  FILE* out = std::fopen(temporary.string().c_str(), "wb");
  if (!out) { host::log("card: cannot write %s", file.path.string().c_str()); return; }
  const bool ok = std::fwrite(file.dir, 1, sizeof file.dir, out) == sizeof file.dir &&
                  std::fwrite(file.data.data(), 1, file.data.size(), out) == file.data.size();
  std::fclose(out);
  std::error_code ec;
  if (ok) std::filesystem::rename(temporary, file.path, ec);
  if (!ok || ec) host::log("card: failed to save %s", file.path.string().c_str());
}
void card_clear_files() {
  for (SourceCardFile* file : g_card_files) delete file;
  g_card_files.clear();
}
void card_mount_files() {
  if (g_card_mounted) return;
  g_card_dir = host::options.card_dir;
  std::error_code ec;
  std::filesystem::create_directories(g_card_dir, ec);
  card_clear_files();
  g_card_files.assign(CARD_MAX_FILES, nullptr);
  size_t slot = 0;
  for (const auto& entry : std::filesystem::directory_iterator(g_card_dir, ec)) {
    if (ec || entry.path().extension() != ".gci" || slot >= CARD_MAX_FILES) continue;
    FILE* in = std::fopen(entry.path().string().c_str(), "rb");
    if (!in) continue;
    auto* file = new SourceCardFile;
    bool ok = std::fread(file->dir, 1, sizeof file->dir, in) == sizeof file->dir;
    if (ok) {
      file->data.resize((size_t)file->blocks() * CARD_SECTOR);
      ok = std::fread(file->data.data(), 1, file->data.size(), in) == file->data.size();
    }
    std::fclose(in);
    if (!ok || file->blocks() == 0 || file->blocks() > CARD_TOTAL_BLOCKS) { delete file; continue; }
    file->path = entry.path();
    g_card_files[slot++] = file;
  }
  g_card_mounted = true;
  host::log("card: source slot A mounted from %s (%zu files, %u of %u blocks used)",
            g_card_dir.string().c_str(), slot, card_used_blocks(), CARD_TOTAL_BLOCKS);
}
void card_reset_mount() { card_clear_files(); g_card_mounted = false; }
uint32_t card_time_2000() { return host::cpu ? (uint32_t)(host::cpu->tb / host::TB_HZ) : 0; }
void card_fill_stat(const SourceCardFile& file, SourceCardStat& stat) {
  std::memset(&stat, 0, sizeof stat);
  std::memcpy(stat.fileName, file.dir + 8, 32);
  stat.length = (uint32_t)file.data.size();
  stat.time = card_be32(file.dir + 0x28);
  std::memcpy(stat.gameName, file.dir, 4);
  std::memcpy(stat.company, file.dir + 4, 2);
  stat.bannerFormat = file.dir[7];
  stat.iconAddr = card_be32(file.dir + 0x2C);
  stat.iconFormat = card_be16(file.dir + 0x30);
  stat.iconSpeed = card_be16(file.dir + 0x32);
  stat.commentAddr = card_be32(file.dir + 0x3C);
  const uint32_t none = 0xFFFFFFFFu;
  for (uint32_t& offset : stat.offsetIcon) offset = none;
  stat.offsetBanner = stat.offsetBannerTlut = stat.offsetIconTlut = none;
  if (stat.iconAddr == none) { stat.offsetData = 0; return; }
  uint32_t offset = stat.iconAddr;
  if ((stat.bannerFormat & 3) == 1) { stat.offsetBanner = offset; offset += 3072; stat.offsetBannerTlut = offset; offset += 512; }
  else if ((stat.bannerFormat & 3) == 2) { stat.offsetBanner = offset; offset += 6144; }
  bool has_tlut = false;
  for (int i = 0; i < 8; ++i) {
    const uint32_t format = (stat.iconFormat >> (2 * i)) & 3;
    if (format == 1) { stat.offsetIcon[i] = offset; offset += 1024; has_tlut = true; }
    else if (format == 2) { stat.offsetIcon[i] = offset; offset += 2048; }
  }
  if (has_tlut) { stat.offsetIconTlut = offset; offset += 512; }
  stat.offsetData = offset;
}
}  // namespace

int32_t h_card_probe(int32_t chan, int32_t* mem_size, int32_t* sector_size) {
  if (chan != 0) return CARD_NOCARD;
  if (mem_size) *mem_size = CARD_MEM_SIZE_MBIT;
  if (sector_size) *sector_size = CARD_SECTOR;
  return CARD_READY;
}
int32_t h_card_mount(int32_t chan) { if (chan != 0) return CARD_NOCARD; card_mount_files(); return CARD_READY; }
int32_t h_card_unmount(int32_t chan) { if (chan != 0) return CARD_NOCARD; card_reset_mount(); return CARD_READY; }
int32_t h_card_open(int32_t chan, const char* name, int32_t* file_no, uint32_t* length) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const int32_t no = card_find(name ? name : "");
  if (no < 0) return CARD_NOFILE;
  if (file_no) *file_no = no;
  if (length) *length = (uint32_t)g_card_files[no]->data.size();
  return CARD_READY;
}
int32_t h_card_close(int32_t chan, int32_t) { return chan == 0 ? CARD_READY : CARD_NOCARD; }
int32_t h_card_create(int32_t chan, const char* name, uint32_t size, int32_t* file_no) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const std::string filename = name ? name : "";
  if (filename.empty() || filename.size() > 32) return CARD_NAMETOOLONG;
  if (card_find(filename) >= 0) return CARD_EXIST;
  if (size == 0 || size % CARD_SECTOR) return CARD_FATAL;
  const uint32_t blocks = size / CARD_SECTOR;
  if (card_used_blocks() + blocks > CARD_TOTAL_BLOCKS) return CARD_INSSPACE;
  int32_t no = -1;
  for (size_t i = 0; i < g_card_files.size(); ++i) if (!g_card_files[i]) { no = (int32_t)i; break; }
  if (no < 0) return CARD_NOENT;
  auto* file = new SourceCardFile;
  std::memcpy(file->dir, card_disk_id(), 6);
  file->dir[6] = 0xFF; file->dir[7] = 0;
  std::memcpy(file->dir + 8, filename.data(), filename.size());
  card_put32(file->dir + 0x28, card_time_2000());
  card_put32(file->dir + 0x2C, 0xFFFFFFFFu);
  card_put16(file->dir + 0x30, 0); card_put16(file->dir + 0x32, 0);
  file->dir[0x34] = 0x04; file->dir[0x35] = 0;
  card_put16(file->dir + 0x36, (uint16_t)(5 + card_used_blocks()));
  card_put16(file->dir + 0x38, (uint16_t)blocks);
  file->dir[0x3A] = 0xFF; file->dir[0x3B] = 0xFF; card_put32(file->dir + 0x3C, 0xFFFFFFFFu);
  file->data.assign(size, 0xFF);
  g_card_files[no] = file;
  card_save(*file);
  if (file_no) *file_no = no;
  return CARD_READY;
}
int32_t h_card_delete(int32_t chan, const char* name) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const int32_t no = card_find(name ? name : "");
  if (no < 0) return CARD_NOFILE;
  SourceCardFile* file = g_card_files[no];
  std::error_code ec; std::filesystem::remove(file->path, ec);
  delete file; g_card_files[no] = nullptr;
  return CARD_READY;
}
int32_t h_card_rename(int32_t chan, const char* old_name, const char* new_name) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const std::string old_file = old_name ? old_name : "", new_file = new_name ? new_name : "";
  if (new_file.empty() || new_file.size() > 32) return CARD_NAMETOOLONG;
  const int32_t no = card_find(old_file);
  if (no < 0) return CARD_NOFILE;
  if (card_find(new_file) >= 0) return CARD_EXIST;
  SourceCardFile& file = *g_card_files[no];
  std::error_code ec; std::filesystem::remove(file.path, ec); file.path.clear();
  std::memset(file.dir + 8, 0, 32); std::memcpy(file.dir + 8, new_file.data(), new_file.size());
  card_save(file);
  return CARD_READY;
}
int32_t h_card_read(int32_t chan, int32_t file_no, void* dst, uint32_t length, uint32_t offset) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardFile& file = *g_card_files[file_no];
  if ((uint64_t)offset + length > file.data.size()) return CARD_LIMIT;
  std::memcpy(dst, file.data.data() + offset, length); return CARD_READY;
}
int32_t h_card_write(int32_t chan, int32_t file_no, const void* src, uint32_t length, uint32_t offset) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardFile& file = *g_card_files[file_no];
  if ((uint64_t)offset + length > file.data.size()) return CARD_LIMIT;
  std::memcpy(file.data.data() + offset, src, length); card_save(file); return CARD_READY;
}
int32_t h_card_stat(int32_t chan, int32_t file_no, void* stat, uint32_t stat_size) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardStat value{}; card_fill_stat(*g_card_files[file_no], value);
  std::memcpy(stat, &value, std::min<uint32_t>(stat_size, sizeof value)); return CARD_READY;
}
int32_t h_card_set_stat(int32_t chan, int32_t file_no, const void* stat, uint32_t stat_size) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  if (stat_size < sizeof(SourceCardStat)) return CARD_LIMIT;
  SourceCardStat value{}; std::memcpy(&value, stat, sizeof value);
  SourceCardFile& file = *g_card_files[file_no];
  file.dir[7] = value.bannerFormat; card_put32(file.dir + 0x2C, value.iconAddr);
  card_put16(file.dir + 0x30, value.iconFormat); card_put16(file.dir + 0x32, value.iconSpeed);
  card_put32(file.dir + 0x3C, value.commentAddr); card_put32(file.dir + 0x28, card_time_2000());
  card_save(file); return CARD_READY;
}
int32_t h_card_free_blocks(int32_t chan, int32_t* bytes_unused, int32_t* files_unused) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (bytes_unused) *bytes_unused = (int32_t)((CARD_TOTAL_BLOCKS - std::min(CARD_TOTAL_BLOCKS, card_used_blocks())) * CARD_SECTOR);
  if (files_unused) *files_unused = (int32_t)(CARD_MAX_FILES - std::count_if(g_card_files.begin(), g_card_files.end(), [](const SourceCardFile* f) { return f != nullptr; }));
  return CARD_READY;
}
int32_t h_card_format(int32_t chan) {
  if (chan != 0) return CARD_NOCARD;
  card_mount_files();
  for (SourceCardFile*& file : g_card_files) {
    if (!file) continue;
    std::error_code ec; std::filesystem::remove(file->path, ec);
    delete file; file = nullptr;
  }
  return CARD_READY;
}

bool card_self_test_impl(const char* directory) {
  if (!directory || !*directory) return false;
  host::options.card_dir = directory;
  card_reset_mount();
  const uint8_t id[6] = {'G', 'A', 'L', 'E', '0', '1'};
  std::memcpy(reinterpret_cast<void*>(MEM1_BASE), id, sizeof id);
  int32_t mem_size = 0, sector_size = 0;
  if (h_card_probe(0, &mem_size, &sector_size) != CARD_READY || mem_size != (int32_t)CARD_MEM_SIZE_MBIT ||
      sector_size != (int32_t)CARD_SECTOR || h_card_mount(0) != CARD_READY)
    return false;
  int32_t file_no = -1;
  if (h_card_create(0, "MU_NATIVE_CARD_TEST", CARD_SECTOR, &file_no) != CARD_READY || file_no < 0)
    return false;
  std::vector<uint8_t> expected(CARD_SECTOR), actual(CARD_SECTOR);
  for (uint32_t i = 0; i < expected.size(); ++i) expected[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);
  if (h_card_write(0, file_no, expected.data(), (uint32_t)expected.size(), 0) != CARD_READY ||
      h_card_read(0, file_no, actual.data(), (uint32_t)actual.size(), 0) != CARD_READY || actual != expected)
    return false;
  SourceCardStat stat{};
  if (h_card_stat(0, file_no, &stat, sizeof stat) != CARD_READY || stat.length != CARD_SECTOR)
    return false;
  if (h_card_unmount(0) != CARD_READY || h_card_mount(0) != CARD_READY)
    return false;
  int32_t reopened = -1; uint32_t length = 0;
  if (h_card_open(0, "MU_NATIVE_CARD_TEST", &reopened, &length) != CARD_READY ||
      reopened < 0 || length != CARD_SECTOR || h_card_read(0, reopened, actual.data(), (uint32_t)actual.size(), 0) != CARD_READY ||
      actual != expected)
    return false;
  if (h_card_format(0) != CARD_READY || h_card_unmount(0) != CARD_READY)
    return false;
  host::log("card: native isolated round-trip passed");
  return true;
}

void h_ai_init_dma(void* buffer, uint32_t length) { audio_core::init_dma(g_audio, buffer, length); }
void h_ai_start_dma(int32_t on) { audio_core::start_dma(g_audio, on != 0, host::cpu->tb, host::TB_HZ); }
void h_ai_set_sample_rate(uint32_t) {}
void h_ai_set_stream_volume(int32_t, int32_t) {}
void h_dsp_mail(uint32_t mail) { audio_core::dsp_mail(mail); }
uint32_t h_dsp_mail_pending() { return audio_core::dsp_mail_pending(); }
void* h_aram_base() { return host::aram; }
uint32_t h_aram_size() { return 0x02000000u; }
void h_aram_dma(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length) {
  if ((uint64_t)aram_offset + length > 0x02000000u) host::die("ARAM DMA out of range %08X+%X", aram_offset, length);
  if (to_aram) std::memcpy(host::aram + aram_offset, mainmem, length);
  else std::memcpy(mainmem, host::aram + aram_offset, length);
}
void* h_native_alloc(uint32_t size) { return std::malloc(size); }
void h_native_free(void* ptr) { std::free(ptr); }

uint32_t h_mem1_size() { return MEM1_SIZE; }
int32_t h_sound_mode() { return 1; }
void h_set_sound_mode(int32_t) {}
int32_t h_progressive_mode() { return 0; }
void h_set_progressive_mode(int32_t) {}
int32_t h_reset_code() { return 0; }
int32_t h_reset_switch() { return 0; }
void h_stop(int32_t reason, int32_t code) {
  host::log("game stopped: reason %d code %d", reason, code);
  host::request_exit(0);
  guarded([] { host::retrace(); });
}

// --match sets this once, before the game starts. An ordinary run never touches it, and the game
// only asks for it where a VS match's rules are finalised.
MuMatchOverride g_match{};
bool g_match_set = false;
const MuMatchOverride* h_match_override() { return g_match_set ? &g_match : nullptr; }

// --rng-seed: forced from host::options once at startup (see main.cpp's flag parsing), read by the
// game at the same first GS_VS frame callback, so a scripted run reaches identical RNG in both the source port and a
// --rng-seed recomp run from the first in-match frame.
void h_rng_seed_override(uint32_t* seed, int32_t* has_value) {
  *has_value = host::options.rng_seed_set ? 1 : 0;
  if (host::options.rng_seed_set) *seed = host::options.rng_seed;
}
// Called from the same first GS_VS frame callback unconditionally (not just when --rng-seed was given), so
// an @match-relative script section starts counting from the match's rules/players being
// finalised on an ordinary offline run too, the same instant the recomp guest's --rng-seed hook
// marks it.
void h_mark_match_start() { host::input_mark_match_start(); }

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
  h.native_alloc = h_native_alloc; h.native_free = h_native_free;
  h.mem1_size = h_mem1_size; h.sound_mode = h_sound_mode; h.set_sound_mode = h_set_sound_mode;
  h.progressive_mode = h_progressive_mode; h.set_progressive_mode = h_set_progressive_mode;
  h.reset_code = h_reset_code; h.reset_switch = h_reset_switch; h.stop = h_stop;
  h.match_override = h_match_override;
  h.rng_seed_override = h_rng_seed_override; h.mark_match_start = h_mark_match_start;
  return h;
}

// A crash inside the game: the faulting address and the game's return addresses on the stack, as
// offsets into melee_game.dll (resolve with addr2line -e melee_game.dbg). The application's own
// crash report still runs afterwards.
LONG CALLBACK on_game_exception(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code < 0x80000000u || code == 0xE06D7363u /* C++ exception */) return EXCEPTION_CONTINUE_SEARCH;
  const uint64_t base = GAME_IMAGE_BASE, end = base + host::game_image_size;
  const uint64_t rip = info->ContextRecord->Rip;
  const bool in_game = rip >= base && rip < end;
  if (!in_game) {
    // A bad indirect call has already left the DLL. Its return address still
    // identifies the game caller, and the unwind below starts from that frame.
    bool called_by_game = false;
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2 &&
        info->ExceptionRecord->ExceptionInformation[0] == 8) {
      __try {
        const uint64_t caller = *(const uint64_t*)info->ContextRecord->Rsp;
        called_by_game = caller >= base && caller < end;
      } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    if (!called_by_game) return EXCEPTION_CONTINUE_SEARCH;
  }
  if (in_game)
    host::log("game crash %08lX at melee_game.dll+0x%llX", code, (unsigned long long)(rip - base));
  else
    host::log("game crash %08lX calling %016llX", code, (unsigned long long)rip);
  const CONTEXT& r = *info->ContextRecord;
  if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
    host::log("  %s address %016llX", info->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" :
              (info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading"),
              (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
  host::log("  rax %016llX rbx %016llX rcx %016llX rdx %016llX", r.Rax, r.Rbx, r.Rcx, r.Rdx);
  host::log("  rsi %016llX rdi %016llX r8  %016llX r9  %016llX", r.Rsi, r.Rdi, r.R8, r.R9);
  // The real call chain, from the unwind tables GCC writes into the DLL (.pdata), so frames are
  // callers rather than whatever return-address-looking values happen to sit on the stack.
  CONTEXT ctx = r;
  for (int depth = 0; depth < 24; ++depth) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    __try {
      if (fn) {
        void* handler_data = nullptr; DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
      } else {
        ctx.Rip = *(const DWORD64*)ctx.Rsp;   // a leaf: the return address is on top
        ctx.Rsp += 8;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
    if (ctx.Rip == 0) break;
    if (ctx.Rip >= base && ctx.Rip < end) host::log("  stack: melee_game.dll+0x%llX", (unsigned long long)(ctx.Rip - 1 - base));
    else { host::log("  (leaves the game at %016llX)", (unsigned long long)ctx.Rip); break; }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

bool card_self_test(const char* directory) { return card_self_test_impl(directory); }

// --match <stage>:<p1>[:<p2>...], each player <kind>[/c<level>][/x<costume>], all numbers decimal
// or 0x-hex. For example "0x14:9:12/c9" is Onett, one human, one level 9 CPU.
bool set_match(const char* spec) {
  if (!spec || !*spec) return false;
  MuMatchOverride m{};
  for (auto& p : m.players) p.kind = -1;

  const char* p = spec;
  char* end = nullptr;
  const long stage = std::strtol(p, &end, 0);
  if (end == p || stage < 0) return false;
  m.stage = (int32_t)stage;
  p = end;

  size_t slot = 0;
  while (*p == ':') {
    ++p;
    if (slot >= sizeof m.players / sizeof m.players[0]) return false;
    const long kind = std::strtol(p, &end, 0);
    if (end == p || kind < 0) return false;
    m.players[slot].kind = (int32_t)kind;
    p = end;
    // Each player's options, in any order: /c<level> makes it a CPU, /x<costume> picks a costume.
    while (*p == '/') {
      const char opt = p[1];
      if (opt != 'c' && opt != 'x') return false;
      p += 2;
      const long v = std::strtol(p, &end, 0);
      if (end == p || v < 0) return false;
      if (opt == 'c') { m.players[slot].cpu = 1; m.players[slot].cpu_level = (int32_t)v; }
      else { m.players[slot].costume = (int32_t)v; }
      p = end;
    }
    ++slot;
  }
  if (*p != '\0' || slot == 0) return false;   // trailing junk, or no players at all

  g_match = m;
  g_match_set = true;
  return true;
}

bool reserve_memory() {
  // First, before anything else in the process can take the range: the game's 32-bit disc pointers
  // expect its memory where the console had it.
  void* mem1 = VirtualAlloc((void*)MEM1_BASE, MEM1_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* lc = VirtualAlloc((void*)LOCKED_CACHE_BASE, LOCKED_CACHE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (mem1 != (void*)MEM1_BASE || lc != (void*)LOCKED_CACHE_BASE) {
    char msg[200];
    std::snprintf(msg, sizeof msg, "Could not reserve the game's memory at 0x80000000 (%p) or 0xE0000000 (%p).", mem1, lc);
    MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
    return false;
  }
  host::ram = (uint8_t*)MEM1_BASE;
  host::ram_size = 64u << 20;   // MEM1 and the game image after it: everything GX can address
  return true;
}

int run(void (*shutdown)(int)) {
  g_shutdown = shutdown;
  host::init_state_digest();
  if (!host::aram) host::aram = (uint8_t*)std::calloc(0x02000000, 1);
  if (!host::cpu) host::cpu = new ppc::Context();   // only its timebase is used: the clock both builds share
  ax::set_memory({ax_native_rd16, ax_native_rd32, ax_native_wr16, ax_native_wr32,
                  host::aram, 0x02000000});
  audio_core::reset(g_audio);
  if (!read_fst()) host::die("cannot read the disc's filesystem table");
  HMODULE module = LoadLibraryA(g_dll.c_str());
  if (!module) host::die("cannot load %s (error %lu)", g_dll.c_str(), GetLastError());
  if ((uintptr_t)module != GAME_IMAGE_BASE) host::die("%s loaded at %p, not at %llX", g_dll.c_str(), (void*)module, (unsigned long long)GAME_IMAGE_BASE);
  const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)module + ((const IMAGE_DOS_HEADER*)module)->e_lfanew);
  host::game_image = (uint8_t*)module;
  host::game_image_size = nt->OptionalHeader.SizeOfImage;
  auto entry = (MuGameEntry)GetProcAddress(module, "mu_game_entry");
  if (!entry) host::die("%s has no mu_game_entry", g_dll.c_str());
  static MuHostApi api = make_host();
  if (entry(&api, &g_game) != 0) host::die("%s refused host API version %u", g_dll.c_str(), api.version);
  if (g_game.version != MU_GAME_API_VERSION || !g_game.state_snapshot)
    host::die("%s has game API version %u, expected %u", g_dll.c_str(), g_game.version, MU_GAME_API_VERSION);
  host::native_retrace = native_retrace;
  host::native_state_snapshot = g_game.state_snapshot;
  AddVectoredExceptionHandler(1, on_game_exception);
  host::log("source port: %s at %p, MEM1 %u MB at %08llX", g_dll.c_str(), (void*)module, MEM1_SIZE >> 20, (unsigned long long)MEM1_BASE);
  int code = 0;
  guarded([&] { code = g_game.run(); });
  host::log("game returned %d", code);
  return code;
}

}  // namespace source_port
