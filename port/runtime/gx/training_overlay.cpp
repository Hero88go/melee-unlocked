// SPDX-License-Identifier: GPL-2.0-or-later
#include "training_overlay.h"
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>

namespace training_overlay {
namespace {

// The decompilation's common motion-state names (ftCommon_MotionState), by action id.
const char* const kActionNames[] = {
    "DeadDown", "DeadLeft", "DeadRight", "DeadUp", "DeadUpStar", "DeadUpStarIce", "DeadUpFall", "DeadUpFallHitCamera",
    "DeadUpFallHitCameraFlat", "DeadUpFallIce", "DeadUpFallHitCameraIce", "Sleep", "Rebirth", "RebirthWait", "Wait", "WalkSlow",
    "WalkMiddle", "WalkFast", "Turn", "TurnRun", "Dash", "Run", "RunDirect", "RunBrake",
    "KneeBend", "JumpF", "JumpB", "JumpAerialF", "JumpAerialB", "Fall", "FallF", "FallB",
    "FallAerial", "FallAerialF", "FallAerialB", "FallSpecial", "FallSpecialF", "FallSpecialB", "DamageFall", "Squat",
    "SquatWait", "SquatRv", "Landing", "LandingFallSpecial", "Attack11", "Attack12", "Attack13", "Attack100Start",
    "Attack100Loop", "Attack100End", "AttackDash", "AttackS3Hi", "AttackS3HiS", "AttackS3S", "AttackS3LwS", "AttackS3Lw",
    "AttackHi3", "AttackLw3", "AttackS4Hi", "AttackS4HiS", "AttackS4S", "AttackS4LwS", "AttackS4Lw", "AttackHi4",
    "AttackLw4", "AttackAirN", "AttackAirF", "AttackAirB", "AttackAirHi", "AttackAirLw", "LandingAirN", "LandingAirF",
    "LandingAirB", "LandingAirHi", "LandingAirLw", "DamageHi1", "DamageHi2", "DamageHi3", "DamageN1", "DamageN2",
    "DamageN3", "DamageLw1", "DamageLw2", "DamageLw3", "DamageAir1", "DamageAir2", "DamageAir3", "DamageFlyHi",
    "DamageFlyN", "DamageFlyLw", "DamageFlyTop", "DamageFlyRoll", "LightGet", "HeavyGet", "LightThrowF", "LightThrowB",
    "LightThrowHi", "LightThrowLw", "LightThrowDash", "LightThrowDrop", "LightThrowAirF", "LightThrowAirB", "LightThrowAirHi", "LightThrowAirLw",
    "HeavyThrowF", "HeavyThrowB", "HeavyThrowHi", "HeavyThrowLw", "LightThrowF4", "LightThrowB4", "LightThrowHi4", "LightThrowLw4",
    "LightThrowAirF4", "LightThrowAirB4", "LightThrowAirHi4", "LightThrowAirLw4", "HeavyThrowF4", "HeavyThrowB4", "HeavyThrowHi4", "HeavyThrowLw4",
    "SwordSwing1", "SwordSwing3", "SwordSwing4", "SwordSwingDash", "BatSwing1", "BatSwing3", "BatSwing4", "BatSwingDash",
    "ParasolSwing1", "ParasolSwing3", "ParasolSwing4", "ParasolSwingDash", "HarisenSwing1", "HarisenSwing3", "HarisenSwing4", "HarisenSwingDash",
    "StarRodSwing1", "StarRodSwing3", "StarRodSwing4", "StarRodSwingDash", "LipstickSwing1", "LipstickSwing3", "LipstickSwing4", "LipstickSwingDash",
    "ItemParasolOpen", "ItemParasolFall", "ItemParasolFallSpecial", "ItemParasolDamageFall", "LGunShoot", "LGunShootAir", "LGunShootEmpty", "LGunShootAirEmpty",
    "FireFlowerShoot", "FireFlowerShootAir", "ItemScrew", "ItemScrewAir", "DamageScrew", "DamageScrewAir", "ItemScopeStart", "ItemScopeRapid",
    "ItemScopeFire", "ItemScopeEnd", "ItemScopeAirStart", "ItemScopeAirRapid", "ItemScopeAirFire", "ItemScopeAirEnd", "ItemScopeStartEmpty", "ItemScopeRapidEmpty",
    "ItemScopeFireEmpty", "ItemScopeEndEmpty", "ItemScopeAirStartEmpty", "ItemScopeAirRapidEmpty", "ItemScopeAirFireEmpty", "ItemScopeAirEndEmpty", "LiftWait", "LiftWalk1",
    "LiftWalk2", "LiftTurn", "GuardOn", "Guard", "GuardOff", "GuardSetOff", "GuardReflect", "DownBoundU",
    "DownWaitU", "DownDamageU", "DownStandU", "DownAttackU", "DownFowardU", "DownBackU", "DownSpotU", "DownBoundD",
    "DownWaitD", "DownDamageD", "DownStandD", "DownAttackD", "DownFowardD", "DownBackD", "DownSpotD", "Passive",
    "PassiveStandF", "PassiveStandB", "PassiveWall", "PassiveWallJump", "PassiveCeil", "ShieldBreakFly", "ShieldBreakFall", "ShieldBreakDownU",
    "ShieldBreakDownD", "ShieldBreakStandU", "ShieldBreakStandD", "Furafura", "Catch", "CatchPull", "CatchDash", "CatchDashPull",
    "CatchWait", "CatchAttack", "CatchCut", "ThrowF", "ThrowB", "ThrowHi", "ThrowLw", "CapturePulledHi",
    "CaptureWaitHi", "CaptureDamageHi", "CapturePulledLw", "CaptureWaitLw", "CaptureDamageLw", "CaptureCut", "CaptureJump", "CaptureNeck",
    "CaptureFoot", "EscapeF", "EscapeB", "EscapeN", "EscapeAir", "ReboundStop", "Rebound", "ThrownF",
    "ThrownB", "ThrownHi", "ThrownLw", "ThrownlwWomen", "Pass", "Ottotto", "OttottoWait", "FlyReflectWall",
    "FlyReflectCeil", "StopWall", "StopCeil", "MissFoot", "CliffCatch", "CliffWait", "CliffClimbSlow", "CliffClimbQuick",
    "CliffAttackSlow", "CliffAttackQuick", "CliffEscapeSlow", "CliffEscapeQuick", "CliffJumpSlow1", "CliffJumpSlow2", "CliffJumpQuick1", "CliffJumpQuick2",
    "AppealSR", "AppealSL", "ShoulderedWait", "ShoulderedWalkSlow", "ShoulderedWalkMiddle", "ShoulderedWalkFast", "ShoulderedTurn", "ThrownFF",
    "ThrownFB", "ThrownFHi", "ThrownFLw", "CaptureCaptain", "CaptureYoshi", "YoshiEgg", "CaptureKoopa", "CaptureDamageKoopa",
    "CaptureWaitKoopa", "ThrownKoopaF", "ThrownKoopaB", "CaptureKoopaAir", "CaptureDamageKoopaAir", "CaptureWaitKoopaAir", "ThrownKoopaAirF", "ThrownKoopaAirB",
    "CaptureKirby", "CaptureWaitKirby", "ThrownKirbyStar", "ThrownCopyStar", "ThrownKirby", "BarrelWait", "Bury", "BuryWait",
    "BuryJump", "DamageSong", "DamageSongWait", "DamageSongRv", "DamageBind", "CaptureMewtwo", "CaptureMewtwoAir", "ThrownMewtwo",
    "ThrownMewtwoAir", "WarpStarJump", "WarpStarFall", "HammerWait", "HammerWalk", "HammerTurn", "HammerKneeBend", "HammerFall",
    "HammerJump", "HammerLanding", "KinokoGiantStart", "KinokoGiantStartAir", "KinokoGiantEnd", "KinokoGiantEndAir", "KinokoSmallStart", "KinokoSmallStartAir",
    "KinokoSmallEnd", "KinokoSmallEndAir", "Entry", "EntryStart", "EntryEnd", "DamageIce", "DamageIceJump", "CaptureMasterHand",
    "CaptureDamageMasterHand", "CaptureWaitMasterHand", "ThrownMasterHand", "CaptureKirbyYoshi", "KirbyYoshiEgg", "CaptureLeadead", "CaptureLikelike", "DownReflect",
    "CaptureCrazyHand", "CaptureDamageCrazyHand", "CaptureWaitCrazyHand", "ThrownCrazyHand", "Barrel",
};

struct Latest {
  bool valid = false;
  MuStatePod state{};
  MuPadStatus pads[4]{};
};
std::mutex g_mutex;
Latest g_latest;

constexpr uint32_t kTraining = 0x1C;   // GM_TRAINING

float bits_to_float(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

bool any_mode() {
  static const bool on = std::getenv("MELEE_LAB_ANY_MODE") != nullptr;
  return on;
}

// One controller: a stick gate with its position, the C-stick, trigger bars and face buttons.
void draw_pad(ImDrawList* dl, ImVec2 o, const MuPadStatus& p) {
  const ImU32 frame = IM_COL32(210, 215, 225, 170), dim = IM_COL32(70, 76, 88, 190);
  const ImU32 main = IM_COL32(236, 240, 246, 255), cstick = IM_COL32(250, 214, 60, 255);
  auto stick = [&](ImVec2 c, float r, int x, int y, ImU32 col) {
    dl->AddCircleFilled(c, r, IM_COL32(20, 22, 28, 170), 24);
    dl->AddCircle(c, r, frame, 24, 1.5f);
    const ImVec2 d(c.x + r * x / 80.0f, c.y - r * y / 80.0f);
    dl->AddLine(c, d, col, 2.0f);
    dl->AddCircleFilled(d, r * 0.22f, col, 16);
  };
  stick(ImVec2(o.x + 22, o.y + 30), 18, p.stick_x, p.stick_y, main);
  stick(ImVec2(o.x + 64, o.y + 36), 12, p.sub_x, p.sub_y, cstick);
  auto bar = [&](ImVec2 a, uint8_t v, bool digital, const char* label) {
    dl->AddRectFilled(a, ImVec2(a.x + 8, a.y + 40), dim, 2.0f);
    const float h = 40.0f * v / 255.0f;
    dl->AddRectFilled(ImVec2(a.x, a.y + 40 - h), ImVec2(a.x + 8, a.y + 40),
                      digital ? IM_COL32(120, 200, 255, 255) : IM_COL32(170, 180, 200, 255), 2.0f);
    dl->AddText(ImVec2(a.x, a.y + 42), frame, label);
  };
  bar(ImVec2(o.x + 86, o.y + 8), p.trigger_l, (p.button & 0x0040) != 0, "L");
  bar(ImVec2(o.x + 98, o.y + 8), p.trigger_r, (p.button & 0x0020) != 0, "R");
  struct Btn { uint16_t mask; const char* label; ImU32 col; float dx, dy, r; };
  const Btn btns[] = {
      {0x0100, "A", IM_COL32(60, 200, 140, 255), 132, 32, 9},
      {0x0200, "B", IM_COL32(230, 70, 70, 255), 118, 42, 6},
      {0x0400, "X", IM_COL32(220, 220, 225, 255), 146, 26, 6},
      {0x0800, "Y", IM_COL32(220, 220, 225, 255), 128, 16, 6},
      {0x0010, "Z", IM_COL32(140, 90, 220, 255), 146, 12, 5},
      {0x1000, "S", IM_COL32(220, 220, 225, 255), 112, 58, 4},
  };
  for (const Btn& b : btns) {
    const bool on = (p.button & b.mask) != 0;
    const ImVec2 c(o.x + b.dx, o.y + b.dy);
    if (on) dl->AddCircleFilled(c, b.r, b.col, 16);
    else dl->AddCircle(c, b.r, b.col, 16, 1.5f);
  }
  // D-pad
  const ImVec2 dp(o.x + 42, o.y + 60);
  auto dpad = [&](uint16_t mask, float x, float y) {
    const ImVec2 a(dp.x + x - 3, dp.y + y - 3), b(dp.x + x + 3, dp.y + y + 3);
    if (p.button & mask) dl->AddRectFilled(a, b, main);
    else dl->AddRect(a, b, frame);
  };
  dpad(0x0008, 0, -6); dpad(0x0004, 0, 6); dpad(0x0001, -6, 0); dpad(0x0002, 6, 0);
}

}  // namespace

const char* action_name(uint32_t action) {
  return action < sizeof kActionNames / sizeof kActionNames[0] ? kActionNames[action] : "";
}

namespace {
std::mutex g_adv_mutex;
int g_adv_frames = 0, g_adv_kind = 0;
}  // namespace

std::atomic<bool> g_input_display{false};
void set_input_display(bool on) { g_input_display.store(on); }
bool input_display() { return g_input_display.load(); }

void set_advantage(int frames, int kind) {
  std::lock_guard<std::mutex> lock(g_adv_mutex);
  g_adv_frames = frames;
  g_adv_kind = kind;
}

void publish(const MuStatePod& state, const MuPadStatus pads[4]) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_latest.valid = true;
  g_latest.state = state;
  std::memcpy(g_latest.pads, pads, sizeof g_latest.pads);
}

void draw(bool enabled, float width, float height) {
  if (!enabled) return;
  Latest now;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    now = g_latest;
  }
  if (!now.valid || now.state.match_frame == 0) return;
  if (g_input_display.load() && now.state.scene_major != kTraining) {
    // Inputs only, one compact controller per player along the bottom of the screen.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    int shown = 0;
    for (int i = 0; i < 4; ++i) {
      if (!now.state.player[i].present) continue;
      const ImVec2 o(16.0f + shown * 176.0f, 16.0f);   // top row: the damage display is at the bottom
      if (o.x + 164.0f > width) break;
      ++shown;
      dl->AddRectFilled(o, ImVec2(o.x + 164.0f, o.y + 88.0f), IM_COL32(10, 12, 18, 150), 6.0f);
      draw_pad(dl, ImVec2(o.x + 4.0f, o.y + 4.0f), now.pads[i]);
    }
    return;
  }
  if (now.state.scene_major != kTraining && !any_mode()) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float panel_w = 250.0f, panel_h = 132.0f;
  int shown = 0;
  for (int i = 0; i < 4; ++i) {
    const MuFighterState& f = now.state.player[i];
    if (!f.present) continue;
    const ImVec2 o(16.0f + shown * (panel_w + 12.0f), 16.0f);
    if (o.x + panel_w > width) break;
    ++shown;
    dl->AddRectFilled(o, ImVec2(o.x + panel_w, o.y + panel_h), IM_COL32(10, 12, 18, 175), 6.0f);
    char line[96];
    const char* name = action_name(f.action);
    if (*name) std::snprintf(line, sizeof line, "P%d  %s", i + 1, name);
    else std::snprintf(line, sizeof line, "P%d  action %u", i + 1, f.action);
    dl->AddText(ImVec2(o.x + 8, o.y + 6), IM_COL32(236, 240, 246, 255), line);
    std::snprintf(line, sizeof line, "frame %.0f   %.1f%%", bits_to_float(f.anim_frame), bits_to_float(f.percent));
    dl->AddText(ImVec2(o.x + 8, o.y + 22), IM_COL32(200, 206, 218, 255), line);
    std::snprintf(line, sizeof line, "speed %.2f, %.2f", bits_to_float(f.vel_x), bits_to_float(f.vel_y));
    dl->AddText(ImVec2(o.x + 8, o.y + 38), IM_COL32(160, 168, 184, 255), line);
    draw_pad(dl, ImVec2(o.x + 40, o.y + 56), now.pads[i]);
  }
  int adv = 0, kind = 0;
  {
    std::lock_guard<std::mutex> lock(g_adv_mutex);
    adv = g_adv_frames;
    kind = g_adv_kind;
  }
  if (shown > 0 && kind != 0) {
    const ImVec2 o(16.0f, 16.0f + panel_h + 8.0f);
    char line[64];
    std::snprintf(line, sizeof line, "Frame advantage  %+d  on %s", adv, kind == 2 ? "shield" : "hit");
    dl->AddRectFilled(o, ImVec2(o.x + panel_w, o.y + 24.0f), IM_COL32(10, 12, 18, 175), 6.0f);
    dl->AddText(ImVec2(o.x + 8, o.y + 5),
                adv >= 0 ? IM_COL32(120, 220, 140, 255) : IM_COL32(240, 120, 110, 255), line);
  }
}

}  // namespace training_overlay
