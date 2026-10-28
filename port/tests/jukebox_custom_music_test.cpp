#include "jukebox.h"
#include "host.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <windows.h>

namespace {
std::filesystem::path custom_path;
std::string disc_path;
bool disc_reader(uint32_t, void*, uint32_t) { return false; }
void put16(std::ofstream& out, uint16_t value) {
  out.put((char)value); out.put((char)(value >> 8));
}
void put32(std::ofstream& out, uint32_t value) {
  put16(out, (uint16_t)value); put16(out, (uint16_t)(value >> 16));
}
bool make_wave(const std::filesystem::path& path) {
  constexpr uint32_t rate = 32000, frames = 6400;
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write("RIFF", 4); put32(out, 36 + frames * 4); out.write("WAVEfmt ", 8);
  put32(out, 16); put16(out, 1); put16(out, 2); put32(out, rate);
  put32(out, rate * 4); put16(out, 4); put16(out, 16); out.write("data", 4);
  put32(out, frames * 4);
  for (uint32_t i = 0; i < frames; ++i) {
    const int16_t sample = (int16_t)((i % 64 < 32) ? 12000 : -12000);
    put16(out, (uint16_t)sample); put16(out, (uint16_t)sample);
  }
  return (bool)out;
}
}

namespace host {
const double tsc_seconds = 1.0;
void sim_cost_add(int, double) {}
void log(const char* format, ...) {
  if (!std::getenv("MELEE_TEST_JUKEBOX_FILE")) return;
  va_list args;
  va_start(args, format);
  std::printf("  log: ");
  std::vprintf(format, args);
  std::printf("\n");
  va_end(args);
}
bool disc_read(uint32_t, void*, uint32_t) { return false; }
bool disc_find_path_by_offset(uint32_t offset, std::string* path) {
  if (offset != 0x1000 || !path) return false;
  *path = disc_path;
  return true;
}
bool disc_music_paths(std::vector<std::string>* paths) { if (paths) paths->clear(); return false; }
}

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  wchar_t module[32768];
  const DWORD module_length = GetModuleFileNameW(nullptr, module, (DWORD)_countof(module));
  if (!module_length || module_length >= _countof(module)) return 1;
  const auto music_root = std::filesystem::path(std::wstring(module, module_length)).parent_path() / L"MusicPacks";
  const auto audio_root = music_root / L"audio";
  const auto slot = audio_root / (L"test_" + std::to_wstring(nonce));
  std::error_code ec;
  const bool made_music_root = std::filesystem::create_directory(music_root, ec);
  if (ec) return 1;
  ec.clear();
  const bool made_audio_root = std::filesystem::create_directory(audio_root, ec);
  if (ec) return 1;
  ec.clear();
  if (!std::filesystem::create_directory(slot, ec) || ec) return 1;
  // MELEE_TEST_JUKEBOX_FILE=<file>: that file instead of the generated one, timing its first sound.
  const char* timed = std::getenv("MELEE_TEST_JUKEBOX_FILE");
  custom_path = slot / (timed ? L"track" + std::filesystem::u8path(timed).extension().wstring() : std::wstring(L"track.wav"));
  disc_path = "/audio/" + slot.filename().string() + ".hps";
  if (timed ? !std::filesystem::copy_file(std::filesystem::u8path(timed), custom_path, ec) : !make_wave(custom_path)) return 2;
  const auto started = std::chrono::steady_clock::now();
  slippi::jukebox::set_music_pack_reader(nullptr);
  slippi::jukebox::set_disc_reader(disc_reader);
  slippi::jukebox::set_music_packs_enabled(true);
  slippi::jukebox::set_user_volume(100);
  slippi::jukebox::start_song(0x1000, 0x80);
  // MELEE_TEST_JUKEBOX_DUMP=<file>: once decoded, 30 seconds of the mixer's output written there raw,
  // to compare two builds' output sample for sample.
  if (const char* dump = std::getenv("MELEE_TEST_JUKEBOX_DUMP")) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::ofstream out(std::filesystem::u8path(dump), std::ios::binary);
    for (int block = 0; block < 32000 * 30 / 256; ++block) {
      int16_t samples[512]{};
      slippi::jukebox::mix(samples, 256, 1.0);
      out.write((const char*)samples, sizeof samples);
    }
  }
  bool heard = false;
  for (int attempt = 0; attempt < (timed ? 4000 : 200) && !heard; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    int16_t samples[512]{};
    slippi::jukebox::mix(samples, 256, 1.0);
    heard = std::any_of(std::begin(samples), std::end(samples), [](int16_t s) { return s != 0; });
  }
  if (timed) std::printf("first sound after %.0f ms\n",
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
  if (timed) std::this_thread::sleep_for(std::chrono::seconds(3));   // the decode's own log line
  slippi::jukebox::stop();
  // One playlist folder for several songs: "<other>+<song>" serves both, and a folder named after
  // the song alone, or a direct replacement file, still comes first.
  bool shared_ok = true;
  if (!timed) {
    const std::string song = "shared_" + std::to_string(nonce);
    const auto shared = audio_root / std::filesystem::u8path("menu01+" + song);
    const auto own = audio_root / std::filesystem::u8path(song);
    std::filesystem::path picked;
    std::filesystem::create_directory(shared, ec);
    ec.clear();
    shared_ok = make_wave(shared / L"a.wav") && slippi::jukebox::resolve_music_pack_path("/audio/" + song + ".hps", &picked) &&
                picked == shared / L"a.wav";
    shared_ok = shared_ok && !slippi::jukebox::resolve_music_pack_path("/audio/" + song.substr(1) + ".hps", &picked);   // whole names only
    std::filesystem::create_directory(own, ec);
    ec.clear();
    shared_ok = shared_ok && make_wave(own / L"b.wav") && slippi::jukebox::resolve_music_pack_path("/audio/" + song + ".hps", &picked) &&
                picked == own / L"b.wav";
    std::filesystem::remove_all(shared, ec);
    ec.clear();
    std::filesystem::remove_all(own, ec);
    ec.clear();
  }
  std::filesystem::remove(custom_path, ec);
  ec.clear();
  std::filesystem::remove(slot, ec);
  ec.clear();
  if (made_audio_root) std::filesystem::remove(audio_root, ec);
  ec.clear();
  if (made_music_root) std::filesystem::remove(music_root, ec);
  return !heard ? 3 : !shared_ok ? 4 : 0;
}
