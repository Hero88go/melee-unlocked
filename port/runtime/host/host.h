// Host services used by the recompiled guest and the HLE layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "ppc.h"
#include "../abi/mu_host.h"
#include "tick_timing.h"

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
#ifdef MELEE_NO_SLIPPI
  std::string sys_dir = "Sys";               // system files beside the game (the DSP coefficient table)
#else
  std::string sys_dir = "port/slippi_sys";   // Slippi Sys folder (code tables, GameFiles served over the EXI device)
#endif
  std::string replay_dir = "replays";        // where .slp recordings are written
  std::string lab_dir = "Lab";               // Lab view character packs (tools/build_lab_assets.py)
  std::string card_dir = "User/GC/CardA";    // memory card slot A as a folder of .gci files
  std::string audio_dump;        // optional WAV file receiving everything the AI DMA plays
  std::string input_log;         // --input-log: CSV of every PADRead (retrace, port, buttons, sticks, triggers)
  bool no_gc_adapter = false;    // hidden/headless runs: never open the GameCube adapter (WinUSB is exclusive; a test run would take it from the player)
  bool vanilla_game = false;     // --vanilla-game: the Source engine without Legacy's always-on code set (parity runs against the code-free recompilation)
  // Static Recomp mod runs: the boot disc (iso) is a mod, and this is the player's vanilla 1.02 disc.
  // The mod's changed game code then runs from RAM, compared against the vanilla code.
  std::string mod_base_iso;
};

extern Options options;
extern uint8_t* ram;             // guest RAM (MEM1)
extern uint32_t ram_size;        // its size in bytes: 24 MB, the console's, unless a build gives the game more
extern uint8_t* aram;            // 16 MB audio RAM (host side)
extern ppc::Context* cpu;

// ---- logging ----
void log(const char* fmt, ...);
void log_guest_text(const char* data, size_t len);  // OSReport output
void log_flush();   // writes every queued log line now (log() hands lines to a writer thread)
[[noreturn]] void die(const char* fmt, ...);
// Ends the process now, with everything of ours already saved, without running DLL unload code
// (see end_process in host.cpp for why).
[[noreturn]] void end_process(int code);
// Called by die() with its message before the process exits, so a fatal error leaves the same crash
// report as an exception (the app registers it; the launcher offers that report).
void set_die_hook(void (*hook)(const char* message));
void set_after_guest_call(void (*hook)(uint32_t addr));   // diagnostics: after each host-delivered guest callback
// Hidden Static runs only, MELEE_TRACE_GUEST_HEAP=1: observe the known retail allocation assert.
// Env off installs no hooks and reads no heap metadata. Allocation behavior is unchanged.
void install_guest_heap_trace();
const char* symbol_name(uint32_t addr);
// Static Recomp: the boot disc is a mod (its changed code runs from RAM; see --mod-base-iso).
bool mod_disc_active();
// The mod disc runs without Slippi's codes (see host.cpp): nothing of Slippi's may be written into RAM.
bool mod_clean_mode();
// The mod's reference image: the code the compiled guest runs (vanilla, Slippi's boot codes and the
// words the in-game applier installs from Slippi's served table). A function whose RAM words all match
// it stays compiled. Sets `n` bytes at `addr` where they fall in the text ranges; false when none do.
bool mod_reference_set(uint32_t addr, const uint8_t* bytes, uint32_t n);
// False with MELEE_MOD_REFERENCE=boot: the reference then stays vanilla plus the boot codes (0.8.1 rule).
bool mod_reference_from_table();

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
void mark_ram_write(uint32_t addr, uint32_t bytes); // direct HLE/DMA write invalidation for renderer snapshots
std::string cstr(uint32_t addr, size_t max = 256);

// ---- disc ----
struct DiscFile { uint32_t offset, size; };
bool disc_open(const std::string& path);
void disc_prefetch_wait();  // finish background file-cache warming before starting simulation/audio
bool disc_read(uint32_t offset, void* dst, uint32_t size);
// File-relative read used by DVDFileInfo calls. Cosmetic replacements are resolved by the file's
// original FST start plus this relative offset; absolute disc reads always use disc_read above.
bool disc_read_file(uint32_t vanilla_file_start, uint32_t file_offset, void* dst, uint32_t size);
uint32_t disc_fst_offset();
uint32_t disc_fst_size();
uint32_t disc_fst_max_size();
bool disc_find_file(const std::string& name, uint32_t* offset, uint32_t* size);

// ---- boot ----
void boot_setup();               // low memory, FST placement, DOL load, registers
bool disc_has_vanilla_dol();     // the disc's main.dol is retail NTSC 1.02 (modded discs are not)
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
uint64_t next_retrace_tb();            // timebase of the next retrace boundary
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
// The player closed the window or chose Quit. While a replay is on screen and has not reached its
// end the exit code is kViewerClosedExit, so the launcher's replay queue stops instead of starting
// the next replay; in every other case it is 0, as before.
constexpr int kViewerClosedExit = 4;
void request_user_exit();
void set_replay_viewing(bool on);
// Local launcher mailbox for an explicitly accepted lobby Direct match.
// Only a normal completed game with one known winner contributes to W/L history.
void publish_lobby_result(int winner_index, int end_method);
// Relaunch this executable with the same command line, then shut down the way request_exit does,
// so the replay is finalised and the pipeline cache written before the new process takes over.
// Used for settings a running device cannot adopt, the graphics backend being the one that
// matters today.
void request_restart();
int exit_code();
uint32_t retrace_count();
// Frame id read by the optional sampling profiler. It is atomic because the
// profiler reads it from its sampling thread while the simulation advances it.
uint32_t profiler_frame_id();
const std::vector<uint32_t>& slow_sim_frames();

