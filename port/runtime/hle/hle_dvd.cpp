// DVD HLE: file reads served from the ISO, completions delivered at guest wait points.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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
void do_file_read(uint32_t block, uint32_t addr, uint32_t length, uint32_t file_start,
                  uint32_t file_offset) {
  host::SimCostScope cost(host::SIM_DVD);
  // A destination outside memory: the drive's transfer lands nowhere and the read still completes.
  uint8_t* dest = hle::guest_buffer("DVDReadPrio", addr, length);
  if (!dest) { finish_read(block, addr, length, file_start + file_offset); return; }
  if (!host::disc_read_file(file_start, file_offset, dest, length))
    host::die("disc file read failed: start %08X offset %X length %X to %08X",
              file_start, file_offset, length, addr);
  finish_read(block, addr, length, file_start + file_offset);
}

// Asynchronous reads run on a worker so a stage load (tens of MB) never stalls the simulation
// thread for long (which starves audio). Completion is delivered at a fixed *virtual* time after
// the request (a quarter frame), in request order, so the guest sees deterministic timing; if
// the worker has not finished by then the simulation waits for it, as it used to for every read.
struct AsyncRead {
  uint32_t block, addr, length, disc_offset, callback;
  uint32_t file_start, file_offset;
  bool file_info;
  uint64_t ready_tb;
  std::shared_ptr<std::atomic<bool>> done;
  // The worker reads into this buffer; the bytes reach guest memory on the sim thread when the read
  // completes (dvd_poll), as a console's DMA has finished by the time its callback runs. Written
  // straight into guest memory from the worker, a read landed at a different moment in each run and
  // still landed after DVDCancelAsync, into memory the game may already have reused.
  std::shared_ptr<std::vector<uint8_t>> data;
};
std::mutex g_dvd_mutex;
std::condition_variable g_dvd_cv, g_dvd_done_cv;
std::deque<AsyncRead> g_dvd_queue;      // for the worker
std::deque<AsyncRead> g_dvd_pending;    // in request order, waiting for their virtual completion time (sim thread only)
std::thread g_dvd_thread;
bool g_dvd_started = false;

