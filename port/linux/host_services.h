// Linux machine services for the native source game. No Wine/Proton runtime.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mu_host.h"
#include <cstdint>
#include <string>
#include <vector>

namespace linux_host {

constexpr uintptr_t mem1_base = 0x80000000u;
constexpr uint32_t mem1_bytes = 40u << 20;
constexpr uintptr_t game_image_base = 0x82800000u;

struct Config {
  std::string iso_path;
  std::string game_path = "./melee_game.so";
  std::string log_path;
  // Empty selects $XDG_DATA_HOME/melee-unlocked/Card A (or ~/.local/share).
  // The caller can use an isolated directory for acceptance runs.
  std::string card_dir;
  std::string input_script;
  uint32_t max_frames = 0;
  bool pace = true;
  bool headless = false;
  bool no_gc_adapter = false;
  bool vanilla_game = false;
  bool trace_state = false;
  bool has_rng_seed = false;
  uint32_t rng_seed = 0;
  bool has_time_base = false;
  uint64_t time_base = 0; // GameCube epoch, 40.5 MHz ticks since 2000-01-01.
  bool has_match = false;
  MuMatchOverride match{};
};

struct Counters {
  uint32_t retraces = 0;
  uint64_t ticks = 0;
  uint64_t disc_reads = 0;
  uint64_t disc_bytes = 0;
  uint64_t fifo_bytes = 0;
  uint64_t audio_blocks = 0;
};

// The renderer is optional for bring-up and must install these before run().
// Retrace runs after the game has queued its VI interrupt, before its next update.
using FifoSink = void (*)(const uint8_t*, uint32_t, void*);
using RetraceSink = void (*)(void*);
void set_video_sinks(FifoSink fifo, RetraceSink retrace, void* user);
using FrameObserver = void (*)(const MuStatePod&, const Counters&, void*);
void set_frame_observer(FrameObserver observer, void* user);

// Call at the start of main, before large allocations. Fails without overwriting
// existing mappings. initialize also calls this when necessary.
bool reserve_memory(std::string* error = nullptr);
bool initialize(const Config&, std::string* error = nullptr);
int run();
void shutdown();

// A scripted pad replaces the hardware pad until clear_pad_override is called.
void set_pad_override(unsigned port, const MuPadStatus& pad);
void clear_pad_override(unsigned port);
bool state_snapshot(MuStatePod* out);
const Counters& counters();
const MuHostApi& host_api();
const MuGameApi& game_api();
const std::vector<MuStateRegion>& writable_image_regions();
// GX addresses are console physical or cached MEM1 addresses, never arbitrary
// process pointers. Invalid ranges return nullptr instead of crossing mappings.
uint8_t* memory_pointer(uint32_t address, uint32_t size);
uint32_t game_image_size();
void request_stop(int code = 0);
// Runs real fixed-memory and GCI disk I/O in a fresh private child of the
// supplied scratch directory. Does not need an ISO or a game library.
bool service_self_test(const char* scratch_directory, std::string* error = nullptr);
void log(const char* format, ...);

} // namespace linux_host
