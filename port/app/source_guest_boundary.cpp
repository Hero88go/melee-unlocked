// Source-only application boundary. No translated game library or interpreter.
// Shared host diagnostics still reference these registries; they are empty here.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_registry.h"
#include "host.h"
#include "gecko_data.h"
#include "ram_translator.h"

// Shared settings still store these choices. No PPC code tables are embedded;
// native equivalents are implemented and enabled individually after validation.
namespace gecko {
const uint8_t codehandler_bin[1] = {}; const size_t codehandler_bin_size = 0;
const uint8_t bootloader_gct[1] = {}; const size_t bootloader_gct_size = 0;
const uint8_t slippi_gct[1] = {}; const size_t slippi_gct_size = 0;
const Write boot_writes[1] = {}; const size_t boot_writes_count = 0;
const HookInstall boot_hooks[1] = {}; const size_t boot_hooks_count = 0;
const OptionalWrite optional_writes[1] = {}; const size_t optional_writes_count = 0;
const uint32_t gct_base_used = 0, optional_gct_offset = 0;
bool option_widescreen = false, option_pal_stock_icons = false, option_no_screen_shake = false;
// The native game carries Lagless FoD as C, latched at boot with the General Codes (mu_gecko.c).
bool option_lagless_fod = false;
const OptionalCode optional_codes[1] = {}; const size_t optional_codes_count = 0;
}

namespace guest {
const FnEntry fn_table[] = {{0, nullptr}};
const size_t fn_table_count = 0;
const NameEntry name_table[] = {{0, ""}};
const size_t name_table_count = 0;
}

namespace ppc {
bool try_translate_ram(Context&, uint8_t*, uint32_t) { return false; }
void configure_ram_translator(bool enabled) {
  if (enabled) host::die("Source engine rejected the PowerPC RAM translator");
}
void reset_ram_translator() {}
RamTranslatorStats ram_translator_stats() { return {}; }
void interpret(Context&, uint8_t*, uint32_t address) {
  host::die("Source engine rejected PowerPC execution at %08X; this feature needs a native implementation", address);
}
void interpreter_stats(uint64_t* calls, uint64_t* instructions) {
  if (calls) *calls = 0;
  if (instructions) *instructions = 0;
}
void interpreter_counts(uint64_t* calls, uint64_t* instructions) {
  if (calls) *calls = 0;
  if (instructions) *instructions = 0;
}
}
