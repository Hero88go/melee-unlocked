// SPDX-License-Identifier: GPL-2.0-or-later
// Port of Dolphin's Core/HW/DSPHLE/UCodes/AX.cpp and AXVoice.h (GameCube AX, ucode 0x4e8a8b21).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "ax_ucode.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

// The port log (runtime/host/host.cpp). Declared here rather than through host.h so the unit test,
// which builds this file on its own, only has to supply this one function.
namespace host { void log(const char* fmt, ...); }

namespace ax {
namespace {

Memory g_mem{};
uint64_t g_frames = 0;

// ---- parameter block layout (u16 fields, big-endian in guest RAM) ----
struct PBMixer { uint16_t left, left_delta, right, right_delta, auxA_left, auxA_left_delta, auxA_right, auxA_right_delta,
                 auxB_left, auxB_left_delta, auxB_right, auxB_right_delta, auxB_surround, auxB_surround_delta,
                 surround, surround_delta, auxA_surround, auxA_surround_delta; };
struct PBInitialTimeDelay { uint16_t on, addrMemHigh, addrMemLow, offsetLeft, offsetRight, targetLeft, targetRight; };
struct PBUpdates { uint16_t num_updates[5]; uint16_t data_hi, data_lo; };
struct PBDpop { int16_t left, auxA_left, auxB_left, right, auxA_right, auxB_right, surround, auxA_surround, auxB_surround; };
struct PBVolumeEnvelope { uint16_t cur_volume; int16_t cur_volume_delta; };
struct PBUnknown2 { uint16_t unknown_reserved[3]; };
struct PBAudioAddr { uint16_t looping, sample_format, loop_addr_hi, loop_addr_lo, end_addr_hi, end_addr_lo, cur_addr_hi, cur_addr_lo; };
struct PBADPCMInfo { int16_t coefs[16]; uint16_t gain, pred_scale; int16_t yn1, yn2; };
struct PBSampleRateConverter { uint16_t ratio_hi, ratio_lo, cur_addr_frac; int16_t last_samples[4]; };
struct PBADPCMLoopInfo { uint16_t pred_scale, yn1, yn2; };
struct PBLowPassFilter { uint16_t enabled; int16_t yn1; uint16_t a0, b0; };

struct AXPB {
  uint16_t next_pb_hi, next_pb_lo, this_pb_hi, this_pb_lo;
  uint16_t src_type, coef_select, mixer_control, running, is_stream;
  PBMixer mixer;
  PBInitialTimeDelay initial_time_delay;
  PBUpdates updates;
  PBDpop dpop;
  PBVolumeEnvelope vol_env;
  PBUnknown2 unknown3;
  PBAudioAddr audio_addr;
  PBADPCMInfo adpcm;
  PBSampleRateConverter src;
  PBADPCMLoopInfo adpcm_loop_info;
  PBLowPassFilter lpf;
  uint16_t padding[25];
};
static_assert(sizeof(AXPB) == 244, "AXPB layout must match Dolphin's");

enum { SRCTYPE_POLYPHASE = 0, SRCTYPE_LINEAR = 1, SRCTYPE_NEAREST = 2 };
enum {
  MIX_L = 0x000001, MIX_L_RAMP = 0x000002, MIX_R = 0x000004, MIX_R_RAMP = 0x000008, MIX_S = 0x000010, MIX_S_RAMP = 0x000020,
  MIX_AUXA_L = 0x000040, MIX_AUXA_L_RAMP = 0x000080, MIX_AUXA_R = 0x000100, MIX_AUXA_R_RAMP = 0x000200, MIX_AUXA_S = 0x000400, MIX_AUXA_S_RAMP = 0x000800,
  MIX_AUXB_L = 0x001000, MIX_AUXB_L_RAMP = 0x002000, MIX_AUXB_R = 0x004000, MIX_AUXB_R_RAMP = 0x008000, MIX_AUXB_S = 0x010000, MIX_AUXB_S_RAMP = 0x020000,
};
enum CmdType {
  CMD_SETUP = 0x00, CMD_DL_AND_VOL_MIX = 0x01, CMD_PB_ADDR = 0x02, CMD_PROCESS = 0x03, CMD_MIX_AUXA = 0x04, CMD_MIX_AUXB = 0x05,
  CMD_UPLOAD_LRS = 0x06, CMD_SET_LR = 0x07, CMD_UNK_08 = 0x08, CMD_MIX_AUXB_NOWRITE = 0x09, CMD_COMPRESSOR_TABLE_ADDR = 0x0A,
  CMD_UNK_0B = 0x0B, CMD_UNK_0C = 0x0C, CMD_MORE = 0x0D, CMD_OUTPUT = 0x0E, CMD_END = 0x0F, CMD_MIX_AUXB_LR = 0x10,
  CMD_SET_OPPOSITE_LR = 0x11, CMD_UNK_12 = 0x12, CMD_SEND_AUX_AND_MIX = 0x13,
};
constexpr uint32_t MAIL_CMDLIST = 0xBABE0000, MAIL_CMDLIST_MASK = 0xFFFF0000;

// 32 samples per millisecond, 5 ms per frame.
int m_samples_left[160], m_samples_right[160], m_samples_surround[160];
int m_samples_auxA_left[160], m_samples_auxA_right[160], m_samples_auxA_surround[160];
int m_samples_auxB_left[160], m_samples_auxB_right[160], m_samples_auxB_surround[160];
uint16_t m_cmdlist[512];
uint32_t m_cmdlist_size = 0;
bool m_next_is_cmdlist = false;
uint16_t m_pending_cmdlist_size = 0;

inline int clamp16(int v) { return std::min(32767, std::max(-32767, v)); }
inline uint32_t hilo(uint16_t hi, uint16_t lo) { return ((uint32_t)hi << 16) | lo; }
inline uint8_t read_aram(uint32_t addr) { return addr < g_mem.aram_size ? g_mem.aram[addr] : 0; }

void read_pb(uint32_t addr, AXPB& pb) {
  uint16_t* dst = (uint16_t*)&pb;
  for (size_t i = 0; i < sizeof(AXPB) / 2; ++i) dst[i] = g_mem.rd16(addr + (uint32_t)i * 2);
}
void write_pb(uint32_t addr, const AXPB& pb) {
  const uint16_t* src = (const uint16_t*)&pb;
  for (size_t i = 0; i < sizeof(AXPB) / 2; ++i) g_mem.wr16(addr + (uint32_t)i * 2, src[i]);
}

// ---- simulated accelerator ----
uint32_t acc_loop_addr, acc_end_addr;
uint32_t* acc_cur_addr;
AXPB* acc_pb;
// End of a one-shot, as the hardware does it: after the end address the accelerator returns no more
// samples (reads give 0, the address stays at the loop address) until the ucode writes YN2, and the
// ucode's end handler (IMEM 0x0c50) writes YN2 only for looping voices, so the rest of a one-shot's
// last 1 ms block is silence (Dolphin's DSPAccelerator does the same). Melee points every one-shot's
// loop address at its own sample start, so without this each ending sound finished its block with
// the opening of the same sound and then snapped to silence: a click on the 1 ms edge (09-29).
// MELEE_AUDIO_OLD_ACC=1 keeps decoding past the end, as before, for comparison.
const bool acc_stop_at_end = [] { const char* v = std::getenv("MELEE_AUDIO_OLD_ACC"); return !(v && *v == '1'); }();
bool acc_reads_stopped = false;
VoiceTraceFn g_voice_trace = nullptr;

// ---- initial time delay (ITD), as the ucode does it (run-source/rel085-final/wp6-20260930/ITD-SPEC.md) ----
// A voice with its ITD flag set is heard 32 - shift samples late on each side: its 1 ms blocks (after
// the resampler and the volume envelope) follow the voice's last 32 samples from the previous frame
// in the DSP's work area, and the left buses (main, AuxA, AuxB) read that stream shiftL samples into
// the old 32, the right buses shiftR samples in; surround buses read the block itself. After every
// rendered millisecond each shift moves one sample toward its target. After the voice's 5 ms the last
// block rendered goes back to the PB's ITD buffer in RAM (the SDK's __AXITD), then the PB with its
// shifts. Melee arms ITD on most sound effects (pan != centre), never on the music stream.
// MELEE_AUDIO_ITD=0 turns all of it off: no work area, no RAM reads or writes, today's output.
bool g_itd_on = [] { const char* v = std::getenv("MELEE_AUDIO_ITD"); return !(v && v[0] == '0'); }();
bool g_itd_logged = false;
// DMEM 0x0CC0-0x0D7F as offsets from 0x0CC0: [0, 32) the history, then up to five 1 ms blocks. It
// keeps whatever the previous voices left there, as DMEM does.
constexpr uint32_t ITD_BLOCK0 = 32, ITD_WORDS = 32 + 5 * 32, ITD_NONE = 0xFFFFFFFFu;
int16_t g_itd_dmem[ITD_WORDS];
uint32_t g_itd_left = ITD_BLOCK0, g_itd_right = ITD_BLOCK0;   // DMEM 0x0E40 / 0x0E41: left and right sources
uint32_t g_itd_next = ITD_BLOCK0;                              // 0x0E42: where the next block goes
// 0x0E1C: start of the last block rendered by any voice. The ucode never resets it, so a flagged voice
// that did not run writes back another voice's block. ITD_NONE until a block exists (the hardware
// word points at DMEM 0x0000, the mix buffer, then; zeros here).
uint32_t g_itd_last = ITD_NONE;

// The read position the ucode computes for a source (0x030B-0x030F): block - 32 + shift. Melee's shifts
// are 0..31; the clamp only keeps shifts the hardware would read outside the work area inside it.
inline uint32_t itd_source(uint32_t block, uint16_t shift) {
  const int32_t at = (int32_t)block - 32 + (int16_t)shift;
  return (uint32_t)std::clamp(at, 0, (int32_t)(ITD_WORDS - 32));
}
// One step toward the target (0x02FC-0x0309: DECM when above, INCM when below).
inline uint16_t itd_step(uint16_t shift, uint16_t target) {
  const int16_t s = (int16_t)shift, t = (int16_t)target;
  return (uint16_t)(s > t ? s - 1 : s < t ? s + 1 : s);
}
inline uint32_t itd_buffer(const AXPB& pb) {
  return hilo(pb.initial_time_delay.addrMemHigh, pb.initial_time_delay.addrMemLow);
}
// Voice setup (0x0210-0x0246), for every PB in the list, running or not, before the first
// millisecond's PB updates: pointers from the current shifts, and the 32-sample history DMA'd in.
void itd_voice_setup(const AXPB& pb) {
  g_itd_next = ITD_BLOCK0;
  if (!pb.initial_time_delay.on) {
    g_itd_left = g_itd_right = ITD_BLOCK0;
    return;
  }
  g_itd_left = itd_source(ITD_BLOCK0, pb.initial_time_delay.offsetLeft);   // 0x0CC0 + shiftL
  g_itd_right = itd_source(ITD_BLOCK0, pb.initial_time_delay.offsetRight);
  const uint32_t buffer = itd_buffer(pb);
  for (uint32_t i = 0; i < 32; ++i) g_itd_dmem[i] = (int16_t)g_mem.rd16(buffer + i * 2);
}
// After the voice's 5 ms (0x0355-0x0368), with the flag as it stands then, before the PB goes back.
void itd_write_back(const AXPB& pb) {
  if (!pb.initial_time_delay.on) return;
  const uint32_t buffer = itd_buffer(pb);
  for (uint32_t i = 0; i < 32; ++i)
    g_mem.wr16(buffer + i * 2, g_itd_last == ITD_NONE ? 0 : (uint16_t)g_itd_dmem[g_itd_last + i]);
}

// ---- per-voice frame trace (set_frame_trace); nothing here runs while the sink is null ----
FrameTraceFn g_frame_trace = nullptr;
constexpr uint64_t FNV_BASIS = 14695981039346656037ull, FNV_PRIME = 1099511628211ull;
struct VoiceFrameAccum {
  uint64_t pcm = FNV_BASIS, mix = FNV_BASIS;
  uint32_t samples = 0, ms_mask = 0;
  int32_t peak = 0;
};
VoiceFrameAccum* g_accum = nullptr;   // set only while a traced block renders
using PBWords = std::array<uint16_t, sizeof(AXPB) / 2>;
std::unordered_map<uint32_t, PBWords> g_written_back;   // each block as the ucode last wrote it
inline uint64_t fnv_add(uint64_t hash, int16_t sample) {
  const uint16_t value = (uint16_t)sample;
  hash = (hash ^ (value & 0xFFu)) * FNV_PRIME;
  return (hash ^ (value >> 8)) * FNV_PRIME;
}
void trace_voice_frame(uint32_t pb_addr, const AXPB& begin, const AXPB& end, const VoiceFrameAccum& accum) {
  PBWords begin_words, end_words;
  std::memcpy(begin_words.data(), &begin, sizeof(AXPB));
  std::memcpy(end_words.data(), &end, sizeof(AXPB));
  auto last = g_written_back.find(pb_addr);
  const bool cpu_changed = last == g_written_back.end() || last->second != begin_words;
  if (last == g_written_back.end()) g_written_back.emplace(pb_addr, end_words);
  else last->second = end_words;
  if (!accum.samples && !cpu_changed && begin.running == end.running) return;
  uint32_t update_count = 0;
  for (uint16_t n : begin.updates.num_updates) update_count += n;
  char line[4096];
  size_t used = (size_t)std::snprintf(line, sizeof line,
      "kind=voice frame=%llu pb=%08X run=%u>%u ms=%X n=%u peak=%d pcm=%016llX mix=%016llX "
      "cur=%08X>%08X end=%08X loop=%08X fmt=%u looping=%u ratio=%08X vol=%04X/%04X updates=%u cpu=%u",
      (unsigned long long)g_frames, pb_addr, begin.running, end.running, accum.ms_mask, accum.samples,
      accum.peak, (unsigned long long)accum.pcm, (unsigned long long)accum.mix,
      hilo(begin.audio_addr.cur_addr_hi, begin.audio_addr.cur_addr_lo),
      hilo(end.audio_addr.cur_addr_hi, end.audio_addr.cur_addr_lo),
      hilo(begin.audio_addr.end_addr_hi, begin.audio_addr.end_addr_lo),
      hilo(begin.audio_addr.loop_addr_hi, begin.audio_addr.loop_addr_lo), begin.audio_addr.sample_format,
      begin.audio_addr.looping, hilo(begin.src.ratio_hi, begin.src.ratio_lo), begin.vol_env.cur_volume,
      (uint16_t)begin.vol_env.cur_volume_delta, update_count, cpu_changed ? 1u : 0u);
  if (cpu_changed && used + 8 + begin_words.size() * 4 < sizeof line) {
    used += (size_t)std::snprintf(line + used, sizeof line - used, " words=");
    for (uint16_t word : begin_words) used += (size_t)std::snprintf(line + used, sizeof line - used, "%04X", word);
  }
  if (update_count && used + 10 < sizeof line) {
    used += (size_t)std::snprintf(line + used, sizeof line - used, " update=");
    const uint32_t updates_addr = hilo(begin.updates.data_hi, begin.updates.data_lo);
    uint32_t index = 0;
    for (uint32_t ms = 0; ms < 5; ++ms)
      for (uint16_t k = 0; k < begin.updates.num_updates[ms] && used + 16 < sizeof line; ++k, ++index)
        used += (size_t)std::snprintf(line + used, sizeof line - used, "%u:%04X=%04X,", ms,
                                      g_mem.rd16(updates_addr + index * 4), g_mem.rd16(updates_addr + index * 4 + 2));
  }
  g_frame_trace(line);
}

void accelerator_setup(AXPB* pb, uint32_t* cur_addr) {
  acc_pb = pb;
  acc_loop_addr = hilo(pb->audio_addr.loop_addr_hi, pb->audio_addr.loop_addr_lo);
  acc_end_addr = hilo(pb->audio_addr.end_addr_hi, pb->audio_addr.end_addr_lo);
  acc_cur_addr = cur_addr;
  acc_reads_stopped = false;   // every voice setup writes YN2 (ucode 0x05df), which resumes reads
}

uint16_t accelerator_get_sample() {
  if (acc_reads_stopped) return 0;
  uint16_t ret;
  uint8_t step_size_bytes = 0;
  switch (acc_pb->audio_addr.sample_format) {
    case 0x00: {  // ADPCM
      if ((*acc_cur_addr & 15) == 0) {
        acc_pb->adpcm.pred_scale = read_aram((*acc_cur_addr & ~15u) >> 1);
        *acc_cur_addr += 2;
      }
      switch (acc_end_addr & 15) {
        case 0: step_size_bytes = 1; break;
        case 1: step_size_bytes = 0; break;
        default: step_size_bytes = 2; break;
      }
      int scale = 1 << (acc_pb->adpcm.pred_scale & 0xF);
      int coef_idx = (acc_pb->adpcm.pred_scale >> 4) & 0x7;
      int32_t coef1 = acc_pb->adpcm.coefs[coef_idx * 2 + 0];
      int32_t coef2 = acc_pb->adpcm.coefs[coef_idx * 2 + 1];
      int temp = (*acc_cur_addr & 1) ? (read_aram(*acc_cur_addr >> 1) & 0xF) : (read_aram(*acc_cur_addr >> 1) >> 4);
      if (temp >= 8) temp -= 16;
      int val = (scale * temp) + ((0x400 + coef1 * acc_pb->adpcm.yn1 + coef2 * acc_pb->adpcm.yn2) >> 11);
      val = clamp16(val);
      acc_pb->adpcm.yn2 = acc_pb->adpcm.yn1;
      acc_pb->adpcm.yn1 = (int16_t)val;
      *acc_cur_addr += 1;
      ret = (uint16_t)val;
      break;
    }
    case 0x0A:  // 16-bit PCM
      ret = (uint16_t)((read_aram(*acc_cur_addr * 2) << 8) | read_aram(*acc_cur_addr * 2 + 1));
      acc_pb->adpcm.yn2 = acc_pb->adpcm.yn1;
      acc_pb->adpcm.yn1 = (int16_t)ret;
      step_size_bytes = 2;
      *acc_cur_addr += 1;
      break;
    case 0x19:  // 8-bit PCM
      ret = (uint16_t)(read_aram(*acc_cur_addr) << 8);
      acc_pb->adpcm.yn2 = acc_pb->adpcm.yn1;
      acc_pb->adpcm.yn1 = (int16_t)ret;
      step_size_bytes = 2;
      *acc_cur_addr += 1;
      break;
    default:
      return 0;
  }
  // End address reached: loop or stop, as the ucode's accelerator interrupt handler does.
  if (*acc_cur_addr == (acc_end_addr + step_size_bytes - 1)) {
    *acc_cur_addr = acc_loop_addr;
    if (acc_pb->audio_addr.looping) {
      acc_pb->adpcm.pred_scale = acc_pb->adpcm_loop_info.pred_scale;
      if (!acc_pb->is_stream) {
        acc_pb->adpcm.yn1 = (int16_t)acc_pb->adpcm_loop_info.yn1;
        acc_pb->adpcm.yn2 = (int16_t)acc_pb->adpcm_loop_info.yn2;
      }
    } else {
      acc_pb->running = 0;
      acc_reads_stopped = acc_stop_at_end;   // no YN2 write for a one-shot: reads stay stopped
    }
  }
  return ret;
}

// The DSP's polyphase filter: 3 sets x 128 phases x 4 taps (Q15), as in the DSP coefficient ROM.
// On whenever a coefficient table is found (MELEE_AUDIO_POLYPHASE=0 turns it off; without a table
// voices render linear, as Dolphin HLE does). Measured 09-29 against Slippi Dolphin's LLE DSP dump
// of the same replay: its tone matches the hardware within 0.6 dB in every octave band (linear is
// 5 dB bright at 8-14 kHz), and with the accelerator stopping at a one-shot's end (acc_stop_at_end)
// it flags 6 clicks where the LLE dump flags 10 (before that fix: 41, all on 1 ms voice block edges:
// this filter passes the newest samples sooner than linear, so it showed the defect more). The math
// below matches the ucode's routine at IMEM 0x05a8, output rounding included: the ucode keeps the
// middle word of the doubled sum with a saturating store, which is sum >> 15 clamped (09-30).
//
// Where the table comes from, first found wins (paths relative to the game's folder, the launcher's
// working folder): MELEE_DSP_COEF (a test override); User\GC\dsp_coef.bin, a player's own dump of a
// console's DSP ROM (Adler-32 f3b93527, the exact hardware filter; Nintendo data, never shipped);
// Sys\GC\dsp_coef.bin, the free table shipped with the game (Dolphin's free DSP ROM table v0.2.1,
// GPL-2.0-or-later, generated windowed sinc: the table the LLE reference used); then a Slippi
// Launcher install. The port log names the file and its Adler-32.
//
// Adler-32 as Dolphin prints it for DSP ROM files: over the 16-bit words in host (little-endian)
// order, which is each big-endian file word with its two bytes swapped.
uint32_t dsp_rom_adler32(const uint8_t* raw, size_t bytes) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    a = (a + raw[i + 1]) % 65521; b = (b + a) % 65521;
    a = (a + raw[i]) % 65521; b = (b + a) % 65521;
  }
  return (b << 16) | a;
}
const char* dsp_coef_name(uint32_t adler) {
  switch (adler) {
    case 0xf3b93527u: return "a console's own table";
    case 0xdb6880c1u: return "free table v0.2.1, as the LLE reference";
    case 0xa4a575f5u: return "free table v0.3 or later";
    case 0xb019c2fbu: return "free table v0.2, linear interpolation only";
    default: return "unknown table";
  }
}
const int16_t* polyphase_coefs() {
  static bool loaded = false;
  static std::array<int16_t, 3 * 512> table{};
  static bool ok = false;
  if (loaded) return ok ? table.data() : nullptr;
  loaded = true;
  if (const char* on = std::getenv("MELEE_AUDIO_POLYPHASE"); on && on[0] == '0') {
    host::log("audio: DSP polyphase filter off (MELEE_AUDIO_POLYPHASE=0), voices use linear interpolation");
    return nullptr;
  }
  std::string paths[6];
  if (const char* p = std::getenv("MELEE_DSP_COEF")) paths[0] = p;
  paths[1] = "User\\GC\\dsp_coef.bin";
  paths[2] = "Sys\\GC\\dsp_coef.bin";
  if (const char* a = std::getenv("APPDATA")) {
    const std::string base = std::string(a) + "\\Slippi Launcher\\";
    paths[3] = base + "netplay\\Sys\\GC\\dsp_coef.bin";
    paths[4] = base + "playback\\Sys\\GC\\dsp_coef.bin";
    paths[5] = base + "netplay-beta\\Sys\\GC\\dsp_coef.bin";
  }
  for (const std::string& path : paths) {
    if (path.empty()) continue;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
      // A measurement that pins the table must not fall through to another one unnoticed.
      if (&path == &paths[0]) host::log("audio: DSP coefficient table %s (MELEE_DSP_COEF) not found", path.c_str());
      continue;
    }
    uint8_t raw[4097];
    const size_t n = std::fread(raw, 1, sizeof raw, f);
    std::fclose(f);
    if (n != 4096) {
      host::log("audio: DSP coefficient table %s skipped (not 4096 bytes)", path.c_str());
      continue;
    }
    for (int i = 0; i < 3 * 512; ++i) table[i] = (int16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
    // Each phase must be a low-pass (taps summing to about 1.0) whose taps' magnitudes stay under
    // 2.0, so the four-tap sum in resample_audio fits an int32 (the DSP accumulates in 40 bits).
    bool sane = true;
    for (int row = 0; row < 3 * 128 && sane; ++row) {
      const int16_t* c = &table[row * 4];
      const int sum = c[0] + c[1] + c[2] + c[3];
      const int magnitude = std::abs(c[0]) + std::abs(c[1]) + std::abs(c[2]) + std::abs(c[3]);
      sane = sum > 30000 && sum < 34000 && magnitude < 65536;
    }
    if (!sane) {
      host::log("audio: DSP coefficient table %s skipped (its filter rows are out of range)", path.c_str());
      continue;
    }
    ok = true;
    const uint32_t adler = dsp_rom_adler32(raw, 4096);
    host::log("audio: DSP coefficient table %s Adler-32 %08x (%s)", path.c_str(), adler, dsp_coef_name(adler));
    return table.data();
  }
  host::log("audio: no DSP coefficient table (MELEE_DSP_COEF, User\\GC, Sys\\GC, Slippi Launcher folders): "
            "voices use linear interpolation");
  return nullptr;
}

