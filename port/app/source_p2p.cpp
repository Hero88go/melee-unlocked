// One peer-to-peer rollback session (one game, or several with --p2p-games) from the command line, in the build without the Slippi layer:
// owns the mu_net session, answers the game's session commands from it, and publishes its state to
// the neutral netplay state the rest of the host reads.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_p2p.h"
#include "host.h"
#include "netplay_state.h"
#include "rollback_session.h"
#include "window.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace source_p2p {
namespace {

constexpr uint8_t kCleanup = 0xBA;   // the game leaves its online scene
// Asked by the game when its match scene ends (mu_p2p_entry.c): one reply byte, 1 when another game
// of this session may follow and the game should wait for it instead of leaving for the title.
constexpr uint8_t kSeriesState = 0xC0;
// The stages a game after the first is drawn from: the legal list of the default singles rules, in
// the order the protocol document gives (docs/mu-net-protocol.md, 14.2). Both sides must hold the
// same list, so it is fixed here and not read from any setting.
const uint16_t kLegalStages[6] = {0x1F, 0x20, 0x1C, 0x08, 0x02, 0x03};

struct Options {
  int local_port = -1;
  std::vector<mu_net::Endpoint> peers;
  int slot = 0;
  uint8_t character[2] = {2, 2};   // external character ids, as the character select screen has them
  uint8_t color[2] = {0, 0};
  int stage = 0x1F;                // external stage id, as the stage select screen has them
  uint32_t seed = 0;
  int delay = 2;
  std::string identity_path = "p2p_identity.json";
  std::string result_path;
  bool has_expected = false;
  mu_net::Bytes32 expected{};
  std::string names[2] = {"P1", "P2"};
  int connect_seconds = 0;         // how long to keep dialing; 0: the session's own limit
  int games = 1;                   // games played over this connection; 0: until a player quits
};

struct State {
  Options opt;
  // Never deleted: the process ends with the session's thread already joined by shutdown(), and a
  // static destructor would run after the host's own exit sequence.
  mu_net::RollbackSession* session = nullptr;
  bool closed = false;
  bool match_marked = false;
  bool connected_logged = false;
  bool over_logged = false;
  bool played = false;       // a game of this session reached its first frame
  bool exit_asked = false;
  mu_net::SessionState last_state = mu_net::SessionState::Idle;
  // The session asks for a speed from its own thread; the simulation thread applies it.
  std::atomic<double> speed{1.0};
  double speed_applied = 1.0;
};
State& g() { static State s; return s; }

bool refuse(const char* name, const char* expected) {
  std::fprintf(stderr, "%s %s\n", name, expected);
  return false;
}

// The session has nothing more for the game: it failed before a match, or its games are played.
// The game library of the build without the Slippi layer handles both itself (a message, then the
// title screen). The library of the build with it has no such way out of the waiting scene and no
// scene to go to after the match, so there the process ends: 2 when no game was played, 0 after one.
// The host's exit sequence then calls shutdown(), which writes the result file and stops the session.
void session_done() {
#ifndef MELEE_NO_SLIPPI
  State& s = g();
  if (s.exit_asked) return;
  s.exit_asked = true;
  host::log("p2p: leaving the game (%s)", s.played ? "the session's game is over" : "no match was played");
  host::request_exit(s.played ? 0 : 2);
#endif
}

bool parse_number(const char* text, int base, long low, long high, long* out) {
  if (!text || !*text) return false;
  char* end = nullptr;
  const long v = std::strtol(text, &end, base);
  if (*end || v < low || v > high) return false;
  *out = v;
  return true;
}

// "<id>" or "<id>/<color>".
bool parse_fighter(const std::string& text, uint8_t* character, uint8_t* color) {
  const size_t slash = text.find('/');
  long id = 0, shade = 0;
  if (!parse_number(text.substr(0, slash).c_str(), 0, 0, 255, &id)) return false;
  if (slash != std::string::npos && !parse_number(text.substr(slash + 1).c_str(), 0, 0, 255, &shade)) return false;
  *character = (uint8_t)id;
  *color = (uint8_t)shade;
  return true;
}

// The game content and rule set both sides must share. Fixed for now: the retail 1.02 disc under
// the project's singles rules. A real hash of the loaded disc and gameplay mods replaces this one
// function, and two builds that load different content then refuse each other in the handshake.
// The rules profile names the game library: the one built with the Slippi layer applies a different
// set of online rule codes than the one built without it, so the same inputs would not give the same
// match. Profile 1 is the library without that layer, 2 the one with it; a game of each kind then
// refuses the other in the handshake instead of desyncing a few seconds in.
void content_identity(mu_net::Bytes32& content_hash, uint32_t& rules_profile) {
  static const char kContent[] = "retail-1.02";
  content_hash.fill(0);
  std::memcpy(content_hash.data(), kContent, sizeof kContent - 1);
#ifdef MELEE_NO_SLIPPI
  rules_profile = 1;
#else
  rules_profile = 2;
#endif
}

// There is no negotiation message yet, so everything in the descriptor comes from options both
// players pass identically. The match id too: it is made from them, not drawn at random.
mu_net::SessionDescriptor build_descriptor(const Options& o) {
  mu_net::SessionDescriptor d;
  d.route = mu_net::Route::P2PDirect;
  d.build_id = MELEE_PORT_VERSION;
  content_identity(d.content_hash, d.rules_profile);
  for (int i = 0; i < 2; ++i) {
    d.players[i].present = true;
    d.players[i].character = o.character[i];
    d.players[i].color = o.color[i];
    d.players[i].name = o.names[i];
  }
  d.stage = (uint16_t)o.stage;
  d.rng_seed = o.seed;
  d.input_delay = (uint8_t)o.delay;
  const uint8_t id[16] = {'c', 'l', 'i', 0, (uint8_t)(o.seed >> 24), (uint8_t)(o.seed >> 16), (uint8_t)(o.seed >> 8),
                          (uint8_t)o.seed, o.character[0], o.color[0], o.character[1], o.color[1],
                          (uint8_t)(o.stage >> 8), (uint8_t)o.stage, (uint8_t)o.delay, 0};
  std::memcpy(d.match_id.data(), id, sizeof id);
  return d;
}

void apply_speed() {
  const double speed = g().speed.load(std::memory_order_relaxed);
  if (speed == g().speed_applied) return;
  g().speed_applied = speed;
  host::set_emulation_speed(speed);
}

// The session's state into the neutral netplay state, and one log line per change worth a line.
void publish() {
  using mu_net::SessionState;
  State& s = g();
  const SessionState state = s.session->state();
  const mu_net::SessionCounters counters = s.session->counters();
  // Between two games of a session the session is still on: nothing that is held back during a
  // network session (offline-only codes and the like) may switch on for those seconds.
  const mu_net::NextGame next = s.session->next_game();
  const bool between = state == SessionState::Ended && (next == mu_net::NextGame::Waiting || next == mu_net::NextGame::Agreed);
  const bool live = state != SessionState::Idle && state != SessionState::Failed && (state != SessionState::Ended || between);
  auto& shared = host::netplay::session();
  shared.mode.store(live ? (int)host::netplay::kDirect : (int)host::netplay::kOffline, std::memory_order_relaxed);
  shared.in_match.store(state == SessionState::Playing, std::memory_order_relaxed);
  shared.in_menus.store(live && state != SessionState::Playing, std::memory_order_relaxed);
  shared.local_slot.store(s.opt.slot, std::memory_order_relaxed);
  shared.ping_ms.store((int)(counters.ping_us / 1000), std::memory_order_relaxed);
  if (state == SessionState::Playing) s.played = true;
  if (state == s.last_state) return;
  s.last_state = state;
  const bool path_up = state == SessionState::Handshaking || state == SessionState::Verifying ||
                       state == SessionState::Ready || state == SessionState::Playing;
  if (path_up && !s.connected_logged) {
    s.connected_logged = true;
    host::log("p2p: connected to the other player (local UDP port %u)", (unsigned)s.session->local_port());
  }
  if (state == SessionState::Failed) {
    host::log("p2p: failed: %s", s.session->failure_text().c_str());
    // Before a match: the game would wait in its loading scene with no way out. A failure during a
    // match is left to the game, which ends the match as disconnected and then asks to clean up.
    if (!s.played) session_done();
  }
}

// One result file per game: the first under the name given, game n under "<name>.g<n><extension>".
std::string result_path(const std::string& base, uint32_t game) {
  if (game <= 1) return base;
  const size_t slash = base.find_last_of("/\\");
  const size_t dot = base.rfind('.');
  const std::string tag = ".g" + std::to_string(game);
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return base + tag;
  return base.substr(0, dot) + tag + base.substr(dot);
}

void write_result() {
  State& s = g();
  if (s.opt.result_path.empty()) return;
  mu_net::GameResult result;
  if (!s.session->result(result)) return;   // no game was played: nothing to record
  const uint32_t game = s.session->game_index();
  const std::string path = result_path(s.opt.result_path, game);
  std::string error;
  if (s.session->write_result_file(path, &error))
    host::log("p2p: result file written: %s (game %u, %d input frames, the other side's inputs %s)", path.c_str(), (unsigned)game,
              result.transcript_frames,
              result.peer_agreement > 0 ? "agree" : result.peer_agreement == 0 ? "DISAGREE" : "not compared yet");
  else
    host::log("p2p: result file not written: %s", error.c_str());
}

// The game is waiting for a match (it asked for the match state). When the next game of the session
// was agreed, it begins here: the finished game's file is written once more first (the other side's
// agreement may have arrived since the game ended), because the session then forgets that game.
void advance_series() {
  State& s = g();
  if (s.session->next_game() != mu_net::NextGame::Agreed) return;
  write_result();
  if (!s.session->begin_next_game()) return;
  s.match_marked = false;   // `@match` sections of an input script count from this game's frame 1
  const mu_net::SessionDescriptor d = s.session->descriptor();
  host::log("p2p: game %u of the session: stage %u, seed %08X", (unsigned)s.session->game_index(), (unsigned)d.stage, d.rng_seed);
}

void close_session() {
  State& s = g();
  if (!s.session || s.closed) return;
  write_result();
  s.session->stop();
  s.closed = true;
  s.speed.store(1.0);
  apply_speed();
  publish();
}

bool on_command(uint8_t cmd, const uint8_t* payload, uint32_t size, std::vector<uint8_t>& reply) {
  using namespace host::netplay;
  State& s = g();
  apply_speed();
  switch (cmd) {
    case kMatchState:
      if (s.closed) {
        // A stopped session reports "idle", which the game's wait would sit on forever.
        static const char text[] = "The match is over";
        reply.assign(kMatchStateSize, 0);
        reply[0] = kStateError;
        std::memcpy(reply.data() + kMatchStateError, text, sizeof text);
        session_done();
        return true;
      }
      advance_series();
      {
        // The game waits here between two games. If the next one is not going to come (the other
        // player left, or did not answer), the wait must end: the same error as a closed session.
        std::string why;
        if (s.session->state() == mu_net::SessionState::Ended && s.session->next_game(&why) == mu_net::NextGame::Over) {
          if (!s.over_logged) { s.over_logged = true; host::log("p2p: the session is over: %s", why.c_str()); }
          static const char text[] = "The match is over";
          reply.assign(kMatchStateSize, 0);
          reply[0] = kStateError;
          std::memcpy(reply.data() + kMatchStateError, text, sizeof text);
          publish();
          session_done();
          return true;
        }
      }
      s.session->match_state_reply(reply);
      publish();
      return true;
    case kSeriesState: {
      // Only a game that reported its end and is waiting for, or has, the other side's NEXT goes
      // back to the waiting scene. Anything else leaves as a single game always did.
      const mu_net::NextGame next = s.closed ? mu_net::NextGame::Over : s.session->next_game();
      reply.assign(1, (uint8_t)(next == mu_net::NextGame::Waiting || next == mu_net::NextGame::Agreed ? 1 : 0));
      publish();
      return true;
    }
    case kInputs:
      // The game resends frame 1 while it waits for the other side; the first one is the start that
      // `@match` sections of an input script count from.
      if (!s.match_marked && size >= 4 && mu_net::get_be32(payload) == 1) {
        s.match_marked = true;
        host::input_mark_match_start();
      }
      s.session->on_inputs(payload, size, reply);
      publish();
      return true;
    case kReportGame: {
      s.session->on_game_end(payload, size);
      const mu_net::SessionCounters c = s.session->counters();
      host::log("p2p: game end: %d input frames confirmed, %llu waits, %llu frames stalled, %llu checksums compared, "
                "%llu mismatched%s", c.transcript_frames, (unsigned long long)c.waits, (unsigned long long)c.stalls,
                (unsigned long long)c.checksums_compared, (unsigned long long)c.checksum_mismatches,
                c.desync ? ", DESYNC" : "");
      write_result();
      publish();
      return true;
    }
    case kCleanup:
      close_session();
      session_done();
      return true;
    default:
      // The other session commands (searching, chat, ranked reports) have nothing behind them here:
      // an empty answer, as the host gives without a session.
      return is_session_command(cmd);
  }
}

}  // namespace

