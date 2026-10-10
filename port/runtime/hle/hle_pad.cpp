// PAD HLE: controller state from the host input layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstdio>
#include <cstring>
#include "lcancel.h"
#include "offline_input_delay.h"
#include "../gx/render_options.h"
#include "user_gecko.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "slippi_online.h"
#include "slippi_playback.h"
#endif
#include "cosmetic_mods.h"
#include "texture_pack.h"
#include "replay_bar.h"

static uint32_t s_spec = 5;

// Character select, L / R on a highlighted costume: that costume slot steps through its installed
// skins (the standard costume, then each skin in the catalog's order), the same as on the Source
// Port. Host only: the pick is saved like a Mods tab choice, the slot's file is published again in
// the game's file table, and the copy the game had already preloaded for the match is let go so the
// match loads the picked skin. The press itself is left to the game, which gives L and R no
// meaning of their own on this screen. Retail game only: a mod disc has its own screen and memory.
namespace css_skins {
constexpr uint16_t kPadR = 0x0020, kPadL = 0x0040, kPadX = 0x0400, kPadY = 0x0800;
// Stage select (mn/mnstagesel.c): the highlighted entry and the list, 0x1C per entry with the stage
// kind at +0x0B; gr/stage.c stage_id_map (12 bytes per stage kind, the ground kind first) and
// gr/ground.c stage_datas (a pointer per ground kind, the file name pointer at +8).
constexpr uint32_t kStageCursor = 0x804D6CAEu, kStageList = 0x803F06D0u;
constexpr uint32_t kStageIdMap = 0x803E9960u, kStageDatas = 0x803DFEDCu;
constexpr uint32_t kScene = 0x80479D30u;          // major scene; +3 the minor one
constexpr uint32_t kDoors = 0x803F0DFCu;          // mnCharSel doors: 4 x 0x24
constexpr uint32_t kIcons = 0x803F0B24u;          // mnCharSel icons: 0x1C each, +1 the fighter
constexpr uint32_t kOnlineMode = 0x804D6640u;     // Slippi's online mode: 1 Unranked, 2 Direct, 3 Teams
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
    // A press that does nothing looked broken to a player: say why, except on a costume with no
    // skins at all, where L and R are just shoulder buttons.
    if (!pick.message.empty() && pick.message.rfind("No other skin", 0) != 0) screen_label::show(pick.message.c_str(), 3.0);
    return;
  }
  const uint32_t fst = host::disc_fst_addr(), fst_size = host::disc_fst_size();
  const auto publish = [&](const std::string& which) {
    const auto result = host::cosmetics::republish_slot(host::ptr(fst, fst_size), fst_size, which);
    if (!result.ok) return false;
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
    return true;
  };
  if (!publish(slot)) return;
  // The Ice Climbers: Nana's slot changed with Popo's, and is published the same way.
  if (!pick.partner_slot.empty() && publish(pick.partner_slot))
    host::log("cosmetics: character select pairs %s with %s", pick.partner_slot.c_str(), slot.c_str());
  // The game's own "the scene changed" mark (lbDvd_8001823C): the preload pass runs again this frame.
  host::wr32(kPreloadSceneChanges, host::rd32(kPreloadSceneChanges) + 1);
  std::vector<gx::texpack::CosmeticCompanion> companions;
  for (const auto& item : host::cosmetics::active_companions())
    companions.push_back({item.kind, item.target_path, item.path});
  gx::texpack::refresh_cosmetic_companions(std::move(companions));
  host::log("cosmetics: character select picked %s for %s", pick.name.c_str(), slot.c_str());
  // The select screen's model and portrait can look the same after a pick: name it, as the stage
  // select does.
  screen_label::show(("Skin: " + pick.name).c_str(), 2.5);
}

