// Host audio output: 32 kHz 16-bit stereo blocks from the game's AI DMA. WASAPI shared mode uses
// compatible 32 kHz output in Safe mode, or the endpoint format and minimum supported period in
// Low latency mode. ASIO (mode 3, opt-in) drives audio interfaces directly. WinMM waveOut is the fallback. Optional dumps of source and output samples.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include "audio.h"
#include "host.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // music is the game's own stream: nothing is mixed in here
#else
#include "jukebox.h"
#endif
#include "audio_buffer_policy.h"
#include "audio_sample_conversion.h"
#include <future>
#include "asio/asiosys.h"
#include "asio/asio.h"
#include "asio/asiodrivers.h"
extern AsioDrivers* asioDrivers;   // asio/asiodrivers.cpp

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace host {
namespace {
constexpr int SAMPLE_RATE = 32000;
constexpr int BLOCK_BYTES = 640;      // one 5 ms AI DMA frame: 160 stereo samples
constexpr int BLOCKS = 24;            // 120 ms of queue; more than that is dropped (fast mode)
std::atomic<int> g_volume{0};
std::atomic<bool> g_muted{false};     // audio_set_muted: the game's blocks are not queued
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

// ---- WASAPI: single-producer single-consumer ring, written by the simulation and read by an
// event-driven thread. No lock: the audio thread must never wait on the simulation thread.
//
// The two run off different clocks (the game's timebase against the sound card's crystal), so the
// consumer resamples with a ratio nudged by how full the ring is, the way Dolphin's mixer does.
// Without that the ring slowly fills or empties until it drops a block or runs dry, which is heard
// as a click or a gap.
constexpr size_t RING_FRAMES = 8192;                            // 256 ms, power of two
constexpr size_t RING_MASK = RING_FRAMES - 1;
// The player's software buffer target; Safe mode can raise it after short underruns.
size_t g_target_frames = SAMPLE_RATE * 40 / 1000;
bool g_priming = true;                                          // fill the ring before the first sample goes out
constexpr double MAX_RATE_SHIFT = 0.002;                        // bounded clock correction (3.5 cents); this is not bit-exact playback
constexpr double MAX_SKEW = 0.001;                              // learned steady clock skew, at most 1000 ppm
double g_skew = 0.0;                                            // consumer only: learned skew between the game's and the device's clocks
int16_t g_ring[RING_FRAMES][2];
std::atomic<uint64_t> g_ring_write{0}, g_ring_read{0};          // frame counters, never wrap in practice
double g_ring_phase = 0.0;                                      // consumer only: position inside the current frame
double g_rate = 1.0;                                            // consumer only: input frames consumed per output frame
double g_fill_average = 0.0;                                    // consumer only: slow average of the fill level
int16_t g_last_output[2] = {0, 0};                              // held through a starved moment instead of silence
std::atomic<uint64_t> g_underruns{0}, g_underrun_frames{0};
std::atomic<double> g_rate_min{1.0}, g_rate_max{1.0};
IAudioClient* g_client = nullptr;
IAudioRenderClient* g_render = nullptr;
HANDLE g_event = nullptr;
std::thread g_thread;
std::atomic<bool> g_running{false};
UINT32 g_buffer_frames = 0;
uint32_t g_device_rate = SAMPLE_RATE;
uint16_t g_device_channels = 2;
bool g_device_float = false;
bool g_device_int32 = false;   // 24-bit (in 32) or 32-bit integer endpoint samples
bool g_device_int24 = false;   // packed 24-bit endpoint samples
// Low latency: the device is kept about one and a half engine periods ahead (not its whole buffer),
// and the ring holds one simulation frame of audio (the game mixes a frame's worth in one burst)
// plus a small margin. A starved moment grows the margin a little, once per gap, up to a cap: a PC
// that cannot keep up settles on a slightly larger buffer by itself instead of crackling.
UINT32 g_period_frames = 0;                                     // the audio engine's period, our rate
UINT32 g_device_target = 0;                                     // frames kept queued in the device
// Shared mode writes each game block to the device as soon as it arrives (the push wakes the output
// thread), so the ring and the device queue are one buffer with one margin. Until 09-29 blocks
// waited in the ring for the engine's next 10 ms event, which needed a whole period there on top of
// the device queue: key press to sound measured 75-83 ms, against 66 ms for 999sian.
// MELEE_AUDIO_TWO_STAGE=1 runs the old path for comparison; exclusive mode always uses it.
bool g_direct = false;
bool g_remember = true;                                         // Auto saves what it learned (not in hidden test runs)
std::atomic<double> g_speed{1.0};                               // the game's speed against real time (online time sync)
std::atomic<bool> g_gameplay{false};                            // a match is running (Auto grows only for its gaps)
HANDLE g_push_event = nullptr;                                  // set by audio_push after each block
HANDLE g_take_timer = nullptr;                                  // fires just before the engine's next take
std::atomic<uint32_t> g_margin_grows{0};
std::atomic<int> g_requested_mode{0}, g_requested_buffer_ms{40};
std::atomic<bool> g_buffering_changed{false};
std::atomic<uint32_t> g_target_ms{40}, g_device_queue_ms{0}, g_engine_period_us{0};
AudioBufferPolicy g_buffer_policy;
std::string g_requested_device;          // endpoint id (UTF-8); empty = the Windows default device
std::string g_open_device_id;            // the endpoint actually opened
bool g_exclusive = false;                // the device is held in WASAPI exclusive mode
constexpr int AUTO_FLOOR_MS = 10, AUTO_START_MS = 40;   // no memory yet: start safe, settle down while clean
// The smallest ring that never runs dry by design: the device takes one engine period at each event
// and the game hands over one 5 ms block at a time, plus 2 ms of scheduling slack.
uint32_t g_floor_ms = AUTO_FLOOR_MS;
std::atomic<int64_t> g_last_push_qpc{0};   // when the game last handed over a block
std::atomic<uint32_t> g_trims{0};          // surplus cut from the ring (after a stall), counted for the log
int64_t qpc_now() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
int64_t qpc_hz() { static const int64_t f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q.QuadPart; }(); return f; }

// MELEE_AUDIO_TRACE=<file>: where the time from a key press to its sound goes (latency work, 09-29).
// Lines are "<event> <QPC> ...": "input" when the game's pad read first shows a new press, "block"
// when the game hands over the first loud block after silence (with the ring position of its first
// loud sample), "write" when the output thread writes that sample to the device (with the device
// queue ahead of it). The key press and the loopback onset come from audio_latency_probe.
FILE* g_trace = [] {
  const char* path = std::getenv("MELEE_AUDIO_TRACE");
  FILE* f = path && *path ? std::fopen(path, "w") : nullptr;
  if (f) std::fprintf(f, "qpc_hz %lld\n", (long long)qpc_hz());
  return f;
}();
std::mutex g_trace_mutex;
std::atomic<uint64_t> g_trace_onset{UINT64_MAX};   // ring frame of the loud sample not yet written
void trace_line(const char* text) {
  if (!g_trace) return;
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  std::fputs(text, g_trace);
  std::fflush(g_trace);
}

std::string to_utf8(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring to_wide(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

// Auto mode remembers, per output device, the buffer it settled on, so the next session starts
// there instead of relearning it through audible gaps. File: %USERPROFILE%\.melee-unlocked\audio-auto-v3.txt,
// one "<ms> <endpoint id>" line per device. v3 holds the whole queue (the direct path); v2 held the
// ring alone, before the device queue, and stays with the two-stage path.
std::string auto_memory_path() {
  const char* home = std::getenv("USERPROFILE");
  return home ? std::string(home) + (g_direct ? "\\.melee-unlocked\\audio-auto-v3.txt" : "\\.melee-unlocked\\audio-auto-v2.txt")
              : std::string();
}
int auto_memory_load(const std::string& device) {
  const std::string path = auto_memory_path();
  FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
  if (!f) return 0;
  char line[1024];
  int found = 0;
  while (std::fgets(line, sizeof line, f)) {
    int ms = 0; char id[900] = {};
    if (std::sscanf(line, "%d %899[^\r\n]", &ms, id) == 2 && device == id) found = ms;
  }
  std::fclose(f);
  return found;
}
void auto_memory_save(const std::string& device, int ms) {
  const std::string path = auto_memory_path();
  if (path.empty() || device.empty()) return;
  std::vector<std::string> keep;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
      int old = 0; char id[900] = {};
      if (std::sscanf(line, "%d %899[^\r\n]", &old, id) == 2 && device != id) keep.push_back(std::to_string(old) + " " + id);
    }
    std::fclose(f);
  }
  CreateDirectoryA((std::string(std::getenv("USERPROFILE")) + "\\.melee-unlocked").c_str(), nullptr);
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    for (const auto& k : keep) std::fprintf(f, "%s\n", k.c_str());
    std::fprintf(f, "%d %s\n", ms, device.c_str());
    std::fclose(f);
  }
}

void wav_header(FILE* f, uint32_t data_bytes, uint32_t rate = SAMPLE_RATE) {
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(data_bytes);
}

// Catmull-Rom through four consecutive samples: smooth enough that a continuously varying rate
// introduces no audible artefacts (Dolphin uses the same shape).
inline double cubic(double a, double b, double c, double d, double t) {
  return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}

