// Linux audio output: the game's 32 kHz stereo AI blocks through SDL2 (PipeWire, PulseAudio or ALSA,
// whichever the desktop runs). The same design as the Windows WASAPI path (runtime/host/audio.cpp):
// a lock-free ring written by the simulation, read by the device callback through a 48-tap Kaiser
// sinc resampler whose ratio follows the ring's fill to track the sound card's clock, with the music
// pack (jukebox) and the settings menu ticks mixed in after it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio.h"
#include "host.h"
#ifndef MELEE_NO_SLIPPI
#include "jukebox.h"
#endif
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace host {
namespace {
constexpr int SAMPLE_RATE = 32000;
constexpr int BLOCK_BYTES = 640;      // one 5 ms AI DMA frame: 160 stereo samples
constexpr size_t RING_FRAMES = 8192;  // 256 ms, power of two
constexpr size_t RING_MASK = RING_FRAMES - 1;
constexpr double MAX_RATE_SHIFT = 0.002;
constexpr double MAX_SKEW = 0.001;

std::atomic<int> g_volume{0};
std::atomic<bool> g_muted{false};
std::atomic<double> g_speed{1.0};
std::atomic<bool> g_gameplay{false};
bool g_open = false;
bool g_remember = true;
FILE* g_wav = nullptr;
uint32_t g_wav_bytes = 0;
std::atomic<uint64_t> g_frames{0}, g_dropped{0};

int16_t g_ring[RING_FRAMES][2];
std::atomic<uint64_t> g_ring_write{0}, g_ring_read{0};
std::atomic<uint64_t> g_underruns{0}, g_underrun_frames{0};
std::atomic<double> g_rate_min{1.0}, g_rate_max{1.0};
std::atomic<int> g_requested_mode{0}, g_requested_buffer_ms{40};
std::atomic<uint32_t> g_target_ms{40}, g_device_queue_ms{0}, g_engine_period_us{0};
std::string g_requested_device;
SDL_AudioDeviceID g_device = 0;
uint32_t g_device_rate = SAMPLE_RATE;
uint16_t g_device_samples = 0;

// Callback state (audio thread only).
double g_phase = 0.0, g_rate = 1.0, g_fill_average = 0.0, g_skew = 0.0;
bool g_priming = true;
int16_t g_last_output[2] = {0, 0};
size_t g_target_frames = SAMPLE_RATE * 40 / 1000;
std::vector<int16_t> g_mix;

FILE* g_trace = [] {
  const char* path = std::getenv("MELEE_AUDIO_TRACE");
  return path && *path ? std::fopen(path, "w") : nullptr;
}();
std::mutex g_trace_mutex;
int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void wav_header(FILE* f, uint32_t data_bytes, uint32_t rate = SAMPLE_RATE) {
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(data_bytes);
}

// The Windows path's band-limited converter: 48 taps, 512 phases, Kaiser beta 6, cut at 96% of the
// lower Nyquist.
constexpr int SINC_TAPS = 48, SINC_PHASES = 512;
struct SincTable {
  std::vector<float> k;
  void build(uint32_t rate) {
    k.assign((SINC_PHASES + 1) * SINC_TAPS, 0.0f);
    const double cutoff = 0.5 * 0.96 * std::min(1.0, (double)rate / SAMPLE_RATE);
    const double beta = 6.0;
    auto bessel0 = [](double x) { double s = 1, t = 1; for (int i = 1; i < 30; ++i) { t *= (x / (2 * i)) * (x / (2 * i)); s += t; } return s; };
    const double denom = bessel0(beta);
    for (int p = 0; p <= SINC_PHASES; ++p) {
      const double frac = (double)p / SINC_PHASES;
      double sum = 0;
      for (int j = 0; j < SINC_TAPS; ++j) {
        const double x = (double)(j - (SINC_TAPS / 2 - 1)) - frac;
        const double r = x / (SINC_TAPS / 2);
        const double w = std::abs(r) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - r * r)) / denom;
        const double s = x == 0.0 ? 2 * cutoff : std::sin(2 * M_PI * cutoff * x) / (M_PI * x);
        k[p * SINC_TAPS + j] = (float)(s * w);
        sum += s * w;
      }
      for (int j = 0; j < SINC_TAPS; ++j) k[p * SINC_TAPS + j] = (float)(k[p * SINC_TAPS + j] / sum);
    }
  }
} g_sinc;

