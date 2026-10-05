// One P2P session from connect to result: handshake, descriptor check, the B0 and B3 replies, stall rule, time sync, desync check, and the agreement on each further game.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "rollback_session.h"
#include "session_security.h"
#include "monocypher.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <windows.h>

namespace mu_net {
namespace {

constexpr int kSyncInterval = 30;          // frames between time-sync decisions (one full offset ring)
constexpr int kTranscriptMarks = 64;       // recent frames whose running digest is kept for comparison
constexpr size_t kMaxChecksums = 600;      // ten seconds of our own finalized-frame checksums
constexpr int32_t kSyncSlack = 10000;      // microseconds of offset the time sync leaves alone
constexpr uint64_t kPingEveryUs = 250000;  // before the match, when no COMMIT measures the round trip
const char kTranscriptDomain[] = "MeleeUnlockedNet1 inputs";
constexpr uint8_t kByeLeft = 0, kByeMismatch = 1;
constexpr uint8_t kEndQuit = 7;            // the game's end method for a quit from the pause screen

// The game's match state reply: byte offsets (its own contract).
enum {
  kMsState = 0, kMsLocalReady = 1, kMsRemoteReady = 2, kMsLocalIndex = 3, kMsRemoteIndex = 4, kMsRng = 5, kMsDelay = 9,
  kMsLocalName = 15, kMsNameSize = 31, kMsPlayerNames = 46, kMsOpponentName = 170, kMsError = 357, kMsErrorSize = 241,
  kMsGameInfo = 598, kGameInfoSize = 0x138, kMsMatchId = 910, kMsMatchIdSize = 51, kMsAltStage = 961,
  kMsStateIdle = 0, kMsStateConnecting = 3, kMsStateConnected = 4, kMsStateError = 5,
};
// The inputs reply: byte offsets.
enum { kRxResult = 0, kRxRemoteCount = 1, kRxChecksums = 2, kRxLatest = 26, kRxSmallestLatest = 38, kRxInputs = 42, kRxInputFrames = 7 };

void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

// Text for the game's menu font: Shift-JIS, with ASCII punctuation as its full-width form (the
// font has no narrow punctuation). `out` is zero-filled and always ends in a zero byte.
void game_text(const std::string& utf8, int max_chars, uint8_t* out, size_t out_size) {
  std::memset(out, 0, out_size);
  if (utf8.empty() || out_size < 2) return;
  wchar_t wide[512];
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)std::min<size_t>(utf8.size(), 480), wide, 512);
  if (n <= 0) return;
  if (n > max_chars) n = max_chars;
  if (n > 0 && wide[n - 1] >= 0xD800 && wide[n - 1] <= 0xDBFF) --n;   // never half a surrogate pair
  for (int i = 0; i < n; ++i) {
    wchar_t& c = wide[i];
    const bool alnum = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    if (c == L'"') c = 0x201D;
    else if (c == L'\'') c = 0x2019;
    else if (c > 0x20 && c < 0x7F && !alnum) c = (wchar_t)(c + 0xFEE0);
  }
  char sj[1100];
  const int m = WideCharToMultiByte(932, 0, wide, n, sj, (int)sizeof sj, nullptr, nullptr);
  if (m <= 0) return;
  const size_t room = std::min<size_t>((size_t)m, out_size - 1);
  size_t k = 0;
  while (k < room) {   // whole characters only
    const unsigned char c = (unsigned char)sj[k];
    const size_t len = ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) ? 2 : 1;
    if (k + len > room) break;
    k += len;
  }
  std::memcpy(out, sj, k);
}

// The game info block the match starts from, written field by field from the descriptor. The
// layout is the game's own start-of-match structure: a 0x60-byte header and six 0x24-byte player
// entries. Everything a descriptor does not decide is the fixed singles setup (no items, no teams).
void build_game_info(const SessionDescriptor& d, uint8_t* b) {
  std::memset(b, 0, kGameInfoSize);
  static const uint8_t one[4] = {0x3F, 0x80, 0x00, 0x00};   // 1.0f
  b[0x00] = 0x32; b[0x01] = 0x01;
  b[0x02] = (d.features & kFeaturePauseAllowed) ? 0x86 : 0x8E;   // bit 3 set = pause off
  b[0x03] = 0x4C; b[0x04] = 0xC3;
  b[0x08] = 0;      // not teams
  b[0x0B] = 0xFF;   // item frequency: none
  b[0x0C] = 0xFF; b[0x0D] = 0x6E;
  b[0x0E] = (uint8_t)(d.stage >> 8); b[0x0F] = (uint8_t)d.stage;
  put32(b + 0x10, d.timer_seconds);
  b[0x20] = b[0x21] = b[0x22] = 0xFF;
  b[0x23] = 0xF8; b[0x27] = 0x0F;   // item switch, 0x23..0x2A: every item off
  std::memcpy(b + 0x2C, one, 4); std::memcpy(b + 0x30, one, 4); std::memcpy(b + 0x34, one, 4);
  for (int i = 0; i < 6; ++i) {
    uint8_t* p = b + 0x60 + i * 0x24;
    const bool human = i < kMaxSlots && d.players[i].present;
    p[0x00] = human ? d.players[i].character : (i < 4 ? 0x15 : 0x21);
    p[0x01] = human ? 0 : 3;                    // 0 = human, 3 = empty
    p[0x02] = i < 4 ? d.stocks : 4;
    p[0x03] = human ? d.players[i].color : 0;
    p[0x05] = human ? (uint8_t)i : 0xFF;
    p[0x08] = 0x09;
    p[0x0A] = 0x78;
    p[0x0C] = i < 4 ? 0xC0 : 0x40;
    p[0x0E] = 0x04;
    p[0x0F] = i < 4 ? 0x01 : 0x00;
    std::memcpy(p + 0x18, one, 4); std::memcpy(p + 0x1C, one, 4); std::memcpy(p + 0x20, one, 4);
  }
  // Two players on the same character and color: the second gets the next shade, as the game's
  // own character select does. Sheik counts as Zelda (one fighter, two forms).
  for (int i = 0; i < kMaxSlots; ++i) {
    if (!d.players[i].present) continue;
    uint8_t shade = 0;
    const uint8_t ci = d.players[i].character == 0x13 ? 0x12 : d.players[i].character;
    for (int j = 0; j < i; ++j) {
      if (!d.players[j].present) continue;
      const uint8_t cj = d.players[j].character == 0x13 ? 0x12 : d.players[j].character;
      if (ci == cj && d.players[i].color == d.players[j].color) ++shade;
    }
    b[0x60 + i * 0x24 + 0x07] = shade;
  }
}

}  // namespace

