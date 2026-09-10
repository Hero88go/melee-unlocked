// Hardware-library stubs: EXI, SI, AI, DSP, AX output, ARAM, memory card.
// These return "no device / done" so the game's init paths complete without hardware.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include "memory_range.h"
#include <cstring>
#include <deque>

// ---------------- EXI ----------------
HLE(EXIInit) {}
HLE(EXIProbe) { RET(0); }
HLE(EXIProbeEx) { RET((uint32_t)-1); }
HLE(EXIGetID) { RET(0); }
HLE(EXILock) { RET(1); }
HLE(EXIUnlock) { RET(1); }
HLE(EXISelect) { RET(1); }
HLE(EXIDeselect) { RET(1); }
HLE(EXIImm) {
  // (chan, buf, len, type, callback): reads return zeros, writes are dropped.
  if (ARG3 == 0 /* EXI_READ */) { for (uint32_t i = 0; i < ARG2 && i < 4; ++i) host::wr8(ARG1 + i, 0); }
  RET(1);
}
HLE(EXIImmEx) {
  if (ARG3 == 0) { for (uint32_t i = 0; i < ARG2; ++i) host::wr8(ARG1 + i, 0); }
  RET(1);
}
HLE(EXIDma) {
  if (ARG3 == 0) { for (uint32_t i = 0; i < ARG2; ++i) host::wr8(ARG1 + i, 0); }
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
static uint32_t s_ai_dma_callback, s_ai_dma_addr, s_ai_dma_len;
HLE(AIInit) {}
HLE(AIRegisterDMACallback) { RET(s_ai_dma_callback); s_ai_dma_callback = ARG0; }
HLE(AIInitDMA) { s_ai_dma_addr = ARG0; s_ai_dma_len = ARG1; }
HLE(AIStartDMA) {}
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
HLE(DSPSendMailToDSP) {}
HLE(DSPCheckMailToDSP) { RET(0); }
HLE(DSPCheckMailFromDSP) { RET(0); }
HLE(DSPReadMailFromDSP) { RET(0); }
HLE(DSPAddTask) { RET(ARG0); }
HLE(DSPAssertTask) { RET(ARG0); }
HLE(__AXOutInit) {}

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
HLE(ARRegisterDMACallback) { RET(s_ar_dma_callback); s_ar_dma_callback = ARG0; }
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

// ---------------- CARD: no card inserted ----------------
static const uint32_t CARD_RESULT_NOCARD = (uint32_t)-3;
HLE(CARDInit) {}
HLE(CARDCheckAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDProbe) { host::pump_completions(); RET(0); }
HLE(CARDProbeEx) { host::pump_completions(); RET(CARD_RESULT_NOCARD); }
HLE(CARDMountAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDUnmount) { RET(CARD_RESULT_NOCARD); }
HLE(CARDOpen) { RET(CARD_RESULT_NOCARD); }
HLE(CARDFastOpen) { RET(CARD_RESULT_NOCARD); }
HLE(CARDClose) { RET(CARD_RESULT_NOCARD); }
HLE(CARDReadAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDRead) { RET(CARD_RESULT_NOCARD); }
HLE(CARDWriteAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDWrite) { RET(CARD_RESULT_NOCARD); }
HLE(CARDCreateAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDDeleteAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDFormatAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDGetStatus) { RET(CARD_RESULT_NOCARD); }
HLE(CARDSetStatusAsync) { RET(CARD_RESULT_NOCARD); }
HLE(CARDGetXferredBytes) { RET(0); }
HLE(CARDFreeBlocks) { RET(CARD_RESULT_NOCARD); }
HLE(CARDRenameAsync) { RET(CARD_RESULT_NOCARD); }