// Band-limited output conversion: a 48-tap Kaiser-windowed sinc (beta 6) with 512 phases, cut at 96%
// of the lower Nyquist. Measured on the continuous kernel: flat to 14 kHz (0.00 dB), -2.5 dB at
// 15 kHz, images above 16.5 kHz at -44 dB and above 17 kHz at -65 dB. Catmull-Rom (the previous
// stage, and Dolphin's) is -3.9 dB at 14 kHz with images at -7 dB. Delay: 24 input samples
// (0.75 ms). MELEE_AUDIO_CUBIC=1 switches back for comparison.
constexpr int SINC_TAPS = 48, SINC_PHASES = 512;
struct SincTable {
  uint32_t device_rate = 0;
  std::vector<float> k;   // (SINC_PHASES + 1) x SINC_TAPS
  void build(uint32_t rate) {
    device_rate = rate;
    k.assign((SINC_PHASES + 1) * SINC_TAPS, 0.0f);
    const double cutoff = 0.5 * 0.96 * std::min(1.0, (double)rate / SAMPLE_RATE);   // cycles per input sample
    const double beta = 6.0;
    auto bessel0 = [](double x) { double s = 1, t = 1; for (int i = 1; i < 30; ++i) { t *= (x / (2 * i)) * (x / (2 * i)); s += t; } return s; };
    const double denom = bessel0(beta);
    for (int p = 0; p <= SINC_PHASES; ++p) {
      const double frac = (double)p / SINC_PHASES;
      double sum = 0;
      for (int j = 0; j < SINC_TAPS; ++j) {
        const double x = (double)(j - (SINC_TAPS / 2 - 1)) - frac;   // taps at read-23 .. read+24
        const double r = x / (SINC_TAPS / 2);
        const double w = std::abs(r) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - r * r)) / denom;
        const double s = x == 0.0 ? 2 * cutoff : std::sin(2 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
        k[p * SINC_TAPS + j] = (float)(s * w);
        sum += s * w;
      }
      for (int j = 0; j < SINC_TAPS; ++j) k[p * SINC_TAPS + j] = (float)(k[p * SINC_TAPS + j] / sum);   // unity DC gain
    }
  }
} g_sinc;   // output thread only

