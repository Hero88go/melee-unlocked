// Native Linux implementation of the existing C host/game ABI.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host_services.h"
#include "input_script.h"
#include "ax_ucode.h"
#include "disc_archive.h"
#ifdef MELEE_LINUX_GRAPHICS
#include "gx_core.h"
#endif
#ifdef MELEE_LINUX_USB
#include "gc_adapter.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <link.h>
#include <memory>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#ifdef MELEE_LINUX_SDL
#include <SDL.h>
#endif

namespace linux_host {
namespace {
constexpr uintptr_t locked_cache_base = 0xE0000000u;
constexpr uint32_t locked_cache_bytes = 16u << 10;
constexpr uint64_t ticks_per_frame = MU_TB_HZ / 60;
constexpr uint32_t aram_bytes = 32u << 20;
using Clock = std::chrono::steady_clock;
Config cfg;
Counters count;
MuHostApi api{};
MuGameApi game{};
FILE* log_file = nullptr;
int disc_fd = -1;
uint64_t disc_size = 0;
std::array<uint8_t, 0x440> disc_header{};
void* module = nullptr;
bool memory_reserved = false;
bool initialized = false;
bool resimulating = false;
bool stop_requested = false;
int stop_code = 0;
uint64_t boot_ticks = 0;
uint64_t next_retrace = ticks_per_frame;
Clock::time_point wall_start;
std::vector<uint8_t> aram;
std::vector<MuStateRegion> image_regions;
std::vector<MuStateRegion> image_mappings;
uint32_t image_bytes = 0;
FifoSink fifo_sink = nullptr;
RetraceSink retrace_sink = nullptr;
void* video_user = nullptr;
FrameObserver frame_observer = nullptr;
void* observer_user = nullptr;
std::array<MuPadStatus, 4> scripted_pads{};
std::array<bool, 4> scripted_pad_set{};
InputScript input_script;
int32_t sound = 1, progressive = 0;
struct Stop { int code; };
struct File { uint32_t start, length; bool directory; };
std::vector<File> files;
std::unordered_map<std::string, int32_t> paths;
struct Completion { MuDiscDone callback; int32_t result; void* user; };
std::deque<Completion> completions;
struct Audio {
  const uint8_t* buffer = nullptr;
  uint32_t bytes = 0;
  uint32_t rate = 32000;
  uint64_t next = 0;
  bool running = false, awaiting = false;
} audio;
#ifdef MELEE_LINUX_SDL
std::array<SDL_GameController*, 4> controllers{};
SDL_AudioDeviceID audio_device = 0;
uint32_t sdl_owned = 0;
#endif

void write_log(const char* format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  std::vfprintf(stderr, format, args);
  std::fputc('\n', stderr);
  if (log_file) {
    std::vfprintf(log_file, format, copy);
    std::fputc('\n', log_file);
    std::fflush(log_file);
  }
  va_end(copy);
}

bool fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

bool read_disc(uint64_t offset, void* out, size_t bytes) {
  if (disc_fd < 0 || offset > disc_size || bytes > disc_size - offset || (!out && bytes)) return false;
  auto* dst = static_cast<uint8_t*>(out);
  size_t read = 0;
  while (read < bytes) {
    const ssize_t n = pread(disc_fd, dst + read, bytes - read, static_cast<off_t>(offset + read));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    read += static_cast<size_t>(n);
  }
  return true;
}

// SHA-1 is used only to identify the existing retail DOL, as on Windows. It
// avoids a platform crypto dependency in the bootstrap executable.
std::array<uint8_t, 20> sha1(const std::vector<uint8_t>& input) {
  std::vector<uint8_t> data(input);
  const uint64_t bits = uint64_t(data.size()) * 8;
  data.push_back(0x80);
  while (data.size() % 64 != 56) data.push_back(0);
  for (int n = 7; n >= 0; --n) data.push_back(uint8_t(bits >> (n * 8)));
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  auto rol = [](uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); };
  for (size_t at = 0; at < data.size(); at += 64) {
    uint32_t w[80];
    for (unsigned i = 0; i < 16; ++i) w[i] = be32(data.data() + at + 4 * i);
    for (unsigned i = 16; i < 80; ++i) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
    for (unsigned i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e=d; d=c; c=rol(b, 30); b=a; a=t;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
  }
  std::array<uint8_t, 20> out{};
  for (unsigned i = 0; i < 20; ++i) out[i] = uint8_t(h[i/4] >> (24 - (i%4)*8));
  return out;
}

bool load_disc(std::string* error) {
  disc_fd = open(cfg.iso_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (disc_fd < 0) return fail(error, "cannot open ISO: " + std::string(std::strerror(errno)));
  struct stat st{};
  if (fstat(disc_fd, &st) != 0 || st.st_size < off_t(disc_header.size()))
    return fail(error, "ISO is missing its disc header");
  disc_size = static_cast<uint64_t>(st.st_size);
  if (!read_disc(0, disc_header.data(), disc_header.size())) return fail(error, "cannot read ISO header");
  if (std::memcmp(disc_header.data(), "GALE01", 6) || disc_header[7] != 2 ||
      be32(disc_header.data() + 0x1c) != 0xC2339F3Du)
    return fail(error, "native game requires an uncompressed Melee NTSC 1.02 ISO (GALE01, revision 2)");
  constexpr uint32_t dol_size = 0x4385E0;
  const std::array<uint8_t, 20> dol_hash = {0x08,0xe0,0xbf,0x20,0x13,0x4d,0xfc,0xb2,0x60,0x69,
                                         0x96,0x71,0x00,0x45,0x27,0xb2,0xd6,0xbb,0x1a,0x45};
  std::vector<uint8_t> dol(dol_size);
  if (!read_disc(be32(disc_header.data() + 0x420), dol.data(), dol.size()) || sha1(dol) != dol_hash)
    return fail(error, "ISO DOL does not match retail Melee NTSC 1.02; select a clean base ISO");
  const uint32_t fst_offset = be32(disc_header.data() + 0x424);
  const uint32_t fst_size = be32(disc_header.data() + 0x428);
  if (fst_size < 12 || fst_size > (16u << 20)) return fail(error, "invalid ISO filesystem table size");
  std::vector<uint8_t> fst(fst_size);
  if (!read_disc(fst_offset, fst.data(), fst.size())) return fail(error, "cannot read ISO filesystem table");
  const uint32_t entries = be32(fst.data() + 8);
  if (fst[0] != 1 || !entries || uint64_t(entries) * 12 > fst.size())
    return fail(error, "invalid ISO filesystem root");
  files.assign(entries, File{});
  files[0].directory = true;
  paths.clear();
  paths["/"] = 0;
  std::vector<std::pair<uint32_t, std::string>> directories{{entries, ""}};
  for (uint32_t i = 1; i < entries; ++i) {
    while (directories.size() > 1 && i >= directories.back().first) directories.pop_back();
    const uint8_t* entry = fst.data() + size_t(i) * 12;
    const uint64_t name_at = uint64_t(entries) * 12 + (be32(entry) & 0xFFFFFFu);
    if (entry[0] > 1 || name_at >= fst.size()) return fail(error, "invalid ISO filesystem entry");
    const char* name = reinterpret_cast<const char*>(fst.data() + name_at);
    const char* end = static_cast<const char*>(std::memchr(name, 0, fst.size() - size_t(name_at)));
    if (!end || name == end || std::memchr(name, '/', size_t(end - name)))
      return fail(error, "invalid ISO filename");
    std::string path = directories.back().second + "/" + std::string(name, end);
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!paths.emplace(path, int32_t(i)).second) return fail(error, "duplicate ISO filesystem path");
    const uint32_t start = be32(entry + 4), length = be32(entry + 8);
    files[i] = {start, length, entry[0] != 0};
    if (entry[0]) {
      if (length <= i || length > directories.back().first)
        return fail(error, "invalid ISO directory extent");
      directories.push_back({length, std::move(path)});
    } else if (uint64_t(start) + length > disc_size) return fail(error, "ISO file extends past end of image");
  }
  const auto rumble = paths.find("/lbrb.dat");
  if (rumble == paths.end() || files[rumble->second].directory)
    return fail(error, "ISO is missing LbRb.dat");
  uint8_t archive_header[0x20];
  const auto& rumble_file = files[rumble->second];
  std::string archive_error;
  if (!read_disc(rumble_file.start, archive_header, sizeof archive_header) ||
      !host::disc_archive_header(archive_header, sizeof archive_header, rumble_file.length, &archive_error))
    return fail(error, "ISO contains damaged LbRb.dat: " + archive_error);
  std::memcpy(reinterpret_cast<void*>(mem1_base), disc_header.data(), 0x20);
  auto low = [](uint32_t offset, uint32_t value) { std::memcpy(reinterpret_cast<void*>(mem1_base + offset), &value, 4); };
  low(0x28, 24u << 20); low(0xCC, 0); low(0xF0, 24u << 20);
  low(0xF8, 162000000); low(0xFC, 486000000);
  log("disc: retail NTSC 1.02, %zu filesystem entries, %llu bytes", files.size(), (unsigned long long)disc_size);
  return true;
}

void check_stop() { if (stop_requested) throw Stop{stop_code}; }
uint64_t console_nanoseconds(uint64_t ticks) {
  return (ticks/MU_TB_HZ)*1000000000ull + (ticks%MU_TB_HZ)*1000000000ull/MU_TB_HZ;
}
void pace_to(uint64_t ticks) {
  if (!cfg.pace) return;
  // Multiply only the remainder: ticks*1e9 overflows after about 7.6 minutes.
  const auto elapsed = std::chrono::nanoseconds(console_nanoseconds(ticks));
  std::this_thread::sleep_until(wall_start + elapsed);
}

#ifdef MELEE_LINUX_SDL
void refresh_controllers() {
  for (auto& controller : controllers) {
    if (controller && !SDL_GameControllerGetAttached(controller)) {
      SDL_GameControllerClose(controller);
      controller = nullptr;
    }
  }
  const int devices = SDL_NumJoysticks();
  for (int i = 0; i < devices; ++i) {
    if (!SDL_IsGameController(i)) continue;
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    bool opened = false;
    for (auto* controller : controllers)
      if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == id) opened = true;
    if (opened) continue;
    auto slot = std::find(controllers.begin(), controllers.end(), nullptr);
    if (slot == controllers.end()) break;
    *slot = SDL_GameControllerOpen(i);
    if (*slot) log("pad: port %zu: %s", size_t(slot - controllers.begin()) + 1, SDL_GameControllerName(*slot));
  }
}
#endif

void pump_events() {
#ifdef MELEE_LINUX_SDL
  SDL_PumpEvents();
  SDL_Event quit;
  if (SDL_PeepEvents(&quit, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) request_stop();
  refresh_controllers();
#endif
  check_stop();
}

void audio_tick() {
  if (!audio.running || !audio.buffer || !audio.bytes || audio.awaiting || count.ticks < audio.next) return;
  const uint64_t period = uint64_t(audio.bytes) * MU_TB_HZ / (uint64_t(audio.rate) * 4);
  audio.next += std::max<uint64_t>(period, 1);
  if (audio.next < count.ticks) audio.next = count.ticks + std::max<uint64_t>(period, 1);
  audio.awaiting = true;
  if (game.ai_dma_done) game.ai_dma_done();
}

void deliver_completions() {
  // Move the whole batch first: callbacks may start another read.
  auto pending = std::move(completions);
  completions.clear();
  for (const auto& completion : pending)
    if (completion.callback) completion.callback(completion.result, completion.user);
}

void retrace() {
  check_stop();
  count.ticks = std::max(count.ticks, next_retrace);
  pace_to(count.ticks);
  next_retrace += ticks_per_frame;
  ++count.retraces;
  pump_events();
  deliver_completions();
  audio_tick();
  if (game.fire_alarms) game.fire_alarms(count.ticks);
  if (game.retrace) game.retrace();
  if (retrace_sink) retrace_sink(video_user);
  if (cfg.trace_state || frame_observer || (cfg.max_frames && count.retraces >= cfg.max_frames)) {
    MuStatePod state{};
    if (state_snapshot(&state)) {
      if (cfg.trace_state) log("state: retrace=%u scene=%u/%u match_frame=%u rng=%08X p1=%u/%u p2=%u/%u",
          count.retraces, state.scene_major, state.scene, state.match_frame, state.rng,
          state.player[0].present, state.player[0].action, state.player[1].present, state.player[1].action);
      if (frame_observer) frame_observer(state, count, observer_user);
    }
  }
  if (cfg.max_frames && count.retraces >= cfg.max_frames) request_stop();
  check_stop();
}

void h_log(const char* text) {
  if (!text) return;
  const size_t n = std::strlen(text);
  if (n && text[n-1] == '\n') log("[game] %.*s", int(n-1), text);
  else log("[game] %s", text);
}
void h_panic(const char* file, int32_t line, const char* message) {
  log("game panic: %s:%d: %s", file ? file : "?", line, message ? message : "");
  throw Stop{1};
}
uint64_t h_ticks() { return count.ticks; }
uint64_t h_boot_time() { return boot_ticks; }
void h_poll() {
  check_stop();
  if (resimulating) return;
  count.ticks += 2048;
  deliver_completions();
  audio_tick();
  if (count.ticks >= next_retrace) retrace();
  else if (game.fire_alarms) game.fire_alarms(count.ticks);
}
void h_fifo(const uint8_t* data, uint32_t size) {
  if (!data && size) h_panic("linux_host", 0, "null FIFO data");
  count.fifo_bytes += size;
  if (fifo_sink) fifo_sink(data, size, video_user);
}
void h_vi_configure(uint32_t w, uint32_t h, uint32_t interlaced) {
  log("VI: %ux%u%s", w, h, interlaced ? " interlaced" : "");
}
void h_framebuffer(void*) {}
void h_void() {}
void h_i32_void(int32_t) {}
void h_vi_wait() { if (!resimulating) retrace(); }
uint32_t h_retraces() { return count.retraces; }
uint32_t h_next_field() { return count.retraces & 1; }
int32_t h_vi_idle() {
  if (!cfg.pace || resimulating || !audio.running || !audio.bytes || audio.awaiting || audio.next >= next_retrace) return 0;
  count.ticks = std::max(count.ticks, audio.next);
  pace_to(count.ticks);
  audio_tick();
  if (game.fire_alarms) game.fire_alarms(count.ticks);
  return 1;
}

void h_pad_read(MuPadStatus out[4]) {
  if (!out) return;
  pump_events();
  if (input_script.active()) {
    MuStatePod state{};
    state_snapshot(&state);
    if (input_script.apply(count.retraces,state,out)) {
      for (unsigned port=0;port<4;++port) if (scripted_pad_set[port]) out[port]=scripted_pads[port];
      return;
    }
    log("script: released, controllers live");
  }
  std::memset(out, 0, sizeof(MuPadStatus) * 4);
#ifdef MELEE_LINUX_USB
  MuPadStatus usb_pads[4]{};
  const unsigned usb_mask=linux_gc::poll(usb_pads);
#endif
  for (unsigned port = 0; port < 4; ++port) {
    auto& pad = out[port];
    pad.err = -1;
#ifdef MELEE_LINUX_SDL
    auto* controller = controllers[port];
    if (controller) {
      pad.err = 0;
      auto button = [&](SDL_GameControllerButton b, uint16_t gc) {
        if (SDL_GameControllerGetButton(controller, b)) pad.button |= gc;
      };
      button(SDL_CONTROLLER_BUTTON_A, 0x0100); button(SDL_CONTROLLER_BUTTON_B, 0x0200);
      button(SDL_CONTROLLER_BUTTON_X, 0x0400); button(SDL_CONTROLLER_BUTTON_Y, 0x0800);
      button(SDL_CONTROLLER_BUTTON_START, 0x1000); button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x0010);
      button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x0001); button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x0002);
      button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x0004); button(SDL_CONTROLLER_BUTTON_DPAD_UP, 0x0008);
      auto stick = [&](SDL_GameControllerAxis axis, bool flip) {
        const int value = SDL_GameControllerGetAxis(controller, axis);
        if (std::abs(value) < 4000) return int8_t(0);
        return int8_t(std::clamp(value * (flip ? -80 : 80) / 32767, -80, 80));
      };
      pad.stick_x=stick(SDL_CONTROLLER_AXIS_LEFTX, false); pad.stick_y=stick(SDL_CONTROLLER_AXIS_LEFTY, true);
      pad.sub_x=stick(SDL_CONTROLLER_AXIS_RIGHTX, false); pad.sub_y=stick(SDL_CONTROLLER_AXIS_RIGHTY, true);
      pad.trigger_l=uint8_t(std::max(0, int(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT))) * 255 / 32767);
      pad.trigger_r=uint8_t(std::max(0, int(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT))) * 255 / 32767);
      if (pad.trigger_l >= 240) pad.button |= 0x0040;
      if (pad.trigger_r >= 240) pad.button |= 0x0020;
    } else if (port == 0 && !cfg.headless) {
      pad.err = 0;
      const uint8_t* key = SDL_GetKeyboardState(nullptr);
      if (key[SDL_SCANCODE_Z]) pad.button |= 0x0100;
      if (key[SDL_SCANCODE_X]) pad.button |= 0x0200;
      if (key[SDL_SCANCODE_C]) pad.button |= 0x0400;
      if (key[SDL_SCANCODE_V]) pad.button |= 0x0800;
      if (key[SDL_SCANCODE_S]) pad.button |= 0x0010;
      if (key[SDL_SCANCODE_Q]) { pad.button |= 0x0040; pad.trigger_l = 255; }
      if (key[SDL_SCANCODE_E]) { pad.button |= 0x0020; pad.trigger_r = 255; }
      if (key[SDL_SCANCODE_RETURN]) pad.button |= 0x1000;
      pad.stick_x=int8_t(80 * (int(key[SDL_SCANCODE_RIGHT]) - int(key[SDL_SCANCODE_LEFT])));
      pad.stick_y=int8_t(80 * (int(key[SDL_SCANCODE_UP]) - int(key[SDL_SCANCODE_DOWN])));
      pad.sub_x=int8_t(80 * (int(key[SDL_SCANCODE_L]) - int(key[SDL_SCANCODE_J])));
      pad.sub_y=int8_t(80 * (int(key[SDL_SCANCODE_I]) - int(key[SDL_SCANCODE_K])));
    }
