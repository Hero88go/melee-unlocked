// The Practice CPU's decision model (mu_practice_cpu_impl.inl): saved settings from version 1 still load,
// and the version 2 options (throws, floorhug, waveland, shield drop, dash jump, run past, turn around,
// double jumps, fast fall delay, aerial timing, RNG seeds) give the controller they are meant to.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "../runtime/abi/mu_practice_cpu_impl.inl"

static int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

static_assert(offsetof(MuPcConfig, move2_odds) == MU_PC_CONFIG_V1_SIZE, "version 2 follows version 1's bytes");
static_assert(sizeof(MuPcConfig) <= 256, "fits one host reply");

// A CPU standing on Battlefield's main stage, the opponent 20 units away.
static MuPcView view() {
  MuPcView v;
  std::memset(&v, 0, sizeof v);
  v.cpu.present = v.opp.present = 1;
  v.cpu.motion = v.opp.motion = MU_PC_MS_WAIT;
  v.cpu.facing = 1.0f;
  v.opp.x = 20.0f;
  v.opp.facing = -1.0f;
  v.cpu.can_act = 1;
  v.cpu.jumps_left = 1;
  v.ledge_x = 68.4f;
  v.land_frames = -1;
  return v;
}

// Settings that only move (no attacks), with one movement option.
static MuPcConfig moving(int move2) {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.aggression = 0;
  std::memset(c.move_odds, 0, sizeof c.move_odds);
  std::memset(c.move2_odds, 0, sizeof c.move2_odds);
  c.move2_odds[move2] = 100;
  return c;
}

static void test_load_version_1() {
  MuPcConfig def;
  mu_pc_defaults(&def);
  unsigned char v1[MU_PC_CONFIG_V1_SIZE];
  std::memcpy(v1, &def, sizeof v1);
  v1[0] = 1;
  v1[offsetof(MuPcConfig, aggression)] = 77;
  MuPcConfig c;
  std::memset(&c, 0xAB, sizeof c);
  CHECK(mu_pc_load(v1, sizeof v1, &c) == 1);
  CHECK(c.version == MU_PC_CONFIG_VERSION);
  CHECK(c.aggression == 77);
  CHECK(c.throw_odds[MU_PC_THROW_FORWARD] == 25 && c.throw_odds[MU_PC_THROW_DOWN] == 25);
  CHECK(c.move2_odds[MU_PC_MOVE2_WAVELAND] == 0);
  CHECK(c.rng_mode == MU_PC_RNG_GAME && c.aerial_timing == MU_PC_TIMING_RISING);
  // This version round trips; other sizes and versions are refused.
  MuPcConfig back;
  CHECK(mu_pc_load(reinterpret_cast<const unsigned char*>(&c), sizeof c, &back) == 1);
  CHECK(std::memcmp(&back, &c, sizeof c) == 0);
  v1[0] = 9;
  CHECK(mu_pc_load(v1, sizeof v1, &back) == 0);
  CHECK(mu_pc_load(reinterpret_cast<const unsigned char*>(&c), sizeof c - 1, &back) == 0);
}

static void test_throw() {
  for (int which = 0; which < MU_PC_THROW_COUNT; ++which) {
    MuPcConfig c;
    mu_pc_defaults(&c);
    c.enabled = 1;
    std::memset(c.throw_odds, 0, sizeof c.throw_odds);
    c.throw_odds[which] = 100;
    MuPcState s;
    mu_pc_reset(&s, 7);
    MuPcView v = view();
    v.cpu.motion = MU_PC_MS_CATCH_WAIT;
    v.cpu.can_act = 0;
    v.cpu.facing = -1.0f;
    bool thrown = false;
    for (int f = 0; f < 20 && !thrown; ++f) {
      MuPcInput in;
      mu_pc_frame(&c, &s, &v, &in);
      const bool fwd = in.lx < -0.5f, back = in.lx > 0.5f, up = in.ly > 0.5f, down = in.ly < -0.5f;
      if (fwd || back || up || down) {
        thrown = true;
        CHECK(which != MU_PC_THROW_FORWARD || fwd);
        CHECK(which != MU_PC_THROW_BACK || back);
        CHECK(which != MU_PC_THROW_UP || up);
        CHECK(which != MU_PC_THROW_DOWN || down);
      }
    }
    CHECK(thrown);
  }
  // No throw weights: it keeps holding, as before.
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  std::memset(c.throw_odds, 0, sizeof c.throw_odds);
  MuPcState s;
  mu_pc_reset(&s, 7);
  MuPcView v = view();
  v.cpu.motion = MU_PC_MS_CATCH_WAIT;
  v.cpu.can_act = 0;
  for (int f = 0; f < 30; ++f) {
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.lx == 0.0f && in.ly == 0.0f);
  }
}

