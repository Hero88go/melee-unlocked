// When the display's next vertical blank comes, for presenting in step with a fixed-refresh display
// (G-Sync and FreeSync off). A helper thread waits on the output's own vertical blank and keeps the
// times it woke; the period is the median of the gaps, and the blank's position is the earliest of
// the recent wakes projected forward (a late wake only ever adds delay, never removes it).
// Healthy only while blanks keep arriving at a period that agrees with the monitor's rate; the
// presentation loop falls back to its own clock otherwise.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "host.h"
#include "vblank_model.h"

namespace gx {

class VBlankClock {
 public:
  ~VBlankClock() { stop(); }
  // Follows the output the swap chain is on; a move to another monitor restarts the clock.
  void track(IDXGISwapChain* swapchain) {
    if (!swapchain) return;
    IDXGIOutput* output = nullptr;
    if (FAILED(swapchain->GetContainingOutput(&output)) || !output) return;
    DXGI_OUTPUT_DESC desc{};
    output->GetDesc(&desc);
    if (thread_.joinable() && desc.Monitor == monitor_) { output->Release(); return; }
    stop();
    monitor_ = desc.Monitor;
    { std::lock_guard<std::mutex> lock(mutex_); model_.clear(); failures_ = 0; }
    running_ = true;
    thread_ = std::thread([this, output] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);   // it only sleeps in the wait
      while (running_.load(std::memory_order_relaxed)) {
        const HRESULT hr = output->WaitForVBlank();
        const double now = host::now_seconds();
        if (FAILED(hr)) {
          { std::lock_guard<std::mutex> lock(mutex_); ++failures_; }
          Sleep(5);
          continue;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        model_.add(now);
      }
      output->Release();
    });
  }
  void stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    monitor_ = nullptr;
  }
  // The first blank at or after `after`, if the clock is healthy against the nominal refresh.
  // `why` says what is wrong when it is not.
  bool next_after(double after, double nominal_hz, double* when, double* period, std::string* why) {
    double p = 0, last = 0, newest = 0; unsigned failures = 0; bool ok;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ok = model_.fit(&p, &last);
      newest = model_.newest();
      failures = failures_;
    }
    if (!thread_.joinable()) { *why = "no output"; return false; }
    if (failures > 8) { *why = "the vertical blank wait fails on this output"; return false; }
    if (!ok) { *why = "learning the vertical blank"; return false; }
    if (host::now_seconds() - newest > 4.0 * p) { *why = "vertical blanks stopped arriving"; return false; }
    if (nominal_hz > 0 && std::fabs(1.0 / p - nominal_hz) > 0.01 * nominal_hz) {
      char text[96]; std::snprintf(text, sizeof text, "blanks every %.3f ms disagree with %.2f Hz", p * 1000.0, nominal_hz);
      *why = text; return false;
    }
    const double k = std::max(0.0, std::ceil((after - last) / p));
    *when = last + k * p; *period = p;
    return true;
  }

 private:
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  VBlankModel model_;
  unsigned failures_ = 0;
  HMONITOR monitor_ = nullptr;
};

}  // namespace gx
