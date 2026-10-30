// The Practice CPU's decision model (mu_practice_cpu_impl.inl): saved settings from version 1 still load,
// and the version 2 options (throws, floorhug, waveland, shield drop, dash jump, run past, turn around,
// double jumps, fast fall delay, aerial timing, RNG seeds) give the controller they are meant to.
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
    dropped |= (in.buttons & MU_PC_R) && in.ly < -0.7f && in.ly > -0.8f;
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

static void test_rng_seed() {
  CHECK(mu_pc_rng_seed(0) == 1u);
  CHECK(mu_pc_rng_seed(5) == mu_pc_rng_seed(5));
  CHECK(mu_pc_rng_seed(5) != mu_pc_rng_seed(6));
}

int main() {
  test_load_version_1();
  test_throw();
  test_floorhug();
  test_waveland();
  test_shield_drop();
  test_dash_jump_run_past_turn();
  test_double_jump_and_fast_fall_delay();
  test_aerial_timing();
  test_no_ground_attack_at_platform();
  test_rng_seed();
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("practice cpu model: ok\n");
  return 0;
}
