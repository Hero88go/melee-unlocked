// Tap jump off and tilt stick: settings, remembered answers and the online agreement (controls.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "controls.h"
#include "controls_policy.h"
#include "controls_wire.h"
#include "gameplay_policy.h"
#include "host.h"
#include "lcancel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "slippi_online.h"
#endif

namespace controls {
namespace {

std::mutex g_mutex;   // everything below except the atomics
Settings g_settings;
std::atomic<bool> g_engine{false};

// ---- remembered answers
std::string g_memory_path;
std::map<std::string, policy::Remembered> g_memory;
bool g_memory_loaded = false;
constexpr size_t kMemoryMax = 500;

void load_memory_locked() {
  if (g_memory_loaded) return;
  g_memory_loaded = true;
  if (g_memory_path.empty()) return;
  std::ifstream in(std::filesystem::u8path(g_memory_path));
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    std::string key;
    unsigned allowed = 0, declined = 0;
    if (!std::getline(row, key, '\t') || key.empty() || !(row >> allowed >> declined)) continue;
    g_memory[key] = {policy::main_bits(allowed), policy::main_bits(declined) & ~policy::main_bits(allowed)};
  }
}

void save_memory_locked() {
  if (g_memory_path.empty()) return;
  const std::filesystem::path path = std::filesystem::u8path(g_memory_path);
  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) { host::log("controls: could not write the remembered answers"); return; }
    for (const auto& [key, r] : g_memory) out << key << '\t' << r.allowed << ' ' << r.declined << '\n';
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) { std::filesystem::remove(path, ec); std::filesystem::rename(temp, path, ec); }
}

policy::Remembered remembered_locked(const std::string& key) {
  load_memory_locked();
  if (key.empty()) return {};
  auto it = g_memory.find(key);
  return it == g_memory.end() ? policy::Remembered{} : it->second;
}

// ---- the online exchange
struct Peer {
  Player who;
  Answer mine = Answer::None;
  policy::Remembered session;   // answered in this connection (an opponent with no key is still not asked twice)
  uint8_t answered_seq = 0;
  bool answer_sent = false;
  bool answer_value = false;
  uint64_t since = 0;         // locked in: when the question (or the own-options yes) began to wait
  bool own_pending = false;   // an own-options yes held for "Press Y for defaults"
};
struct Session {
  bool active = false;
  int mode = -1;
  bool yougame = false;
  int local = -1;
  std::vector<Peer> peers;
  wire::State state;
  uint8_t seq = 0;
  unsigned offered = 0;
  bool offer_sent = false;
  bool ready = false;
  bool in_match = false;
  std::vector<std::vector<uint8_t>> outgoing;
};
Session g;

uint64_t steady_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint64_t (*g_clock)() = steady_ms;

std::atomic<uint32_t> g_frozen{0};
std::atomic<int> g_frozen_mode{-1};
std::atomic<int> g_frozen_local{-1};
std::atomic<bool> g_frozen_yougame{false};

// This PC's game bits from the settings, with g_mutex held.
unsigned own_bits_locked() {
  const Settings& s = g_settings;
  unsigned bits = 0;
  if (s.tapjump_off) bits |= MU_CONTROL_TAPJUMP_OFF;
  if (s.tilt_stick) bits |= MU_CONTROL_TILT_STICK;
  if (s.tilt_dash_attack) bits |= MU_CONTROL_TILT_DASH_ATTACK;
  if (s.tilt_item_tilt_throw) bits |= MU_CONTROL_TILT_ITEM_TILT_THROW;
  return mu_controls_normalize(bits);
}

unsigned own_offer_locked() {
  return policy::offer_bits(own_bits_locked(), lcancel::automatic_enabled() && !host::gameplay::vanilla_only(), g.mode, g.yougame);
}