#endif
#ifdef MELEE_LINUX_USB
    if (usb_mask & (1u << port)) pad=usb_pads[port];
#endif
    if (scripted_pad_set[port]) pad = scripted_pads[port];
  }
}
void h_pad_rumble(int32_t port, int32_t on) {
#ifdef MELEE_LINUX_USB
  linux_gc::rumble(port,on!=0);
#endif
#ifdef MELEE_LINUX_SDL
  if (port >= 0 && port < 4 && controllers[port])
    SDL_GameControllerRumble(controllers[port], on ? 0xFFFF : 0, on ? 0xFFFF : 0, on ? 1000 : 0);
#else
  (void)port; (void)on;
#endif
}
int32_t h_disc_entry(const char* raw) {
  std::string path = raw ? raw : "";
  std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (path.empty() || path[0] != '/') path.insert(path.begin(), '/');
  const auto at = paths.find(path);
  return at == paths.end() ? -1 : at->second;
}
int32_t h_disc_file(int32_t entry, uint32_t* start, uint32_t* length) {
  if (entry <= 0 || size_t(entry) >= files.size() || files[entry].directory || !start || !length) return 0;
  *start = files[entry].start; *length = files[entry].length;
  return 1;
}
void h_disc_read(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user) {
  const bool ok = size <= uint32_t(std::numeric_limits<int32_t>::max()) && read_disc(offset, dst, size);
  ++count.disc_reads;
  if (ok) count.disc_bytes += size;
  else log("disc: read %08X+%X failed", offset, size);
  // The game callback queues a drive interrupt. Deliver it from poll() so boot
  // DOL reads and ordinary DVD reads follow the same event contract.
  completions.push_back({done, ok ? int32_t(size) : -1, user});
}
int32_t h_zero_i32() { return 0; }
uint32_t h_zero_u32() { return 0; }
float h_one_f32() { return 1.0f; }
uint32_t h_disc_id(void* out, uint32_t size) {
  const uint32_t n = out ? std::min<uint32_t>(size, 0x20) : 0;
  if (n) std::memcpy(out, disc_header.data(), n);
  return n;
}

