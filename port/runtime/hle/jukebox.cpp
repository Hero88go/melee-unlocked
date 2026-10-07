// Port of Slippi's jukebox (Rust, hps_decode 0.3.0) onto the host audio mixer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "jukebox.h"
#include "host.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <shellapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <objbase.h>

namespace slippi::jukebox {
namespace {
constexpr uint32_t OUTPUT_RATE = 32000;
// The console's own level for a streamed song (synth.c, lbaudio_ax.c): stream volume 254/255 x music
// group volume 254/255 (32510 of 32768) x the centre-pan level each of the stereo stream's two voices
// gets (23124 left, 23215 right, of 32768). Measured against our AX path and the real DSP (LLE) within
// 0.04 dB. Slippi's jukebox constant 0.8 played the music 1.16 dB (left) / 1.12 dB (right) louder.
constexpr double CONSOLE_GAIN[2] = {32510.0 / 32768.0 * 23124.0 / 32768.0,    // 0.700133
                                    32510.0 / 32768.0 * 23215.0 / 32768.0};   // 0.702887

struct Song {
  std::vector<int16_t> samples;   // interleaved stereo at OUTPUT_RATE
  size_t loop_frame = SIZE_MAX;   // frame index the song restarts at, or SIZE_MAX for one-shot
  size_t position = 0;            // frames played
  double phase = 0.0;            // fractional input sample when the endpoint runs above 32 kHz
  float gain = 1.0f;             // per-song gain, set when the song is started
  double level = 0.0;            // smoothed fade level, 0 to 1
};

constexpr double FADE_STEP = 1.0 / (0.030 * OUTPUT_RATE);   // 30 ms fades on start, stop and swap
constexpr double GAIN_SMOOTH = 1.0 / (0.010 * OUTPUT_RATE); // volume changes glide over about 10 ms

std::mutex g_mutex;
std::shared_ptr<Song> g_song;     // replaced atomically under the mutex; the mixer holds a copy
std::shared_ptr<Song> g_fading;   // the previous song, fading out after a stop or a swap
double g_gain_now = -1.0;         // smoothed output gain (mixer thread only)
std::atomic<uint32_t> g_song_generation{0};   // a stop() or a newer start_song() cancels an in-flight decode
std::atomic<int> g_melee_volume{254}, g_user_volume{100};
std::atomic<float> g_next_song_gain{1.0f}, g_song_gain{1.0f};
std::atomic<DiscReader> g_reader{nullptr};
std::atomic<MusicPackReader> g_music_pack_reader{nullptr};
std::atomic<bool> g_music_packs_enabled{true};
std::atomic<bool> g_paused{false};   // set_paused: a replay viewer holding the picture

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
  // The resampler below makes 32000 / rate output frames per input frame: a rate of a few Hz would
  // ask for gigabytes. No song is recorded under 8 kHz (0 plays as 32 kHz).
  if (rate != 0 && rate < 8000) { host::log("jukebox: %u Hz unsupported", rate); return nullptr; }
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
  // The lengths and offsets are the file's own, so they are checked in 64 bits (a 32-bit sum wraps
  // and passes). Blocks do not overlap in a real song, so one holds at most size / 0x20 blocks and
  // size bytes of frames: a file that chains past either is damaged, and what was read so far plays.
  const uint64_t size = file.size();
  const size_t max_blocks = (size_t)(size / 0x20);
  uint64_t budget = size;
  while ((uint64_t)off + 0x20 <= size && blocks.size() < max_blocks) {
    const uint8_t* b = file.data() + off;
    uint32_t len = be32(b), next = be32(b + 8);
    if (len > size - off - 0x20 || len > budget || (len % 8) != 0) break;
    budget -= len;
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
    // The chain only moves forward (a next at or before this block is the loop point), so it ends.
    if (next == 0xFFFFFFFFu || next <= off || next >= size) break;
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

std::shared_ptr<Song> decode_custom_file(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  if (ext == ".hps") {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec || size > 64u * 1024 * 1024 || size < 0x80) return nullptr;
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    std::vector<uint8_t> file((size_t)size);
    if (!in.read((char*)file.data(), (std::streamsize)file.size())) return nullptr;
    return decode_hps(file);
  }

  // Windows Media Foundation decodes common player formats (WAV, MP3, M4A/AAC and installed
  // codecs) to the same stereo mixer format used by HPS. Unsupported formats fail back to disc.
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninit_com = SUCCEEDED(com_hr);
  if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) return nullptr;
  static std::once_flag mf_once;
  static HRESULT mf_hr = E_FAIL;
  std::call_once(mf_once, [] { mf_hr = MFStartup(MF_VERSION, MFSTARTUP_LITE); });
  if (FAILED(mf_hr)) { if (uninit_com) CoUninitialize(); return nullptr; }

