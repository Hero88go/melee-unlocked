// Missed-L-cancel indicator (display only) and automatic L-cancel (input injection).
//
// THE INDICATOR WRITES NOTHING INTO THE GAME
// The Gecko code that flashes a fighter red for a missed L-cancel does it by storing a colour
// overlay into the fighter (External/FlashRedFailedLCancel/TriggerColor.asm writes fp+0x564), which
// is a change to state both clients simulate. This does the same thing from outside: it reads the
// action state, and when an aerial lands without the halving it asks the renderer to tint that
// player's draws red (gx::set_player_tint). A draw knows which player rendered it because the
// observer resolves HSD_GObj_804D7814 at capture time (render_observer.cpp). The whole path is
// reads plus a colour in a constant buffer, so the simulation is byte for byte what it was.
//
// WHY INPUT INJECTION AND NOTHING ELSE
// Slippi netplay has every client simulate both fighters from exchanged inputs, so any change to
// how a fighter behaves that is not carried by the inputs makes the two clients disagree. The
// automatic L-cancel therefore never touches the fighter, the landing-lag calculation or the guest
// code: it raises the analog L trigger on the local pad inside HLE(PADRead), which is upstream of
// the point where the Slippi code reads the pad and sends it to the opponent. The press travels
// over the wire like a real one and both clients compute the same landing lag from it.
//
// HOW MELEE'S L-CANCEL ACTUALLY WORKS (melee/src/melee/ft/...)
//   fighter.c:2105  if (fp->input.pressed_buttons & HSD_PAD_LR) fp->x67F = 0; else fp->x67F++;
//   fighter.c:1886  held_buttons gets the synthetic HSD_PAD_LR bit when the digital L or R bit is
//                   held, OR when the post-deadzone analog trigger is non-zero, OR when Z is held.
//   ftCo_LandingAir.c:43  on landing out of an aerial:
//                   if (fp->x67F < p_ftCommonData->xE4) lag = (int)(lag / p_ftCommonData->xE8);
// So x67F counts frames since the last trigger press and the halving needs it below the window
// (7 frames). pressed_buttons is a rising edge (held & ~held_last), so HOLDING the trigger down
// through the fall does NOT work: the press has to be a fresh edge. The injection presses on one
// frame, releases on the next, and repeats, which keeps a fresh edge within 2 frames at all times.
//
// WHY AN ANALOG PRESS AND NOT THE DIGITAL L BIT
//   ftCo_EscapeAir.c:26  the air dodge check is `pressed_buttons & (HSD_PAD_R | HSD_PAD_L)`, the
//                        literal digital bits. An analog-only press can never air dodge.
//   fighter.c:2110       the tech timers x680/x684 are also driven by the literal digital bits, so
//                        an analog-only press leaves tech timing untouched.
// The only remaining interaction is `held LR && pressed A`, which is the tether/item-pickup grab
// (ftCo_AirCatch.c:54, ftCo_Attack100.c:297). The injection skips any frame on which A or Z is
// newly pressed so it can never turn the player's aerial into a tether.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lcancel.h"
#include "gameplay_policy.h"
#include "controls.h"   // automatic L-cancel agreed with a YouGame Unranked opponent
#include "test_env.h"
#include "../abi/mu_lcancel_flash.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "gx_shader.h"
#include "ppc.h"
#include "render_observer.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "slippi_net.h"
#include "slippi_online.h"
#endif