static void test_floorhug() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.floorhug = 100;
  MuPcState s;
  mu_pc_reset(&s, 3);
  MuPcView v = view();
  v.cpu.hitlag = 1;
  v.cpu.can_act = 0;
  MuPcInput in;
  for (int f = 0; f < 5; ++f) {
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.ly == -1.0f && in.cy == -1.0f && in.lx == 0.0f);
  }
  // In the air it DIs as set instead.
  mu_pc_reset(&s, 3);
  v.cpu.airborne = 1;
  std::memset(c.di_odds, 0, sizeof c.di_odds);
  c.di_odds[MU_PC_DI_IN] = 100;
  c.sdi_amount = 0;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(in.lx == 1.0f && in.cy == 0.0f);
}

static void test_waveland() {
  MuPcConfig c = moving(MU_PC_MOVE2_WAVELAND);
  MuPcState s;
  mu_pc_reset(&s, 11);
  MuPcView v = view();
  MuPcInput in;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(in.buttons & MU_PC_X);   // the hop
  v.cpu.airborne = 1;
  v.cpu.can_act = 0;
  v.cpu.vy = 1.0f;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(!(in.buttons & MU_PC_L));   // not on the way up
  v.cpu.vy = -1.5f;
  v.land_frames = 2;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK((in.buttons & MU_PC_L) && in.ly < 0.0f && in.lx != 0.0f);   // the air dodge into the floor
}

static void test_shield_drop() {
  MuPcConfig c = moving(MU_PC_MOVE2_SHIELD_DROP);
  // On a platform: shield, then the stick down to the shield drop notch.
  MuPcState s;
  mu_pc_reset(&s, 5);
  MuPcView v = view();
  v.cpu.y = 27.2f;
  bool shielded = false, dropped = false;
  for (int f = 0; f < 12; ++f) {
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    shielded |= (in.buttons & MU_PC_R) != 0;
    // Inside the notch: the game's drop threshold (y <= -0.66) short of its spot dodge (y <= -0.7),
    // also after the Static Recomp's 1/128 steps.
    const float static_ly = std::lround(in.ly * 128.0f) / 128.0f;
    dropped |= (in.buttons & MU_PC_R) && in.ly <= -0.66f && in.ly > -0.7f && static_ly <= -0.66f && static_ly > -0.7f;
  }
  CHECK(shielded && dropped);
  // On the main stage it never pulls down (that would be a spot dodge).
  mu_pc_reset(&s, 5);
  v.cpu.y = 0.0f;
  for (int f = 0; f < 12; ++f) {
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.ly == 0.0f);
  }
}

static void test_dash_jump_run_past_turn() {
  {
    MuPcConfig c = moving(MU_PC_MOVE2_DASH_JUMP);
    MuPcState s;
    mu_pc_reset(&s, 13);
    MuPcView v = view();
    int jump_at = -1;
    for (int f = 0; f < 8; ++f) {
      MuPcInput in;
      mu_pc_frame(&c, &s, &v, &in);
      if ((in.buttons & MU_PC_X) && jump_at < 0) jump_at = f;
      if (f < 5) CHECK(in.lx == 1.0f || in.lx == -1.0f);   // the dash
    }
    CHECK(jump_at == 5);
  }
  {
    MuPcConfig c = moving(MU_PC_MOVE2_RUN_PAST);
    MuPcState s;
    mu_pc_reset(&s, 17);
    MuPcView v = view();
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.lx == 1.0f);   // toward the opponent on its right
    v.cpu.x = 30.0f;        // past them, but not far enough yet
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.lx == 1.0f);
    v.cpu.x = 40.0f;        // 20 past: it stops
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.lx == 0.0f);
  }
  {
    MuPcConfig c = moving(MU_PC_MOVE2_TURN);
    MuPcState s;
    mu_pc_reset(&s, 19);
    MuPcView v = view();
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.lx < 0.0f && in.lx > -0.5f);   // a soft tilt away from its facing: a turn, not a dash
  }
}

