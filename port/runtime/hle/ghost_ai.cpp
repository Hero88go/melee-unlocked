// SPDX-License-Identifier: GPL-2.0-or-later
// The inputs are built exactly as ghost-ai/train.py builds them from a replay (features.py), and the
// layers are PyTorch's: Linear, ReLU, GRU (gates r, z, n), Linear heads. Keep the two in step.
#include "ghost_ai.h"

#include "host.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace ghost_ai {
namespace {

constexpr int kNum = 16, kFlags = 5, kPad = 10, kPrev = 2, kStickSteps = 9, kStick = 81, kCStick = 5, kBtn = 5;
constexpr float kNumScale[kNum] = {1 / 100.f, 1 / 100.f, 1, 1 / 100.f, 1 / 60.f, 1 / 4.f, 1 / 60.f, 1 / 60.f, 1, 1 / 6.f,
                                   1 / 3.f, 1 / 3.f, 1 / 3.f, 1 / 3.f, 1 / 3.f, 1 / 10.f};
constexpr float kTemperature = 1.0f;
constexpr uint32_t kBtnOut[kBtn] = {0x0100, 0x0200, 0x0400, 0x0010, 0x0020};   // A B X Z R

struct Model {
  uint32_t cont = 0, n_states = 0, e_state = 0, n_chars = 0, e_char = 0, n_stages = 0, e_stage = 0, n_pros = 0, e_pro = 0,
           h_in = 0, h_rnn = 0, h_out = 0;
  std::vector<float> state, chr, stage, pro, inp_w, inp_b, w_ih, w_hh, b_ih, b_hh, mid_w, mid_b, st_w, st_b, cs_w, cs_b,
      bt_w, bt_b, stick_mean;
  uint32_t n_in() const { return cont + 2 * kFlags * 8 + 2 * e_state + 2 * e_char + e_stage + e_pro; }
};

struct Slot {
  std::vector<float> h;
  float prev[kPrev][kPad] = {};
  uint32_t last_frame = 0;
  bool live = false;
  MuGhostAct last_act{};
  std::mt19937 rng{0x5EED};
};

std::mutex g_mutex;
Model g_model;
int g_loaded = 0;   // 0 not tried, 1 loaded, -1 missing or bad
bool g_enabled = false;
Slot g_slots[4];
struct Ko { bool dead = false; float percent = 0, x = 0, y = 0; unsigned motion = 0; } g_ko[4];

std::string model_path() {
  if (const char* v = std::getenv("MELEE_GHOST_AI_FILE")) return v;
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe);
  dir.resize(dir.find_last_of("\\/") + 1);
  return dir + "ghost_ai.bin";
}

bool load() {
  FILE* f = std::fopen(model_path().c_str(), "rb");
  if (!f) { host::log("ghost ai: %s not found", model_path().c_str()); return false; }
  char magic[4] = {};
  uint32_t version = 0, dims[12] = {};
  bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "MUGA", 4) == 0 && std::fread(&version, 4, 1, f) == 1 &&
            version == MU_GHOST_VERSION && std::fread(dims, 4, 12, f) == 12;
  Model& m = g_model;
  if (ok) {
    m.cont = dims[0]; m.n_states = dims[1]; m.e_state = dims[2]; m.n_chars = dims[3]; m.e_char = dims[4]; m.n_stages = dims[5];
    m.e_stage = dims[6]; m.n_pros = dims[7]; m.e_pro = dims[8]; m.h_in = dims[9]; m.h_rnn = dims[10]; m.h_out = dims[11];
    ok = m.cont == 2 * kNum + 3 + kPrev * kPad && m.h_in && m.h_in <= 4096 && m.h_rnn && m.h_rnn <= 4096 && m.h_out <= 4096;
  }
  const auto read = [&](std::vector<float>& v, size_t n) {
    v.resize(n);
    ok = ok && std::fread(v.data(), 4, n, f) == n;
  };
  if (ok) {
    read(m.state, (size_t)m.n_states * m.e_state);
    read(m.chr, (size_t)m.n_chars * m.e_char);
    read(m.stage, (size_t)m.n_stages * m.e_stage);
    read(m.pro, (size_t)m.n_pros * m.e_pro);
    read(m.inp_w, (size_t)m.h_in * m.n_in());
    read(m.inp_b, m.h_in);
    read(m.w_ih, (size_t)3 * m.h_rnn * m.h_in);
    read(m.w_hh, (size_t)3 * m.h_rnn * m.h_rnn);
    read(m.b_ih, 3 * m.h_rnn);
    read(m.b_hh, 3 * m.h_rnn);
    read(m.mid_w, (size_t)m.h_out * (m.h_rnn + m.h_in));
    read(m.mid_b, m.h_out);
    read(m.st_w, (size_t)kStick * m.h_out);
    read(m.st_b, kStick);
    read(m.cs_w, (size_t)kCStick * m.h_out);
    read(m.cs_b, kCStick);
    read(m.bt_w, (size_t)kBtn * m.h_out);
    read(m.bt_b, kBtn);
    read(m.stick_mean, (size_t)kStick * 2);
  }
  std::fclose(f);
  host::log("ghost ai: %s %s (%u pros)", model_path().c_str(), ok ? "loaded" : "unreadable", m.n_pros);
  return ok;
}

