// Host audio output: 32 kHz 16-bit stereo blocks from the emulated AI DMA. Primary path is WASAPI
// shared mode on the default render endpoint (the device Windows and browsers use, with the
// mixer's own resampling); WinMM waveOut is the fallback. Optional WAV dump of everything played.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "audio.h"
#include "host.h"
#include "jukebox.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

namespace host {
namespace {
constexpr int SAMPLE_RATE = 32000;
constexpr int BLOCK_BYTES = 640;      // one 5 ms AI DMA frame: 160 stereo samples
constexpr int BLOCKS = 24;            // 120 ms of queue; more than that is dropped (fast mode)
std::atomic<int> g_volume{0};
std::mutex g_mutex;
uint64_t g_frames = 0, g_dropped = 0;
bool g_open = false;
FILE* g_wav = nullptr;
uint32_t g_wav_bytes = 0;

// ---- WinMM fallback
HWAVEOUT g_out = nullptr;
WAVEHDR g_headers[BLOCKS];
int16_t g_blocks[BLOCKS][BLOCK_BYTES / 2];
int g_next = 0;

// ---- WASAPI: ring of stereo frames fed by the simulation, drained by an event-driven thread.
constexpr size_t RING_FRAMES = SAMPLE_RATE * 120 / 1000;   // 120 ms
constexpr size_t PREFILL_FRAMES = SAMPLE_RATE * 48 / 1000;  // 48 ms of headroom before output resumes after an underrun
int16_t g_ring[RING_FRAMES * 2];
bool g_prefilling = true;
uint64_t g_underruns = 0, g_underrun_frames = 0;                // guarded by g_mutex
size_t g_ring_read = 0, g_ring_count = 0;                   // guarded by g_mutex
IAudioClient* g_client = nullptr;
IAudioRenderClient* g_render = nullptr;
HANDLE g_event = nullptr;
std::thread g_thread;
std::atomic<bool> g_running{false};
UINT32 g_buffer_frames = 0;

void wav_header(FILE* f, uint32_t data_bytes) {
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(SAMPLE_RATE); u32(SAMPLE_RATE * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(data_bytes);
}

void wasapi_thread() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (g_running.load()) {
    if (WaitForSingleObject(g_event, 200) != WAIT_OBJECT_0) continue;
    UINT32 padding = 0;
    if (FAILED(g_client->GetCurrentPadding(&padding))) continue;
    UINT32 want = g_buffer_frames > padding ? g_buffer_frames - padding : 0;
    if (!want) continue;
    BYTE* dst = nullptr;
    if (FAILED(g_render->GetBuffer(want, &dst))) continue;
    int16_t* out = (int16_t*)dst;
    int volume = g_volume.load();
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      // After an underrun (or at start) hold silence until the ring has PREFILL again, so a single
      // simulation hitch costs one gap instead of a burst of crackles while the ring stays near empty.
      if (g_prefilling && g_ring_count < PREFILL_FRAMES) { std::memset(dst, 0, want * 4); g_render->ReleaseBuffer(want, 0); continue; }
      g_prefilling = false;
      UINT32 have = (UINT32)std::min<size_t>(g_ring_count, want);
      if (have < want) { ++g_underruns; g_underrun_frames += want - have; g_prefilling = true; }
      for (UINT32 i = 0; i < have; ++i) {
        size_t idx = (g_ring_read + i) % RING_FRAMES;
        out[i * 2] = (int16_t)((int32_t)g_ring[idx * 2] * volume / 100);
        out[i * 2 + 1] = (int16_t)((int32_t)g_ring[idx * 2 + 1] * volume / 100);
      }
      g_ring_read = (g_ring_read + have) % RING_FRAMES;
      g_ring_count -= have;
      if (have < want) std::memset(out + have * 2, 0, (want - have) * 4);   // underrun: silence
    }
    if (volume > 0) slippi::jukebox::mix(out, want);
    g_render->ReleaseBuffer(want, 0);
  }
  CoUninitialize();
}

bool wasapi_open() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { log("audio: CoInitialize failed (%08X)", (unsigned)hr); return false; }
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) return false;
  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  enumerator->Release();
  if (FAILED(hr)) { log("audio: no default render device (%08X)", (unsigned)hr); return false; }
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client);
  device->Release();
  if (FAILED(hr)) { log("audio: IAudioClient activate failed (%08X)", (unsigned)hr); return false; }
  WAVEFORMATEX fmt{};
  fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = SAMPLE_RATE;
  fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = SAMPLE_RATE * 4;
  const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  hr = g_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 40 * 10000 /* 40 ms, 100 ns units */, 0, &fmt, nullptr);
  if (FAILED(hr)) { log("audio: IAudioClient initialize failed (%08X)", (unsigned)hr); g_client->Release(); g_client = nullptr; return false; }
  g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (FAILED(g_client->SetEventHandle(g_event)) || FAILED(g_client->GetBufferSize(&g_buffer_frames)) ||
      FAILED(g_client->GetService(__uuidof(IAudioRenderClient), (void**)&g_render))) {
    log("audio: IAudioClient setup failed"); g_client->Release(); g_client = nullptr; return false;
  }
  g_running.store(true);
  g_thread = std::thread(wasapi_thread);
  if (FAILED(g_client->Start())) { log("audio: IAudioClient start failed"); g_running.store(false); g_thread.join(); g_render->Release(); g_render = nullptr; g_client->Release(); g_client = nullptr; return false; }
  return true;
}

