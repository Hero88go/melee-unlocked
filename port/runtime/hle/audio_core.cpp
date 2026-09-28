#include "audio_core.h"

#include "audio.h"
#include "ax_ucode.h"
#include "host.h"

namespace audio_core {

void reset(State& state) {
  state = State{};
  ax::reset();
}

void init_dma(State& state, const void* buffer, uint32_t length,
              bool native_pcm) {
  state.dma_buffer = static_cast<const uint8_t*>(buffer);
  state.dma_length = length;
  state.native_pcm = native_pcm;
  state.awaiting_block = false;
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
  if (now_tb > state.next_tb + period * 20) {   // long stall: skip ahead
    state.next_tb = now_tb;
    host::log("audio: AI clock skip retrace=%u next_tick_after_boundary=%lld", host::retrace_count(),
              (long long)(state.next_tb - (host::next_retrace_tb() - host::TB_PER_FRAME)));
  }
  // The native game mixes the next block after this call returns (its handler is an event), so it
  // gets one block per call: pushing again before it has handed over a new buffer would replay the
  // block just played. After a stall (a rollback's re-simulation, a hitch) that replay repeated
  // every 5 ms block until the backlog cleared, heard as crackle. A block that never comes (audio
  // stopped) releases the wait after four periods.
  if (state.native_pcm && state.awaiting_block) {
    if (now_tb - state.awaiting_since_tb < period * 4) return;
    state.awaiting_block = false;
  }
  for (int guard = 0; guard < 8 && now_tb >= state.next_tb; ++guard) {
    state.next_tb += period;
    if (state.native_pcm)
      host::audio_push_native(state.dma_buffer, state.dma_length);
    else
      host::audio_push(state.dma_buffer, state.dma_length);
    if (done) done(user);
    if (state.native_pcm) {
      state.awaiting_block = true;
      state.awaiting_since_tb = now_tb;
      break;
    }
  }
}

void dsp_mail(uint32_t mail) {
  ax::handle_mail(mail);
}

uint32_t dsp_mail_pending() {
  return 0;
}

}  // namespace audio_core
