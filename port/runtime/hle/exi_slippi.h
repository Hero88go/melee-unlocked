// Slippi EXI device (slot B): the game-side Slippi codes talk to it with DMA writes (command
// buffers) and DMA reads (responses). Port of Dolphin's CEXISlippi, grown feature by feature.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

namespace slippi {

void init();
void shutdown();
// EXI transfers on the Slippi channel.
void dma_write(uint32_t addr, uint32_t size);
void dma_read(uint32_t addr, uint32_t size);
void imm_write(uint32_t data, uint32_t size);
uint32_t imm_read(uint32_t size);
// Diagnostics.
uint32_t gct_load_address();
// Calls visit(address, bytes) for every place Slippi's main code list writes in the game.
void for_each_served_code_write(const std::function<void(uint32_t addr, uint32_t size)>& visit);
uint64_t commands_seen();
uint64_t replays_written();
const std::string& replay_directory();
const std::string& last_replay_path();   // the .slp most recently written (for the game report upload)
bool recording();                        // a .slp is open and taking this game's events (Static Recomp)
// Widescreen 16:9 (Slippi's optional code, compiled in both ways). request_* is thread-safe and
// takes effect on the simulation thread at the next retrace; the initial value comes from the
// command line / settings before the game loads the code table.
void request_widescreen(bool on);
bool widescreen();
void request_fod_reflections(bool on);
void poll_options();
// Sys/GameFiles/GALE01/<name> as the EXI file commands (D1/D2) serve it: the loose file, or its
// .diff applied to the disc copy. Empty when missing or when the diff fails.
std::vector<uint8_t> system_game_file(const std::string& name);
// Where the game loaded Slippi's code table, [*lo, *hi). False before the game has loaded it.
bool gct_range(uint32_t* lo, uint32_t* hi);
// The replay being written is removed when it closes: the match runs code a replay cannot reproduce.
void discard_current_replay();

}  // namespace slippi
