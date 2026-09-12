// Port of Slippi's jukebox (Rust, hps_decode 0.3.0) onto the host audio mixer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "jukebox.h"
#include "host.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace slippi::jukebox {
namespace {
constexpr uint32_t OUTPUT_RATE = 32000;
constexpr double VOLUME_REDUCTION = 0.8;   // Slippi plays music a little under the game's level

struct Song {
  std::vector<int16_t> samples;   // interleaved stereo at OUTPUT_RATE
  size_t loop_frame = SIZE_MAX;   // frame index the song restarts at, or SIZE_MAX for one-shot
  size_t position = 0;            // frames played
};

std::mutex g_mutex;
std::shared_ptr<Song> g_song;     // replaced atomically under the mutex; the mixer holds a copy
std::atomic<int> g_melee_volume{254}, g_user_volume{100};

uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
int16_t be16(const uint8_t* p) { return (int16_t)((p[0] << 8) | p[1]); }

// Decodes one channel's DSP-ADPCM frames (8 bytes: header + 7 data bytes = 14 samples).
void decode_frames(const uint8_t* data, size_t frames, int16_t hist1, int16_t hist2, const int16_t coef[16], std::vector<int16_t>& out) {
  static const int8_t nibble_to_i8[16] = {0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1};
  for (size_t f = 0; f < frames; ++f) {
    const uint8_t* frame = data + f * 8;
    int scale = 1 << (frame[0] & 0xF);
    int ci = (frame[0] >> 4) & 7;
    int c1 = coef[ci * 2], c2 = coef[ci * 2 + 1];
    for (int b = 1; b < 8; ++b) {
      for (int n = 0; n < 2; ++n) {
        int nib = nibble_to_i8[n == 0 ? (frame[b] >> 4) & 0xF : frame[b] & 0xF];
        int32_t s = (((nib * scale) << 11) + 1024 + (c1 * hist1 + c2 * hist2)) >> 11;
        int16_t sample = (int16_t)std::clamp(s, -32768, 32767);
        hist2 = hist1; hist1 = sample;
        out.push_back(sample);
      }
    }
  }
}

std::shared_ptr<Song> decode_hps(const std::vector<uint8_t>& file) {
  if (file.size() < 0x80 || std::memcmp(file.data(), " HALPST\0", 8) != 0) { host::log("jukebox: not an HPS file"); return nullptr; }
  uint32_t rate = be32(file.data() + 8), channels = be32(file.data() + 12);
  if (channels != 2) { host::log("jukebox: %u channels unsupported", channels); return nullptr; }
  int16_t coef[2][16];
  for (int ch = 0; ch < 2; ++ch) {
    const uint8_t* info = file.data() + 0x10 + ch * 0x38;
    for (int k = 0; k < 16; ++k) coef[ch][k] = be16(info + 0x10 + k * 2);
  }
  // Blocks: length, (unused), next offset, two decoder states (8 bytes each), pad, then frames.
  struct Block { uint32_t offset, next; std::vector<int16_t> pcm; };
  std::vector<Block> blocks;
  std::vector<int16_t> left, right;
  uint32_t off = 0x80;
  while (off + 0x20 <= file.size()) {
    const uint8_t* b = file.data() + off;
    uint32_t len = be32(b), next = be32(b + 8);
    if (off + 0x20 + len > file.size() || (len % 8) != 0) break;
    int16_t h1l = be16(b + 0x0C + 2), h2l = be16(b + 0x0C + 4), h1r = be16(b + 0x14 + 2), h2r = be16(b + 0x14 + 4);
    size_t frames = len / 8, half = frames / 2;
    left.clear(); right.clear();
    decode_frames(b + 0x20, half, h1l, h2l, coef[0], left);
    decode_frames(b + 0x20 + half * 8, frames - half, h1r, h2r, coef[1], right);
    Block blk{off, next, {}};
    size_t n = std::min(left.size(), right.size());
    blk.pcm.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { blk.pcm.push_back(left[i]); blk.pcm.push_back(right[i]); }
    blocks.push_back(std::move(blk));
    if (next == 0xFFFFFFFFu || next <= off || next >= file.size()) break;
    off = next;
  }
  if (blocks.empty()) { host::log("jukebox: no blocks"); return nullptr; }
  auto song = std::make_shared<Song>();
  size_t loop_frame = SIZE_MAX, frame_index = 0;
  uint32_t loop_target = blocks.back().next;
  for (auto& blk : blocks) {
    if (blk.offset == loop_target) loop_frame = frame_index;
    frame_index += blk.pcm.size() / 2;
  }
  // Resample to the mixer rate if the track is not 32 kHz (Melee's are).
  if (rate == OUTPUT_RATE || rate == 0) {
    for (auto& blk : blocks) song->samples.insert(song->samples.end(), blk.pcm.begin(), blk.pcm.end());
    song->loop_frame = loop_frame;
  } else {
    std::vector<int16_t> all;
    for (auto& blk : blocks) all.insert(all.end(), blk.pcm.begin(), blk.pcm.end());
    size_t in_frames = all.size() / 2, out_frames = (size_t)((uint64_t)in_frames * OUTPUT_RATE / rate);
    song->samples.resize(out_frames * 2);
    for (size_t i = 0; i < out_frames; ++i) {
      double src = (double)i * rate / OUTPUT_RATE;
      size_t a = std::min((size_t)src, in_frames - 1), bb = std::min(a + 1, in_frames - 1);
      double t = src - (double)a;
      for (int ch = 0; ch < 2; ++ch) song->samples[i * 2 + ch] = (int16_t)(all[a * 2 + ch] * (1.0 - t) + all[bb * 2 + ch] * t);
    }
    song->loop_frame = loop_frame == SIZE_MAX ? SIZE_MAX : (size_t)((uint64_t)loop_frame * OUTPUT_RATE / rate);
  }
  host::log("jukebox: song %u Hz, %zu frames, %s", rate, song->samples.size() / 2, song->loop_frame == SIZE_MAX ? "no loop" : "loops");
  return song;
}
}  // namespace

