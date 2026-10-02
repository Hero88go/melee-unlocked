// SPDX-License-Identifier: GPL-2.0-or-later
#include "net_trace.h"
#include "net_trace_file.h"
#include "host.h"
#include <atomic>
#include <mutex>

namespace net_trace {
namespace {

std::mutex g_mutex;
Ring g_ring;
std::atomic<uint32_t> g_presents{0};
std::atomic<bool> g_file_open{false};   // a match's trace file is taking rows
std::atomic<bool> g_file_used{false};   // the writer exists (it starts with the first online match)

Writer& writer() {
  static Writer w([](const char* line) { host::log("%s", line); });
  return w;
}

}  // namespace

void begin_match() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_ring.clear();
}

void push(const Record& record) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ring.push(record);
  }
  if (g_file_open.load(std::memory_order_relaxed)) writer().add(record);
}

size_t snapshot(Record* out, size_t max) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ring.copy(out, max);
}

void note_present() { g_presents.fetch_add(1, std::memory_order_relaxed); }
uint32_t take_presents() { return g_presents.exchange(0, std::memory_order_relaxed); }

void file_begin(const char* replay_dir, const char* slp_path) {
  g_file_used.store(true);
  writer().begin(replay_dir ? replay_dir : "", slp_path ? slp_path : "");
  g_file_open.store(true);
}

void file_end() {
  if (g_file_open.exchange(false)) writer().end();
}

void replay_saved(const char* slp_path) {
  if (!slp_path || !g_file_used.load()) return;
  writer().replay_saved(slp_path);
  // The replay is written when its match is over, or when the game closes in the middle of one.
  // The trace is finished here and now, so a closing game does not leave a ".part" file behind.
  if (g_file_open.exchange(false)) writer().end();
  writer().wait_idle();
}

}  // namespace net_trace