struct RollbackSession::Impl {
  mutable std::mutex mu;
  std::thread pump;
  std::atomic<bool> pump_quit{false};

  SessionCallbacks cb;
  SessionOptions opt;
  PeerIdentity identity;
  bool has_secret = false;
  Bytes32 session_secret{};

  SessionDescriptor desc;
  Bytes32 digest{};
  int local_slot = 0, remote_slot = 1;
  std::unique_ptr<PeerConnector> connector;
  Link* link = nullptr;
  SessionSecurity security;
  InputTransport transport;
  SessionState state = SessionState::Idle;
  std::string failure;
  bool has_expected = false;
  Bytes32 expected{};

  // What spans the games of one session. Reset by start() only.
  struct Series {
    uint32_t game = 1;              // which game `desc` describes
    bool peer_info = false;         // the peer sent INFO; without it the peer is revision 0
    uint8_t peer_minor = 0;
    uint16_t peer_games = 1;
    NextGame next = NextGame::None;
    bool have_peer_next = false;    // the peer's NEXT may arrive while this side's game still runs
    uint16_t peer_next_game = 0;
    Bytes32 peer_next_digest{};
    SessionDescriptor next_desc;
    Bytes32 next_digest{};
    uint64_t next_deadline_us = 0;
    std::string over_why;
  } series;

  // One game, from start() or begin_next() to the next of either. Reset as a block.
  struct Run {
    uint64_t deadline_us = 0, next_ping_us = 0;
    uint32_t hello_generation = 0;
    bool verify_sent = false, pinned = false;
    bool saw_wrong_key = false, saw_other_version = false;
    bool match_begun = false;
    int32_t last_frame = 0;
    uint8_t delay = 2;
    bool peer_gone = false;
    bool gone_after_end = false;   // ... and only after the game had reported its end: the game itself was not cut short
    int wait_frames = 0;
    uint64_t wait_started_us = 0;
    int32_t wait_latest = 0;
    bool shedding = false, advancing = false;
    int frames_to_shed = 0, frames_to_advance = 0;
    bool gap_logged = false, risk_logged = false;
    int32_t floor = 0;   // highest finalized frame the game has reported
    int32_t last_compared = 0;
    int32_t transcript_frames = 0;
    bool transcript_ok = true;
    bool have_peer_result = false;
    int32_t peer_result_frames = 0;
    Bytes32 peer_result_digest{};
    int agreement = -1;
    bool game_reported = false;
    uint64_t reject_logs = 0;
  } run;
  bool speed_changed = false;
  std::map<int32_t, uint32_t> checksums;
  SessionCounters counters;
  crypto_blake2b_ctx transcript;
  struct Mark { int32_t frame = -1; Bytes32 digest{}; } marks[kTranscriptMarks];
  GameResult game;
  std::vector<uint8_t> plain, packet, answer, message;

  // ------------------------------------------------------------ small helpers
  uint64_t now() const {
    if (cb.now_us) return cb.now_us();
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void log(const char* fmt, ...) {
    if (!cb.log) return;
    char line[512];
    int at = std::snprintf(line, sizeof line, "mu_net: ");
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line + at, sizeof line - at, fmt, args);
    va_end(args);
    try { cb.log(line); } catch (...) {}
  }
  void set_speed(double speed) {
    if (speed == 1.0 && !speed_changed) return;
    speed_changed = speed != 1.0;
    if (!cb.set_emulation_speed) return;
    try { cb.set_emulation_speed(speed); } catch (...) {}
  }
  void fail(const std::string& why) {
    if (state == SessionState::Failed) return;
    state = SessionState::Failed;
    failure = why;
    log("session failed: %s", why.c_str());
    set_speed(1.0);
  }
  bool playable() const { return state == SessionState::Ready || state == SessionState::Playing || state == SessionState::Ended; }
  bool send_plain(uint8_t channel, const std::vector<uint8_t>& bytes, bool reliable) {
    if (!link || !security.seal(channel, bytes.data(), bytes.size(), packet)) return false;
    return link->send(channel, packet.data(), packet.size(), reliable);
  }
  void send_hello() {
    const auto& hello = security.hello();
    if (link && !hello.empty()) link->send(kChannelControl, hello.data(), hello.size(), true);
  }
  void send_verify() {
    message.clear();
    Writer w(message);
    w.u8(kMsgVerify);
    w.bytes(digest.data(), digest.size());
    w.u8((uint8_t)local_slot);
    send_plain(kChannelControl, message, true);
    run.verify_sent = true;
  }
  void send_bye(uint8_t reason) {
    message.clear();
    Writer w(message);
    w.u8(kMsgBye);
    w.u8(reason);
    send_plain(kChannelControl, message, true);
  }