void send_answer_locked(Peer& p, bool yes) {
  const uint8_t seq = g.state.seq[p.who.port];
  if (p.answer_sent && p.answered_seq == seq && p.answer_value == yes) return;
  p.answer_sent = true;
  p.answered_seq = seq;
  p.answer_value = yes;
  const wire::Message m{wire::kAnswer, (uint8_t)g.local, (uint8_t)p.who.port, seq, 0, yes};
  wire::apply(g.state, m);
  g.outgoing.push_back(wire::encode_answer((uint8_t)g.local, (uint8_t)p.who.port, seq, yes));
  host::log("controls: answered %s to P%d's %s", yes ? "yes" : "no", p.who.port + 1, describe(g.state.bits[p.who.port]).c_str());
}

// Decide this player's answer to one peer's latest offer, by the rules, and send it when it is one.
void evaluate_locked(Peer& p) {
  const int port = p.who.port;
  if (!g.state.have_offer[port]) return;
  const unsigned bits = g.state.bits[port];
  if (p.mine == Answer::NoThisMatch) { send_answer_locked(p, false); return; }
  policy::Remembered r = remembered_locked(p.who.key);
  r.allowed |= p.session.allowed;
  r.declined = (r.declined | p.session.declined) & ~r.allowed;
  switch (policy::auto_answer(bits, g.offered, r, false)) {
    case policy::Auto::Yes: p.mine = Answer::Yes; p.own_pending = false; send_answer_locked(p, true); return;
    case policy::Auto::YesOwn:
      if (p.mine == Answer::YesOwn && (p.own_pending || p.answer_sent)) return;   // already on its way
      p.mine = Answer::YesOwn;
      // Locked in: the yes waits kOwnSeconds for "Press Y for defaults" (tick sends it). Not locked in:
      // at once; the player can still say "defaults" until the lock-in.
      if (g.ready) { p.own_pending = true; p.since = g_clock(); }
      else send_answer_locked(p, true);
      return;
    case policy::Auto::No: p.mine = Answer::No; p.own_pending = false; send_answer_locked(p, false); return;
    case policy::Auto::NoUnused: p.mine = Answer::NoUnused; p.own_pending = false; send_answer_locked(p, false); return;
    case policy::Auto::Ask:
      // Asked on the character select. Locked in, the match waits up to kAskSeconds for the answer.
      if (p.mine != Answer::Ask) { p.mine = Answer::Ask; p.since = g_clock(); }
      p.own_pending = false;
      return;
  }
}

void offer_locked() {
  const unsigned bits = own_offer_locked();
  if (g.offer_sent && bits == g.offered) return;
  g.offered = bits;
  g.seq = (uint8_t)(g.seq + 1);
  g.offer_sent = true;
  const wire::Message m{wire::kOffer, (uint8_t)g.local, 0, g.seq, (uint8_t)bits, false};
  wire::apply(g.state, m);
  g.outgoing.push_back(wire::encode_offer((uint8_t)g.local, g.seq, bits));
  host::log("controls: offering %s (offer %u)", describe(bits).c_str(), (unsigned)g.seq);
  // An own option that changed can turn a question into this player's own yes.
  for (auto& p : g.peers) if (p.mine == Answer::Ask) evaluate_locked(p);
}

Peer* peer_locked(int port) {
  for (auto& p : g.peers) if (p.who.port == port) return &p;
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- settings
Settings settings() { std::lock_guard<std::mutex> lk(g_mutex); return g_settings; }
void set_settings(const Settings& s) { std::lock_guard<std::mutex> lk(g_mutex); g_settings = s; }
unsigned own_bits() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return own_bits_locked();
}
void set_engine_supported(bool on) { g_engine.store(on, std::memory_order_relaxed); }
bool engine_supported() { return g_engine.load(std::memory_order_relaxed); }
bool advertise() { return engine_supported() && !host::gameplay::vanilla_only(); }
std::atomic<bool> g_test_run{false};
void set_test_run(bool on) { g_test_run.store(on, std::memory_order_relaxed); }
bool test_run() { return g_test_run.load(std::memory_order_relaxed); }

void set_memory_path(const std::string& path) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_memory_path = path;
  g_memory.clear();
  g_memory_loaded = false;
}
size_t remembered_count() { std::lock_guard<std::mutex> lk(g_mutex); load_memory_locked(); return g_memory.size(); }
void forget_all() {
  std::lock_guard<std::mutex> lk(g_mutex);
  load_memory_locked();
  g_memory.clear();
  save_memory_locked();
  host::log("controls: remembered answers forgotten");
}

