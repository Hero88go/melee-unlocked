#include "audio_core.h"

#include "audio.h"
#include "ax_ucode.h"
#include "host.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace audio_core {

// The native game's blocks go to the sound card the moment the DSP has mixed them (as on the
// Static Recomp, see hle_stubs.cpp): its AX library hands the block to the AI DMA one period after
// mixing, and the DMA used to be pushed when it completed, another period on. The game's own DMA
// timing is unchanged. MELEE_AUDIO_DMA_PUSH=1 pushes at DMA completion again (comparison), and
// MELEE_AUDIO_FRESH_CHECK=1 checks every DMA against the block handed over early.
namespace {
const bool g_push_at_dma = [] { const char* v = std::getenv("MELEE_AUDIO_DMA_PUSH"); return v && *v == '1'; }();
const bool g_fresh_check = [] { const char* v = std::getenv("MELEE_AUDIO_FRESH_CHECK"); return v && *v == '1'; }();
int16_t g_fresh[4][320];
unsigned g_fresh_next = 0;
uint64_t g_fresh_same = 0, g_fresh_diff = 0;

void push_fresh(const int16_t* rl, uint32_t frames) {
  if (g_fresh_check && frames == 160) { std::memcpy(g_fresh[g_fresh_next & 3], rl, sizeof g_fresh[0]); ++g_fresh_next; }
  if (!g_push_at_dma) host::audio_push_native((const uint8_t*)rl, (size_t)frames * 4);
}

void check_dma_block(const uint8_t* le, uint32_t len) {
  if (!g_fresh_check || len != 640) return;
  bool same = false;
  for (int k = 0; k < 4 && !same; ++k) same = std::memcmp(le, g_fresh[k], 640) == 0;
  (same ? g_fresh_same : g_fresh_diff) += 1;
  if ((g_fresh_same + g_fresh_diff) % 2000 == 0)
    host::log("audio: early blocks matched %llu DMAs, differed from %llu", (unsigned long long)g_fresh_same, (unsigned long long)g_fresh_diff);
}
}  // namespace

void reset(State& state) {
  state = State{};
  ax::reset();
  ax::set_output_sink(push_fresh);
}

void init_dma(State& state, const void* buffer, uint32_t length,
              bool native_pcm) {
  state.dma_buffer = static_cast<const uint8_t*>(buffer);
  state.dma_length = length;
  state.native_pcm = native_pcm;
  state.awaiting_block = false;
  state.stale_pushes = 0;
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
    ++state.stale_pushes;
  }
  for (int guard = 0; guard < 8 && now_tb >= state.next_tb; ++guard) {
    state.next_tb += period;
    if (!g_push_at_dma) {
      // The block went to the sound card when it was mixed (push_fresh). A block that never came
      // is left to the output's own fade: replaying an older one here would jump back in time.
      if (state.native_pcm && state.stale_pushes > 0) ++state.stale_pushes;
      else if (state.native_pcm) check_dma_block(state.dma_buffer, state.dma_length);
    } else if (state.native_pcm && state.stale_pushes > 0) {
      // No new block: the old one faded out once, then silence (a repeat was heard as a buzz).
      static std::vector<uint8_t> quiet;
      quiet.assign(state.dma_length, 0);
      const size_t frames = state.dma_length / 4;
      if (state.stale_pushes == 1) {
        for (size_t f = 0; f < frames; ++f) {
          const double g = 1.0 - (double)(f + 1) / (double)frames;
          for (int ch = 0; ch < 2; ++ch) {
            const uint8_t* p = state.dma_buffer + f * 4 + ch * 2;
            const int16_t v = (int16_t)(int16_t)(p[0] | (p[1] << 8));
            const int16_t o = (int16_t)(v * g);
            quiet[f * 4 + ch * 2] = (uint8_t)(o & 0xFF);
            quiet[f * 4 + ch * 2 + 1] = (uint8_t)((uint16_t)o >> 8);
          }
        }
      }
      ++state.stale_pushes;
      host::audio_push_native(quiet.data(), quiet.size());
    } else if (state.native_pcm)
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