  // ------------------------------------------------------------ more than one game
  bool peer_knows_next() const { return opt.minor >= 1 && series.peer_info && series.peer_minor >= 1; }
  // Games this session plays: the smaller of the two sides' numbers, one with a revision 0 peer.
  uint32_t game_limit() const {
    if (!peer_knows_next()) return 1;
    const uint32_t mine = opt.max_games ? std::min(opt.max_games, kMaxSessionGames) : kMaxSessionGames;
    const uint32_t theirs = series.peer_games ? std::min<uint32_t>(series.peer_games, kMaxSessionGames) : kMaxSessionGames;
    return std::min(mine, theirs);
  }
  void send_info() {
    if (opt.minor < 1) return;
    message.clear();
    Writer w(message);
    w.u8(kMsgInfo);
    w.u8(opt.minor);
    w.u16((uint16_t)std::min(opt.max_games, kMaxSessionGames));
    send_plain(kChannelControl, message, true);
  }
  void series_over(const char* why) {
    if (series.next == NextGame::Over) return;
    series.next = NextGame::Over;
    series.over_why = why;
    log("no further game in this session: %s", why);
  }
  void judge_next() {
    if (series.next != NextGame::Waiting || !series.have_peer_next) return;
    if (series.peer_next_game != series.game + 1 || crypto_verify32(series.peer_next_digest.data(), series.next_digest.data()) != 0) {
      send_bye(kByeMismatch);
      series_over("The two games do not agree on the next game's setup");
      return;
    }
    series.next = NextGame::Agreed;
    log("next game agreed: game %u, match setup %s, stage %u", (unsigned)(series.game + 1), to_hex(series.next_digest.data(), 8).c_str(),
        (unsigned)series.next_desc.stage);
  }
  // The game has reported its end: either this side says it is ready for the next game, or the
  // session is over. Every reason not to go on is one both sides see (the limit both announced, a
  // synchronized end method, a desync both checksums show) or one the peer learns from the BYE
  // that follows when the host stops.
  void offer_next(uint64_t now_us) {
    if (series.next != NextGame::None) return;
    if (series.game >= game_limit())
      return series_over(opt.minor >= 1 && !peer_knows_next() ? "The other game plays one game per session" : "The session's last game was played");
    if (run.peer_gone) return series_over("The other player left");
    if (counters.desync) return series_over("The two games went out of sync");
    if (!game.has_outcome || game.winner < -1) return series_over("The game did not finish");
    if (game.end_method == kEndQuit) return series_over("A player quit the game");
    series.next_desc = next_descriptor(desc, digest, series.game + 1, opt.stage_pool);
    series.next_digest = mu_net::digest(series.next_desc);
    message.clear();
    Writer w(message);
    w.u8(kMsgNext);
    w.u16((uint16_t)(series.game + 1));
    w.bytes(series.next_digest.data(), series.next_digest.size());
    send_plain(kChannelControl, message, true);
    series.next = NextGame::Waiting;
    series.next_deadline_us = now_us + opt.next_limit_us;
    judge_next();
  }
  bool begin_next(uint64_t now_us) {
    if (series.next != NextGame::Agreed || state != SessionState::Ended || !link) return false;
    desc = series.next_desc;
    digest = series.next_digest;
    ++series.game;
    series.next = NextGame::None;
    series.have_peer_next = false;
    // The connection and its keys stay; everything counted per game starts over.
    const Run before = run;
    run = Run();
    run.verify_sent = true;
    run.pinned = true;
    run.hello_generation = before.hello_generation;
    run.reject_logs = before.reject_logs;
    run.next_ping_us = now_us;
    const SessionCounters kept = counters;
    counters = SessionCounters();
    counters.packets_rejected = kept.packets_rejected;
    counters.messages_malformed = kept.messages_malformed;
    checksums.clear();
    game = GameResult();
    for (auto& m : marks) m = Mark();
    // This game's frames travel above every frame of the games before (kGameFrameSpan apart), so
    // a COMMIT or ACK of the finished game that arrives now is recognised and dropped.
    transport.reset(opt.game_frame_us, (int32_t)(series.game - 1) * kGameFrameSpan);
    state = SessionState::Ready;
    log("ready: game %u, both games hold match setup %s", (unsigned)series.game, to_hex(digest.data(), 8).c_str());
    return true;
  }

  // ------------------------------------------------------------ start and stop
  bool start_common(const SessionDescriptor& descriptor, int slot, Link* use_link, const Bytes32* expected_peer) {
    run = Run();
    series = Series();
    counters = SessionCounters();
    checksums.clear();
    failure.clear();
    game = GameResult();
    for (auto& m : marks) m = Mark();
    state = SessionState::Idle;
    desc = descriptor;
    std::string why;
    if (!validate(desc, &why)) { fail(why); return false; }
    if (slot < 0 || slot > 1) { fail("The local player slot is not one of the two players"); return false; }
    if (!identity.valid) { fail("There is no peer identity to connect with"); return false; }
    local_slot = slot;
    remote_slot = 1 - slot;
    PlayerSlot& me = desc.players[local_slot];
    const PlayerSlot& peer = desc.players[remote_slot];
    if (!all_zero(me.identity_key) && me.identity_key != identity.public_key) { fail("The match was set up for another identity than this game's"); return false; }
    if (!all_zero(me.session_key) && !has_secret) { fail("The match names a session key this game was not given"); return false; }
    has_expected = expected_peer != nullptr;
    if (expected_peer) expected = *expected_peer;
    else if (!all_zero(peer.identity_key)) { has_expected = true; expected = peer.identity_key; }
    if (!security.begin(identity, has_expected ? &expected : nullptr, has_secret ? &session_secret : nullptr)) {
      fail("The secure session could not be prepared");
      return false;
    }
    if (!all_zero(me.session_key) && me.session_key != security.local_session_key()) { fail("The session key does not match the one the match was set up with"); return false; }
    transport.reset(opt.game_frame_us);
    link = use_link;
    state = SessionState::Connecting;
    run.deadline_us = now() + opt.connect_limit_us;
    log("session starts: %s, slot %d, protocol %u, delay %u, build %s%s",
        desc.route == Route::P2PUnranked ? "P2P Unranked" : "P2P Direct", local_slot, (unsigned)kProtocolVersion,
        (unsigned)desc.input_delay, desc.build_id.c_str(), has_expected ? ", peer identity required" : "");
    return true;
  }

  void teardown() {
    if (link) {
      if (security.established() && state != SessionState::Idle) send_bye(kByeLeft);
      link->flush();
      if (connector) connector->close();
    }
    link = nullptr;
    connector.reset();
    security.wipe();
    if (state != SessionState::Idle) log("session stopped");
    state = SessionState::Idle;
    set_speed(1.0);
  }

  // ------------------------------------------------------------ network
  void on_handshake() {
    PlayerSlot& me = desc.players[local_slot];
    PlayerSlot& peer = desc.players[remote_slot];
    me.identity_key = identity.public_key;
    me.session_key = security.local_session_key();
    if (!all_zero(peer.session_key) && peer.session_key != security.peer_session_key()) {
      fail("The other game did not use the session key the match was set up with");
      return;
    }
    peer.identity_key = security.peer_identity();
    peer.session_key = security.peer_session_key();
    // From here both sides hold a descriptor that names the two identities and the two session
    // keys of this very connection. Equal digests therefore mean equal setup and the same peers.
    digest = mu_net::digest(desc);
    state = SessionState::Verifying;
    log("handshake done with %s", to_hex(peer.identity_key.data(), 8).c_str());
    if (security.decider()) {
      // Two paths can be up at once (each side dialed the other). The decider keeps the one this
      // hello came on and says so by sending its VERIFY there; the hello goes first because this
      // side may never have sent one on that particular path.
      link->pin_path();
      run.pinned = true;
      send_hello();
      send_verify();
    }
  }