// ---- settings menu ticks ----
// A decaying sine: short and quiet so it reads as UI feedback, never over the game's own sound.
std::atomic<int> g_ui_sound_request{0};
struct UiTick { int remaining = 0, total = 0; double step = 0, phase = 0, level = 0; } g_ui_tick;  // output thread only
void mix_ui_sound(int16_t* out, size_t frames, int volume, uint32_t rate = SAMPLE_RATE) {
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

// Test-only (MELEE_AUDIO_DEVICE_DUMP=<file.wav>): record exactly what the output thread renders,
// after the ring resampler, the jukebox and the UI ticks, at full volume, and send silence to the
// device instead. It measures what a player hears (pitch tracking, gaps, music path) without
// making a sound on the machine running the test. A second file (<file>.rate.csv) logs the
// resampling ratio, the ring fill and the starved frames of every device callback.
FILE* g_device_dump = nullptr;
FILE* g_device_rate_log = nullptr;
uint32_t g_device_dump_bytes = 0;

void wasapi_thread() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  DWORD task_index = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
  if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
  uint64_t gap_frames = 0;
  uint32_t recovery_left = 0;
  double recovery_offset[2] = {};
  bool interrupted = false;
  bool started = false;
  std::vector<int16_t> samples((size_t)g_buffer_frames * 2);
  // The device-dump log's time column: QPC, so callbacks 5-10 ms apart are not rounded to the
  // 15.6 ms system tick.
  LARGE_INTEGER qpc_freq{}, qpc_start{};
  QueryPerformanceFrequency(&qpc_freq); QueryPerformanceCounter(&qpc_start);
  // Direct path: the engine takes one period at each of its events. A timer set for 2 ms before the
  // next take checks the device still holds that much; only then is a missing block a gap (the
  // stream fades out), so blocks that are merely a little late never cost a click.
  HANDLE waits[3] = {g_event, g_push_event, g_take_timer};
  const DWORD wait_count = g_direct && g_push_event && g_take_timer ? 3 : 1;
  const LONGLONG take_check_100ns = -(LONGLONG)(g_period_frames * 10000000ull / g_device_rate) + 2 * 10000;
  int64_t last_update = qpc_now();
  double held_level = -1.0;
  while (g_running.load()) {
    const DWORD woke = WaitForMultipleObjects(wait_count, waits, FALSE, 200);
    static int timeouts_logged = 0;
    if (woke == WAIT_TIMEOUT && started && timeouts_logged < 3) { ++timeouts_logged; log("audio: no device event for 200 ms (%s)", g_exclusive ? "exclusive" : "shared"); }
    const bool engine = woke == WAIT_OBJECT_0;
    const bool take_check = woke == WAIT_OBJECT_0 + 2;
    if (!engine && woke != WAIT_OBJECT_0 + 1 && !take_check) continue;
    if (engine && wait_count == 3) {
      LARGE_INTEGER due; due.QuadPart = std::min<LONGLONG>(take_check_100ns, -1);
      SetWaitableTimer(g_take_timer, &due, 0, nullptr, nullptr, FALSE);
    }
    // Auto and Exclusive manage their own buffer; a slider change only matters for the fixed modes.
    if (g_buffering_changed.exchange(false) && g_requested_mode.load() == 1 && !g_exclusive) {
      const size_t previous_target = g_target_frames;
      g_buffer_policy.configure(g_requested_mode.load(), std::max<int>(g_requested_buffer_ms.load(), (int)g_floor_ms));
      g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
      if (!g_direct) g_device_target = std::min(g_buffer_frames, g_period_frames + g_period_frames / 2);
      g_target_ms.store(g_buffer_policy.target_ms);
      g_device_queue_ms.store(g_direct ? 0 : g_device_target * 1000 / g_device_rate);
      if (g_target_frames > previous_target) g_priming = true;
      gap_frames = 0;
    }
    UINT32 padding = 0;
    // Exclusive, event driven: each event asks for the whole buffer, and the padding is not meaningful
    // (the MOTU reports the buffer as full, so waiting for room wrote nothing at all: silence and a full
    // ring until 09-30). Other wake-ups have nothing to do in this mode.
    if (g_exclusive) { if (!engine) continue; }
    else if (FAILED(g_client->GetCurrentPadding(&padding))) continue;
    const UINT32 fill_to = g_device_target ? std::min(g_device_target, g_buffer_frames) : g_buffer_frames;
    // Two-stage path: top the device up to its queue target at each engine event.
    const UINT32 top_up = fill_to > padding ? fill_to - padding : 0;
    if (!g_direct && !top_up) continue;
    int16_t* out = samples.data();
    const int volume = g_device_dump ? 100 : g_volume.load();
    uint64_t read = g_ring_read.load(std::memory_order_relaxed);

    // Rate control: steer the ring towards the target fill instead of letting it drift into an
    // overflow (a dropped block) or a gap. On the direct path the audio still queued in the device
    // counts too: the two together are the delay.
    const uint64_t ring_buffered = g_ring_write.load(std::memory_order_acquire) - read;
    const uint64_t buffered = ring_buffered + (g_direct ? (uint64_t)padding * SAMPLE_RATE / g_device_rate : 0);
    if (g_priming) {
      if (buffered >= g_target_frames) {
        g_priming = false;
        g_fill_average = (double)buffered;
        g_rate = g_speed.load(std::memory_order_relaxed) * (1.0 + g_skew);   // resume at the learned clock skew: no pitch dip after a gap
      }
    }
    // Clock tracking. The ring fill moves only with the difference between the game's clock (blocks
    // arrive at the simulation's real-time pace) and the sound card's crystal: tens to hundreds of
    // ppm. Steer on a one-second average of the fill: a proportional term that removes an error over
    // about four seconds, plus a slow integral that learns the steady skew between the two clocks.
    // The correction is capped at 0.2% (3.5 cents, inaudible). Until 09-29 a fast loop capped at
    // 1.5% oscillated continuously and reached its cap in every measured run (a +-20 cent wobble).
    const int64_t now = qpc_now();
    const double dt = g_direct ? std::clamp((double)(now - last_update) / (double)qpc_hz(), 0.0, 0.1)
                               : (double)top_up / (double)g_device_rate;
    last_update = now;
    // Comparison switch: MELEE_AUDIO_OLD_CLOCK=1 runs the pre-09-29 loop (fast, capped at 1.5%).
    static const bool old_clock = [] { const char* v = std::getenv("MELEE_AUDIO_OLD_CLOCK"); return v && *v == '1'; }();
    if (g_fill_average == 0.0) g_fill_average = (double)buffered;
    // Direct path: wake-ups come at the moments the level steps (a block in, a period taken), so the
    // level measured last time is what held since then: weight that one by the time it held.
    const double level = g_direct && held_level >= 0.0 ? held_level : (double)buffered;
    held_level = (double)buffered;
    if (old_clock) {
      g_fill_average += ((double)buffered - g_fill_average) * 0.02;
      const double error = (g_fill_average - (double)g_target_frames) / (double)g_target_frames;
      const double target_rate = 1.0 + std::clamp(error * 0.25, -0.015, 0.015);
      g_rate += (target_rate - g_rate) * 0.05;
    } else {
      g_fill_average += (level - g_fill_average) * std::min(1.0, dt / 1.0);
      const double error_s = (g_fill_average - (double)g_target_frames) / (double)SAMPLE_RATE;
      if (!g_priming) g_skew = std::clamp(g_skew + error_s * dt / (4.0 * 30.0), -MAX_SKEW, MAX_SKEW);
      // The game's own speed (online time sync) is followed directly; the capped correction only
      // tracks the two clocks on top of it.
      const double target_rate = g_speed.load(std::memory_order_relaxed) *
                                 (1.0 + std::clamp(error_s / 4.0 + g_skew, -MAX_RATE_SHIFT, MAX_RATE_SHIFT));
      g_rate += (target_rate - g_rate) * std::min(1.0, dt / 0.2);   // eased over ~200 ms, so the pitch never steps
    }
    if (g_rate < g_rate_min.load()) g_rate_min.store(g_rate);
    if (g_rate > g_rate_max.load()) g_rate_max.store(g_rate);

    // Surplus: after a stall the game resumes and the ring holds more than the target. The clock
    // loop is deliberately gentle (pitch), so it would carry that extra delay for many seconds; cut
    // it at once instead, crossfading from the last sample over 5 ms (the resume path below).
    bool trim_now = false;
    if (!g_priming) {
      // Direct: the level swings a whole engine period every cycle (the engine takes a period at
      // once, the game adds 5 ms at a time), so only what is left beyond that swing is surplus.
      // A margin inside the swing cut real audio 29 times in a 60 s match (09-29).
      const uint64_t swing = g_direct ? (uint64_t)g_period_frames * SAMPLE_RATE / g_device_rate : 0;
      const uint64_t margin = std::max<uint64_t>(SAMPLE_RATE * 8 / 1000 + swing, g_target_frames / 2);
      if (buffered > g_target_frames + margin) {
        // Only the ring can be cut; the direct path keeps any surplus there (see device_cap).
        const uint64_t cut = std::min<uint64_t>(buffered - g_target_frames, ring_buffered);
        if (cut && g_device_rate_log)
          std::fprintf(g_device_rate_log, "%.3f,0,%.6f,%llu,0,%llu,%u,%zu,%u,x\n",
                       (double)(qpc_now() - qpc_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart, g_rate,
                       (unsigned long long)buffered, (unsigned long long)ring_buffered, padding, g_target_frames, g_trims.load());
        if (cut) {
          // Applied here, not with the next write: a wake-up with a full device writes nothing and
          // returned before storing the read position, so the cut was lost and counted again at
          // every wake-up, with the level average reset to the target each time. After a stalled
          // frame and its catch-up burst in an online match that held the queue about 20 ms high
          // for the rest of the match (09-29, 5,000 counted cuts in 40 s).
          read += cut;
          g_ring_read.store(read, std::memory_order_release);
          if (started) interrupted = true;   // the next sample written crossfades from the last one
          g_fill_average = (double)g_target_frames;
          held_level = (double)(buffered - cut);
          trim_now = true;
          g_trims.fetch_add(1);
        }
      }
    }

    uint32_t starved = 0;
    const bool priming_at_start = g_priming;
    const double fade = std::pow(0.98, (double)SAMPLE_RATE / g_device_rate);
    static const bool use_cubic = [] { const char* v = std::getenv("MELEE_AUDIO_CUBIC"); return v && *v == '1'; }();
    if (!use_cubic && g_sinc.device_rate != g_device_rate) g_sinc.build(g_device_rate);
    const uint64_t lookahead = use_cubic ? 4 : SINC_TAPS / 2 + 1;

    // How much to write. Two-stage: the top-up, whatever the ring holds. Direct: every output frame
    // the ring can supply now, but no more than the target plus one block in the device, so a burst
    // after a stall stays in the ring where the surplus cut above can reach it. At the check before
    // the engine's take, a device short of one period is a real gap: fade out to fill it.
    UINT32 want = top_up, produce = top_up;
    if (g_direct) {
      const double step = g_rate * SAMPLE_RATE / g_device_rate;
      const double usable = (double)(g_ring_write.load(std::memory_order_acquire) - read) - (double)lookahead - g_ring_phase;
      const uint64_t device_cap = std::min<uint64_t>(g_buffer_frames,
          (g_target_frames + SAMPLE_RATE * 6 / 1000) * (uint64_t)g_device_rate / SAMPLE_RATE);
      produce = g_priming || usable <= 0.0 ? 0 : (UINT32)(usable / step);
      produce = (UINT32)std::min<uint64_t>(produce, device_cap > padding ? device_cap - padding : 0);
      want = produce;
      if (take_check && started && padding + produce < g_period_frames)
        want = std::min(g_buffer_frames - padding, g_period_frames - padding);
      if (!want) continue;
    }
    BYTE* dst = nullptr;
    if (const HRESULT got = g_render->GetBuffer(want, &dst); FAILED(got)) {
      static int failures_logged = 0;
      if (failures_logged < 3) { ++failures_logged; log("audio: GetBuffer(%u) failed %08lX (padding %u of %u)", want, (unsigned long)got, padding, g_buffer_frames); }
      continue;
    }
    // The volume glides over about 10 ms instead of stepping once per callback (a zipper click).
    static double vol_now = -1.0;
    if (vol_now < 0.0) vol_now = volume;
    const double vol_glide = std::min(1.0, 100.0 / g_device_rate);
    if (trim_now && started) interrupted = true;   // the next sample crossfades from the last one
    const uint64_t read_before = read;
    for (UINT32 i = 0; i < want; ++i) {
      vol_now += (volume - vol_now) * vol_glide;
      const double vg = vol_now / 100.0;
      const uint64_t write = g_ring_write.load(std::memory_order_acquire);
      if (i >= produce || g_priming || write - read < lookahead) {
        // Fade an interrupted stream towards silence, then crossfade its return. Holding a
        // nonzero sample indefinitely produces a step (a click) when the stream returns.
        g_last_output[0] = (int16_t)(g_last_output[0] * fade);
        g_last_output[1] = (int16_t)(g_last_output[1] * fade);
        out[i * 2] = (int16_t)(g_last_output[0] * vg);
        out[i * 2 + 1] = (int16_t)(g_last_output[1] * vg);
        interrupted = true;
        if (started && !host::exit_requested()) ++starved;
        continue;
      }
      const bool resuming = interrupted;
      started = true;
      if (resuming) { recovery_left = g_device_rate * 5 / 1000; interrupted = false; }
      const float* kern = nullptr;
      if (!use_cubic) {
        const double pos = g_ring_phase * SINC_PHASES;
        kern = &g_sinc.k[std::min((int)(pos + 0.5), SINC_PHASES) * SINC_TAPS];
      }
      for (int channel = 0; channel < 2; ++channel) {
        double v;
        if (kern) {
          v = 0.0;
          for (int j = 0; j < SINC_TAPS; ++j)
            v += kern[j] * g_ring[(read + j - (SINC_TAPS / 2 - 1)) & RING_MASK][channel];
        } else {
          const double a = g_ring[(read - 1) & RING_MASK][channel];
          const double b = g_ring[read & RING_MASK][channel];
          const double c = g_ring[(read + 1) & RING_MASK][channel];
          const double d = g_ring[(read + 2) & RING_MASK][channel];
          v = cubic(a, b, c, d, g_ring_phase);
        }
        if (resuming) recovery_offset[channel] = g_last_output[channel] - v;
        if (recovery_left) v += recovery_offset[channel] * recovery_left / (g_device_rate * 5.0 / 1000);
        g_last_output[channel] = (int16_t)std::clamp(v, -32768.0, 32767.0);
        out[i * 2 + channel] = (int16_t)(g_last_output[channel] * vg);
      }
      if (recovery_left) --recovery_left;
      g_ring_phase += g_rate * SAMPLE_RATE / g_device_rate;
      while (g_ring_phase >= 1.0) { g_ring_phase -= 1.0; ++read; }
    }
    g_ring_read.store(read, std::memory_order_release);
    if (g_trace && read > g_trace_onset.load()) {
      // The traced sample went out in this write: the device frames queued ahead of it are what was
      // already there plus the part of this write before it.
      const uint64_t onset = g_trace_onset.exchange(UINT64_MAX);
      const double ahead = padding + (onset > read_before ? (double)(onset - read_before) * g_device_rate / (SAMPLE_RATE * g_rate) : 0.0);
      char line[96];
      std::snprintf(line, sizeof line, "write %lld %.0f %u\n", (long long)qpc_now(), ahead, g_device_rate);
      trace_line(line);
    }
    // Safe mode actually refills after starvation, rather than merely increasing a target that
    // clock tracking would take seconds to reach. Low latency keeps the player's fixed target.
    // A gap because the game itself stopped handing over blocks (a load, a hitch) is not the output
    // buffer's fault: a bigger buffer would not have filled either. Only a gap while the game kept
    // its pace means the buffer is too small for this PC.
    const bool game_stalled = (qpc_now() - g_last_push_qpc.load(std::memory_order_relaxed)) * 1000 / qpc_hz() >
                              (int64_t)(10 + g_period_frames * 1000 / std::max<uint32_t>(1, g_device_rate));
    if (starved) {
      g_underruns.fetch_add(1); g_underrun_frames.fetch_add(starved);
      gap_frames += starved;
      // Only gaps during a match grow Auto's buffer: in menus and while a scene loads the game
      // hitches for 15 to 45 ms at times, which says nothing about what a match needs, and growing
      // for them cost the next match 10 to 15 ms of delay that took minutes to settle (09-29).
      const bool gameplay = g_gameplay.load(std::memory_order_relaxed);
      if (game_stalled) {
        g_priming = true;   // the game stopped: refill to the target, then play on at the same latency
        gap_frames = 0;
      } else if (g_buffer_policy.adaptive && !priming_at_start && (gameplay || !g_direct)) {
        if (g_buffer_policy.gap(gap_frames, g_device_rate)) g_margin_grows.fetch_add(1);
        g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
        // The direct path plays on and the clock loop brings the level up to the new target. A
        // refill first turned a shortfall of a fraction of a millisecond (a block landing in the
        // last 2 ms before the engine's take) into a 5 to 10 ms fade (09-29).
        if (!g_direct) g_priming = true;
        gap_frames = 0;
      }
    } else {
      gap_frames = 0;
      if (!g_priming) g_buffer_policy.clean(want, g_device_rate);
      g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
    }
    g_target_ms.store(g_buffer_policy.target_ms);
    // Music follows the same clock as the game's sound: the device consumes the stream g_rate times
    // faster than nominal, so both stay in step instead of drifting against each other.
    slippi::jukebox::mix(out, want, volume / 100.0, (double)g_device_rate / g_rate);
    mix_ui_sound(out, want, volume, g_device_rate);
    if (g_device_dump) {
      std::fwrite(out, 4, want, g_device_dump);
      g_device_dump_bytes += want * 4;
      if (g_device_rate_log) {
        LARGE_INTEGER qpc_now{}; QueryPerformanceCounter(&qpc_now);
        const double ms = qpc_freq.QuadPart ? (double)(qpc_now.QuadPart - qpc_start.QuadPart) * 1000.0 / (double)qpc_freq.QuadPart : 0.0;
        std::fprintf(g_device_rate_log, "%.3f,%u,%.6f,%llu,%u,%llu,%u,%zu,%u,%c\n", ms, want, g_rate, (unsigned long long)buffered,
                     starved, (unsigned long long)ring_buffered, padding, g_target_frames, g_trims.load(),
                     engine ? 'e' : take_check ? 't' : 'p');
      }
      g_render->ReleaseBuffer(want, AUDCLNT_BUFFERFLAGS_SILENT);
      continue;
    }
    for (UINT32 i = 0; i < want; ++i) for (uint16_t ch = 0; ch < g_device_channels; ++ch) {
      const int16_t value = ch < 2 ? out[i * 2 + ch] : 0;
      const size_t index = (size_t)i * g_device_channels + ch;
      if (g_device_float) ((float*)dst)[index] = value / 32768.0f;
      else if (g_device_int32) ((int32_t*)dst)[index] = (int32_t)((uint32_t)(uint16_t)value << 16);
      else if (g_device_int24) audio_store_pcm24_le(dst + index * 3, value);
      else ((int16_t*)dst)[index] = value;
    }
    g_render->ReleaseBuffer(want, 0);
  }
  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
  CoUninitialize();
}

bool wasapi_open() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { log("audio: CoInitialize failed (%08X)", (unsigned)hr); return false; }
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) return false;
  IMMDevice* device = nullptr;
  hr = E_FAIL;
  if (!g_requested_device.empty()) {
    hr = enumerator->GetDevice(to_wide(g_requested_device).c_str(), &device);
    if (FAILED(hr)) log("audio: chosen output device not found (%08X); using the Windows default", (unsigned)hr);
  }
  if (FAILED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  enumerator->Release();
  if (FAILED(hr)) { log("audio: no default render device (%08X)", (unsigned)hr); return false; }
  {
    LPWSTR id = nullptr;
    g_open_device_id.clear();
    if (SUCCEEDED(device->GetId(&id))) { g_open_device_id = to_utf8(id); CoTaskMemFree(id); }
  }
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client);
  if (FAILED(hr)) { device->Release(); log("audio: IAudioClient activate failed (%08X)", (unsigned)hr); return false; }
  WAVEFORMATEX fmt{};
  fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = SAMPLE_RATE;
  fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = SAMPLE_RATE * 4;
  const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  UINT32 low_period = 0;
  g_device_rate = SAMPLE_RATE; g_device_channels = 2; g_device_float = false; g_device_int32 = false; g_device_int24 = false;
  g_exclusive = false;
  hr = E_FAIL;
  // Exclusive (opt-in): the game owns the device, event driven at its minimum period. Other apps
  // (and routing tools such as virtual cables) cannot play through it while the game runs.
  if (g_requested_mode.load() == 2 && !std::getenv("MELEE_AUDIO_DEVICE_DUMP")) {
    REFERENCE_TIME def_period = 0, min_period = 0;
    g_client->GetDevicePeriod(&def_period, &min_period);
    const DWORD rates[] = {48000, 44100, 96000, 32000};
    HRESULT refused = S_OK;   // the last answer Windows gave, for the log
    for (DWORD rate : rates) {
      // PCM16, 24-bit in 32, 32-bit integer, float32: audio interfaces often take only the wider ones.
      for (int kind = 0; kind < 4; ++kind) {
        const int bits = kind == 0 ? 16 : kind == 1 ? 24 : 32;
        WAVEFORMATEXTENSIBLE x{};
        x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; x.Format.nChannels = 2; x.Format.nSamplesPerSec = rate;
        x.Format.wBitsPerSample = (WORD)(bits == 24 ? 32 : bits); x.Samples.wValidBitsPerSample = (WORD)bits;
        x.Format.nBlockAlign = (WORD)(2 * x.Format.wBitsPerSample / 8); x.Format.nAvgBytesPerSec = rate * x.Format.nBlockAlign;
        x.Format.cbSize = 22; x.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        x.SubFormat = kind == 3 ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
        if (const HRESULT ok = g_client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&x, nullptr); ok != S_OK) { refused = ok; continue; }
        REFERENCE_TIME period = min_period > 0 ? min_period : def_period;
        hr = g_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*)&x, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
          UINT32 frames = 0; g_client->GetBufferSize(&frames);
          g_client->Release(); g_client = nullptr;
          if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client))) break;
          period = (REFERENCE_TIME)(10000000.0 * frames / rate + 0.5);
          hr = g_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*)&x, nullptr);
        }
        if (SUCCEEDED(hr)) {
          g_exclusive = true; g_device_rate = rate; g_device_channels = 2; g_device_float = kind == 3; g_device_int32 = kind == 1 || kind == 2;
          low_period = (UINT32)(period * rate / 10000000);
          break;
        }
        // A failed Initialize leaves the client unusable: start again with a fresh one.
        refused = hr;
        g_client->Release(); g_client = nullptr;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client))) break;
        hr = E_FAIL;
      }
      if (g_exclusive || !g_client) break;
    }
    if (!g_exclusive)
      log("audio: exclusive mode unavailable on this device (%s, 0x%08lX); using shared low latency",
          refused == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED ? "Windows setting: allow applications to take exclusive control is off"
          : refused == AUDCLNT_E_DEVICE_IN_USE ? "another app holds it" : "no format accepted", (unsigned long)refused);
  }
  if (!g_exclusive && g_client) {
    // Ask every supported format before choosing a stream: a driver's shortest shared period
    // can differ for float32, PCM16 and PCM24. Keep the mix format first on equal periods.
    struct SharedFormat { WAVEFORMATEXTENSIBLE format{}; UINT32 minimum = 0; bool floating = false, integer32 = false, packed24 = false; };
    std::vector<SharedFormat> formats;
    WAVEFORMATEX* mix = nullptr;
    if (SUCCEEDED(g_client->GetMixFormat(&mix))) {
      auto add = [&](const WAVEFORMATEX* f) {
        if (f->nChannels < 2 || f->nChannels > 8 || f->nSamplesPerSec < 32000 ||
            f->nBlockAlign != f->nChannels * (f->wBitsPerSample / 8)) return;
        const bool extended = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22;
        const bool floating = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (extended && ((const WAVEFORMATEXTENSIBLE*)f)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const bool pcm = f->wFormatTag == WAVE_FORMAT_PCM ||
            (extended && ((const WAVEFORMATEXTENSIBLE*)f)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM);
        if (!((floating && f->wBitsPerSample == 32) ||
              (pcm && (f->wBitsPerSample == 16 || f->wBitsPerSample == 24 || f->wBitsPerSample == 32)))) return;
        SharedFormat candidate;
        std::memcpy(&candidate.format, f, sizeof(WAVEFORMATEX) + (extended ? 22 : 0));
        candidate.format.Format.cbSize = extended ? 22 : 0;
        candidate.floating = floating; candidate.integer32 = pcm && f->wBitsPerSample == 32;
        candidate.packed24 = pcm && f->wBitsPerSample == 24;
        formats.push_back(candidate);
      };
      add(mix);
      // Preserve the endpoint's own channel layout while comparing 48 kHz sample encodings.
      if (mix->nChannels >= 2 && mix->nChannels <= 8) {
        for (int kind = 0; kind < 4; ++kind) {
          WAVEFORMATEXTENSIBLE f{};
          f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.cbSize = 22;
          f.Format.nChannels = mix->nChannels; f.Format.nSamplesPerSec = 48000;
          f.Format.wBitsPerSample = kind == 1 ? 16 : kind == 2 ? 24 : 32;
          f.Samples.wValidBitsPerSample = kind >= 2 ? 24 : f.Format.wBitsPerSample;
          f.Format.nBlockAlign = (WORD)(f.Format.nChannels * f.Format.wBitsPerSample / 8);
          f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * f.Format.nBlockAlign;
          f.dwChannelMask = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22
              ? ((const WAVEFORMATEXTENSIBLE*)mix)->dwChannelMask
              : mix->nChannels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
          f.SubFormat = kind == 0 ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
          add((const WAVEFORMATEX*)&f);
        }
      }
      CoTaskMemFree(mix);
    }
    IAudioClient3* query = nullptr;
    if (SUCCEEDED(g_client->QueryInterface(__uuidof(IAudioClient3), (void**)&query))) {
      for (auto& candidate : formats) {
        WAVEFORMATEX* closest = nullptr;
        auto* f = (WAVEFORMATEX*)&candidate.format;
        const HRESULT supported = g_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, f, &closest);
        if (closest) CoTaskMemFree(closest);
        UINT32 normal = 0, fundamental = 0, minimum = 0, maximum = 0;
        const HRESULT period = supported == S_OK
            ? query->GetSharedModeEnginePeriod(f, &normal, &fundamental, &minimum, &maximum) : supported;
        if (period == S_OK && minimum) candidate.minimum = minimum;
        log("audio: shared format %u Hz %u channels %s%u: support %08X, period query %08X, min %.3f ms (%u frames)",
            f->nSamplesPerSec, f->nChannels, candidate.floating ? "float" : "PCM", f->wBitsPerSample,
            (unsigned)supported, (unsigned)period, candidate.minimum * 1000.0 / f->nSamplesPerSec, candidate.minimum);
      }
      query->Release();
    }
    formats.erase(std::remove_if(formats.begin(), formats.end(), [](const SharedFormat& f) { return !f.minimum; }), formats.end());
    std::stable_sort(formats.begin(), formats.end(), [](const SharedFormat& a, const SharedFormat& b) {
      return (uint64_t)a.minimum * b.format.Format.nSamplesPerSec < (uint64_t)b.minimum * a.format.Format.nSamplesPerSec;
    });
    // Failed Initialize can leave a client unusable. Every attempted format gets a fresh client.
    for (const auto& candidate : formats) {
      g_client->Release(); g_client = nullptr;
      if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client))) break;
      IAudioClient3* client3 = nullptr;
      hr = g_client->QueryInterface(__uuidof(IAudioClient3), (void**)&client3);
      if (SUCCEEDED(hr)) {
        hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, candidate.minimum,
                                                (const WAVEFORMATEX*)&candidate.format, nullptr);
        client3->Release();
      }
      if (SUCCEEDED(hr)) {
        low_period = candidate.minimum; g_device_rate = candidate.format.Format.nSamplesPerSec;
        g_device_channels = candidate.format.Format.nChannels; g_device_float = candidate.floating;
        g_device_int32 = candidate.integer32; g_device_int24 = candidate.packed24;
        break;
      }
      log("audio: shared minimum-period stream refused format (%08X); trying the next supported format", (unsigned)hr);
    }
    if (!low_period) {
      log("audio: minimum shared-period initialization failed (%08X); using the compatible default period", (unsigned)hr);
      if (g_client) g_client->Release(); g_client = nullptr;
      hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_client);
      if (SUCCEEDED(hr)) hr = E_FAIL; // a fresh client still needs Initialize below
    }
  }
  device->Release();
  if (!g_client) return false;
  if (g_exclusive) hr = S_OK;
  // Older devices and unsupported low-period formats retain the compatible conversion path.
  if (FAILED(hr)) hr = g_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &fmt, nullptr);
  if (FAILED(hr)) hr = g_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 20 * 10000 /* 20 ms */, 0, &fmt, nullptr);
  if (FAILED(hr)) { log("audio: IAudioClient initialize failed (%08X)", (unsigned)hr); g_client->Release(); g_client = nullptr; return false; }
  g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (FAILED(g_client->SetEventHandle(g_event)) || FAILED(g_client->GetBufferSize(&g_buffer_frames)) ||
      FAILED(g_client->GetService(__uuidof(IAudioRenderClient), (void**)&g_render))) {
    log("audio: IAudioClient setup failed"); g_client->Release(); g_client = nullptr; return false;
  }
  {
    REFERENCE_TIME period = 0, minimum = 0;
    if (FAILED(g_client->GetDevicePeriod(&period, &minimum)) || period <= 0) period = 10 * 10000;
    g_period_frames = low_period ? low_period : (UINT32)std::max<long long>(32, period * g_device_rate / 10000000);
  }
  const bool auto_mode = g_requested_mode.load() == 0;
  // One queue from the game's block to the engine (shared mode), unless the comparison switch asks
  // for the two-stage path. Exclusive fills one whole device period per event, so it keeps the ring.
  g_direct = !g_exclusive && !(std::getenv("MELEE_AUDIO_TWO_STAGE") && *std::getenv("MELEE_AUDIO_TWO_STAGE") == '1');
  if (g_direct) {
    if (!g_push_event) g_push_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_take_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (!g_take_timer) g_take_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!g_push_event || !g_take_timer) g_direct = false;
  }
  // The physical floor of this device.
  // Two-stage: one engine period taken per event, one 5 ms game block arriving at a time, 2 ms of
  // slack, in the ring, with the device queue on top: 17 ms + 15 ms on a 10 ms shared-mode device.
  // Direct: the device must hold one period when the engine takes it, even with one block 5 ms
  // late, plus 2 ms of slack; the average sits about half a block below that peak and at most a
  // period below it, which leaves period + 4 ms: 14 ms in all on a 10 ms shared-mode device.
  const uint32_t period_ms = (uint32_t)((g_period_frames * 1000 + g_device_rate - 1) / g_device_rate);
  g_floor_ms = std::max<uint32_t>(AUTO_FLOOR_MS, g_direct ? period_ms + 4 : period_ms + 5 + 2);
  if (auto_mode) {
    // Auto: never below the floor; start where this device settled last time (gaps from the game's
    // own stalls no longer count, so the remembered value is the device's real need), else at the
    // floor on the direct path (it held there through a 60 s match with one gap, 09-29) or a little
    // above it on the two-stage path; grow after real gaps and settle back while clean.
    g_buffer_policy.configure(0, (int)g_floor_ms);
    const int remembered = auto_memory_load(g_open_device_id);
    g_buffer_policy.target_ms = (uint32_t)std::clamp(remembered > 0 ? remembered : (int)g_floor_ms + (g_direct ? 0 : 8),
                                                     (int)g_floor_ms, 120);
  } else {
    // Exclusive uses the device floor; Low latency keeps its explicit player target above it.
    g_buffer_policy.configure_for_output(g_requested_mode.load(), g_requested_buffer_ms.load(), (int)g_floor_ms);
  }
  // Two-stage: the device is kept one and a half engine periods ahead (enough for a late wake-up),
  // not its whole buffer; exclusive mode is one period by construction. Direct has no separate
  // device target: the queue is one.
  g_device_target = g_direct ? 0 : g_exclusive ? g_buffer_frames : std::min(g_buffer_frames, g_period_frames + g_period_frames / 2);
  g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
  g_target_ms.store(g_buffer_policy.target_ms);
  g_device_queue_ms.store(g_device_target * 1000 / g_device_rate);
  g_engine_period_us.store((uint32_t)(g_period_frames * 1000000ull / g_device_rate));
  if (g_direct)
    log("audio: %s: engine period %.1f ms, one queue from game to engine %.1f ms (device buffer %.1f ms)",
        auto_mode ? "Auto" : "Low latency", g_period_frames * 1000.0 / g_device_rate, g_target_frames * 1000.0 / SAMPLE_RATE,
        g_buffer_frames * 1000.0 / g_device_rate);
  else
    log("audio: %s: engine period %.1f ms, device queue %.1f ms, ring %.1f ms",
        g_exclusive ? "Exclusive" : auto_mode ? "Auto" : g_buffer_policy.adaptive ? "Safe" : "Low latency",
        g_period_frames * 1000.0 / g_device_rate, g_device_target * 1000.0 / g_device_rate, g_target_frames * 1000.0 / SAMPLE_RATE);
  log("audio: endpoint stream %u Hz, %u channels, %s", g_device_rate, g_device_channels, g_device_float ? "float32" : g_device_int32 ? "PCM32" : g_device_int24 ? "PCM24" : "PCM16");
  g_priming = true;
  g_fill_average = 0.0;
  if (const char* dump_env = std::getenv("MELEE_AUDIO_DEVICE_DUMP")) {
    // "{pid}" in the path becomes this process's id, so two instances (an online pair) keep apart.
    std::string dump = dump_env;
    if (const size_t at = dump.find("{pid}"); at != std::string::npos) dump.replace(at, 5, std::to_string(GetCurrentProcessId()));
    if ((g_device_dump = std::fopen(dump.c_str(), "wb")) != nullptr) {
      wav_header(g_device_dump, 0, g_device_rate);
      g_device_dump_bytes = 0;
      g_device_rate_log = std::fopen((dump + ".rate.csv").c_str(), "w");
      if (g_device_rate_log) std::fprintf(g_device_rate_log, "ms,frames,rate,buffered,starved,ring,padding,target,trims,why\n");
      log("audio: test dump of the device output to %s (the device plays silence)", dump.c_str());
    }
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
  if (g_remember && g_requested_mode.load() == 0 && !g_exclusive && !g_device_dump && !g_open_device_id.empty())
    auto_memory_save(g_open_device_id, (int)g_buffer_policy.target_ms);
  log("audio: ring ended at %.1f ms (floor %u ms, %u surplus trims, %u increases after gaps)", g_target_frames * 1000.0 / SAMPLE_RATE, g_floor_ms, g_trims.load(),
      g_margin_grows.load());
  if (g_device_dump) {
    std::fseek(g_device_dump, 0, SEEK_SET);
    wav_header(g_device_dump, g_device_dump_bytes, g_device_rate);
    std::fclose(g_device_dump); g_device_dump = nullptr;
  }
  if (g_device_rate_log) { std::fclose(g_device_rate_log); g_device_rate_log = nullptr; }
  g_client->Stop();
  if (g_render) { g_render->Release(); g_render = nullptr; }
  g_client->Release(); g_client = nullptr;
  if (g_event) { CloseHandle(g_event); g_event = nullptr; }
  // g_push_event stays open: the simulation thread may still be signalling it.
  if (g_take_timer) { CancelWaitableTimer(g_take_timer); CloseHandle(g_take_timer); g_take_timer = nullptr; }
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

// ---- ASIO (opt-in, audio mode 3). Steinberg ASIO SDK host files under its GPL Version 3 option
// (port/third_party/asio). The driver calls bufferSwitch once per buffer on its own thread; each call
// pulls exactly one buffer from the same ring the WASAPI path reads, resampled with the same sinc
// kernel and steered by the same clock loop. Outputs 1 and 2. The driver is loaded, run and released
// on one control thread (drivers are COM objects in its apartment), which also handles reset requests.
std::string g_asio_requested;                 // driver name as listed in HKLM\SOFTWARE\ASIO
int g_asio_requested_buffer = 0;              // frames; 0 = the driver's preferred size
std::atomic<bool> g_asio_running{false};      // buffers are live and bufferSwitch may run
std::thread g_asio_thread;
HANDLE g_asio_wake = nullptr;                 // control thread: stop or reset
std::atomic<bool> g_asio_stop{false}, g_asio_reset{false}, g_asio_refresh_latency{false};
long g_asio_buffer = 0, g_asio_latency_in = 0, g_asio_latency_out = 0;
std::atomic<long> g_asio_ui_buffer{0}, g_asio_ui_in{0}, g_asio_ui_out{0};
ASIOSampleType g_asio_type = ASIOSTInt32LSB;
ASIOBufferInfo g_asio_buffers[2];
bool g_asio_output_ready = false;
std::vector<int16_t> g_asio_mix;
std::string g_asio_driver_name;
// Render state (driver callback thread only).
struct AsioRender { bool interrupted = false, started = false; uint32_t recovery_left = 0; double recovery_offset[2] = {}; double vol_now = -1.0; } g_ar;

void asio_render(long index) {
  const long n = g_asio_buffer;
  int16_t* out = g_asio_mix.data();
  const int volume = g_volume.load();
  uint64_t read = g_ring_read.load(std::memory_order_relaxed);
  const uint64_t buffered = g_ring_write.load(std::memory_order_acquire) - read;
  const uint64_t pull32 = (uint64_t)n * SAMPLE_RATE / g_device_rate + 1;
  if (g_priming && buffered >= g_target_frames) {
    g_priming = false;
    g_fill_average = (double)buffered;
    g_rate = g_speed.load(std::memory_order_relaxed) * (1.0 + g_skew);
  }
  // Clock loop as in the WASAPI thread: one-second fill average, slow skew integral, capped at 0.2%.
  const double dt = (double)n / g_device_rate;
  if (g_fill_average == 0.0) g_fill_average = (double)buffered;
  g_fill_average += ((double)buffered - g_fill_average) * std::min(1.0, dt / 1.0);
  const double error_s = (g_fill_average - (double)g_target_frames) / (double)SAMPLE_RATE;
  if (!g_priming) g_skew = std::clamp(g_skew + error_s * dt / (4.0 * 30.0), -MAX_SKEW, MAX_SKEW);
  const double target_rate = g_speed.load(std::memory_order_relaxed) *
                             (1.0 + std::clamp(error_s / 4.0 + g_skew, -MAX_RATE_SHIFT, MAX_RATE_SHIFT));
  g_rate += (target_rate - g_rate) * std::min(1.0, dt / 0.2);
  if (g_rate < g_rate_min.load()) g_rate_min.store(g_rate);
  if (g_rate > g_rate_max.load()) g_rate_max.store(g_rate);
  // Surplus after a stall: cut to the target at once (crossfaded), as the WASAPI path does.
  if (!g_priming) {
    const uint64_t margin = std::max<uint64_t>(SAMPLE_RATE * 8 / 1000 + pull32, g_target_frames / 2);
    if (buffered > g_target_frames + margin) {
      read += buffered - g_target_frames;
      g_ring_read.store(read, std::memory_order_release);
      if (g_ar.started) g_ar.interrupted = true;
      g_fill_average = (double)g_target_frames;
      g_trims.fetch_add(1);
    }
  }
  // The control thread builds this kernel before buffers start. No allocation on the callback.
  const uint64_t lookahead = SINC_TAPS / 2 + 1;
  const double fade = std::pow(0.98, (double)SAMPLE_RATE / g_device_rate);
  const bool priming_at_start = g_priming;
  if (g_ar.vol_now < 0.0) g_ar.vol_now = volume;
  const double vol_glide = std::min(1.0, 100.0 / g_device_rate);
  const uint64_t read_before = read;
  uint32_t starved = 0;
  for (long i = 0; i < n; ++i) {
    g_ar.vol_now += (volume - g_ar.vol_now) * vol_glide;
    const double vg = g_ar.vol_now / 100.0;
    if (g_priming || g_ring_write.load(std::memory_order_acquire) - read < lookahead) {
      g_last_output[0] = (int16_t)(g_last_output[0] * fade);
      g_last_output[1] = (int16_t)(g_last_output[1] * fade);
      out[i * 2] = (int16_t)(g_last_output[0] * vg);
      out[i * 2 + 1] = (int16_t)(g_last_output[1] * vg);
      g_ar.interrupted = true;
      if (g_ar.started && !host::exit_requested()) ++starved;
      continue;
    }
    const bool resuming = g_ar.interrupted;
    g_ar.started = true;
    if (resuming) { g_ar.recovery_left = g_device_rate * 5 / 1000; g_ar.interrupted = false; }
    const float* kern = &g_sinc.k[std::min((int)(g_ring_phase * SINC_PHASES + 0.5), SINC_PHASES) * SINC_TAPS];
    for (int channel = 0; channel < 2; ++channel) {
      double v = 0.0;
      for (int j = 0; j < SINC_TAPS; ++j) v += kern[j] * g_ring[(read + j - (SINC_TAPS / 2 - 1)) & RING_MASK][channel];
      if (resuming) g_ar.recovery_offset[channel] = g_last_output[channel] - v;
      if (g_ar.recovery_left) v += g_ar.recovery_offset[channel] * g_ar.recovery_left / (g_device_rate * 5.0 / 1000);
      g_last_output[channel] = (int16_t)std::clamp(v, -32768.0, 32767.0);
      out[i * 2 + channel] = (int16_t)(g_last_output[channel] * vg);
    }
    if (g_ar.recovery_left) --g_ar.recovery_left;
    g_ring_phase += g_rate * SAMPLE_RATE / g_device_rate;
    while (g_ring_phase >= 1.0) { g_ring_phase -= 1.0; ++read; }
  }
  g_ring_read.store(read, std::memory_order_release);
  if (g_trace && read > g_trace_onset.load()) {
    // Frames ahead of the traced sample: its place in this buffer plus the driver's output latency.
    const uint64_t onset = g_trace_onset.exchange(UINT64_MAX);
    const double ahead = g_asio_ui_out.load(std::memory_order_relaxed) + (onset > read_before ? (double)(onset - read_before) * g_device_rate / (SAMPLE_RATE * g_rate) : 0.0);
    char line[96];
    std::snprintf(line, sizeof line, "write %lld %.0f %u\n", (long long)qpc_now(), ahead, g_device_rate);
    trace_line(line);
  }
  if (starved) {
    g_underruns.fetch_add(1); g_underrun_frames.fetch_add(starved);
    const bool game_stalled = (qpc_now() - g_last_push_qpc.load(std::memory_order_relaxed)) * 1000 / qpc_hz() >
                              (int64_t)(10 + n * 1000 / std::max<uint32_t>(1, g_device_rate));
    if (game_stalled) g_priming = true;
    else if (!priming_at_start && g_gameplay.load(std::memory_order_relaxed)) {
      if (g_buffer_policy.gap(starved, g_device_rate)) g_margin_grows.fetch_add(1);
      g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
    }
  } else if (!g_priming) {
    g_buffer_policy.clean((uint32_t)n, g_device_rate);
    g_target_frames = SAMPLE_RATE * g_buffer_policy.target_ms / 1000;
  }
  g_target_ms.store(g_buffer_policy.target_ms);
  slippi::jukebox::mix(out, (size_t)n, volume / 100.0, (double)g_device_rate / g_rate);
  mix_ui_sound(out, (size_t)n, volume, g_device_rate);
  for (int ch = 0; ch < 2; ++ch) {
    uint8_t* dst = (uint8_t*)g_asio_buffers[ch].buffers[index];
    for (long i = 0; i < n; ++i) {
      const int16_t s = out[i * 2 + ch];
      switch (g_asio_type) {
        case ASIOSTInt16LSB: ((int16_t*)dst)[i] = s; break;
        case ASIOSTInt24LSB: audio_store_pcm24_le(dst + i * 3, s); break;
        case ASIOSTFloat32LSB: ((float*)dst)[i] = s / 32768.0f; break;
        default: ((int32_t*)dst)[i] = (int32_t)((uint32_t)(uint16_t)s << 16); break;   // Int32LSB
      }
    }
  }
  if (g_asio_output_ready) ASIOOutputReady();
}

void asio_buffer_switch(long index, ASIOBool) { if (g_asio_running.load(std::memory_order_acquire)) asio_render(index); }
ASIOTime* asio_buffer_switch_time(ASIOTime* params, long index, ASIOBool direct) { asio_buffer_switch(index, direct); return params; }
void asio_rate_changed(ASIOSampleRate) { g_asio_reset.store(true); if (g_asio_wake) SetEvent(g_asio_wake); }
long asio_message(long selector, long value, void*, double*) {
  switch (selector) {
    case kAsioSelectorSupported:
      return value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest ||
             value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo ? 1 : 0;
    case kAsioEngineVersion: return 2;
    case kAsioResetRequest: g_asio_reset.store(true); if (g_asio_wake) SetEvent(g_asio_wake); return 1;
    case kAsioResyncRequest: g_asio_reset.store(true); if (g_asio_wake) SetEvent(g_asio_wake); return 1;
    case kAsioLatenciesChanged: g_asio_refresh_latency.store(true); if (g_asio_wake) SetEvent(g_asio_wake); return 1;
    case kAsioSupportsTimeInfo: return 0;
  }
  return 0;
}
ASIOCallbacks g_asio_callbacks = {asio_buffer_switch, asio_rate_changed, asio_message, asio_buffer_switch_time};

const char* asio_type_name(ASIOSampleType t) {
  switch (t) {
    case ASIOSTInt16LSB: return "Int16LSB"; case ASIOSTInt24LSB: return "Int24LSB";
    case ASIOSTInt32LSB: return "Int32LSB"; case ASIOSTFloat32LSB: return "Float32LSB";
  }
  return nullptr;
}

// One driver session: load, init, buffers, start. Returns an error text, or empty on success.
std::string asio_start_session() {
  if (!asioDrivers) asioDrivers = new AsioDrivers();
  std::vector<char> name(g_asio_requested.begin(), g_asio_requested.end()); name.push_back('\0');
  if (g_asio_requested.empty() || !asioDrivers->loadDriver(name.data())) return "driver not found or could not be loaded";
  ASIODriverInfo info{}; info.asioVersion = 2; info.sysRef = nullptr;
  if (ASIOInit(&info) != ASE_OK) return std::string("init failed: ") + info.errorMessage;
  g_asio_driver_name = info.name;
  long in_ch = 0, out_ch = 0;
  if (ASIOGetChannels(&in_ch, &out_ch) != ASE_OK || out_ch < 2) { ASIOExit(); return "fewer than two outputs"; }
  if (ASIOCanSampleRate(48000.0) == ASE_OK) ASIOSetSampleRate(48000.0);
  ASIOSampleRate rate = 0;
  if (ASIOGetSampleRate(&rate) != ASE_OK || rate < 8000) { ASIOExit(); return "no sample rate"; }
  long minimum = 0, maximum = 0, preferred = 0, granularity = 0;
  if (ASIOGetBufferSize(&minimum, &maximum, &preferred, &granularity) != ASE_OK) { ASIOExit(); return "no buffer size"; }
  long size = preferred;
  if (g_asio_requested_buffer > 0) {
    size = std::clamp<long>(g_asio_requested_buffer, minimum, maximum);
    if (granularity > 0) size = minimum + (size - minimum) / granularity * granularity;
    else if (granularity == -1) { long p = minimum; while (p * 2 <= size && p * 2 <= maximum) p *= 2; size = p; }
    else if (granularity == 0) size = preferred;
  }
  ASIOChannelInfo ci[2] = {};
  for (int c = 0; c < 2; ++c) {
    ci[c].channel = c; ci[c].isInput = ASIOFalse;
    if (ASIOGetChannelInfo(&ci[c]) != ASE_OK) { ASIOExit(); return "no channel info"; }
  }
  g_asio_type = ci[0].type;
  if (!asio_type_name(g_asio_type) || ci[1].type != g_asio_type) { ASIOExit(); return "unsupported sample type " + std::to_string(ci[0].type); }
  for (int c = 0; c < 2; ++c) { g_asio_buffers[c] = {}; g_asio_buffers[c].isInput = ASIOFalse; g_asio_buffers[c].channelNum = c; }
  g_asio_buffer = size;
  g_asio_mix.assign((size_t)size * 2, 0);
  g_device_rate = (uint32_t)(rate + 0.5);
  g_sinc.build(g_device_rate);   // allocate and prepare before any driver callback
  if (ASIOCreateBuffers(g_asio_buffers, 2, size, &g_asio_callbacks) != ASE_OK) { ASIOExit(); return "buffers refused at " + std::to_string(size) + " frames"; }
  g_asio_latency_in = g_asio_latency_out = 0;
  ASIOGetLatencies(&g_asio_latency_in, &g_asio_latency_out);
  g_asio_output_ready = ASIOOutputReady() == ASE_OK;
  const int bytes = g_asio_type == ASIOSTInt16LSB ? 2 : g_asio_type == ASIOSTInt24LSB ? 3 : 4;
  for (int c = 0; c < 2; ++c) for (int b = 0; b < 2; ++b) std::memset(g_asio_buffers[c].buffers[b], 0, (size_t)size * bytes);
  // Queue target: one ASIO buffer taken per switch, one 5 ms game block arriving at a time, 2 ms slack.
  // Grows by itself after real gaps during a match, like Auto, and settles back while clean.
  const uint32_t period_ms = (uint32_t)((size * 1000 + g_device_rate - 1) / g_device_rate);
  g_floor_ms = period_ms + 7;
  g_buffer_policy.configure(0, AUTO_FLOOR_MS);
  g_buffer_policy.floor_ms = g_buffer_policy.target_ms = g_floor_ms;
  g_target_frames = SAMPLE_RATE * g_floor_ms / 1000;
  g_target_ms.store(g_floor_ms);
  g_device_queue_ms.store(0);
  g_engine_period_us.store((uint32_t)(size * 1000000ull / g_device_rate));
  g_asio_ui_buffer.store(size); g_asio_ui_in.store(g_asio_latency_in); g_asio_ui_out.store(g_asio_latency_out);
  g_priming = true; g_fill_average = 0.0; g_ar = AsioRender{};
  g_asio_running.store(true, std::memory_order_release);
  if (ASIOStart() != ASE_OK) { g_asio_running.store(false); ASIODisposeBuffers(); ASIOExit(); return "start failed"; }
  log("audio: ASIO %s buffer %ld frames, latency in/out %ld/%ld samples (%u Hz, %s, outputs 1-2, queue %u ms; driver min %ld max %ld preferred %ld)",
      g_asio_driver_name.c_str(), size, g_asio_latency_in, g_asio_latency_out, g_device_rate, asio_type_name(g_asio_type),
      g_floor_ms, minimum, maximum, preferred);
  return {};
}
void asio_end_session() {
  ASIOStop();
  g_asio_running.store(false, std::memory_order_release);
  ASIODisposeBuffers();
  ASIOExit();
}

void asio_control(std::promise<std::string>* started) {
  std::string error = asio_start_session();
  started->set_value(error);   // `started` is gone after this line
  if (!error.empty()) { delete asioDrivers; asioDrivers = nullptr; return; }
  while (!g_asio_stop.load()) {
    // Pump messages: some drivers post to their apartment's thread.
    const DWORD woke = MsgWaitForMultipleObjects(1, &g_asio_wake, FALSE, 500, QS_ALLINPUT);
    if (woke == WAIT_OBJECT_0 + 1) { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    if (g_asio_stop.load()) break;
    if (g_asio_refresh_latency.exchange(false)) {
      long input = 0, output = 0;
      if (ASIOGetLatencies(&input, &output) == ASE_OK) {
        g_asio_latency_in = input; g_asio_latency_out = output;
        g_asio_ui_in.store(input); g_asio_ui_out.store(output);
        log("audio: ASIO driver latency changed to %ld/%ld samples", input, output);
      }
    }
    if (g_asio_reset.exchange(false)) {
      log("audio: ASIO driver asked for a reset; restarting it");
      asio_end_session();
      if (const std::string again = asio_start_session(); !again.empty()) { log("audio: ASIO restart failed (%s); no sound until the game restarts", again.c_str()); break; }
    }
  }
  if (g_asio_running.load()) asio_end_session();
  delete asioDrivers; asioDrivers = nullptr;
}

bool asio_open() {
  if (!g_asio_wake) g_asio_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_asio_stop.store(false); g_asio_reset.store(false); g_asio_refresh_latency.store(false);
  std::promise<std::string> started;
  auto result = started.get_future();
  g_asio_thread = std::thread(asio_control, &started);
  const std::string error = result.get();
  if (!error.empty()) {
    g_asio_thread.join();
    log("audio: ASIO %s unavailable (%s); falling back to WASAPI Auto", g_asio_requested.empty() ? "(no driver chosen)" : g_asio_requested.c_str(), error.c_str());
    return false;
  }
  return true;
}
void asio_close() {
  if (!g_asio_thread.joinable()) return;
  g_asio_stop.store(true);
  SetEvent(g_asio_wake);
  g_asio_thread.join();
  log("audio: ASIO ended, queue %u ms (floor %u ms, %u surplus trims, %u increases after gaps)",
      g_buffer_policy.target_ms, g_floor_ms, g_trims.load(), g_margin_grows.load());
}
}  // namespace

void audio_set_volume(int volume) { g_volume.store(std::clamp(volume, 0, 100)); }
void audio_set_muted(bool muted) { g_muted.store(muted, std::memory_order_relaxed); }
void audio_set_device(const char* id) { g_requested_device = id ? id : ""; }
void audio_set_remember(bool remember) { g_remember = remember; }
void audio_set_speed(double speed) { g_speed.store(std::clamp(speed, 0.5, 2.0), std::memory_order_relaxed); }
void audio_set_gameplay(bool on) { g_gameplay.store(on, std::memory_order_relaxed); }
std::vector<AudioDevice> audio_list_devices() {
  std::vector<AudioDevice> list;
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDeviceCollection* devices = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)) &&
      SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* d = nullptr;
      if (FAILED(devices->Item(i, &d))) continue;
      AudioDevice entry;
      LPWSTR id = nullptr;
      if (SUCCEEDED(d->GetId(&id))) { entry.id = to_utf8(id); CoTaskMemFree(id); }
      IPropertyStore* props = nullptr;
      if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT name; PropVariantInit(&name);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) entry.name = to_utf8(name.pwszVal);
        PropVariantClear(&name);
        props->Release();
      }
      if (entry.name.empty()) entry.name = entry.id;
      list.push_back(std::move(entry));
      d->Release();
    }
  }
  if (devices) devices->Release();
  if (enumerator) enumerator->Release();
  if (SUCCEEDED(co)) CoUninitialize();
  return list;
}
bool audio_exclusive() { return g_exclusive; }
void audio_set_asio(const char* driver, int buffer_frames) {
  g_asio_requested = driver ? driver : "";
  g_asio_requested_buffer = buffer_frames > 0 ? std::clamp(buffer_frames, 32, 2048) : 0;
}
std::vector<std::string> audio_list_asio_drivers() {
  // The names the SDK's driver list uses: each key under HKLM\SOFTWARE\ASIO, its Description if set.
  std::vector<std::string> names;
  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS) return names;
  wchar_t key[256];
  for (DWORD i = 0;; ++i) {
    DWORD len = 256;
    if (RegEnumKeyExW(root, i, key, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    wchar_t desc[256] = {}; DWORD bytes = sizeof desc - sizeof(wchar_t);
    const bool has = RegGetValueW(root, key, L"Description", RRF_RT_REG_SZ, nullptr, desc, &bytes) == ERROR_SUCCESS && desc[0];
    names.push_back(to_utf8(has ? desc : key));
  }
  RegCloseKey(root);
  return names;
}
bool audio_asio_info(AsioStatus* status) {
  if (!g_asio_running.load()) return false;
  if (status) {
    status->driver = g_asio_driver_name; status->buffer_frames = g_asio_ui_buffer.load();
    status->latency_in = g_asio_ui_in.load(); status->latency_out = g_asio_ui_out.load(); status->sample_rate = g_device_rate;
  }
  return true;
}

void audio_set_buffering(int mode, int milliseconds) {
  g_requested_mode.store(std::clamp(mode, 0, 3));
  g_requested_buffer_ms.store(std::clamp(milliseconds, 5, 120));
  g_buffering_changed.store(true);
}
uint32_t audio_target_ms() { return g_target_ms.load(); }
uint32_t audio_device_queue_ms() { return g_device_queue_ms.load(); }
uint32_t audio_engine_period_us() { return g_engine_period_us.load(); }
int audio_volume() { return g_volume.load(); }
bool audio_running() { return g_open; }

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
  g_asio_driver_name.clear();
  if (g_requested_mode.load() == 3) {
    if (!std::getenv("MELEE_AUDIO_DEVICE_DUMP") && asio_open()) {
      g_open = true;
      log("audio: ASIO, 32 kHz stereo resampled to %u Hz, volume %d%%", g_device_rate, v);
      return true;
    }
    g_requested_mode.store(0);   // WASAPI Auto
  }
  if (wasapi_open()) backend = g_exclusive ? "WASAPI exclusive mode" : g_requested_device.empty() ? "WASAPI shared mode, default device" : "WASAPI shared mode, chosen device";
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
  asio_close();
  wasapi_close();
  if (g_out) {
    waveOutReset(g_out);
    for (int i = 0; i < BLOCKS; ++i) waveOutUnprepareHeader(g_out, &g_headers[i], sizeof(WAVEHDR));
    waveOutClose(g_out);
    g_out = nullptr;
  }
  g_open = false;
}

