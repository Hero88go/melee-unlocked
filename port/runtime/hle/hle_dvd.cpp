// DVD HLE: file reads served from the ISO, completions delivered at guest wait points.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {
constexpr uint32_t DVD_STATE_END = 0, DVD_STATE_BUSY = 1;
constexpr uint32_t DVD_COMMAND_READ = 1;

// DVDFileInfo: cb (0x30 bytes) + startAddr(0x30) + length(0x34) + callback(0x38)
void finish_read(uint32_t block, uint32_t addr, uint32_t length, uint32_t disc_offset) {
  host::wr32(block + 0x08, DVD_COMMAND_READ);
  host::wr32(block + 0x0C, DVD_STATE_END);
  host::wr32(block + 0x10, disc_offset);
  host::wr32(block + 0x14, length);
  host::wr32(block + 0x18, addr);
  host::wr32(block + 0x1C, length);
  host::wr32(block + 0x20, length);
}
void do_read(uint32_t block, uint32_t addr, uint32_t length, uint32_t disc_offset) {
  if (!host::disc_read(disc_offset, host::ptr(addr, length), length))
    host::die("disc read failed: offset %08X length %X to %08X", disc_offset, length, addr);
  finish_read(block, addr, length, disc_offset);
}

// Asynchronous reads run on a worker so a stage load (tens of MB) never stalls the simulation
// thread for long (which starves audio). Completion is delivered at a fixed *virtual* time after
// the request (a quarter frame), in request order, so the guest sees deterministic timing; if
// the worker has not finished by then the simulation waits for it, as it used to for every read.
struct AsyncRead {
  uint32_t block, addr, length, disc_offset, callback; bool file_info;
  uint64_t ready_tb;
  std::shared_ptr<std::atomic<bool>> done;
};
std::mutex g_dvd_mutex;
std::condition_variable g_dvd_cv;
std::deque<AsyncRead> g_dvd_queue;      // for the worker
std::deque<AsyncRead> g_dvd_pending;    // in request order, waiting for their virtual completion time (sim thread only)
std::thread g_dvd_thread;
bool g_dvd_started = false;

void dvd_worker() {
  for (;;) {
    AsyncRead r;
    { std::unique_lock<std::mutex> lk(g_dvd_mutex); g_dvd_cv.wait(lk, [] { return !g_dvd_queue.empty(); }); r = g_dvd_queue.front(); g_dvd_queue.pop_front(); }
    if (!host::disc_read(r.disc_offset, host::ptr(r.addr, r.length), r.length)) host::die("disc read failed: offset %08X length %X to %08X", r.disc_offset, r.length, r.addr);
    r.done->store(true, std::memory_order_release);
  }
}
void start_read(AsyncRead r) {
  host::wr32(r.block + 0x08, DVD_COMMAND_READ);
  host::wr32(r.block + 0x0C, DVD_STATE_BUSY);
  r.ready_tb = host::cpu->tb + host::TB_PER_FRAME / 4;
  r.done = std::make_shared<std::atomic<bool>>(false);
  g_dvd_pending.push_back(r);
  std::lock_guard<std::mutex> lk(g_dvd_mutex);
  if (!g_dvd_started) { g_dvd_started = true; g_dvd_thread = std::thread(dvd_worker); g_dvd_thread.detach(); }
  g_dvd_queue.push_back(r);
  g_dvd_cv.notify_one();
}
}  // namespace

namespace hle {
// Called from the simulation thread at every wait point.
void dvd_poll() {
  while (!g_dvd_pending.empty()) {
    AsyncRead& r = g_dvd_pending.front();
    if (host::cpu->tb < r.ready_tb) return;
    while (!r.done->load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::microseconds(50));
    finish_read(r.block, r.addr, r.length, r.disc_offset);
    if (r.callback) { uint32_t cb = r.callback, len = r.length, blk = r.block; host::post_completion([cb, len, blk] { host::call_guest(cb, len, blk); }); }
    g_dvd_pending.pop_front();
  }
}
}  // namespace hle

HLE(DVDInit) {
  // Only the filesystem tables need initialising; everything else is host-side.
  host::call_guest(gs::__DVDFSInit);
  host::wr32(0x80000000u + 0, host::rd32(0x80000000u));  // keep disc id (no-op, documents intent)
  TRACE("DVDInit");
}

// BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback cb, s32 prio)
HLE(DVDReadAsyncPrio) {
  uint32_t info = ARG0, addr = ARG1, length = ARG2, offset = ARG3, callback = ARG4;
  host::pump_completions();
  uint32_t start = host::rd32(info + 0x30);
  host::wr32(info + 0x38, callback);
  TRACE("DVDReadAsyncPrio info=%08X addr=%08X len=%X off=%X cb=%08X", info, addr, length, offset, callback);
  start_read(AsyncRead{info, addr, length, start + offset, callback, true});
  RET(1);
}

// s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio)
HLE(DVDReadPrio) {
  uint32_t info = ARG0, addr = ARG1, length = ARG2, offset = ARG3;
  uint32_t start = host::rd32(info + 0x30);
  TRACE("DVDReadPrio info=%08X addr=%08X len=%X off=%X", info, addr, length, offset);
  do_read(info, addr, length, start + offset);
  RET(length);
}

// BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback cb, s32 prio)
HLE(DVDReadAbsAsyncPrio) {
  uint32_t block = ARG0, addr = ARG1, length = ARG2, offset = ARG3, callback = ARG4;
  host::pump_completions();
  host::wr32(block + 0x28, callback);
  TRACE("DVDReadAbsAsyncPrio block=%08X addr=%08X len=%X off=%X", block, addr, length, offset);
  start_read(AsyncRead{block, addr, length, offset, callback, false});
  RET(1);
}

HLE(DVDGetCommandBlockStatus) { host::pump_completions(); RET(host::rd32(ARG0 + 0x0C)); }
HLE(DVDCheckDisk) { host::pump_completions(); RET(1); }
HLE(DVDGetDriveStatus) { host::pump_completions(); RET(0); }
HLE(DVDGetCurrentDiskID) { RET(0x80000000u); }
HLE(DVDCancelAsync) { if (ARG1) { uint32_t cb = ARG1, block = ARG0; host::post_completion([cb, block] { host::call_guest(cb, 0, block); }); } RET(1); }
HLE(DVDCancel) { RET(0); }
HLE(DVDReset) {}
HLE(DVDPrepareStreamAsync) { RET(0); }
HLE(DVDPrepareStream) { RET(0); }
HLE(DVDCancelStreamAsync) { RET(0); }
HLE(DVDCancelStream) { RET(0); }
HLE(DVDStopStreamAtEndAsync) { RET(0); }
HLE(DVDGetStreamPlayAddrAsync) { RET(0); }
HLE(DVDGetStreamStartAddrAsync) { RET(0); }
HLE(DVDGetStreamLengthAsync) { RET(0); }
HLE(DVDGetStreamErrorStatusAsync) { RET(0); }
HLE(DVDSeekAsyncPrio) { RET(1); }