void dvd_worker() {
  for (;;) {
    AsyncRead r;
    { std::unique_lock<std::mutex> lk(g_dvd_mutex); g_dvd_cv.wait(lk, [] { return !g_dvd_queue.empty(); }); r = g_dvd_queue.front(); g_dvd_queue.pop_front(); }
    r.data->resize(r.length);
    bool ok = r.file_info
        ? host::disc_read_file(r.file_start, r.file_offset, r.data->data(), r.length)
        : host::disc_read(r.disc_offset, r.data->data(), r.length);
    if (!ok) host::die("disc read failed: offset %08X length %X to %08X", r.disc_offset, r.length, r.addr);
    { std::lock_guard<std::mutex> lk(g_dvd_mutex); r.done->store(true, std::memory_order_release); }
    g_dvd_done_cv.notify_all();
  }
}
void start_read(AsyncRead r) {
  host::wr32(r.block + 0x08, DVD_COMMAND_READ);
  host::wr32(r.block + 0x0C, DVD_STATE_BUSY);
  r.ready_tb = host::cpu->tb + host::TB_PER_FRAME / 4;
  r.done = std::make_shared<std::atomic<bool>>(false);
  r.data = std::make_shared<std::vector<uint8_t>>();
  // Checked now, where the request is made and the caller is known. A destination outside memory is
  // logged and the read still completes on time; its bytes are dropped in dvd_poll.
  (void)hle::guest_buffer("DVDReadAsync", r.addr, r.length);
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
    if (!r.done->load(std::memory_order_acquire)) { host::SimCostScope cost(host::SIM_DVD); std::unique_lock<std::mutex> lk(g_dvd_mutex); g_dvd_done_cv.wait(lk, [&] { return r.done->load(std::memory_order_acquire); }); }
    uint8_t* dest = host::try_ptr(r.addr, r.length);   // null was logged by start_read
    if (dest) std::memcpy(dest, r.data->data(), r.length);
    // The renderer reuses a texture snapshot while this memory's write version is unchanged, so the
    // copy must count as a write (as host::disc_read does when it reads straight into guest memory).
    // Without it a texture drawn before its file arrived stayed blank (0.8.6: black track names).
    // MELEE_TEST_DVD_NO_MARK=1 (tests): the 0.8.6 behaviour, to prove the stale-snapshot check.
    static const bool no_mark = [] { const char* v = std::getenv("MELEE_TEST_DVD_NO_MARK"); return v && *v == '1'; }();
    if (dest && !no_mark) host::mark_ram_write(r.addr, r.length);
    finish_read(r.block, r.addr, r.length, r.disc_offset);
    if (r.callback) { uint32_t cb = r.callback, len = r.length, blk = r.block; host::post_completion([cb, len, blk] { host::call_guest(cb, len, blk); }); }
    g_dvd_pending.pop_front();
  }
}
// The worker copies into guest RAM on its own clock, so a read that straddles a retrace is either in
// RAM or not when the state trace hashes it, depending on how busy the machine is. The game cannot
// tell (it does not look before the completion), but the trace can, and it showed up as one
// mismatching checkpoint in a loaded run. Only the trace calls this.
void dvd_settle() {
  for (AsyncRead& r : g_dvd_pending) {
    if (r.done->load(std::memory_order_acquire)) continue;
    std::unique_lock<std::mutex> lk(g_dvd_mutex);
    g_dvd_done_cv.wait(lk, [&] { return r.done->load(std::memory_order_acquire); });
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
  if ((uint64_t)start + offset > UINT32_MAX) host::die("disc file read offset overflow");
  start_read(AsyncRead{info, addr, length, start + offset, callback, start, offset, true});
  RET(1);
}

// s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio)
HLE(DVDReadPrio) {
  uint32_t info = ARG0, addr = ARG1, length = ARG2, offset = ARG3;
  uint32_t start = host::rd32(info + 0x30);
  TRACE("DVDReadPrio info=%08X addr=%08X len=%X off=%X", info, addr, length, offset);
  if ((uint64_t)start + offset > UINT32_MAX) host::die("disc file read offset overflow");
  do_file_read(info, addr, length, start, offset);
  RET(length);
}

// BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback cb, s32 prio)
HLE(DVDReadAbsAsyncPrio) {
  uint32_t block = ARG0, addr = ARG1, length = ARG2, offset = ARG3, callback = ARG4;
  host::pump_completions();
  host::wr32(block + 0x28, callback);
  TRACE("DVDReadAbsAsyncPrio block=%08X addr=%08X len=%X off=%X", block, addr, length, offset);
  start_read(AsyncRead{block, addr, length, offset, callback, 0, 0, false});
  RET(1);
}

HLE(DVDGetCommandBlockStatus) { host::pump_completions(); RET(host::rd32(ARG0 + 0x0C)); }
HLE(DVDCheckDisk) { host::pump_completions(); RET(1); }
HLE(DVDGetDriveStatus) { host::pump_completions(); RET(0); }
HLE(DVDGetCurrentDiskID) { RET(0x80000000u); }
// A cancelled read never reaches guest memory: the game may reuse the buffer at once.
static bool dvd_cancel_pending(uint32_t block) {
  for (auto it = g_dvd_pending.begin(); it != g_dvd_pending.end(); ++it) {
    if (it->block != block) continue;
    host::log("dvd: read into %08X (%X bytes) cancelled before it completed", it->addr, it->length);
    g_dvd_pending.erase(it);
    return true;
  }
  return false;
}
HLE(DVDCancelAsync) { dvd_cancel_pending(ARG0); if (ARG1) { uint32_t cb = ARG1, block = ARG0; host::post_completion([cb, block] { host::call_guest(cb, 0, block); }); } RET(1); }
HLE(DVDCancel) { dvd_cancel_pending(ARG0); RET(0); }
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
