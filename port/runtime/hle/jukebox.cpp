// Port of Slippi's jukebox (Rust, hps_decode 0.3.0) onto the host audio mixer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "jukebox.h"
#include "host.h"
#include "audio_sinc.h"
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
#include <functional>
#include <new>
#include <string>
#include <windows.h>
#include <shellapi.h>
#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <objbase.h>
#else
#include "full/linux_media.h"
#endif

namespace slippi::jukebox {
namespace {
constexpr uint32_t OUTPUT_RATE = 32000;
std::atomic<uint32_t> g_output_rate{OUTPUT_RATE};
// The console's own level for a streamed song (synth.c, lbaudio_ax.c): stream volume 254/255 x music
// group volume 254/255 (32510 of 32768) x the centre-pan level each of the stereo stream's two voices
// gets (23124 left, 23215 right, of 32768). Measured against our AX path and the real DSP (LLE) within
// 0.04 dB. Slippi's jukebox constant 0.8 played the music 1.16 dB (left) / 1.12 dB (right) louder.
constexpr double CONSOLE_GAIN[2] = {32510.0 / 32768.0 * 23124.0 / 32768.0,    // 0.700133
                                    32510.0 / 32768.0 * 23215.0 / 32768.0};   // 0.702887

// A player's own file (MP3, WAV, ...) is played while it decodes: decoding a whole track before the
// first sample took up to a second or more, heard as a gap where the song should start. The decoder
// thread writes frames into the buffer and then publishes how many are ready; the mixer plays only
// those. A buffer that has to grow is copied into a larger one on the decoder thread and swapped in,
// so the audio thread never waits on a lock or an allocation.
struct StreamBuffer {
  std::unique_ptr<float[]> data;   // interleaved stereo at the song's native rate, in 16-bit sample units
  size_t capacity = 0;               // frames
};
struct Song {
  std::vector<float> samples;   // native-rate PCM in 16-bit sample units; no integer rounding after decode
  uint32_t rate = OUTPUT_RATE;
  host::audio_resampling::SincTable filter;   // prepared by the decoder before publication
  std::shared_ptr<StreamBuffer> stream;   // a song decoded while it plays; std::atomic_load/store
  std::atomic<size_t> ready{0};           // frames of `stream` written; read before `stream`
  std::atomic<bool> complete{true};       // false while `stream` is still being written
  size_t loop_frame = SIZE_MAX;   // frame index the song restarts at, or SIZE_MAX for one-shot
  size_t position = 0;            // frames played
  double phase = 0.0;            // fractional input sample when the endpoint runs above 32 kHz
  bool looped = false;
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
  // Refuse damaged rates, including ones that would require an unbounded filter. 0 means 32 kHz.
  if (rate != 0 && (rate < 8000 || rate > 384000)) { host::log("jukebox: %u Hz unsupported", rate); return nullptr; }
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
  song->rate = rate ? rate : OUTPUT_RATE;
  for (auto& blk : blocks) song->samples.insert(song->samples.end(), blk.pcm.begin(), blk.pcm.end());
  song->loop_frame = loop_frame;
  song->filter.build(g_output_rate.load(), song->rate);
  host::log("jukebox: song %u Hz, %zu frames, %s", rate, song->samples.size() / 2, song->loop_frame == SIZE_MAX ? "no loop" : "loops");
  return song;
}

// Ten minutes at 32 kHz, or the same 154 MB memory budget at higher native rates.
constexpr size_t MAX_CUSTOM_FRAMES = OUTPUT_RATE * 60u * 10u;
using PcmSink = std::function<bool(const float* pcm, size_t frames)>;

// A player's file in any format the system decodes (all but HPS), to `sink` piece by piece as
// interleaved stereo at its native rate. *rate and *expected_frames are set before
// the first piece. False when nothing could be decoded, or the file is over ten minutes.
bool decode_player_file(const std::filesystem::path& path, const PcmSink& sink, uint32_t* rate, size_t* expected_frames, bool* too_long) {
  *expected_frames = 0;
  *too_long = false;
#ifndef _WIN32
  // Linux: FFmpeg decodes the player formats (WAV, MP3, M4A/AAC, OGG, FLAC, Opus).
  return linux_media::decode_audio(path, rate, MAX_CUSTOM_FRAMES, sink, expected_frames, too_long);
#else
  // Windows Media Foundation decodes common player formats (WAV, MP3, M4A/AAC and installed codecs).
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninit_com = SUCCEEDED(com_hr);
  if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) return false;
  static std::once_flag mf_once;
  static HRESULT mf_hr = E_FAIL;
  std::call_once(mf_once, [] { mf_hr = MFStartup(MF_VERSION, MFSTARTUP_LITE); });
  if (FAILED(mf_hr)) { if (uninit_com) CoUninitialize(); return false; }