  IMFSourceReader* reader = nullptr;
  IMFMediaType* output_type = nullptr;
  HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&output_type);
  if (SUCCEEDED(hr)) hr = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (SUCCEEDED(hr)) hr = output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, OUTPUT_RATE);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, OUTPUT_RATE * 4);
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, output_type);
  if (output_type) output_type->Release();
  auto song = SUCCEEDED(hr) ? std::make_shared<Song>() : nullptr;
  bool finished = false, too_long = false;
  // Ten minutes of decoded stereo is about 77 MB held in memory; longer files use the disc track.
  constexpr size_t MAX_FRAMES = OUTPUT_RATE * 60u * 10u;
  while (song && !finished) {
    DWORD stream = 0, flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = nullptr;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream, &flags, &timestamp, &sample);
    if (FAILED(hr)) { song.reset(); break; }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) finished = true;
    if (sample) {
      IMFMediaBuffer* buffer = nullptr;
      hr = sample->ConvertToContiguousBuffer(&buffer);
      if (SUCCEEDED(hr)) {
        BYTE* data = nullptr;
        DWORD maximum = 0, length = 0;
        hr = buffer->Lock(&data, &maximum, &length);
        if (SUCCEEDED(hr)) {
          if ((length & 3u) != 0 || song->samples.size() / 2 + length / 4 > MAX_FRAMES) {
            too_long = (length & 3u) == 0;
            song.reset();
          } else {
            const size_t before = song->samples.size();
            song->samples.resize(before + length / 2);
            std::memcpy(song->samples.data() + before, data, length);
          }
          buffer->Unlock();
        } else song.reset();
        buffer->Release();
      } else song.reset();
      sample->Release();
    }
  }
  if (reader) reader->Release();
  if (uninit_com) CoUninitialize();
  if (!song || song->samples.empty()) {
    host::log("jukebox: custom audio %s %s, using the disc track", path.filename().string().c_str(),
              too_long ? "is longer than 10 minutes" : "could not be decoded");
    return nullptr;
  }
  song->loop_frame = 0;
  host::log("jukebox: custom audio %s, %zu frames", path.filename().string().c_str(), song->samples.size() / 2);
  return song;
}
}  // namespace

bool resolve_music_pack_path(const std::string& disc_path, std::filesystem::path* out) {
  if (!out || disc_path.empty()) return false;
  const std::string relative_text = !disc_path.empty() && (disc_path[0] == '/' || disc_path[0] == '\\')
      ? disc_path.substr(1) : disc_path;
  const std::filesystem::path disc = std::filesystem::u8path(relative_text).lexically_normal();
  for (const auto& part : disc) if (part == ".." || part == ".") return false;
  const std::string first = disc.begin() == disc.end() ? std::string() : disc.begin()->string();
  if (_stricmp(first.c_str(), "audio") != 0) return false;

  wchar_t module[32768];
  const DWORD n = GetModuleFileNameW(nullptr, module, (DWORD)_countof(module));
  if (!n || n >= _countof(module)) return false;
  const std::filesystem::path root = std::filesystem::path(std::wstring(module, n)).parent_path() / L"MusicPacks";
  const std::filesystem::path direct = root / disc;
  std::error_code ec;
  if (std::filesystem::is_regular_file(direct, ec) && !ec) { *out = direct; return true; }
  ec.clear();
  for (const char* extension : {".mp3", ".wav", ".m4a", ".flac", ".ogg"}) {
    std::filesystem::path alternate = direct;
    alternate.replace_extension(extension);
    if (std::filesystem::is_regular_file(alternate, ec) && !ec) { *out = alternate; return true; }
    ec.clear();
  }
  const std::filesystem::path playlist = root / disc.parent_path() / disc.stem();
  std::vector<std::filesystem::path> tracks;
  std::filesystem::directory_iterator it(playlist, std::filesystem::directory_options::skip_permission_denied, ec);
  for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
    std::error_code item_ec;
    if (!it->is_regular_file(item_ec) || item_ec) continue;
    const std::string ext = it->path().extension().string();
    if (_stricmp(ext.c_str(), ".hps") == 0 || _stricmp(ext.c_str(), ".wav") == 0 ||
        _stricmp(ext.c_str(), ".mp3") == 0 || _stricmp(ext.c_str(), ".m4a") == 0 ||
        _stricmp(ext.c_str(), ".flac") == 0 || _stricmp(ext.c_str(), ".ogg") == 0)
      tracks.push_back(it->path());
  }
  if (tracks.empty()) return false;
  std::sort(tracks.begin(), tracks.end());
  static std::atomic<uint32_t> seed{(uint32_t)GetTickCount() ^ 0x9E3779B9u};
  uint32_t value = seed.fetch_add(0x9E3779B9u, std::memory_order_relaxed);
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  *out = tracks[value % tracks.size()];
  return true;
}

