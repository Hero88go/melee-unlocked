// Shared host-side AI DMA clock and AX mailbox handling.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_core {

struct State {
  const uint8_t* dma_buffer = nullptr;
  uint32_t dma_length = 0;
  uint64_t next_tb = 0;
  bool running = false;
  bool native_pcm = false;
  // Native game only: its DMA-done handler mixes the next block later, as an event, and hands it
  // over through init_dma. Until then the buffer holds the block already played.
  bool awaiting_block = false;
  uint64_t awaiting_since_tb = 0;
  // Set when the wait timed out: the next pushes are the old block faded to silence, then silence,
  // never the stale block repeated. Cleared by the next init_dma.
  int stale_pushes = 0;
};

using DmaDone = void (*)(void* user);

void reset(State& state);
void init_dma(State& state, const void* buffer, uint32_t length,
              bool native_pcm = false);
void start_dma(State& state, bool on, uint64_t now_tb, uint64_t tb_hz);
void tick(State& state, uint64_t now_tb, uint64_t tb_hz, DmaDone done, void* user);
void dsp_mail(uint32_t mail);
uint32_t dsp_mail_pending();

}  // namespace audio_core
