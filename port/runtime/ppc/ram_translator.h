// Optional RAM-code translation for the Static engine. Source provides rejecting stubs.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc.h"

// These cells belong to the current invocation, saved/restored across nested guest calls.
// Functions are at most 64 KB, so at most two existing RAM write-generation blocks are needed.
extern "C" {
extern const std::atomic<uint32_t>* mu_ram_version0;
extern const std::atomic<uint32_t>* mu_ram_version1;
extern uint32_t mu_ram_expected0, mu_ram_expected1;
extern bool mu_ram_invalidated;
void mu_ram_translation_guard(uint32_t pc);
void mu_ram_translation_invalidate(uint32_t address, uint32_t bytes);
}

namespace ppc {
struct RamTranslationResume { uint32_t pc; };
struct RamTranslatorStats {
  uint64_t translated = 0, refused = 0, hits = 0, invalidated = 0, resumed = 0;
  size_t entries = 0, native_bytes = 0;
};
void configure_ram_translator(bool enabled);
void reset_ram_translator();
bool try_translate_ram(Context& context, uint8_t* ram, uint32_t address);
RamTranslatorStats ram_translator_stats();
// Continue an invocation already partly executed, retaining its original return address.
void resume_interpret(Context& context, uint8_t* ram, uint32_t pc, uint32_t entry_lr);
// Shared by the interpreter and runtime translations, including MELEE_INTERP_POLL diagnostics.
void ram_branch_poll(Context& context, uint32_t pc, uint32_t target, bool linked);
}
