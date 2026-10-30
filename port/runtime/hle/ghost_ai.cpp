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
  bool prev_valid = false;
  bool fresh = true;   // the memory was just cleared (self-play records start a sequence here)
  unsigned pro = MU_GHOST_NONE;
  std::mt19937 rng{0x5EED};
};

std::mutex g_mutex;
Model g_model;
Model g_model_p1;    // tests: MELEE_GHOST_AI_FILE_P1 gives port 1 another network (old against new)
bool g_has_p1 = false;
constexpr unsigned kSpecialists = 5;
const char* const kSpecialistNames[kSpecialists] = {"mang0", "zain", "hbox", "cody", "amsa"};
Model g_specialists[kSpecialists];
int g_specialist_status[kSpecialists] = {}; // tried only when selected: 0 unknown, 1 loaded, -1 absent/bad
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

bool load_file(const std::string& path, Model& m) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { host::log("ghost ai: %s not found", path.c_str()); return false; }
  char magic[4] = {};
  uint32_t version = 0, dims[12] = {};
  bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "MUGA", 4) == 0 && std::fread(&version, 4, 1, f) == 1 &&
            (version == 1 || version == 2) && std::fread(dims, 4, 12, f) == 12;
  if (ok) {
    m.cont = dims[0]; m.n_states = dims[1]; m.e_state = dims[2]; m.n_chars = dims[3]; m.e_char = dims[4]; m.n_stages = dims[5];
    m.e_stage = dims[6]; m.n_pros = dims[7]; m.e_pro = dims[8]; m.h_in = dims[9]; m.h_rnn = dims[10]; m.h_out = dims[11];
    ok = m.cont == 2 * kNum + 3 + kPrev * kPad &&
         m.n_states && m.n_states <= 4096 && m.e_state && m.e_state <= 256 &&
         m.n_chars && m.n_chars <= 256 && m.e_char && m.e_char <= 256 &&
         m.n_stages && m.n_stages <= 256 && m.e_stage && m.e_stage <= 256 &&
         m.n_pros && m.n_pros <= 256 && m.e_pro && m.e_pro <= 256 &&
         m.h_in && m.h_in <= 4096 && m.h_rnn && m.h_rnn <= 4096 && m.h_out && m.h_out <= 4096;
  }
  struct Array {
    std::vector<float>* values;
    uint32_t rows, cols;
    bool matrix;
  };
  // Format 1 stores every array as float32. Format 2 stores each matrix row as one float32
  // scale followed by signed int8 weights. Biases and stick means stay float32. Both formats
  // dequantize into the same inference representation, so this reduces disk size, not RAM.
  const Array arrays[] = {
      {&m.state, m.n_states, m.e_state, true}, {&m.chr, m.n_chars, m.e_char, true},
      {&m.stage, m.n_stages, m.e_stage, true}, {&m.pro, m.n_pros, m.e_pro, true},
      {&m.inp_w, m.h_in, m.n_in(), true}, {&m.inp_b, 1, m.h_in, false},
      {&m.w_ih, 3 * m.h_rnn, m.h_in, true}, {&m.w_hh, 3 * m.h_rnn, m.h_rnn, true},
      {&m.b_ih, 1, 3 * m.h_rnn, false}, {&m.b_hh, 1, 3 * m.h_rnn, false},
      {&m.mid_w, m.h_out, m.h_rnn + m.h_in, true}, {&m.mid_b, 1, m.h_out, false},
      {&m.st_w, kStick, m.h_out, true}, {&m.st_b, 1, kStick, false},
      {&m.cs_w, kCStick, m.h_out, true}, {&m.cs_b, 1, kCStick, false},
      {&m.bt_w, kBtn, m.h_out, true}, {&m.bt_b, 1, kBtn, false},
      {&m.stick_mean, 1, kStick * 2, false},
  };
  if (ok) {
    // Validate the complete layout before allocating. A wrong pro count shifts every later layer;
    // accepting trailing bytes used to make such a model appear to load successfully.
    uint64_t count = 0, bytes = 0;
    for (const Array& a : arrays) {
      const uint64_t n = uint64_t(a.rows) * a.cols;
      count += n;
      bytes += version == 2 && a.matrix ? n + uint64_t(a.rows) * sizeof(float) : n * sizeof(float);
    }
    const long start = std::ftell(f);
    ok = count <= (256u * 1024u * 1024u / sizeof(float)) && std::fseek(f, 0, SEEK_END) == 0;
    if (ok) ok = std::ftell(f) == start + bytes && std::fseek(f, start, SEEK_SET) == 0;
  }
  if (ok) {
    for (const Array& a : arrays) {
      if (!ok) break;
      auto& v = *a.values;
      const size_t n = (size_t)a.rows * a.cols;
      v.resize(n);
      if (version == 2 && a.matrix) {
        std::vector<int8_t> q(a.cols);
        for (uint32_t row = 0; row < a.rows && ok; ++row) {
          float scale = 0.f;
          ok = std::fread(&scale, sizeof scale, 1, f) == 1 && std::isfinite(scale) && scale > 0.f &&
               std::fread(q.data(), sizeof(int8_t), a.cols, f) == a.cols;
          if (ok) for (uint32_t col = 0; col < a.cols; ++col) v[(size_t)row * a.cols + col] = q[col] * scale;
        }
      } else {
        ok = std::fread(v.data(), sizeof(float), n, f) == n;
      }
      ok = ok && std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
    }
    if (ok) ok = std::all_of(m.stick_mean.begin(), m.stick_mean.end(), [](float x) { return std::fabs(x) <= 1.f; });
  }
  std::fclose(f);
  host::log("ghost ai: %s %s (%u pros)", path.c_str(), ok ? "loaded" : "unreadable", m.n_pros);
  return ok;
}