static void audio_push_ordered(const uint8_t* samples, size_t bytes,
                               bool little_endian) {
  if (!g_open) return;
  // A replay viewer that is paused, seeking or not at normal speed: the output runs dry and holds
  // silence, as it does through any gap, and resumes with the first block queued afterwards.
  if (g_muted.load(std::memory_order_relaxed)) return;
  std::unique_lock<std::mutex> winmm_lock(g_mutex, std::defer_lock);
  const bool ring = g_client || g_asio_running.load(std::memory_order_relaxed);
  if (!ring) winmm_lock.lock();   // the WASAPI path is lock free; only the fallback needs this
  for (size_t off = 0; off + BLOCK_BYTES <= bytes; off += BLOCK_BYTES) {
    int16_t converted[BLOCK_BYTES / 2];
    const uint8_t* src = samples + off;
    for (int i = 0; i < BLOCK_BYTES / 4; ++i) {
      int16_t r = little_endian
          ? (int16_t)(src[i * 4] | (src[i * 4 + 1] << 8))
          : (int16_t)((src[i * 4] << 8) | src[i * 4 + 1]);
      int16_t l = little_endian
          ? (int16_t)(src[i * 4 + 2] | (src[i * 4 + 3] << 8))
          : (int16_t)((src[i * 4 + 2] << 8) | src[i * 4 + 3]);
      converted[i * 2] = l; converted[i * 2 + 1] = r;
    }
    if (g_wav) { std::fwrite(converted, 1, BLOCK_BYTES, g_wav); g_wav_bytes += BLOCK_BYTES; }
    if (ring) {
      constexpr size_t frames = BLOCK_BYTES / 4;
      uint64_t write = g_ring_write.load(std::memory_order_relaxed);
      if (write + frames - g_ring_read.load(std::memory_order_acquire) > RING_FRAMES - 4) { ++g_dropped; continue; }
      for (size_t i = 0; i < frames; ++i) {
        g_ring[(write + i) & RING_MASK][0] = converted[i * 2];
        g_ring[(write + i) & RING_MASK][1] = converted[i * 2 + 1];
      }
      if (g_trace) {
        // The first loud sample after at least 100 ms of near silence.
        static uint64_t quiet_frames = 0;
        uint64_t loud = UINT64_MAX;
        for (size_t i = 0; i < frames && loud == UINT64_MAX; ++i)
          if (std::abs((int)converted[i * 2]) > 600 || std::abs((int)converted[i * 2 + 1]) > 600) loud = i;
        if (loud != UINT64_MAX && quiet_frames + loud >= SAMPLE_RATE / 10) {
          g_trace_onset.store(write + loud);
          char line[96];
          std::snprintf(line, sizeof line, "block %lld %llu\n", (long long)qpc_now(), (unsigned long long)(write + loud));
          trace_line(line);
        }
        if (loud == UINT64_MAX) quiet_frames += frames;
        else {
          uint64_t last_loud = loud;
          for (size_t i = loud; i < frames; ++i)
            if (std::abs((int)converted[i * 2]) > 600 || std::abs((int)converted[i * 2 + 1]) > 600) last_loud = i;
          quiet_frames = frames - 1 - last_loud;
        }
      }
      g_ring_write.store(write + frames, std::memory_order_release);
      g_frames += frames;
      g_last_push_qpc.store(qpc_now(), std::memory_order_relaxed);
      if (g_push_event) SetEvent(g_push_event);   // direct path: out to the device now, not at the next engine event
      continue;
    }
    if (!g_out) { g_frames += BLOCK_BYTES / 4; continue; }
    WAVEHDR& h = g_headers[g_next];
    if (!(h.dwFlags & WHDR_DONE)) { ++g_dropped; continue; }
    for (int i = 0; i < BLOCK_BYTES / 2; ++i)
      g_blocks[g_next][i] = (int16_t)((int32_t)converted[i] * g_volume / 100);
    slippi::jukebox::mix(g_blocks[g_next], BLOCK_BYTES / 4, g_volume / 100.0);
    mix_ui_sound(g_blocks[g_next], BLOCK_BYTES / 4, g_volume.load());
    h.dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(g_out, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { h.dwFlags |= WHDR_DONE; ++g_dropped; continue; }
    g_next = (g_next + 1) % BLOCKS;
    g_frames += BLOCK_BYTES / 4;
  }
}

void audio_push(const uint8_t* be_samples, size_t bytes) {
  audio_push_ordered(be_samples, bytes, false);
}

void audio_push_native(const uint8_t* le_samples, size_t bytes) {
  audio_push_ordered(le_samples, bytes, true);
}

void audio_trace_event(const char* name) {
  if (!g_trace) return;
  char line[96];
  std::snprintf(line, sizeof line, "%s %lld\n", name, (long long)qpc_now());
  trace_line(line);
}
bool audio_tracing() { return g_trace != nullptr; }

uint64_t audio_pushed_frames() { return g_frames; }
void audio_ui_sound(int kind) { g_ui_sound_request.store(kind); }
uint64_t audio_dropped_blocks() { return g_dropped; }
uint64_t audio_underruns(uint64_t* silent_ms) { if (silent_ms) *silent_ms = g_underrun_frames.load() * 1000 / g_device_rate; return g_underruns.load(); }
void audio_rate_range(double* low, double* high) { if (low) *low = g_rate_min.load(); if (high) *high = g_rate_max.load(); }
uint32_t audio_buffered_ms() { return (uint32_t)((g_ring_write.load() - g_ring_read.load()) * 1000 / SAMPLE_RATE); }

}  // namespace host
