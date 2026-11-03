// In-process presentation evidence, staging agent sessions only. No environment controls.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_paths.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace yougame::agent_present {
struct Config {
  std::string log, directory;
  unsigned every = 0;
  bool enabled() const { return !log.empty() || every != 0; }
};

inline bool plain_name(const std::string& name) {
  return agent_paths::relative_valid(name) && name != "." &&
      name.find_first_of("/\\") == std::string::npos;
}
inline bool frame_spec(const std::string& spec, std::string& directory, unsigned& every) {
  const auto colon = spec.find(':');
  if (colon == std::string::npos || !plain_name(spec.substr(0, colon))) return false;
  const auto count = spec.substr(colon + 1);
  if (count.empty() || count.size() > 3 || count.find_first_not_of("0123456789") != std::string::npos) return false;
  const auto n = static_cast<unsigned>(std::stoul(count));
  if (n < 1 || n > 600) return false;
  directory = spec.substr(0, colon); every = n;
  return true;
}

inline std::string header(uint64_t wall_ms) {
  return "present-log v1 wall_ms=" + std::to_string(wall_ms) +
      " fields=present us sim scene backbuffer window scale\n";
}
inline std::string line(uint64_t present, uint64_t us, uint32_t sim, uint8_t major, uint8_t minor,
                        int bw, int bh, int ww, int wh) {
  // Integer thousandths avoid locale-sensitive decimal formatting.
  const uint64_t scale = ww > 0 ? (uint64_t(bw) * 1000 + ww / 2) / ww : 0;
  char text[256];
  std::snprintf(text, sizeof text, "%llu %llu %u %02X:%02X %dx%d %dx%d %llu.%03llu\n",
      (unsigned long long)present, (unsigned long long)us, sim, unsigned(major), unsigned(minor),
      bw, bh, ww, wh, (unsigned long long)(scale / 1000), (unsigned long long)(scale % 1000));
  return text;
}
inline std::string frame_name(const std::string& directory, uint64_t present, uint64_t us) {
  return directory + "/" + std::to_string(present) + "-" + std::to_string(us) + ".ppm";
}
struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> pixels; // RGB, top row first
  std::string ppm_header() const {
    return "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
  }
};
// Area-weighted box average, including fractional edge coverage. GL input is bottom-up.
inline Image downscale(const uint8_t* rgb, int w, int h, int maximum_width = 480, bool bottom_up = true) {
  Image out;
  if (!rgb || w < 1 || h < 1 || maximum_width < 1) return out;
  out.width = std::min(w, maximum_width);
  out.height = std::max(1, int(int64_t(h) * out.width / w));
  out.pixels.resize(size_t(out.width) * out.height * 3);
  for (int y = 0; y < out.height; ++y) {
    const int64_t y0 = int64_t(y) * h, y1 = int64_t(y + 1) * h;
    for (int x = 0; x < out.width; ++x) {
      const int64_t x0 = int64_t(x) * w, x1 = int64_t(x + 1) * w;
      uint64_t sum[3]{};
      for (int sy = int(y0 / out.height); sy < (y1 + out.height - 1) / out.height; ++sy) {
        const auto wy = std::min(y1, int64_t(sy + 1) * out.height) - std::max(y0, int64_t(sy) * out.height);
        for (int sx = int(x0 / out.width); sx < (x1 + out.width - 1) / out.width; ++sx) {
          const auto wx = std::min(x1, int64_t(sx + 1) * out.width) - std::max(x0, int64_t(sx) * out.width);
          const auto at = (size_t(bottom_up ? h - 1 - sy : sy) * w + sx) * 3;
          for (int c = 0; c < 3; ++c) sum[c] += rgb[at + c] * uint64_t(wx * wy);
        }
      }
      for (int c = 0; c < 3; ++c)
        out.pixels[(size_t(y) * out.width + x) * 3 + c] = uint8_t(sum[c] / (uint64_t(w) * h));
    }
  }
  return out;
}

struct Budget {
  static constexpr uint64_t last_present = 18000, byte_limit = uint64_t(2) * 1024 * 1024 * 1024;
  uint64_t reserved = 0;
  bool stopped = false;
  const char* reserve(uint64_t present, uint64_t bytes) {
    if (stopped) return nullptr;
    const char* reason = present > last_present ? "18000 presents" :
        bytes > byte_limit - reserved ? "2 GiB limit" : nullptr;
    if (reason) stopped = true;
    else reserved += bytes;
    return reason;
  }
};