bool load() {
  if (!load_file(model_path(), g_model)) return false;
  if (const char* p1 = std::getenv("MELEE_GHOST_AI_FILE_P1")) g_has_p1 = load_file(p1, g_model_p1);
  return true;
}

bool ready() {
  if (!g_enabled) return false;
  if (g_loaded == 0) g_loaded = load() ? 1 : -1;
  return g_loaded == 1;
}

const Model* selected_model(unsigned pro, unsigned slot, uint32_t& row) {
  if (!g_enabled || pro >= kSpecialists) return nullptr;
  const bool base_ready = ready();
  // Explicit evaluation files own the entire selection. Auto-discovered specialists must not
  // replace either side of an old/new comparison requested by the test harness.
  if (slot == 0 && g_has_p1) {
    row = g_model_p1.n_pros == 1 ? 0 : pro;
    return row < g_model_p1.n_pros ? &g_model_p1 : nullptr;
  }
  if (!std::getenv("MELEE_GHOST_AI_FILE") && pro < kSpecialists) {
    if (g_specialist_status[pro] == 0) {
      std::string path = model_path();
      path.resize(path.find_last_of("\\/") + 1);
      path += std::string("ghost_ai_") + kSpecialistNames[pro] + ".bin";
      const bool loaded = load_file(path, g_specialists[pro]);
      g_specialist_status[pro] = loaded && g_specialists[pro].n_pros == 1 ? 1 : -1;
      if (loaded && g_specialist_status[pro] != 1)
        host::log("ghost ai: specialist %s needs one player embedding", kSpecialistNames[pro]);
    }
    if (g_specialist_status[pro] == 1) {
      row = 0;
      return &g_specialists[pro];
    }
  }
  row = std::getenv("MELEE_GHOST_AI_FILE") && g_model.n_pros == 1 ? 0 : pro;
  return base_ready && row < g_model.n_pros ? &g_model : nullptr;
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
  if (ready()) return true;
  uint32_t row;
  for (unsigned pro = 0; pro < kSpecialists; ++pro)
    if (selected_model(pro, 1, row)) return true;
  return false;
}

bool available(unsigned pro) {
  std::lock_guard<std::mutex> lk(g_mutex);
  uint32_t row;
  return selected_model(pro, 1, row) != nullptr;
}

bool specialist_available(unsigned pro) {
  std::lock_guard<std::mutex> lk(g_mutex);
  uint32_t row;
  if (!selected_model(pro, 1, row)) return false;
  // An explicit one-policy file is used by isolated runtime tests. A release menu must expose a
  // named player only when that player's own independent file exists.
  return std::getenv("MELEE_GHOST_AI_FILE") != nullptr ||
         (pro < kSpecialists && g_specialist_status[pro] == 1);
}

