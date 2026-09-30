// Host audio output: 32 kHz stereo AI DMA blocks, through WASAPI with a WinMM fallback.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace host {
void audio_set_volume(int volume);
// 0 = Auto (adaptive, remembered per device), 1 = Low latency (fixed), 2 = Exclusive (WASAPI
// exclusive, falls back to Low latency), 3 = ASIO (the driver set by audio_set_asio; falls back to
// Auto). Mode and device apply when the device opens (restart).
void audio_set_buffering(int mode, int milliseconds);
struct AudioDevice { std::string id, name; };
std::vector<AudioDevice> audio_list_devices();   // active output devices (UTF-8)
void audio_set_device(const char* id);           // empty or null = Windows default device
void audio_set_remember(bool remember);
// ASIO (mode 3): driver name as listed under HKLM\SOFTWARE\ASIO; buffer in frames, 0 = the driver's preferred.
void audio_set_asio(const char* driver, int buffer_frames);
std::vector<std::string> audio_list_asio_drivers();
struct AsioStatus { std::string driver; long buffer_frames = 0, latency_in = 0, latency_out = 0; uint32_t sample_rate = 0; };
bool audio_asio_info(AsioStatus* status);        // false unless an ASIO driver is playing now          // false: Auto mode never saves what it learned (tests)
// The game's speed against real time (Slippi's online time sync sets it, 0.995 to 1.01): the output
// consumes the game's sound that much faster, a pitch change as on the console, so the queue holds.
void audio_set_speed(double speed);
// Whether a match is running (its frame counter moving). Auto grows its buffer only for gaps then.
void audio_set_gameplay(bool on);
bool audio_exclusive();                          // the device is held in exclusive mode right now
uint32_t audio_target_ms();
uint32_t audio_device_queue_ms();
uint32_t audio_engine_period_us();
int audio_volume();
// Whether an audio device (or a WAV dump) is actually running. The settings window opened from the
// launcher has neither, and its volume slider must not read itself back from a device that is not
// there: doing so pinned it to zero every frame, so it appeared to do nothing.
bool audio_running();
// volume_percent 0..100; 0 keeps the session muted (default for development).
bool audio_open(int volume_percent, const char* wav_dump_path = nullptr, bool open_device = true);
void audio_close();
// `bytes` of big-endian 16-bit samples ordered R, L, R, L ... (GameCube AI DMA format).
void audio_push(const uint8_t* be_samples, size_t bytes);
// Native AX writes the same R,L sample words in host byte order.
void audio_push_native(const uint8_t* le_samples, size_t bytes);
uint64_t audio_pushed_frames();
uint64_t audio_dropped_blocks();
uint64_t audio_underruns(uint64_t* silent_ms);   // output gaps (ring empty), and the total time they held the last sample
void audio_rate_range(double* low, double* high);  // resampling ratio extremes used to track the sound card's clock
uint32_t audio_buffered_ms();                      // how much audio is queued for the device right now
// Settings menu feedback: 1 = move between items, 2 = select / open, 3 = back / close.
// Mixed into the output on the audio thread; safe to call from any thread, never blocks.
void audio_ui_sound(int kind);
// Latency trace (MELEE_AUDIO_TRACE=<file>): a named moment with its QPC time, e.g. "input" when the
// game's pad read first shows a new press. audio_tracing() is false unless the variable is set.
void audio_trace_event(const char* name);
bool audio_tracing();
}  // namespace host