// Slot A is a Dolphin-compatible folder of GCI files. These explicit byte
// conversions preserve the Windows port's on-disk format across host ABIs.
namespace card {
constexpr int32_t ready=0, no_card=-3, no_file=-4, exists=-7, no_entry=-8,
                  insufficient=-9, limit=-11, name_too_long=-12, fatal=-128;
constexpr uint32_t sector=0x2000, max_files=127, total_blocks=2043;
struct SavedFile {
  std::array<uint8_t, 64> header{};
  std::vector<uint8_t> data;
  std::filesystem::path path;
  std::string name() const {
    const char* name=reinterpret_cast<const char*>(header.data()+8);
    return std::string(name, strnlen(name, 32));
  }
  uint32_t blocks() const { return uint32_t(header[0x38]) << 8 | header[0x39]; }
};
struct Stat {
  char name[32];
  uint32_t length, time;
  uint8_t game[4], company[2], banner;
  uint32_t icon;
  uint16_t icon_format, icon_speed;
  uint32_t comment, offset_banner, offset_banner_tlut, offset_icon[8], offset_icon_tlut, offset_data;
};
static_assert(sizeof(Stat)==0x6C, "game CARDStat ABI");
std::array<std::unique_ptr<SavedFile>, max_files> saved;
std::filesystem::path directory;
bool mounted=false;
uint16_t get16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
void put16(uint8_t* p, uint16_t n) { p[0]=uint8_t(n >> 8); p[1]=uint8_t(n); }
void put32(uint8_t* p, uint32_t n) { for (unsigned i=0; i<4; ++i) p[i]=uint8_t(n >> (24-8*i)); }
uint32_t used() {
  uint32_t n=0;
  for (const auto& file : saved) if (file) n+=file->blocks();
  return n;
}
bool same_game(const SavedFile& file) { return !std::memcmp(file.header.data(), disc_header.data(), 6); }
int32_t find(const char* name) {
  if (!name) return -1;
  for (uint32_t i=0; i<max_files; ++i) if (saved[i] && same_game(*saved[i]) && saved[i]->name()==name) return int32_t(i);
  return -1;
}
std::string filename(const SavedFile& file) {
  std::string name;
  auto safe=[](unsigned char c) { return c<0x80 && (std::isalnum(c) || c=='_' || c=='-' || c=='.'); };
  for (unsigned i=0; i<6; ++i) name+=safe(file.header[i]) ? char(file.header[i]) : '_';
  name+='-';
  for (unsigned char c : file.name()) name+=safe(c) ? char(c) : '_';
  // Distinct game names that sanitize identically must not share a disk path.
  uint32_t hash=2166136261u;
  for (unsigned i=0; i<40; ++i) { hash^=file.header[i]; hash*=16777619u; }
  char suffix[20]; std::snprintf(suffix, sizeof suffix, "-%08x.gci", hash);
  return name+suffix;
}
bool persist(SavedFile& file) {
  const auto target=file.path.empty() ? directory/filename(file) : file.path;
  std::string temporary=(target.string()+".tmp-XXXXXX");
  std::vector<char> path(temporary.begin(), temporary.end()); path.push_back(0);
  const int fd=mkstemp(path.data());
  if (fd<0) { log("card: cannot create save: %s", std::strerror(errno)); return false; }
  auto write_all=[&](const uint8_t* data, size_t bytes) {
    while (bytes) {
      const ssize_t n=write(fd, data, bytes);
      if (n<0 && errno==EINTR) continue;
      if (n<=0) return false;
      data+=n; bytes-=size_t(n);
    }
    return true;
  };
  bool ok=write_all(file.header.data(), file.header.size()) && write_all(file.data.data(), file.data.size());
  if (ok) ok=fsync(fd)==0;
  const int saved_errno=errno;
  if (close(fd)!=0) ok=false;
  if (ok) ok=rename(path.data(), target.c_str())==0;
  if (!ok) { unlink(path.data()); log("card: cannot commit save %s: %s", target.c_str(), std::strerror(saved_errno)); return false; }
  file.path=target;
  // Flush the directory entry as well as the file data for crash-safe saves.
  const int dirfd=open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dirfd>=0) { fsync(dirfd); close(dirfd); }
  return true;
}
int32_t mount(int32_t chan) {
  if (chan!=0) return no_card;
  if (mounted) return ready;
  if (!cfg.card_dir.empty()) directory=cfg.card_dir;
  else {
    const char* xdg=std::getenv("XDG_DATA_HOME");
    const char* home=std::getenv("HOME");
    if (xdg && *xdg && xdg[0]=='/') directory=std::filesystem::path(xdg)/"melee-unlocked"/"Card A";
    else if (home && *home) directory=std::filesystem::path(home)/".local"/"share"/"melee-unlocked"/"Card A";
    else return no_card;
  }
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) { log("card: cannot open folder %s: %s", directory.c_str(), ec.message().c_str()); return no_card; }
  std::vector<std::filesystem::path> entries;
  for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it!=end; it.increment(ec))
    if (it->path().extension()==".gci") entries.push_back(it->path());
  if (ec) return no_card;
  std::sort(entries.begin(), entries.end());
  for (auto& file : saved) file.reset();
  size_t slot=0;
  for (const auto& path : entries) {
    if (slot>=max_files) break;
    auto file=std::make_unique<SavedFile>();
    FILE* in=std::fopen(path.c_str(), "rb");
    if (!in) continue;
    const bool header_ok=std::fread(file->header.data(), 1, 64, in)==64;
    const uint32_t blocks=header_ok ? file->blocks() : 0;
    const uint64_t length=std::filesystem::file_size(path, ec);
    bool ok=!ec && blocks && blocks<=total_blocks && length==64ull+uint64_t(blocks)*sector;
    if (ok) { file->data.resize(size_t(blocks)*sector); ok=std::fread(file->data.data(),1,file->data.size(),in)==file->data.size(); }
    std::fclose(in);
    if (!ok || used()+blocks>total_blocks) { log("card: ignored invalid or oversized GCI %s", path.c_str()); continue; }
    file->path=path;
    if (same_game(*file) && find(file->name().c_str())>=0) {
      log("card: duplicate save name in %s; using the first file alphabetically", path.c_str());
      continue;
    }
    saved[slot++]=std::move(file);
  }
  mounted=true;
  log("card: native slot A: %s, %zu files", directory.c_str(), slot);
  return ready;
}
SavedFile* get(int32_t chan, int32_t file) {
  return chan==0 && mounted && file>=0 && uint32_t(file)<max_files ? saved[size_t(file)].get() : nullptr;
}
int32_t missing(int32_t chan) { return chan!=0 || !mounted ? no_card : no_file; }
uint32_t now() { return uint32_t((boot_ticks+count.ticks)/MU_TB_HZ); }
void reset() { for (auto& file : saved) file.reset(); mounted=false; directory.clear(); }
void fill_stat(const SavedFile& file, Stat& stat) {
  stat={};
  std::memcpy(stat.name, file.header.data()+8, 32);
  stat.length=uint32_t(file.data.size()); stat.time=be32(file.header.data()+0x28);
  std::memcpy(stat.game,file.header.data(),4); std::memcpy(stat.company,file.header.data()+4,2);
  stat.banner=file.header[7]; stat.icon=be32(file.header.data()+0x2C);
  stat.icon_format=get16(file.header.data()+0x30); stat.icon_speed=get16(file.header.data()+0x32);
  stat.comment=be32(file.header.data()+0x3C);
  std::fill(std::begin(stat.offset_icon),std::end(stat.offset_icon),UINT32_MAX);
  stat.offset_banner=stat.offset_banner_tlut=stat.offset_icon_tlut=UINT32_MAX;
  if (stat.icon==UINT32_MAX) return;
  uint32_t offset=stat.icon;
  if ((stat.banner&3)==1) { stat.offset_banner=offset; offset+=3072; stat.offset_banner_tlut=offset; offset+=512; }
  else if ((stat.banner&3)==2) { stat.offset_banner=offset; offset+=6144; }
  bool tlut=false;
  for (unsigned i=0;i<8;++i) {
    const uint32_t format=(stat.icon_format>>(2*i))&3;
    if (format==1) { stat.offset_icon[i]=offset; offset+=1024; tlut=true; }
    else if (format==2) { stat.offset_icon[i]=offset; offset+=2048; }
  }
  if (tlut) { stat.offset_icon_tlut=offset; offset+=512; }
  stat.offset_data=offset;
}
} // namespace card
int32_t h_card_probe(int32_t chan, int32_t* size, int32_t* sector) {
  if (chan!=0) return card::no_card;
  if (size) *size=128;
  if (sector) *sector=card::sector;
  return card::ready;
}
int32_t h_card_mount(int32_t chan) { return card::mount(chan); }
int32_t h_card_unmount(int32_t chan) { if (chan!=0) return card::no_card; card::reset(); return card::ready; }
int32_t h_card_open(int32_t chan, const char* name, int32_t* file, uint32_t* length) {
  if (chan!=0 || !card::mounted) return card::no_card;
  const int32_t index=card::find(name);
  if (index<0) return card::no_file;
  if (file) *file=index;
  if (length) *length=uint32_t(card::saved[size_t(index)]->data.size());
  return card::ready;
}
int32_t h_card_close(int32_t chan, int32_t file) { return card::get(chan,file) ? card::ready : card::missing(chan); }
int32_t h_card_create(int32_t chan, const char* name, uint32_t bytes, int32_t* file_no) {
  if (chan!=0 || !card::mounted) return card::no_card;
  const size_t length=name ? strnlen(name,33) : 0;
  if (!length || length>32) return card::name_too_long;
  if (card::find(name)>=0) return card::exists;
  if (!bytes || bytes%card::sector) return card::fatal;
  const uint32_t blocks=bytes/card::sector;
  if (blocks>card::total_blocks || blocks+card::used()>card::total_blocks) return card::insufficient;
  const auto slot=std::find_if(card::saved.begin(),card::saved.end(),[](const auto& p){return !p;});
  if (slot==card::saved.end()) return card::no_entry;
  auto file=std::make_unique<card::SavedFile>();
  auto* h=file->header.data();
  std::memcpy(h,disc_header.data(),6); h[6]=0xFF;
  std::memcpy(h+8,name,length); card::put32(h+0x28,card::now());
  card::put32(h+0x2C,UINT32_MAX); h[0x34]=4;
  card::put16(h+0x36,uint16_t(5+card::used())); card::put16(h+0x38,uint16_t(blocks));
  h[0x3A]=h[0x3B]=0xFF; card::put32(h+0x3C,UINT32_MAX);
  file->data.assign(bytes,0xFF);
  if (!card::persist(*file)) return card::fatal;
  if (file_no) *file_no=int32_t(slot-card::saved.begin());
  *slot=std::move(file);
  return card::ready;
}
int32_t h_card_delete(int32_t chan, const char* name) {
  if (chan!=0 || !card::mounted) return card::no_card;
  const int32_t index=card::find(name);
  if (index<0) return card::no_file;
  auto& file=card::saved[size_t(index)];
  if (unlink(file->path.c_str())!=0 && errno!=ENOENT) return card::fatal;
  file.reset(); return card::ready;
}
int32_t h_card_rename(int32_t chan, const char* old_name, const char* new_name) {
  if (chan!=0 || !card::mounted) return card::no_card;
  const size_t length=new_name ? strnlen(new_name,33) : 0;
  if (!length || length>32) return card::name_too_long;
  const int32_t index=card::find(old_name);
  if (index<0) return card::no_file;
  if (card::find(new_name)>=0) return card::exists;
  auto& file=*card::saved[size_t(index)];
  card::SavedFile replacement=file;
  std::memset(replacement.header.data()+8,0,32);
  std::memcpy(replacement.header.data()+8,new_name,length);
  replacement.path.clear();
  if (!card::persist(replacement)) return card::fatal;
  if (unlink(file.path.c_str())!=0 && errno!=ENOENT) { unlink(replacement.path.c_str()); return card::fatal; }
  file=std::move(replacement);
  return card::ready;
}
int32_t h_card_read(int32_t chan, int32_t file_no, void* out, uint32_t bytes, uint32_t offset) {
  const auto* file=card::get(chan,file_no);
  if (!file) return card::missing(chan);
  if ((!out && bytes) || uint64_t(offset)+bytes>file->data.size()) return card::limit;
  if (bytes) std::memcpy(out,file->data.data()+offset,bytes);
  return card::ready;
}
int32_t h_card_write(int32_t chan, int32_t file_no, const void* data, uint32_t bytes, uint32_t offset) {
  auto* file=card::get(chan,file_no);
  if (!file) return card::missing(chan);
  if ((!data && bytes) || uint64_t(offset)+bytes>file->data.size()) return card::limit;
  // Retain the previous in-memory data when a disk write fails.
  card::SavedFile changed=*file;
  if (bytes) std::memcpy(changed.data.data()+offset,data,bytes);
  if (!card::persist(changed)) return card::fatal;
  *file=std::move(changed); return card::ready;
}
int32_t h_card_stat(int32_t chan, int32_t file_no, void* out, uint32_t bytes) {
  const auto* file=card::get(chan,file_no);
  if (!file) return card::missing(chan);
  if (!out || bytes<sizeof(card::Stat)) return card::limit;
  card::Stat stat{}; card::fill_stat(*file,stat); std::memcpy(out,&stat,sizeof stat);
  return card::ready;
}
int32_t h_card_set_stat(int32_t chan, int32_t file_no, const void* data, uint32_t bytes) {
  auto* file=card::get(chan,file_no);
  if (!file) return card::missing(chan);
  if (!data || bytes<sizeof(card::Stat)) return card::limit;
  card::Stat stat{}; std::memcpy(&stat,data,sizeof stat);
  card::SavedFile changed=*file; auto* h=changed.header.data();
  h[7]=stat.banner; card::put32(h+0x2C,stat.icon); card::put16(h+0x30,stat.icon_format);
  card::put16(h+0x32,stat.icon_speed); card::put32(h+0x3C,stat.comment); card::put32(h+0x28,card::now());
  if (!card::persist(changed)) return card::fatal;
  *file=std::move(changed); return card::ready;
}
int32_t h_card_free(int32_t chan, int32_t* bytes, int32_t* entries) {
  if (chan!=0 || !card::mounted) return card::no_card;
  if (bytes) *bytes=int32_t((card::total_blocks-std::min(card::total_blocks,card::used()))*card::sector);
  if (entries) *entries=int32_t(std::count_if(card::saved.begin(),card::saved.end(),[](const auto& p){return !p;}));
  return card::ready;
}
int32_t h_card_format(int32_t chan) {
  const int32_t result=card::mount(chan);
  if (result!=card::ready) return result;
  for (auto& file : card::saved) if (file) {
    if (unlink(file->path.c_str())!=0 && errno!=ENOENT) return card::fatal;
    file.reset();
  }
  return card::ready;
}

