// Linux: the media decoding Windows does with Media Foundation and WIC, through FFmpeg. Used by the
// music packs (jukebox.cpp) and the menu video backgrounds and border art (video_background.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace linux_media {

// The file as interleaved signed 16-bit stereo at `rate`, handed to `sink` piece by piece as it is
// decoded (frames, not samples); the sink returns false to stop early. *expected_frames gets the
// length the file states (0 when it states none). False when it cannot be decoded or is longer than
// `max_frames` (then *too_long is set; a stated length over it is refused before anything decodes).
using AudioSink = std::function<bool(const int16_t* pcm, size_t frames)>;
bool decode_audio(const std::filesystem::path& path, int rate, size_t max_frames, const AudioSink& sink,
                  size_t* expected_frames, bool* too_long);

// A picture (PNG, JPG, BMP, WebP) as BGRA rows from the top, scaled down to fit max_w x max_h.
bool load_picture(const std::filesystem::path& path, uint32_t max_w, uint32_t max_h, uint32_t* width, uint32_t* height,
                  std::vector<uint8_t>& bgra);

// A video's frames as BGRA with the bottom row first (as the Windows decoder hands them on), each
// with its presentation time in 100 ns units.
class VideoReader {
 public:
  VideoReader();
  ~VideoReader();
  bool open(const std::filesystem::path& path, std::string* error);
  uint32_t width() const;
  uint32_t height() const;
  // 1: a frame, 0: end of the file, -1: an error (in *error).
  int read(std::vector<uint8_t>& bgra, int64_t* time_100ns, std::string* error);
  bool rewind();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace linux_media