bool ready() {
  if (!g_enabled) return false;
  if (g_loaded == 0) g_loaded = load() ? 1 : -1;
  return g_loaded == 1;
}

// y = W x + b, W row-major (rows x cols).
void affine(const float* w, const float* b, const float* x, int rows, int cols, float* y) {
  for (int r = 0; r < rows; ++r) {
    const float* row = w + (size_t)r * cols;
    // Eight running sums: the compiler can keep them in one vector register.
    float acc[8] = {};
    int c = 0;
    for (; c + 8 <= cols; c += 8)
      for (int k = 0; k < 8; ++k) acc[k] += row[c + k] * x[c + k];
    float s = b ? b[r] : 0.f;
    for (; c < cols; ++c) s += row[c] * x[c];
    y[r] = s + ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7]));
  }
}

float sigmoid(float v) { return 1.f / (1.f + std::exp(-v)); }

void player_block(const MuGhostFighter& p, float* out) {
  for (int i = 0; i < kNum; ++i) {
    const float v = std::isfinite(p.num[i]) ? p.num[i] * kNumScale[i] : 0.f;
    out[i] = std::clamp(v, -6.f, 6.f);
  }
}

int sample(std::mt19937& rng, const float* logits, int n, float temperature) {
  float mx = logits[0];
  for (int i = 1; i < n; ++i) mx = std::max(mx, logits[i]);
  float p[96], sum = 0;
  for (int i = 0; i < n; ++i) sum += p[i] = std::exp((logits[i] - mx) / temperature);
  float u = std::uniform_real_distribution<float>(0.f, sum)(rng);
  for (int i = 0; i < n; ++i) { if ((u -= p[i]) <= 0) return i; }
  return n - 1;
}

}  // namespace

void enable() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_enabled = true;
}

bool available() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return ready();
}

