// Window-free execution of the production native predictor, console lifecycle and RXB ABI.
// The scene body is a deterministic integer model; checkpoints use native_savestate::Engine.
// Included by yougame_console_engine_test.cpp, which supplies CHECK and prerecorded hardware().
namespace rollback_test {
#include <melee/gm/types.h>
#include <sysdolphin/baselib/rumble.h>
using u8 = uint8_t; using s8 = int8_t; using u16 = uint16_t;
using u32 = uint32_t; using s16 = int16_t;
struct PADStatus {
  u16 button; s8 stickX, stickY, substickX, substickY;
  u8 triggerLeft, triggerRight, analogA, analogB; s8 err;
};
enum {
  ROLLBACK_MAX = 7, PREDICTED_INPUTS_COUNT = 14, RXB_INPUTS_COUNT = 7, REMOTES = 3,
  PAD_SIZE = 12, USED_PADS_COUNT = 16, START_SYNC_FRAME = 78,
  // Slippi 3.7.0's RXB (mu_online.c): despawn frames, predicted inputs and the next pad's RNG bits follow the inputs.
  RXB_OPNT_COUNT = 1, RXB_OPNT_FRAME_NUMS = 26, RXB_SMALLEST_LATEST = 38, RXB_OPNT_INPUTS = 42,
  RXB_DESPAWN_FRAMES = RXB_OPNT_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES,
  RXB_OPNT_PREDICTED_INPUTS = RXB_DESPAWN_FRAMES + 4 * REMOTES,
  RXB_NEXT_PAD_RNG_BITS = RXB_OPNT_PREDICTED_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES,
  RXB_SIZE = RXB_NEXT_PAD_RNG_BITS + 1,
  RNG_HISTORY_COUNT = 16, PAD_RNG_BITS = 8, PAD_RNG_ROLLBACK_MARKER = 11,
  SIDE_B_MIN_STICK_X = 48, DOWN_B_MAX_STICK_Y = -44,
  CKind_GameWatch = 3, CKind_Luigi = 7, CKind_Peach = 12,
  ENGINE_CONTINUE = 0, ENGINE_RESIM, ENGINE_ROLLBACK_DONE,
  CONSOLE_QUERY = 0, CONSOLE_MATCH_BEGIN, CONSOLE_FORWARD, CONSOLE_INPUTS,
  CONSOLE_REPLAY, CONSOLE_MATCH_END, CONSOLE_FAIL,
  CONSOLE_BOUNDARY, CONSOLE_BODY_END, CONSOLE_CONFIRM, CONSOLE_COMMIT, CONSOLE_SERVICE,
       CONSOLE_VALIDATED,
  GS_VS = 2, GS_SUDDEN_DEATH = 3,
  MU_RC_INIT_STAGE_DATA = 1, MU_RC_INIT_PLAYER_DATA = 2, MU_RC_OFFSCREEN_DAMAGE = 4,
  MU_RC_DEAD_UP_FALL = 8, MU_RC_FD_BG_SEED = 16, MU_RC_PS_ZERO_BUFFER = 32,
  MU_RC_PS_IS_VALID = 64, MU_RC_PS_FILE_LOAD = 128, MU_RC_WHISPY_FIX = 256,
  MU_RC_NANA_DETERMINISM = 512, MU_RC_PS_MONITOR = 1024
};
// Only fields accessed by the included production routines; no bookkeeping is snapshotted.
struct Bookkeeping {
  int console_setup, console_active, console_injecting; unsigned console_epoch;
  int delay, game_over, local_index, finalized, stable_finalized, frame;
  u8 rxb[RXB_SIZE], last_local[12]; unsigned rxb_size;
  int rollback_active, rollback_should_load, rollback_end_frame, resim;
  int stable_rollback_active, stable_rollback_end_frame, stable_should_load, stable_savestate_frame;
  int savestate_predicting, savestate_frame, player_savestate_frame[3];
  u8 player_predicting[3], predicted_read[3], predicted_write[3];
  u8 predicted[3][14][12], predicted_early[3][14];
  int used_frame[16]; u8 used_confirmed[16], used_pads[16][4][12];
  unsigned early_used, predictions, mispredictions, early_wrong, early_right, early_rollbacks;
  unsigned early_scored[3], early_missed[3]; u8 early_distrusted[3];
  int selftest_rollback; unsigned long long rollback_start_ticks;
  unsigned rollbacks, max_depth, loads, captures, resim_frames;
  u8 b_held_history[RNG_HISTORY_COUNT], rng_seed_bits_history[RNG_HISTORY_COUNT];
  unsigned rng_bit_frames, rng_rollbacks;
};
struct State {
  gm_80479D58_t engine;
  HSD_RumbleData rumble[4];
  uint64_t hash = 17;
  uint32_t rng = 123, timer = 8 * 60 * 60;
  int stocks[4]{7, 5, 3, 2}, damage[4]{};
  int rules = 0x1234567, paused = 0;
  int rumble_wait = 4, rumble_expired = -1, rumble_steps = 0;
};
struct Network {
  struct Packet { int to, from; uint32_t frame; MuPadStatus pad; uint64_t at; };
  std::vector<Packet> pending;
  std::map<uint32_t, MuPadStatus> authoritative[4], delivered[4][4];
  std::map<uint32_t, uint64_t> digests[4];
  uint64_t now = 0, waits = 0;
  uint32_t origin = 10;
  int depth = 0, loss = 0, reordered = 0, sends[4]{};
  bool noisy = false, over_horizon = false;
  bool race = false, race_replay = false, released_59 = false, released_57 = false;
  int terminal = 0;
  std::map<uint32_t, uint64_t> expected;
  std::map<uint32_t, unsigned> digest_sends[4];
  void pump(int ms) {
    now += ms; waits += ms;
    for (auto it = pending.begin(); it != pending.end();) {
      if (it->at > now) { ++it; continue; }
      auto& history = delivered[it->to][it->from];
      if (!history.empty() && history.rbegin()->first > it->frame) ++reordered;
      history.emplace(it->frame, it->pad);
      it = pending.erase(it);
    }
  }
  MuPadStatus sample(int mine, uint32_t f) const {
    MuPadStatus p{};
    if (mine == 3 && f >= origin) {
      const auto body = f - origin;
      if (terminal == 1) p.button = body == 1;
      if (terminal == 2 || terminal == 3) p.button = body >= 1;
      if (terminal == 4) p.button = body >= 2 ? 8 : 0;
      if (terminal == 5) p.stick_x = body >= 1 ? 42 : 0;
    }
    return p;
  }
  void elapse(int ms) { now += ms; pump(0); } // simulation pacing is not link waiting
};
struct Link : SessionLink {
  Network& n; int mine;
  Link(Network& net, int port) : n(net), mine(port - 1) {}
  void send_pad(uint32_t f, const MuPadStatus& hardware) override {
    MuPadStatus p = hardware;
    CHECK(n.authoritative[mine].emplace(f, p).second); // duplicate issuance is a test failure
    ++n.sends[mine];
    for (int to : {1, 3}) if (to != mine) { // sparse ports 2 and 4
      uint64_t transit = 0;
      if (mine == 3 && f == n.origin + 1 && n.depth)
        transit = uint64_t(n.depth + 2) * 17; // sampled two frames early; delivered at b=depth+1
      if (n.noisy && f >= n.origin + 2) {
        transit = mine == 3 ? 15 + (f * 17 % 61) : f % 11;
        if (f % 9 == 0) { transit += 50; ++n.loss; } // dropped first transmission, later retry
      }
      if (n.over_horizon && mine == 3 && f == n.origin + 1) transit = 600;
      if (n.terminal && mine == 3 && f == n.origin + (n.terminal == 1 || n.terminal == 4 ? 2 : 1))
        transit = 7 * 17;
      n.pending.push_back({to, mine, f, p, n.now + transit});
    }
    n.pump(0);
  }
  std::optional<MuPadStatus> pad(int p, uint32_t f) override {
    n.pump(0);
    if (n.race && mine == 1 && p == 4) {
      if (f == 59 && !n.released_59) return {};
      if (n.race_replay && f == 57 && !n.released_57) return {};
    }
    auto& h = n.delivered[mine][p - 1]; auto it = h.find(f);
    return it == h.end() ? std::nullopt : std::optional<MuPadStatus>(it->second);
  }
  void send_digest(uint32_t f, uint64_t d) override {
    CHECK(++n.digest_sends[mine][f] == 1);
    CHECK(n.digests[mine].emplace(f, d).second);
    if (f && f >= n.origin) {
      const int remote = mine == 1 ? 3 : 1;
      CHECK(n.delivered[mine][remote].count(f - 1)); // every causal input is authoritative
    }
    if (n.expected.count(f)) CHECK(d == n.expected.at(f));
  }
  std::optional<uint64_t> digest(int p, uint32_t f) override {
    auto& h = n.digests[p - 1]; auto it = h.find(f);
    return it == h.end() ? std::nullopt : std::optional<uint64_t>(it->second);
  }
  void pump(int ms) override { n.pump(ms); }
  std::optional<std::string> lost() const override { return {}; }
};
// Model-only callback transcript: this binary does NOT link mu_os.c/mu_vi.c, OSSetAlarm,
// mu_poll, OSRestoreInterrupts or VIWaitForRetrace dispatch. Observe the existing rumble
// wait adapter below; these entries are not evidence about production hardware callbacks.
struct ModelCallback {
  std::string handler;
  int payload;
  uint64_t ticks, offset; // logical time and position after this body's boundary
  bool operator==(const ModelCallback& other) const {
    return handler == other.handler && payload == other.payload &&
        ticks == other.ticks && offset == other.offset;
  }
};
struct CallbackAttempt {
  int body;
  uint64_t start;
  bool replay;
  std::vector<ModelCallback> entries; // delivery order, including duplicates; never overwritten
};
struct Context {
  Engine session; Bookkeeping online{}; State state; Network* network = nullptr;
  native_savestate::Engine snapshots;
  Pads injected{};
  std::vector<std::string> logs;
  int samples = 0, phases = 0, cameras = 0;
  bool phase = false, missing = false;
  std::vector<uint8_t> audio_state;
  int voices = 0, keyoffs = 0, motors = 0, last_motor = 0, music = 0;
  bool slippi = false;
  bool transcript = false;
  unsigned pending_wait_loads = 0;
  std::vector<CallbackAttempt> callbacks; // observation only: outside the snapshotted State
};
Pads issued_hardware(Context& c) {
  const auto frame = c.network && c.network->authoritative[c.session.config().my_port - 1].size() ==
      size_t(c.session.config().input_delay) ? 0 : c.session.frame() + 1;
  if (!c.network || !c.network->terminal) return hardware(frame * 3 + c.session.config().my_port);
  Pads pads; for (auto& p : pads) p.err = -1;
  pads[0] = c.network->sample(c.session.config().my_port - 1, frame + c.session.config().input_delay);
  return pads;
}
Context* current;
void select(Context&);
MuStatePod diagnostic(const State&, bool console = false);
#define mu_online (rollback_test::current->online)
struct SceneInfo { int scene_kind = GS_VS; } scene_info;
SceneInfo* gm_804D6720 = &scene_info;
gm_80479D58_t* mu_gm_engine_state() { return &current->state.engine; }
int global_frame() { return current->state.engine.unk_0; }
bool trace_renew() { return false; }
unsigned long long mu_ticks_since_start() { return 0; }
const u8* early_pad(int, int, int) { return nullptr; }
void logf_(const char* fmt, ...) {
  char line[512]; va_list args; va_start(args, fmt); vsnprintf(line, sizeof line, fmt, args); va_end(args);
  current->logs.emplace_back(line);
}
void wire_to_pad(PADStatus* p, const u8* a) {
  *p = {u16(a[0] << 8 | a[1]), s8(a[2]), s8(a[3]), s8(a[4]), s8(a[5]),
        a[6], a[7], a[8], a[9], s8(a[10])};
}
void pad_to_wire(u8* a, const PADStatus* p) {
  a[0] = p->button >> 8; a[1] = p->button;
  a[2] = p->stickX; a[3] = p->stickY; a[4] = p->substickX; a[5] = p->substickY;
  a[6] = p->triggerLeft; a[7] = p->triggerRight; a[8] = p->analogA; a[9] = p->analogB;
  a[10] = p->err; a[11] = 0;
}
u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
int Player_GetPlayerCharacter(int) { return 0; }   // no character rolls with B here
// mu_online.c's port and reply helpers (Triples): a console session is always four ports, Slippi's layout.
int online_ports() { return 4; }
int online_remotes() { return REMOTES; }
const u8* rx_part(int count, unsigned base, unsigned stride) { return mu_online.rxb + base + stride * (unsigned)count; }
#define RX_FRAME_NUM(c) rx_part((c), RXB_OPNT_FRAME_NUMS, 4)
#define RX_INPUTS(c) rx_part((c), RXB_OPNT_INPUTS, PAD_SIZE * RXB_INPUTS_COUNT)
#define RX_PREDICTED(c) rx_part((c), RXB_OPNT_PREDICTED_INPUTS, PAD_SIZE * RXB_INPUTS_COUNT)
#include "../../sourceport/game/shim/mu_online_prediction.inc"
void camera_tasks() { ++current->cameras; }
// Audio production execution is added below; these declarations match the game header.
void mu_online_audio_match_start(int, int);
void mu_online_audio_frame_begin();
void mu_online_audio_frame_end();
void mu_online_audio_rollback_end();
void mu_online_audio_match_exit();
void mu_online_audio_console_restore(int);
void mu_online_audio_console_commit(int);
void mu_online_abi_resim_phase(int entering) {
  current->phase = entering; if (entering) ++current->phases;
}
bool capture_state(int f) {
  u8 payload[32]{}; payload[0] = u32(f) >> 24; payload[1] = u32(f) >> 16;
  payload[2] = u32(f) >> 8; payload[3] = f;
  unsigned got = 0;
  const auto result = ::mu_online_abi_command(0xB1, payload, sizeof payload, nullptr, 0, &got);
  ++mu_online.captures;
  CHECK(current->snapshots.kept() <= 7); return result == 0;
}
bool load_state(int f) {
  ++mu_online.loads;
  u8 payload[32]{}; payload[0] = u32(f) >> 24; payload[1] = u32(f) >> 16;
  payload[2] = u32(f) >> 8; payload[3] = f;
  unsigned got = 0;
  const bool loaded = ::mu_online_abi_command(0xB2, payload, sizeof payload, nullptr, 0, &got) == 0;
  if (loaded && current->transcript && f == 1) {
    CHECK(current->state.rumble_wait == 3); // registered before checkpoint, still pending on load
    ++current->pending_wait_loads;
  }
  return loaded;
}
int mu_console_abi(int, unsigned, u8*, u8*, unsigned*, int*);
static int console_pad_renew(PADStatus*);
void mu_console_replace_raw() {
  PADStatus p[4]{};
  CHECK(!console_pad_renew(p));
  for (int i = 0; i < 4; ++i)
    current->injected[i] = {p[i].button, p[i].stickX, p[i].stickY, p[i].substickX, p[i].substickY,
        p[i].triggerLeft, p[i].triggerRight, p[i].analogA, p[i].analogB, p[i].err};
}
void mu_console_rumble_step() {
  // Active wait adapter: its state belongs to the real native checkpoints, not audio bookkeeping.
  auto& s = current->state;
  auto record = [&](const char* handler, int payload) {
    if (!current->transcript) return;
    CHECK(!current->callbacks.empty());
    auto& attempt = current->callbacks.back();
    CHECK(attempt.body == s.engine.unk_0);
    const auto ticks = current->session.clock_ticks();
    attempt.entries.push_back({handler, payload, ticks, ticks - attempt.start});
  };
  record("model.rumble_step", s.rumble_wait);
  ++s.rumble_steps;
  if (s.rumble_wait && !--s.rumble_wait) {
    record("model.wait_expired", s.rumble_wait);
    s.rumble_expired = s.engine.unk_0;
  }
}
#include "../../sourceport/game/shim/mu_console_rollback.inc"
int mu_console_abi(int op, unsigned body, u8* local, u8* rxb, unsigned* epoch, int* port) {
  // Execute the actual ABI serializer below against a host callback for this instance.
  return test_console_abi(op, body, local, rxb, epoch, port);
}
int32_t host_call(int32_t op, uint32_t body, MuConsoleInputs* out) {
  auto& c = *current; auto& e = c.session;
  if (!e.rollback_enabled()) return 0;
  switch (op) {
  case MU_CONSOLE_QUERY: return 1;
  case MU_CONSOLE_SERVICE: e.service_clock(body != 0); return 1;
  case MU_CONSOLE_MATCH_BEGIN:
    CHECK(e.begin_match());
    c.snapshots.begin({{reinterpret_cast<uint8_t*>(&c.state), sizeof c.state}}, 7);
    out->epoch = e.match_epoch(); out->local_port = e.config().my_port - 1; return 1;
  case MU_CONSOLE_FORWARD:
    if (c.network->race_replay && e.config().my_port == 2 && body == 61)
      c.network->released_57 = true;
    ++c.samples;
    CHECK(!c.phase);
    if (!e.advance(issued_hardware(c), diagnostic(c.state))) return 0;
    CHECK(e.frame() == e.match_origin() + body);
    return e.inputs(body, true, *out);
  case MU_CONSOLE_REPLAY:
    CHECK(c.phase);
    if (c.network->race_replay && !c.network->released_59 && e.config().my_port == 2 && body == 60) {
      CHECK(c.online.player_predicting[2] && c.online.player_savestate_frame[2] == 59);
      CHECK(!c.network->digests[1].count(60));
      c.network->released_59 = true;
    }
    return e.replay(body) && e.inputs(body, false, *out);
  case MU_CONSOLE_INPUTS:
    if (c.network->race && !c.network->race_replay && e.config().my_port == 2 && body == 60) {
      // Exact review interleaving: check_predictions already ran; receive now sees body 59.
      CHECK(c.online.player_predicting[2] && c.online.player_savestate_frame[2] == 59);
      CHECK(!c.network->digests[1].count(60));
      c.network->released_59 = true;
    }
    return e.inputs(body, false, *out);
  case MU_CONSOLE_VALIDATED: return e.validated(body);
  case MU_CONSOLE_BOUNDARY:
    if (!e.boundary(body, diagnostic(c.state, true))) return 0;
    if (c.transcript) c.callbacks.push_back({int(body), e.clock_ticks(), c.phase, {}});
    return 1;
  case MU_CONSOLE_BODY_END: return e.finish_body(body);
  case MU_CONSOLE_CONFIRM: return e.confirm(body) && e.inputs(body, false, *out);
  case MU_CONSOLE_COMMIT:
    if (!e.commit_match(body, diagnostic(c.state, true))) return 0;
    c.snapshots.end(); return 1;
  case MU_CONSOLE_MATCH_END: e.end_match(); c.snapshots.end(); return 1;
  case MU_CONSOLE_FAIL: e.end("rollback failed"); return 1;
  default: return 0;
  }
}
// Execute the real presentation reconciler; hardware operations are observable counters.
enum { MU_SFX_NO_TICKET = -1, MU_SFX_ADOPTED = -2, MU_MUSIC_START = 1, MU_MUSIC_STOP = 2 };
int mu_online_active() { return current->slippi; }
int mu_online_resim_active() { return current->online.resim; }
int mu_redo_tracking() { return 0; }   // the tick re-run (mu_redo.c) is off in these games
void mu_online_abi_log(const char* line) { current->logs.emplace_back(line); }
int snprintf(char* out, size_t n, const char* format, ...) {
  va_list args; va_start(args, format); int result = ::vsnprintf(out, n, format, args); va_end(args);
  return result;
}
char* getenv(const char*) { return nullptr; }
HSD_RumbleData HSD_Rumble_804C22E0[4];
void HSD_AudioSFXKeyOff(int) { ++current->keyoffs; }
int AXDriver_8038E5DC() { return current->voices - current->keyoffs; }
void HSD_AudioPStreamStartChParam(const char*, u8, int) { ++current->music; }
void AXDriverStop() { --current->music; }
void mu_pad_motor(int port, int on) {
  CHECK(port == (current->slippi ? 2 : current->session.config().my_port - 1));
  ++current->motors; current->last_motor = on;
}
#define MU_NATIVE_H
#define MU_SLIPPI_CHAT_DECLS
#include "../../sourceport/game/shim/mu_online_audio.c"
#undef MU_NATIVE_H
#undef MU_SLIPPI_CHAT_DECLS
void select(Context& c) {
  if (current) {
    current->audio_state.resize(sizeof audio);
    std::memcpy(current->audio_state.data(), &audio, sizeof audio);
  }
  current = &c;
  if (c.audio_state.empty()) std::memset(&audio, 0, sizeof audio);
  else std::memcpy(&audio, c.audio_state.data(), sizeof audio);
  std::copy(std::begin(c.state.rumble), std::end(c.state.rumble), HSD_Rumble_804C22E0);
}
void sound_body() {
  int adopted = -1;
  int ticket = mu_online_sfx_begin(10 + current->state.engine.unk_0 % 4, 1, &adopted);
  if (ticket >= 0) mu_online_sfx_started(ticket, ++current->voices);
  const int local = current->session.config().my_port - 1;
  const int on = current->injected[local].button % 2;
  current->state.rumble[local].last_status = HSD_Rumble_804C22E0[local].last_status = on ? 2 : 0;
  const auto port = mu_online_motor_port(local, on);
  if (port >= 0) mu_pad_motor(port, on);
}
MuStatePod diagnostic(const State& s, bool console) {
  MuStatePod p{}; p.rng = s.rng; p.match_frame = s.engine.unk_0;
  p.menu_kind = uint32_t(s.hash); p.menu_previous = uint32_t(s.hash >> 32);
  for (int i = 0; i < 4; ++i) { p.player[i].stocks = s.stocks[i]; p.player[i].percent = s.damage[i]; }
  if (console) {
    p.console_valid = 1; p.console_result = s.engine.unk_C;
    p.console_seconds = s.timer / 60; p.console_timer_frames = s.timer % 60;
    p.console_teams = s.rules & 1; p.console_item_frequency = s.rules;
    p.console_pauser = s.paused;
    for (int i = 0; i < 4; ++i) { p.console_stocks[i] = s.stocks[i]; p.console_damage[i] = s.damage[i]; }
  }
  return p;
}
void simulate(State& s, const Pads& pads) {
  // All console fields affect the model, including presence and triggers below Slippi's deadzone.
  for (int p = 0; p < 4; ++p) {
    const auto& a = pads[p];
    const uint32_t fields[] = {a.button, uint8_t(a.stick_x), uint8_t(a.stick_y), uint8_t(a.sub_x),
      uint8_t(a.sub_y), a.trigger_l, a.trigger_r, a.analog_a, a.analog_b, uint8_t(a.err)};
    for (auto value : fields) { s.hash = s.hash * 31 + value + p; s.rng = s.rng * 1664525 + value + 1013904223; }
    s.damage[p] = (s.damage[p] + a.analog_a + a.trigger_l) % 999;
  }
  // Pause aliases the gameplay frame while total bodies and input history continue.
  s.paused = (s.engine.unk_0 % 19 >= 10);
  ++s.engine.unk_0;
  if (!s.paused) { ++s.engine.unk_8; --s.timer; }
}
bool same_state(const State& a, const State& b) {
  return a.hash == b.hash && a.rng == b.rng && a.timer == b.timer && a.rules == b.rules &&
      a.engine.unk_0 == b.engine.unk_0 && a.engine.unk_8 == b.engine.unk_8 &&
      std::equal(a.damage, a.damage + 4, b.damage) && std::equal(a.stocks, a.stocks + 4, b.stocks);
}
void step(Context& c) {
  select(c);
  int iterations = 0;
  for (;;) {
    console_frame_begin();
    if (c.state.engine.unk_C) return;
    // Game reads and polls must repeat, while between-body pacing is deliberately unequal.
    for (int i = 0; i < c.state.engine.unk_0 % 5; ++i) { c.session.ticks(0); c.session.poll(); }
    sound_body();
    simulate(c.state, c.injected);
    CHECK(++iterations <= 9);
    if (console_frame_end() != ENGINE_RESIM) break;
  }
  CHECK(!c.phase);
}
std::string digest_hex(uint64_t value) {
  char out[17]; ::snprintf(out, sizeof out, "%016llx", (unsigned long long) value); return out;
}
void hash_logs(Network& net, Context& a, Context& b) {
  for (auto* c : {&a, &b}) {
    const auto mine = c->session.config().my_port - 1;
    std::vector<std::string> hashes;
    for (const auto& line : c->logs) if (line.rfind("console: hash ", 0) == 0) hashes.push_back(line);
    CHECK(hashes.size() == net.digests[mine].size());
    for (const auto& [f, value] : net.digests[mine]) {
      const auto line = "console: hash " + std::to_string(f) + " " + digest_hex(value);
      CHECK(std::count(hashes.begin(), hashes.end(), line) == 1);
      CHECK(net.digests[mine == 1 ? 3 : 1].at(f) == value);
    }
  }
}
void callback_transcripts(Context& a, Context& b, int bodies, int depth) {
  std::map<int, const CallbackAttempt*> original;
  for (const auto& attempt : b.callbacks)
    original.emplace(attempt.body, &attempt); // first B execution; all later attempts checked below
  CHECK(original.size() == size_t(bodies));
  auto dump = [](const char* label, const CallbackAttempt& attempt) {
    std::cerr << label << " body " << attempt.body << (attempt.replay ? " replay" : " forward") << '\n';
    for (size_t i = 0; i < attempt.entries.size(); ++i) {
      const auto& entry = attempt.entries[i];
      std::cerr << "  position " << i << " handler " << entry.handler << " payload " << entry.payload
                << " logical ticks " << entry.ticks << " offset " << entry.offset << '\n';
    }
    if (attempt.entries.empty()) std::cerr << "  <no deliveries>\n";
  };
  for (auto* c : {&a, &b}) {
    CHECK(std::count_if(c->callbacks.begin(), c->callbacks.end(), [](const auto& attempt) {
      return attempt.replay;
    }) == c->online.resim_frames);
    for (const auto& attempt : c->callbacks) {
      const auto& expected = *original.at(attempt.body);
      if (attempt.entries != expected.entries) {
        std::cerr << "model callback transcript mismatch, depth " << depth << " seat "
                  << c->session.config().my_port << '\n';
        dump("original B", expected); dump("actual", attempt);
      }
      CHECK(attempt.entries == expected.entries);
      // A load must replay the pending wait, with one delivery at body 3 in EACH timeline.
      // Raw re-executions are retained above, not collapsed into a last-write-wins map.
      CHECK(attempt.entries.size() == size_t(attempt.body == 3 ? 2 : 1));
      CHECK(attempt.entries.front().handler == "model.rumble_step");
      CHECK(attempt.entries.front().offset == 0);
      if (attempt.body == 3) {
        CHECK(attempt.entries.back().handler == "model.wait_expired");
        CHECK(attempt.entries.back().payload == 0 && attempt.entries.back().offset == 0);
      }
    }
  }
  CHECK(a.pending_wait_loads > 0); // forced checkpoint 1 restores the wait scheduled before it
  CHECK(a.callbacks.size() > size_t(bodies)); // comparisons include repeated simulation bodies
  std::cout << "model callback transcript: " << bodies << " bodies, " << a.callbacks.size() << "/"
            << b.callbacks.size() << " forward/replay attempts equal; pending wait delivered once at body 3\n";
  a.transcript = b.transcript = false;
}
void boundary_race(bool replay_race) {
  ++cases;
  Network net; net.origin = 0; net.race = true; net.race_replay = replay_race;
  Context a, b;
  MuHostApi api{}; api.version = MU_HOST_API_VERSION; api.console_rollback = host_call;
  set_test_console_host(&api);
  for (auto* c : {&a, &b}) {
    c->network = &net;
    const int port = c == &a ? 2 : 4;
    auto cfg = config(port, 2); cfg.ports = {2, 4};
    EngineHooks h; h.now_ms = [&] { return net.now; };
    h.log = [c](const auto& line) { c->logs.push_back(line); };
    CHECK(c->session.begin(std::make_unique<Link>(net, port), cfg, h, true));
    select(*c); mu_console_match_setup();
  }
  State baseline;
  for (unsigned f = 0; f < 68; ++f) {
    net.elapse(17);
    if (f == 60) net.expected[f] = Engine::state_digest(diagnostic(baseline, true));
    Pads actual{}; for (auto& p : actual) p.err = -1;
    for (int p : {1, 3}) actual[p] = net.authoritative[p].at(f);
    simulate(baseline, actual);
    step(a); step(b);
    CHECK(a.session.active() && b.session.active());
    if (f == 60) CHECK(!net.digests[1].count(60));
    if (f == 61 && replay_race) CHECK(!net.digests[1].count(60));
  }
  CHECK(net.released_59 && (!replay_race || net.released_57));
  CHECK(a.online.loads == unsigned(replay_race ? 2 : 1));
  CHECK(same_state(a.state, baseline) && same_state(b.state, baseline));
  const auto line = "console: hash 60 " + digest_hex(net.expected.at(60));
  for (auto* c : {&a, &b}) {
    CHECK(net.digests[c->session.config().my_port - 1].at(60) == net.expected.at(60));
    CHECK(std::count(c->logs.begin(), c->logs.end(), line) == 1);
    CHECK(std::count_if(c->logs.begin(), c->logs.end(), [](const auto& s) {
      return s.rfind("console: hash 60 ", 0) == 0;
    }) == 1);
    CHECK(c->state.rumble_expired == 3 && c->state.rumble_steps == 68);
  }
  hash_logs(net, a, b);
  std::cout << "boundary 60 race (" << (replay_race ? "replay" : "forward") << "): corrected hash once per seat, " << a.online.loads << " loads\n";
  current = nullptr;
}
void run(int depth, bool noisy = false, bool over_horizon = false, bool missing = false) {
  ++cases;
  Network net; net.depth = depth; net.noisy = noisy; net.over_horizon = over_horizon;
  Context a, b;
  a.transcript = b.transcript = true;
  a.missing = missing;
  MuHostApi api{}; api.version = MU_HOST_API_VERSION; api.console_rollback = host_call;
  set_test_console_host(&api);
  for (auto* c : {&a, &b}) {
    c->network = &net;
    const int port = c == &a ? 2 : 4;
    auto cfg = config(port, 2); cfg.ports = {2, 4};
    EngineHooks h; h.now_ms = [&] { return net.now; };
    h.log = [c](const std::string& line) { c->logs.push_back(line); };
    h.responsive = [&, c] {
      if (net.over_horizon && c->state.engine.unk_0 == 8) {
        CHECK(c->snapshots.kept() == 7); CHECK(c->snapshots.has(1));
      }
    };
    CHECK(c->session.begin(std::make_unique<Link>(net, port), cfg, h, true));
  }
  for (unsigned s = 0; s < net.origin; ++s) {
    net.elapse(17);
    for (auto* c : {&a, &b}) CHECK(c->session.advance(hardware(s * 3 + c->session.config().my_port), {}));
  }
  for (auto* c : {&a, &b}) {
    select(*c); mu_console_match_setup(); CHECK(mu_online.console_setup);
    CHECK(mu_console_rollback_codes() != 0); CHECK(!mu_console_rollback_active());
  }
  State baseline;
  int corrected = 0;
  const int bodies = noisy ? 220 : 24;
  for (int f = 0; f < bodies; ++f) {
    if (noisy && f == bodies - 12) { net.noisy = false; net.elapse(200); }
    net.elapse(17);
    // Finish delivery before the baseline consumes this frame; only earlier owner samples apply.
    Pads actual{}; for (auto& p : actual) p.err = -1;
    for (int p : {1, 3}) actual[p] = net.authoritative[p].at(net.origin + f);
    net.expected[net.origin + f] = Engine::state_digest(diagnostic(baseline, true));
    simulate(baseline, actual);
    step(a); step(b);
    if (f) { // presentation and service counts differ, with no simulation-clock effect
      const auto before = a.session.clock_ticks();
      a.session.retrace(); a.session.poll(); a.session.poll(); b.session.poll();
      CHECK(a.session.clock_ticks() == before);
    }
    if (missing && !a.session.active()) {
      CHECK(a.state.engine.unk_C == 2); CHECK(!a.phase);
      CHECK(std::any_of(a.logs.begin(), a.logs.end(), [](const auto& s) {
        return s.find("required snapshot missing") != std::string::npos;
      }));
      current = nullptr;
      return;
    }
    CHECK(a.session.active() && b.session.active());
    for (auto* c : {&a, &b}) if (!c->online.savestate_predicting) {
      CHECK(same_state(c->state, baseline)); ++corrected;
    }
    CHECK(a.state.engine.unk_0 == f + 1 && b.state.engine.unk_0 == f + 1);
  }
  CHECK(!missing); CHECK(corrected > 5);
  // An active wait spans the forced correction/checkpoint at body 1 (depths 1..7).
  for (auto* c : {&a, &b}) {
    CHECK(c->state.rumble_expired == 3 && c->state.rumble_wait == 0);
    CHECK(c->state.rumble_steps == bodies);
  }
  CHECK(same_state(a.state, baseline) && same_state(b.state, baseline));
  CHECK(a.session.clock_ticks() == b.session.clock_ticks());
  CHECK(a.session.retrace_count() == b.session.retrace_count());
  CHECK(a.session.alarm_ticks() != b.session.alarm_ticks());
  callback_transcripts(a, b, bodies, depth);
  CHECK(a.online.loads > 0 && a.online.mispredictions > 0 && a.online.resim_frames > 0);
  CHECK(a.phases == int(a.online.loads)); CHECK(a.samples == bodies && b.samples == bodies);
  CHECK(a.online.max_depth <= 7); CHECK(a.snapshots.stats().missing_loads == 0);
  if (!noisy && !over_horizon) CHECK(a.online.max_depth == unsigned(depth));
  if (over_horizon) CHECK(net.waits >= 300);
  if (noisy) { CHECK(net.loss > 0); CHECK(net.reordered > 0); CHECK(b.online.loads > 0); }
  std::cout << "rollback " << (noisy ? "jitter/loss" : over_horizon ? "horizon stall" : "depth " + std::to_string(depth))
            << ": loads " << a.online.loads + b.online.loads << ", misses "
            << a.online.mispredictions + b.online.mispredictions << ", replay bodies "
            << a.online.resim_frames + b.online.resim_frames << ", corrected boundaries " << corrected
            << ", wait " << net.waits << " ms\n";
  // Canonical periodic candidates are finalized after correction, once per boundary.
  if (noisy) CHECK(net.digests[1].count(60) && net.digests[3].count(60));
  select(a);
  CHECK(audio.adopted >= a.online.loads);
  CHECK(a.voices == bodies && audio.started == unsigned(bodies));
  select(b);
  CHECK(b.voices == bodies && audio.started == unsigned(bodies));
  for (auto* c : {&a, &b}) {
    select(*c);
    // Finish this model match with an authoritative terminal body at the next boundary.
    console_frame_begin(); simulate(c->state, c->injected); c->state.engine.unk_C = 1;
    CHECK(!mu_online_scene_terminal());
    console_match_exit();
    CHECK(!audio.motor_on && !c->last_motor);
    CHECK(audio.beyond_ring == 0);
    CHECK(c->session.rollback_stats().commits == 1);
    CHECK(!c->session.match_active() && !mu_console_rollback_active());
    CHECK(mu_console_rollback_codes() == 0); CHECK(!c->snapshots.active());
  }
  CHECK(a.session.clock_ticks() == b.session.clock_ticks());
  // Repeated match: a new epoch and body zero, continuous session IDs and preserved RNG/rules.
  net.expected.clear();
  scene_info.scene_kind = GS_SUDDEN_DEATH;
  for (auto* c : {&a, &b}) {
    select(*c); c->state.engine = {}; mu_console_match_setup();
  }
  net.noisy = net.over_horizon = false; net.depth = 0;
  net.elapse(1000);
  step(a); step(b);
  CHECK(a.online.console_epoch == 2 && b.online.console_epoch == 2);
  CHECK(a.snapshots.kept() == 1 && b.snapshots.kept() == 1);
  CHECK(a.snapshots.has(0) && b.snapshots.has(0));
  CHECK(!a.snapshots.has(1) && !b.snapshots.has(1));
  CHECK(a.session.frame() == net.origin + bodies + 1 && b.session.frame() == net.origin + bodies + 1);
  CHECK(a.state.rules == baseline.rules && b.state.rules == baseline.rules);
  CHECK(net.sends[1] == int(a.session.frame() + 3));
  CHECK(net.sends[3] == int(b.session.frame() + 3));
  scene_info.scene_kind = GS_VS;
  hash_logs(net, a, b);
  current = nullptr;
}
// Exercise the actual pre-break hook and predictor with terminal bodies in a small scene model.
void terminal_cases(int kind) {
  ++cases;
  Network net; net.terminal = kind;
  Context a, b;
  MuHostApi api{}; api.version = MU_HOST_API_VERSION; api.console_rollback = host_call;
  set_test_console_host(&api);
  for (auto* c : {&a, &b}) {
    c->network = &net;
    const int port = c == &a ? 2 : 4;
    auto cfg = config(port, 2); cfg.ports = {2, 4};
    EngineHooks h; h.now_ms = [&] { return net.now; };
    h.log = [c](const std::string& line) { c->logs.push_back(line); };
    CHECK(c->session.begin(std::make_unique<Link>(net, port), cfg, h, true));
  }
  for (unsigned f = 0; f < net.origin; ++f) {
    net.elapse(17);
    for (auto* c : {&a, &b}) CHECK(c->session.advance(issued_hardware(*c), {}));
  }
  for (auto* c : {&a, &b}) { select(*c); mu_console_match_setup(); }
  State committed[2]; uint64_t clocks[2]{}; uint32_t vis[2]{};
  int exits[2]{-1, -1}, attempts[2]{}, corrections[2]{}, replay_exits = 0;
  uint32_t issued_frontier = 0;
  auto menu = [&](Context& c) {
    const auto sends = net.sends[c.session.config().my_port - 1];
    const bool sample = c.session.needs_sample();
    CHECK(c.session.advance(sample ? issued_hardware(c) : Pads{}, diagnostic(c.state)));
    if (!sample) CHECK(sends == net.sends[c.session.config().my_port - 1]);
    for (unsigned i = 0; i < 360 + c.session.frame() % 3; ++i) {
      c.session.ticks(0); c.session.poll();
      if (c.session.retrace_due()) { // loading service follows logical deadlines after handoff
        c.session.logical_retrace(); c.session.service_retrace();
      }
      const auto logical = c.session.clock_ticks();
      c.session.service_clock(true);
      for (unsigned event = 0; event < unsigned(c.session.config().my_port); ++event) c.session.ticks(0);
      c.session.service_clock(false);
      CHECK(c.session.clock_ticks() == logical);
    }
    c.session.retrace();
    Pads pads; c.session.read({}, pads); simulate(c.state, pads);
  };
  for (int f = 0; f < 15; ++f) {
    net.elapse(17);
    for (int peer = 0; peer < 2; ++peer) {
      auto& c = peer ? b : a; select(c);
      if (exits[peer] >= 0) { menu(c); continue; }
      for (int spins = 0;;) {
        CHECK(++spins < 12);
        console_frame_begin(); CHECK(c.session.active());
        const int body = c.state.engine.unk_0;
        // Time reads depend on the body's corrected logical key and input, never replay count.
        for (unsigned i = 0; i < unsigned(body % 4) + c.injected[3].stick_x % 3; ++i) {
          c.session.ticks(0); c.session.poll();
        }
        sound_body(); simulate(c.state, c.injected);
        bool terminal = false;
        if (kind == 1) terminal = (body >= 2 && c.injected[3].button == 1) || body == 8;
        if (kind == 2) { c.state.stocks[3] = c.injected[3].button ? 0 : 1; terminal = !c.state.stocks[3]; }
        if (kind == 3) { c.state.damage[3] += c.injected[3].button; terminal = c.state.damage[3] >= 3; }
        if (kind == 4) { c.state.paused = 1; terminal = c.injected[3].button == 8; }
        if (kind == 5) terminal = body == 4; // retail timeout is still subject to the barrier
        if (terminal) {
          ++attempts[peer];
          const bool replay = c.phase;
          c.state.engine.unk_C = 1;
          if (mu_online_scene_terminal()) { ++corrections[peer]; continue; }
          if (c.session.match_active() || c.phase || c.snapshots.active()) {
            std::cerr << "terminal failure scenario " << kind << " peer " << peer << " body " << body
                      << " active " << c.session.active() << " match " << c.session.match_active()
                      << " phase " << c.phase << " snapshots " << c.snapshots.active() << "\n";
            for (auto& line : c.logs) std::cerr << line << "\n";
          }
          CHECK(!c.session.match_active() && !c.phase && !c.snapshots.active());
          CHECK(!c.last_motor && !audio.motor_on);
          CHECK(!audio.in_body && audio.music_op == 0);
          exits[peer] = body; committed[peer] = c.state;
          clocks[peer] = c.session.clock_ticks(); vis[peer] = c.session.retrace_count();
          if (replay) ++replay_exits;
          if (!peer) issued_frontier = net.authoritative[1].rbegin()->first;
          c.state.engine = {}; // results/loading begin only after the terminal commit
          break;
        }
        if (console_frame_end() != ENGINE_RESIM) break;
      }
      // Arbitrary local presentation and alarm service cannot alter simulation time.
      if (c.session.match_active()) for (int i = 0; i < peer + 2; ++i) {
        c.session.retrace(); c.session.poll();
      }
    }
    if (exits[0] >= 0 && exits[1] >= 0) break;
  }
  CHECK(exits[0] >= 0 && exits[0] == exits[1]);
  CHECK(same_state(committed[0], committed[1]));
  CHECK(clocks[0] == clocks[1] && vis[0] == vis[1]);
  if (kind == 1) { CHECK(exits[0] == 8); CHECK(attempts[0] == 2 && corrections[0] == 1); }
  if (kind == 2 || kind == 3 || kind == 4) {
    CHECK(replay_exits > 0); CHECK(issued_frontier > net.origin + exits[0] + 2);
  }
  CHECK(a.session.rollback_stats().commits == 1 && b.session.rollback_stats().commits == 1);
  CHECK(net.digests[1].at(net.origin) == net.digests[3].at(net.origin));
  CHECK(net.digests[1].at(net.origin + exits[0] + 1) == net.digests[3].at(net.origin + exits[1] + 1));
  // The peer which corrected to an earlier terminal reuses, without resampling, its issued pads
  // while it traverses results and loading to the same continuous boundary as its peer.
  while (a.session.frame() < b.session.frame()) { select(a); menu(a); }
  while (b.session.frame() < a.session.frame()) { select(b); menu(b); }
  for (int f = 0; f < 3; ++f) { select(a); menu(a); select(b); menu(b); }
  CHECK(same_state(a.state, b.state));
  CHECK(a.session.clock_ticks() == b.session.clock_ticks());
  CHECK(a.session.retrace_count() == b.session.retrace_count());
  CHECK(a.session.rollback_stats().loads > b.session.rollback_stats().loads);
  hash_logs(net, a, b);
  std::cout << "terminal scenario " << kind << ": body " << exits[0] << ", attempts " << attempts[0]
            << ", replay exits " << replay_exits << ", wait " << net.waits << " ms; clocks equal through menus\n";
  current = nullptr;
}
void presentation_case() {
  ++cases;
  Context c; Network net; auto cfg = config(2, 2); cfg.ports = {2, 4};
  EngineHooks h; h.now_ms = [&] { return net.now; };
  CHECK(c.session.begin(std::make_unique<Link>(net, 2), cfg, h));
  select(c); c.online.console_active = 1; c.online.local_index = 1;
  mu_online_audio_match_start(1, 1);
  auto body = [&](int frame, int sound) {
    c.state.engine.unk_0 = frame; c.state.engine.unk_8 = 0; // paused bodies must not alias
    mu_online_audio_frame_begin();
    int adopted = -1;
    const int ticket = mu_online_sfx_begin(sound, 0, &adopted);
    if (ticket >= 0) mu_online_sfx_started(ticket, ++c.voices);
    ++c.state.engine.unk_0; mu_online_audio_frame_end();
  };
  body(0, 20); body(1, 21); body(2, 22);
  CHECK(c.voices == 3 && audio.started == 3);
  c.online.resim = 1; mu_online_audio_console_restore(0);
  body(0, 20); CHECK(c.voices == 3 && audio.adopted == 1);
  c.state.engine.unk_C = 1; // terminal sounds and music still reconcile
  body(1, 23); CHECK(c.voices == 4 && c.keyoffs == 1);
  CHECK(mu_online_music_defer(MU_MUSIC_START, "test-song", 100, 0));
  CHECK(mu_online_motor_port(1, 1) == -1);
  HSD_Rumble_804C22E0[1].last_status = 0;
  audio.motor_on = 1; c.last_motor = 1;
  mu_online_audio_console_commit(1); CHECK(c.keyoffs == 2); // abandoned future body 2
  c.online.resim = 0;
  mu_online_audio_rollback_end(); CHECK(c.last_motor == 0 && c.music == 1);
  mu_online_audio_rollback_end(); CHECK(c.music == 1); // flush exactly once
  mu_online_audio_match_exit(); CHECK(!audio.in_body && !audio.motor_on);
  CHECK(audio.gaps == 0 && audio.beyond_ring == 0);
  current = nullptr;
}
void slippi_off_audio() {
  ++cases;
  Context c; select(c);
  CHECK(!c.session.active() && !c.online.console_active);
  // Offline calls have no audio ring, music or motor bookkeeping side effects.
  mu_online_audio_match_start(1, 2);
  c.state.engine.unk_0 = 91; c.state.engine.unk_8 = 7;
  CHECK(engine_frame() == 7);
  auto untouched = audio;
  int adopted = -1;
  mu_online_audio_frame_begin();
  CHECK(mu_online_sfx_begin(20, 1, &adopted) == MU_SFX_NO_TICKET);
  CHECK(!mu_online_music_defer(MU_MUSIC_START, "song", 100, 1));
  CHECK(mu_online_motor_port(3, 1) == 3);
  mu_online_audio_frame_end(); mu_online_audio_rollback_end(); mu_online_audio_match_exit();
  CHECK(std::memcmp(&audio, &untouched, sizeof audio) == 0);
  // Slippi uses unk_8, including its paused/terminal gates, and physical input-source routing.
  c.slippi = true;
  mu_online_audio_frame_begin(); CHECK(sfx_hooks_on());
  const int ticket = mu_online_sfx_begin(20, 1, &adopted);
  CHECK(ticket == 7 * 256); mu_online_sfx_started(ticket, 100);
  c.state.engine.unk_8 = 8; mu_online_audio_frame_end();
  CHECK(audio.latest == 7 && audio.f[7].stable_count == 1 && !audio.in_body);
  CHECK(!mu_online_music_defer(MU_MUSIC_START, "song", 100, 1)); // forward Slippi music
  CHECK(mu_online_motor_port(0, 1) == -1);
  CHECK(mu_online_motor_port(1, 1) == 2 && audio.motor_on == 1);
  c.online.resim = 1; c.state.engine.unk_8 = 7;
  mu_online_audio_frame_begin();
  CHECK(mu_online_sfx_begin(20, 1, &adopted) == MU_SFX_ADOPTED && adopted == 100);
  CHECK(mu_online_music_defer(MU_MUSIC_START, "song", 100, 1));
  CHECK(mu_online_motor_port(1, 0) == -1);
  c.state.engine.unk_8 = 8; mu_online_audio_frame_end();
  CHECK(audio.latest == 7 && audio.adopted == 1 && !c.keyoffs);
  c.state.engine.unk_C = 1;
  untouched = audio;
  mu_online_audio_frame_begin(); mu_online_audio_frame_end();
  CHECK(!sfx_hooks_on() && !mu_online_music_defer(MU_MUSIC_STOP, nullptr, 0, 0));
  CHECK(std::memcmp(&audio, &untouched, sizeof audio) == 0);
  c.state.engine.unk_C = 0; c.online.resim = 0;
  HSD_Rumble_804C22E0[1].last_status = 0;
  mu_online_audio_rollback_end(); CHECK(c.music == 1 && c.motors == 1 && !c.last_motor);
  mu_online_audio_match_exit(); CHECK(!audio.console_finished);
  current = nullptr;
}
void all() {
  ++cases;
  Context c; select(c);
  c.online.console_active = 1;
  u8 neutral[12]{}, changed[12]{};
  for (int i = 0; i < 11; ++i) {
    std::memset(changed, 0, sizeof changed); changed[i] = 1;
    CHECK(pads_differ(neutral, changed));
  }
  changed[11] = 1; changed[10] = 0; CHECK(!pads_differ(neutral, changed));
  c.online.console_active = 0; changed[10] = 1; CHECK(!pads_differ(neutral, changed));
  changed[6] = 1; CHECK(!pads_differ(neutral, changed)); // Slippi trigger dead zone unchanged
  c.online.console_active = 1; c.online.stable_finalized = -1; c.online.savestate_predicting = 1;
  for (int r = 0; r < 2; ++r) {
    c.online.player_predicting[r] = 1; c.online.player_savestate_frame[r] = r ? 1 : 3;
    c.online.predicted_write[r] = 1;
    // Confirmed through 6. Both remotes contradict; the lowest body key must win.
    c.online.rxb[RXB_OPNT_FRAME_NUMS + 4 * r + 3] = 6;
    c.online.rxb[RXB_OPNT_INPUTS + (r * 7 + (r ? 5 : 3)) * 12 + 10] = 1;
  }
  CHECK(check_predictions(7)); CHECK(c.online.savestate_frame == 1);
  CHECK(c.online.mispredictions == 2 && c.online.rollback_end_frame == 7);
  current = nullptr;
  for (int depth = 1; depth <= 7; ++depth) run(depth);
  run(3, true); run(7, false, true); run(3, false, false, true);
  for (int kind = 1; kind <= 5; ++kind) terminal_cases(kind);
  presentation_case();
  slippi_off_audio();
  boundary_race(false); boundary_race(true);
  std::cout << "native console predictor/replay: depths 1..7, asymmetric jitter/loss/reordering, "
               "horizon stall, sparse ports, paused bodies, repeated epochs, missing checkpoint\n";
}
#undef mu_online
} // namespace rollback_test
