// OS-level HLE: console output, thread waits, contexts, exit.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstdlib>
#include <cstring>
#include <string>

// int __write_console(u32 handle, u8* buf, u32* count, void (*idle)(void))
HLE(__write_console) {
  uint32_t buf = ARG1, count_ptr = ARG2;
  // Console text is only for the log, so a count or text pointer outside memory prints nothing
  // instead of ending the game.
  const uint8_t* count = hle::guest_buffer("__write_console", count_ptr, 4);
  if (!count) { RET(0); return; }
  uint32_t n = host::rd32(count_ptr);
  const uint8_t* text = hle::guest_buffer("__write_console", buf, n);
  if (!text) { RET(0); return; }
  std::string s((const char*)text, n);
  host::log_guest_text(s.data(), s.size());
  RET(0);
}
HLE(__read_console) { host::wr32(ARG2, 0); RET(0); }
HLE(exit) {
  host::log("guest called exit(%d)", (int)ARG0);
  host::request_exit((int)ARG0);
  throw ExitRequested{(int)ARG0};
}
HLE(OSResetSystem) {
  host::log("OSResetSystem(%u, %u, %u)", ARG0, ARG1, ARG2);
  host::request_exit(0);
  throw ExitRequested{0};
}
HLE(OSPanic) {
  std::string file = host::cstr(ARG0), msg = host::cstr(ARG2);
  if (ARG1 == 233 && file == "lbmemory.c") host::report_heap_panic(c);
  if (ARG1 == 52 && file == "memory.c") host::report_object_memory();
  host::report_recent_files();
  // On a mod disc this stop is the screen being loaded not fitting in the game's memory (an older
  // Akaneia's character select, for one: the log above lists the blocks). The game's own text is
  // empty, so the player is told what it means; the start of the line is unchanged for grouping.
  if (ARG1 == 52 && file == "memory.c" && msg.empty() && host::mod_disc_active())
    host::die("OSPanic at memory.c:52: this mod ran out of the game's memory while loading a screen. "
              "An older build of a mod can need more than fits here: try the mod's newest build");
  host::die("OSPanic at %s:%u: %s", file.c_str(), ARG1, msg.c_str());
}
// HSD_MemAlloc (sysdolphin memory.c), as the game has it: nothing for a size of 0 or less, else
// OSAllocFromHeap on HSD_GetHeap's heap, and the assert at line 52 when that heap is full. Before
// the assert, the heap is given space above the console's memory (host::grow_object_heap) and the
// allocation tried once more; everything that fits in the console's memory is untouched by that.
// MELEE_TEST_HEAP_FILL (tests only, hidden runs): before each allocation, any free piece of the heap
// that still lies in the console's memory is used up, so everything the game allocates from then on
// comes from the space above it: models, textures and a mod's code all run from there.
static void test_fill_console_part(ppc::Context& c, uint8_t* m, uint32_t heap) {
  static const bool on = std::getenv("MELEE_TEST_HEAP_FILL") && host::options.no_gc_adapter;
  if (!on) return;
  constexpr uint32_t kAllocFromHeap = 0x80343EF0u, kConsoleEnd = ppc::RAM_BASE + ppc::CONSOLE_RAM_SIZE;
  const uint32_t heaps = host::rd32(0x804D7360u);
  uint32_t filled = 0;
  for (uint32_t piece : {0x10000u, 0x1000u, 0x100u, 0x20u})
    for (int guard = 0; guard < 4096; ++guard) {
      uint32_t cell = host::rd32(heaps + heap * 12u + 4u), room = 0;
      for (int k = 0; cell && cell < kConsoleEnd && k < 100000; ++k) {
        if (host::rd32(cell + 8u) >= piece + 32u) { room = cell; break; }
        cell = host::rd32(cell + 4u);
      }
      if (!room) break;
      c.r[3] = heap; c.r[4] = piece;
      ppc::call(c, m, kAllocFromHeap);
      if (!c.r[3] || c.r[3] >= kConsoleEnd) break;
      filled += piece;
    }
  if (filled) host::log("test: heap %u: %u bytes of the console's memory filled", heap, filled);
}

HLE(HSD_MemAlloc) {
  constexpr uint32_t kGetHeap = 0x80375404u, kAllocFromHeap = 0x80343EF0u, kAssert = 0x80388220u;
  const int32_t size = (int32_t)ARG0;
  if (size <= 0) { RET(0); return; }
  const uint32_t lr = c.lr;
  ppc::call(c, m, kGetHeap);
  const uint32_t heap = c.r[3];
  test_fill_console_part(c, m, heap);
  c.r[3] = heap; c.r[4] = (uint32_t)size;
  ppc::call(c, m, kAllocFromHeap);
  uint32_t adr = c.r[3];
  if (!adr && host::grow_object_heap(heap, (uint32_t)size)) {
    c.r[3] = heap; c.r[4] = (uint32_t)size;
    ppc::call(c, m, kAllocFromHeap);
    adr = c.r[3];
  }
  if (!adr) {
    c.r[3] = 0x80407300u;            // "memory.c"
    c.r[4] = 52u;
    c.r[5] = c.r[13] - 22320u;       // "adr"
    ppc::call(c, m, kAssert);
  }
  c.lr = lr;
  RET(adr);
}
HLE(__OSUnhandledException) {
  host::die("__OSUnhandledException %u (context %08X dsisr %08X dar %08X)", ARG0, ARG1, ARG2, ARG3);
}
HLE(__OSInitAudioSystem) {}
HLE(__OSStopAudioSystem) {}

// Threads: Melee runs one thread. Sleeping means "wait for the next hardware event".
HLE(OSSleepThread) { host::wait_event(); }
HLE(OSWakeupThread) {}
HLE(OSYieldThread) {}
HLE(__OSReschedule) {}
HLE(OSCreateThread) { host::log("OSCreateThread ignored (entry %08X)", ARG1); RET(0); }
HLE(OSResumeThread) { RET(0); }
HLE(OSSuspendThread) { RET(0); }
HLE(OSJoinThread) { RET(0); }

// OSLoadContext never returns on hardware; unwind to the host interrupt dispatcher.
HLE(OSLoadContext) { throw LoadContextUnwind{ARG0}; }
