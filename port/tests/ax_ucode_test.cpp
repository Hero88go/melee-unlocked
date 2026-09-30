// AX HLE: ADPCM decode through a parameter block, output interleave, mixer control mapping, and the
// ucode's initial time delay (ITD).
#include "ax_ucode.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
// ax_ucode.cpp writes a few lines to the port log; this test has no host, so they go to stdout.
namespace host {
void log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::putchar('\n');
}
}  // namespace host
static std::vector<uint8_t> g_ram(0x20000);
static std::vector<uint8_t> g_aram(0x10000);
static std::vector<uint8_t> g_le_ram(0x20000);
static uint16_t rd16(uint32_t a) { a &= 0x1FFFF; return (uint16_t)((g_ram[a] << 8) | g_ram[a + 1]); }
static uint32_t rd32(uint32_t a) { return ((uint32_t)rd16(a) << 16) | rd16(a + 2); }
static void wr16(uint32_t a, uint16_t v) { a &= 0x1FFFF; g_ram[a] = (uint8_t)(v >> 8); g_ram[a + 1] = (uint8_t)v; }
static void wr32(uint32_t a, uint32_t v) { wr16(a, (uint16_t)(v >> 16)); wr16(a + 2, (uint16_t)v); }
static uint16_t rd16le(uint32_t a) { uint16_t v; std::memcpy(&v, &g_le_ram[a & 0x1FFFF], sizeof v); return v; }
static uint32_t rd32le(uint32_t a) { return ((uint32_t)rd16le(a) << 16) | rd16le(a + 2); }
static void wr16le(uint32_t a, uint16_t v) { std::memcpy(&g_le_ram[a & 0x1FFFF], &v, sizeof v); }
static void wr32le(uint32_t a, uint32_t v) { wr16le(a, (uint16_t)(v >> 16)); wr16le(a + 2, (uint16_t)v); }
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); std::fflush(stdout); std::exit(1); } }

// One PCM16 voice through the nearest-sample path at full volume, so each mixed sample is the input
// sample: x[n] = 100 + n from ARAM, history h[i] = 1000 + i in RAM (the SDK's __AXITD buffer).
// ITD shifts L 4 -> target 6, R 0 -> 0. The checks read the mixed output and RAM.
namespace itd {
constexpr uint32_t PB = 0x4000, PB_IDLE = 0x4200, HISTORY = 0x6000, HISTORY_IDLE = 0x6100, MIXED = 0x8000, ZERO = 0xA000;
constexpr uint32_t VOL_ENV = 2 * (9 + 18 + 7 + 7 + 9);
constexpr uint32_t AUDIO_ADDR = VOL_ENV + 2 * (2 + 3);
int16_t x(int n) { return (int16_t)(100 + n); }              // the voice's samples, n >= 0
int16_t h(int i) { return (int16_t)(1000 + i); }             // its history, i = 0..31
void voice(uint32_t pb, bool running, uint32_t history, uint32_t next) {
  for (uint32_t i = 0; i < 0x100; i += 2) wr16(pb + i, 0);
  wr16(pb + 0x00, (uint16_t)(next >> 16)); wr16(pb + 0x02, (uint16_t)next);
  wr16(pb + 0x08, 2);                                       // src_type = nearest
  wr16(pb + 0x0C, 0);                                       // mixer_control: L and R
  wr16(pb + 0x0E, running ? 1 : 0);
  wr16(pb + 0x12, 0x8000); wr16(pb + 0x16, 0x8000);         // mixer left, right: 1.0
  wr16(pb + 0x36, 1);                                       // ITD on
  wr16(pb + 0x38, (uint16_t)(history >> 16)); wr16(pb + 0x3A, (uint16_t)history);
  wr16(pb + 0x3C, 4); wr16(pb + 0x3E, 0);                   // current shift L, R
  wr16(pb + 0x40, 6); wr16(pb + 0x42, 0);                   // target shift L, R
  wr16(pb + VOL_ENV, 0x8000);                               // volume 1.0
  wr16(pb + AUDIO_ADDR + 2, 0x0A);                          // PCM16
  wr16(pb + AUDIO_ADDR + 8, 0); wr16(pb + AUDIO_ADDR + 10, 0x7000);   // end far away
  wr16(pb + AUDIO_ADDR + 12, 0); wr16(pb + AUDIO_ADDR + 14, 0x1000);  // cur: ARAM byte 0x2000
}
void setup(uint32_t history_value_base) {
  for (int n = 0; n < 480; ++n) { g_aram[0x2000 + 2 * n] = (uint8_t)(x(n) >> 8); g_aram[0x2000 + 2 * n + 1] = (uint8_t)x(n); }
  for (int i = 0; i < 32; ++i) wr16(HISTORY + 2 * i, (uint16_t)(history_value_base + i));
  for (int i = 0; i < 32; ++i) wr16(HISTORY_IDLE + 2 * i, 7777);
  for (uint32_t i = 0; i < 0x40; i += 2) wr16(ZERO + i, 0);
}
int16_t out_left(int i) { return (int16_t)rd16(MIXED + 4 * i + 2); }
int16_t out_right(int i) { return (int16_t)rd16(MIXED + 4 * i); }
// What the left bus must hear at output sample 32 j + i of frame 0 (shift 4, 5, 6, 6, 6).
int16_t expect_left(int j, int i) {
  static const int shift[5] = {4, 5, 6, 6, 6};
  const int n = 32 * j + i - 32 + shift[j];
  return n < 0 ? h(32 + n) : x(n);
}
}  // namespace itd

