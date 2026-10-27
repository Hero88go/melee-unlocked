// Linux media decoding through FFmpeg (libavformat, libavcodec, libswscale, libswresample).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "linux_media.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cstring>

namespace linux_media {
namespace {
std::string av_text(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof buf);
  return buf;
}

struct Input {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  int stream = -1;
  ~Input() {
    if (codec) avcodec_free_context(&codec);
    if (format) avformat_close_input(&format);
  }
  bool open(const std::filesystem::path& path, AVMediaType type, std::string* error) {
    int err = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
    if (err < 0) { if (error) *error = "cannot open (" + av_text(err) + ")"; return false; }
    if ((err = avformat_find_stream_info(format, nullptr)) < 0) { if (error) *error = "no stream information (" + av_text(err) + ")"; return false; }
    const AVCodec* decoder = nullptr;
    stream = av_find_best_stream(format, type, -1, -1, &decoder, 0);
    if (stream < 0 || !decoder) { if (error) *error = type == AVMEDIA_TYPE_VIDEO ? "no video stream" : "no audio stream"; return false; }
    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) < 0) { if (error) *error = "decoder setup failed"; return false; }
    codec->thread_count = 0;   // as many as the decoder likes
    if ((err = avcodec_open2(codec, decoder, nullptr)) < 0) { if (error) *error = "cannot start the decoder (" + av_text(err) + ")"; return false; }
    return true;
  }
  // The next decoded frame of the chosen stream: 1, 0 at the end, -1 on an error.
  int next(AVFrame* frame, AVPacket* packet, bool* draining) {
    for (;;) {
      int err = avcodec_receive_frame(codec, frame);
      if (err == 0) return 1;
      if (err == AVERROR_EOF) return 0;
      if (err != AVERROR(EAGAIN)) return -1;
      if (*draining) return 0;
      err = av_read_frame(format, packet);
      if (err == AVERROR_EOF) { *draining = true; avcodec_send_packet(codec, nullptr); continue; }
      if (err < 0) return -1;
      if (packet->stream_index == stream) avcodec_send_packet(codec, packet);
      av_packet_unref(packet);
    }
  }
};
}  // namespace

bool decode_audio(const std::filesystem::path& path, int rate, size_t max_frames, const AudioSink& sink,
                  size_t* expected_frames, bool* too_long) {
  if (too_long) *too_long = false;
  if (expected_frames) *expected_frames = 0;
  Input in;
  if (!in.open(path, AVMEDIA_TYPE_AUDIO, nullptr)) return false;
  if (in.format->duration > 0) {
    const double frames = (double)in.format->duration / AV_TIME_BASE * rate;
    if (frames > (double)max_frames) { if (too_long) *too_long = true; return false; }
    if (expected_frames) *expected_frames = (size_t)frames;
  }
  SwrContext* swr = nullptr;
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  AVChannelLayout source_layout{};
  if (in.codec->ch_layout.nb_channels > 0) av_channel_layout_copy(&source_layout, &in.codec->ch_layout);
  else av_channel_layout_default(&source_layout, 2);
  if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, rate, &source_layout, in.codec->sample_fmt, in.codec->sample_rate, 0, nullptr) < 0 ||
      swr_init(swr) < 0) {
    av_channel_layout_uninit(&source_layout);
    swr_free(&swr);
    return false;
  }
  av_channel_layout_uninit(&source_layout);
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  bool draining = false, ok = true;
  size_t written = 0;
  std::vector<int16_t> chunk;
  for (;;) {
    const int got = in.next(frame, packet, &draining);
    if (got < 0) { ok = false; break; }
    const int capacity = got ? swr_get_out_samples(swr, frame->nb_samples) : swr_get_out_samples(swr, 0);
    if (capacity > 0) {
      chunk.resize((size_t)capacity * 2);
      uint8_t* dst = reinterpret_cast<uint8_t*>(chunk.data());
      const int converted = swr_convert(swr, &dst, capacity, got ? (const uint8_t**)frame->extended_data : nullptr, got ? frame->nb_samples : 0);
      if (converted < 0) { ok = false; break; }
      if (written + (size_t)converted > max_frames) { if (too_long) *too_long = true; ok = false; break; }
      if (converted > 0 && !sink(chunk.data(), (size_t)converted)) break;   // the caller stopped it
      written += (size_t)converted;
    }
    if (!got) break;
    av_frame_unref(frame);
  }
  av_packet_free(&packet);
  av_frame_free(&frame);
  swr_free(&swr);
  return ok && written > 0;
}

