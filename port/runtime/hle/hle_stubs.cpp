// Hardware-library stubs: EXI, SI, AI, DSP, AX output, ARAM, memory card.
// These return "no device / done" so the game's init paths complete without hardware.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include "ax_ucode.h"
#include "audio.h"
#include "exi_slippi.h"
#include "memory_range.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>

// ---------------- EXI ----------------
// Channel 1 (memory card slot B) carries the Slippi device; channel 0 device 1 is the console's
// RTC and SRAM; other channels have no device.
static constexpr uint32_t SLIPPI_CHANNEL = 1;
static uint32_t s_exi_selected_dev[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

// ---- SRAM ----
// The console keeps its settings in 64 bytes of battery-backed RAM behind the RTC on channel 0
// device 1: sound mode, progressive scan, language, screen position. The game reads and writes it
// through the real SDK code (OSGetSoundMode and friends are not HLE'd), so all that is needed here
// is a device that remembers. Without one every read returned zeros, which is mono, English and
// interlaced, and every write was dropped: a player could set stereo, have it hold for the rest of
// the session because the SDK keeps its own copy in RAM, and find it mono again on the next start.
//
// Layout (dolphin/os/OSRtc.h, struct OSSram): checkSum, checkSumInv, ead0, ead1, counterBias,
// displayOffsetH, ntd, language, flags. Sound mode is bit 2 of flags at offset 19, progressive
// scan is bit 7 of the same byte. Nothing in the SDK validates the checksum when reading, so a
// fresh image only has to be the right size.
namespace {
constexpr uint32_t SRAM_SIZE = 64;
uint8_t s_sram[SRAM_SIZE];
bool s_sram_loaded = false;
bool s_sram_dirty = false;
uint32_t s_sram_offset = 0;      // byte offset the pending command selected
bool s_sram_writing = false;     // the pending command is a write

std::string sram_path() {
  // Beside the memory card, which is where this installation's other console state already lives.
  std::filesystem::path dir(host::options.card_dir);
  if (dir.has_parent_path()) dir = dir.parent_path();
  return (dir / "sram.bin").string();
}

void sram_load() {
  if (s_sram_loaded) return;
  s_sram_loaded = true;
  std::memset(s_sram, 0, sizeof s_sram);
  // A console leaves the factory set to stereo, and that is what a player expects to find.
  s_sram[19] = 0x04;
  bool from_file = false;
  if (FILE* in = std::fopen(sram_path().c_str(), "rb")) {
    uint8_t buf[SRAM_SIZE];
    if (std::fread(buf, 1, sizeof buf, in) == sizeof buf) { std::memcpy(s_sram, buf, sizeof s_sram); from_file = true; }
    std::fclose(in);
  }
  host::log("sram: %s, sound %s, %s scan", from_file ? "loaded" : "new",
            (s_sram[19] & 0x04) ? "stereo" : "mono",
            (s_sram[19] & 0x80) ? "progressive" : "interlaced");
}

void sram_save() {
  if (!s_sram_dirty) return;
  s_sram_dirty = false;
  const std::string path = sram_path();
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const std::string tmp = path + ".tmp";
  FILE* out = std::fopen(tmp.c_str(), "wb");
  if (!out) { host::log("sram: cannot write %s", path.c_str()); return; }
  const bool ok = std::fwrite(s_sram, 1, sizeof s_sram, out) == sizeof s_sram;
  std::fclose(out);
  if (ok) std::filesystem::rename(tmp, path, ec);
  if (!ok || ec) host::log("sram: failed to save %s", path.c_str());
}

bool exi_is_sram(uint32_t chan) { return chan == 0 && s_exi_selected_dev[0] == 1; }

// The first immediate write of a transfer carries the command. Bit 31 set means write; the address
// is the low 24 bits, where SRAM starts at 0x100 and each byte is one step of 0x40
// (dolphin/os/OSRtc.c: ReadSram sends 0x20000100, WriteSram sends 0xA0000000 | off<<6 | 0x100).
// The RTC shares the device and asks for 0x20000000, whose address is below SRAM, so it selects no
// byte and keeps reading zero exactly as it did before.
void sram_command(uint32_t cmd) {
  s_sram_writing = (cmd & 0x80000000u) != 0;
  const uint32_t addr = cmd & 0x00FFFFFFu;
  s_sram_offset = addr >= 0x100 ? (addr - 0x100) >> 6 : SRAM_SIZE;
}

void sram_transfer(uint32_t buf, uint32_t len, bool write) {
  sram_load();
  for (uint32_t i = 0; i < len; ++i) {
    const uint32_t off = s_sram_offset + i;
    if (off >= SRAM_SIZE) break;
    if (write) { s_sram[off] = host::rd8(buf + i); s_sram_dirty = true; }
    else host::wr8(buf + i, s_sram[off]);
  }
  if (write) sram_save();
}
}  // namespace

HLE(EXIInit) { sram_load(); }
HLE(EXIProbe) { RET(ARG0 == SLIPPI_CHANNEL || ARG0 == 0 ? 1 : 0); }
HLE(EXIProbeEx) { RET(ARG0 == SLIPPI_CHANNEL || ARG0 == 0 ? 1 : (uint32_t)-1); }
HLE(EXIGetID) { if (ARG0 == SLIPPI_CHANNEL && ARG1 == 0 && ARG2) host::wr32(ARG2, 0); RET(ARG0 == SLIPPI_CHANNEL ? 1 : 0); }
HLE(EXILock) { RET(1); }
HLE(EXIUnlock) { RET(1); }
HLE(EXISelect) { if (ARG0 < 3) s_exi_selected_dev[ARG0] = ARG1; RET(1); }
HLE(EXIDeselect) { if (ARG0 < 3) s_exi_selected_dev[ARG0] = 0xFFFFFFFF; RET(1); }
static bool exi_is_slippi(uint32_t chan) { return chan == SLIPPI_CHANNEL && s_exi_selected_dev[chan] == 0; }
HLE(EXIImm) {
  // (chan, buf, len, type, callback): type 0 read, 1 write, 2 read/write.
  uint32_t chan = ARG0, buf = ARG1, len = ARG2, type = ARG3;
  if (exi_is_slippi(chan)) {
    if (type != 0) { uint32_t data = 0; for (uint32_t i = 0; i < len && i < 4; ++i) data |= (uint32_t)host::rd8(buf + i) << (24 - 8 * i); slippi::imm_write(data, len); }
    if (type != 1) { uint32_t data = slippi::imm_read(len); for (uint32_t i = 0; i < len && i < 4; ++i) host::wr8(buf + i, (uint8_t)(data >> (24 - 8 * i))); }
  } else if (exi_is_sram(chan)) {
    // A 4-byte immediate write opens a transfer and names the byte; it carries no data of its own,
    // so it must not also be treated as a read. Everything else on this device is the RTC, whose
    // reads keep returning zero exactly as they did before.
    if (type == 1 && len == 4) {
      uint32_t cmd = 0;
      for (uint32_t i = 0; i < 4; ++i) cmd |= (uint32_t)host::rd8(buf + i) << (24 - 8 * i);
      sram_command(cmd);
    } else if (type != 0) {
      sram_transfer(buf, len, true);
    } else if (s_sram_offset < SRAM_SIZE) {
      sram_transfer(buf, len, false);
    } else {
      for (uint32_t i = 0; i < len && i < 4; ++i) host::wr8(buf + i, 0);
    }
  } else if (type != 1) {
    for (uint32_t i = 0; i < len && i < 4; ++i) host::wr8(buf + i, 0);
  }
  RET(1);
}
HLE(EXIImmEx) {
  uint32_t chan = ARG0, buf = ARG1, len = ARG2, type = ARG3;
  if (exi_is_slippi(chan)) {
    if (type != 0) slippi::dma_write(buf, len);
    if (type != 1) slippi::dma_read(buf, len);
  } else if (exi_is_sram(chan) && s_sram_offset < SRAM_SIZE) {
    sram_transfer(buf, len, type != 0);   // WriteSram's payload arrives here
  } else if (type != 1) {
    for (uint32_t i = 0; i < len; ++i) host::wr8(buf + i, 0);
  }
  RET(1);
}
HLE(EXIDma) {
  uint32_t chan = ARG0, buf = ARG1, len = ARG2, type = ARG3;
  if (exi_is_slippi(chan)) {
    if (type == 1) slippi::dma_write(buf, len);
    else slippi::dma_read(buf, len);
  } else if (exi_is_sram(chan) && s_sram_offset < SRAM_SIZE) {
    sram_transfer(buf, len, type == 1);   // ReadSram pulls the whole 64 bytes here
  } else if (type == 0) {
    for (uint32_t i = 0; i < len; ++i) host::wr8(buf + i, 0);
  }
  RET(1);
}
HLE(EXISync) { RET(1); }
HLE(EXIAttach) { RET(0); }
HLE(EXIDetach) { RET(1); }
HLE(EXIGetState) { RET(0); }
HLE(EXIClearInterrupts) {}
HLE(EXISetExiCallback) { RET(0); }

// ---------------- SI ----------------
HLE(SIInit) {}
HLE(SIRefreshSamplingRate) {}
HLE(SIGetType) { RET(0x08000000); }  // SI_GC_CONTROLLER
HLE(SIGetTypeAsync) { RET(0x08000000); }
HLE(SIEnablePolling) { RET(0); }
HLE(SIDisablePolling) { RET(0); }
HLE(SISetCommand) {}
HLE(SIGetResponse) { RET(0); }
HLE(SITransfer) { RET(0); }
HLE(SIBusy) { RET(0); }
HLE(SIIsChanBusy) { RET(0); }
HLE(SIGetStatus) { RET(0); }
HLE(SIRegisterPollingHandler) { RET(1); }
HLE(SIUnregisterPollingHandler) { RET(1); }
HLE(SISetXY) {}

// ---------------- AI / DSP / AX ----------------
// The recompiled AX library drives audio exactly as on hardware: every AI DMA completion (5 ms)
// its callback asserts the DSP task, the resume callback builds the next command list and mails
// it, and the DSP mixes voices into RAM. Here the "DSP" is ax_ucode (Dolphin's AX HLE) run
// synchronously when the command-list mail arrives, and the AI DMA clock is derived from the
// guest timebase so the sequence is deterministic and independent of host audio.
static uint32_t s_ai_dma_callback, s_ai_dma_addr, s_ai_dma_len;
static bool s_ai_dma_running = false;
static uint64_t s_ai_next_tb = 0;
static uint32_t s_dsp_task = 0;
HLE(AIInit) {}
HLE(AIRegisterDMACallback) { uint32_t cb = ARG0; RET(s_ai_dma_callback); s_ai_dma_callback = cb; host::log("audio: AI DMA callback %08X", cb); }
HLE(AIInitDMA) { s_ai_dma_addr = ARG0; s_ai_dma_len = ARG1; }
HLE(AIStartDMA) {
  if (!s_ai_dma_running) {
    s_ai_next_tb = host::cpu->tb + (uint64_t)s_ai_dma_len * host::TB_HZ / 128000;
    host::log("audio: AI DMA started at %08X+%X (period %llu ticks)", s_ai_dma_addr, s_ai_dma_len, (unsigned long long)s_ai_dma_len * host::TB_HZ / 128000);
  }
  s_ai_dma_running = true;
}

namespace hle {
// longjmp cannot return through the host call stack; the setjmp caller catches this (ppc.h).
void __longjmp(ppc::Context& c, uint8_t*) { throw ppc::GuestLongJmp{c.r[3], c.r[4]}; }
// Called at interrupt-safe points (see host::pump_completions / host::retrace).
void audio_tick(bool force) {
  static bool ticking = false;
  if (ticking || !s_ai_dma_running || !s_ai_dma_callback || !s_ai_dma_len) return;
  if (!force && !ppc::interrupts_on(*host::cpu)) return;
  ticking = true;
  uint64_t period = (uint64_t)s_ai_dma_len * host::TB_HZ / 128000;   // bytes / (32 kHz * 4 bytes)
  if (host::cpu->tb > s_ai_next_tb + period * 20) s_ai_next_tb = host::cpu->tb;   // long stall: skip ahead
  for (int guard = 0; guard < 8 && host::cpu->tb >= s_ai_next_tb; ++guard) {
    s_ai_next_tb += period;
    // The DMA that just completed played the buffer AX set up last time.
    host::audio_push(host::ptr(s_ai_dma_addr, s_ai_dma_len), s_ai_dma_len);
    ppc::Context saved = *host::cpu;
    try { host::call_guest(s_ai_dma_callback); } catch (const LoadContextUnwind&) {}
    uint64_t tb = host::cpu->tb;
    *host::cpu = saved;
    host::cpu->tb = tb;
    ppc::update_mxcsr(*host::cpu);
  }
  ticking = false;
}
}  // namespace hle
HLE(AISetStreamVolLeft) {}
HLE(AISetStreamVolRight) {}
HLE(AIGetStreamVolLeft) { RET(0); }
HLE(AIGetStreamVolRight) { RET(0); }
HLE(AISetStreamPlayState) {}
HLE(AIGetStreamPlayState) { RET(0); }
HLE(AIGetStreamSampleRate) { RET(1); }
HLE(AISetDSPSampleRate) {}
HLE(AIGetDSPSampleRate) { RET(0); }
HLE(DSPInit) {}
HLE(DSPCheckInit) { RET(1); }
HLE(DSPSendMailToDSP) { host::SimCostScope cost(host::SIM_AX); ax::handle_mail(ARG0); }
HLE(DSPCheckMailToDSP) { RET(0); }
HLE(DSPCheckMailFromDSP) { RET(0); }
HLE(DSPReadMailFromDSP) { RET(0); }
// DSPTaskInfo: +0 state, +4 priority, +8 flags, +0x28 init_cb, +0x2C res_cb, +0x30 done_cb, +0x34 req_cb.
HLE(DSPAddTask) {
  uint32_t task = ARG0;
  s_dsp_task = task;
  host::wr32(task + 0, 1);   // DSP_TASK_STATE_RUN
  uint32_t init_cb = host::rd32(task + 0x28);
  host::log("audio: DSP task %08X added (init_cb %08X, res_cb %08X)", task, init_cb, host::rd32(task + 0x2C));
  if (init_cb) host::call_guest(init_cb, task);   // the ucode's init-done mail, delivered at once
  RET(task);
}
HLE(DSPAssertTask) {
  uint32_t task = ARG0;
  uint32_t res_cb = host::rd32(task + 0x2C);
  if (res_cb) host::call_guest(res_cb, task);      // DSP resumed: run the task's resume callback
  RET(task);
}

// ---------------- ARAM ----------------
static uint32_t s_ar_stack_index_addr, s_ar_num_entries, s_ar_stack_pointer, s_ar_free_blocks, s_ar_block_length;
static uint32_t s_ar_dma_callback;
static bool s_ar_init;
static uint32_t s_arq_chunk = 4096, s_arq_callback;

HLE(ARInit) {
  // (stack_index_addr, num_entries) -> base address of allocatable ARAM
  s_ar_stack_index_addr = ARG0;
  s_ar_num_entries = ARG1;
  s_ar_stack_pointer = 0x4000;
  s_ar_free_blocks = ARG1;
  s_ar_block_length = ARG0;
  s_ar_init = true;
  host::wr32(0x800000D0, 0x01000000);
  RET(s_ar_stack_pointer);
}
HLE(ARAlloc) {
  uint32_t length = ARG0;
  uint32_t addr = s_ar_stack_pointer;
  s_ar_stack_pointer += length;
  host::wr32(s_ar_block_length, length);
  s_ar_block_length += 4;
  --s_ar_free_blocks;
  RET(addr);
}
HLE(ARFree) {
  s_ar_block_length -= 4;
  uint32_t length = host::rd32(s_ar_block_length);
  if (ARG0) host::wr32(ARG0, length);
  s_ar_stack_pointer -= length;
  ++s_ar_free_blocks;
  RET(s_ar_stack_pointer);
}
HLE(ARGetSize) { RET(0x01000000); }
HLE(ARRegisterDMACallback) { uint32_t cb = ARG0; RET(s_ar_dma_callback); s_ar_dma_callback = cb; }
static void aram_dma(uint32_t type, uint32_t mainmem, uint32_t aram, uint32_t length) {
  if (!host::valid_range(aram, length, 0x01000000)) host::die("ARAM DMA out of range %08X+%X", aram, length);
  if (type == 0) std::memcpy(host::aram + aram, host::ptr(mainmem, length), length);   // MRAM -> ARAM
  else std::memcpy(host::ptr(mainmem, length), host::aram + aram, length);            // ARAM -> MRAM
}
HLE(ARStartDMA) {
  aram_dma(ARG0, ARG1, ARG2, ARG3);
  uint32_t cb = s_ar_dma_callback;
  if (cb) host::post_completion([cb] { host::call_guest(cb); });
}
HLE(ARQInit) { s_arq_chunk = 4096; }
HLE(ARQPostRequest) {
  // (ARQRequest* task, owner, type, priority, source, dest, length, callback)
  uint32_t task = ARG0, type = ARG2, source = ARG4, dest = ARG5, length = ARG6, callback = ARG7;
  uint32_t owner = ARG1, priority = ARG3;
  host::pump_completions();
  host::wr32(task + 0x04, owner);
  host::wr32(task + 0x08, type);
  host::wr32(task + 0x0C, priority);
  host::wr32(task + 0x10, source);
  host::wr32(task + 0x14, dest);
  host::wr32(task + 0x18, length);
  host::wr32(task + 0x1C, callback);
  if (type == 0) aram_dma(0, source, dest, length);  // MRAM->ARAM: source is main memory
  else aram_dma(1, dest, source, length);            // ARAM->MRAM: source is ARAM
  if (callback) host::post_completion([callback, task] { host::call_guest(callback, task); });
}

// CARD: see hle_card.cpp (GCI-folder memory card in slot A).