static void test_double_jump_and_fast_fall_delay() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.aggression = 0;
  std::memset(c.move_odds, 0, sizeof c.move_odds);
  c.move_odds[MU_PC_MOVE_SHORT_HOP] = 100;
  c.double_jump = 100;
  c.fast_fall = 0;
  MuPcState s;
  mu_pc_reset(&s, 23);
  MuPcView v = view();
  MuPcInput in;
  mu_pc_frame(&c, &s, &v, &in);   // the hop
  v.cpu.airborne = 1;
  v.cpu.can_act = 0;
  v.cpu.vy = 0.5f;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(!(in.buttons & MU_PC_X));
  v.cpu.vy = 0.0f;   // the top
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(in.buttons & MU_PC_X);

  // Fast fall 8 frames after the top, not before.
  c.double_jump = 0;
  c.fast_fall = 100;
  c.fast_fall_delay = 8;
  mu_pc_reset(&s, 29);
  v = view();
  mu_pc_frame(&c, &s, &v, &in);
  v.cpu.airborne = 1;
  v.cpu.can_act = 0;
  v.cpu.vy = -0.5f;
  int ff_at = -1;
  for (int f = 0; f < 20 && ff_at < 0; ++f) {
    mu_pc_frame(&c, &s, &v, &in);
    if (in.ly < -0.9f) ff_at = f;
  }
  CHECK(ff_at == 8);
}

static void test_aerial_timing() {
  for (int timing = MU_PC_TIMING_RISING; timing <= MU_PC_TIMING_FALLING; ++timing) {
    MuPcConfig c;
    mu_pc_defaults(&c);
    c.enabled = 1;
    c.aggression = 100;
    c.attack_range = 40;
    std::memset(c.attack_odds, 0, sizeof c.attack_odds);
    c.attack_odds[MU_PC_ATK_NAIR] = 100;
    c.full_hop_aerials = 0;
    c.double_jump = 0;
    c.aerial_timing = (unsigned char)timing;
    MuPcState s;
    mu_pc_reset(&s, 31);
    MuPcView v = view();
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);   // the jump
    v.cpu.airborne = 1;
    v.cpu.can_act = 0;
    v.cpu.vy = 1.0f;
    mu_pc_frame(&c, &s, &v, &in);
    const bool rising_press = (in.buttons & MU_PC_A) != 0;
    v.cpu.vy = -0.5f;
    mu_pc_frame(&c, &s, &v, &in);
    const bool falling_press = (in.buttons & MU_PC_A) != 0;
    if (timing == MU_PC_TIMING_RISING) CHECK(rising_press);
    else CHECK(!rising_press && falling_press);
  }
}

// An opponent on a platform above: no grabbing at the air under them.
static void test_no_ground_attack_at_platform() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.aggression = 100;
  c.attack_range = 40;
  std::memset(c.attack_odds, 0, sizeof c.attack_odds);
  c.attack_odds[MU_PC_ATK_GRAB] = 100;
  MuPcState s;
  mu_pc_reset(&s, 37);
  MuPcView v = view();
  v.opp.x = 4.0f;
  v.opp.y = 27.2f;
  for (int f = 0; f < 60; ++f) {
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(!(in.buttons & MU_PC_Z));
  }
  v.opp.y = 0.0f;   // on its floor: it grabs
  bool grabbed = false;
  for (int f = 0; f < 60 && !grabbed; ++f) {
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    grabbed = (in.buttons & MU_PC_Z) != 0;
  }
  CHECK(grabbed);
}

