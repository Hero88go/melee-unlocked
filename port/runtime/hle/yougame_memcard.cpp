// Answer only the memory-card scene's create/created prompts, through its own input path.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_memcard.h"
#include "yougame_link.h"
#include "host.h"

namespace yougame::memcard {
namespace {
constexpr uint32_t kInput = 0x801AEDC8u, kMessage = 0x801AEE6Cu;
constexpr uint32_t kProbe = 0x801AF0D4u, kDecide = 0x801AF250u, kFrame = 0x801AF568u;
// NTSC 1.02 gmscmemcard.c enter_data: channel +4, decision +0x14, Yes/No +0x1C.
constexpr uint32_t kData = 0x80480DA8u;
ppc::Fn original_input = nullptr, original_message = nullptr;

void input(ppc::Context& c, uint8_t* m) {
  // gm_801AEDC8 is also used by other menus. Only calls from the card scene qualify.
  if (c.lr >= kMessage && c.lr < 0x801B0264u &&
      confirm(yougame::active(), host::rd8(kData + 4), host::rd32(kData + 0x14))) {
    host::wr8(kData + 0x1C, 0);  // Yes, even if a physical direction was held at boot.
    c.r[3] = 0x100;             // PAD_BUTTON_A, consumed only by this menu.
    return;
  }
  original_input(c, m);
}

void message(ppc::Context& c, uint8_t* m) {
  if (hide_message(yougame::active(), host::rd8(kData + 4), c.r[4])) return;
  original_message(c, m);
}
}  // namespace

void install_static_boot() {
  if (!yougame::active() || original_input) return;
  if (!ppc::lookup(kInput) || !ppc::lookup(kMessage)) {
    host::log("yougame: memory-card prompt functions are unavailable");
    return;
  }
  // Translated functions call each other directly. Interpret just these three small
  // menu functions so their input/message calls reach the dispatch hooks, including
  // a mod's current RAM code. The original probe, create, write and error paths run.
  if (!ppc::redirect_to_interpreter(kFrame) || !ppc::redirect_to_interpreter(kProbe) ||
      !ppc::redirect_to_interpreter(kDecide)) {
    host::log("yougame: could not install memory-card menu answers");
    return;
  }
  original_input = ppc::set_hook(kInput, input);
  original_message = ppc::set_hook(kMessage, message);
}
}  // namespace yougame::memcard
