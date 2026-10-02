// PAD HLE: controller state from the host input layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstdio>
#include <cstring>
#include "lcancel.h"
#include "user_gecko.h"
#include "slippi_online.h"
#include "slippi_playback.h"
#include "cosmetic_mods.h"
#include "texture_pack.h"

static uint32_t s_spec = 5;

// Character select, L / R on a highlighted costume: that costume slot steps through its installed
// skins (the standard costume, then each skin in the catalog's order), the same as on the Source
// Port. Host only: the pick is saved like a Mods tab choice, the slot's file is published again in
// the game's file table, and the copy the game had already preloaded for the match is let go so the
// match loads the picked skin. The press itself is left to the game, which gives L and R no
// meaning of their own on this screen. Retail game only: a mod disc has its own screen and memory.
namespace css_skins {
constexpr uint16_t kPadR = 0x0020, kPadL = 0x0040;
constexpr uint32_t kScene = 0x80479D30u;          // major scene; +3 the minor one
constexpr uint32_t kDoors = 0x803F0DFCu;          // mnCharSel doors: 4 x 0x24
constexpr uint32_t kIcons = 0x803F0B24u;          // mnCharSel icons: 0x1C each, +1 the fighter
constexpr uint32_t kPreload = 0x80432078u;        // lbDvd's preload cache
constexpr uint32_t kPreloadSceneChanges = kPreload + 0x54, kPreloadEntries = kPreload + 0xAC;
constexpr uint32_t kPreloadEntrySize = 0x1C, kPreloadCount = 80, kPreloadHeap = kPreload + 0x96C;

// The preload cache as lbDvd keeps it: every entry's state is 0 to 4 and the heap word is small.
// Anything else means this is not the layout the addresses above assume, and nothing is touched.
bool preload_sane(bool* loading) {
  *loading = false;
  if (host::rd32(kPreloadHeap) > 8) return false;
  for (uint32_t i = 0; i < kPreloadCount; ++i) {
    const uint8_t state = host::rd8(kPreloadEntries + i * kPreloadEntrySize);
    if (state > 4) return false;
    if (state == 2) *loading = true;
  }
  return true;
}

void cycle(int door, int direction) {
  const uint32_t at = kDoors + (uint32_t)door * 0x24;
  const uint8_t icon = host::rd8(at + 0x0E), costume = host::rd8(at + 0x0D);
  if (icon >= 0x19 || host::rd8(at + 0x0B) != 0) return;   // nothing highlighted, or not a human's door
  const std::string slot = host::cosmetics::costume_slot_file(host::rd8(kIcons + icon * 0x1Cu + 1), costume);
  if (slot.empty()) return;
  // A file the game is reading right now must not change under the read: the next press works.
  bool loading = false;
  if (!preload_sane(&loading)) { host::log("cosmetics: character select skins are off (unknown preload layout)"); return; }
  if (loading) return;
  const auto pick = host::cosmetics::cycle_slot_live(slot, direction);
  if (!pick.ok || !pick.changed) {
    if (!pick.message.empty()) host::log("cosmetics: %s skin not changed (%s)", slot.c_str(), pick.message.c_str());
    return;
  }
  const uint32_t fst = host::rd32(0x80000038u), fst_size = host::disc_fst_size();
  const auto result = host::cosmetics::republish_slot(host::ptr(fst, fst_size), fst_size, slot);
  if (!result.ok) return;
  for (const auto& file : result.files) {
    host::mark_ram_write(fst + file.fst_index * 12 + 8, 4);   // the length the catalog wrote there
    // The copy preloaded under this entry has the old skin and the old length. Its entry number is
    // taken away (0xFFFE names no file), so the game's next preload pass finds nothing for the file,
    // loads it again, and frees the orphan with the other unused entries of its heap.
    for (uint32_t i = 0; i < kPreloadCount; ++i) {
      const uint32_t entry = kPreloadEntries + i * kPreloadEntrySize;
      const uint8_t state = host::rd8(entry);
      if (host::rd16(entry + 6) != (uint16_t)file.fst_index) continue;
      if (state == 3 || state == 4) host::wr16(entry + 6, 0xFFFE);
      else if (state == 1) host::wr32(entry + 0x0C, 0);   // still waiting to load: its length is read again then
    }
  }
  // The game's own "the scene changed" mark (lbDvd_8001823C): the preload pass runs again this frame.
  host::wr32(kPreloadSceneChanges, host::rd32(kPreloadSceneChanges) + 1);
  std::vector<gx::texpack::CosmeticCompanion> companions;
  for (const auto& item : host::cosmetics::active_companions())
    companions.push_back({item.kind, item.target_path, item.path});
  gx::texpack::refresh_cosmetic_companions(std::move(companions));
  host::log("cosmetics: character select picked %s for %s", pick.name.c_str(), slot.c_str());
}

// Called with the freshly polled pads, like lcancel::apply.
void apply(const host::PadState pads[4]) {
  static uint16_t held[4];
  uint16_t pressed[4];
  bool any = false;
  for (int i = 0; i < 4; ++i) {
    const uint16_t now = pads[i].err == 0 ? (uint16_t)(pads[i].button & (kPadL | kPadR)) : 0;
    pressed[i] = (uint16_t)(now & ~held[i]);
    // Both at once is the start of the L+R+A+START reset, not a pick.
    if (now == (kPadL | kPadR)) pressed[i] = 0;
    held[i] = now;
    any |= pressed[i] != 0;
  }
  if (!any || host::mod_disc_active() || slippi::playback::enabled() || !host::cosmetics::runtime_initialized()) return;
  const uint8_t major = host::rd8(kScene), minor = host::rd8(kScene + 3);
  if (minor != 0) return;
  if (major == 2) {
    // VS mode: each port has its own door.
    for (int i = 0; i < 4; ++i)
      if (pressed[i]) cycle(i, (pressed[i] & kPadR) ? 1 : -1);
  } else if (major == 8) {
    // The online character select has one door, the local player's. No change in Teams (the team
    // picks the color); a queued or running match is refused by the catalog itself.
    if (slippi::online::session_mode() == 3) return;
    for (int i = 0; i < 4; ++i)
      if (pressed[i]) { cycle(0, (pressed[i] & kPadR) ? 1 : -1); break; }
  }
}
}  // namespace css_skins

