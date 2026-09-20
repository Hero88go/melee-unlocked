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
};

using DmaDone = void (*)(void* user);

void reset(State& state);
void init_dma(State& state, const void* buffer, uint32_t length);
void start_dma(State& state, bool on, uint64_t now_tb, uint64_t tb_hz);
void tick(State& state, uint64_t now_tb, uint64_t tb_hz, DmaDone done, void* user);
void dsp_mail(uint32_t mail);
uint32_t dsp_mail_pending();

}  // namespace audio_core