  IMFSourceReader* reader = nullptr;
  IMFMediaType* output_type = nullptr;
  HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
  IMFMediaType* native_type = nullptr;
  UINT32 native_rate = 0;
  if (SUCCEEDED(hr)) hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native_type);
  if (SUCCEEDED(hr)) hr = native_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &native_rate);
  if (native_type) native_type->Release();
  if (native_rate < 8000 || native_rate > 384000) hr = E_FAIL;
  *rate = native_rate;
  const size_t max_frames = std::min(MAX_CUSTOM_FRAMES, (size_t)native_rate * 600u);
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&output_type);
  if (SUCCEEDED(hr)) hr = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (SUCCEEDED(hr)) hr = output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, native_rate);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
  if (SUCCEEDED(hr)) hr = output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, native_rate * 8);
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, output_type);
  if (output_type) output_type->Release();
  if (SUCCEEDED(hr)) {
    // The length the file states (100 ns units), to size the buffer once and refuse a long file early.
    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) &&
        duration.vt == VT_UI8) {
      const double frames = (double)duration.uhVal.QuadPart / 1e7 * native_rate;
      if (frames > (double)max_frames) { *too_long = true; hr = E_FAIL; }
      else *expected_frames = (size_t)frames;
    }
    PropVariantClear(&duration);
  }
  bool ok = SUCCEEDED(hr), finished = false;
  size_t written = 0;
  while (ok && !finished) {
    DWORD stream = 0, flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = nullptr;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream, &flags, &timestamp, &sample);
    if (FAILED(hr)) { ok = false; break; }
    // Never play a changed stream format using the old rate and frame layout.
    if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
      if (sample) sample->Release();
      ok = false; break;
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) finished = true;
    if (sample) {
      IMFMediaBuffer* buffer = nullptr;
      hr = sample->ConvertToContiguousBuffer(&buffer);
      if (SUCCEEDED(hr)) {
        BYTE* data = nullptr;
        DWORD maximum = 0, length = 0;
        hr = buffer->Lock(&data, &maximum, &length);
        if (SUCCEEDED(hr)) {
          if ((length & 7u) != 0) ok = false;
          else if (written + length / 8 > max_frames) { *too_long = true; ok = false; }
          else if (length && !sink(reinterpret_cast<const float*>(data), length / 8)) finished = true;   // the caller stopped it
          else written += length / 8;
          buffer->Unlock();
        } else ok = false;
        buffer->Release();
      } else ok = false;
      sample->Release();
    }
  }
  if (reader) reader->Release();
  if (uninit_com) CoUninitialize();
  return ok && written > 0;
#endif
}

// Starts `song` as the one playing, unless another song has been asked for since `generation`.
void publish_song(const std::shared_ptr<Song>& song, uint32_t generation) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_song_generation.load() != generation) return;
  if (song) song->filter.build(g_output_rate.load(), song->rate);
  if (g_song) g_fading = g_song;   // the old song fades out while the new one fades in
  else if (song) song->level = 1.0;   // nothing playing: start as the console does, at full level
  g_song = song;
}