HLE(PADInit) { RET(1); }
HLE(PADReset) { RET(1); }
// The game asks for the controller's neutral to be re-read. We used to accept and do nothing, so a
// stick deflected when the adapter first reported kept a wrong neutral for the whole session.
HLE(PADRecalibrate) {
  const int port = ARG0 == 0xFFFFFFFFu ? -1 : (int)ARG0;
  host::gcadapter_recalibrate(port);
  host::switchpro_recalibrate(port);   // same story: its neutral also comes from the first report
  RET(1);
}
// PADControlMotor(chan, command): 0 stop, 1 rumble, 2 stop hard.
// Online, the game's ports are the match's slots, not the sockets on the adapter: the local
// player's controller may be in socket 2 while the match has them in slot 1. Only the local slot's
// motor command means anything here, and it goes to whatever controller is feeding the local
// input. Everything else is the opponent's rumble, which is theirs to feel, not ours.
static void rumble(int game_port, bool on) {
  if (slippi::online::is_online_match()) {
    if (game_port == slippi::online::local_player_slot()) host::input_rumble_local(on);
    return;
  }
  host::input_rumble(game_port, on);
}
HLE(PADControlMotor) { rumble((int)ARG0, ARG1 == 1); }
HLE(PADControlAllMotors) { for (int i = 0; i < 4; ++i) rumble(i, host::rd32(ARG0 + 4 * i) == 1); }
HLE(PADSetSpec) { s_spec = ARG0; }
HLE(PADGetSpec) { RET(s_spec); }
HLE(PADGetType) { if (ARG1) host::wr32(ARG1, 0x08000000); RET(1); }
HLE(PADSync) { RET(1); }
HLE(PADSetAnalogMode) {}
HLE(PADSetSamplingRate) {}

// u32 PADRead(PADStatus* status[4]) -> bitmask of channels with fresh data
HLE(PADRead) {
  host::pump_completions();
  static int reported = 0;
  if (host::options.trace_calls && reported++ < 10) host::log("[pad] PADRead(%08X)", ARG0);
  host::PadState pads[4];
  host::input_poll(pads);
  // Automatic L-cancel, if the player turned it on: it presses the analog trigger here, one step
  // upstream of everything the game does with the pad, so the press is sampled, recorded into the
  // replay and sent to the opponent exactly like a press the player made.
  lcancel::apply(pads);
  css_skins::apply(pads);
  // The player's own Gecko codes (data writes only), re-applied each frame like the Gecko handler.
  user_gecko::apply();
  host::apply_wide_fighter_draw();
  uint32_t base = ARG0, mask = 0;
  for (int i = 0; i < 4; ++i) {
    uint32_t p = base + i * 12;
    host::wr16(p + 0, pads[i].button);
    host::wr8(p + 2, (uint8_t)pads[i].stick_x);
    host::wr8(p + 3, (uint8_t)pads[i].stick_y);
    host::wr8(p + 4, (uint8_t)pads[i].sub_x);
    host::wr8(p + 5, (uint8_t)pads[i].sub_y);
    host::wr8(p + 6, pads[i].trig_l);
    host::wr8(p + 7, pads[i].trig_r);
    host::wr8(p + 8, pads[i].analog_a);
    host::wr8(p + 9, pads[i].analog_b);
    host::wr8(p + 10, (uint8_t)pads[i].err);
    host::wr8(p + 11, 0);
    if (pads[i].err == 0) mask |= 0x80000000u >> i;
  }
  if (!host::options.input_log.empty()) {
    static FILE* input_log = std::fopen(host::options.input_log.c_str(), "w");
    if (input_log) {
      static bool header = (std::fputs("retrace,port,buttons,stick_x,stick_y,cstick_x,cstick_y,trigger_l,trigger_r\n", input_log), true);
      (void)header;
      for (int i = 0; i < 4; ++i)
        if (pads[i].err == 0)
          std::fprintf(input_log, "%u,%d,%04X,%d,%d,%d,%d,%u,%u\n", host::retrace_count(), i + 1, pads[i].button, pads[i].stick_x, pads[i].stick_y,
                       pads[i].sub_x, pads[i].sub_y, pads[i].trig_l, pads[i].trig_r);
      std::fflush(input_log);
    }
  }
  RET(mask);
}