uint32_t resample_audio(int16_t* output, uint32_t count, int16_t* last_samples, uint32_t curr_pos, uint32_t ratio, int srctype,
                        int coef_select) {
  const int16_t* poly = srctype == SRCTYPE_POLYPHASE && coef_select >= 0 && coef_select < 3 ? polyphase_coefs() : nullptr;
  if (poly) {
    poly += coef_select * 512;
    int16_t temp[4];
    uint32_t idx = 0;
    temp[idx++ & 3] = last_samples[0]; temp[idx++ & 3] = last_samples[1];
    temp[idx++ & 3] = last_samples[2]; temp[idx++ & 3] = last_samples[3];
    for (uint32_t i = 0; i < count; ++i) {
      curr_pos += ratio;
      while (curr_pos >= 0x10000) { temp[idx++ & 3] = (int16_t)accelerator_get_sample(); curr_pos -= 0x10000; }
      const int16_t* c = poly + ((curr_pos & 0xFFFF) >> 9) * 4;
      int32_t s = temp[idx & 3] * c[0] + temp[(idx + 1) & 3] * c[1] + temp[(idx + 2) & 3] * c[2] + temp[(idx + 3) & 3] * c[3];
      s >>= 15;
      output[i] = (int16_t)std::clamp(s, -32768, 32767);
    }
    last_samples[0] = temp[idx & 3]; last_samples[1] = temp[(idx + 1) & 3];
    last_samples[2] = temp[(idx + 2) & 3]; last_samples[3] = temp[(idx + 3) & 3];
  } else if (srctype == SRCTYPE_LINEAR || srctype == SRCTYPE_POLYPHASE) {
    int16_t temp[4];
    uint32_t idx = 0;
    temp[idx++ & 3] = last_samples[0]; temp[idx++ & 3] = last_samples[1];
    temp[idx++ & 3] = last_samples[2]; temp[idx++ & 3] = last_samples[3];
    for (uint32_t i = 0; i < count; ++i) {
      curr_pos += ratio;
      while (curr_pos >= 0x10000) { temp[idx++ & 3] = (int16_t)accelerator_get_sample(); curr_pos -= 0x10000; }
      uint16_t curr_frac = (uint16_t)(curr_pos & 0xFFFF);
      uint16_t inv_curr_frac = (uint16_t)(-curr_frac);
      int16_t sample;
      if (curr_frac) {
        int32_t s0 = temp[idx++ & 3];
        int32_t s1 = temp[idx++ & 3];
        sample = (int16_t)(((s0 * inv_curr_frac) + (s1 * curr_frac)) >> 16);
        idx += 2;
      } else {
        sample = temp[idx++ & 3];
        idx += 3;
      }
      output[i] = sample;
    }
    last_samples[3] = temp[--idx & 3]; last_samples[2] = temp[--idx & 3];
    last_samples[1] = temp[--idx & 3]; last_samples[0] = temp[--idx & 3];
  } else {  // SRCTYPE_NEAREST
    for (uint32_t i = 0; i < count; ++i) output[i] = (int16_t)accelerator_get_sample();
    std::memcpy(last_samples, output + count - 4, 4 * sizeof(int16_t));
  }
  return curr_pos;
}