// Ledgedash: drop, jump at once, and the air dodge only once the fighter is above the stage. It used
// to come two frames after the jump, below the ledge, into the wall (a player saw the CPU fall).
static void test_ledgedash() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  std::memset(c.ledge_odds, 0, sizeof c.ledge_odds);
  c.ledge_odds[MU_PC_LEDGE_DASH] = 100;
  c.ledge_delay = 0;
  MuPcState s;
  mu_pc_reset(&s, 7);
  MuPcView v = view();
  v.ledge_y = 0.0f;
  v.cpu.x = 70.0f;   // hanging on the right ledge: the stage is to the left
  v.cpu.y = -15.0f;
  v.cpu.motion = MU_PC_MS_CLIFF_WAIT;
  v.cpu.can_act = 1;
  MuPcInput in;
  bool dropped = false, jumped = false;
  for (int i = 0; i < 8 && !jumped; ++i) {   // the wait (at most one frame here), the drop, the jump
    mu_pc_frame(&c, &s, &v, &in);
    if (in.lx > 0.5f) dropped = true;   // away from the stage
    if (in.buttons & MU_PC_X) { jumped = true; CHECK(in.lx < 0.0f); }
    if (dropped) { v.cpu.motion = 0x1D; v.cpu.airborne = 1; v.cpu.can_act = 0; }   // Fall
  }
  CHECK(dropped && jumped);
  // Rising, still below the stage: no dodge, drifting in.
  v.cpu.vy = 2.5f;
  const float rising[] = {-12.0f, -6.0f, -1.0f, 1.5f};
  for (float y : rising) {
    v.cpu.y = y;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(!(in.buttons & MU_PC_L) && in.lx < 0.0f);
  }
  // Above the stage: the dodge in and down.
  v.cpu.y = 3.0f;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK((in.buttons & MU_PC_L) && in.lx < 0.0f && in.ly < 0.0f);
  // Only once.
  v.cpu.y = 2.0f;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(!(in.buttons & MU_PC_L));
}

static void test_rng_seed() {
  CHECK(mu_pc_rng_seed(0) == 1u);
  CHECK(mu_pc_rng_seed(5) == mu_pc_rng_seed(5));
  CHECK(mu_pc_rng_seed(5) != mu_pc_rng_seed(6));
}

// Moving target (no_attacks): every attack at full weight and full aggression, and still no attack,
// grab or special on stage: in neutral, out of shield, from the ledge and getting up.
static bool attack_plan(int plan) {
  return plan == PC_P_GROUND_ATTACK || plan == PC_P_AERIAL || plan == PC_P_SPECIAL || plan == PC_P_THROW;
}
static void test_moving_target() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.no_attacks = 1;
  c.aggression = 100;
  c.attack_range = 80;
  for (auto& w : c.attack_odds) w = 100;
  std::memset(c.oos_odds, 0, sizeof c.oos_odds);
  c.oos_odds[MU_PC_OOS_NAIR] = c.oos_odds[MU_PC_OOS_GRAB] = c.oos_odds[MU_PC_OOS_SHINE] = c.oos_odds[MU_PC_OOS_UP_B] = 100;
  std::memset(c.ledge_odds, 0, sizeof c.ledge_odds);
  c.ledge_odds[MU_PC_LEDGE_ATTACK] = 100;
  std::memset(c.getup_odds, 0, sizeof c.getup_odds);
  c.getup_odds[MU_PC_GETUP_ATTACK] = 100;
  MuPcState s;
  std::memset(&s, 0, sizeof s);
  s.rng = 12345u;
  int pressed = 0, plans = 0, moved = 0;
  for (int frame = 0; frame < 6000; ++frame) {
    MuPcView v = view();
    v.frame = (mu_pc_u32) frame;
    v.opp.x = 10.0f + (float) (frame % 40);
    const int phase = (frame / 300) % 4;
    if (phase == 1 && frame % 300 == 0) v.cpu.motion = MU_PC_MS_GUARD_SET_OFF;   // shieldstun, then guard
    if (phase == 1 && frame % 300 == 1) v.cpu.motion = MU_PC_MS_GUARD;
    if (phase == 2 && frame % 300 < 2) v.cpu.motion = MU_PC_MS_CLIFF_WAIT;
    if (phase == 3 && frame % 300 < 2) v.cpu.motion = MU_PC_MS_DOWN_WAIT_U;
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    if (in.buttons & (MU_PC_A | MU_PC_B | MU_PC_Z)) ++pressed;
    if (attack_plan(s.plan)) ++plans;
    if (in.lx != 0.0f || (in.buttons & (MU_PC_X | MU_PC_Y | MU_PC_L | MU_PC_R))) ++moved;
  }
  CHECK(pressed == 0);
  CHECK(plans == 0);
  CHECK(moved > 500);   // it still moves around
  // Off again: the same settings attack.
  c.no_attacks = 0;
  std::memset(&s, 0, sizeof s);
  s.rng = 12345u;
  int attacks = 0;
  for (int frame = 0; frame < 600; ++frame) {
    MuPcView v = view();
    v.frame = (mu_pc_u32) frame;
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    if (attack_plan(s.plan)) ++attacks;
  }
  CHECK(attacks > 0);
}