uint32_t match_word() {
  if (!engine_supported() || host::gameplay::vanilla_only()) return 0;
  if (slippi::online::session_mode() >= 0) return g_frozen.load(std::memory_order_relaxed);
  // Offline the automatic L-cancel bit rides along too, so Start + D-pad Right can turn it off in a match.
  unsigned bits = own_bits() & MU_CONTROL_GAME_BITS;
  if (lcancel::automatic_enabled()) bits |= MU_CONTROL_AUTO_LCANCEL;
  return bits * 0x01010101u;
}

bool lcancel_agreed() {
  const int local = g_frozen_local.load(std::memory_order_relaxed);
  return local >= 0 && g_frozen_yougame.load(std::memory_order_relaxed) &&
         policy::lcancel_needs_consent(g_frozen_mode.load(std::memory_order_relaxed), true) &&
         (mu_controls_slot(g_frozen.load(std::memory_order_relaxed), local) & MU_CONTROL_AUTO_LCANCEL) != 0 &&
         slippi::online::session_mode() == g_frozen_mode.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------- the exchange
void session_begin(int local_port, const std::vector<Player>& others, int mode, bool yougame, bool ready) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g = Session{};
  g.ready = ready;
  if (local_port < 0 || local_port >= wire::kPorts || others.empty() || !policy::exchange(mode, yougame)) return;
  g.active = true;
  g.mode = mode;
  g.yougame = yougame;
  g.local = local_port;
  g.state.player[local_port] = true;
  for (const auto& o : others) {
    if (o.port < 0 || o.port >= wire::kPorts || o.port == local_port || g.state.player[o.port]) continue;
    g.state.player[o.port] = true;
    Peer peer;
    peer.who = o;
    g.peers.push_back(peer);
  }
  host::log("controls: agreeing with %zu player%s (mode %d)", g.peers.size(), g.peers.size() == 1 ? "" : "s", mode);
  offer_locked();
}

void session_end() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g = Session{};
  g_frozen.store(0); g_frozen_mode.store(-1); g_frozen_local.store(-1); g_frozen_yougame.store(false);
}

bool session_active() { std::lock_guard<std::mutex> lk(g_mutex); return g.active; }

std::vector<std::vector<uint8_t>> take_outgoing() {
  std::lock_guard<std::mutex> lk(g_mutex);
  std::vector<std::vector<uint8_t>> out;
  out.swap(g.outgoing);
  return out;
}

void receive(const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (!g.active) return;
  wire::Message m;
  if (!wire::decode(data, size, &m) || m.sender == g.local || !g.state.player[m.sender]) {
    host::log("controls: a malformed message was ignored");
    return;
  }
  wire::apply(g.state, m);
  if (m.kind == wire::kOffer) {
    if (Peer* p = peer_locked(m.sender)) {
      host::log("controls: P%d offers %s (offer %u)", m.sender + 1, describe(m.bits).c_str(), (unsigned)m.seq);
      if (p->mine == Answer::NoLocked) p->mine = Answer::None;
      evaluate_locked(*p);
    }
  } else if (m.about == g.local) {
    host::log("controls: P%d answered %s to this player's offer", m.sender + 1, m.yes ? "yes" : "no");
  }
}

void tick() {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (!g.active || g.in_match) return;
  if (!g.ready) { offer_locked(); return; }
  const uint64_t now = g_clock();
  for (auto& p : g.peers) {
    if (p.mine == Answer::Ask && now - p.since >= (uint64_t)kAskSeconds * 1000) {
      host::log("controls: no answer to P%d's options in %d s: default controls this match", p.who.port + 1, kAskSeconds);
      p.mine = Answer::NoLocked;
      send_answer_locked(p, false);
    } else if (p.own_pending && now - p.since >= (uint64_t)kOwnSeconds * 1000) {
      p.own_pending = false;
      send_answer_locked(p, true);
    }
  }
}

