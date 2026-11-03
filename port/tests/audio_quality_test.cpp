// End-to-end music conversion, quiet output precision and loop/stream boundaries.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../runtime/hle/jukebox.cpp"
#include "audio_sample_conversion.h"
#include "console_sound_mode.h"
#include <cstdio>
#include <limits>
#include <chrono>

namespace host {
void log(const char*, ...) {}
bool disc_read(uint32_t, void*, uint32_t) { return false; }
bool disc_find_path_by_offset(uint32_t, std::string*) { return false; }
bool disc_music_paths(std::vector<std::string>* paths) { if (paths) paths->clear(); return false; }
void sim_cost_add(int, double) {}
const double tsc_seconds = 0.0;
}

namespace {
using namespace slippi::jukebox;
constexpr double PI = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* label) {
  if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
// Start away from the file boundary, so the measurement covers the steady-state converter.
std::shared_ptr<Song> tone(uint32_t rate, double frequency, double amplitude = 10000.0) {
  auto song = std::make_shared<Song>();
  song->rate = rate;
  song->position = 512;
  song->level = 1.0;
  song->samples.resize(((size_t)rate / 4 + 1024) * 2);
  for (size_t i = 0; i < song->samples.size() / 2; ++i) {
    song->samples[i * 2] = (float)(amplitude * std::sin(2 * PI * frequency * i / rate));
    song->samples[i * 2 + 1] = (float)(amplitude * std::cos(2 * PI * frequency * i / rate));
  }
  return song;
}
void measure(uint32_t source, uint32_t device, double frequency, bool reject = false) {
  auto song = tone(source, frequency);
  const size_t count = device / 20;
  std::vector<double> gains(count, 1.0);
  std::vector<float> output(count * 2, 0.0f);
  check(render(*song, output.data(), count, device, device, 1.0, gains.data()), "tone stays alive");
  double error = 0.0, energy = 0.0;
  for (size_t i = 0; i < count; ++i) for (int ch = 0; ch < 2; ++ch) {
    const double phase = 2 * PI * frequency * (512.0 / source + (double)i / device);
    const double ideal = 10000.0 * CONSOLE_GAIN[ch] * (ch ? std::cos(phase) : std::sin(phase));
    const double delta = output[i * 2 + ch] - (reject ? 0.0 : ideal);
    error += delta * delta;
    energy += ideal * ideal;
  }
  const double db = 10 * std::log10(std::max(error / energy, 1e-30));
  std::printf("%u -> %u Hz, tone %.0f Hz: %s %.2f dB\n", source, device, frequency, reject ? "alias" : "error", db);
  check(db < (frequency == 15000 && source == 32000 ? -50.0 : -75.0), reject ? "out-of-band rejection" : "passband fidelity");
}
}