  void on_control(uint64_t now_us) {
    if (plain.empty()) { ++counters.messages_malformed; return; }
    Reader r(plain.data(), plain.size());
    switch (r.u8()) {
      case kMsgVerify: {
        Bytes32 theirs{};
        r.bytes(theirs.data(), theirs.size());
        const uint8_t slot = r.u8();
        if (!r.done()) { ++counters.messages_malformed; return; }
        if (state != SessionState::Verifying) return;   // a repeat
        if (!run.verify_sent) {
          link->pin_path();
          run.pinned = true;
          send_verify();
        }
        if (crypto_verify32(theirs.data(), digest.data()) != 0 || slot != remote_slot) {
          send_bye(kByeMismatch);
          fail("The two games do not agree on the match setup (build, mods, characters, stage or rules differ)");
          return;
        }
        state = SessionState::Ready;
        run.next_ping_us = now_us;
        log("ready: both games hold match setup %s", to_hex(digest.data(), 8).c_str());
        send_info();   // after VERIFY on the ordered channel, so it never reaches a peer before its own Ready
        return;
      }
      case kMsgBye: {
        const uint8_t reason = r.u8();
        if (!r.done()) { ++counters.messages_malformed; return; }
        if (state == SessionState::Playing || state == SessionState::Ended) {
          if (!run.peer_gone) {
            log(run.game_reported ? "the other player left the session" : "the other player left the match");
            run.gone_after_end = run.game_reported;
          }
          run.peer_gone = true;
          if (series.next == NextGame::Waiting || series.next == NextGame::Agreed)
            series_over(reason == kByeMismatch ? "The two games do not agree on the next game's setup" : "The other player left");
        } else if (state != SessionState::Failed && state != SessionState::Idle) {
          fail(reason == kByeMismatch ? "The two games do not agree on the match setup (build, mods, characters, stage or rules differ)"
                                      : "The other player left before the match started");
        }
        return;
      }
      case kMsgResult: {
        const int32_t frames = r.i32();
        Bytes32 theirs{};
        r.bytes(theirs.data(), theirs.size());
        if (!r.done() || frames < 0) { ++counters.messages_malformed; return; }
        run.have_peer_result = true;
        run.peer_result_frames = frames;
        run.peer_result_digest = theirs;
        judge_agreement();
        return;
      }
      case kMsgInfo: {
        const uint8_t minor = r.u8();
        const uint16_t games = r.u16();
        if (opt.minor < 1 || !r.done()) { ++counters.messages_malformed; return; }
        if (series.peer_info) return;   // said once per session; the first stands
        series.peer_info = true;
        series.peer_minor = minor;
        series.peer_games = games;
        return;
      }
      case kMsgNext: {
        const uint16_t next = r.u16();
        Bytes32 theirs{};
        r.bytes(theirs.data(), theirs.size());
        if (opt.minor < 1 || !r.done()) { ++counters.messages_malformed; return; }
        // One per game, and only for the game after the one this side is in. It can come before
        // this side's own game has reported its end; it is judged when this side's NEXT goes out.
        if (series.have_peer_next || series.next == NextGame::Over || state == SessionState::Failed || state == SessionState::Idle) return;
        series.have_peer_next = true;
        series.peer_next_game = next;
        series.peer_next_digest = theirs;
        judge_next();
        return;
      }
      default:
        ++counters.messages_malformed;   // a control message this build does not know
        return;
    }
  }

  void on_packet(uint8_t channel, const uint8_t* data, size_t size, uint64_t now_us) {
    // Refused before any cryptography: nothing, too much, or a channel that does not exist.
    if (!data || size == 0 || size > kMaxPacket || channel >= kChannelCount) { ++counters.packets_rejected; return; }
    if (channel == kChannelControl && SessionSecurity::is_hello(data, size)) {
      const SecurityError error = security.accept_hello(data, size);
      if (error == SecurityError::None) { on_handshake(); return; }
      if (error == SecurityError::Duplicate) return;
      if (error == SecurityError::WrongPeer) run.saw_wrong_key = true;
      if (error == SecurityError::BadVersion) run.saw_other_version = true;
      ++counters.packets_rejected;
      if (run.reject_logs++ < 8) log("hello refused: %s", describe(error));
      return;
    }
    const SecurityError error = security.open(channel, data, size, plain);
    if (error != SecurityError::None) {
      ++counters.packets_rejected;
      // The first few and then every 256th: a flood of bad packets must not become a flood of log.
      if (run.reject_logs++ < 8 || run.reject_logs % 256 == 0)
        log("packet refused (%s, %u bytes, channel %u; %llu so far)", describe(error), (unsigned)size, (unsigned)channel,
            (unsigned long long)counters.packets_rejected);
      return;
    }
    if (channel == kChannelControl) { on_control(now_us); return; }
    answer.clear();
    if (!transport.on_message(plain.data(), plain.size(), now_us, answer)) { ++counters.messages_malformed; return; }
    if (!answer.empty()) send_plain(kChannelInput, answer, false);
  }

  void tick(uint64_t now_us) {
    if (!link || state == SessionState::Idle || state == SessionState::Failed) return;
    link->poll(now_us, [this, now_us](uint8_t channel, const uint8_t* data, size_t size) { on_packet(channel, data, size, now_us); });
    if (!link || state == SessionState::Idle || state == SessionState::Failed) { if (link) link->flush(); return; }
    std::string why;
    switch (state) {
      case SessionState::Connecting:
      case SessionState::Handshaking:
      case SessionState::Verifying:
        if (link->failed(&why)) {
          fail(run.saw_other_version ? "The other player runs a version with a different network protocol"
               : run.saw_wrong_key ? "The player who answered is not the one this match was arranged with" : why);
        } else if (link->connected() && !run.pinned && link->generation() != run.hello_generation) {
          // A path came up (or another one did): the hello goes out on every open path.
          run.hello_generation = link->generation();
          send_hello();
          if (state == SessionState::Connecting) state = SessionState::Handshaking;
        }
        if (state != SessionState::Failed && now_us >= run.deadline_us) {
          if (run.saw_other_version) fail("The other player runs a version with a different network protocol");
          else if (run.saw_wrong_key) fail("The player who answered is not the one this match was arranged with");
          else if (state == SessionState::Connecting)
            fail("The other player could not be reached. A router or firewall on one side blocks direct connections; "
                 "forwarding the game's UDP port on either side usually fixes it");
          else fail("The other game was reached but did not finish setting up the match");
        }
        break;
      case SessionState::Ready:
        if (!link->connected()) { fail("The other player left before the match started"); break; }
        if (now_us >= run.next_ping_us) {
          run.next_ping_us = now_us + kPingEveryUs;
          transport.build_ping(now_us, message);
          send_plain(kChannelInput, message, false);
        }
        break;
      case SessionState::Playing:
      case SessionState::Ended:
        if (!link->connected() && !run.peer_gone) {
          run.peer_gone = true;
          run.gone_after_end = run.game_reported;
          log("the connection to the other player was lost");
        }
        advance_transcript();
        if (series.next == NextGame::Waiting || series.next == NextGame::Agreed) {
          if (run.peer_gone) series_over("The other player left");
          else if (series.next == NextGame::Waiting && now_us >= series.next_deadline_us) series_over("The other game did not answer for a next game");
        }
        break;
      default:
        break;
    }
    if (link) link->flush();
  }

