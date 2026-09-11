// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include "audio.h"
#include "host.h"

#pragma comment(lib, "winmm.lib")

namespace host {
namespace {
constexpr int SAMPLE_RATE = 32000;
constexpr int BLOCK_BYTES = 640;      // one 5 ms AI DMA frame: 160 stereo samples
constexpr int BLOCKS = 24;            // 120 ms of queue; more than that is dropped (fast mode)
HWAVEOUT g_out = nullptr;
WAVEHDR g_headers[BLOCKS];
int16_t g_blocks[BLOCKS][BLOCK_BYTES / 2];
int g_next = 0;
std::mutex g_mutex;
uint64_t g_frames = 0, g_dropped = 0;
bool g_open = false;
FILE* g_wav = nullptr;
uint32_t g_wav_bytes = 0;

void wav_header(FILE* f, uint32_t data_bytes) {
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(SAMPLE_RATE); u32(SAMPLE_RATE * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(data_bytes);
}
}  // namespace

bool audio_open(int volume_percent, const char* wav_dump_path, bool open_device) {
  if (wav_dump_path && *wav_dump_path) {
    g_wav = std::fopen(wav_dump_path, "wb");
    if (g_wav) { wav_header(g_wav, 0); g_wav_bytes = 0; g_open = true; }
    else log("audio: cannot open %s", wav_dump_path);
  }
  if (!open_device) return g_open;
  WAVEFORMATEX fmt{};
  fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = SAMPLE_RATE;
  fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = SAMPLE_RATE * 4;
  if (waveOutOpen(&g_out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    log("audio: waveOutOpen failed; audio output disabled");
    g_out = nullptr;
    return false;
  }
  for (int i = 0; i < BLOCKS; ++i) {
    std::memset(&g_headers[i], 0, sizeof(WAVEHDR));
    g_headers[i].lpData = (LPSTR)g_blocks[i];
    g_headers[i].dwBufferLength = BLOCK_BYTES;
    waveOutPrepareHeader(g_out, &g_headers[i], sizeof(WAVEHDR));
    g_headers[i].dwFlags |= WHDR_DONE;   // free
  }
  int v = std::clamp(volume_percent, 0, 100);
  DWORD vol16 = (DWORD)(0xFFFF * v / 100);
  waveOutSetVolume(g_out, vol16 | (vol16 << 16));   // per-session volume on modern Windows
  g_open = true;
  log("audio: WinMM 32 kHz stereo, volume %d%%", v);
  return true;
}

void audio_close() {
  if (g_wav) {
    std::fseek(g_wav, 0, SEEK_SET);
    wav_header(g_wav, g_wav_bytes);
    std::fclose(g_wav); g_wav = nullptr;
  }
  if (!g_out) { g_open = false; return; }
  waveOutReset(g_out);
  for (int i = 0; i < BLOCKS; ++i) waveOutUnprepareHeader(g_out, &g_headers[i], sizeof(WAVEHDR));
  waveOutClose(g_out);
  g_out = nullptr; g_open = false;
}

void audio_push(const uint8_t* be_samples, size_t bytes) {
  if (!g_open) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (size_t off = 0; off + BLOCK_BYTES <= bytes; off += BLOCK_BYTES) {
    int16_t converted[BLOCK_BYTES / 2];
    const uint8_t* src = be_samples + off;
    for (int i = 0; i < BLOCK_BYTES / 4; ++i) {
      int16_t r = (int16_t)((src[i * 4] << 8) | src[i * 4 + 1]);
      int16_t l = (int16_t)((src[i * 4 + 2] << 8) | src[i * 4 + 3]);
      converted[i * 2] = l; converted[i * 2 + 1] = r;
    }
    if (g_wav) { std::fwrite(converted, 1, BLOCK_BYTES, g_wav); g_wav_bytes += BLOCK_BYTES; }
    if (!g_out) { g_frames += BLOCK_BYTES / 4; continue; }
    WAVEHDR& h = g_headers[g_next];
    if (!(h.dwFlags & WHDR_DONE)) { ++g_dropped; continue; }   // queue full: drop instead of stalling the simulation
    std::memcpy(g_blocks[g_next], converted, BLOCK_BYTES);
    h.dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(g_out, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { h.dwFlags |= WHDR_DONE; ++g_dropped; continue; }
    g_next = (g_next + 1) % BLOCKS;
    g_frames += BLOCK_BYTES / 4;
  }
}

uint64_t audio_pushed_frames() { return g_frames; }
uint64_t audio_dropped_blocks() { return g_dropped; }

}  // namespace host
