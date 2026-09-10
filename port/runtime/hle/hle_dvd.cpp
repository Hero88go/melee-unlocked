// DVD HLE: file reads served from the ISO, completions delivered at guest wait points.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstring>

namespace {
constexpr uint32_t DVD_STATE_END = 0, DVD_STATE_BUSY = 1;
constexpr uint32_t DVD_COMMAND_READ = 1;

// DVDFileInfo: cb (0x30 bytes) + startAddr(0x30) + length(0x34) + callback(0x38)
void do_read(uint32_t block, uint32_t addr, uint32_t length, uint32_t disc_offset) {
  if (!host::disc_read(disc_offset, host::ptr(addr, length), length))
    host::die("disc read failed: offset %08X length %X to %08X", disc_offset, length, addr);
  host::wr32(block + 0x08, DVD_COMMAND_READ);
  host::wr32(block + 0x0C, DVD_STATE_END);
  host::wr32(block + 0x10, disc_offset);
  host::wr32(block + 0x14, length);
  host::wr32(block + 0x18, addr);
  host::wr32(block + 0x1C, length);
  host::wr32(block + 0x20, length);
}
}  // namespace

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
  do_read(info, addr, length, start + offset);
  if (callback) host::post_completion([callback, info, length] { host::call_guest(callback, length, info); });
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
  do_read(block, addr, length, offset);
  if (callback) host::post_completion([callback, block, length] { host::call_guest(callback, length, block); });
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