bool option(const std::string& name, const char* value) {
  Options& o = g().opt;
  const std::string text = value ? value : "";
  long n = 0;
  if (name == "--p2p-port") {
    if (!parse_number(value, 10, 0, 65535, &n)) return refuse("--p2p-port", "<local UDP port, 0 to 65535>");
    o.local_port = (int)n;
  } else if (name == "--p2p-peer") {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || !parse_number(text.c_str() + colon + 1, 10, 1, 65535, &n))
      return refuse("--p2p-peer", "<dotted IPv4 address>:<port>");
    if ((int)o.peers.size() >= mu_net::kMaxCandidates) return refuse("--p2p-peer", "was given too many times");
    mu_net::Endpoint e;
    e.address = text.substr(0, colon);
    e.port = (uint16_t)n;
    o.peers.push_back(e);
  } else if (name == "--p2p-slot") {
    if (!parse_number(value, 10, 0, 1, &n)) return refuse("--p2p-slot", "<0|1>");
    o.slot = (int)n;
  } else if (name == "--p2p-chars") {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || !parse_fighter(text.substr(0, colon), &o.character[0], &o.color[0]) ||
        !parse_fighter(text.substr(colon + 1), &o.character[1], &o.color[1]))
      return refuse("--p2p-chars", "<character id>[/<color>]:<character id>[/<color>] (slot 0, then slot 1)");
  } else if (name == "--p2p-stage") {
    if (!parse_number(value, 0, 0, 0xFFFF, &n)) return refuse("--p2p-stage", "<stage id>");
    o.stage = (int)n;
  } else if (name == "--p2p-seed") {
    char* end = nullptr;
    const unsigned long seed = std::strtoul(text.c_str(), &end, 16);
    if (text.empty() || *end || text.size() > 10) return refuse("--p2p-seed", "<hex, up to 8 digits>");
    o.seed = (uint32_t)seed;
  } else if (name == "--p2p-delay") {
    if (!parse_number(value, 10, mu_net::kMinInputDelay, mu_net::kMaxInputDelay, &n)) return refuse("--p2p-delay", "<1 to 15 frames>");
    o.delay = (int)n;
  } else if (name == "--p2p-identity") {
    if (text.empty()) return refuse("--p2p-identity", "<file>");
    o.identity_path = text;
  } else if (name == "--p2p-expect") {
    if (!mu_net::from_hex(text.data(), text.size(), o.expected.data(), o.expected.size()))
      return refuse("--p2p-expect", "<64 hex digits: the other player's identity key>");
    o.has_expected = true;
  } else if (name == "--p2p-result") {
    if (text.empty()) return refuse("--p2p-result", "<file>");
    o.result_path = text;
  } else if (name == "--p2p-names") {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size() || colon > mu_net::kMaxNameBytes ||
        text.size() - colon - 1 > mu_net::kMaxNameBytes)
      return refuse("--p2p-names", "<slot 0 name>:<slot 1 name> (at most 31 bytes each)");
    o.names[0] = text.substr(0, colon);
    o.names[1] = text.substr(colon + 1);
  } else if (name == "--p2p-connect-seconds") {
    if (!parse_number(value, 10, 1, 600, &n)) return refuse("--p2p-connect-seconds", "<1 to 600>");
    o.connect_seconds = (int)n;
  } else if (name == "--p2p-games") {
    if (!parse_number(value, 10, 0, (long)mu_net::kMaxSessionGames, &n)) return refuse("--p2p-games", "<1 to 127 games over one connection, or 0 for no limit>");
    o.games = (int)n;
  } else {
    std::fprintf(stderr, "unknown option %s\n", name.c_str());
    return false;
  }
  return true;
}