void h_ai_dma(void* buffer, uint32_t length) {
  audio.buffer = static_cast<const uint8_t*>(buffer);
  audio.bytes = length;
  audio.awaiting = false;
}
void h_ai_start(int32_t on) {
  if (on && !audio.running && audio.bytes)
    audio.next = count.ticks + uint64_t(audio.bytes) * MU_TB_HZ / (uint64_t(audio.rate) * 4);
  audio.running = on && audio.bytes;
}
void h_ai_rate(uint32_t rate48) { audio.rate = rate48 ? 48000 : 32000; }
void h_ai_volume(int32_t, int32_t) {}
void h_dsp_mail(uint32_t mail) { ax::handle_mail(mail); }
void* h_aram() { return aram.data(); }
uint32_t h_aram_size() { return aram_bytes; }
void h_aram_dma(int32_t to_aram, void* mainmem, uint32_t offset, uint32_t length) {
  if ((!mainmem && length) || uint64_t(offset) + length > aram.size())
    h_panic("linux_host", 0, "ARAM DMA out of range");
  if (to_aram) std::memcpy(aram.data() + offset, mainmem, length);
  else std::memcpy(mainmem, aram.data() + offset, length);
}
void* h_alloc(uint32_t bytes) { return std::malloc(bytes); }
void h_free(void* ptr) { std::free(ptr); }
uint32_t h_mem1() { return mem1_bytes; }
int32_t h_sound() { return sound; }
void h_set_sound(int32_t value) { sound = value != 0; }
int32_t h_progressive() { return progressive; }
void h_set_progressive(int32_t value) { progressive = value != 0; }
void h_stop(int32_t reason, int32_t code) { log("game stopped: reason=%d code=%d", reason, code); throw Stop{reason == MU_STOP_PANIC ? 1 : code}; }
const MuMatchOverride* h_match() { return cfg.has_match ? &cfg.match : nullptr; }
void h_rng(uint32_t* seed, int32_t* has) {
  if (has) *has = cfg.has_rng_seed;
  if (cfg.has_rng_seed && seed) *seed = cfg.rng_seed;
}
void h_match_start() {
  input_script.mark_match_start(count.retraces);
  log("match: rules and players finalized at retrace %u", count.retraces);
}
uint32_t h_options() { return cfg.vanilla_game ? MU_GAME_OPTION_VANILLA : 0; }
uint32_t h_music() { return 100; }
uint32_t h_hud_scales() { return 100u | (100u << 8); }
#ifdef MELEE_LINUX_GRAPHICS
int32_t h_pose_enabled() { return gx::native_pose_capture_enabled() ? 1 : 0; }
void h_pose_snapshot(const MuNativePoseSnapshot* snapshot) { if (snapshot) gx::native_pose_snapshot(snapshot); }
void h_render_scope(const MuNativeRenderScopeEvent* event) {
  if (event) gx::native_render_scope_event({event->sequence,event->scope_id,event->phase,event->reserved});
}
int32_t h_draw_identity() { return 1; }
#endif
int32_t h_slippi(uint8_t, const uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t* size) {
  if (size) *size = 0;
  return -1;
}
void h_resim(int32_t entering) { resimulating = entering != 0; }