// A player's file played while it decodes (StreamBuffer): it starts as soon as one second is ready,
// where decoding all of it first left a gap of up to a second or more at the start of every song.
// False when it could not start (the disc track plays instead). Once started, a file that fails or
// runs past ten minutes partway ends where its decoding stopped.
bool stream_custom_file(const std::filesystem::path& path, uint32_t generation, float gain) {
  auto song = std::make_shared<Song>();
  song->gain = gain;
  song->loop_frame = 0;
  song->complete.store(false, std::memory_order_relaxed);
  std::shared_ptr<StreamBuffer> buffer;
  size_t written = 0, expected = 0;
  bool started = false, out_of_memory = false;
  const auto sink = [&](const float* pcm, size_t frames) {
    if (g_song_generation.load() != generation) return false;   // another song was asked for
    if (!buffer) song->filter.build(g_output_rate.load(), song->rate);
    if (!buffer || written + frames > buffer->capacity) {
      // Sized from the stated length plus a second, so a file that states it is copied at most once.
      const size_t want = std::min(MAX_CUSTOM_FRAMES, std::max({written + frames, expected + song->rate,
                                    buffer ? buffer->capacity * 2 : (size_t)song->rate * 60}));
      auto next = std::make_shared<StreamBuffer>();
      next->data.reset(new (std::nothrow) float[want * 2]);
      if (!next->data) { out_of_memory = true; return false; }
      next->capacity = want;
      if (written) std::memcpy(next->data.get(), buffer->data.get(), written * 2 * sizeof(float));
      std::atomic_store(&song->stream, next);   // swapped in before more frames are counted
      buffer = std::move(next);
    }
    for (size_t i = 0; i < frames * 2; ++i)
      buffer->data[written * 2 + i] = std::isfinite(pcm[i]) ? pcm[i] * 32768.0f : 0.0f;
    written += frames;
    song->ready.store(written, std::memory_order_release);
    if (!started && written >= song->rate) { started = true; publish_song(song, generation); }
    return true;
  };
  bool too_long = false;
  const bool ok = decode_player_file(path, sink, &song->rate, &expected, &too_long);
  song->complete.store(true, std::memory_order_release);
  const std::string name = path.filename().u8string();
  if (g_song_generation.load() != generation) return true;   // replaced meanwhile: nothing to play
  if (!started) {
    if (!ok || !written) {
      host::log("jukebox: custom audio %s %s, using the disc track", name.c_str(),
                too_long ? "exceeds the music length or memory limit" : out_of_memory ? "did not fit in memory" : "could not be decoded");
      return false;
    }
    publish_song(song, generation);   // shorter than a second
  }
  if (!ok || out_of_memory)
    host::log("jukebox: custom audio %s stopped decoding after %zu frames (%s); it ends there", name.c_str(), written,
              too_long ? "music length or memory limit" : out_of_memory ? "out of memory" : "decode error");
  else
    host::log("jukebox: custom audio %s, %u Hz, %zu frames", name.c_str(), song->rate, written);
  return true;
}