  // ------------------------------------------------------------ match
  void begin_match(uint8_t delay, uint64_t now_us) {
    (void)now_us;
    state = SessionState::Playing;
    run.match_begun = true;
    run.delay = delay;
    if (delay != desc.input_delay) log("the game runs input delay %u, the match setup says %u", (unsigned)delay, (unsigned)desc.input_delay);
    crypto_blake2b_init(&transcript, 32);
    crypto_blake2b_update(&transcript, reinterpret_cast<const uint8_t*>(kTranscriptDomain), sizeof kTranscriptDomain - 1);
    crypto_blake2b_update(&transcript, digest.data(), digest.size());
    // The first `delay` frames have no pad of their own (the pad of frame f travels as f + delay).
    static const uint8_t zero[kPadWireSize] = {};
    for (int f = 1; f <= delay; ++f) transport.commit(f, zero);
    log("match starts: input delay %u, rollback window %d, ping %u ms", (unsigned)delay, kRollbackHorizon, transport.ping_us() / 1000);
  }

  void commit_local(int32_t target, const uint8_t* pad) {
    // The game asks frame by frame, so this is the next frame. A jump would leave frames the peer
    // waits for forever; they are filled with the last pad and said once.
    while (transport.local_latest() + 1 < target) {
      uint8_t last[kPadWireSize];
      transport.local_pad(transport.local_latest(), last);
      if (!run.gap_logged) { run.gap_logged = true; log("the game skipped from frame %d to %d; the gap repeats the last pad", transport.local_latest(), target); }
      transport.commit(transport.local_latest() + 1, last);
    }
    transport.commit(target, pad);   // refused when already committed: the first commit stands
  }

  // The window of the peer's pads the game is shown: the seven oldest frames held from its
  // finalized frame on. The game reads a pad as (latest - frame) into seven entries, and the
  // frames it still has to confirm start right after its finalized frame, so the window is
  // anchored there and NOT at the newest frame received. Anchored at the newest, a burst of eight
  // or more frames arriving at once (the peer catching up after a hitch) pushes the oldest
  // unconfirmed frame out of the seven entries: the game then reads past them, takes a wrong pad
  // as confirmed, and the two games diverge for good.
  int32_t window_base() const {
    const int32_t held = transport.remote_latest();
    if (held < 1) return 0;
    return std::min(std::max(run.floor, 1), held);   // frames before the finalized one are gone; the newest always stays
  }
  int32_t window_latest() const {
    const int32_t held = transport.remote_latest();
    if (held < 1) return 0;
    return std::min(held, window_base() + kRollbackHorizon - 1);
  }

  bool should_skip(int32_t frame, uint64_t now_us) {
    const int32_t latest = window_latest();
    // The stall rule: the game can roll back kRollbackHorizon frames, so it may not run a frame
    // more than that past the newest frame of the window.
    if (latest < frame - kRollbackHorizon) {
      if (run.wait_frames++ == 0) {
        ++counters.waits;
        run.wait_started_us = now_us;
        run.wait_latest = latest;
        log("waiting for the other player's inputs on frame %d (their latest frame %d, ping %u ms)", frame, latest, transport.ping_us() / 1000);
      }
      ++counters.stalls;
      if (now_us - run.wait_started_us > opt.input_limit_us && !run.peer_gone) {
        run.peer_gone = true;
        log("no inputs from the other player for %.1f s (frame %d, their latest %d): the match ends as disconnected",
            (double)opt.input_limit_us / 1e6, frame, latest);
      }
      return true;
    }
    if (run.wait_frames > 0) {
      // Which side stopped: a game that kept running while we waited comes back with at least as
      // many new frames as we skipped; one that had stopped itself comes back with few.
      const int gained = latest - run.wait_latest;
      log("inputs arrived on frame %d after %d skipped frames (%.2f s); their latest frame went %d to %d: %s", frame, run.wait_frames,
          (double)(now_us - run.wait_started_us) / 1e6, run.wait_latest, latest,
          gained >= run.wait_frames ? "their game kept running, the inputs were held up on the way" : "their game fell behind too");
      run.wait_frames = 0;
    }
    if (!opt.time_sync) return false;
    if (frame % kSyncInterval == 0 && !run.shedding) {
      const int32_t offset = transport.time_offset_us();
      counters.time_offset_us = offset;
      // Strict while the match starts (both games should begin in step), then only when this side
      // is more than two frames ahead: shedding a frame is visible, running slower is not.
      const bool opening = frame <= 120;
      if (offset > (opening ? kSyncSlack : kSyncSlack + 2 * opt.game_frame_us)) {
        run.shedding = true;
        run.frames_to_shed = std::min((offset - kSyncSlack) / opt.game_frame_us + 1, opening ? 5 : 1);
        log("holding %d frames on frame %d for time sync (this side is %d us ahead)", run.frames_to_shed, frame, offset);
      }
    }
    if (run.frames_to_shed > 0) { --run.frames_to_shed; ++counters.sync_skips; return true; }
    run.shedding = false;
    return false;
  }