std::atomic<int> g_ui_sound_request{0};
struct UiTick { int remaining = 0, total = 0; double step = 0, phase = 0, level = 0; } g_ui_tick;
void mix_ui_sound(int16_t* out, size_t frames, int volume, uint32_t rate) {
  if (const int kind = g_ui_sound_request.exchange(0)) {
    const double freq = kind == 1 ? 740.0 : kind == 2 ? 990.0 : 590.0;
    const double seconds = kind == 1 ? 0.030 : 0.055;
    g_ui_tick.total = g_ui_tick.remaining = (int)(rate * seconds);
    g_ui_tick.step = 6.283185307179586 * freq / rate;
    g_ui_tick.phase = 0.0;
    g_ui_tick.level = (kind == 1 ? 0.045 : 0.065) * 32767.0;
  }
  if (g_ui_tick.remaining <= 0 || volume <= 0) return;
  const double gain = g_ui_tick.level * volume / 100.0;
  for (size_t i = 0; i < frames && g_ui_tick.remaining > 0; ++i, --g_ui_tick.remaining) {
    const double env = (double)g_ui_tick.remaining / g_ui_tick.total;
    const int v = (int)(std::sin(g_ui_tick.phase) * env * env * gain);
    g_ui_tick.phase += g_ui_tick.step;
    for (int c = 0; c < 2; ++c) out[i * 2 + c] = (int16_t)std::clamp((int)out[i * 2 + c] + v, -32768, 32767);
  }
}

// One device period: resample the ring into `frames` stereo frames at the device rate.
void render(int16_t* out, size_t frames) {
  const uint64_t write = g_ring_write.load(std::memory_order_acquire);
  uint64_t read = g_ring_read.load(std::memory_order_relaxed);
  const size_t fill = (size_t)(write - read);
  if (g_priming) {
    if (fill < g_target_frames + SINC_TAPS) {   // not enough queued yet: silence, no underrun counted
      std::memset(out, 0, frames * 4);
      return;
    }
    g_priming = false;
  }
  // The clock correction: a slow average of the fill against the target nudges the ratio within
  // MAX_RATE_SHIFT, and a learned skew carries the steady part (the two crystals' difference).
  g_fill_average += (fill - g_fill_average) * 0.01;
  const double error = (g_fill_average - (double)g_target_frames) / std::max<double>(g_target_frames, 1.0);
  g_skew = std::clamp(g_skew + error * 1e-6, -MAX_SKEW, MAX_SKEW);
  const double shift = std::clamp(error * 0.004, -MAX_RATE_SHIFT, MAX_RATE_SHIFT);
  g_rate = (1.0 + g_skew + shift) * g_speed.load(std::memory_order_relaxed);
  const double step = g_rate * SAMPLE_RATE / g_device_rate;
  if (g_rate < g_rate_min.load()) g_rate_min.store(g_rate);
  if (g_rate > g_rate_max.load()) g_rate_max.store(g_rate);
  const int volume = g_volume.load(std::memory_order_relaxed);
  uint64_t starved = 0;
  for (size_t i = 0; i < frames; ++i) {
    const uint64_t base = read;
    if (base + SINC_TAPS / 2 + 1 >= write) {   // ran dry: hold the last sample
      out[i * 2] = g_last_output[0]; out[i * 2 + 1] = g_last_output[1];
      ++starved;
      continue;
    }
    const int phase = std::clamp((int)(g_phase * SINC_PHASES), 0, SINC_PHASES);
    const float* k = &g_sinc.k[(size_t)phase * SINC_TAPS];
    double l = 0, r = 0;
    for (int j = 0; j < SINC_TAPS; ++j) {
      const uint64_t at = base + j - (SINC_TAPS / 2 - 1);
      const int16_t* s = g_ring[at & RING_MASK];
      l += s[0] * k[j]; r += s[1] * k[j];
    }
    const int sl = std::clamp((int)std::lround(l * volume / 100.0), -32768, 32767);
    const int sr = std::clamp((int)std::lround(r * volume / 100.0), -32768, 32767);
    out[i * 2] = (int16_t)sl; out[i * 2 + 1] = (int16_t)sr;
    g_last_output[0] = (int16_t)sl; g_last_output[1] = (int16_t)sr;
    g_phase += step;
    const double whole = std::floor(g_phase);
    read += (uint64_t)whole;
    g_phase -= whole;
  }
  if (starved) {
    g_underruns.fetch_add(1, std::memory_order_relaxed);
    g_underrun_frames.fetch_add(starved, std::memory_order_relaxed);
    g_priming = true;   // refill to the target before playing again, as the Windows path does
  }
  g_ring_read.store(read, std::memory_order_release);
}

