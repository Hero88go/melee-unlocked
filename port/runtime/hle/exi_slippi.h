// Slippi EXI device (slot B): the game-side Slippi codes talk to it with DMA writes (command
// buffers) and DMA reads (responses). Port of Dolphin's CEXISlippi, grown feature by feature.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>

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
uint64_t commands_seen();
uint64_t replays_written();
const std::string& replay_directory();

}  // namespace slippi