  bool should_advance(int32_t frame) {
    if (!opt.time_sync) return false;
    if (frame % kSyncInterval == 0) {
      const int32_t offset = transport.time_offset_us();
      counters.time_offset_us = offset;
      // Small corrections first: up to 1 % faster when behind, up to 0.5 % slower when ahead,
      // scaled over three frames of offset. The base factor makes the host's tick the game's.
      double deviation = 0.0;
      const double span = 3.0 * (double)opt.game_frame_us;
      if (offset <= -250) deviation = std::min(-offset / span, 1.0) * 0.01;
      else if (offset >= 8000) deviation = std::min(offset / span, 1.0) * -0.005;
      set_speed((double)opt.host_frame_us / (double)opt.game_frame_us * (1.0 + deviation));
      if (offset < -(kSyncSlack + opt.game_frame_us) && !run.advancing) {
        run.advancing = true;
        run.frames_to_advance = std::min((-offset - kSyncSlack) / opt.game_frame_us + 1, frame > 120 ? 3 : 0);
        if (run.frames_to_advance > 0) log("running %d extra frames from frame %d for time sync (this side is %d us behind)", run.frames_to_advance, frame, -offset);
      }
    }
    if (run.frames_to_advance > 0) {
      if (frame % 5 != 0) return false;   // spread out, one extra frame every five
      --run.frames_to_advance;
      ++counters.advances;
      return true;
    }
    run.advancing = false;
    return false;
  }

  void compare_checksums() {
    const int32_t frame = transport.remote_checksum_frame();
    const uint32_t theirs = transport.remote_checksum();
    if (frame <= run.last_compared || theirs == 0) return;
    const auto it = checksums.find(frame);
    if (it == checksums.end()) return;
    run.last_compared = frame;
    ++counters.checksums_compared;
    const uint32_t ours = it->second;
    if (ours == theirs) {
      if (counters.checksums_compared % 20 == 0)
        log("checksums agree through frame %d (%llu compared, %llu mismatched)", frame, (unsigned long long)counters.checksums_compared,
            (unsigned long long)counters.checksum_mismatches);
      return;
    }
    ++counters.checksum_mismatches;
    // The same judgment the game makes on these values: the low half may differ by one count
    // (a value both games round on different sides of); anything more is a real divergence.
    const int diff = (int)(int16_t)(ours & 0xFFFF) - (int)(int16_t)(theirs & 0xFFFF);
    if (diff < -1 || diff > 1) {
      if (!counters.desync) log("DESYNC: checksum mismatch at frame %d (ours %08X, theirs %08X)", frame, ours, theirs);
      counters.desync = true;
    } else if (!run.risk_logged) {
      run.risk_logged = true;
      log("desync risk at frame %d (ours %08X, theirs %08X)", frame, ours, theirs);
    }
  }

  void advance_transcript() {
    if (!run.match_begun) return;
    while (run.transcript_ok) {
      const int32_t f = run.transcript_frames + 1;
      if (f > transport.local_latest() || f > transport.remote_latest()) break;
      uint8_t ours[kPadWireSize], theirs[kPadWireSize];
      if (!transport.local_pad(f, ours) || !transport.remote_pad(f, theirs)) {
        run.transcript_ok = false;
        log("input transcript stops at frame %d: the pads were no longer held", f - 1);
        break;
      }
      uint8_t number[4] = {(uint8_t)f, (uint8_t)(f >> 8), (uint8_t)(f >> 16), (uint8_t)(f >> 24)};
      crypto_blake2b_update(&transcript, number, 4);
      crypto_blake2b_update(&transcript, local_slot < remote_slot ? ours : theirs, kPadWireSize);   // slot 0 first on both sides
      crypto_blake2b_update(&transcript, local_slot < remote_slot ? theirs : ours, kPadWireSize);
      run.transcript_frames = f;
      Mark& mark = marks[f % kTranscriptMarks];
      crypto_blake2b_ctx copy = transcript;
      crypto_blake2b_final(&copy, mark.digest.data());
      mark.frame = f;
    }
    counters.transcript_frames = run.transcript_frames;
    if (run.have_peer_result && run.agreement < 0) judge_agreement();
  }

  bool transcript_at(int32_t frames, Bytes32& out) const {
    if (frames == 0) {
      crypto_blake2b_ctx copy;
      crypto_blake2b_init(&copy, 32);
      crypto_blake2b_update(&copy, reinterpret_cast<const uint8_t*>(kTranscriptDomain), sizeof kTranscriptDomain - 1);
      crypto_blake2b_update(&copy, digest.data(), digest.size());
      crypto_blake2b_final(&copy, out.data());
      return true;
    }
    if (frames < 0) return false;
    const Mark& mark = marks[frames % kTranscriptMarks];
    if (mark.frame != frames) return false;
    out = mark.digest;
    return true;
  }

  // The two games end with slightly different confirmed frame counts (the last inputs in flight).
  // The side that has reached the other's count can compare; the other side learns nothing here
  // and its file says "unknown".
  void judge_agreement() {
    Bytes32 ours{};
    if (!run.have_peer_result || !transcript_at(run.peer_result_frames, ours)) return;
    run.agreement = crypto_verify32(ours.data(), run.peer_result_digest.data()) == 0 ? 1 : 0;
    if (run.agreement == 0) log("the two games disagree on the inputs through frame %d", run.peer_result_frames);
  }

  void build_reply(int32_t frame, uint8_t result, std::vector<uint8_t>& reply) {
    reply.assign(kInputsReplySize, 0);
    uint8_t* b = reply.data();
    b[kRxResult] = result;
    b[kRxRemoteCount] = 1;
    put32(b + kRxChecksums, (uint32_t)transport.remote_checksum_frame());
    put32(b + kRxChecksums + 4, transport.remote_checksum());
    // Never ahead of the frame asked for: the game indexes the pads below by (latest - frame).
    const int32_t base = window_base();
    const int32_t latest = std::min(window_latest(), frame);
    put32(b + kRxLatest, (uint32_t)latest);
    put32(b + kRxLatest + 4, (uint32_t)frame);   // the two unused remotes never hold the game back
    put32(b + kRxLatest + 8, (uint32_t)frame);
    put32(b + kRxSmallestLatest, (uint32_t)std::min(latest, frame));
    // Entry k is the pad of frame (latest - k). Frames outside the window are zero.
    for (int k = 0; k < kRxInputFrames; ++k) {
      const int32_t f = latest - k;
      if (base < 1 || f < base) break;
      uint8_t pad[kPadWireSize];
      transport.remote_pad(f, pad);
      std::memcpy(b + kRxInputs + k * kPadGameSize, pad, kPadWireSize);
    }
    counters.local_frame = transport.local_latest();
    counters.remote_frame = transport.remote_latest();
  }