bool act(const MuGhostObs& o, MuGhostAct& out) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (!ready() || o.version != MU_GHOST_VERSION || o.slot >= 4 || o.pro >= g_model.n_pros) return false;
  const Model& m = g_model;
  Slot& s = g_slots[o.slot];
  // Asked twice in one frame (a fighter's input can be read more than once): the same answer, and the
  // network does not step again. Stepping again looked like a gap and wiped its memory every frame.
  if (s.live && !o.reset && o.frame == s.last_frame) {
    static int repeats = 0;
    if (++repeats <= 3) host::log("ghost ai: P%d asked again in frame %u", o.slot + 1, o.frame);
    out = s.last_act;
    return true;
  }
  // A new match, a restart, a savestate or a gap: the memory of the match starts over.
  if (o.reset || !s.live || o.frame != s.last_frame + 1) {
    s.h.assign(m.h_rnn, 0.f);
    std::memset(s.prev, 0, sizeof s.prev);
    s.rng.seed(0x5EED ^ (o.frame * 2654435761u) ^ o.slot);
  }
  s.live = true;
  s.last_frame = o.frame;
  // Each KO (the dead motions are the first eleven), with the percent it came at: a KO near 0% is
  // usually a self-destruct. A dead fighter may not be asked for input, so both sides report, once
  // per port (1v1: the opponent is the other of P1 and P2). Tests read these lines to score matches.
  const auto note = [&](int port, const MuGhostFighter& f, const MuGhostFighter& other) {
    const bool dead = f.motion <= 10;
    if (dead && !g_ko[port].dead)
      host::log("ghost ai: P%d KO at %.0f%% (frame %u, opponent at %.0f%%) last at %.0f,%.0f motion %u", port + 1,
                g_ko[port].percent, o.frame, other.num[3], g_ko[port].x, g_ko[port].y, g_ko[port].motion);
    g_ko[port].dead = dead;
    if (!dead) { g_ko[port].percent = f.num[3]; g_ko[port].x = f.num[0]; g_ko[port].y = f.num[1]; g_ko[port].motion = f.motion; }
  };
  note(o.slot, o.self, o.opp);
  note(o.slot == 0 ? 1 : 0, o.opp, o.self);

  // Inputs, in train.py's order: cont (self, opp, rel, pad t-1, pad t-2), flag bits, embeddings.
  static std::vector<float> x;   // reused under the lock: no allocation each frame
  x.assign(m.n_in(), 0.f);
  float* p = x.data();
  player_block(o.self, p); p += kNum;
  player_block(o.opp, p); p += kNum;
  const float dx = o.opp.num[0] - o.self.num[0], dy = o.opp.num[1] - o.self.num[1];
  *p++ = dx / 100.f; *p++ = dy / 100.f; *p++ = std::fabs(dx) / 100.f;
  for (int k = 0; k < kPrev; ++k) for (int j = 0; j < kPad; ++j) *p++ = s.prev[k][j];
  for (const MuGhostFighter* f : {&o.self, &o.opp})
    for (int b = 0; b < kFlags; ++b) for (int bit = 0; bit < 8; ++bit) *p++ = (float)((f->flags[b] >> bit) & 1);
  const auto embed = [&](const std::vector<float>& table, uint32_t count, uint32_t width, int index) {
    index = std::clamp(index, 0, (int)count - 1);
    std::memcpy(p, table.data() + (size_t)index * width, width * sizeof(float));
    p += width;
  };
  embed(m.state, m.n_states, m.e_state, o.self.motion);
  embed(m.state, m.n_states, m.e_state, o.opp.motion);
  embed(m.chr, m.n_chars, m.e_char, o.self.kind);
  embed(m.chr, m.n_chars, m.e_char, o.opp.kind);
  static const int16_t kStages[][2] = {{2, 1}, {3, 2}, {8, 3}, {28, 4}, {31, 5}, {32, 6}};
  int stage = 0;
  for (const auto& st : kStages) if (st[0] == o.stage) stage = st[1];
  embed(m.stage, m.n_stages, m.e_stage, stage);
  embed(m.pro, m.n_pros, m.e_pro, o.pro);

  const double t0 = host::now_seconds();
  const int H = (int)m.h_rnn, I = (int)m.h_in;
  static std::vector<float> a, gi, gh, mid_in, y;
  a.resize(I); gi.resize(3 * H); gh.resize(3 * H); mid_in.resize(H + I); y.resize(m.h_out);
  affine(m.inp_w.data(), m.inp_b.data(), x.data(), I, (int)m.n_in(), a.data());
  for (float& v : a) v = std::max(v, 0.f);
  affine(m.w_ih.data(), m.b_ih.data(), a.data(), 3 * H, I, gi.data());
  affine(m.w_hh.data(), m.b_hh.data(), s.h.data(), 3 * H, H, gh.data());
  for (int i = 0; i < H; ++i) {
    const float r = sigmoid(gi[i] + gh[i]);
    const float z = sigmoid(gi[H + i] + gh[H + i]);
    const float n = std::tanh(gi[2 * H + i] + r * gh[2 * H + i]);
    s.h[i] = (1.f - z) * n + z * s.h[i];
  }
  std::memcpy(mid_in.data(), s.h.data(), H * sizeof(float));
  std::memcpy(mid_in.data() + H, a.data(), I * sizeof(float));
  affine(m.mid_w.data(), m.mid_b.data(), mid_in.data(), (int)m.h_out, H + I, y.data());
  for (float& v : y) v = std::max(v, 0.f);
  float ls[kStick], lc[kCStick], lb[kBtn];
  affine(m.st_w.data(), m.st_b.data(), y.data(), kStick, (int)m.h_out, ls);
  affine(m.cs_w.data(), m.cs_b.data(), y.data(), kCStick, (int)m.h_out, lc);
  affine(m.bt_w.data(), m.bt_b.data(), y.data(), kBtn, (int)m.h_out, lb);

  // How sure it plays: below 1 it keeps to what the pro most likely held (fewer stray inputs).
  // MELEE_GHOST_TEMP (tests) sets it to compare.
  static const float temp = [] { const char* v = std::getenv("MELEE_GHOST_TEMP"); return v ? (float)std::atof(v) : kTemperature; }();
  const int stick = sample(s.rng, ls, kStick, temp);
  const int cstick = sample(s.rng, lc, kCStick, temp);
  std::memset(&out, 0, sizeof out);
  out.lx = m.stick_mean[stick * 2];
  out.ly = m.stick_mean[stick * 2 + 1];
  static const float kC[kCStick][2] = {{0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  out.cx = kC[cstick][0];
  out.cy = kC[cstick][1];
  float pressed[kBtn];
  for (int b = 0; b < kBtn; ++b) {
    pressed[b] = std::uniform_real_distribution<float>(0.f, 1.f)(s.rng) < sigmoid(lb[b] / temp) ? 1.f : 0.f;
    if (pressed[b] != 0.f) out.buttons |= kBtnOut[b];
  }
  if (pressed[4] != 0.f) out.r = 1.f;

  // Its cost, once a minute of play (the frame budget is 16.7 ms; this runs on the game's thread).
  static double spent = 0;
  static int calls = 0;
  spent += host::now_seconds() - t0;
  if (++calls == 3600) {
    host::log("ghost ai: %.3f ms a frame", spent / calls * 1000.0);
    spent = 0;
    calls = 0;
  }
  s.last_act = out;
  // What it held, as a replay records it (the game's dead zone applied), for the next frame.
  std::memmove(s.prev[1], s.prev[0], sizeof s.prev[0]);
  const auto deadzone = [](float v) { return std::fabs(v) < 0.2875f ? 0.f : v; };
  float* q = s.prev[0];
  q[0] = deadzone(out.lx); q[1] = deadzone(out.ly); q[2] = out.cx; q[3] = out.cy; q[4] = out.r;
  for (int b = 0; b < kBtn; ++b) q[5 + b] = pressed[b];
  return true;
}

}  // namespace ghost_ai