bool act(const MuGhostObs& o, MuGhostAct& out) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (o.version != MU_GHOST_VERSION || o.slot >= 4) return false;
  uint32_t model_pro = 0;
  const Model* selected = selected_model(o.pro, o.slot, model_pro);
  if (!selected) return false;
  const Model& m = *selected;
  for (const MuGhostFighter* f : {&o.self, &o.opp})
    for (float v : f->num) if (!std::isfinite(v)) return false;
  Slot& s = g_slots[o.slot];
  // Asked twice in one frame (a fighter's input can be read more than once): the same answer, and the
  // network does not step again. Stepping again looked like a gap and wiped its memory every frame.
  if (s.live && s.pro == o.pro && !o.reset && o.frame == s.last_frame) {
    static int repeats = 0;
    if (++repeats <= 3) host::log("ghost ai: P%d asked again in frame %u", o.slot + 1, o.frame);
    out = s.last_act;
    return true;
  }
  // A new match, a restart, a savestate or a gap: the memory of the match starts over.
  if (o.reset || !s.live || s.pro != o.pro || o.frame != s.last_frame + 1) {
    host::log("ghost ai: P%d driving pro %u as fighter %u (memory reset at frame %u)",
              o.slot + 1, o.pro, o.self.kind, o.frame);
    s.h.assign(m.h_rnn, 0.f);
    s.fresh = true;
    std::memset(s.prev, 0, sizeof s.prev);
    s.prev_valid = false;
    s.rng.seed(0x5EED ^ (o.frame * 2654435761u) ^ o.slot);
  }
  s.live = true;
  s.pro = o.pro;
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

  // Last frame as the game really applied it (the safety net or a dropped air dodge may differ from
  // what the network asked for): its memory is of what it held, as in the pros' replays.
  if (!o.reset && s.prev_valid) {
    const auto deadzone = [](float v) { return std::fabs(v) < 0.2875f ? 0.f : v; };
    float* q = s.prev[0];
    const MuGhostAct& ap = o.applied;
    const float trig = std::max(ap.l, ap.r);
    q[0] = deadzone(ap.lx); q[1] = deadzone(ap.ly); q[2] = ap.cx; q[3] = ap.cy; q[4] = trig;
    q[5] = (ap.buttons & 0x0100) ? 1.f : 0.f;
    q[6] = (ap.buttons & 0x0200) ? 1.f : 0.f;
    q[7] = (ap.buttons & 0x0C00) ? 1.f : 0.f;
    q[8] = (ap.buttons & 0x0010) ? 1.f : 0.f;
    q[9] = ((ap.buttons & 0x0060) || trig > 0.3f) ? 1.f : 0.f;
  }
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
  embed(m.pro, m.n_pros, m.e_pro, model_pro);

  const double t0 = host::now_seconds();
  const int H = (int)m.h_rnn, I = (int)m.h_in;
  static std::vector<float> a, gi, gh, mid_in, y;
  a.resize(I); gi.resize(3 * H); gh.resize(3 * H); mid_in.resize(H + I); y.resize(m.h_out);
  affine(m.inp_w.data(), m.inp_b.data(), x.data(), I, (int)m.n_in(), a.data());
  for (float& v : a) v = std::max(v, 0.f);
  affine(m.w_ih.data(), m.b_ih.data(), a.data(), 3 * H, I, gi.data());
  affine(m.w_hh.data(), m.b_hh.data(), s.h.data(), 3 * H, H, gh.data());
  static std::vector<float> h_before;
  h_before = s.h;
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
  static const float temp_all = [] { const char* v = std::getenv("MELEE_GHOST_TEMP"); return v ? (float)std::atof(v) : kTemperature; }();
  static const float temp_p1 = [] { const char* v = std::getenv("MELEE_GHOST_TEMP_P1"); return v ? (float)std::atof(v) : -1.f; }();
  const float requested_temp = (o.slot == 0 && temp_p1 > 0) ? temp_p1 : temp_all;
  const float temp = std::isfinite(requested_temp) && requested_temp > 0 ? requested_temp : kTemperature;
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

  // Self-play (MELEE_GHOST_ROLLOUT=<folder>, tests and training only): each frame what it saw, what it
  // chose and how likely that was, for ghost-ai/selfplay.py. The memory is written every 64 frames.
  static const char* rollout = std::getenv("MELEE_GHOST_ROLLOUT");
  if (rollout && *rollout) {
    struct Rec {
      float cont[55];
      uint8_t flags[10];
      int16_t cat[6];
      uint8_t stick, cstick, btn, reset;
      float logp, self_pct, opp_pct;
      uint8_t self_dead, opp_dead, has_h, pad;
      uint32_t frame;
    };
    static FILE* rec_f[4] = {};
    static FILE* h_f[4] = {};
    static uint32_t rec_n[4] = {};
    if (!rec_f[o.slot]) {
      char path[1024];
      std::snprintf(path, sizeof path, "%s/slot%d.rec", rollout, o.slot);
      rec_f[o.slot] = std::fopen(path, "wb");
      std::snprintf(path, sizeof path, "%s/slot%d.h", rollout, o.slot);
      h_f[o.slot] = std::fopen(path, "wb");
    }
    const auto logsoftmax = [&](const float* l, int n, int k) {
      float mx = l[0];
      for (int i = 1; i < n; ++i) mx = std::max(mx, l[i]);
      double sum = 0;
      for (int i = 0; i < n; ++i) sum += std::exp((l[i] - mx) / temp);
      return (float)((l[k] - mx) / temp - std::log(sum));
    };
    Rec r{};
    std::memcpy(r.cont, x.data(), sizeof r.cont);
    std::memcpy(r.flags, o.self.flags, 5);
    std::memcpy(r.flags + 5, o.opp.flags, 5);
    r.cat[0] = (int16_t)std::clamp((int)o.self.motion, 0, (int)m.n_states - 1);
    r.cat[1] = (int16_t)std::clamp((int)o.opp.motion, 0, (int)m.n_states - 1);
    r.cat[2] = o.self.kind; r.cat[3] = o.opp.kind; r.cat[4] = (int16_t)stage; r.cat[5] = (int16_t)model_pro;
    r.stick = (uint8_t)stick; r.cstick = (uint8_t)cstick;
    float lp = logsoftmax(ls, kStick, stick) + logsoftmax(lc, kCStick, cstick);
    for (int b = 0; b < kBtn; ++b) {
      if (pressed[b] != 0.f) r.btn |= (uint8_t)(1u << b);
      const float pr = sigmoid(lb[b] / temp);
      lp += std::log(std::max(1e-6f, pressed[b] != 0.f ? pr : 1.f - pr));
    }
    r.logp = lp;
    r.reset = (uint8_t)(rec_n[o.slot] == 0 || s.fresh);
    r.self_pct = o.self.num[3]; r.opp_pct = o.opp.num[3];
    r.self_dead = o.self.motion <= 10; r.opp_dead = o.opp.motion <= 10;
    r.frame = o.frame;
    r.has_h = (uint8_t)(rec_n[o.slot] % 64 == 0 || r.reset);
    if (r.has_h && h_f[o.slot]) {
      std::fwrite(&rec_n[o.slot], 4, 1, h_f[o.slot]);
      std::fwrite(h_before.data(), sizeof(float), h_before.size(), h_f[o.slot]);
    }
    std::fwrite(&r, sizeof r, 1, rec_f[o.slot]);
    if (++rec_n[o.slot] % 600 == 0) { std::fflush(rec_f[o.slot]); if (h_f[o.slot]) std::fflush(h_f[o.slot]); }
  }

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
  s.fresh = false;
  s.prev_valid = true;
  // What it held, as a replay records it (the game's dead zone applied), for the next frame.
  std::memmove(s.prev[1], s.prev[0], sizeof s.prev[0]);
  const auto deadzone = [](float v) { return std::fabs(v) < 0.2875f ? 0.f : v; };
  float* q = s.prev[0];
  q[0] = deadzone(out.lx); q[1] = deadzone(out.ly); q[2] = out.cx; q[3] = out.cy; q[4] = out.r;
  for (int b = 0; b < kBtn; ++b) q[5 + b] = pressed[b];
  return true;
}

}  // namespace ghost_ai