bool requested() { return g().opt.local_port >= 0 && !g().opt.peers.empty(); }

bool check() {
  const Options& o = g().opt;
  if (requested()) return true;
  // Half a request is a mistake worth stopping for: the game would boot to the title with no word.
  if (o.local_port >= 0 || !o.peers.empty()) {
    std::fprintf(stderr, "a P2P match needs both --p2p-port and --p2p-peer\n");
    return false;
  }
  return true;
}

void start(bool harness) {
  State& s = g();
  if (!requested() || s.session) return;
  const Options& o = s.opt;
  mu_net::PeerIdentity identity;
  std::string error;
  const mu_net::IdentityFile loaded = mu_net::load_or_create_identity(o.identity_path, identity, &error);
  if (loaded == mu_net::IdentityFile::Failed) {
    host::log("p2p: identity file %s cannot be used: %s", o.identity_path.c_str(), error.c_str());
    session_done();
    return;
  }
  host::log("p2p: identity %s (%s %s)", identity.id().c_str(), loaded == mu_net::IdentityFile::Created ? "created" : "from",
            o.identity_path.c_str());

  s.session = new mu_net::RollbackSession;
  mu_net::SessionCallbacks callbacks;
  callbacks.log = [](const char* line) { host::log("p2p: %s", line); };
  callbacks.set_emulation_speed = [](double speed) { g().speed.store(speed, std::memory_order_relaxed); };
  // A launcher starts the two games seconds apart and each boots for several more before it gets
  // here, so it asks for a longer wait than two games started by hand at the same moment need.
  mu_net::SessionOptions session_options;
  if (o.connect_seconds > 0) session_options.connect_limit_us = (uint64_t)o.connect_seconds * 1000000;
  session_options.max_games = (uint32_t)o.games;
  session_options.stage_pool.assign(std::begin(kLegalStages), std::end(kLegalStages));
  s.session->configure(callbacks, session_options);
  s.session->set_identity(identity);
  identity.wipe();

  mu_net::EndpointInfo endpoint;
  endpoint.local_port = (uint16_t)o.local_port;
  endpoint.candidates = o.peers;
  endpoint.has_expected_peer = o.has_expected;
  endpoint.expected_peer = o.expected;
  const mu_net::SessionDescriptor descriptor = build_descriptor(o);
  std::string peers;
  for (const mu_net::Endpoint& e : o.peers) peers += (peers.empty() ? "" : ", ") + e.address + ":" + std::to_string(e.port);
  host::log("p2p: slot %d, characters %u/%u and %u/%u, stage %d, seed %08X, delay %d, games %d, UDP port %d, peer %s", o.slot,
            (unsigned)o.character[0], (unsigned)o.color[0], (unsigned)o.character[1], (unsigned)o.color[1], o.stage,
            o.seed, o.delay, o.games, o.local_port, peers.c_str());
  // The launcher's lobby held this port a moment ago and closes it just before it starts the game:
  // a port still busy gets two seconds to come free. Every other failure is final at once.
  bool started = s.session->start(descriptor, o.slot, endpoint);
  for (int attempt = 0; !started && attempt < 10 && o.local_port > 0 &&
                        s.session->failure_text().find("port could not be opened") != std::string::npos; ++attempt) {
    if (attempt == 0) host::log("p2p: UDP port %d is busy, trying again for 2 s", o.local_port);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    started = s.session->start(descriptor, o.slot, endpoint);
  }
  if (!started) {
    // Without a started session the game is not asked to enter a match: it boots as usual (the build
    // without the Slippi layer), or the run ends with the failure code (the build with it, which a
    // launcher started for this one match).
    host::log("p2p: the session did not start: %s", s.session->failure_text().c_str());
    s.closed = true;
    session_done();
    return;
  }
  host::netplay::config().delay = o.delay;
  std::array<std::string, 4> names;
  names[0] = o.names[0];
  names[1] = o.names[1];
  host::netplay::set_player_names(names);
  host::netplay::set_command_handler(on_command);
  host::netplay::MatchRequest request;
  request.requested = true;
  request.harness = harness;
  request.mode = host::netplay::kDirect;
  request.local_port = 0;   // the local player always holds the first controller
  host::netplay::set_match_request(request);
  publish();
}

void shutdown() {
  close_session();
}

}  // namespace source_p2p