// The disc read and HPS decode (tens of ms for a full track) run on a worker: music start time
// is not part of the deterministic simulation, and the game's own audio must not hitch for it.
void start_song(uint32_t disc_offset, uint32_t size) {
  const float song_gain = g_next_song_gain.exchange(1.0f);   // belongs to this song, not the one playing
  host::SimCostScope cost(host::SIM_JUKEBOX);
  if (size == 0 || size > 64u * 1024 * 1024) { host::log("jukebox: bad song size %u", size); return; }
  const uint32_t generation = ++g_song_generation;
  std::thread([disc_offset, size, generation, song_gain] {
    std::filesystem::path custom_path;
    const MusicPackReader pack_reader = g_music_packs_enabled.load(std::memory_order_relaxed)
        ? g_music_pack_reader.load() : nullptr;
    bool custom = pack_reader && pack_reader(disc_offset, &custom_path);
    if (!custom && g_music_packs_enabled.load(std::memory_order_relaxed)) {
      std::string disc_path;
      if (host::disc_find_path_by_offset(disc_offset, &disc_path))
        custom = resolve_music_pack_path(disc_path, &custom_path);
    }
    if (custom) host::log("jukebox: music pack picked %s", custom_path.u8string().c_str());
    std::shared_ptr<Song> song = custom ? decode_custom_file(custom_path) : nullptr;
    const DiscReader reader = g_reader.load();
    // A malformed optional replacement should never mute a song that otherwise worked.
    if (!song) {
      std::vector<uint8_t> file(size);
      if (!(reader ? reader(disc_offset, file.data(), size) : host::disc_read(disc_offset, file.data(), size))) {
        host::log("jukebox: cannot read song at %08X", disc_offset);
        return;
      }
      song = decode_hps(file);
    }
    if (song) song->gain = song_gain;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_song_generation.load() == generation) {
      if (g_song) g_fading = g_song;   // the old song fades out while the new one fades in
      else if (song) song->level = 1.0;   // nothing playing: start as the console does, at full level
      g_song = song;
    }
  }).detach();
}

void set_disc_reader(DiscReader reader) { g_reader.store(reader); }
void set_music_pack_reader(MusicPackReader reader) { g_music_pack_reader.store(reader); }
void set_music_packs_enabled(bool enabled) { g_music_packs_enabled.store(enabled, std::memory_order_relaxed); }
bool music_packs_enabled() { return g_music_packs_enabled.load(std::memory_order_relaxed); }
void open_music_packs_folder() {
  wchar_t path[32768];
  const DWORD length = GetModuleFileNameW(nullptr, path, (DWORD)_countof(path));
  const std::filesystem::path folder = length && length < _countof(path)
      ? std::filesystem::path(std::wstring(path, length)).parent_path() / L"MusicPacks"
      : std::filesystem::path(L"MusicPacks");
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (ec) { host::log("jukebox: cannot create music folder (%s)", ec.message().c_str()); return; }
  if (!g_music_pack_reader.load()) {
    std::vector<std::string> paths;
    if (host::disc_music_paths(&paths) && !paths.empty()) {
      std::ofstream list(folder / L"Tracks.txt", std::ios::trunc);
      if (list) {
        list << "Menu and stage music files on this game disc:\n";
        for (const auto& disc_path : paths) list << disc_path << "\n";
      }
    }
  }
  const auto readme = folder / L"README.txt";
  if (!std::filesystem::exists(readme, ec)) {
    std::ofstream note(readme);
    note << "Custom music\n\n"
            "Tracks.txt lists the original menu and stage music files (refresh it from Audio settings while the game is running). For /audio/name.hps, replace it with\n"
            "MusicPacks/audio/name.hps (HPS), or MusicPacks/audio/name.mp3 (MP3), MusicPacks/audio/name.wav\n"
            "(WAV), or another supported audio extension. For a random playlist, put audio files in\n"
            "MusicPacks/audio/name/. The game picks one each time it starts that song.\n"
            "Windows Media Foundation handles common formats; HPS files must be stereo.\n"
            "Custom tracks play locally; other players do not need the same pack. Vanilla audio mode keeps the disc music.\n";
  }
  ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void stop() {
  ++g_song_generation;
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_song) g_fading = g_song;   // fades out over 30 ms instead of a hard cut
  g_song.reset();
}
void set_melee_volume(uint8_t volume) { g_melee_volume.store(volume); }
void set_next_song_gain(float gain) { g_next_song_gain.store(gain); }
void set_user_volume(int percent) { g_user_volume.store(std::clamp(percent, 0, 100)); }
int user_volume() { return g_user_volume.load(); }