  void inputs(int32_t frame, int32_t finalized, uint32_t checksum, uint8_t delay, const uint8_t* pad, std::vector<uint8_t>& reply) {
    const uint64_t now_us = now();
    tick(now_us);
    if (!playable() || !pad) { build_reply(frame, kInputsDisconnected, reply); return; }
    delay = (uint8_t)std::max(kMinInputDelay, std::min<int>(delay, kMaxInputDelay));
    if (!run.match_begun) {
      begin_match(delay, now_us);
    } else if (frame == 1 && run.last_frame > 1) {
      // The transcript, the checksums and the peer's frames all count from this game's frame 1. A
      // further game has to be agreed with the peer first (NEXT, then begin_next_game()).
      log("the game asked for frame 1 again after frame %d; a new game has to be agreed with the other side first", run.last_frame);
      build_reply(frame, kInputsDisconnected, reply);
      return;
    }
    run.last_frame = frame;
    if (run.peer_gone) { build_reply(frame, kInputsDisconnected, reply); return; }

    if (finalized > 0 && checksum != 0) {
      checksums[finalized] = checksum;
      while (checksums.size() > kMaxChecksums) checksums.erase(checksums.begin());
    }
    transport.set_floor(finalized);
    if (finalized > run.floor) run.floor = finalized;

    const bool skip = should_skip(frame, now_us);
    if (!skip) {
      // The pad sampled on frame F is applied by this game on frame F + delay (its delay buffer),
      // so it is committed as frame F + delay and the peer applies it on that same frame. The
      // finalized frame and its checksum travel with it, as given in this very call.
      commit_local(frame + run.delay, pad);
      transport.set_checksum(finalized, checksum);
    }
    // Sent on a skipped tick as well: the unacknowledged frames again, and the heartbeat.
    transport.build_commit(now_us, message);
    if (!message.empty()) send_plain(kChannelInput, message, false);
    advance_transcript();
    compare_checksums();

    uint8_t result = kInputsNormal;
    if (run.peer_gone) result = kInputsDisconnected;
    else if (skip) result = kInputsSkip;
    else if (should_advance(frame)) result = kInputsAdvance;
    build_reply(frame, result, reply);
    counters.ping_us = transport.ping_us();
    if (link) link->flush();
  }

  void fill_result(GameResult& out) const {
    out = game;
    out.route = desc.route;
    out.build_id = desc.build_id;
    out.descriptor_digest = digest;
    out.match_id = desc.match_id;
    out.local_slot = (uint8_t)local_slot;
    out.peer_identity = desc.players[remote_slot].identity_key;
    out.transcript_frames = run.transcript_frames;
    out.transcript_complete = run.transcript_ok;
    transcript_at(run.transcript_frames, out.transcript_digest);
    out.peer_agreement = run.agreement;
    out.desync = counters.desync;
    out.disconnected = run.peer_gone && !run.gone_after_end;   // a peer that left after the game's end did not cut the game short
    if (!out.has_outcome) out.game_index = series.game;
  }
};

RollbackSession::RollbackSession() : impl_(new Impl) {}
RollbackSession::~RollbackSession() { stop(); }

void RollbackSession::configure(const SessionCallbacks& callbacks, const SessionOptions& options) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->cb = callbacks;
  impl_->opt = options;
  if (impl_->opt.game_frame_us <= 0) impl_->opt.game_frame_us = 16683;
  if (impl_->opt.host_frame_us <= 0) impl_->opt.host_frame_us = impl_->opt.game_frame_us;
}

void RollbackSession::set_identity(const PeerIdentity& identity) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->identity = identity;
}

void RollbackSession::set_session_secret(const Bytes32& x25519_secret) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->has_secret = true;
  impl_->session_secret = x25519_secret;
}