// An HPS file from a music pack, decoded whole: that takes tens of milliseconds.
std::shared_ptr<Song> decode_custom_hps(const std::filesystem::path& path) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(path, ec);
  if (ec || size > 64u * 1024 * 1024 || size < 0x80) return nullptr;
  std::ifstream in(path, std::ios::binary);
  if (!in) return nullptr;
  std::vector<uint8_t> file((size_t)size);
  if (!in.read((char*)file.data(), (std::streamsize)file.size())) return nullptr;
  return decode_hps(file);
}
// The game's song files by id (lbaudio_ax.c hps_files), for the stage songs the game reports.
const char* const kSongFiles[] = {
    "1p_qk", "akaneia", "baloon", "bigblue", "castle", "continue", "corneria", "docmari", "ending", "famidemo",
    "ff_1p01", "ff_1p02", "ff_bad", "ff_dk", "ff_emb", "ff_flat", "ff_fox", "ff_fzero", "ff_good", "ff_ice",
    "ff_kirby", "ff_link", "ff_mario", "ff_nes", "ff_poke", "ff_samus", "ff_step1", "ff_step2", "ff_step3", "ff_yoshi",
    "flatzone", "fourside", "gameover", "garden", "greatbay", "greens", "howto", "howto_s", "hyaku", "hyaku2",
    "icemt", "inis1_01", "inis1_02", "inis2_01", "inis2_02", "intro_es", "intro_nm", "item_h", "item_s", "izumi",
    "kongo", "kraid", "menu01", "menu02", "menu3", "mrider", "mutecity", "old_dk", "old_kb", "old_ys",
    "onetto", "onetto2", "opening", "pokesta", "pstadium", "pura", "rcruise", "s_info1", "s_info2", "s_info3",
    "s_new1", "s_new2", "s_newcom", "s_select", "saria", "shrine", "siren", "smari3", "sp_end", "sp_giga",
    "sp_metal", "sp_zako", "swm_15min", "target", "venom", "vl_battle", "vl_castle", "vl_corneria", "vl_cosmos", "vl_figure1",
    "vl_figure2", "vl_fzero", "vl_last_v2", "vs_hyou1", "vs_hyou2", "yorster", "ystory", "zebes", "testnz",
};
// A stage's alternate song -> its main song (lowercase stems), learned as each stage picks its music.
std::mutex g_alternates_mutex;
std::vector<std::pair<std::string, std::string>> g_alternates;
std::string lower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return v;
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
  std::filesystem::path playlist = root / disc.parent_path() / disc.stem();
  // One playlist for several songs (a stage's main and alternate songs): a folder named after each
  // of them joined with "+", such as audio/ystory+smari3/. A folder named after the song alone wins.
  if (!std::filesystem::is_directory(playlist, ec)) {
    ec.clear();
    const std::string stem = disc.stem().u8string();
    std::filesystem::directory_iterator shared(root / disc.parent_path(), std::filesystem::directory_options::skip_permission_denied, ec);
    std::vector<std::filesystem::path> matches;
    for (std::filesystem::directory_iterator end; !ec && shared != end; shared.increment(ec)) {
      std::error_code item_ec;
      if (!shared->is_directory(item_ec) || item_ec) continue;
      const std::string name = shared->path().filename().u8string();
      for (size_t start = 0; start <= name.size();) {
        const size_t plus = std::min(name.find('+', start), name.size());
        if (plus - start == stem.size() && _strnicmp(name.c_str() + start, stem.c_str(), stem.size()) == 0) {
          matches.push_back(shared->path());
          break;
        }
        start = plus + 1;
      }
    }
    ec.clear();
    if (!matches.empty()) { std::sort(matches.begin(), matches.end()); playlist = matches.front(); }   // the same one every time
  }
  ec.clear();
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
  if (tracks.empty()) {
    // A stage's alternate song with no music of its own plays from its main song's music, so a
    // folder made for a stage covers the whole stage.
    std::string main;
    { const std::string stem = lower(disc.stem().u8string());
      std::lock_guard<std::mutex> lock(g_alternates_mutex);
      for (const auto& a : g_alternates) if (a.first == stem) { main = a.second; break; } }
    if (main.empty()) return false;
    return resolve_music_pack_path((disc.parent_path() / std::filesystem::u8path(main + ".hps")).generic_u8string(), out);
  }
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
  std::thread([disc_offset, size, generation, song_gain] { host::optional_work("jukebox", [&] {
    // MELEE_TEST_JUKEBOX_OOM (tests only, hidden runs): every song load runs out of memory.
    if (std::getenv("MELEE_NO_GC_ADAPTER") && std::getenv("MELEE_TEST_JUKEBOX_OOM")) throw std::bad_alloc();
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
    std::shared_ptr<Song> song;
    if (custom) {
      std::string ext = custom_path.extension().u8string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      if (ext == ".hps") song = decode_custom_hps(custom_path);
      else if (stream_custom_file(custom_path, generation, song_gain)) return;   // playing (or replaced) already
    }
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
    publish_song(song, generation);
  }); }).detach();
}

