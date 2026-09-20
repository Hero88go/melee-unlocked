#include "audio_core.h"

#include "audio.h"
#include "ax_ucode.h"

namespace audio_core {

void reset(State& state) {
  state = State{};
  ax::reset();
}

void init_dma(State& state, const void* buffer, uint32_t length) {
  state.dma_buffer = static_cast<const uint8_t*>(buffer);
  state.dma_length = length;
}

void start_dma(State& state, bool on, uint64_t now_tb, uint64_t tb_hz) {
  if (!on) {
    state.running = false;
    return;
  }
  if (!state.running && state.dma_length != 0) {
    state.next_tb = now_tb + (uint64_t) state.dma_length * tb_hz / 128000;
  }
  state.running = state.dma_length != 0;
}

void tick(State& state, uint64_t now_tb, uint64_t tb_hz, DmaDone done, void* user) {
  if (!state.running || !state.dma_buffer || !state.dma_length) return;
  const uint64_t period = (uint64_t) state.dma_length * tb_hz / 128000;
  if (!period || now_tb < state.next_tb) return;
  if (now_tb > state.next_tb + period * 20) state.next_tb = now_tb;
  for (int guard = 0; guard < 8 && now_tb >= state.next_tb; ++guard) {
    state.next_tb += period;
    host::audio_push(state.dma_buffer, state.dma_length);
    if (done) done(user);
  }
}

void dsp_mail(uint32_t mail) {
  ax::handle_mail(mail);
}

uint32_t dsp_mail_pending() {
  return 0;
}

}  // namespace audio_core