void get_input_samples(AXPB& pb, int16_t* samples, uint16_t count) {
  uint32_t cur_addr = hilo(pb.audio_addr.cur_addr_hi, pb.audio_addr.cur_addr_lo);
  accelerator_setup(&pb, &cur_addr);
  uint32_t curr_pos = resample_audio(samples, count, pb.src.last_samples, pb.src.cur_addr_frac, hilo(pb.src.ratio_hi, pb.src.ratio_lo), pb.src_type,
                                    pb.coef_select);
  pb.src.cur_addr_frac = (uint16_t)(curr_pos & 0xFFFF);
  pb.audio_addr.cur_addr_hi = (uint16_t)(cur_addr >> 16);
  pb.audio_addr.cur_addr_lo = (uint16_t)(cur_addr & 0xFFFF);
}

void mix_add(int* out, const int16_t* input, uint32_t count, uint16_t* pvol, int16_t* dpop, bool ramp) {
  uint16_t& volume = pvol[0];
  uint16_t volume_delta = ramp ? pvol[1] : 0;
  VoiceFrameAccum* const accum = g_accum;
  for (uint32_t i = 0; i < count; ++i) {
    int64_t sample = input[i];
    sample *= volume;
    sample >>= 15;
    sample = clamp16((int32_t)sample);
    out[i] += (int16_t)sample;
    if (accum) accum->mix = fnv_add(accum->mix, (int16_t)sample);
    volume += volume_delta;
    *dpop = (int16_t)sample;
  }
}