std::vector<std::vector<uint8_t>> before_selection(bool ready) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (!g.active) return {};
  if (!ready) {
    g.ready = false;
    offer_locked();
    // Back from a lock-in: a held own-options yes goes now, and the questions answered no only because
    // they ran out of time are asked again.
    for (auto& p : g.peers) {
      if (p.own_pending) { p.own_pending = false; send_answer_locked(p, true); }
      if (p.mine == Answer::NoLocked) { p.mine = Answer::None; evaluate_locked(p); }
    }
  } else if (!g.ready) {
    offer_locked();
    g.ready = true;
    // Locked in with a question open: its time to answer starts now (tick answers no when it runs out).
    for (auto& p : g.peers) if (p.mine == Answer::Ask) p.since = g_clock();
  }
  std::vector<std::vector<uint8_t>> out;
  out.swap(g.outgoing);
  return out;
}

uint64_t fold_inputs(uint64_t inputs) {
  std::lock_guard<std::mutex> lk(g_mutex);
  return wire::fold_inputs(inputs, g.active, g.active ? wire::decide(g.state) : 0);
}

bool settled() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g.active && wire::settled(g.state);
}

void match_started() {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g.active) g.in_match = true;
}

void freeze(const int owner[4]) {
  std::lock_guard<std::mutex> lk(g_mutex);
  uint32_t word = 0;
  if (g.active) {
    const uint32_t decided = wire::decide(g.state);
    for (int i = 0; i < wire::kPorts; ++i)
      if (owner[i] >= 0 && owner[i] < wire::kPorts) word = mu_controls_with_slot(word, i, mu_controls_slot(decided, owner[i]));
  }
  if (word != g_frozen.load() || g_frozen_mode.load() != g.mode) {
    host::log("controls: this match plays %s", word ? "agreed control options" : "default controls");
    for (int i = 0; i < wire::kPorts && word; ++i)
      if (mu_controls_slot(word, i)) host::log("controls:   P%d %s", i + 1, describe(mu_controls_slot(word, i)).c_str());
  }
  g_frozen.store(word);
  g_frozen_mode.store(g.active ? g.mode : -1);
  g_frozen_local.store(g.active ? g.local : -1);
  g_frozen_yougame.store(g.active && g.yougame);
}

void match_ended() {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_frozen.store(0); g_frozen_mode.store(-1); g_frozen_local.store(-1); g_frozen_yougame.store(false);
  if (!g.active) return;
  g.in_match = false;
  g.ready = false;   // back on the character select, not locked in until the next selection says so
  // "Not this match" was for that match: the next one is answered by the usual rules again.
  for (auto& p : g.peers) if (p.mine == Answer::NoThisMatch) { p.mine = Answer::None; evaluate_locked(p); }
}

CssLine css_line() {
  std::lock_guard<std::mutex> lk(g_mutex);
  CssLine line;
  if (!g.active || g.in_match) return line;
  const uint64_t now = g_clock();
  line.mine = policy::main_bits(g.offered);
  auto left = [&](uint64_t since, int seconds) {
    const uint64_t gone = now - since, total = (uint64_t)seconds * 1000;
    return gone >= total ? 0 : (int)((total - gone + 999) / 1000);
  };
  for (const auto& p : g.peers)
    if (p.mine == Answer::Ask) {
      line.state = Css::Asked;
      line.port = p.who.port;
      line.theirs = policy::main_bits(g.state.bits[p.who.port]);
      line.seconds = g.ready ? left(p.since, kAskSeconds) : 0;
      return line;
    }
  for (const auto& p : g.peers)
    if (p.mine == Answer::YesOwn && (p.own_pending || !g.ready)) {
      line.state = Css::AllowedOwn;
      line.port = p.who.port;
      line.theirs = policy::main_bits(g.state.bits[p.who.port]);
      line.seconds = p.own_pending ? left(p.since, kOwnSeconds) : 0;
      return line;
    }
  bool anyone = line.mine != 0;
  for (const auto& p : g.peers) anyone |= g.state.have_offer[p.who.port] && policy::main_bits(g.state.bits[p.who.port]);
  if (!anyone) return line;
  if (!wire::settled(g.state)) {
    line.state = Css::Waiting;
    if (!g.peers.empty()) line.port = g.peers[0].who.port;
    return line;
  }
  const uint32_t word = wire::decide(g.state);
  line.state = word ? Css::Agreed : Css::Defaults;
  for (int i = 0; i < wire::kPorts; ++i) line.theirs |= policy::main_bits(mu_controls_slot(word, i));
  return line;
}

