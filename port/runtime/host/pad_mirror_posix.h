// Read-only, borrowed-fd YGPM reader. No window, USB or platform shim dependencies.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../hle/yougame_pad_mirror.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>

namespace yougame::pad_mirror {
struct alignas(8) PadMirror {
  char magic[4];
  uint32_t version;
  uint32_t seq;
  uint32_t reports;
  double at_ms;
  float rate_hz;
  uint8_t connected;
  uint8_t report[37];
  uint8_t reserved[2];
  uint32_t rumble_seq;
  uint8_t rumble[4];
};
static_assert(offsetof(PadMirror, seq) == 8 && offsetof(PadMirror, report) == 29 &&
              offsetof(PadMirror, rumble_seq) == 68 && sizeof(PadMirror) == 80,
              "the helper's layout");
static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr), "shared sequence needs lock-free loads");

class Reader {
 public:
  using Clock = std::chrono::steady_clock;
  Reader() = default;
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  ~Reader() { reset(); }
  void reset() {
    if (mirror_) munmap(const_cast<PadMirror*>(mirror_), size_);
    mirror_ = nullptr;
    size_ = 0;
    reports_ = 0;
    moved_ = {};
    rate_ = 0;
  }
  bool attach(Attachment info) {
    reset();
    struct stat st{};
    if (info.fd < 3 || info.size < sizeof(PadMirror)) { errno = EINVAL; return false; }
    if (fstat(info.fd, &st)) return false;
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<uintmax_t>(st.st_size) < info.size) {
      errno = EINVAL; return false;
    }
    const int flags = fcntl(info.fd, F_GETFD);
    if (flags < 0 || fcntl(info.fd, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
    const int access = fcntl(info.fd, F_GETFL);
    if (access < 0) return false;
    if ((access & O_ACCMODE) != O_RDONLY) { errno = EACCES; return false; }
    void* view = mmap(nullptr, info.size, PROT_READ, MAP_SHARED, info.fd, 0);
    if (view == MAP_FAILED) return false;
    mirror_ = static_cast<const PadMirror*>(view);
    size_ = info.size;
    if (std::memcmp(mirror_->magic, "YGPM", 4) || mirror_->version != 1) {
      reset(); errno = EINVAL; return false;
    }
    return true;
  }

  template<class Reset, class Log> void attach_once(Attachment info, Reset reset_origins, Log log) {
    if (info.fd == -1 || (attempted_ && info == last_)) return;
    last_ = info;
    attempted_ = true;
    const bool ok = attach(info);
    const int error = errno;
    reset_origins();
    log(ok ? "gc adapter: reading the YouGame app's adapter" :
             "gc adapter: the YouGame app's adapter could not be read (error %d)", error);
  }

  struct Yield { void operator()() const { std::this_thread::yield(); } };
  template<class Retry = Yield>
  bool read(uint8_t rep[37], Clock::time_point now = Clock::now(), Retry retry = {}) {
    rate_ = 0;
    if (!mirror_) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
      const uint32_t before = __atomic_load_n(&mirror_->seq, __ATOMIC_ACQUIRE);
      if (before & 1) { retry(); continue; }
      // Atomic byte loads keep the snapshot loads explicit while the other process publishes.
      for (size_t i = 0; i < 37; ++i) rep[i] = __atomic_load_n(&mirror_->report[i], __ATOMIC_RELAXED);
      const bool connected = __atomic_load_n(&mirror_->connected, __ATOMIC_RELAXED) != 0;
      const uint32_t reports = __atomic_load_n(&mirror_->reports, __ATOMIC_RELAXED);
      float rate;
      __atomic_load(&mirror_->rate_hz, &rate, __ATOMIC_RELAXED);
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      if (__atomic_load_n(&mirror_->seq, __ATOMIC_ACQUIRE) != before) { retry(); continue; }
      if (reports != reports_) { reports_ = reports; moved_ = now; }
      const bool live = connected && reports && now - moved_ < std::chrono::milliseconds(500) && rep[0] == 0x21;
      rate_ = live ? rate : 0;
      return live;
    }
    return false;
  }
  bool attached() const { return mirror_ != nullptr; }
  double rate_hz() const { return rate_; }
  const PadMirror* view() const { return mirror_; }

 private:
  const PadMirror* mirror_ = nullptr;
  size_t size_ = 0;
  uint32_t reports_ = 0;
  Clock::time_point moved_{};
  double rate_ = 0;
  Attachment last_;
  bool attempted_ = false;
};

// Same calibration and raw report translation as the existing adapter path.
struct Origin { bool set = false; uint8_t sx = 128, sy = 128, cx = 128, cy = 128, tl = 0, tr = 0; };
template<class Pad> uint32_t decode(const uint8_t rep[37], Origin (&origins)[4], Pad* out) {
  uint32_t mask = 0;
  for (int port = 0; port < 4; ++port) {
    const uint8_t* c = rep + 1 + port * 9;
    Origin& o = origins[port];
    if (!(c[0] & 0x30)) { o.set = false; continue; }
    if (!o.set) o = {true, c[3], c[4], c[5], c[6], c[7], c[8]};
    Pad& p = out[port];
    p = {};
    p.button = ((c[1] & 0x0f) << 8) | ((c[1] & 0xf0) >> 4) |
               ((c[2] & 1) << 12) | ((c[2] & 0x0e) << 3);
    const auto axis = [](uint8_t v, uint8_t origin) {
      int a = int(v) - int(origin);
      return static_cast<int8_t>(a > 127 ? 127 : a < -128 ? -128 : a);
    };
    p.stick_x = axis(c[3], o.sx); p.stick_y = axis(c[4], o.sy);
    p.sub_x = axis(c[5], o.cx); p.sub_y = axis(c[6], o.cy);
    p.trig_l = c[7] > o.tl ? c[7] - o.tl : 0;
    p.trig_r = c[8] > o.tr ? c[8] - o.tr : 0;
    mask |= 1u << port;
  }
  return mask;
}
} // namespace yougame::pad_mirror