bool RollbackSession::start(const SessionDescriptor& descriptor, int local_slot, const EndpointInfo& endpoint) {
  stop();
  try {
    {
      std::lock_guard<std::mutex> lock(impl_->mu);
      auto& s = *impl_;
      s.connector.reset(new PeerConnector);
      s.connector->set_limit_us(s.opt.connect_limit_us);
      if (!s.start_common(descriptor, local_slot, s.connector.get(), endpoint.has_expected_peer ? &endpoint.expected_peer : nullptr)) {
        s.link = nullptr; s.connector.reset();
        return false;
      }
      std::string why;
      if (!s.connector->open(endpoint.local_port, &why) || !s.connector->connect(endpoint.candidates, s.now(), &why)) {
        s.fail(why);
        s.link = nullptr; s.connector.reset();
        return false;
      }
      s.log("UDP port %u open, %u candidate address%s", (unsigned)s.connector->local_port(), (unsigned)endpoint.candidates.size(),
            endpoint.candidates.size() == 1 ? "" : "es");
    }
    if (impl_->opt.pump_thread) {
      impl_->pump_quit.store(false);
      Impl* s = impl_.get();
      s->pump = std::thread([s] {
        while (!s->pump_quit.load(std::memory_order_relaxed)) {
          {
            std::lock_guard<std::mutex> lock(s->mu);
            try { s->tick(s->now()); } catch (...) {}
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      });
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool RollbackSession::start(const SessionDescriptor& descriptor, int local_slot, Link& link, const Bytes32* expected_peer) {
  stop();
  try {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (!impl_->start_common(descriptor, local_slot, &link, expected_peer)) { impl_->link = nullptr; return false; }
    return true;
  } catch (...) {
    return false;
  }
}

void RollbackSession::tick() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  try { impl_->tick(impl_->now()); } catch (...) {}
}

void RollbackSession::on_inputs(int32_t frame, int32_t finalized, uint32_t finalized_checksum, uint8_t delay,
                                const uint8_t pad[kPadGameSize], std::vector<uint8_t>& reply) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  try {
    impl_->inputs(frame, finalized, finalized_checksum, delay, pad, reply);
  } catch (...) {
    // Whatever went wrong, the game gets a well-formed answer that ends the match cleanly.
    try { reply.assign(kInputsReplySize, 0); reply[0] = kInputsDisconnected; reply[1] = 1; } catch (...) {}
  }
}

void RollbackSession::on_inputs(const uint8_t* payload, size_t size, std::vector<uint8_t>& reply) {
  if (!payload || size < kInputsPayloadSize) {
    on_inputs(0, 0, 0, 0, nullptr, reply);
    return;
  }
  on_inputs((int32_t)get_be32(payload), (int32_t)get_be32(payload + 4), get_be32(payload + 8), payload[12], payload + 13, reply);
}

void RollbackSession::match_state_reply(std::vector<uint8_t>& reply) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  try {
    auto& s = *impl_;
    s.tick(s.now());
    reply.assign(kMatchStateReplySize, 0);
    uint8_t* b = reply.data();
    const bool up = s.playable();
    b[kMsState] = (uint8_t)(s.state == SessionState::Failed ? kMsStateError : up ? kMsStateConnected
                            : s.state == SessionState::Idle ? kMsStateIdle : kMsStateConnecting);
    // Ready is said for exactly as long as a match may start: not after a game's end, and again
    // only once the next game was agreed and begun (begin_next_game).
    const bool ready = s.state == SessionState::Ready || s.state == SessionState::Playing;
    b[kMsLocalReady] = ready;
    b[kMsRemoteReady] = ready;
    b[kMsLocalIndex] = (uint8_t)s.local_slot;
    b[kMsRemoteIndex] = (uint8_t)s.remote_slot;
    put32(b + kMsRng, s.desc.rng_seed);
    b[kMsDelay] = s.desc.input_delay;
    game_text(s.desc.players[s.local_slot].name, 15, b + kMsLocalName, kMsNameSize);
    for (int i = 0; i < kMaxSlots; ++i) game_text(s.desc.players[i].name, 15, b + kMsPlayerNames + i * kMsNameSize, kMsNameSize);
    game_text(s.desc.players[s.remote_slot].name, 15, b + kMsOpponentName, kMsNameSize);
    if (s.state == SessionState::Failed) game_text(s.failure, 120, b + kMsError, kMsErrorSize);
    build_game_info(s.desc, b + kMsGameInfo);
    const std::string id = "mu.p2p-" + to_hex(s.desc.match_id.data(), s.desc.match_id.size());
    std::memcpy(b + kMsMatchId, id.data(), std::min<size_t>(id.size(), kMsMatchIdSize - 1));
    b[kMsAltStage] = s.desc.alt_stage;
    // The game asks for this in the menus, never during a match: full speed there.
    if (s.state != SessionState::Playing) s.set_speed(1.0);
  } catch (...) {
    try { reply.assign(kMatchStateReplySize, 0); reply[kMsState] = kMsStateError; } catch (...) {}
  }
}

void RollbackSession::on_game_end(const uint8_t* payload, size_t size) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  try {
    auto& s = *impl_;
    s.tick(s.now());
    if (!s.run.match_begun) { s.log("a game end was reported with no match running"); return; }
    if (s.run.game_reported) return;   // the game may say it twice; the first report stands
    if (!parse_game_end(payload, size, s.game)) s.log("the game end report was %u bytes, %u expected; the outcome is not recorded", (unsigned)size, (unsigned)kGameEndPayloadSize);
    s.run.game_reported = true;
    s.advance_transcript();
    Bytes32 mine{};
    s.transcript_at(s.run.transcript_frames, mine);
    s.message.clear();
    Writer w(s.message);
    w.u8(kMsgResult);
    w.i32(s.run.transcript_frames);
    w.bytes(mine.data(), mine.size());
    s.send_plain(kChannelControl, s.message, true);
    if (s.link) s.link->flush();
    if (s.state == SessionState::Playing) s.state = SessionState::Ended;
    s.set_speed(1.0);
    const auto& c = s.counters;
    s.log("game over: %d frames of inputs confirmed, %llu waits, %llu frames stalled, %llu shed, %llu advanced, ping %u ms, %llu checksums compared, %llu mismatched%s",
          s.run.transcript_frames, (unsigned long long)c.waits, (unsigned long long)c.stalls, (unsigned long long)c.sync_skips,
          (unsigned long long)c.advances, s.transport.ping_us() / 1000, (unsigned long long)c.checksums_compared,
          (unsigned long long)c.checksum_mismatches, c.desync ? ", DESYNC" : "");
    s.offer_next(s.now());
    if (s.link) s.link->flush();
  } catch (...) {}
}

NextGame RollbackSession::next_game(std::string* why) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (why) *why = impl_->series.next == NextGame::Over ? impl_->series.over_why : std::string();
  return impl_->series.next;
}

bool RollbackSession::begin_next_game() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  try { return impl_->begin_next(impl_->now()); } catch (...) { return false; }
}

uint32_t RollbackSession::game_index() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->series.game;
}

void RollbackSession::stop() {
  // The thread is joined before the lock is taken: it takes the lock itself every millisecond.
  impl_->pump_quit.store(true);
  if (impl_->pump.joinable()) impl_->pump.join();
  std::lock_guard<std::mutex> lock(impl_->mu);
  try { impl_->teardown(); } catch (...) {}
}

SessionState RollbackSession::state() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->state;
}

std::string RollbackSession::failure_text() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->failure;
}

SessionCounters RollbackSession::counters() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  SessionCounters c = impl_->counters;
  c.ping_us = impl_->transport.ping_us();
  c.time_offset_us = impl_->transport.time_offset_us();
  c.local_frame = impl_->transport.local_latest();
  c.remote_frame = impl_->transport.remote_latest();
  c.transcript_frames = impl_->run.transcript_frames;
  c.peer_gone = impl_->run.peer_gone;
  c.transport = impl_->transport.stats();
  return c;
}

uint16_t RollbackSession::local_port() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->connector ? impl_->connector->local_port() : 0;
}

Bytes32 RollbackSession::descriptor_digest() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->digest;
}

SessionDescriptor RollbackSession::descriptor() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->desc;
}

bool RollbackSession::result(GameResult& out) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (!impl_->run.match_begun) return false;
  try { impl_->fill_result(out); } catch (...) { return false; }
  return true;
}

bool RollbackSession::write_result_file(const std::string& path_utf8, std::string* error) const {
  GameResult record;
  PeerIdentity identity;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (!impl_->run.match_begun) { if (error) *error = "No match was played in this session"; return false; }
    try { impl_->fill_result(record); } catch (...) { if (error) *error = "The result could not be assembled"; return false; }
    identity = impl_->identity;
  }
  return write_result(path_utf8, record, identity, error);   // file work outside the lock
}

}  // namespace mu_net
