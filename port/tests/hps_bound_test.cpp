// SPDX-License-Identifier: GPL-2.0-or-later
// The HPS song decoder against damaged files: lengths that wrap a 32-bit sum, a block chain that
// points at itself or overlaps, a sample rate that would ask the resampler for gigabytes.
// The decoder is internal to jukebox.cpp, so the source is compiled into this test.
#include "../runtime/hle/jukebox.cpp"
#include <cstdio>

namespace host {
void log(const char*, ...) {}
bool disc_read(uint32_t, void*, uint32_t) { return false; }
bool disc_find_path_by_offset(uint32_t, std::string*) { return false; }
bool disc_music_paths(std::vector<std::string>* paths) { if (paths) paths->clear(); return false; }
void sim_cost_add(int, double) {}
const double tsc_seconds = 0.0;
}  // namespace host

namespace {
using Bytes = std::vector<uint8_t>;
void set32(Bytes& file, size_t at, uint32_t value) {
  for (int k = 0; k < 4; ++k) file[at + k] = (uint8_t)(value >> (24 - 8 * k));
}
Bytes header(size_t size, uint32_t rate = 32000) {
  Bytes file(size, 0);
  std::memcpy(file.data(), " HALPST\0", 8);
  set32(file, 8, rate);
  set32(file, 12, 2);
  return file;
}
void block(Bytes& file, uint32_t off, uint32_t len, uint32_t next) {
  set32(file, off, len);
  set32(file, off + 8, next);
}
// Two blocks of 0x40 bytes of frames each (4 frames a channel, 56 samples), the second looping to the first.
Bytes valid_song(uint32_t rate = 32000, uint32_t last_next = 0x80) {
  Bytes file = header(0x140, rate);
  block(file, 0x80, 0x40, 0xE0);
  block(file, 0xE0, 0x40, last_next);
  file[0xA1] = 0x17;   // left channel, first data byte: samples 1 and 7 (scale 1, zero coefficients)
  file[0xC1] = 0x8F;   // right channel: samples -8 and -1
  return file;
}
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAILED: %s\n", what); ++failures; }
}
}  // namespace

int main() {
  using slippi::jukebox::decode_hps;
  // A whole song decodes as it always did.
  {
    auto song = decode_hps(valid_song());
    check(song && song->samples.size() == 224 && song->loop_frame == 0, "looping song: 112 frames, loop at 0");
    check(song && song->samples.size() == 224 && song->samples[0] == 1 && song->samples[2] == 7 &&
          song->samples[1] == -8 && song->samples[3] == -1 && song->samples[4] == 0, "decoded samples");
    auto once = decode_hps(valid_song(32000, 0xFFFFFFFFu));
    check(once && once->samples.size() == 224 && once->loop_frame == SIZE_MAX, "one-shot song");
    auto resampled = decode_hps(valid_song(48000));
    check(resampled && resampled->samples.size() == 148 && resampled->loop_frame == 0, "48 kHz song resampled");
    auto unstated = decode_hps(valid_song(0));
    check(unstated && unstated->samples.size() == 224, "rate 0 plays as 32 kHz");
  }
  // A length that wraps the 32-bit bound (0x80 + 0x20 + 0xFFFFFF80 = 0x20) is refused.
  {
    Bytes file = header(0x140);
    block(file, 0x80, 0xFFFFFF80u, 0xFFFFFFFFu);
    check(!decode_hps(file), "wrapping length");
    block(file, 0x80, 0xFFFFFFF8u, 0xFFFFFFFFu);
    check(!decode_hps(file), "largest length");
    block(file, 0x80, 0xA8, 0xFFFFFFFFu);   // 8 bytes more than the file holds
    check(!decode_hps(file), "length past the end");
    block(file, 0x80, 0xA0, 0xFFFFFFFFu);   // exactly what the file holds
    auto song = decode_hps(file);
    check(song && song->samples.size() == 280, "length to the end of the file");
  }
  // A good block, then one whose length wraps: the first still plays, the second is not read.
  {
    Bytes file = valid_song();
    block(file, 0xE0, 0xFFFFFF00u, 0x80);
    auto song = decode_hps(file);
    check(song && song->samples.size() == 112 && song->loop_frame == SIZE_MAX, "wrapping length in a later block");
  }
  // A next that points at its own block or backwards ends the chain (and is the loop point).
  {
    auto self = decode_hps(valid_song(32000, 0xE0));
    check(self && self->samples.size() == 224 && self->loop_frame == 56, "next pointing at itself");
    Bytes file = valid_song();
    block(file, 0x80, 0x40, 0x80);
    auto first = decode_hps(file);
    check(first && first->samples.size() == 112 && first->loop_frame == 0, "first block pointing at itself");
    block(file, 0x80, 0x40, 0x10);
    auto back = decode_hps(file);
    check(back && back->samples.size() == 112 && back->loop_frame == SIZE_MAX, "next before the first block");
  }
  // Overlapping blocks, each claiming the rest of the file: decoded once, not once per block.
  {
    const uint32_t size = 0x2080;
    Bytes file = header(size);
    for (uint32_t off = 0x80; off + 0x20 <= size; off += 0x20) block(file, off, (size - off - 0x20) & ~7u, off + 0x20);
    auto song = decode_hps(file);
    check(song && song->samples.size() == 14280, "overlapping chain stops at the file's own size");
  }
  // Headers only, a short file, and a rate that would make the resampler ask for gigabytes.
  {
    check(!decode_hps(header(0x80)), "no blocks");
    check(!decode_hps(Bytes(0x40, 0)), "short file");
    check(!decode_hps(valid_song(1)), "1 Hz song");
    check(!decode_hps(valid_song(7999)), "7999 Hz song");
    auto low = decode_hps(valid_song(8000));
    check(low && low->samples.size() == 896, "8 kHz song resampled");
  }
  if (failures) return 1;
  std::printf("HPS bound test passed\n");
  return 0;
}