struct Buffers { int* ptrs[9]; };

void process_voice(AXPB& pb, const Buffers& b, uint16_t count, uint32_t mctrl,
                   uint32_t pb_addr, uint16_t millisecond) {
  if (!pb.running) return;
  int16_t samples[32];
  const uint32_t end_addr = hilo(pb.audio_addr.end_addr_hi,
                                 pb.audio_addr.end_addr_lo);
  const bool trace_voice = g_voice_trace && !pb.audio_addr.looping &&
      (end_addr == 0x00009FF2u || end_addr == 0x003EA94Cu ||
       end_addr == 0x00220F57u || end_addr == 0x00309E72u ||
       end_addr == 0x00B97F06u || end_addr == 0x00B61C06u);
  const uint32_t cur_before = trace_voice
      ? hilo(pb.audio_addr.cur_addr_hi, pb.audio_addr.cur_addr_lo) : 0;
  const uint16_t frac_before = trace_voice ? pb.src.cur_addr_frac : 0;
  const uint16_t volume_before = trace_voice ? pb.vol_env.cur_volume : 0;
  const uint16_t volume_delta = trace_voice
      ? (uint16_t) pb.vol_env.cur_volume_delta : 0;
  get_input_samples(pb, samples, count);
  int16_t input_peak = 0;
  int16_t input_first = 0, input_last = 0;
  if (trace_voice) {
    input_first = samples[0];
    input_last = samples[count - 1];
    for (uint32_t i = 0; i < count; ++i) {
      int32_t magnitude = samples[i] < 0 ? -(int32_t) samples[i] : samples[i];
      if (magnitude > input_peak) input_peak = (int16_t) magnitude;
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    samples[i] = (int16_t)clamp16(((int32_t)samples[i] * pb.vol_env.cur_volume) >> 15);
    pb.vol_env.cur_volume += pb.vol_env.cur_volume_delta;
  }
  if (g_accum) {
    for (uint32_t i = 0; i < count; ++i) {
      g_accum->pcm = fnv_add(g_accum->pcm, samples[i]);
      const int32_t magnitude = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
      g_accum->peak = std::max(g_accum->peak, magnitude);
    }
    g_accum->samples += count;
    g_accum->ms_mask |= 1u << millisecond;
  }
  if (trace_voice) {
    int16_t output_peak = 0;
    for (uint32_t i = 0; i < count; ++i) {
      int32_t magnitude = samples[i] < 0 ? -(int32_t) samples[i] : samples[i];
      if (magnitude > output_peak) output_peak = (int16_t) magnitude;
    }
    const VoiceTrace trace{
        g_frames, pb_addr, cur_before,
        hilo(pb.audio_addr.cur_addr_hi, pb.audio_addr.cur_addr_lo),
        hilo(pb.audio_addr.end_addr_hi, pb.audio_addr.end_addr_lo),
        hilo(pb.src.ratio_hi, pb.src.ratio_lo), millisecond, frac_before,
        pb.src.cur_addr_frac, volume_before, volume_delta, input_first,
        input_last, input_peak, samples[0], samples[count - 1], output_peak};
    g_voice_trace(trace);
  }
  // Initial time delay (ucode 0x0267-0x02EF): this block goes into the work area after its volume
  // envelope, and the left and right buses read from their own positions in it (ITD-SPEC.md).
  // Voices without the flag read their block itself, so their output does not change.
  const int16_t* input_left = samples;
  const int16_t* input_right = samples;
  if (g_itd_on && count == 32 && g_itd_next <= ITD_WORDS - 32) {
    g_itd_last = g_itd_next;   // 0x0269: 0x0E1C = 0x0E42
    std::memcpy(&g_itd_dmem[g_itd_next], samples, sizeof samples);
    g_itd_next += 32;
    input_left = &g_itd_dmem[g_itd_left];
    input_right = &g_itd_dmem[g_itd_right];
  }
  // (Dolphin keeps the low-pass filter disabled.)
#define MIX_ON(C) (0 != (mctrl & MIX_##C))
#define RAMP_ON(C) (0 != (mctrl & MIX_##C##_RAMP))
  if (MIX_ON(L)) mix_add(b.ptrs[0], input_left, count, &pb.mixer.left, &pb.dpop.left, RAMP_ON(L));
  if (MIX_ON(R)) mix_add(b.ptrs[1], input_right, count, &pb.mixer.right, &pb.dpop.right, RAMP_ON(R));
  if (MIX_ON(S)) mix_add(b.ptrs[2], samples, count, &pb.mixer.surround, &pb.dpop.surround, RAMP_ON(S));
  if (MIX_ON(AUXA_L)) mix_add(b.ptrs[3], input_left, count, &pb.mixer.auxA_left, &pb.dpop.auxA_left, RAMP_ON(AUXA_L));
  if (MIX_ON(AUXA_R)) mix_add(b.ptrs[4], input_right, count, &pb.mixer.auxA_right, &pb.dpop.auxA_right, RAMP_ON(AUXA_R));
  if (MIX_ON(AUXA_S)) mix_add(b.ptrs[5], samples, count, &pb.mixer.auxA_surround, &pb.dpop.auxA_surround, RAMP_ON(AUXA_S));
  if (MIX_ON(AUXB_L)) mix_add(b.ptrs[6], input_left, count, &pb.mixer.auxB_left, &pb.dpop.auxB_left, RAMP_ON(AUXB_L));
  if (MIX_ON(AUXB_R)) mix_add(b.ptrs[7], input_right, count, &pb.mixer.auxB_right, &pb.dpop.auxB_right, RAMP_ON(AUXB_R));
  if (MIX_ON(AUXB_S)) mix_add(b.ptrs[8], samples, count, &pb.mixer.auxB_surround, &pb.dpop.auxB_surround, RAMP_ON(AUXB_S));
#undef MIX_ON
#undef RAMP_ON
  // After the mix (0x02F1-0x0330), with the flag as this millisecond's updates left it: the shifts
  // step toward their targets and the sources move to the next block less 32 plus the new shift.
  if (g_itd_on) {
    PBInitialTimeDelay& itd = pb.initial_time_delay;
    if (itd.on) {
      itd.offsetLeft = itd_step(itd.offsetLeft, itd.targetLeft);
      itd.offsetRight = itd_step(itd.offsetRight, itd.targetRight);
      g_itd_left = itd_source(g_itd_next, itd.offsetLeft);
      g_itd_right = itd_source(g_itd_next, itd.offsetRight);
    } else {
      g_itd_left = g_itd_right = g_itd_next;
    }
  }
}

void apply_updates_for_ms(int curr_ms, uint16_t* pb, const uint16_t* num_updates, uint32_t updates_addr) {
  uint32_t start_idx = 0;
  for (int i = 0; i < curr_ms; ++i) start_idx += num_updates[i];
  for (uint32_t i = start_idx; i < start_idx + num_updates[curr_ms]; ++i) {
    uint16_t update_off = g_mem.rd16(updates_addr + i * 4);
    uint16_t update_val = g_mem.rd16(updates_addr + i * 4 + 2);
    if (update_off < sizeof(AXPB) / 2) pb[update_off] = update_val;
  }
}

void download_and_mix_with_volume(uint32_t addr, uint16_t vol_main, uint16_t vol_auxa, uint16_t vol_auxb) {
  int* buffers_main[3] = {m_samples_left, m_samples_right, m_samples_surround};
  int* buffers_auxa[3] = {m_samples_auxA_left, m_samples_auxA_right, m_samples_auxA_surround};
  int* buffers_auxb[3] = {m_samples_auxB_left, m_samples_auxB_right, m_samples_auxB_surround};
  int** buffers[3] = {buffers_main, buffers_auxa, buffers_auxb};
  uint16_t volumes[3] = {vol_main, vol_auxa, vol_auxb};
  for (uint32_t i = 0; i < 3; ++i) {
    uint32_t ptr = addr;   // Dolphin re-reads from `addr` for each group (pointer reset per iteration)
    uint16_t volume = volumes[i];
    for (uint32_t j = 0; j < 3; ++j) {
      int* buffer = buffers[i][j];
      for (uint32_t k = 0; k < 160; ++k) {
        int64_t sample = (int64_t)(int32_t)g_mem.rd32(ptr); ptr += 4;
        sample *= volume;
        buffer[k] += (int32_t)(sample >> 15);
      }
    }
  }
}

void mix_aux_samples(int aux_id, uint32_t write_addr, uint32_t read_addr) {
  int* buffers[3] = {nullptr, nullptr, nullptr};
  if (aux_id == 0) { buffers[0] = m_samples_auxA_left; buffers[1] = m_samples_auxA_right; buffers[2] = m_samples_auxA_surround; }
  else { buffers[0] = m_samples_auxB_left; buffers[1] = m_samples_auxB_right; buffers[2] = m_samples_auxB_surround; }
  if (write_addr) {
    uint32_t ptr = write_addr;
    for (int* buffer : buffers) for (uint32_t j = 0; j < 160; ++j) { g_mem.wr32(ptr, (uint32_t)buffer[j]); ptr += 4; }
  }
  uint32_t ptr = read_addr;
  // Diagnostic, off unless MELEE_TRACE_AX_AUX=1: energy sent to and returned from each aux bus.
  static const bool trace = [] { const char* v = std::getenv("MELEE_TRACE_AX_AUX"); return v && v[0] == '1'; }();
  static double sent[2], back[2], dry; static uint32_t frames;
  if (trace) {
    for (int* buffer : buffers) for (uint32_t j = 0; j < 160; ++j) sent[aux_id] += (double)buffer[j] * buffer[j];
    for (uint32_t j = 0; j < 480; ++j) { const double v = (int)g_mem.rd32(read_addr + j * 4); back[aux_id] += v * v; }
    if (aux_id == 0) { for (int v : m_samples_left) dry += (double)v * v; if (++frames % 1000 == 0) {
      std::fprintf(stderr, "[ax-aux] frames %u dryL %.1f dB auxA sent %.1f back %.1f | auxB sent %.1f back %.1f\n", frames,
                   10 * std::log10(dry / frames + 1), 10 * std::log10(sent[0] / frames + 1), 10 * std::log10(back[0] / frames + 1),
                   10 * std::log10(sent[1] / frames + 1), 10 * std::log10(back[1] / frames + 1));
      sent[0] = sent[1] = back[0] = back[1] = dry = 0; frames = 0; } }
  }
  for (int& s : m_samples_left) { s += (int)g_mem.rd32(ptr); ptr += 4; }
  for (int& s : m_samples_right) { s += (int)g_mem.rd32(ptr); ptr += 4; }
  for (int& s : m_samples_surround) { s += (int)g_mem.rd32(ptr); ptr += 4; }
}

void upload_lrs(uint32_t dst_addr) {
  uint32_t ptr = dst_addr;
  for (int s : m_samples_left) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
  for (int s : m_samples_right) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
  for (int s : m_samples_surround) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
}

void set_main_lr(uint32_t src_addr) {
  for (uint32_t i = 0; i < 160; ++i) {
    int samp = (int)g_mem.rd32(src_addr + i * 4);
    m_samples_left[i] = samp; m_samples_right[i] = samp; m_samples_surround[i] = 0;
  }
}

void mix_auxb_lr(uint32_t ul_addr, uint32_t dl_addr) {
  uint32_t ptr = ul_addr;
  for (int s : m_samples_auxB_left) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
  for (int s : m_samples_auxB_right) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
  ptr = dl_addr;
  for (uint32_t i = 0; i < 160; ++i) { int samp = (int)g_mem.rd32(ptr); ptr += 4; m_samples_auxB_left[i] = samp; m_samples_left[i] += samp; }
  for (uint32_t i = 0; i < 160; ++i) { int samp = (int)g_mem.rd32(ptr); ptr += 4; m_samples_auxB_right[i] = samp; m_samples_right[i] += samp; }
}

void set_opposite_lr(uint32_t src_addr) {
  for (uint32_t i = 0; i < 160; ++i) {
    int inp = (int)g_mem.rd32(src_addr + i * 4);
    m_samples_left[i] = -inp; m_samples_right[i] = inp; m_samples_surround[i] = 0;
  }
}

void send_aux_and_mix(uint32_t main_auxa_up, uint32_t auxb_s_up, uint32_t main_l_dl, uint32_t main_r_dl, uint32_t auxb_l_dl, uint32_t auxb_r_dl) {
  int* up_buffers[] = {m_samples_auxA_left, m_samples_auxA_right, m_samples_auxA_surround};
  uint32_t ptr = main_auxa_up;
  for (int* up : up_buffers) for (uint32_t j = 0; j < 160; ++j) { g_mem.wr32(ptr, (uint32_t)up[j]); ptr += 4; }
  ptr = auxb_s_up;
  for (int s : m_samples_auxB_surround) { g_mem.wr32(ptr, (uint32_t)s); ptr += 4; }
  int* dl_buffers[] = {m_samples_left, m_samples_right, m_samples_auxB_left, m_samples_auxB_right};
  uint32_t dl_addrs[] = {main_l_dl, main_r_dl, auxb_l_dl, auxb_r_dl};
  for (size_t i = 0; i < 4; ++i)
    for (uint32_t j = 0; j < 160; ++j) dl_buffers[i][j] += (int)g_mem.rd32(dl_addrs[i] + j * 4);
}

void copy_cmdlist(uint32_t addr, uint16_t size) {
  if (size >= 512) { m_cmdlist_size = 0; return; }
  for (uint32_t i = 0; i < size; ++i, addr += 2) m_cmdlist[i] = g_mem.rd16(addr);
  m_cmdlist_size = size;
}

void handle_command_list() {
  uint32_t pb_addr = 0;
  uint32_t idx = 0;
  bool end = false;
  int hops = 0, steps = 0;
  auto next = [&]() -> uint16_t { return idx < 512 ? m_cmdlist[idx++] : (uint16_t)CMD_END; };
  if (g_frame_trace) {
    char line[48];
    std::snprintf(line, sizeof line, "kind=frame frame=%llu", (unsigned long long)g_frames);
    g_frame_trace(line);
  }
  while (!end && idx < 512 && steps++ < 4096) {
    uint16_t cmd = next();
    switch (cmd) {
      case CMD_SETUP: { uint16_t hi = next(), lo = next(); setup_processing(hilo(hi, lo)); break; }
      case CMD_DL_AND_VOL_MIX: { uint16_t hi = next(), lo = next(); uint16_t vm = next(), va = next(), vb = next(); download_and_mix_with_volume(hilo(hi, lo), vm, va, vb); break; }
      case CMD_PB_ADDR: { uint16_t hi = next(), lo = next(); pb_addr = hilo(hi, lo); break; }
      case CMD_PROCESS: process_pb_list(pb_addr); break;
      case CMD_MIX_AUXA:
      case CMD_MIX_AUXB: { uint16_t hi = next(), lo = next(), hi2 = next(), lo2 = next(); mix_aux_samples(cmd - CMD_MIX_AUXA, hilo(hi, lo), hilo(hi2, lo2)); break; }
      case CMD_UPLOAD_LRS: { uint16_t hi = next(), lo = next(); upload_lrs(hilo(hi, lo)); break; }
      case CMD_SET_LR: { uint16_t hi = next(), lo = next(); set_main_lr(hilo(hi, lo)); break; }
      case CMD_UNK_08: idx += 10; break;
      case CMD_MIX_AUXB_NOWRITE: { uint16_t hi = next(), lo = next(); mix_aux_samples(1, 0, hilo(hi, lo)); break; }
      case CMD_COMPRESSOR_TABLE_ADDR: idx += 2; break;
      case CMD_UNK_0B: break;
      case CMD_UNK_0C: break;
      case CMD_MORE: { uint16_t hi = next(), lo = next(), size = next(); if (++hops > 16 || size == 0 || size >= 512) { end = true; break; } copy_cmdlist(hilo(hi, lo), size); idx = 0; break; }
      case CMD_OUTPUT: { uint16_t hi = next(), lo = next(), hi2 = next(), lo2 = next(); output_samples(hilo(hi2, lo2), hilo(hi, lo)); break; }
      case CMD_END: end = true; break;
      case CMD_MIX_AUXB_LR: { uint16_t hi = next(), lo = next(), hi2 = next(), lo2 = next(); mix_auxb_lr(hilo(hi, lo), hilo(hi2, lo2)); break; }
      case CMD_SET_OPPOSITE_LR: { uint16_t hi = next(), lo = next(); set_opposite_lr(hilo(hi, lo)); break; }
      case CMD_UNK_12: idx += 4; break;
      case CMD_SEND_AUX_AND_MIX: {
        uint16_t a_hi = next(), a_lo = next(), b_hi = next(), b_lo = next(), c_hi = next(), c_lo = next();
        uint16_t d_hi = next(), d_lo = next(), e_hi = next(), e_lo = next(), f_hi = next(), f_lo = next();
        send_aux_and_mix(hilo(a_hi, a_lo), hilo(b_hi, b_lo), hilo(c_hi, c_lo), hilo(d_hi, d_lo), hilo(e_hi, e_lo), hilo(f_hi, f_lo));
        break;
      }
      default: end = true; break;
    }
  }
  ++g_frames;
}

}  // namespace