bool load_picture(const std::filesystem::path& path, uint32_t max_w, uint32_t max_h, uint32_t* width, uint32_t* height,
                  std::vector<uint8_t>& bgra) {
  Input in;
  if (!in.open(path, AVMEDIA_TYPE_VIDEO, nullptr)) return false;
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  bool draining = false;
  const int got = in.next(frame, packet, &draining);
  bool ok = false;
  if (got == 1 && frame->width > 0 && frame->height > 0) {
    uint32_t w = (uint32_t)frame->width, h = (uint32_t)frame->height;
    if (w > max_w || h > max_h) {
      const double fit = std::min((double)max_w / w, (double)max_h / h);
      w = std::max<uint32_t>(1, (uint32_t)(w * fit)); h = std::max<uint32_t>(1, (uint32_t)(h * fit));
    }
    SwsContext* sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, (int)w, (int)h,
                                     AV_PIX_FMT_BGRA, SWS_LANCZOS, nullptr, nullptr, nullptr);
    if (sws) {
      bgra.assign((size_t)w * h * 4, 0);
      uint8_t* dst[1] = {bgra.data()};
      const int dst_stride[1] = {(int)w * 4};
      ok = sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dst_stride) > 0;
      sws_freeContext(sws);
      if (ok) { *width = w; *height = h; }
    }
  }
  av_packet_free(&packet);
  av_frame_free(&frame);
  return ok;
}

struct VideoReader::Impl {
  Input in;
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  SwsContext* sws = nullptr;
  bool draining = false;
  std::vector<uint8_t> top_down;
  ~Impl() {
    if (sws) sws_freeContext(sws);
    av_packet_free(&packet);
    av_frame_free(&frame);
  }
};
VideoReader::VideoReader() : impl_(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;
bool VideoReader::open(const std::filesystem::path& path, std::string* error) {
  if (!impl_->in.open(path, AVMEDIA_TYPE_VIDEO, error)) return false;
  if (impl_->in.codec->width <= 0 || impl_->in.codec->height <= 0) { if (error) *error = "video dimensions are missing"; return false; }
  return true;
}
uint32_t VideoReader::width() const { return impl_->in.codec ? (uint32_t)impl_->in.codec->width : 0; }
uint32_t VideoReader::height() const { return impl_->in.codec ? (uint32_t)impl_->in.codec->height : 0; }
int VideoReader::read(std::vector<uint8_t>& bgra, int64_t* time_100ns, std::string* error) {
  Impl& m = *impl_;
  const int got = m.in.next(m.frame, m.packet, &m.draining);
  if (got <= 0) { if (got < 0 && error) *error = "video decode failed"; return got; }
  const int w = m.in.codec->width, h = m.in.codec->height;
  m.sws = sws_getCachedContext(m.sws, m.frame->width, m.frame->height, (AVPixelFormat)m.frame->format, w, h,
                               AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!m.sws) { if (error) *error = "cannot convert the decoded frame"; return -1; }
  m.top_down.resize((size_t)w * h * 4);
  uint8_t* dst[1] = {m.top_down.data()};
  const int dst_stride[1] = {w * 4};
  sws_scale(m.sws, m.frame->data, m.frame->linesize, 0, m.frame->height, dst, dst_stride);
  bgra.resize(m.top_down.size());
  const size_t row = (size_t)w * 4;
  for (int y = 0; y < h; ++y) std::memcpy(bgra.data() + (size_t)y * row, m.top_down.data() + (size_t)(h - 1 - y) * row, row);
  const AVRational tb = m.in.format->streams[m.in.stream]->time_base;
  const int64_t pts = m.frame->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : m.frame->best_effort_timestamp;
  if (time_100ns) *time_100ns = av_rescale_q(pts, tb, AVRational{1, 10000000});
  av_frame_unref(m.frame);
  return 1;
}
bool VideoReader::rewind() {
  Impl& m = *impl_;
  m.draining = false;
  avcodec_flush_buffers(m.in.codec);
  return av_seek_frame(m.in.format, m.in.stream, 0, AVSEEK_FLAG_BACKWARD) >= 0;
}

}  // namespace linux_media