namespace {
// Adds one song into out[] (float accumulator). The fade level moves toward target at FADE_STEP per
// 32 kHz frame. Returns false when the song has ended or faded out.
bool render(Song& song, float* acc, size_t frames, double step, double target, const double* gains) {
  const size_t total = song.samples.size() / 2;
  if (total == 0) return false;
  const double fade = FADE_STEP * step;
  for (size_t i = 0; i < frames; ++i) {
    song.level += song.level < target ? std::min(fade, target - song.level) : -std::min(fade, song.level - target);
    if (song.position >= total) {
      if (song.loop_frame == SIZE_MAX) return false;
      song.position = std::min(song.loop_frame, total - 1);
    }
    const size_t next = song.position + 1 < total ? song.position + 1 :
        song.loop_frame == SIZE_MAX ? song.position : std::min(song.loop_frame, total - 1);
    const double g = gains[i] * song.gain * song.level;
    for (int ch = 0; ch < 2; ++ch) {
      const double sample = song.samples[song.position * 2 + ch] * (1.0 - song.phase) + song.samples[next * 2 + ch] * song.phase;
      acc[i * 2 + ch] += (float)(sample * g * CONSOLE_GAIN[ch]);
    }
    song.phase += step;
    while (song.phase >= 1.0) { song.phase -= 1.0; ++song.position; }
  }
  return target > 0.0 || song.level > 0.0;
}
}  // namespace

void set_paused(bool paused) { g_paused.store(paused, std::memory_order_relaxed); }

void mix(int16_t* out, size_t frames, double master, double output_rate) {
  if (g_paused.load(std::memory_order_relaxed)) return;   // the song keeps its place and goes on from it
  std::shared_ptr<Song> song, fading;
  { std::lock_guard<std::mutex> lk(g_mutex); song = g_song; fading = g_fading; }
  if (!song && !fading) { g_gain_now = -1.0; return; }
  const double target = (g_melee_volume.load() / 254.0) * (g_user_volume.load() / 100.0) * master;
  const double step = (double)OUTPUT_RATE / std::max(1.0, output_rate);
  if (g_gain_now < 0.0) g_gain_now = target;
  static thread_local std::vector<double> gains;
  static thread_local std::vector<float> acc;
  gains.resize(frames);
  acc.assign(frames * 2, 0.0f);
  const double glide = GAIN_SMOOTH * step;
  for (size_t i = 0; i < frames; ++i) {
    g_gain_now += (target - g_gain_now) * std::min(1.0, glide * 4.0);
    gains[i] = g_gain_now;
  }
  bool song_alive = true, fading_alive = true;
  if (song) song_alive = render(*song, acc.data(), frames, step, 1.0, gains.data());
  if (fading) fading_alive = render(*fading, acc.data(), frames, step, 0.0, gains.data());
  for (size_t i = 0; i < frames * 2; ++i)
    out[i] = (int16_t)std::clamp((int32_t)out[i] + (int32_t)std::lround(acc[i]), -32768, 32767);
  if (!song_alive || !fading_alive) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!song_alive && g_song == song) g_song.reset();
    if (!fading_alive && g_fading == fading) g_fading.reset();
  }
}
}  // namespace slippi::jukebox