void set_memory(const Memory& mem) { g_mem = mem; }
void set_voice_trace(VoiceTraceFn trace) { g_voice_trace = trace; }
void set_frame_trace(FrameTraceFn sink) { g_frame_trace = sink; }
void set_itd_enabled(bool on) { g_itd_on = on; }
void reset() {
  m_cmdlist_size = 0; m_next_is_cmdlist = false; m_pending_cmdlist_size = 0; g_frames = 0;
  g_written_back.clear();
  int* all[] = {m_samples_left, m_samples_right, m_samples_surround, m_samples_auxA_left, m_samples_auxA_right, m_samples_auxA_surround, m_samples_auxB_left, m_samples_auxB_right, m_samples_auxB_surround};
  for (int* b : all) std::memset(b, 0, 160 * sizeof(int));
  // The DSP's work area as a freshly booted DSP has it (its DMEM image is zeros).
  std::memset(g_itd_dmem, 0, sizeof g_itd_dmem);
  g_itd_left = g_itd_right = g_itd_next = ITD_BLOCK0;
  g_itd_last = ITD_NONE;
}
uint64_t frames_processed() { return g_frames; }

uint32_t convert_mixer_control(uint16_t mixer_control) {
  // ucode 0x4e8a8b21 mapping (Dolphin AXUCode::ConvertMixerControl).
  uint32_t ret = MIX_L | MIX_R;
  if (mixer_control & 0x0001) ret |= MIX_AUXA_L | MIX_AUXA_R;
  if (mixer_control & 0x0002) ret |= MIX_AUXB_L | MIX_AUXB_R;
  if (mixer_control & 0x0004) {
    ret |= MIX_S;
    if (ret & MIX_AUXA_L) ret |= MIX_AUXA_S;
    if (ret & MIX_AUXB_L) ret |= MIX_AUXB_S;
  }
  if (mixer_control & 0x0008) {
    ret |= MIX_L_RAMP | MIX_R_RAMP;
    if (ret & MIX_AUXA_L) ret |= MIX_AUXA_L_RAMP | MIX_AUXA_R_RAMP;
    if (ret & MIX_AUXB_L) ret |= MIX_AUXB_L_RAMP | MIX_AUXB_R_RAMP;
    if (ret & MIX_AUXA_S) ret |= MIX_AUXA_S_RAMP;
    if (ret & MIX_AUXB_S) ret |= MIX_AUXB_S_RAMP;
  }
  return ret;
}