void start_song(uint32_t disc_offset, uint32_t size) {
  if (size == 0 || size > 64u * 1024 * 1024) { host::log("jukebox: bad song size %u", size); return; }
  std::vector<uint8_t> file(size);
  if (!host::disc_read(disc_offset, file.data(), size)) { host::log("jukebox: cannot read song at %08X", disc_offset); return; }
  auto song = decode_hps(file);
  std::lock_guard<std::mutex> lk(g_mutex);
  g_song = song;
}

void stop() { std::lock_guard<std::mutex> lk(g_mutex); g_song.reset(); }
void set_melee_volume(uint8_t volume) { g_melee_volume.store(volume); }
void set_user_volume(int percent) { g_user_volume.store(std::clamp(percent, 0, 100)); }
int user_volume() { return g_user_volume.load(); }

void mix(int16_t* out, size_t frames) {
  std::shared_ptr<Song> song;
  { std::lock_guard<std::mutex> lk(g_mutex); song = g_song; }
  if (!song || song->samples.empty()) return;
  double gain = (g_melee_volume.load() / 254.0) * (g_user_volume.load() / 100.0) * VOLUME_REDUCTION;
  if (gain <= 0.0) return;
  size_t total = song->samples.size() / 2;
  for (size_t i = 0; i < frames; ++i) {
    if (song->position >= total) {
      if (song->loop_frame == SIZE_MAX) { std::lock_guard<std::mutex> lk(g_mutex); if (g_song == song) g_song.reset(); return; }
      song->position = std::min(song->loop_frame, total - 1);
    }
    for (int ch = 0; ch < 2; ++ch) {
      int32_t v = out[i * 2 + ch] + (int32_t)(song->samples[song->position * 2 + ch] * gain);
      out[i * 2 + ch] = (int16_t)std::clamp(v, -32768, 32767);
    }
    ++song->position;
  }
}
}  // namespace slippi::jukebox
