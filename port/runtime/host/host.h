// Host services used by the recompiled guest and the HLE layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "ppc.h"

namespace host {

struct Options {
  std::string iso;
  std::string state_trace;        // optional per-retrace CPU/RAM/ARAM verification CSV
  uint32_t frames = 0;           // stop after N retraces (0 = run until exit)
  bool fast = false;             // no real-time pacing
  bool trace_calls = false;      // log HLE calls
  bool quiet = false;
  uint64_t time_base = 0;        // preset timebase (0 = derive from wall clock like Dolphin)
  int volume = 0;                // audio output volume percent (0 = muted, the development default)
  double hang_watch = 0.0;       // seconds without a retrace before the guest is declared hung (0 = off)
  std::string sys_dir = "slippi/Data/Sys";   // Slippi Sys folder (GameFiles served over the EXI device)
  std::string replay_dir = "replays";        // where .slp recordings are written
  std::string audio_dump;        // optional WAV file receiving everything the AI DMA plays
};

extern Options options;
extern uint8_t* ram;             // 24 MB guest RAM
extern uint8_t* aram;            // 16 MB audio RAM (host side)
extern ppc::Context* cpu;

// ---- logging ----
void log(const char* fmt, ...);
void log_guest_text(const char* data, size_t len);  // OSReport output
[[noreturn]] void die(const char* fmt, ...);
const char* symbol_name(uint32_t addr);

// ---- guest memory (host side, big-endian) ----
uint32_t rd32(uint32_t addr);
uint16_t rd16(uint32_t addr);
uint8_t rd8(uint32_t addr);
void wr32(uint32_t addr, uint32_t v);
void wr16(uint32_t addr, uint16_t v);
void wr8(uint32_t addr, uint8_t v);
uint8_t* ptr(uint32_t addr, uint32_t bytes = 1); // checks the complete RAM span
std::string cstr(uint32_t addr, size_t max = 256);

// ---- disc ----
struct DiscFile { uint32_t offset, size; };
bool disc_open(const std::string& path);
bool disc_read(uint32_t offset, void* dst, uint32_t size);
uint32_t disc_fst_offset();
uint32_t disc_fst_size();
uint32_t disc_fst_max_size();
bool disc_find_file(const std::string& name, uint32_t* offset, uint32_t* size);

// ---- boot ----
void boot_setup();               // low memory, FST placement, DOL load, registers

// ---- events (interrupt delivery at guest wait points) ----
using Completion = std::function<void()>;
void post_completion(Completion fn);   // callback delivered at the next wait point
void set_pe_finish_pending();
void set_pe_token_pending(uint16_t token);
void wait_event();                     // one OSSleepThread step
void pump_completions();               // deliver queued callbacks now (from HLE entry points)
void retrace();                        // one VI retrace: time, alarms, VI interrupt
void deliver_interrupt(uint32_t number);
bool exit_requested();
void request_exit(int code);
int exit_code();
uint32_t retrace_count();

// ---- time ----
constexpr uint64_t TB_HZ = 40500000ull;   // bus clock / 4
constexpr uint64_t TB_PER_FRAME = TB_HZ / 60;
void advance_time(uint64_t ticks);
// Retrace pacing multiplier (Slippi Online nudges it by up to 1% to keep peers in step).
void set_emulation_speed(double speed);
double emulation_speed();
// Host steady-clock seconds of the current simulation frame's retrace (the scheduled deadline when
// paced, wall time when --fast). Sub-frame presentation measures its phase from this.
double frame_time();
double now_seconds();

// ---- GX FIFO sink ----
void gx_write(uint32_t value, int bytes);  // write-gather pipe data
void gx_frame_present(uint32_t xfb_addr);
void gx_stats(uint64_t* commands, uint64_t* draws, uint64_t* vertices, uint32_t* efb_copies);
extern uint64_t g_disc_reads, g_disc_bytes;
extern bool g_has_window;

// ---- MMIO (0xCC000000 range) ----
uint32_t mmio_read(uint32_t addr, int bytes);
void mmio_write(uint32_t addr, uint32_t value, int bytes);

// ---- input ----
struct PadState { uint16_t button; int8_t stick_x, stick_y, sub_x, sub_y; uint8_t trig_l, trig_r, analog_a, analog_b; int8_t err; };
void input_poll(PadState out[4]);

// Guest call helpers for HLE code.
void call_guest(uint32_t addr, uint32_t r3 = 0, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0);

}  // namespace host

// Thrown by hle::OSLoadContext to unwind a guest interrupt/exception handler.
struct LoadContextUnwind { uint32_t context; };

// Unwind a normal game stop so renderer threads can drain and join.
struct ExitRequested { int code; };
