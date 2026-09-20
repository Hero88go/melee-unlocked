// Host services used by the recompiled guest and the HLE layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "ppc.h"
#include "../abi/mu_host.h"

namespace host {

struct Options {
  std::string iso;
  std::string state_trace;        // optional per-retrace CPU/RAM/ARAM verification CSV
  std::string state_digest;       // per-retrace gameplay state shared with the source build
  std::string log_file;           // console log copy (default melee_port.log in the working directory)
  uint32_t frames = 0;           // stop after N retraces (0 = run until exit)
  bool fast = false;             // no real-time pacing
  bool trace_calls = false;      // log HLE calls
  bool quiet = false;
  uint64_t time_base = 0;        // preset timebase (0 = derive from wall clock like Dolphin)
  uint32_t rng_seed = 0;         // --rng-seed: force HSD_RandSeedPtr when a VS match starts (0 = off, game picks its own)
  bool rng_seed_set = false;
  int volume = 0;                // audio output volume percent (0 = muted, the development default)
  double hang_watch = 0.0;       // seconds without a retrace before the guest is declared hung (0 = off)
  std::string sys_dir = "port/slippi_sys";   // Slippi Sys folder (code tables, GameFiles served over the EXI device)
  std::string replay_dir = "replays";        // where .slp recordings are written
  std::string card_dir = "User/GC/CardA";    // memory card slot A as a folder of .gci files
  std::string audio_dump;        // optional WAV file receiving everything the AI DMA plays
  std::string input_log;         // --input-log: CSV of every PADRead (retrace, port, buttons, sticks, triggers)
  bool no_gc_adapter = false;    // hidden/headless runs: never open the GameCube adapter (WinUSB is exclusive; a test run would take it from the player)
};

extern Options options;
extern uint8_t* ram;             // guest RAM (MEM1)
extern uint32_t ram_size;        // its size in bytes: 24 MB, the console's, unless a build gives the game more
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
// The same lookup without dying: nullptr when the span is not the game's memory. The source port
// also has the game's own image, whose statics the console build kept in RAM; its physical form
// (address & 0x3FFFFFFF) is 0x10000000 + offset for an image at 0x50000000.
uint8_t* try_ptr(uint32_t addr, uint32_t bytes = 1);
extern uint8_t* game_image;       // null for the recompiled build
extern uint32_t game_image_size;
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
void init_state_digest();        // source path skips boot_setup but shares the digest writer

// ---- events (interrupt delivery at guest wait points) ----
using Completion = std::function<void()>;
void post_completion(Completion fn);   // callback delivered at the next wait point
void set_pe_finish_pending();
void set_pe_token_pending(uint16_t token);
void wait_event();                     // one OSSleepThread step
void pump_completions();               // deliver queued callbacks now (from HLE entry points)
void retrace();                        // one VI retrace: time, alarms, VI interrupt
bool retrace_due();                    // the timebase has reached the next retrace boundary
// Source port: the game is native code, not the recompiled guest. When set, retrace() calls this in
// place of the guest's alarm, audio and VI interrupt delivery (everything else, pacing, window,
// logging, exit, is shared).
extern void (*native_retrace)();
extern void (*native_state_snapshot)(MuStatePod*);
// The game's current mode/scene (GameRouting::curr_mode, curr_state_id), read the same way for
// both builds (native_state_snapshot when the source port is running, direct guest reads
// otherwise). Used by an @scene script directive to align input to a game point instead of an
// absolute retrace, so the same script drives both builds into the same match despite differing
// boot and menu timing. Safe to call every retrace; cheap either way.
void current_scene(uint32_t* major, uint32_t* minor, uint32_t* match_frame);
void deliver_interrupt(uint32_t number);
bool exit_requested();
void request_exit(int code);
// Relaunch this executable with the same command line, then shut down the way request_exit does,
// so the replay is finalised and the pipeline cache written before the new process takes over.
// Used for settings a running device cannot adopt, the graphics backend being the one that
// matters today.
void request_restart();
int exit_code();
uint32_t retrace_count();

// ---- simulation-thread cost accounting (per retrace; logged when a frame exceeds 20 ms) ----
enum SimCost { SIM_DVD, SIM_AX, SIM_JUKEBOX, SIM_EXI, SIM_SNAPSHOT, SIM_QUEUE, SIM_OBSERVE, SIM_RECORD, SIM_DECODE, SIM_COST_COUNT };
void sim_cost_add(int slot, double seconds);
double last_sim_frame_ms();            // work time of the most recent simulation frame (sleep excluded)

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
// Per-draw scopes run tens of thousands of times a frame, so they read the CPU time stamp counter
// (one instruction) instead of QueryPerformanceCounter; tsc_seconds is calibrated once at startup.
extern const double tsc_seconds;
struct SimCostScope { int slot; uint64_t t0; explicit SimCostScope(int s) : slot(s), t0(__rdtsc()) {} ~SimCostScope() { sim_cost_add(slot, (double)(__rdtsc() - t0) * tsc_seconds); } };

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
// The state the game read on the last PADRead, for the on-screen controller overlay.
void input_last_pads(PadState out[4]);
// GameCube controller adapter (WUP-028 over WinUSB): fills plugged ports, returns their mask.
uint32_t gcadapter_poll(PadState out[4]);
void gcadapter_rumble(int port, bool on);
// Forget a port's stored neutral so the next report re-establishes it (-1 for every port). The game
// asks for this through PADRecalibrate.
void gcadapter_recalibrate(int port);
void gcadapter_shutdown();
// EXPERIMENTAL, untested against hardware. Nintendo Switch Pro Controller (also the Joy-Cons and
// the SNES Online pad) over USB or Bluetooth HID. Fills the slots that are sending input and returns
// their mask, plus the raw SWPRO_* button bits for the remappable binding table. See switch_pro.cpp
// for why this needs more than Raw Input.
uint32_t switchpro_poll(PadState out[4], uint16_t buttons[4]);
void switchpro_recalibrate(int slot);
void switchpro_shutdown();

// Guest call helpers for HLE code.
void call_guest(uint32_t addr, uint32_t r3 = 0, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0);

}  // namespace host

// Thrown by hle::OSLoadContext to unwind a guest interrupt/exception handler.
struct LoadContextUnwind { uint32_t context; };

// Unwind a normal game stop so renderer threads can drain and join.
struct ExitRequested { int code; };