MuHostApi make_api() {
  MuHostApi h{};
  h.version = MU_HOST_API_VERSION;
  h.log=h_log; h.panic=h_panic; h.ticks=h_ticks; h.boot_time=h_boot_time; h.poll=h_poll;
  h.gx_fifo=h_fifo; h.vi_configure=h_vi_configure; h.vi_set_next_framebuffer=h_framebuffer;
  h.vi_flush=h_void; h.vi_retrace_count=h_retraces; h.vi_next_field=h_next_field;
  h.vi_set_black=h_i32_void; h.vi_wait_retrace=h_vi_wait;
  h.pad_read=h_pad_read; h.pad_rumble=h_pad_rumble;
  h.disc_entrynum=h_disc_entry; h.disc_file=h_disc_file; h.disc_read=h_disc_read;
  h.disc_status=h_zero_i32; h.disc_id=h_disc_id;
  h.card_probe=h_card_probe; h.card_mount=h_card_mount; h.card_unmount=h_card_unmount;
  h.card_open=h_card_open; h.card_close=h_card_close; h.card_create=h_card_create;
  h.card_delete=h_card_delete; h.card_rename=h_card_rename; h.card_read=h_card_read;
  h.card_write=h_card_write; h.card_stat=h_card_stat; h.card_set_stat=h_card_set_stat;
  h.card_free_blocks=h_card_free; h.card_format=h_card_format;
  h.ai_init_dma=h_ai_dma; h.ai_start_dma=h_ai_start; h.ai_set_sample_rate=h_ai_rate;
  h.ai_set_stream_volume=h_ai_volume; h.dsp_mail=h_dsp_mail; h.dsp_mail_pending=h_zero_u32; h.picture_widen=h_one_f32;
  h.aram_base=h_aram; h.aram_size=h_aram_size; h.aram_dma=h_aram_dma;
  h.native_alloc=h_alloc; h.native_free=h_free;
  h.mem1_size=h_mem1; h.sound_mode=h_sound; h.set_sound_mode=h_set_sound;
  h.progressive_mode=h_progressive; h.set_progressive_mode=h_set_progressive;
  h.reset_code=h_zero_i32; h.reset_switch=h_zero_i32; h.stop=h_stop;
  h.match_override=h_match; h.rng_seed_override=h_rng; h.mark_match_start=h_match_start;
  h.native_pose_capture_enabled=h_zero_i32; h.native_owner_tracking=h_zero_i32;
  h.game_options=h_options; h.game_options2=h_zero_u32; h.game_options3=h_zero_u32;
  h.display_options=h_zero_u32; h.music_volume=h_music; h.native_draw_identity=h_zero_i32;
  h.mod_flags=h_zero_u32; h.slippi_command=h_slippi; h.resim_phase=h_resim;
  h.hud_scales=h_hud_scales; h.vi_idle_step=h_vi_idle;
#ifdef MELEE_LINUX_GRAPHICS
  h.native_pose_capture_enabled=h_pose_enabled;
  h.native_pose_snapshot=h_pose_snapshot;
  h.native_render_scope_event=h_render_scope;
  h.native_draw_identity=h_draw_identity;
#endif
  return h;
}

