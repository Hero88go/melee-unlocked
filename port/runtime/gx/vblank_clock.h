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
#include <cstdint>
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
    track_output(output, desc.Monitor);
  }
  // Follows the output showing `monitor`, for a renderer without a DXGI swap chain (Vulkan): the
  // output is found through DXGI's adapter list, and the wait itself is the same.
  void track(HMONITOR monitor) {
    if (!monitor) return;
    if (thread_.joinable() && monitor == monitor_) return;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory) return;
    IDXGIOutput* found = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; !found && factory->EnumAdapters1(a, &adapter) == S_OK; ++a) {
      IDXGIOutput* output = nullptr;
      for (UINT o = 0; !found && adapter->EnumOutputs(o, &output) == S_OK; ++o) {
        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) found = output;
        else output->Release();
      }
      adapter->Release();
    }
    factory->Release();
    if (found) track_output(found, monitor);
  }
  void stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    monitor_ = nullptr;
  }
  // Vertical blanks seen since the clock started on this output (for frames-per-refresh counting,
  // vrr_detect.h, where no DXGI frame statistics exist).
  uint64_t wakes() const { return wakes_.load(std::memory_order_relaxed); }
  // Blanks are arriving: the thread runs, the waits succeed, and the newest blank is recent.
  bool healthy() const {
    if (!thread_.joinable()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return failures_ <= 8 && model_.newest() > 0.0 && host::now_seconds() - model_.newest() < 0.25;
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
  // Takes ownership of `output` (released when the thread ends).
  void track_output(IDXGIOutput* output, HMONITOR monitor) {
    stop();
    monitor_ = monitor;
    { std::lock_guard<std::mutex> lock(mutex_); model_.clear(); failures_ = 0; }
    wakes_.store(0, std::memory_order_relaxed);
    running_ = true;
    thread_ = std::thread([this, output] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);   // it only sleeps in the wait
      double last_wake = 0;
      while (running_.load(std::memory_order_relaxed)) {
        const HRESULT hr = output->WaitForVBlank();
        const double now = host::now_seconds();
        if (FAILED(hr)) {
          { std::lock_guard<std::mutex> lock(mutex_); ++failures_; }
          Sleep(5);
          continue;
        }
        // A display that is asleep or switched off returns from the wait at once instead of waiting
        // for a blank (seen 2026-11-05: millions of wakes a second, a core spinning at this thread's
        // priority). No display refreshes faster than about every 1.7 ms (600 Hz), so a return within
        // a millisecond of the last is not a blank: it is not counted, and the thread sleeps instead.
        // The time after the sleep is the reference, so the next immediate return is caught too.
        if (now - last_wake < 0.001) { Sleep(10); last_wake = host::now_seconds(); continue; }
        last_wake = now;
        wakes_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        model_.add(now);
      }
      output->Release();
    });
  }
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> wakes_{0};
  mutable std::mutex mutex_;
  VBlankModel model_;
  unsigned failures_ = 0;
  HMONITOR monitor_ = nullptr;
};

}  // namespace gx
