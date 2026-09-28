// AX HLE: ADPCM decode through a parameter block, output interleave, mixer control mapping.
#include "ax_ucode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
  ax::set_memory({rd16le, rd32le, wr16le, wr32le, g_aram.data(), (uint32_t)g_aram.size()});
  ax::reset();
  wr16le(0x120, 0x1234);
  wr32le(0x124, 0x89ABCDEF);
  check(rd16le(0x120) == 0x1234 && rd32le(0x124) == 0x89ABCDEF,
        "little-endian identity AX memory preserves hi/lo words");
  std::puts("AX ucode ADPCM decode, PB write-back, output interleave and mixer control passed");
}