void setup_processing(uint32_t init_addr) {
  uint16_t init_data[0x20];
  for (uint32_t i = 0; i < 0x20; ++i) init_data[i] = g_mem.rd16(init_addr + 2 * i);
  int* buffers[] = {m_samples_left, m_samples_right, m_samples_surround, m_samples_auxA_left, m_samples_auxA_right, m_samples_auxA_surround, m_samples_auxB_left, m_samples_auxB_right, m_samples_auxB_surround};
  uint32_t init_idx = 0;
  for (int* buffer : buffers) {
    int32_t init_val = (int32_t)(((uint32_t)init_data[init_idx] << 16) | init_data[init_idx + 1]);
    int16_t delta = (int16_t)init_data[init_idx + 2];
    init_idx += 3;
    if (!init_val) std::memset(buffer, 0, 160 * sizeof(int));
    else for (uint32_t j = 0; j < 160; ++j) { buffer[j] = init_val; init_val += delta; }
  }
}

void process_pb_list(uint32_t pb_addr) {
  const uint32_t spms = 32;
  AXPB pb;
  int guard = 0;
  if (!g_itd_logged) {
    g_itd_logged = true;
    host::log(g_itd_on ? "audio: AX initial time delay on (MELEE_AUDIO_ITD=0 turns it off)"
                       : "audio: AX initial time delay off (MELEE_AUDIO_ITD=0)");
  }
  while (pb_addr && guard++ < 256) {
    Buffers buffers{{m_samples_left, m_samples_right, m_samples_surround, m_samples_auxA_left, m_samples_auxA_right, m_samples_auxA_surround, m_samples_auxB_left, m_samples_auxB_right, m_samples_auxB_surround}};
    read_pb(pb_addr, pb);
    AXPB begin;
    VoiceFrameAccum accum;
    if (g_frame_trace) {
      begin = pb;
      g_accum = &accum;
    }
    if (g_itd_on) itd_voice_setup(pb);
    uint32_t updates_addr = hilo(pb.updates.data_hi, pb.updates.data_lo);
    for (int curr_ms = 0; curr_ms < 5; ++curr_ms) {
      apply_updates_for_ms(curr_ms, (uint16_t*)&pb, pb.updates.num_updates, updates_addr);
      process_voice(pb, buffers, (uint16_t)spms,
                    convert_mixer_control(pb.mixer_control), pb_addr,
                    (uint16_t) curr_ms);
      for (int*& p : buffers.ptrs) p += spms;
    }
    g_accum = nullptr;
    if (g_itd_on) itd_write_back(pb);   // the ucode sends the history back before the PB
    write_pb(pb_addr, pb);
    if (g_frame_trace) trace_voice_frame(pb_addr, begin, pb, accum);
    pb_addr = hilo(pb.next_pb_hi, pb.next_pb_lo);
  }
}