void wasapi_close() {
  if (!g_client) return;
  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();
  g_client->Stop();
  if (g_render) { g_render->Release(); g_render = nullptr; }
  g_client->Release(); g_client = nullptr;
  if (g_event) { CloseHandle(g_event); g_event = nullptr; }
}

bool winmm_open() {
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
  return true;
}
}  // namespace

void audio_set_volume(int volume) { g_volume.store(std::clamp(volume, 0, 100)); }
int audio_volume() { return g_volume.load(); }

bool audio_open(int volume_percent, const char* wav_dump_path, bool open_device) {
  if (wav_dump_path && *wav_dump_path) {
    g_wav = std::fopen(wav_dump_path, "wb");
    if (g_wav) { wav_header(g_wav, 0); g_wav_bytes = 0; g_open = true; }
    else log("audio: cannot open %s", wav_dump_path);
  }
  if (!open_device) return g_open;
  int v = std::clamp(volume_percent, 0, 100);
  g_volume = v; // software gain applies only to our PCM
  const char* backend = "none";
  if (wasapi_open()) backend = "WASAPI shared mode, default device";
  else if (winmm_open()) backend = "WinMM waveOut";
  else return g_open;
  g_open = true;
  log("audio: %s, 32 kHz stereo, volume %d%%", backend, v);
  return true;
}

void audio_close() {
  if (g_wav) {
    std::fseek(g_wav, 0, SEEK_SET);
    wav_header(g_wav, g_wav_bytes);
    std::fclose(g_wav); g_wav = nullptr;
  }
  wasapi_close();
  if (g_out) {
    waveOutReset(g_out);
    for (int i = 0; i < BLOCKS; ++i) waveOutUnprepareHeader(g_out, &g_headers[i], sizeof(WAVEHDR));
    waveOutClose(g_out);
    g_out = nullptr;
  }
  g_open = false;
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
    if (g_client) {
      constexpr size_t frames = BLOCK_BYTES / 4;
      if (g_ring_count + frames > RING_FRAMES) { ++g_dropped; continue; }   // queue full: drop instead of stalling the simulation
      size_t write = (g_ring_read + g_ring_count) % RING_FRAMES;
      for (size_t i = 0; i < frames; ++i) {
        size_t idx = (write + i) % RING_FRAMES;
        g_ring[idx * 2] = converted[i * 2]; g_ring[idx * 2 + 1] = converted[i * 2 + 1];
      }
      g_ring_count += frames;
      g_frames += frames;
      continue;
    }
    if (!g_out) { g_frames += BLOCK_BYTES / 4; continue; }
    WAVEHDR& h = g_headers[g_next];
    if (!(h.dwFlags & WHDR_DONE)) { ++g_dropped; continue; }
    for (int i = 0; i < BLOCK_BYTES / 2; ++i)
      g_blocks[g_next][i] = (int16_t)((int32_t)converted[i] * g_volume / 100);
    if (g_volume > 0) slippi::jukebox::mix(g_blocks[g_next], BLOCK_BYTES / 4);
    h.dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(g_out, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { h.dwFlags |= WHDR_DONE; ++g_dropped; continue; }
    g_next = (g_next + 1) % BLOCKS;
    g_frames += BLOCK_BYTES / 4;
  }
}

uint64_t audio_pushed_frames() { return g_frames; }
uint64_t audio_dropped_blocks() { return g_dropped; }
uint64_t audio_underruns(uint64_t* silent_ms) { std::lock_guard<std::mutex> lock(g_mutex); if (silent_ms) *silent_ms = g_underrun_frames * 1000 / SAMPLE_RATE; return g_underruns; }

}  // namespace host