void note_stage_songs(int main_song, int alternate_song) {
  constexpr int count = (int)(sizeof kSongFiles / sizeof kSongFiles[0]);
  if (main_song < 0 || main_song >= count || alternate_song < 0 || alternate_song >= count || main_song == alternate_song) return;
  const std::string alt = kSongFiles[alternate_song], main = kSongFiles[main_song];
  std::lock_guard<std::mutex> lock(g_alternates_mutex);
  for (auto& a : g_alternates) if (a.first == alt) { a.second = main; return; }
  g_alternates.emplace_back(alt, main);
  host::log("jukebox: %s plays from %s's music pack when it has none of its own", alt.c_str(), main.c_str());
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
    note << "Custom music / audio packs\n\n"
            "1. In F1 > Audio, enable Use custom music and use Open music packs to open this build's folder.\n"
            "2. Open Tracks.txt for the exact original names. Start the game with its ISO loaded, then open this folder again if the list is missing.\n"
            "3. Extract downloaded ZIP packs first. The audio folder goes directly inside MusicPacks, without an extra pack-name folder.\n"
            "Tracks.txt lists the original menu and stage music files (refresh it from Audio settings while the game is running). For /audio/name.hps, replace it with\n"
            "MusicPacks/audio/name.hps (HPS), or MusicPacks/audio/name.mp3 (MP3), MusicPacks/audio/name.wav\n"
            "(WAV), or another supported audio extension. For a random playlist, put audio files in\n"
            "MusicPacks/audio/name/. The game picks one each time it starts that song.\n"
            "Use the actual name from Tracks.txt in place of name. Keep the file's real extension; renaming an MP3 to HPS does not convert it.\n"
            "One folder can serve several tracks (a stage's main and alternate songs): name it after each joined with +, like MusicPacks/audio/name+othername/.\n"
            "A direct replacement takes priority over the random playlist. Leave and re-enter the stage/menu to start the new song.\n"
            "These packs replace music, not character voices or sound effects. Check the Music and Volume sliders if silent.\n"
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
bool render(Song& song, float* acc, size_t frames, double output_rate, uint32_t device_rate, double target, const double* gains) {
  // `complete` and `ready` before `stream`: a count that is read is never more than the buffer that
  // is then loaded holds (the decoder swaps in a grown buffer before it counts frames written to it).
  const bool complete = song.complete.load(std::memory_order_acquire);
  const size_t ready = song.ready.load(std::memory_order_acquire);
  const std::shared_ptr<StreamBuffer> stream = std::atomic_load(&song.stream);
  const float* samples = stream ? stream->data.get() : song.samples.data();
  const size_t total = stream ? ready : song.samples.size() / 2;
  if (total == 0 && complete) return false;
  song.filter.build(device_rate, song.rate);
  const double step = (double)song.rate / output_rate;
  const double fade = FADE_STEP * OUTPUT_RATE / output_rate;
  const int half = song.filter.taps / 2;
  const size_t loop = song.loop_frame == SIZE_MAX ? total : std::min(song.loop_frame, total - 1);
  // Kernel taps see the loop as a continuous stream. Before the first loop, retain the intro;
  // after it, taps to the left of the loop point come from the tail, never from the intro again.
  const auto sample_at = [&](int64_t frame, int ch) -> double {
    if (complete && loop < total && (frame >= (int64_t)total || (song.looped && frame < (int64_t)loop))) {
      const int64_t length = (int64_t)(total - loop);
      frame = (int64_t)loop + ((frame - (int64_t)loop) % length + length) % length;
    }
    if (frame < 0 || frame >= (int64_t)total) return 0.0;
    return samples[(size_t)frame * 2 + ch];
  };
  for (size_t i = 0; i < frames; ++i) {
    song.level += song.level < target ? std::min(fade, target - song.level) : -std::min(fade, song.level - target);
    // Decoding has not reached this point yet (it runs far faster than playback, so only a computer
    // under heavy load gets here): hold the place in silence rather than loop or end early.
    if (!complete && song.position + half >= total) continue;
    if (song.position >= total) {
      if (song.loop_frame == SIZE_MAX) return false;
      song.position = loop + (song.position - total) % (total - loop);
      song.looped = true;
    }
    const double g = gains[i] * song.gain * song.level;
    std::array<double, 2> value{};
    if (song.position >= (size_t)(half - 1) && song.position + half < total &&
        (!song.looped || song.position >= loop + half - 1)) {
      const float* base = samples + song.position * 2;
      value = song.filter.stereo(song.phase, [&](int offset) { return base + (ptrdiff_t)offset * 2; });
    } else {
      for (int ch = 0; ch < 2; ++ch)
        value[ch] = song.filter.sample(song.phase, [&](int offset) { return sample_at((int64_t)song.position + offset, ch); });
    }
    for (int ch = 0; ch < 2; ++ch) {
      acc[i * 2 + ch] += (float)(value[ch] * g * CONSOLE_GAIN[ch]);
    }
    song.phase += step;
    const size_t advance = (size_t)song.phase;
    song.phase -= advance;
    song.position += advance;
  }
  return target > 0.0 || song.level > 0.0;
}
}  // namespace