// Stage select, X / Y (or R / L) on a highlighted stage: that stage's file steps through the
// standard stage and its installed stage skins, like a costume on the character select. The copy
// the game preloaded is let go the same way, so the match loads the pick.
void cycle_stage(int direction) {
  const uint8_t at = host::rd8(kStageCursor);
  if (at >= 29) return;                                   // Random and the other non-stage entries
  const uint32_t stkind = host::rd8(kStageList + at * 0x1Cu + 0x0B);
  const uint32_t grkind = host::rd32(kStageIdMap + stkind * 12);
  if (grkind >= 0x6Fu) return;
  const uint32_t data = host::rd32(kStageDatas + grkind * 4);
  if (!host::try_ptr(data, 12)) return;
  const uint32_t name_at = host::rd32(data + 8);
  if (!host::try_ptr(name_at, 1)) return;
  const std::string file = host::cstr(name_at, 64);
  bool loading = false;
  if (!preload_sane(&loading) || loading) return;
  const auto pick = host::cosmetics::cycle_stage_live(file, direction);
  if (!pick.ok || !pick.changed) {
    if (!pick.message.empty()) host::log("cosmetics: %s skin not changed (%s)", file.c_str(), pick.message.c_str());
    return;
  }
  const uint32_t fst = host::disc_fst_addr(), fst_size = host::disc_fst_size();
  const auto result = host::cosmetics::republish_stage(host::ptr(fst, fst_size), fst_size, file);
  if (!result.ok) return;
  for (const auto& item : result.files) {
    host::mark_ram_write(fst + item.fst_index * 12 + 8, 4);
    for (uint32_t i = 0; i < kPreloadCount; ++i) {
      const uint32_t entry = kPreloadEntries + i * kPreloadEntrySize;
      const uint8_t state = host::rd8(entry);
      if (host::rd16(entry + 6) != (uint16_t)item.fst_index) continue;
      if (state == 3 || state == 4) host::wr16(entry + 6, 0xFFFE);
      else if (state == 1) host::wr32(entry + 0x0C, 0);
    }
  }
  host::wr32(kPreloadSceneChanges, host::rd32(kPreloadSceneChanges) + 1);
  screen_label::show(("Stage skin: " + pick.name).c_str(), 2.5);
  host::log("cosmetics: stage select picked %s for %s", pick.name.c_str(), file.c_str());
}

// Called with the freshly polled pads, like lcancel::apply.
void apply(const host::PadState pads[4]) {
  static uint16_t held[4];
  // Stage select has its own edge memory, so the character select's is not disturbed.
  static uint16_t stage_held[4];
  if (!host::mod_disc_active() && !slippi::playback::enabled() && host::cosmetics::runtime_initialized() &&
      slippi::online::session_mode() < 0 && host::rd8(kScene) == 2 && host::rd8(kScene + 3) == 1) {
    for (int i = 0; i < 4; ++i) {
      const uint16_t now = pads[i].err == 0 ? (uint16_t)(pads[i].button & (kPadL | kPadR | kPadX | kPadY)) : 0;
      const uint16_t fresh = (uint16_t)(now & ~stage_held[i]);
      stage_held[i] = now;
      if ((now & (kPadL | kPadR)) == (kPadL | kPadR)) continue;   // the start of the reset combination
      if (fresh & (kPadX | kPadR)) { cycle_stage(1); break; }
      if (fresh & (kPadY | kPadL)) { cycle_stage(-1); break; }
    }
  } else {
    for (int i = 0; i < 4; ++i) stage_held[i] = 0xFFFF;         // nothing held on arrival is a press
  }
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
    // picks the color): the mode the player chose in the online menu is the game's own byte
    // (Slippi's online mode, r13 - 0x5060), since the session has no mode before its first search.
    // After lock-in the catalog itself refuses (the profile is frozen from the search on), and
    // inside the online flow it only lets a skin proven to change looks alone, or the standard one.
    if (slippi::online::session_mode() == 3 || host::rd8(kOnlineMode) == 3) return;
    for (int i = 0; i < 4; ++i)
      if (pressed[i]) { cycle(0, (pressed[i] & kPadR) ? 1 : -1); break; }
  }
}
}  // namespace css_skins