static_assert(offsetof(MuPcConfig, def2_odds) == MU_PC_CONFIG_V2_SIZE, "version 3 follows version 2's bytes");

// Version 3: a saved version 2 setting loads with the new options off.
static void test_load_version_2() {
  MuPcConfig def;
  mu_pc_defaults(&def);
  unsigned char v2[MU_PC_CONFIG_V2_SIZE];
  std::memcpy(v2, &def, sizeof v2);
  v2[0] = 2;
  v2[offsetof(MuPcConfig, floorhug)] = 40;
  MuPcConfig c;
  std::memset(&c, 0xAB, sizeof c);
  CHECK(mu_pc_load(v2, sizeof v2, &c) == 1);
  CHECK(c.version == MU_PC_CONFIG_VERSION && c.floorhug == 40);
  CHECK(c.def2_odds[0] == 0 && c.tech2_odds[1] == 0 && c.act_delay == 0 && c.throw_wait == 0);
  CHECK(c.respawn == MU_PC_RESPAWN_WAIT && c.run_shine == 0 && c.no_attacks == 0);
}

// Rolls to a side of the stage: on the right half the ledge is to the right, the middle to the left,
// whichever side the opponent is on.
static void test_side_rolls() {
  for (int side = 0; side < MU_PC_SIDE_COUNT; ++side) {
    MuPcConfig c;
    mu_pc_defaults(&c);
    c.enabled = 1;
    std::memset(c.tech_odds, 0, sizeof c.tech_odds);
    std::memset(c.tech2_odds, 0, sizeof c.tech2_odds);
    c.tech2_odds[side] = 100;
    MuPcState s;
    std::memset(&s, 0, sizeof s);
    MuPcView v = view();
    v.cpu.x = 40.0f;   // right half
    v.opp.x = 60.0f;   // the opponent further right: "away" would be left, "in" right
    v.cpu.airborne = 1;
    v.cpu.hitstun = 1;
    v.cpu.motion = MU_PC_MS_DAMAGE_FALL;
    v.land_frames = 3;
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    CHECK(in.r > 0.0f || (in.buttons & (MU_PC_L | MU_PC_R)));   // the tech press
    CHECK(in.lx == (side == MU_PC_SIDE_LEDGE ? 1.0f : -1.0f));
  }
  // Defense: roll to the middle from the left half goes right.
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  std::memset(c.defend_odds, 0, sizeof c.defend_odds);
  c.def2_odds[MU_PC_SIDE_MIDDLE] = 100;
  c.reaction_frames = 0;
  MuPcState s;
  std::memset(&s, 0, sizeof s);
  MuPcView v = view();
  v.cpu.x = -30.0f;
  v.opp.x = -40.0f;
  v.opp.motion = MU_PC_MS_ATTACK_11;
  MuPcInput in;
  mu_pc_frame(&c, &s, &v, &in);
  CHECK(s.plan == PC_P_ROLL && s.plan_dir == 1);
}