void set_test_clock(uint64_t (*now_ms)()) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_clock = now_ms ? now_ms : steady_ms;
}

// ---------------------------------------------------------------- the character select and panel
View view() {
  std::lock_guard<std::mutex> lk(g_mutex);
  View v;
  v.active = g.active;
  if (!g.active) return v;
  v.mode = g.mode;
  v.mine = g.offered;
  v.ready = g.ready;
  v.word = wire::decide(g.state);
  for (const auto& p : g.peers) {
    PeerView pv;
    pv.port = p.who.port;
    pv.name = p.who.name;
    pv.bits = g.state.have_offer[p.who.port] ? g.state.bits[p.who.port] : 0;
    pv.mine = g.state.have_offer[p.who.port] ? p.mine : Answer::None;
    pv.they_agree = policy::main_bits(g.offered) == 0 ||
                    (g.state.have_answer[p.who.port][g.local] && g.state.answer_seq[p.who.port][g.local] == g.seq &&
                     g.state.yes[p.who.port][g.local]);
    v.peers.push_back(pv);
  }
  return v;
}

void answer(int port, bool allow) {
  std::lock_guard<std::mutex> lk(g_mutex);
  Peer* p = g.active ? peer_locked(port) : nullptr;
  if (!p || !g.state.have_offer[port]) return;
  const unsigned bits = g.state.bits[port];
  if (!p->who.key.empty()) {
    load_memory_locked();
    g_memory[p->who.key] = policy::remember(remembered_locked(p->who.key), bits, allow);
    if (g_memory.size() > kMemoryMax) g_memory.erase(g_memory.begin());
    save_memory_locked();
  }
  p->session = policy::remember(p->session, bits, allow);
  host::log("controls: %s P%d's %s (remembered)", allow ? "allowed" : "declined", port + 1, describe(bits).c_str());
  // In the match, or locked in with an answer already sent: what decides this match must stay as it is
  // on every machine, so the answer counts from the next match. An open question is answered now, locked
  // in or not: nothing has been sent for it, and the match is waiting for exactly this.
  if (g.in_match || (g.ready && p->mine != Answer::Ask)) return;
  p->mine = allow ? Answer::Yes : Answer::No;
  p->own_pending = false;
  send_answer_locked(*p, allow);
}

void decline_this_match(int port) {
  std::lock_guard<std::mutex> lk(g_mutex);
  Peer* p = g.active ? peer_locked(port) : nullptr;
  // Before the lock-in at any time; locked in only while an own-options yes is still held back.
  if (!p || g.in_match || (g.ready && !(p->mine == Answer::YesOwn && p->own_pending))) return;
  p->own_pending = false;
  p->mine = Answer::NoThisMatch;
  send_answer_locked(*p, false);
}

std::string describe(unsigned bits) {
  std::string out;
  auto add = [&](const char* s) { if (!out.empty()) out += ", "; out += s; };
  if (bits & MU_CONTROL_TAPJUMP_OFF) add("Tap jump off");
  if (bits & MU_CONTROL_TILT_STICK) {
    std::string tilt = "Tilt stick";
    std::string sub;
    if (bits & MU_CONTROL_TILT_DASH_ATTACK) sub = "dash attack";
    if (bits & MU_CONTROL_TILT_ITEM_TILT_THROW) sub += sub.empty() ? "light throws" : ", light throws";
    if (!sub.empty()) tilt += " (" + sub + ")";
    add(tilt.c_str());
  }
  if (bits & MU_CONTROL_AUTO_LCANCEL) add("Auto L-cancel");
  return out.empty() ? std::string("default controls") : out;
}

}  // namespace controls