template<typename T> T ax_read(uint32_t address) {
  T value{};
  auto* p = memory_pointer(address, sizeof value);
  if (!p) h_panic("linux_host", 0, "AX read outside native memory");
  std::memcpy(&value, p, sizeof value);
  return value;
}
template<typename T> void ax_write(uint32_t address, T value) {
  auto* p = memory_pointer(address, sizeof value);
  if (!p) h_panic("linux_host", 0, "AX write outside native memory");
  std::memcpy(p, &value, sizeof value);
}
void ax_output(const int16_t* rl, uint32_t frames) {
  ++count.audio_blocks;
#ifdef MELEE_LINUX_SDL
  if (!audio_device || !rl || !frames) return;
  std::vector<int16_t> lr(size_t(frames) * 2);
  for (uint32_t i=0; i<frames; ++i) { lr[2*i]=rl[2*i+1]; lr[2*i+1]=rl[2*i]; }
  // Bound queued audio after a stalled simulation. Dropping an excessive queue
  // prevents seconds of obsolete sound playing after a debugger resumes.
  if (SDL_GetQueuedAudioSize(audio_device) > 32000u * 4 / 4) SDL_ClearQueuedAudio(audio_device);
  SDL_QueueAudio(audio_device, lr.data(), uint32_t(lr.size() * sizeof(int16_t)));
#else
  (void)rl; (void)frames;
#endif
}

bool inspect_image(std::string* error) {
  struct link_map* map = nullptr;
  if (dlinfo(module, RTLD_DI_LINKMAP, &map) != 0 || !map) return fail(error, "cannot inspect game ELF mappings");
  struct Scan { uintptr_t bias; bool found; } scan{uintptr_t(map->l_addr), false};
  dl_iterate_phdr([](dl_phdr_info* info, size_t, void* user) {
    auto& scan = *static_cast<Scan*>(user);
    if (uintptr_t(info->dlpi_addr) != scan.bias) return 0;
    // l_addr may also be zero for the host executable. Match the exported
    // symbol's owning library by its low fixed load address.
    bool native = false;
    for (ElfW(Half) i=0; i<info->dlpi_phnum; ++i)
      if (info->dlpi_phdr[i].p_type == PT_LOAD &&
          info->dlpi_addr + info->dlpi_phdr[i].p_vaddr == game_image_base) native = true;
    if (!native) return 0;
    scan.found = true;
    for (ElfW(Half) i=0; i<info->dlpi_phnum; ++i) {
      const auto& p = info->dlpi_phdr[i];
      if (p.p_type != PT_LOAD || !p.p_memsz || p.p_memsz > std::numeric_limits<uint32_t>::max()) continue;
      const uintptr_t address = uintptr_t(info->dlpi_addr + p.p_vaddr);
      image_mappings.push_back({reinterpret_cast<void*>(address), uint32_t(p.p_memsz)});
      if (p.p_flags & PF_W) image_regions.push_back({reinterpret_cast<void*>(address), uint32_t(p.p_memsz)});
      if (address + p.p_memsz >= game_image_base && address + p.p_memsz - game_image_base <= UINT32_MAX)
        image_bytes = std::max(image_bytes, uint32_t(address + p.p_memsz - game_image_base));
    }
    return 1;
  }, &scan);
  if (!scan.found || image_mappings.empty() || image_regions.empty())
    return fail(error, "melee_game.so did not load at 0x82800000; rebuild with the Linux fixed-address linker script");
  if (uint64_t(game_image_base) + image_bytes > mem1_base + (64u << 20))
    return fail(error, "game ELF extends past the 64 MiB GX physical address window");
  log("source game: ELF at %08lX, %u bytes, %zu writable load segments", (unsigned long)game_image_base, image_bytes, image_regions.size());
  return true;
}
} // namespace