void callback(void*, Uint8* stream, int len) {
  int16_t* out = reinterpret_cast<int16_t*>(stream);
  const size_t frames = (size_t)len / 4;
  render(out, frames);
#ifndef MELEE_NO_SLIPPI
  slippi::jukebox::mix(out, frames, g_volume.load() / 100.0, (double)g_device_rate / g_rate);
#endif
  mix_ui_sound(out, frames, g_volume.load(), g_device_rate);
}

bool open_device() {
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    log("audio: SDL cannot start its audio (%s)", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want{}, have{};
  // The device's own rate, so the desktop's mixer does no second conversion; 48 kHz when unknown.
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  // Low latency asks for 5 ms periods; Auto starts safe at 10 ms.
  const int mode = g_requested_mode.load();
  want.samples = (Uint16)(mode == 1 || mode == 2 ? 256 : 512);
  want.callback = callback;
  const char* name = g_requested_device.empty() ? nullptr : g_requested_device.c_str();
  g_device = SDL_OpenAudioDevice(name, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (!g_device && name) {
    log("audio: the chosen device \"%s\" is not there; using the default", name);
    g_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  }
  if (!g_device) { log("audio: cannot open an output device (%s)", SDL_GetError()); return false; }
  g_device_rate = (uint32_t)have.freq;
  g_device_samples = have.samples;
  g_sinc.build(g_device_rate);
  const int target_ms = mode == 1 ? std::max(10, g_requested_buffer_ms.load()) : mode == 2 ? 10 : 40;
  g_target_frames = (size_t)SAMPLE_RATE * target_ms / 1000;
  g_target_ms.store((uint32_t)target_ms);
  g_engine_period_us.store((uint32_t)((uint64_t)have.samples * 1000000 / std::max<uint32_t>(g_device_rate, 1)));
  g_device_queue_ms.store((uint32_t)((uint64_t)have.samples * 1000 / std::max<uint32_t>(g_device_rate, 1)));
  g_priming = true;
  SDL_PauseAudioDevice(g_device, 0);
  log("audio: %s (%s), %u Hz, %u-frame periods, buffer %d ms", SDL_GetCurrentAudioDriver(),
      name ? name : "default device", g_device_rate, (unsigned)have.samples, target_ms);
  return true;
}
}  // namespace

void audio_set_volume(int volume) { g_volume.store(std::clamp(volume, 0, 100)); }
void audio_set_muted(bool muted) { g_muted.store(muted, std::memory_order_relaxed); }
void audio_set_device(const char* id) { g_requested_device = id ? id : ""; }
void audio_set_remember(bool remember) { g_remember = remember; }
void audio_set_speed(double speed) { g_speed.store(std::clamp(speed, 0.5, 2.0), std::memory_order_relaxed); }
void audio_set_gameplay(bool on) { g_gameplay.store(on, std::memory_order_relaxed); }
std::vector<AudioDevice> audio_list_devices() {
  std::vector<AudioDevice> out;
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return out;
  const int n = SDL_GetNumAudioDevices(0);
  for (int i = 0; i < n; ++i)
    if (const char* name = SDL_GetAudioDeviceName(i, 0)) out.push_back({name, name});
  return out;
}
bool audio_exclusive() { return false; }
// ASIO is a Windows driver model; on Linux the low-latency path is PipeWire/JACK through SDL.
void audio_set_asio(const char*, int) {}
std::vector<std::string> audio_list_asio_drivers() { return {}; }
bool audio_asio_info(AsioStatus*) { return false; }
void audio_set_buffering(int mode, int milliseconds) {
  g_requested_mode.store(mode == 3 ? 0 : mode);
  g_requested_buffer_ms.store(std::clamp(milliseconds, 5, 200));
  if (g_requested_mode.load() == 1) g_target_frames = (size_t)SAMPLE_RATE * std::max(10, g_requested_buffer_ms.load()) / 1000;
}
uint32_t audio_target_ms() { return g_target_ms.load(); }
uint32_t audio_device_queue_ms() { return g_device_queue_ms.load(); }
uint32_t audio_engine_period_us() { return g_engine_period_us.load(); }
int audio_volume() { return g_volume.load(); }
bool audio_running() { return g_open; }

bool audio_open(int volume_percent, const char* wav_dump_path, bool open) {
  if (wav_dump_path && *wav_dump_path) {
    g_wav = std::fopen(wav_dump_path, "wb");
    if (g_wav) { wav_header(g_wav, 0); g_wav_bytes = 0; g_open = true; }
    else log("audio: cannot open %s", wav_dump_path);
  }
  if (!open) return g_open;
  g_volume = std::clamp(volume_percent, 0, 100);
  if (!open_device()) return g_open;
  g_open = true;
  return true;
}

void audio_close() {
  if (g_device) { SDL_CloseAudioDevice(g_device); g_device = 0; }
  if (g_wav) {
    std::fseek(g_wav, 0, SEEK_SET);
    wav_header(g_wav, g_wav_bytes);
    std::fclose(g_wav); g_wav = nullptr;
  }
  g_open = false;
}

static void push_ordered(const uint8_t* samples, size_t bytes, bool little_endian) {
  if (!g_open || g_muted.load(std::memory_order_relaxed)) return;
  for (size_t off = 0; off + BLOCK_BYTES <= bytes; off += BLOCK_BYTES) {
    int16_t converted[BLOCK_BYTES / 2];
    const uint8_t* src = samples + off;
    for (int i = 0; i < BLOCK_BYTES / 4; ++i) {
      const int16_t r = little_endian ? (int16_t)(src[i * 4] | (src[i * 4 + 1] << 8)) : (int16_t)((src[i * 4] << 8) | src[i * 4 + 1]);
      const int16_t l = little_endian ? (int16_t)(src[i * 4 + 2] | (src[i * 4 + 3] << 8)) : (int16_t)((src[i * 4 + 2] << 8) | src[i * 4 + 3]);
      converted[i * 2] = l; converted[i * 2 + 1] = r;
    }
    if (g_wav) { std::fwrite(converted, 1, BLOCK_BYTES, g_wav); g_wav_bytes += BLOCK_BYTES; }
    constexpr size_t frames = BLOCK_BYTES / 4;
    if (!g_device) { g_frames += frames; continue; }
    const uint64_t write = g_ring_write.load(std::memory_order_relaxed);
    if (write + frames - g_ring_read.load(std::memory_order_acquire) > RING_FRAMES - 4) { ++g_dropped; continue; }
    for (size_t i = 0; i < frames; ++i) {
      g_ring[(write + i) & RING_MASK][0] = converted[i * 2];
      g_ring[(write + i) & RING_MASK][1] = converted[i * 2 + 1];
    }
    g_ring_write.store(write + frames, std::memory_order_release);
    g_frames += frames;
  }
}
void audio_push(const uint8_t* be_samples, size_t bytes) { push_ordered(be_samples, bytes, false); }
void audio_push_native(const uint8_t* le_samples, size_t bytes) { push_ordered(le_samples, bytes, true); }

void audio_trace_event(const char* name) {
  if (!g_trace) return;
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  std::fprintf(g_trace, "%s %lld\n", name, (long long)now_ns());
  std::fflush(g_trace);
}
bool audio_tracing() { return g_trace != nullptr; }
uint64_t audio_pushed_frames() { return g_frames.load(); }
void audio_ui_sound(int kind) { g_ui_sound_request.store(kind); }
uint64_t audio_dropped_blocks() { return g_dropped.load(); }
uint64_t audio_underruns(uint64_t* silent_ms) {
  if (silent_ms) *silent_ms = g_underrun_frames.load() * 1000 / std::max<uint32_t>(g_device_rate, 1);
  return g_underruns.load();
}
void audio_rate_range(double* low, double* high) { if (low) *low = g_rate_min.load(); if (high) *high = g_rate_max.load(); }
uint32_t audio_buffered_ms() { return (uint32_t)((g_ring_write.load() - g_ring_read.load()) * 1000 / SAMPLE_RATE); }

}  // namespace host