int main() {
  ax::set_memory({rd16, rd32, wr16, wr32, g_aram.data(), (uint32_t)g_aram.size()});
  ax::reset();
  // Mixer control mapping for ucode 0x4e8a8b21.
  check(ax::convert_mixer_control(0) == 0x5, "plain voice mixes L and R");
  check((ax::convert_mixer_control(0x1) & 0x140) == 0x140, "bit 0 adds AUXA L/R");
  check((ax::convert_mixer_control(0x8) & 0xA) == 0xA, "bit 3 ramps L/R");
  // One ADPCM frame in ARAM at nibble address 0x2000 (byte 0x1000): header 0x00 (scale 1, coefs 0),
  // nibbles +1 ... with coefficients zero the decoded sample equals scale*nibble.
  const uint32_t aram_byte = 0x1000;
  g_aram[aram_byte] = 0x00;
  for (int i = 1; i < 8; ++i) g_aram[aram_byte + i] = 0x12;   // nibbles 1, 2, 1, 2, ...
  // Parameter block at RAM 0x4000: running, ADPCM, nearest SRC, looping off, mix to L/R at full volume.
  const uint32_t pb = 0x4000;
  std::memset(g_ram.data(), 0, g_ram.size());
  wr16(pb + 0x08, 2);          // src_type = nearest
  wr16(pb + 0x0C, 0);          // mixer_control
  wr16(pb + 0x0E, 1);          // running
  wr16(pb + 0x12, 0x8000);     // mixer.left volume 1.0
  wr16(pb + 0x16, 0x8000);     // mixer.right
  const uint32_t vol_env = pb + 2 * (9 + 18 + 7 + 7 + 9);
  wr16(vol_env, 0x8000);       // vol_env.cur_volume 1.0
  const uint32_t audio_addr = vol_env + 2 * (2 + 3);
  wr16(audio_addr + 0, 0);     // looping
  wr16(audio_addr + 2, 0);     // ADPCM
  wr16(audio_addr + 4, 0); wr16(audio_addr + 6, 0x2000);           // loop addr (nibbles)
  wr16(audio_addr + 8, 0); wr16(audio_addr + 10, 0x2000 + 0x1000);  // end addr far away
  wr16(audio_addr + 12, 0); wr16(audio_addr + 14, 0x2000);         // cur addr
  ax::process_pb_list(pb);
  // The first 14 decoded samples alternate 1, 2 (scale 1 << 0, coefficients 0).
  ax::output_samples(0x8000, 0x9000);
  int16_t r0 = (int16_t)rd16(0x8000), l0 = (int16_t)rd16(0x8002), l1 = (int16_t)rd16(0x8006);
  check(l0 == 1 && r0 == 1 && l1 == 2, "ADPCM nibbles decode to scaled values and interleave R,L");
  check((int16_t)rd16(0x8000 + 4 * 13) == 2, "fourteen data nibbles decoded before the next frame header");
  // cur_addr advanced by 160 samples (+ one header per 16 nibbles) and was written back.
  uint32_t cur = ((uint32_t)rd16(audio_addr + 12) << 16) | rd16(audio_addr + 14);
  check(cur == 0x2000 + 160 + 2 * 12, "accelerator position written back to the PB (12 frame headers consumed)");
  check(rd16(pb + 0x0E) == 1, "voice still running (end not reached)");

  // Initial time delay, as the ucode does it (run-source/rel085-final/wp6-20260930/ITD-SPEC.md).
  {
    using namespace itd;
    ax::set_itd_enabled(true);
    ax::reset();
    std::memset(g_ram.data(), 0, g_ram.size());
    setup(1000);
    voice(PB, true, HISTORY, PB_IDLE);
    voice(PB_IDLE, false, HISTORY_IDLE, 0);   // flagged, never runs this frame
    ax::setup_processing(ZERO);
    ax::process_pb_list(PB);
    ax::output_samples(MIXED, MIXED + 0x1000);
    bool left_ok = true, right_ok = true;
    for (int j = 0; j < 5; ++j)
      for (int i = 0; i < 32; ++i) {
        const int n = 32 * j + i - 32;   // right: shift 0, a full 32 samples late
        left_ok = left_ok && out_left(32 * j + i) == expect_left(j, i);
        right_ok = right_ok && out_right(32 * j + i) == (n < 0 ? h(32 + n) : x(n));
      }
    check(left_ok, "ITD: left is 32 - shift late, shift 4 stepping to its target 6 once per millisecond");
    check(right_ok, "ITD: right with shift 0 is a whole millisecond late, starting with the history");
    check(rd16(PB + 0x3C) == 6 && rd16(PB + 0x3E) == 0, "ITD: stepped shifts written back with the PB");
    bool history_ok = true, idle_ok = true;
    for (int i = 0; i < 32; ++i) {
      history_ok = history_ok && (int16_t)rd16(HISTORY + 2 * i) == x(128 + i);
      idle_ok = idle_ok && (int16_t)rd16(HISTORY_IDLE + 2 * i) == x(128 + i);
    }
    check(history_ok, "ITD: the last block rendered (post volume) is the voice's history for the next frame");
    check(idle_ok, "ITD: a flagged voice that did not run writes back the last block any voice rendered");
    check(rd16(PB_IDLE + 0x3C) == 4, "ITD: a voice that did not run does not step its shift");
    // Next frame: the history carries over and the shift stays at its target.
    ax::setup_processing(ZERO);
    ax::process_pb_list(PB);
    ax::output_samples(MIXED, MIXED + 0x1000);
    check(out_left(0) == x(160 - 32 + 6) && out_left(159) == x(319 - 32 + 6), "ITD: next frame, left 26 late");
    check(out_right(0) == x(128) && out_right(159) == x(287), "ITD: next frame, right 32 late across the frame edge");
    // Off (MELEE_AUDIO_ITD=0): the same flagged voice renders undelayed and nothing ITD-related is
    // read or written, exactly as before ITD existed.
    ax::set_itd_enabled(false);
    ax::reset();
    setup(1000);
    voice(PB, true, HISTORY, 0);
    ax::setup_processing(ZERO);
    ax::process_pb_list(PB);
    ax::output_samples(MIXED, MIXED + 0x1000);
    bool undelayed = true, untouched = true;
    for (int k = 0; k < 160; ++k) undelayed = undelayed && out_left(k) == x(k) && out_right(k) == x(k);
    for (int i = 0; i < 32; ++i) untouched = untouched && (int16_t)rd16(HISTORY + 2 * i) == h(i);
    check(undelayed, "ITD off: a flagged voice mixes undelayed");
    check(untouched && rd16(PB + 0x3C) == 4 && rd16(PB + 0x3E) == 0, "ITD off: history buffer and PB shifts untouched");
    ax::set_itd_enabled(true);
  }

  ax::set_memory({rd16le, rd32le, wr16le, wr32le, g_aram.data(), (uint32_t)g_aram.size()});
  ax::reset();
  wr16le(0x120, 0x1234);
  wr32le(0x124, 0x89ABCDEF);
  check(rd16le(0x120) == 0x1234 && rd32le(0x124) == 0x89ABCDEF,
        "little-endian identity AX memory preserves hi/lo words");
  std::puts("AX ucode ADPCM decode, PB write-back, output interleave, mixer control and ITD passed");
}