// Voice mods (docs/voice-mods.md). When a fight is about to load, the catalog decides each fighter
// sound bank from the fighters of that match. A bank the game still holds in audio memory with
// other content is let go here with the game's own routine and marked not loaded, so the game's
// loader reads it again in this same load. Host only, and before the scene's own entry runs: the
// three fight scene entries are called through the scene tables, which the dispatch table sees.
namespace voice_banks {
constexpr uint32_t kSceneEntries[3] = {0x8016E934u, 0x8016EBC0u, 0x8016EC28u};   // VS, Sudden Death, Training
constexpr uint32_t kBankState = 0x80433984u;      // per bank: -1 not loaded, 1 loaded, 2 in use
constexpr uint32_t kBankEntry = 0x80433A64u;      // per bank: the entry number it was loaded from
constexpr uint32_t kLoadedBytes = 0x804D6448u;    // sample bytes loaded in the fighter and stage bank
constexpr uint32_t kBankSizes = 0x803BC4E4u;      // per bank: two words, the first its sample bytes
constexpr uint32_t kStopBank = 0x803899B0u;       // HSD_SynthSFXStopRange(bank id)
constexpr uint32_t kRemoveGroup = 0x80388E08u;    // HSD_SynthSFXGroupDataRemove(entry number)
constexpr int kBanks = 55;
ppc::Fn g_entry[3] = {};

// The loader's tables as the game keeps them: every state is -1, 1 or 2.
bool tables_sane() {
  for (int i = 0; i < kBanks; ++i) {
    const uint32_t state = host::rd32(kBankState + 4u * (uint32_t)i);
    if (state != 0xFFFFFFFFu && state != 1 && state != 2) return false;
  }
  return true;
}

void plan(uint32_t start_data) {
  if (!host::cosmetics::voice_bank_count() || host::mod_disc_active()) return;
  if (!host::try_ptr(start_data, 0x60 + 4 * 0x24)) return;
  host::cosmetics::MatchFighter ports[4];
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t player = start_data + 0x60 + i * 0x24;   // the match's players: fighter, slot type, stocks, costume
    if (host::rd8(player + 1) == 3) continue;                // nobody in this port
    ports[i].character = (int8_t)host::rd8(player);
    ports[i].costume = host::rd8(player + 3);
  }
  const auto made = host::cosmetics::plan_match_voices(ports);
  if (made.changed.empty()) return;
  if (!tables_sane()) { host::log("cosmetics: voice banks are not reloaded (unknown sound loader layout)"); return; }
  bool stopped = false;
  for (const auto& bank : made.changed) {
    if (bank.bank < 0 || bank.bank >= kBanks) continue;
    const uint32_t state = kBankState + 4u * (uint32_t)bank.bank, entry = kBankEntry + 4u * (uint32_t)bank.bank;
    if (host::rd32(state) == 0xFFFFFFFFu) continue;   // not in audio memory: the load that follows reads the new bank
    // The game stops this bank's sounds itself before it packs the bank again, a moment later in
    // this load; here it comes first, so nothing still plays from the samples that are let go.
    if (!stopped) { host::call_guest(kStopBank, 2); stopped = true; }
    host::call_guest(kRemoveGroup, host::rd32(entry));
    host::wr32(entry, 0xFFFFFFFFu);
    host::wr32(state, 0xFFFFFFFFu);
    host::wr32(kLoadedBytes, host::rd32(kLoadedBytes) - host::rd32(kBankSizes + 8u * (uint32_t)bank.bank));
    host::log("cosmetics: %s is let go from audio memory; this match loads the picked voice", bank.file.c_str());
  }
}

template <int N> void scene_entry(ppc::Context& c, uint8_t* m) {
  // The entry's own arguments survive the guest calls plan() may make.
  uint32_t saved[10];
  for (int i = 0; i < 10; ++i) saved[i] = c.r[3 + i];
  plan(c.r[3]);
  for (int i = 0; i < 10; ++i) c.r[3 + i] = saved[i];
  g_entry[N](c, m);
}

// Once, at the first pad poll: the dispatch table exists by then.
void install() {
  static bool done = false;
  if (done) return;
  done = true;
  const ppc::Fn hooks[3] = {scene_entry<0>, scene_entry<1>, scene_entry<2>};
  for (int i = 0; i < 3; ++i) {
    const ppc::Fn previous = ppc::set_hook(kSceneEntries[i], hooks[i]);
    if (!previous) {
      ppc::set_hook(kSceneEntries[i], nullptr);   // never leave a hook with nothing to call behind it
      host::log("cosmetics: voice bank hook not installed at %08X (not in the dispatch table)", kSceneEntries[i]);
      continue;
    }
    g_entry[i] = previous;
  }
}
}  // namespace voice_banks