// ---- simulation-thread cost accounting (per retrace; logged when a frame exceeds 20 ms) ----
enum SimCost { SIM_DVD, SIM_AX, SIM_JUKEBOX, SIM_EXI, SIM_SNAPSHOT, SIM_QUEUE, SIM_OBSERVE, SIM_RECORD, SIM_DECODE, SIM_COST_COUNT };
void sim_cost_add(int slot, double seconds);
double last_sim_frame_ms();            // work time of the most recent simulation frame (sleep excluded)

// ---- time ----
constexpr uint64_t TB_HZ = 40500000ull;   // bus clock / 4
constexpr uint64_t TB_PER_FRAME = TB_HZ / 60;
void advance_time(uint64_t ticks);
// Idle-wait pacing: waits until the real time of console time `tb` inside the frame now running
// (the frame started at its tick deadline; the coming retrace is one frame later on both clocks),
// then advances the timebase to `tb`. False, without waiting, when `tb` is not before the coming
// retrace or when running unpaced (--fast).
bool wait_until_console_time(uint64_t tb);
void install_audio_pacing();
void install_clean_mode_music();   // Static Recomp, clean mode: music through the host's player
void install_mod_disc_guards();   // Static Recomp, mod disc: a file the disc lacks opens as an empty file   // Static Recomp: audio blocks at their 5 ms times during the frame wait
void report_heap_panic(ppc::Context& c);   // OSPanic at lbmemory.c:233: logs the heap that had no room and the title demo's draw
void apply_wide_fighter_draw();   // Static Recomp, once per game frame: fighters in the added sides draw under True 16:9
void install_console_clock();  // Static Recomp: OSGetTime carries the date (see os_get_time_dated)
void install_language_override();  // Static Recomp: the functions that read the saved language follow g_game_language
void install_low_poly_fighters();  // Static Recomp: the fighter parts hook behind "Low poly fighters" (gx::low_poly_fighters_active)
void apply_low_poly_fighters();    // Static Recomp, once per game frame: runs the fighter draw from RAM while the option is on
uint64_t console_epoch_ticks(); // the console clock at start: ticks since 2000-01-01, local time
void note_frame_submitted();   // the game handed over the frame's picture; until the retrace it only waits
// Retrace pacing multiplier (Slippi Online nudges it by up to 1% to keep peers in step).
void set_emulation_speed(double speed);
double emulation_speed();
// Host steady-clock seconds of the current simulation frame's retrace (the scheduled deadline when
// paced, wall time when --fast). Sub-frame presentation measures its phase from this.
double frame_time();
double now_seconds();
bool latency_trace_enabled();
TickTiming& tick_timing();
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
// Measured incoming USB reports per second, shared by the four adapter sockets; zero if stale.
double gcadapter_poll_rate_hz();
void gcadapter_rumble(int port, bool on);
// Rumble for an IN-GAME port, delivered to whichever adapter socket the port assignment routes to
// that port (or nowhere, for a device without a motor). The game names ports, not sockets: a
// player plugged into socket 2 and playing as port 1 online used to feel the OPPONENT's rumble,
// because port 2's motor command went straight to socket 2.
// The player's own switch for controller rumble (Controls tab), on by default. The game's rumble
// option is not reachable in Slippi's online menus, so this is the only way to turn it off there.
extern std::atomic<bool> g_rumble_enabled;
// Background input (Game tab), on by default. On: controllers keep playing while another window has
// focus, as they always have, and the keyboard is read then too. Off: every port reads neutral
// until the game window has focus again, as in Dolphin with Background Input unticked.
extern bool g_background_input;
// The game's language as chosen in the PC settings (Game tab): 0 leaves the game's own choice
// (Options > Language, kept in its save), 1 Japanese, 2 English. It is answered wherever the game
// asks for its saved language, so the save itself is never changed, and it takes effect as each
// screen loads. game_language_override() gives it in the game's own numbering: -1 none, 0 Japanese,
// 1 English.
extern std::atomic<int> g_game_language;
int game_language_override();
// "20XX CPUs" (Game tab, both engines): CPUs play with the 20XX Hack Pack's AI, offline only. On the
// Static Recomp hackpack_ai.cpp reads this at each match start (MELEE_TEST_20XX_AI=1 forces it on).
extern std::atomic<bool> g_cpu_20xx;
void input_rumble(int game_port, bool on);
// Immediately clear any motors that were active when the Controls switch is turned off.
void input_stop_all_rumble();
// Rumble for the local netplay player through the first assigned rumble-capable input source.
void input_rumble_local(bool on);
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