OutputSink g_output_sink = nullptr;
uint64_t g_blocks_output = 0;

void output_samples(uint32_t lr_addr, uint32_t surround_addr) {
  for (uint32_t i = 0; i < 160; ++i) g_mem.wr32(surround_addr + i * 4, (uint32_t)m_samples_surround[i]);
  // Clamped 16-bit samples interleaved R L R L ... (AI DMA order).
  int16_t rl[320];
  for (uint32_t i = 0; i < 160; ++i) {
    int left = clamp16(m_samples_left[i]), right = clamp16(m_samples_right[i]);
    g_mem.wr16(lr_addr + i * 4, (uint16_t)right);
    g_mem.wr16(lr_addr + i * 4 + 2, (uint16_t)left);
    rl[i * 2] = (int16_t)right;
    rl[i * 2 + 1] = (int16_t)left;
  }
  ++g_blocks_output;
  if (g_output_sink) g_output_sink(rl, 160);
}

void set_output_sink(OutputSink sink) { g_output_sink = sink; }
uint64_t blocks_output() { return g_blocks_output; }

void handle_mail(uint32_t mail) {
  if (m_next_is_cmdlist) {
    m_next_is_cmdlist = false;
    copy_cmdlist(mail, m_pending_cmdlist_size);
    handle_command_list();
    m_cmdlist_size = 0;
    return;
  }
  if ((mail & MAIL_CMDLIST_MASK) == MAIL_CMDLIST) {
    m_next_is_cmdlist = true;
    m_pending_cmdlist_size = (uint16_t)(mail & ~MAIL_CMDLIST_MASK);
  }
  // MAIL_RESUME / MAIL_CONTINUE / MAIL_RESET need no action: the DSP acknowledges instantly here.
}

}  // namespace ax