namespace random_stage_skins {
ppc::Fn previous = nullptr;
void preload_scene(ppc::Context& c, uint8_t* m) {
  const uint32_t state = c.r[3];
  if (!host::mod_disc_active() && !slippi::playback::enabled() && host::try_ptr(state, 24)) {
    const uint8_t kind = host::rd8(state + 12);
    if (kind == 2 || kind == 4) { // new VS / Training; Sudden Death keeps the match choice
      uint32_t saved[10];
      for (int i = 0; i < 10; ++i) saved[i] = c.r[3 + i];
      // Quiesce old preload reads before replacing a file snapshot, just as preloadState does.
      host::call_guest(0x80018CF4u, host::rd8(state + 1));
      const uint32_t stkind = host::rd32(css_skins::kPreload + 0x10);
      if (stkind < 0x148u) {
        const uint32_t grkind = host::rd32(css_skins::kStageIdMap + stkind * 12);
        if (grkind < 0x6Fu) {
          const uint32_t data = host::rd32(css_skins::kStageDatas + grkind * 4);
          if (host::try_ptr(data, 12) && host::try_ptr(host::rd32(data + 8), 1)) {
            const std::string file = host::cstr(host::rd32(data + 8), 64);
            static uint64_t token = 0;
            const uint32_t fst = host::disc_fst_addr(), size = host::disc_fst_size();
            const auto result = host::cosmetics::plan_stage_skin(host::ptr(fst, size), size, file, ++token);
            if (result.ok) {
              for (const auto& item : result.files) {
                host::mark_ram_write(fst + item.fst_index * 12 + 8, 4);
                for (uint32_t i = 0; i < css_skins::kPreloadCount; ++i) {
                  const uint32_t at = css_skins::kPreloadEntries + i * css_skins::kPreloadEntrySize;
                  if (host::rd16(at + 6) != (uint16_t)item.fst_index) continue;
                  const uint8_t phase = host::rd8(at);
                  if (phase == 3 || phase == 4) host::wr16(at + 6, 0xFFFE);
                  else if (phase == 1) host::wr32(at + 12, 0);
                }
              }
              host::wr32(css_skins::kPreloadSceneChanges, host::rd32(css_skins::kPreloadSceneChanges) + 1);
            }
          }
        }
      }
      for (int i = 0; i < 10; ++i) c.r[3 + i] = saved[i];
    }
  }
  previous(c, m);
}
void install() {
  static bool done = false;
  if (done) return;
  done = true;
  previous = ppc::set_hook(0x801A3F48u, preload_scene);
  if (!previous) { ppc::set_hook(0x801A3F48u, nullptr); host::log("cosmetics: random stage preload hook unavailable"); }
}
} // namespace random_stage_skins

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
  bool offline_gameplay = false;
  if (gx::RenderOptions::live_offline_delay() && slippi::online::session_mode() < 0) {
    for (uint32_t slot = 0; slot < 4; ++slot) {
      const uint32_t player = 0x80453080u + slot * 0xE90u;
      if (host::rd32(player) != 2 || host::rd32(player + 8) != 0) continue;
      const uint32_t gobj = host::rd32(player + 0xB0);
      if (!host::try_ptr(gobj, 0x30)) continue;
      const uint32_t fp = host::rd32(gobj + 0x2C);
      if (host::try_ptr(fp, 0x2400) && host::rd32(fp) == gobj) offline_gameplay = true;
    }
  }
  host::offline_delay::apply(pads, offline_gameplay);
  css_skins::apply(pads);
  voice_banks::install();
  random_stage_skins::install();
  // The player's own Gecko codes (data writes only), re-applied each frame like the Gecko handler.
  user_gecko::apply();
  host::apply_wide_fighter_draw();
  host::apply_low_poly_fighters();
  host::apply_player_tags_always();
  host::apply_final_destination_guard();
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