int main() {
  {
    const auto dir = std::filesystem::current_path() / "audio-quality-sound-mode";
    { host::ConsoleSoundMode mode(dir); check(mode.get() == 1, "new console defaults to stereo"); check(mode.set(0), "mono saves"); }
    { host::ConsoleSoundMode mode(dir); check(mode.get() == 0, "mono survives restart"); check(mode.set(1), "stereo saves"); }
    { host::ConsoleSoundMode mode(dir); check(mode.get() == 1, "stereo survives restart"); }
    std::error_code ec; std::filesystem::remove(dir / "sound-mode.txt", ec); std::filesystem::remove(dir, ec);
  }
  // Exercise the real system decoder, including floats quieter than a 16-bit sample. Decoding
  // these at 32 kHz or into int16 would destroy either the high-frequency tone or the quiet detail.
  for (uint32_t rate : {44100u, 48000u, 96000u, 192000u, 384000u}) {
    const auto path = std::filesystem::current_path() / ("audio-quality-" + std::to_string(rate) + ".wav");
    const size_t frames = rate / 10;
    {
      std::ofstream file(path, std::ios::binary);
      auto put16 = [&](uint16_t v) { file.put((char)v); file.put((char)(v >> 8)); };
      auto put32 = [&](uint32_t v) { put16((uint16_t)v); put16((uint16_t)(v >> 16)); };
      file.write("RIFF", 4); put32(36 + (uint32_t)frames * 8); file.write("WAVEfmt ", 8);
      put32(16); put16(3); put16(2); put32(rate); put32(rate * 8); put16(8); put16(32);
      file.write("data", 4); put32((uint32_t)frames * 8);
      for (size_t i = 0; i < frames; ++i) {
        float pcm[2] = {(float)(0.1 * std::sin(2 * PI * 20000 * i / rate)), 0.0000001f};
        file.write((const char*)pcm, sizeof pcm);
      }
    }
    uint32_t actual_rate = 0; size_t expected = 0; bool too_long = false;
    std::vector<float> pcm;
    const bool decoded = decode_player_file(path, [&](const float* data, size_t count) {
      pcm.insert(pcm.end(), data, data + count * 2); return true;
    }, &actual_rate, &expected, &too_long);
    check(decoded && actual_rate == rate && pcm.size() == frames * 2, "decoder preserves native rate and frame count");
    check(!pcm.empty() && std::abs(pcm[1] - 0.0000001f) < 1e-12f, "decoder preserves sub-16-bit detail");
    std::error_code ec; std::filesystem::remove(path, ec);
  }
  for (uint32_t rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u}) {
    measure(32000, rate, 1000);
    measure(32000, rate, 14000);
    measure(32000, rate, 15000);
  }
  measure(44100, 48000, 20000);
  measure(48000, 96000, 20000);
  measure(192000, 44100, 50000, true);
  measure(384000, 48000, 100000, true);

  // At the reported 3% master volume, sub-LSB detail must survive into a wide endpoint.
  {
    auto song = tone(32000, 1000, 1.0);
    g_song = song; g_fading.reset(); g_gain_now = -1.0;
    g_melee_volume = 254; g_user_volume = 100;
    std::vector<float> output(4800, 0.0f);
    mix(output.data(), output.size() / 2, 0.03, 48000, 48000);
    double peak = 0;
    for (float value : output) peak = std::max(peak, std::abs((double)value));
    check(peak > 0.020 && peak < 0.022, "quiet detail survives 3% volume");
    check(host::audio_quantize_pcm(0.03, 24) == 8, "24-bit endpoint retains sub-16-bit samples");
    check(host::audio_quantize_pcm(0.03, 32) == 1966, "32-bit endpoint retains sub-16-bit samples");
    check(host::audio_pcm_float(0.03) > 0.0f, "float endpoint retains quiet samples");
    check(host::audio_quantize_pcm(32768, 32) == INT32_MAX && host::audio_quantize_pcm(-32768, 32) == INT32_MIN, "integer full scale saturates without wrap");
    check(host::audio_quantize_pcm(std::numeric_limits<double>::quiet_NaN(), 24) == 0, "non-finite PCM is silent");
    uint8_t packed[3]{}; host::audio_store_pcm24_le(packed, 0.03f);
    check(packed[0] == 8 && packed[1] == 0 && packed[2] == 0, "packed PCM24 keeps low bits");
    g_song.reset();
  }
  // A short loop with an intro must wrap filter history into the tail after the first traversal.
  {
    auto song = tone(32000, 1000);
    song->samples.assign(256, 1000.0f);
    song->loop_frame = 32; song->position = 127; song->phase = 0.75;
    std::vector<double> gains(512, 1.0);
    std::vector<float> output(1024, 0.0f);
    check(render(*song, output.data(), 512, 8000, 8000, 1.0, gains.data()), "short loop survives large steps");
    check(song->position >= 32 && song->position < 132 && song->looped, "loop keeps overshoot and native position");
    // The very first sample still includes the file's zero-padded start. Thereafter it has looped.
    for (size_t i = 1; i < 512; ++i)
      check(std::abs(output[i * 2] - 1000 * CONSOLE_GAIN[0]) < 0.001, "loop has no filter seam");
    song->loop_frame = SIZE_MAX; song->position = 128;
    check(!render(*song, output.data(), 1, 48000, 48000, 1.0, gains.data()), "one-shot ends");
  }
  {
    auto song = tone(48000, 1000); song->samples.clear(); song->position = 100;
    auto buffer = std::make_shared<StreamBuffer>();
    buffer->capacity = 512; buffer->data = std::make_unique<float[]>(1024);
    song->stream = buffer; song->ready = 120; song->complete = false;
    double gain = 1.0; float output[2]{};
    check(render(*song, output, 1, 48000, 48000, 1.0, &gain) && song->position == 100, "stream waits for all filter lookahead");
    check(output[0] == 0 && output[1] == 0, "unwritten streaming data stays silent");
  }
  {
    auto song = tone(48000, 20000);
    song->loop_frame = 0;
    constexpr uint32_t device = 384000;
    constexpr size_t frames = 1920;  // 5 ms at the highest tested device rate
    std::vector<double> gains(frames, 1.0);
    std::vector<float> output(frames * 2);
    song->filter.build(device, song->rate);
    double worst_ms = 0, total_ms = 0;
    for (int callback = 0; callback < 100; ++callback) {
      const auto start = std::chrono::steady_clock::now();
      render(*song, output.data(), frames, device, device, 1.0, gains.data());
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      worst_ms = std::max(worst_ms, ms); total_ms += ms;
    }
    std::printf("384 kHz music callback (5 ms budget): average %.3f ms, worst %.3f ms\n", total_ms / 100, worst_ms);
  }
  return failures ? 1 : 0;
}