void set_paused(bool paused) { g_paused.store(paused, std::memory_order_relaxed); }
void set_output_rate(uint32_t rate) {
  g_output_rate.store(rate, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_song) g_song->filter.build(rate, g_song->rate);
  if (g_fading) g_fading->filter.build(rate, g_fading->rate);
}

void mix(float* out, size_t frames, double master, double output_rate, uint32_t device_rate) {
  output_rate = std::max(1.0, output_rate);
  if (!device_rate) device_rate = (uint32_t)std::lround(output_rate);
  g_output_rate.store(device_rate, std::memory_order_relaxed);
  if (g_paused.load(std::memory_order_relaxed)) return;   // the song keeps its place and goes on from it
  std::shared_ptr<Song> song, fading;
  { std::lock_guard<std::mutex> lk(g_mutex); song = g_song; fading = g_fading; }
  if (!song && !fading) { g_gain_now = -1.0; return; }
  const double target = (g_melee_volume.load() / 254.0) * (g_user_volume.load() / 100.0) * master;
  const double step = (double)OUTPUT_RATE / output_rate;
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
  if (song) song_alive = render(*song, acc.data(), frames, output_rate, device_rate, 1.0, gains.data());
  if (fading) fading_alive = render(*fading, acc.data(), frames, output_rate, device_rate, 0.0, gains.data());
  for (size_t i = 0; i < frames * 2; ++i)
    out[i] += acc[i];   // retain headroom and fractional samples until the final device conversion
  if (!song_alive || !fading_alive) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!song_alive && g_song == song) g_song.reset();
    if (!fading_alive && g_fading == fading) g_fading.reset();
  }
}
// Legacy 16-bit output (WinMM and callers that explicitly request integer PCM).
void mix(int16_t* out, size_t frames, double master, double output_rate, uint32_t device_rate) {
  static thread_local std::vector<float> pcm;
  pcm.assign(out, out + frames * 2);
  mix(pcm.data(), frames, master, output_rate, device_rate);
  for (size_t i = 0; i < frames * 2; ++i)
    out[i] = (int16_t)std::lround(std::clamp((double)pcm[i], -32768.0, 32767.0));
}
}  // namespace slippi::jukebox