// Respawn platform: "get off" holds down there; "wait" leaves it to the game.
static void test_respawn() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  MuPcState s;
  std::memset(&s, 0, sizeof s);
  MuPcView v = view();
  v.cpu.motion = MU_PC_MS_REBIRTH_WAIT;
  MuPcInput in;
  CHECK(mu_pc_frame(&c, &s, &v, &in) == 0);   // wait: the game's own
  c.respawn = MU_PC_RESPAWN_LEAVE;
  int down = 0;
  for (int f = 0; f < 6; ++f) { v.frame = (mu_pc_u32) f; CHECK(mu_pc_frame(&c, &s, &v, &in) == 1); down += in.ly < 0.0f; }
  CHECK(down >= 3);
}

// CPU delay out of shield: it keeps shielding for the delay, then acts; throw wait holds that long.
static void test_delays() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.act_delay = 10;
  std::memset(c.oos_odds, 0, sizeof c.oos_odds);
  c.oos_odds[MU_PC_OOS_JUMP] = 100;
  MuPcState s;
  std::memset(&s, 0, sizeof s);
  MuPcView v = view();
  v.cpu.motion = MU_PC_MS_GUARD_SET_OFF;
  MuPcInput in;
  mu_pc_frame(&c, &s, &v, &in);
  v.cpu.motion = MU_PC_MS_GUARD;
  int first_jump = -1;
  for (int f = 1; f < 30 && first_jump < 0; ++f) {
    v.frame = (mu_pc_u32) f;
    mu_pc_frame(&c, &s, &v, &in);
    if (in.buttons & MU_PC_X) first_jump = f;
  }
  CHECK(first_jump >= 10);
  // throw wait
  MuPcConfig t;
  mu_pc_defaults(&t);
  t.enabled = 1;
  t.throw_wait = 20;
  MuPcState ts;
  std::memset(&ts, 0, sizeof ts);
  MuPcView tv = view();
  tv.cpu.motion = MU_PC_MS_CATCH_WAIT;
  int first_stick = -1;
  for (int f = 0; f < 40 && first_stick < 0; ++f) {
    tv.frame = (mu_pc_u32) f;
    mu_pc_frame(&t, &ts, &tv, &in);
    if (in.lx != 0.0f || in.ly != 0.0f) first_stick = f;
  }
  CHECK(first_stick >= 19);
}

// Run shine: a dash toward the opponent, then down + B.
static void test_run_shine() {
  MuPcConfig c;
  mu_pc_defaults(&c);
  c.enabled = 1;
  c.aggression = 100;
  c.attack_range = 80;
  std::memset(c.attack_odds, 0, sizeof c.attack_odds);
  c.run_shine = 100;
  MuPcState s;
  std::memset(&s, 0, sizeof s);
  int dashed = 0, shined = 0;
  for (int f = 0; f < 60 && !shined; ++f) {
    MuPcView v = view();
    v.frame = (mu_pc_u32) f;
    v.opp.x = 30.0f;
    MuPcInput in;
    mu_pc_frame(&c, &s, &v, &in);
    if (in.lx > 0.5f) ++dashed;
    if ((in.buttons & MU_PC_B) && in.ly < 0.0f) shined = 1;
  }
  CHECK(dashed > 0 && shined);
}

int main() {
  test_load_version_2();
  test_side_rolls();
  test_respawn();
  test_delays();
  test_run_shine();
  test_moving_target();
  test_load_version_1();
  test_throw();
  test_floorhug();
  test_waveland();
  test_shield_drop();
  test_dash_jump_run_past_turn();
  test_double_jump_and_fast_fall_delay();
  test_aerial_timing();
  test_no_ground_attack_at_platform();
  test_ledgedash();
  test_rng_seed();
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("practice cpu model: ok\n");
  return 0;
}
