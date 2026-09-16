// L-cancel indicator (display only) and automatic L-cancel (input injection).
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

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "ppc.h"
#include "slippi_net.h"
#include "slippi_online.h"

namespace lcancel {
namespace {

// ---- guest layout (NTSC 1.02; names from the decomp, addresses from port/recomp/GALE01_symbols.txt)
constexpr uint32_t kPlayerSlots = 0x80453080;      // StaticPlayer player_slots[6]
constexpr uint32_t kPlayerStride = 0xE90;
constexpr uint32_t kPFtCommonData = 0x804D6554;    // ftCommonData* p_ftCommonData
constexpr uint32_t kHsdPadLibData = 0x804C1F78;    // PadLibData HSD_PadLibData

constexpr uint32_t kSpState = 0x00;      // enum_t player_state (0 = not playing)
constexpr uint32_t kSpType = 0x08;       // Gm_PKind slot_type (0 = human)
constexpr uint32_t kSpCtrl = 0x46;       // s8 controller_index
constexpr uint32_t kSpEntity = 0xB0;     // HSD_GObj* player_entity[0]
constexpr uint32_t kGObjUserData = 0x2C; // HSD_GObj::user_data -> Fighter*

constexpr uint32_t kFtGObj = 0x000;      // Fighter::gobj (used to sanity check the pointer)
constexpr uint32_t kFtMotionId = 0x010;  // FtMotionId motion_id
constexpr uint32_t kFtGroundAir = 0x0E0; // GroundOrAir ground_or_air (0 ground, 1 air)
constexpr uint32_t kFtX67F = 0x67F;      // u8 frames since the last trigger press
constexpr uint32_t kFtAnimFrame = 0x894; // float cur_anim_frame

constexpr uint32_t kFcDeadzone = 0x10;   // float analog_shoulder_deadzone
constexpr uint32_t kFcLcWindow = 0xE4;   // int, the L-cancel input window (7)

constexpr uint32_t kPlShift = 0x20, kPlMax = 0x21, kPlMin = 0x22, kPlScale = 0x27;

// ftCommon_MotionState, melee/src/melee/ft/kinds/ftCommon/forward.h
constexpr int32_t kAttackAirN = 65, kAttackAirLw = 69;
constexpr int32_t kLandingAirN = 70, kLandingAirLw = 74;

// GameCube pad bits, as window.cpp:159 defines them.
constexpr uint16_t kPadZ = 0x0010, kPadR = 0x0020, kPadL = 0x0040, kPadA = 0x0100;

constexpr double kFlashSeconds = 0.9;

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
std::atomic<bool> g_indicator{false}, g_automatic{false};
std::string g_log_path;
FILE* g_log = nullptr;

// ---- indicator state, written on the simulation thread and read by the renderer
std::mutex g_flash_mutex;
int g_flash_port = 0, g_flash_frames = 0;
double g_flash_time = -1e9;

// ---- pad calibration, exactly as HSD_PadClamp/HSD_PadScale in sysdolphin/baselib/controller.c
struct PadCal {
  int min = 30, max = 180, scale = 150;
  bool shift = true;
  float deadzone = 0.30f;
};

PadCal read_pad_cal() {
  PadCal c;
  int shift = rd8(kHsdPadLibData + kPlShift), max = rd8(kHsdPadLibData + kPlMax);
  int min = rd8(kHsdPadLibData + kPlMin), scale = rd8(kHsdPadLibData + kPlScale);
  if (scale > 0 && max > min && min >= 0) { c.min = min; c.max = max; c.scale = scale; c.shift = shift == 1; }
  uint32_t common = rd32(kPFtCommonData);
  if (mapped(common, kFcDeadzone + 4)) {
    float dz = rdf32(common + kFcDeadzone);
    if (dz > 0.0f && dz < 1.0f) c.deadzone = dz;
  }
  return c;
}

// The L-cancel input window (ftCommonData::xE4). 7 on a stock PlCo.dat.
int read_lcancel_window() {
  uint32_t common = rd32(kPFtCommonData);
  if (!mapped(common, kFcLcWindow + 4)) return 7;
  int32_t w = (int32_t)rd32(common + kFcLcWindow);
  return (w > 0 && w <= 60) ? w : 7;
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
  uint16_t last_button = 0;         // the emitted pad's buttons last frame (for the A/Z rising edge)
  int32_t last_motion = -1;
  bool cached_inject = false;       // decision of the current retrace, replayed if PADRead repeats
  uint8_t cached_value = 0;
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

// Fill in, per controller port, the fighter that port drives locally. Online only the local
// player's fighter is filled in: the opponent's pad comes off the network, so injecting into it
// would do nothing at all, and reading their fighter is not our business.
void local_fighters(uint32_t out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0;
  const int mode = slippi::online::session_mode();
  if (mode >= 0) {
    int slot = slippi::online::local_player_index();
    if (slot < 0 || slot > 3) return;
    uint32_t fp = fighter_of_slot(slot);
    if (!fp) return;
    int port = (int8_t)rd8(kPlayerSlots + (uint32_t)slot * kPlayerStride + kSpCtrl);
    if (port < 0 || port > 3) port = 0;
    out[port] = fp;
    return;
  }
  for (int slot = 0; slot < 4; ++slot) {
    uint32_t base = kPlayerSlots + (uint32_t)slot * kPlayerStride;
    if (!mapped(base, kPlayerStride)) continue;
    if (rd32(base + kSpType) != 0) continue;   // Gm_PKind_Human
    int port = (int8_t)rd8(base + kSpCtrl);
    if (port < 0 || port > 3 || out[port]) continue;
    out[port] = fighter_of_slot(slot);
  }
}

void raise_flash(int port, int frames_since_press) {
  {
    std::lock_guard<std::mutex> lock(g_flash_mutex);
    g_flash_port = port + 1;
    g_flash_frames = frames_since_press;
    g_flash_time = host::now_seconds();
  }
  // Only while the diagnostic log is on: during normal play this fires several times a minute.
  if (g_log) host::log("lcancel: P%d missed, %d frames since a trigger press", port + 1, frames_since_press);
}

void log_row(uint32_t retrace, int port, int32_t motion, uint32_t ground_air, uint8_t x67f, float anim, bool injected) {
  if (!g_log) return;
  std::fprintf(g_log, "%u,%d,%d,%u,%u,%.3f,%d\n", retrace, port + 1, motion, ground_air, x67f, anim, injected ? 1 : 0);
  std::fflush(g_log);
}

}  // namespace

void set_indicator(bool on) { g_indicator.store(on, std::memory_order_relaxed); }
void set_automatic(bool on) { g_automatic.store(on, std::memory_order_relaxed); }
bool indicator_enabled() { return g_indicator.load(std::memory_order_relaxed); }
bool automatic_enabled() { return g_automatic.load(std::memory_order_relaxed); }

void set_log_path(const std::string& path) {
  g_log_path = path;
  if (g_log) { std::fclose(g_log); g_log = nullptr; }
  if (path.empty()) return;
  g_log = std::fopen(path.c_str(), "w");
  if (g_log) std::fprintf(g_log, "retrace,port,motion_id,ground_or_air,frames_since_trigger,anim_frame,injected\n");
}

const char* auto_suppressed_mode() {
  const int mode = slippi::online::session_mode();
  if (mode < 0) return nullptr;                                  // offline
  if (mode == slippi::Matchmaking::DIRECT) return nullptr;        // the one allowed online mode
  switch (mode) {
    case slippi::Matchmaking::RANKED: return "Ranked";
    case slippi::Matchmaking::UNRANKED: return "Unranked";
    case slippi::Matchmaking::TEAMS: return "Teams";
    case slippi::Matchmaking::PARTY: return "Party";
    default: return "this online mode";
  }
}

bool online_session_pending() {
  // The mode only becomes known when the search starts, which happens on the character select
  // screen once the player has locked a character in, and the notice then stays up through
  // matchmaking, the opponent connecting and the rest of that screen.
  return slippi::online::session_mode() >= 0 && slippi::online::in_online_menus();
}

Flash flash() {
  Flash f;
  double t, now = host::now_seconds();
  {
    std::lock_guard<std::mutex> lock(g_flash_mutex);
    t = g_flash_time;
    f.port = g_flash_port;
    f.frames_since_press = g_flash_frames;
  }
  const double age = now - t;
  if (age < 0.0 || age > kFlashSeconds) return f;
  f.active = true;
  // Hold it solid for the first half, then fade, so a glance catches it at full contrast.
  const double hold = kFlashSeconds * 0.5;
  f.alpha = age <= hold ? 1.0f : (float)(1.0 - (age - hold) / (kFlashSeconds - hold));
  return f;
}

void apply(host::PadState pads[4]) {
  const bool want_indicator = indicator_enabled();
  const bool want_auto = automatic_enabled();
  // Default state: not one guest read, not one byte changed, so the game is bit for bit the game.
  if (!want_indicator && !want_auto && !g_log) return;

  const uint32_t retrace = host::retrace_count();
  if (retrace == g_last_retrace) {
    // PADRead called twice inside one retrace: replay the decision instead of taking a new one, so
    // the two reads cannot disagree about what the player pressed.
    for (int p = 0; p < 4; ++p)
      if (g_ports[p].cached_inject) pads[p].trig_l = std::max(pads[p].trig_l, g_ports[p].cached_value);
    return;
  }
  g_last_retrace = retrace;

  const PadCal cal = read_pad_cal();
  const uint8_t press = inject_value(cal);
  const int window = read_lcancel_window();
  const char* suppressed = auto_suppressed_mode();
  const bool auto_allowed = want_auto && suppressed == nullptr;
  if (g_log) {
    static int last_mode = -2;
    const int mode = slippi::online::session_mode();
    if (mode != last_mode) {
      last_mode = mode;
      host::log("lcancel: session mode %d (%s), auto L-cancel %s", mode, suppressed ? suppressed : "offline or Direct",
                want_auto ? (auto_allowed ? "allowed" : "suppressed") : "off");
    }
  }

  uint32_t fighters[4];
  local_fighters(fighters);

  for (int p = 0; p < 4; ++p) {
    PortState& st = g_ports[p];
    host::PadState& pad = pads[p];
    st.cached_inject = false;
    const uint32_t fp = fighters[p];
    if (!fp) {
      st.last_motion = -1;
      st.last_effective_lr = effective_lr(pad, cal);
      st.last_button = pad.button;
      continue;
    }

    const int32_t motion = (int32_t)rd32(fp + kFtMotionId);
    const uint32_t ground_air = rd32(fp + kFtGroundAir);
    const uint8_t since = rd8(fp + kFtX67F);

    // Indicator: the frame the fighter enters a LandingAir* state is the frame the game ran the
    // x67F < window test, and x67F has not moved since. Display only, so it is safe in every mode.
    if (want_indicator && motion >= kLandingAirN && motion <= kLandingAirLw && motion != st.last_motion &&
        since >= (uint8_t)std::min(window, 255))
      raise_flash(p, since);

    // Automatic press: only while the fighter is airborne in one of the five aerial attacks. An
    // aerial cannot be interrupted into an air dodge or a shield, so a shoulder input there has no
    // effect other than resetting x67F.
    bool inject = false;
    if (auto_allowed && ground_air == 1 && motion >= kAttackAirN && motion <= kAttackAirLw) {
      const bool a_rising = (pad.button & kPadA) && !(st.last_button & kPadA);
      const bool z_rising = (pad.button & kPadZ) && !(st.last_button & kPadZ);
      // Press only when the previous emitted frame was not already holding a shoulder input: the
      // game needs a rising edge, so a continuous hold would never register a second time.
      inject = !st.last_effective_lr && !a_rising && !z_rising;
    }
    if (inject) {
      pad.trig_l = std::max(pad.trig_l, press);
      st.cached_inject = true;
      st.cached_value = press;
    }

    if (g_log) log_row(retrace, p, motion, ground_air, since, rdf32(fp + kFtAnimFrame), inject);

    st.last_motion = motion;
    st.last_effective_lr = effective_lr(pad, cal);
    st.last_button = pad.button;
  }
}

}  // namespace lcancel