// One producer (render thread). All opens, writes, flushes and closes occur on the worker.
// Its mutex protects queue operations only, never filesystem I/O or image processing.
class Recorder {
 public:
  using Clock = std::chrono::steady_clock;
  ~Recorder() { stop(); }
  void stop() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    ready_.notify_one();
    if (worker_.joinable()) worker_.join();
    root_.reset(); config_ = {}; queue_.clear(); budget_ = {}; first_ = true;
    log_failed_ = false; frames_failed_ = false;
  }
  bool arm(const Config& config, const agent_paths::Root& root) {
    stop();
    if (!config.enabled()) return true;
    // Retain the already verified capability; never reopen the root by pathname.
#ifdef _WIN32
    if (!DuplicateHandle(GetCurrentProcess(), root.handle, GetCurrentProcess(), &root_.handle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) return false;
#else
    root_.fd = ::fcntl(root.fd, F_DUPFD_CLOEXEC, 0);
    if (root_.fd < 0) return false;
#endif
    root_.path = root.path; config_ = config; stopping_ = false;
    worker_ = std::thread([this] { write_loop(); });
    return true;
  }
  bool enabled() const { return config_.enabled(); }
  bool wants_frame(uint64_t present) {
    if (!config_.every || frames_failed_ || budget_.stopped) return false;
    if (const auto reason = budget_.reserve(present, 0)) enqueue_notice(reason);
    return !budget_.stopped && present % config_.every == 0;
  }
  void after_swap(uint64_t present, uint32_t sim, uint8_t major, uint8_t minor,
                  int bw, int bh, int ww, int wh, Image image = {},
                  Clock::time_point now = Clock::now(),
                  std::chrono::system_clock::time_point wall = std::chrono::system_clock::now()) {
    if (!enabled()) return;
    Job job;
    if (first_) {
      first_ = false; epoch_ = now;
      job.header = header(std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count());
    }
    const auto us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - epoch_).count());
    if (!config_.log.empty() && !log_failed_) job.line = line(present, us, sim, major, minor, bw, bh, ww, wh);
    job.flush = present % 60 == 0;
    if (!image.pixels.empty() && !frames_failed_ && !budget_.stopped) {
      const auto bytes = image.ppm_header().size() + image.pixels.size();
      if (const auto reason = budget_.reserve(present, bytes)) job.notice = reason;
      else {
        job.name = frame_name(config_.directory, present, us); job.image = std::move(image);
        if (budget_.reserved == Budget::byte_limit) { budget_.stopped = true; job.notice = "2 GiB limit"; }
      }
    }
    if (present == Budget::last_present && !budget_.stopped && config_.every) {
      budget_.stopped = true; job.notice = "18000 presents";
    }
    enqueue(std::move(job));
  }
 private:
  struct Job {
    std::string header, line, name, notice;
    Image image;
    bool flush = false;
  };
  Config config_;
  agent_paths::Root root_;
  Budget budget_;
  bool first_ = true, stopping_ = true;
  std::atomic<bool> log_failed_{false}, frames_failed_{false};
  Clock::time_point epoch_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Job> queue_;
  std::thread worker_;
  void enqueue(Job job) {
    { std::lock_guard<std::mutex> lock(mutex_); queue_.push_back(std::move(job)); }
    ready_.notify_one();
  }
  void enqueue_notice(const char* reason) { Job job; job.notice = reason; enqueue(std::move(job)); }
  void write_loop() {
    FILE* log = nullptr;
    std::array<char, 64 * 1024> buffer{};
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) break;
        job = std::move(queue_.front()); queue_.pop_front();
      }
      if (!job.header.empty() && !config_.log.empty()) {
        log = agent_paths::open(root_.path, config_.log, agent_paths::Mode::Truncate, false, root_);
        if (!log) {
          log_failed_ = true;
          std::fprintf(stderr, "agent-test: present_log open refused; ignored\n");
        } else {
          std::setvbuf(log, buffer.data(), _IOFBF, buffer.size());
          if (std::fwrite(job.header.data(), 1, job.header.size(), log) != job.header.size()) {
            log_failed_ = true;
            std::fprintf(stderr, "agent-test: present_log write failed; stopped\n");
          }
        }
      }
      if (log && !log_failed_) {
        if (std::fwrite(job.line.data(), 1, job.line.size(), log) != job.line.size() ||
            (job.flush && std::fflush(log) != 0)) {
          log_failed_ = true;
          std::fprintf(stderr, "agent-test: present_log write failed; stopped\n");
        }
      }
      if (!job.name.empty() && !frames_failed_) {
        FILE* frame = agent_paths::open(root_.path, job.name, agent_paths::Mode::Exclusive, true, root_);
        bool ok = false;
        if (frame) {
          const auto ppm = job.image.ppm_header();
          ok = std::fwrite(ppm.data(), 1, ppm.size(), frame) == ppm.size() &&
              std::fwrite(job.image.pixels.data(), 1, job.image.pixels.size(), frame) == job.image.pixels.size();
          if (std::fclose(frame)) ok = false;
        }
        if (!ok) {
          frames_failed_ = true;
          std::fprintf(stderr, "agent-test: present_frames write refused or failed; stopped\n");
        }
      }
      if (!job.notice.empty()) std::fprintf(stderr, "agent-test: present_frames stopped: %s\n", job.notice.c_str());
    }
    if (log && std::fclose(log)) std::fprintf(stderr, "agent-test: present_log close failed\n");
  }
};
inline Recorder& recorder() { static Recorder value; return value; }
} // namespace yougame::agent_present
#endif