void log(const char* format, ...) { va_list args; va_start(args, format); write_log(format, args); va_end(args); }
void set_video_sinks(FifoSink fifo, RetraceSink retrace, void* user) { fifo_sink=fifo; retrace_sink=retrace; video_user=user; }
void set_frame_observer(FrameObserver observer, void* user) { frame_observer=observer; observer_user=user; }
bool reserve_memory(std::string* error) {
  if (memory_reserved) return true;
  auto reserve = [](uintptr_t address, size_t bytes) -> void* {
#ifdef MAP_FIXED_NOREPLACE
    void* p = mmap(reinterpret_cast<void*>(address), bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    // WSL1 and old kernels reject NOREPLACE. A non-fixed hint is equally safe
    // when its result is checked: it can never overwrite a live mapping.
    if (p==MAP_FAILED && (errno==EINVAL || errno==ENOTSUP || errno==ENOSYS))
      p=mmap(reinterpret_cast<void*>(address),bytes,PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
    // Kernels before 4.17 can ignore NOREPLACE. Always check the result.
#else
    void* p = mmap(reinterpret_cast<void*>(address), bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    if (p != MAP_FAILED && uintptr_t(p) != address) { munmap(p, bytes); errno=EEXIST; return MAP_FAILED; }
    return p;
  };
  void* mem = reserve(mem1_base, mem1_bytes);
  if (mem == MAP_FAILED) return fail(error, "cannot reserve MEM1 at 0x80000000: " + std::string(std::strerror(errno)));
  void* cache = reserve(locked_cache_base, locked_cache_bytes);
  if (cache == MAP_FAILED) {
    const std::string message = "cannot reserve locked cache at 0xE0000000: " + std::string(std::strerror(errno));
    munmap(mem, mem1_bytes);
    return fail(error, message);
  }
  memory_reserved = true;
  return true;
}

bool initialize(const Config& config, std::string* error) {
  if (initialized || module || disc_fd >= 0) return fail(error, "native Linux host is already initialized; shut it down first");
  cfg=config; count={}; game={}; audio={}; stop_requested=false; stop_code=0; resimulating=false;
  next_retrace=ticks_per_frame; sound=1; progressive=0;
  if (!cfg.log_path.empty()) {
    log_file=std::fopen(cfg.log_path.c_str(), "w");
    if (!log_file) return fail(error, "cannot open log file: " + std::string(std::strerror(errno)));
  }
  auto startup_failed = [&] { shutdown(); return false; };
  if (!reserve_memory(error)) return startup_failed();
  if (!cfg.input_script.empty() && !input_script.load(cfg.input_script,error)) return startup_failed();
  // Linux uses the game's own native AX music stream until a native jukebox
  // service is wired. Keep ordinary Melee Unlocked gameplay options enabled.
  if (!std::getenv("MELEE_NO_JUKEBOX")) setenv("MELEE_NO_JUKEBOX","1",0);
  if (!load_disc(error)) return startup_failed();
  if (cfg.has_time_base) boot_ticks=cfg.time_base;
  else {
    // The console RTC stores local wall time. Match the Windows host by
    // interpreting that local calendar as UTC, rather than dating saves UTC.
    const std::time_t now=std::time(nullptr);
    std::tm local{};
    localtime_r(&now,&local);
    const int64_t local_seconds=timegm(&local);
    boot_ticks=uint64_t(std::max<int64_t>(0,local_seconds-946684800))*MU_TB_HZ;
  }
  aram.assign(aram_bytes, 0);
  api=make_api();
  module=dlopen(cfg.game_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module) { fail(error, "cannot load native game ELF: " + std::string(dlerror())); return startup_failed(); }
  if (!inspect_image(error)) return startup_failed();
  dlerror();
  auto entry=reinterpret_cast<MuGameEntry>(dlsym(module, "mu_game_entry"));
  if (!entry || entry(&api, &game) != 0 || game.version != MU_GAME_API_VERSION ||
      !game.run || !game.retrace || !game.fire_alarms || !game.state_snapshot ||
      !game.state_regions || !game.state_exclusions || !game.snapshot_ranges || !game.practice) {
    fail(error, "native game/host ABI mismatch; rebuild the whole Linux v0.9.0 target together");
    return startup_failed();
  }
  ax::set_memory({ax_read<uint16_t>, ax_read<uint32_t>, ax_write<uint16_t>, ax_write<uint32_t>, aram.data(), aram_bytes});
  ax::reset(); ax::set_output_sink(ax_output);
#ifdef MELEE_LINUX_SDL
  const uint32_t requested=SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | (cfg.headless ? 0 : SDL_INIT_AUDIO);
  sdl_owned=requested & ~SDL_WasInit(requested);
  if (SDL_InitSubSystem(sdl_owned) != 0) log("SDL input/audio initialization: %s", SDL_GetError());
  if (!cfg.headless) {
    SDL_AudioSpec want{};
    want.freq=32000; want.format=AUDIO_S16SYS; want.channels=2; want.samples=512;
    audio_device=SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    else log("audio: no SDL output device: %s", SDL_GetError());
  }
  refresh_controllers();
#endif
#ifdef MELEE_LINUX_USB
  if (!cfg.headless && !cfg.no_gc_adapter && !std::getenv("MELEE_NO_GC_ADAPTER")) linux_gc::initialize();
#endif
  initialized=true;
  log("native Linux host: API %u/%u, MEM1 %u MiB, ARAM %u MiB%s", api.version, game.version,
      mem1_bytes >> 20, aram_bytes >> 20, cfg.headless ? ", headless" : "");
  return true;
}

int run() {
  if (!initialized) { log("native Linux host is not initialized"); return 1; }
  wall_start=Clock::now();
  try { return game.run(); }
  catch (const Stop& stopped) { return stopped.code; }
  catch (const std::exception& error) { log("native host failure: %s", error.what()); return 1; }
}

void shutdown() {
  initialized=false;
  card::reset();
  input_script.clear();
#ifdef MELEE_LINUX_USB
  linux_gc::shutdown();
#endif
  ax::set_output_sink(nullptr);
  ax::set_memory({});
#ifdef MELEE_LINUX_SDL
  if (audio_device) { SDL_CloseAudioDevice(audio_device); audio_device=0; }
  for (auto& controller : controllers) { if (controller) SDL_GameControllerClose(controller); controller=nullptr; }
  if (sdl_owned) { SDL_QuitSubSystem(sdl_owned); sdl_owned=0; }
#endif
  if (module) { dlclose(module); module=nullptr; }
  game={}; api={}; image_regions.clear(); image_mappings.clear(); image_bytes=0;
  if (disc_fd >= 0) { close(disc_fd); disc_fd=-1; }
  files.clear(); paths.clear(); completions.clear(); aram.clear();
  if (memory_reserved) {
    munmap(reinterpret_cast<void*>(mem1_base), mem1_bytes);
    munmap(reinterpret_cast<void*>(locked_cache_base), locked_cache_bytes);
    memory_reserved=false;
  }
  if (log_file) { std::fclose(log_file); log_file=nullptr; }
}
void set_pad_override(unsigned port, const MuPadStatus& pad) { if (port < 4) { scripted_pads[port]=pad; scripted_pad_set[port]=true; } }
void clear_pad_override(unsigned port) { if (port < 4) scripted_pad_set[port]=false; }
bool state_snapshot(MuStatePod* out) { if (!out || !game.state_snapshot) return false; game.state_snapshot(out); return true; }
const Counters& counters() { return count; }
const MuHostApi& host_api() { return api; }
const MuGameApi& game_api() { return game; }
const std::vector<MuStateRegion>& writable_image_regions() { return image_regions; }
uint32_t game_image_size() { return image_bytes; }
uint8_t* memory_pointer(uint32_t address, uint32_t size) {
  uintptr_t at=address;
  if (address < (64u << 20)) at=mem1_base + address;
  else if (address >= 0xC0000000u && address < 0xC4000000u) at=mem1_base + (address & 0x03FFFFFFu);
  const uint64_t end=uint64_t(at) + size;
  if (memory_reserved && at >= mem1_base && end <= mem1_base + mem1_bytes) return reinterpret_cast<uint8_t*>(at);
  if (memory_reserved && at >= locked_cache_base && end <= locked_cache_base + locked_cache_bytes) return reinterpret_cast<uint8_t*>(at);
  for (const auto& region : image_mappings) {
    const uintptr_t start=reinterpret_cast<uintptr_t>(region.address);
    if (at >= start && end <= uint64_t(start) + region.size) return reinterpret_cast<uint8_t*>(at);
  }
  return nullptr;
}
void request_stop(int code) { stop_requested=true; stop_code=code; }
bool service_self_test(const char* scratch_directory, std::string* error) {
  if (initialized || module || disc_fd>=0) return fail(error,"self-test requires an inactive host");
  if (!scratch_directory || !*scratch_directory) return fail(error,"self-test requires a scratch directory");
  std::error_code ec;
  const std::filesystem::path root=std::filesystem::absolute(scratch_directory,ec);
  if (ec) return fail(error,"cannot resolve self-test scratch directory");
  std::filesystem::create_directories(root,ec);
  if (ec) return fail(error,"cannot create self-test scratch directory: "+ec.message());
  const std::string pattern=(root/"linux-host-test-XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
  if (!mkdtemp(name.data())) return fail(error,"cannot create private self-test folder");
  const std::filesystem::path test_dir=name.data();
  bool success=false;
  auto expect=[](bool condition,const char* message) { if (!condition) throw std::runtime_error(message); };
  try {
    std::string reserve_error;
    if (!reserve_memory(&reserve_error)) throw std::runtime_error(reserve_error);
    cfg={}; cfg.card_dir=test_dir.string(); count={}; boot_ticks=123ull*MU_TB_HZ;
    api=make_api(); aram.assign(aram_bytes,0);
    std::memcpy(disc_header.data(),"GALE01",6);
    expect(memory_pointer(0x80000000u,mem1_bytes)==reinterpret_cast<uint8_t*>(mem1_base),"MEM1 direct mapping");
    expect(memory_pointer(0,4)==memory_pointer(0x80000000u,4),"MEM1 physical alias");
    expect(memory_pointer(0xC0000000u,4)==memory_pointer(0x80000000u,4),"MEM1 uncached alias");
    expect(memory_pointer(0x80000000u+mem1_bytes-1,2)==nullptr,"MEM1 overrun rejection");
    expect(memory_pointer(0xE0000000u,locked_cache_bytes)!=nullptr,"locked cache mapping");
    expect(memory_pointer(0xE0000000u+locked_cache_bytes-1,2)==nullptr,"locked cache overrun rejection");
    const std::array<uint8_t,20> abc_hash={0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    const std::array<uint8_t,20> empty_hash={0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09};
    expect(sha1({'a','b','c'})==abc_hash && sha1({})==empty_hash,"retail DOL SHA-1 verification vectors");
    expect(api.boot_time()==boot_ticks && api.ticks()==0,"console epoch clock");
    expect(console_nanoseconds(900ull*MU_TB_HZ)==900000000000ull,"15-minute pacing conversion does not wrap");
    expect(console_nanoseconds(86400ull*MU_TB_HZ)==86400000000000ull,"24-hour pacing conversion does not wrap");
    api.poll(); expect(api.ticks()==2048,"busy-wait console clock");
    uint8_t aram_data[8]={1,2,3,4,5,6,7,8},aram_copy[8]={};
    api.aram_dma(1,aram_data,aram_bytes-8,8); api.aram_dma(0,aram_copy,aram_bytes-8,8);
    expect(!std::memcmp(aram_data,aram_copy,8),"ARAM DMA roundtrip");
    bool rejected=false;
    try { api.aram_dma(1,aram_data,aram_bytes-4,8); } catch(const Stop&) {rejected=true;}
    expect(rejected,"ARAM DMA bounds rejection");
    int32_t capacity=0,sector=0,file=-1; uint32_t length=0;
    expect(api.card_probe(0,&capacity,&sector)==0 && capacity==128 && sector==0x2000,"slot A probe");
    expect(api.card_mount(1)==-3,"slot B absent");
    expect(api.card_mount(0)==0,"slot A mount");
    expect(api.card_open(0,"ROUNDTRIP",&file,&length)==-4,"empty card missing file");
    expect(api.card_create(0,"ROUNDTRIP",0x4000,&file)==0,"create GCI");
    const int32_t original=file;
    expect(api.card_create(0,"ROUNDTRIP",0x4000,nullptr)==-7,"duplicate GCI rejection");
    expect(api.card_create(0,"BADSIZE",1,nullptr)==-128,"unaligned GCI size rejection");
    std::vector<uint8_t> expected(0x4000),actual(expected.size());
    for (size_t i=0;i<expected.size();++i) expected[i]=uint8_t((i*73+11)&255);
    expect(api.card_write(0,file,expected.data(),uint32_t(expected.size()),0)==0,"save GCI payload");
    for (uint32_t i=0;i<8;++i) {
      const uint32_t at=11+i*513;
      const uint8_t value=uint8_t(i*31);
      expect(api.card_write(0,file,&value,1,at)==0,"repeated GCI update");
      expected[at]=value;
    }
    expect(api.card_write(0,file,expected.data(),2,0x3FFF)==-11,"GCI write bounds rejection");
    expect(api.card_read(0,file,actual.data(),uint32_t(actual.size()),0)==0 && actual==expected,"GCI immediate readback");
    card::Stat metadata{};
    expect(api.card_stat(0,file,&metadata,sizeof metadata)==0 && metadata.length==0x4000 && metadata.time==123,"native CARDStat values");
    metadata.banner=2; metadata.icon=0x100; metadata.icon_format=2; metadata.icon_speed=3; metadata.comment=0x80;
    expect(api.card_set_stat(0,file,&metadata,sizeof metadata)==0,"persist GCI metadata");
    expect(api.card_unmount(0)==0 && api.card_mount(0)==0,"card disk remount");
    expect(api.card_open(0,"ROUNDTRIP",&file,&length)==0 && length==expected.size(),"reopen persisted GCI");
    expect(api.card_read(0,file,actual.data(),uint32_t(actual.size()),0)==0 && actual==expected,"GCI survives remount");
    expect(api.card_stat(0,file,&metadata,sizeof metadata)==0 && metadata.banner==2 && metadata.icon==0x100 && metadata.comment==0x80 && metadata.icon_format==2,"metadata survives remount");
    expect(api.card_rename(0,"ROUNDTRIP","RENAMED")==0 && api.card_open(0,"ROUNDTRIP",nullptr,nullptr)==-4,"GCI rename");
    expect(api.card_open(0,"RENAMED",&file,&length)==0,"renamed GCI open");
    expect(api.card_create(0,"X/A",0x2000,nullptr)==0 && api.card_create(0,"X?A",0x2000,nullptr)==0,"safe filenames remain distinct");
    expect(api.card_rename(0,"RENAMED","X/A")==-7,"rename conflict rejection");
    int32_t bytes_free=0,files_free=0;
    expect(api.card_free_blocks(0,&bytes_free,&files_free)==0 && bytes_free==int32_t((2043-4)*0x2000) && files_free==124,"card capacity accounting");
    expect(api.card_unmount(0)==0 && api.card_mount(0)==0,"second card disk remount");
    expect(api.card_open(0,"X/A",nullptr,nullptr)==0 && api.card_open(0,"X?A",nullptr,nullptr)==0,"distinct filenames survive disk remount");
    expect(api.card_delete(0,"RENAMED")==0 && api.card_open(0,"RENAMED",nullptr,nullptr)==-4,"GCI delete");
    expect(api.card_read(0,original,actual.data(),UINT32_MAX,0)==-11 || api.card_read(0,original,actual.data(),UINT32_MAX,0)==-4,"invalid read rejection");
    expect(api.card_format(0)==0,"format private test card");
    expect(api.card_unmount(0)==0 && api.card_mount(0)==0 && api.card_free_blocks(0,&bytes_free,&files_free)==0 && files_free==127,"format persisted");
    success=true;
  } catch(const std::exception& exception) { fail(error,exception.what()); }
  catch(...) { fail(error,"unexpected native host failure during self-test"); }
  shutdown();
  // Delete only the private directory created by mkdtemp above. Existing files
  // in the requested scratch root never participate in the acceptance test.
  std::filesystem::remove_all(test_dir,ec);
  if (success) log("native host services: fixed memory, clock, ARAM, SHA-1 and persistent GCI checks passed");
  return success;
}
} // namespace linux_host

// AX's portable mixer has precisely one host dependency.
namespace host {
void log(const char* format, ...) {
  char line[4096];
  va_list args; va_start(args, format); std::vsnprintf(line, sizeof line, format, args); va_end(args);
  linux_host::log("%s", line);
}
} // namespace host
