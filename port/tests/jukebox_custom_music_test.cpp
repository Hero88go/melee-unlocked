#include "jukebox.h"
#include "host.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
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
void log(const char*, ...) {}
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
  custom_path = slot / L"track.wav";
  disc_path = "/audio/" + slot.filename().string() + ".hps";
  if (!make_wave(custom_path)) return 2;
  slippi::jukebox::set_music_pack_reader(nullptr);
  slippi::jukebox::set_disc_reader(disc_reader);
  slippi::jukebox::set_music_packs_enabled(true);
  slippi::jukebox::set_user_volume(100);
  slippi::jukebox::start_song(0x1000, 0x80);
  bool heard = false;
  for (int attempt = 0; attempt < 200 && !heard; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    int16_t samples[512]{};
    slippi::jukebox::mix(samples, 256, 1.0);
    heard = std::any_of(std::begin(samples), std::end(samples), [](int16_t s) { return s != 0; });
  }
  slippi::jukebox::stop();
  std::filesystem::remove(custom_path, ec);
  ec.clear();
  std::filesystem::remove(slot, ec);
  ec.clear();
  if (made_audio_root) std::filesystem::remove(audio_root, ec);
  ec.clear();
  if (made_music_root) std::filesystem::remove(music_root, ec);
  return heard ? 0 : 3;
}