namespace lcancel {
namespace {

// ---- guest layout (NTSC 1.02; names from the decomp, addresses from port/recomp/GALE01_symbols.txt)
constexpr uint32_t kPlayerSlots = 0x80453080;      // StaticPlayer player_slots[6]
constexpr uint32_t kPlayerStride = 0xE90;
constexpr uint32_t kPFtCommonData = 0x804D6554;    // ftCommonData* p_ftCommonData
constexpr uint32_t kHsdPadLibData = 0x804C1F78;    // PadLibData HSD_PadLibData

constexpr uint32_t kSpState = 0x00;      // enum_t player_state (0 = not playing)
constexpr uint32_t kSpType = 0x08;       // Gm_PKind slot_type (0 = human)
constexpr uint32_t kSpCtrl = 0x48;       // u8 pad_port, the controller (0x46 is the costume tint, sub_color)
constexpr uint32_t kSpEntity = 0xB0;     // HSD_GObj* player_entity[0]
constexpr uint32_t kGObjUserData = 0x2C; // HSD_GObj::user_data -> Fighter*

constexpr uint32_t kFtGObj = 0x000;      // Fighter::gobj (used to sanity check the pointer)
constexpr uint32_t kFtMotionId = 0x010;  // FtMotionId motion_id
constexpr uint32_t kFtGroundAir = 0x0E0; // GroundOrAir ground_or_air (0 ground, 1 air)
constexpr uint32_t kFtX67F = 0x67F;      // u8 frames since the last trigger press
constexpr uint32_t kFtAnimFrame = 0x894; // float cur_anim_frame

constexpr uint32_t kFcDeadzone = 0x10;   // float analog_shoulder_deadzone
constexpr uint32_t kFcZAnalog = 0x14;    // float z_press_analog_value
constexpr uint32_t kFcLcWindow = 0xE4;   // int, the L-cancel input window (7)

constexpr uint32_t kPlShift = 0x20, kPlMax = 0x21, kPlMin = 0x22, kPlScale = 0x27;

// ftCommon_MotionState, melee/src/melee/ft/kinds/ftCommon/forward.h
constexpr int32_t kAttackAirN = 65, kAttackAirLw = 69;
constexpr int32_t kLandingAirN = 70, kLandingAirLw = 74;
// The jump squat, jumps and falls: an attack press there starts an aerial.
constexpr int32_t kKneeBend = 24, kFallAerialB = 34;

// GameCube pad bits, as window.cpp:159 defines them.
constexpr uint16_t kPadZ = 0x0010, kPadR = 0x0020, kPadL = 0x0040, kPadA = 0x0100;
// The C-stick counts as pushed past this (of 127): it starts an aerial like an attack press.
constexpr int kCStickPush = 40;
// How long presses go on after an attack pressed in a jump, until the aerial itself is seen:
// longer than the largest online input delay.
constexpr int kPredictFrames = 9;

// The Gecko code (External/FlashRedFailedLCancel) hands the fighter to Melee's own colour-overlay
// effect, so its length lives in PlCo.dat rather than in the code: it reads as a short pop of red.
// These match that by eye, in simulation frames so the flash is frame locked rather than wall-clock
// locked and a paused or slowed game holds it.
constexpr uint32_t kFlashFrames = 10, kFlashSolidFrames = 6;
constexpr float kFlashAmount = 0.85f;
constexpr float kFlashRed = 1.0f, kFlashGreen = 0.06f, kFlashBlue = 0.05f;

// ---- safe guest reads. host::rd32 kills the process on a bad address, and every pointer here
// comes out of guest memory that is garbage until the game has booted a match.
bool mapped(uint32_t addr, uint32_t bytes) {
  if (addr < 0x80000000u) return false;
  uint64_t off = (uint64_t)(addr & 0x3FFFFFFFu);
  return off + bytes <= (uint64_t)ppc::RAM_SIZE;
}
uint32_t rd32(uint32_t addr) { return mapped(addr, 4) ? host::rd32(addr) : 0; }
uint8_t rd8(uint32_t addr) { return mapped(addr, 1) ? host::rd8(addr) : 0; }
float rdf32(uint32_t addr) { uint32_t v = rd32(addr); float f; std::memcpy(&f, &v, 4); return f; }

// ---- settings
std::atomic<int> g_indicator{MU_LCFLASH_OFF};
std::atomic<bool> g_automatic{false};
std::string g_log_path;
FILE* g_log = nullptr;

// ---- indicator state. One entry per Melee player slot, because that is what a draw carries.
constexpr int kPlayerSlotCount = 6;
struct SlotFlash { bool active = false; uint32_t start_retrace = 0; bool success = false; };
SlotFlash g_flash[kPlayerSlotCount];
bool g_tints_set = false;       // something was handed to the renderer and has to be taken back
bool g_owner_tracking = false;  // last value pushed to the observer

// ---- pad calibration, exactly as HSD_PadClamp/HSD_PadScale in sysdolphin/baselib/controller.c
struct PadCal {
  int min = 30, max = 180, scale = 150;
  bool shift = true;
  float deadzone = 0.30f;
};

PadCal pad_cal_of(const MuLcancelView& v) {
  PadCal c;
  if (v.pad_scale > 0 && v.pad_max > v.pad_min && v.pad_min >= 0) {
    c.min = v.pad_min; c.max = v.pad_max; c.scale = v.pad_scale; c.shift = v.pad_shift == 1;
  }
  if (v.have_common && v.trigger_deadzone > 0.0f && v.trigger_deadzone < 1.0f) c.deadzone = v.trigger_deadzone;
  return c;
}

// The L-cancel input window (ftCommonData::xE4). 7 on a stock PlCo.dat.
int lcancel_window_of(const MuLcancelView& v) {
  if (!v.have_common) return 7;
  return (v.lcancel_window > 0 && v.lcancel_window <= 60) ? v.lcancel_window : 7;
}

float nml_trigger(uint8_t raw, const PadCal& c) {
  int v = raw;
  if (v < c.min) return 0.0f;
  if (v > c.max) v = c.max;
  if (c.shift) v -= c.min;
  return (float)v / (float)c.scale;
}

// The raw PADStatus trigger byte the injection writes. It has to clear analog_shoulder_deadzone
// (0.3) after the game's clamp and scale, with enough margin that the Slippi pad round trip cannot
// push it back under, and it must stay a long way below 255 so it never reads as the digital click.
uint8_t inject_value(const PadCal& c) {
  float target = std::min(0.95f, c.deadzone + 0.45f);
  int v = (int)(target * (float)c.scale + 0.5f);
  if (c.shift) v += c.min;
  v = std::min(v, c.max);
  v = std::min(v, 254);
  v = std::max(v, c.min + 1);
  if (nml_trigger((uint8_t)v, c) <= c.deadzone) v = std::min(254, c.max);   // pathological calibration
  return (uint8_t)v;
}

// The game's own "is a shoulder input held" test: HSD_PAD_LR, fighter.c:1886.
bool effective_lr(const host::PadState& p, const PadCal& c) {
  if (p.button & (kPadL | kPadR | kPadZ)) return true;
  return std::max(nml_trigger(p.trig_l, c), nml_trigger(p.trig_r, c)) > c.deadzone;
}

// ---- per controller port, carried across frames
struct PortState {
  bool last_effective_lr = false;   // the emitted pad held a shoulder input last frame
  uint16_t last_button = 0;         // the player's own buttons last frame (for rising edges)
  bool last_c_out = false;          // the player's C-stick was pushed last frame
  int32_t last_motion = -1;
  // Shoulder buttons (L, R, Z) the player is holding that are being emitted in their analog form
  // (see apply): set while an aerial is on, kept until he lets go of the button.
  uint16_t converted = 0;
  int since_edge = 255;             // emitted frames since the last emitted shoulder press edge
  int predict = 0;                  // frames left of "an aerial is about to start" (attack pressed in a jump)
  bool was_active = false;
  bool cached = false;              // the emitted pad of the current retrace, replayed if PADRead repeats
  uint16_t cached_button = 0;
  uint8_t cached_l = 0, cached_r = 0;
};
PortState g_ports[4];
uint32_t g_last_retrace = 0xFFFFFFFFu;

// The Fighter behind a player slot, or 0.
uint32_t fighter_of_slot(int slot) {
  uint32_t base = kPlayerSlots + (uint32_t)slot * kPlayerStride;
  if (!mapped(base, kPlayerStride)) return 0;
  if (rd32(base + kSpState) == 0) return 0;
  uint32_t gobj = rd32(base + kSpEntity);
  if (!mapped(gobj, kGObjUserData + 4)) return 0;
  uint32_t fp = rd32(gobj + kGObjUserData);
  if (!mapped(fp, 0x2400)) return 0;
  if (rd32(fp + kFtGObj) != gobj) return 0;   // a Fighter points back at its own GObj
  return fp;
}

// Fill in, per controller port, the fighter that port drives locally and the player slot it sits
// in (the slot is what a draw carries, so it is what the tint is keyed on). Online only the local
// player's fighter is filled in: the opponent's pad comes off the network, so injecting into it
// would do nothing at all, and reading their fighter is not our business.
struct Target { uint32_t fighter = 0; int slot = -1; };
void local_fighters(Target out[4]) {
  for (int p = 0; p < 4; ++p) out[p] = Target{};
  const int mode = slippi::online::session_mode();
  if (mode >= 0) {
    int slot = slippi::online::local_player_index();
    if (slot < 0 || slot > 3) return;
    uint32_t fp = fighter_of_slot(slot);
    if (!fp) return;
    int port = (int8_t)rd8(kPlayerSlots + (uint32_t)slot * kPlayerStride + kSpCtrl);
    if (port < 0 || port > 3) port = 0;
    out[port] = Target{fp, slot};
    return;
  }
  for (int slot = 0; slot < 4; ++slot) {
    uint32_t base = kPlayerSlots + (uint32_t)slot * kPlayerStride;
    if (!mapped(base, kPlayerStride)) continue;
    if (rd32(base + kSpType) != 0) continue;   // Gm_PKind_Human
    int port = (int8_t)rd8(base + kSpCtrl);
    if (port < 0 || port > 3 || out[port].fighter) continue;
    uint32_t fp = fighter_of_slot(slot);
    if (fp) out[port] = Target{fp, slot};
  }
}

void (*g_native_view)(MuLcancelView* out) = nullptr;

// Everything apply() decides from, read once per retrace.
void gather(MuLcancelView& v) {
  std::memset(&v, 0, sizeof v);
  if (g_native_view) {
    g_native_view(&v);
    // A network match: only the local player's fighter, as local_fighters does for the recompiled
    // game. The flash is feedback for the person at this PC, not for the opponent's misses.
    if (slippi::online::session_mode() >= 0) {
      const int local = slippi::online::local_player_index();
      for (int p = 0; p < 4; ++p)
        if (v.port[p].present && v.port[p].slot != local) v.port[p] = MuLcancelFighter{};
    }
    return;
  }
  v.pad_shift = rd8(kHsdPadLibData + kPlShift); v.pad_max = rd8(kHsdPadLibData + kPlMax);
  v.pad_min = rd8(kHsdPadLibData + kPlMin); v.pad_scale = rd8(kHsdPadLibData + kPlScale);
  const uint32_t common = rd32(kPFtCommonData);
  if (mapped(common, kFcLcWindow + 4)) {
    v.have_common = 1;
    v.trigger_deadzone = rdf32(common + kFcDeadzone);
    v.z_analog = rdf32(common + kFcZAnalog);
    v.lcancel_window = (int32_t)rd32(common + kFcLcWindow);
  }
  Target targets[4];
  local_fighters(targets);
  for (int p = 0; p < 4; ++p) {
    const uint32_t fp = targets[p].fighter;
    if (!fp) continue;
    MuLcancelFighter& f = v.port[p];
    f.present = 1;
    f.slot = targets[p].slot;
    f.motion_id = (int32_t)rd32(fp + kFtMotionId);
    f.ground_or_air = rd32(fp + kFtGroundAir);
    f.frames_since_trigger = rd8(fp + kFtX67F);
    f.anim_frame = rdf32(fp + kFtAnimFrame);
  }
}

void raise_flash(int slot, int port, int frames_since_press, uint32_t retrace, bool success) {
  if (slot < 0 || slot >= kPlayerSlotCount) return;
  g_flash[slot] = {true, retrace, success};
  // Only while the diagnostic log is on: during normal play this fires several times a minute.
  if (g_log) host::log("lcancel: P%d (slot %d) %s, %d frames since a trigger press", port + 1, slot,
                       success ? "success" : "missed", frames_since_press);
}

// Hand the renderer the colour for every flashing slot and take it back when the flash is over.
// This is the only thing the indicator does: a colour in a constant buffer, no guest write.
void publish_tints(uint32_t retrace) {
  bool any = false;
  for (int slot = 0; slot < kPlayerSlotCount; ++slot) {
    SlotFlash& f = g_flash[slot];
    if (!f.active) continue;
    const uint32_t age = retrace - f.start_retrace;
    if (age >= kFlashFrames) {
      f.active = false;
      gx::set_player_tint(slot, 1.f, 1.f, 1.f, 0.f);
      continue;
    }
    const float fade = age < kFlashSolidFrames
                           ? 1.0f
                           : 1.0f - (float)(age - kFlashSolidFrames) / (float)(kFlashFrames - kFlashSolidFrames);
    gx::set_player_tint(slot, f.success ? 0.12f : kFlashRed,
                        f.success ? 1.0f : kFlashGreen,
                        f.success ? 0.12f : kFlashBlue, kFlashAmount * fade);
    any = true;
  }
  g_tints_set = any;
}

// Put the renderer back exactly as it was: no tints, and no owner resolution cost per draw.
void release_renderer() {
  if (g_tints_set) { gx::clear_player_tints(); g_tints_set = false; }
  for (auto& f : g_flash) f.active = false;
  if (g_owner_tracking) { gx::set_owner_tracking(false); g_owner_tracking = false; }
}

void log_row(uint32_t retrace, int port, int32_t motion, uint32_t ground_air, uint8_t x67f, float anim, bool injected) {
  if (!g_log) return;
  std::fprintf(g_log, "%u,%d,%d,%u,%u,%.3f,%d\n", retrace, port + 1, motion, ground_air, x67f, anim, injected ? 1 : 0);
  std::fflush(g_log);
}

}  // namespace

void set_indicator(bool on) { set_flash_mode(on ? MU_LCFLASH_MU_MISSED : MU_LCFLASH_OFF); }
void set_flash_mode(int mode) {
  g_indicator.store(mu_lcancel_renderer_mode(mode) ? mode : MU_LCFLASH_OFF, std::memory_order_relaxed);
}
int flash_mode() { return g_indicator.load(std::memory_order_relaxed); }
void set_native_view(void (*fill)(MuLcancelView* out)) { g_native_view = fill; }
#ifdef YOUGAME_AGENT_TEST_HOOK
void agent_test_view(MuLcancelView* out) { gather(*out); }
#endif
void set_automatic(bool on) {
  // A parked session (warming up in Training) still counts: the opponent was told at connect time.
  g_automatic.store(policy::automatic_after_toggle(slippi::online::session_mode_or_parked(), automatic_enabled(), on),
                    std::memory_order_relaxed);
}
bool automatic_setting_locked() { return policy::automatic_setting_locked(slippi::online::session_mode_or_parked()); }
bool indicator_enabled() { return flash_mode() != MU_LCFLASH_OFF; }
bool automatic_enabled() { return g_automatic.load(std::memory_order_relaxed); }

void set_log_path(const std::string& path) {
  g_log_path = path;
  if (g_log) { std::fclose(g_log); g_log = nullptr; }
  if (path.empty()) return;
  g_log = std::fopen(path.c_str(), "w");
  if (g_log) std::fprintf(g_log, "retrace,port,motion_id,ground_or_air,frames_since_trigger,anim_frame,injected\n");
}

const char* auto_suppressed_mode() {
  return policy::auto_suppressed_mode_agreed(slippi::online::session_mode(), controls::lcancel_agreed());
}

bool online_session_pending() {
  // The mode only becomes known when the search starts, which happens on the character select
  // screen once the player has locked a character in, and the notice then stays up through
  // matchmaking, the opponent connecting and the rest of that screen.
  return slippi::online::session_mode() >= 0 && slippi::online::in_online_menus();
}

bool automatic_active_online() {
  return automatic_enabled() && !host::gameplay::vanilla_only() &&
         policy::automatic_allowed(slippi::online::session_mode(), true, false, controls::lcancel_agreed());
}

void apply(host::PadState pads[4], bool allow_automatic) {
  const bool want_indicator = indicator_enabled();
  const bool want_auto = automatic_enabled() && !host::gameplay::vanilla_only();
  // Default state: not one guest read, not one byte changed, so the game is bit for bit the game.
  if (!want_indicator && !want_auto && !g_log) { release_renderer(); return; }

  const uint32_t retrace = host::retrace_count();
  if (retrace == g_last_retrace) {
    // PADRead called twice inside one retrace: replay the decision instead of taking a new one, so
    // the two reads cannot disagree about what the player pressed.
    for (int p = 0; p < 4; ++p)
      if (allow_automatic && g_ports[p].cached) { pads[p].button = g_ports[p].cached_button; pads[p].trig_l = g_ports[p].cached_l; pads[p].trig_r = g_ports[p].cached_r; }
    return;
  }
  g_last_retrace = retrace;

  MuLcancelView view;
  gather(view);
  const PadCal cal = pad_cal_of(view);
  const uint8_t press = inject_value(cal);
  const int window = lcancel_window_of(view);
  const char* suppressed = auto_suppressed_mode();
  const bool auto_allowed = allow_automatic && policy::automatic_allowed(slippi::online::session_mode(), want_auto, false,
                                                                         controls::lcancel_agreed());
  // With the automatic press actually doing the L-cancel there is nothing to report, so the flash
  // is off entirely rather than merely never firing. It comes back the moment the press is
  // suppressed (a matchmaking mode), which is exactly when the player wants the feedback again.
  const bool indicate = want_indicator && !auto_allowed;
  if (indicate != g_owner_tracking) { gx::set_owner_tracking(indicate); g_owner_tracking = indicate; }
  if (!indicate && g_tints_set) { gx::clear_player_tints(); g_tints_set = false; for (auto& f : g_flash) f.active = false; }
  if (g_log) {
    static int last_mode = -2, last_allowed = -1;
    const int mode = slippi::online::session_mode();
    if (mode != last_mode || (int)auto_allowed != last_allowed) {   // also when an opponent's OK switches it on (controls.h)
      last_mode = mode;
      last_allowed = auto_allowed;
      host::log("lcancel: session mode %d (%s), auto L-cancel %s", mode,
                suppressed ? suppressed : mode >= 0 && mode != 2 ? "agreed with the opponent" : "offline or Direct",
                want_auto ? (auto_allowed ? "allowed" : "suppressed") : "off");
    }
  }

  // Online there is one local fighter, and which physical pad the game reads for it is not this
  // code's to know (the view filed a second player under its slot, so the press went to a pad the
  // game never read and player 2 got no automatic L-cancel at all). Every pad is treated as that
  // fighter's; pads the game does not read are not looked at by anything.
  const bool online = slippi::online::session_mode() >= 0;
  const MuLcancelFighter* local = nullptr;
  if (online)
    for (int p = 0; p < 4; ++p) if (view.port[p].present) { local = &view.port[p]; break; }
  const uint8_t full = (uint8_t)std::clamp(cal.max, cal.min + 1, 254);   // reads 1.0, as a digital press does
  const float z_level = view.z_analog > cal.deadzone && view.z_analog < 1.0f ? view.z_analog : 0.35f;
  const uint8_t z_value = (uint8_t)std::clamp((int)(z_level * (float)cal.scale + 0.5f) + (cal.shift ? cal.min : 0), cal.min + 1, 254);

  for (int p = 0; p < 4; ++p) {
    PortState& st = g_ports[p];
    host::PadState& pad = pads[p];
    st.cached = false;
    static const MuLcancelFighter none{};
    const MuLcancelFighter& fighter = online ? (local ? *local : none) : view.port[p];
    const uint16_t raw_button = pad.button;
    const bool c_out = std::abs((int)pad.sub_x) >= kCStickPush || std::abs((int)pad.sub_y) >= kCStickPush;
    if (!fighter.present) {
      st.last_motion = -1;
      st.last_effective_lr = effective_lr(pad, cal);
      st.last_button = raw_button; st.last_c_out = c_out;
      st.converted = 0; st.predict = 0; st.was_active = false; st.since_edge = 255;
      continue;
    }

    const int32_t motion = fighter.motion_id;
    const uint32_t ground_air = fighter.ground_or_air;
    const uint8_t since = (uint8_t)fighter.frames_since_trigger;

    // Indicator: the frame the fighter enters a LandingAir* state is the frame the game ran the
    // x67F < window test, and x67F has not moved since. Display only, so it is safe in every mode.
    // Online every pad stands for the one local fighter: raised once.
    if (indicate && (!online || p == 0) && motion >= kLandingAirN && motion <= kLandingAirLw && motion != st.last_motion) {
      const bool success = since < (uint8_t)std::min(window, 255);
      if (mu_lcancel_renderer_reports(flash_mode(), success))
        raise_flash(fighter.slot, p, since, retrace, success);
    }

    // ---- Automatic L-cancel.
    // The game L-cancels a landing when a shoulder input was PRESSED (went from not held to held)
    // fewer than 7 frames before it, and for that test a light analog press counts like a click
    // (fighter.c: HSD_PAD_LR). What only a click does (air dodge, tech and its 40 frame lockout, the
    // shield's own press checks) reads the digital L/R bits, which this never presses or re-presses.
    //
    // While an aerial is on, the emitted pad therefore carries a fresh light press every third
    // frame. A shoulder button the player himself holds through the aerial would hide those (a held
    // input cannot be pressed again), so his hold is emitted in its analog form instead: the frame
    // he presses goes out untouched, the click included, and from the next frame the hold is a full
    // analog press (which the game reads exactly as a held click: 1.0, or Z's own level with A held)
    // with one frame let go every third frame. The digital bit never comes back while he holds, so
    // no click he did not make ever reaches the game; when he lets go, the button is his again.
    //
    // An attack pressed in a jump starts the aerial a frame later (and online a few frames after
    // this read), so presses start with it and not only once the aerial is seen.
    bool emitted_on;
    bool changed = false;
    if (!auto_allowed) {
      st.converted = 0; st.predict = 0; st.was_active = false;
      emitted_on = effective_lr(pad, cal);
    } else {
      const uint16_t shoulders = raw_button & (kPadL | kPadR | kPadZ);
      const uint16_t fresh = shoulders & (uint16_t)~st.last_button;
      const bool a_rising = (raw_button & kPadA) && !(st.last_button & kPadA);
      const bool aerial = ground_air == 1 && motion >= kAttackAirN && motion <= kAttackAirLw;
      const bool jumping = motion >= kKneeBend && motion <= kFallAerialB;
      if ((a_rising || (c_out && !st.last_c_out) || (fresh & kPadZ)) && jumping) st.predict = kPredictFrames;
      else if (st.predict > 0 && !(jumping || aerial)) st.predict = 0;
      else if (st.predict > 0) --st.predict;
      // The press that starts the aerial goes out alone: a held shoulder with it is another move
      // (a tether for Link and Samus), so presses begin on the next frame.
      const bool active = aerial || (st.predict > 0 && st.predict < kPredictFrames);

      st.converted &= shoulders;                                  // let go: the button is his again
      // Tests: MELEE_TEST_LCANCEL_ADD_ONLY=1 never alters a hold (presses are only ever added).
      static const bool add_only = host::test_env::getenv("MELEE_TEST_LCANCEL_ADD_ONLY") != nullptr;
      if (active && !add_only) st.converted |= shoulders & (uint16_t)~fresh;   // held, not pressed this frame
      const bool own_analog = std::max(nml_trigger(pad.trig_l, cal), nml_trigger(pad.trig_r, cal)) > cal.deadzone;
      const bool hold = st.converted != 0 || own_analog || (add_only && shoulders != 0);
      bool on;   // whether a shoulder input goes out held this frame
      if (!active) on = hold || fresh;
      else if (fresh) on = true;                                           // his own press, as he made it
      else if (a_rising) on = hold;                                        // with an attack press: exactly his own state
      else if (hold) on = add_only || !(st.last_effective_lr && st.since_edge >= 2);   // let go one frame in three
      else on = !st.last_effective_lr && (st.since_edge >= 3 || !st.was_active);

      uint16_t button = raw_button & (uint16_t)~st.converted;
      if (st.converted & kPadZ) button |= kPadA;   // Z holds A too; it stays held through the gaps
      uint8_t l = pad.trig_l, r = pad.trig_r;
      if (on) {
        if (st.converted & kPadL) l = std::max(l, full);
        if (st.converted & kPadR) r = std::max(r, full);
        if (st.converted & kPadZ) l = std::max(l, z_value);
        if (active && !hold && !fresh) l = std::max(l, press);
      } else {
        if (nml_trigger(l, cal) > cal.deadzone) l = 0;
        if (nml_trigger(r, cal) > cal.deadzone) r = 0;
      }
      changed = button != pad.button || l != pad.trig_l || r != pad.trig_r;
      pad.button = button; pad.trig_l = l; pad.trig_r = r;
      st.cached = true; st.cached_button = button; st.cached_l = l; st.cached_r = r;
      st.was_active = active;
      emitted_on = effective_lr(pad, cal);
    }
    if (g_log) log_row(retrace, p, motion, ground_air, since, fighter.anim_frame, changed);

    st.since_edge = emitted_on && !st.last_effective_lr ? 0 : std::min(st.since_edge + 1, 255);
    st.last_motion = motion;
    st.last_effective_lr = emitted_on;
    st.last_button = raw_button; st.last_c_out = c_out;
  }

  if (indicate || g_tints_set) publish_tints(retrace);
}

}  // namespace lcancel
