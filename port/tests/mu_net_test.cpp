// In-process tests of the mu_net session subsystem: handshake, packet rejection, lossy 2,000-frame exchange, stall rule, desync and descriptor checks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_transport.h"
#include "nat_discovery.h"
#include "peer_connector.h"
#include "peer_identity.h"
#include "result_outbox.h"
#include "rollback_session.h"
#include "session_security.h"
#include "session_types.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <enet/enet.h>   // a plain UDP socket for test_connect_diagnosis

using namespace mu_net;

static int g_failures = 0;
#define EXPECT(condition) \
  do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

// ---------------------------------------------------------------- fixtures
// Two queues in memory. Everything sent is delivered, in order, on the receiver's next poll; the
// faults come from FaultLink around it.
struct Wire {
  std::deque<std::pair<uint8_t, std::vector<uint8_t>>> to_a, to_b;
  bool up = true;
};
class MemoryLink : public Link {
 public:
  MemoryLink(Wire& wire, bool is_a) : wire_(wire), is_a_(is_a) {}
  bool send(uint8_t channel, const uint8_t* data, size_t size, bool) override {
    if (!wire_.up) return false;
    (is_a_ ? wire_.to_b : wire_.to_a).emplace_back(channel, std::vector<uint8_t>(data, data + size));
    return true;
  }
  void poll(uint64_t, const Receiver& on_packet) override {
    std::deque<std::pair<uint8_t, std::vector<uint8_t>>> batch;
    batch.swap(is_a_ ? wire_.to_a : wire_.to_b);   // the receiver sends while it is handed packets
    for (auto& p : batch) on_packet(p.first, p.second.data(), p.second.size());
  }
  bool connected() const override { return wire_.up; }

 private:
  Wire& wire_;
  bool is_a_;
};

struct Clock { uint64_t us = 5000000; };

static PeerIdentity test_identity(uint8_t fill) {
  Bytes32 seed;
  seed.fill(fill);
  return identity_from_seed(seed);
}

static SessionDescriptor test_descriptor() {
  SessionDescriptor d;
  d.route = Route::P2PDirect;
  d.build_id = "test-build-1";
  d.content_hash.fill(0x42);
  d.match_id.fill(0x07);
  d.players[0].present = true; d.players[0].character = 0x02; d.players[0].color = 1; d.players[0].name = "Left";
  d.players[1].present = true; d.players[1].character = 0x02; d.players[1].color = 1; d.players[1].name = "Right!";
  d.stage = 0x20;
  d.rng_seed = 0x12345678;
  d.input_delay = 2;
  d.features = kFeaturePauseAllowed;
  return d;
}

struct Peer {
  RollbackSession session;
  std::vector<std::string> log;
  double speed = 1.0;
  // Set before setup(): the games this side plays in one session, its stage pool, its revision.
  uint32_t max_games = 1;
  std::vector<uint16_t> pool;
  uint8_t minor = kProtocolMinor;
  uint64_t next_limit_us = 20 * 1000000;
  void setup(Clock& clock, uint8_t identity_fill, bool time_sync = false, uint64_t connect_limit_us = 2000000) {
    SessionCallbacks cb;
    cb.log = [this](const char* line) { log.push_back(line); };
    cb.now_us = [&clock] { return clock.us; };
    cb.set_emulation_speed = [this](double s) { speed = s; };
    SessionOptions opt;
    opt.pump_thread = false;
    opt.time_sync = time_sync;
    opt.connect_limit_us = connect_limit_us;
    opt.max_games = max_games;
    opt.stage_pool = pool;
    opt.minor = minor;
    opt.next_limit_us = next_limit_us;
    session.configure(cb, opt);
    session.set_identity(test_identity(identity_fill));
  }
  bool logged(const char* text) const {
    for (const auto& line : log) if (line.find(text) != std::string::npos) return true;
    return false;
  }
};

static bool settle(Peer& a, Peer& b, Clock& clock, SessionState want = SessionState::Ready, int rounds = 400) {
  for (int i = 0; i < rounds; ++i) {
    a.session.tick();
    b.session.tick();
    clock.us += 1000;
    if (a.session.state() == want && b.session.state() == want) return true;
  }
  return false;
}

// The pad the script gives `slot` on game frame `frame` (8 wire bytes, 4 zero bytes).
static void script_pad(int slot, int frame, uint8_t pad[kPadGameSize]) {
  std::memset(pad, 0, kPadGameSize);
  for (int i = 0; i < kPadWireSize; ++i) pad[i] = (uint8_t)(frame * (31 + slot * 7) + i * 13 + slot + (frame >> 8));
}
// What the peer must be handed for commit frame `commit` of `slot` (the pad of frame f travels as f + delay).
static void expected_pad(int slot, int commit, int delay, uint8_t pad[kPadWireSize]) {
  uint8_t full[kPadGameSize] = {};
  if (commit > delay) script_pad(slot, commit - delay, full);
  std::memcpy(pad, full, kPadWireSize);
}

// The part of the game this subsystem talks to: asks for inputs frame by frame, repeats a frame
// that was answered "skip", and remembers the newest frame it has the peer's inputs for.
struct FakeGame {
  RollbackSession* session = nullptr;
  int slot = 0, delay = 2;
  int frame = 1, finalized = 0;
  uint32_t checksum_bias = 0;
  int skips = 0, last_result = 0;
  uint32_t checksum_of(int f) const { return 0x00010000u + (uint32_t)f * 7u + checksum_bias; }
  int step() {
    uint8_t pad[kPadGameSize];
    script_pad(slot, frame, pad);
    std::vector<uint8_t> reply;
    const int sent_finalized = finalized;
    session->on_inputs(frame, finalized, finalized > 0 ? checksum_of(finalized) : 0, (uint8_t)delay, pad, reply);
    EXPECT(reply.size() == kInputsReplySize);
    if (reply.size() != kInputsReplySize) return 3;
    last_result = reply[0];
    EXPECT(reply[1] == 1);
    const int latest = (int)get_be32(&reply[26]);
    EXPECT(latest <= frame);
    EXPECT(latest <= (sent_finalized > 1 ? sent_finalized : 1) + 6);   // the window starts at the finalized frame
    EXPECT((int)get_be32(&reply[38]) == latest);
    for (int k = 0; k < 7; ++k) {
      const int f = latest - k;
      if (f < 1 || f < sent_finalized) break;   // older than the window: not served
      uint8_t want[kPadWireSize];
      expected_pad(1 - slot, f, delay, want);
      EXPECT(std::memcmp(&reply[42 + k * 12], want, kPadWireSize) == 0);   // newest first, never changed
      static const uint8_t zero[4] = {};
      EXPECT(std::memcmp(&reply[42 + k * 12 + 8], zero, 4) == 0);
    }
    if (latest > finalized) finalized = latest < frame ? latest : frame;
    if (last_result == kInputsSkip) { ++skips; return last_result; }
    if (last_result == kInputsDisconnected) return last_result;
    ++frame;
    return last_result;
  }
};

// ---------------------------------------------------------------- tests
static void test_descriptor_digest() {
  SessionDescriptor d = test_descriptor();
  std::string why;
  EXPECT(validate(d, &why));
  std::vector<uint8_t> bytes;
  serialize(d, bytes);
  SessionDescriptor back;
  EXPECT(parse(bytes.data(), bytes.size(), back));
  EXPECT(digest(back) == digest(d));
  EXPECT(!parse(bytes.data(), bytes.size() - 1, back));
  bytes.push_back(0);
  EXPECT(!parse(bytes.data(), bytes.size(), back));
  SessionDescriptor other = d;
  other.stage = 0x1F;
  EXPECT(digest(other) != digest(d));
  other = d;
  other.players[1].name = "Right?";
  EXPECT(digest(other) != digest(d));
  other = d;
  other.features |= kFeatureSubframeSamples;   // reserved: refused, not ignored
  EXPECT(!validate(other, &why));
  other = d;
  other.rollback_horizon = 8;
  EXPECT(!validate(other, &why));
  other = d;
  other.protocol_version = kProtocolVersion + 1;
  EXPECT(!validate(other, &why));
}

static void test_security_packets() {
  const PeerIdentity ia = test_identity(1), ib = test_identity(2), ic = test_identity(3);
  SessionSecurity a, b;
  EXPECT(a.begin(ia, nullptr));
  EXPECT(b.begin(ib, &ia.public_key));
  EXPECT(a.hello().size() == kHelloSize);

  // Damaged hellos: short, long, a flipped bit anywhere, another version, our own.
  std::vector<uint8_t> hello = a.hello();
  EXPECT(b.accept_hello(hello.data(), hello.size() - 1) == SecurityError::TooShort);
  EXPECT(b.accept_hello(hello.data(), 3) == SecurityError::TooShort);
  hello.push_back(0);
  EXPECT(b.accept_hello(hello.data(), hello.size()) == SecurityError::TooLong);
  hello.pop_back();
  for (size_t at : {8u, 39u, 40u, 71u, 72u, 95u, 96u, 159u}) {
    std::vector<uint8_t> bad = hello;
    bad[at] ^= 0x10;
    EXPECT(b.accept_hello(bad.data(), bad.size()) != SecurityError::None);
    EXPECT(!b.established());
  }
  {
    std::vector<uint8_t> bad = hello;
    bad[4] = 2;
    EXPECT(b.accept_hello(bad.data(), bad.size()) == SecurityError::BadVersion);
    EXPECT(a.accept_hello(hello.data(), hello.size()) == SecurityError::Reflected);
  }
  // A hello signed by somebody else than the expected identity.
  {
    SessionSecurity c;
    EXPECT(c.begin(ic, nullptr));
    EXPECT(b.accept_hello(c.hello().data(), c.hello().size()) == SecurityError::WrongPeer);
    EXPECT(!b.established());
  }
  std::vector<uint8_t> packet, plain;
  const uint8_t text[] = {1, 2, 3, 4, 5};
  EXPECT(!a.seal(kChannelInput, text, sizeof text, packet));   // nothing is protected before the handshake
  EXPECT(b.open(kChannelInput, hello.data(), hello.size(), plain) != SecurityError::None);

  EXPECT(b.accept_hello(hello.data(), hello.size()) == SecurityError::None);
  EXPECT(a.accept_hello(b.hello().data(), b.hello().size()) == SecurityError::None);
  EXPECT(a.established() && b.established());
  EXPECT(a.decider() != b.decider());
  EXPECT(a.peer_identity() == ib.public_key && b.peer_identity() == ia.public_key);
  EXPECT(b.accept_hello(hello.data(), hello.size()) == SecurityError::Duplicate);

  // Good packet, then the same packet again.
  EXPECT(a.seal(kChannelInput, text, sizeof text, packet));
  EXPECT(packet.size() == kPacketHeader + sizeof text + kPacketMac);
  EXPECT(b.open(kChannelInput, packet.data(), packet.size(), plain) == SecurityError::None);
  EXPECT(plain.size() == sizeof text && std::memcmp(plain.data(), text, sizeof text) == 0);
  EXPECT(b.open(kChannelInput, packet.data(), packet.size(), plain) == SecurityError::Replay);
  EXPECT(a.open(kChannelInput, packet.data(), packet.size(), plain) == SecurityError::BadMac);   // our own packet, turned around

  // Truncated at every length, and every single bit flipped: never accepted, never a crash.
  EXPECT(a.seal(kChannelInput, text, sizeof text, packet));
  for (size_t n = 0; n < packet.size(); ++n) EXPECT(b.open(kChannelInput, packet.data(), n, plain) != SecurityError::None);
  for (size_t at = 0; at < packet.size(); ++at) {
    for (int bit = 0; bit < 8; ++bit) {
      std::vector<uint8_t> bad = packet;
      bad[at] ^= (uint8_t)(1 << bit);
      EXPECT(b.open(kChannelInput, bad.data(), bad.size(), plain) != SecurityError::None);
    }
  }
  EXPECT(b.open(kChannelControl, packet.data(), packet.size(), plain) == SecurityError::BadChannel);
  EXPECT(b.open(kChannelInput, packet.data(), packet.size(), plain) == SecurityError::None);   // the untouched one still opens

  // Oversized: refused before decrypting, whatever it holds.
  std::vector<uint8_t> big(kMaxPacket + 1, 0);
  big[0] = kPacketVersion; big[1] = kChannelInput;
  EXPECT(b.open(kChannelInput, big.data(), big.size(), plain) == SecurityError::TooLong);
  std::vector<uint8_t> too_much(kMaxPlaintext + 1, 0);
  EXPECT(!a.seal(kChannelInput, too_much.data(), too_much.size(), packet));
  std::vector<uint8_t> most(kMaxPlaintext, 7);
  EXPECT(a.seal(kChannelInput, most.data(), most.size(), packet) && packet.size() == kMaxPacket);
  EXPECT(b.open(kChannelInput, packet.data(), packet.size(), plain) == SecurityError::None && plain == most);

  // Reordering inside the window is fine, each packet once; older than the window is refused.
  std::vector<std::vector<uint8_t>> burst(kReplayWindow + 6);
  for (auto& p : burst) EXPECT(a.seal(kChannelInput, text, sizeof text, p));
  EXPECT(b.open(kChannelInput, burst[3].data(), burst[3].size(), plain) == SecurityError::None);
  EXPECT(b.open(kChannelInput, burst[1].data(), burst[1].size(), plain) == SecurityError::None);
  EXPECT(b.open(kChannelInput, burst[1].data(), burst[1].size(), plain) == SecurityError::Replay);
  EXPECT(b.open(kChannelInput, burst.back().data(), burst.back().size(), plain) == SecurityError::None);
  EXPECT(b.open(kChannelInput, burst[2].data(), burst[2].size(), plain) == SecurityError::Replay);   // fell out of the window
  EXPECT(b.open(kChannelInput, burst[10].data(), burst[10].size(), plain) == SecurityError::None);

  // The control channel is ordered: an older sequence is a replay even when never seen.
  std::vector<uint8_t> c1, c2;
  EXPECT(a.seal(kChannelControl, text, sizeof text, c1) && a.seal(kChannelControl, text, sizeof text, c2));
  EXPECT(b.open(kChannelControl, c2.data(), c2.size(), plain) == SecurityError::None);
  EXPECT(b.open(kChannelControl, c1.data(), c1.size(), plain) == SecurityError::Replay);
}

static void test_transport_parsing() {
  InputTransport a, b;
  a.reset(16683);
  b.reset(16683);
  std::vector<uint8_t> message, reply;
  uint8_t pad[kPadWireSize] = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT(!a.commit(2, pad));   // frames start at 1 and never skip
  EXPECT(a.commit(1, pad) && a.commit(2, pad) && !a.commit(2, pad));
  a.build_commit(1000, message);
  EXPECT(message.size() == kCommitHeader + 2 * kPadWireSize);
  EXPECT(b.on_message(message.data(), message.size(), 2000, reply));
  EXPECT(b.remote_latest() == 2 && reply.size() == 9 && reply[0] == kMsgAck);
  EXPECT(a.on_message(reply.data(), reply.size(), 3000, message));
  EXPECT(a.peer_ack() == 2 && a.has_ping() && a.ping_us() == 2000);

  // Malformed COMMITs: every one refused, none stored.
  a.build_commit(4000, message);
  const std::vector<uint8_t> good = message;
  auto refused = [&](std::vector<uint8_t> bytes) {
    const int before = b.remote_latest();
    const bool ok = b.on_message(bytes.data(), bytes.size(), 5000, reply);
    return !ok && b.remote_latest() == before && reply.empty();
  };
  { auto m = good; m.pop_back(); EXPECT(refused(m)); }
  { auto m = good; m.push_back(0); EXPECT(refused(m)); }
  { auto m = good; m[5] = 0; EXPECT(refused(m)); }                       // count 0
  { auto m = good; m[5] = 200; EXPECT(refused(m)); }                     // count over the cap
  { auto m = good; m[5] = 9; m.resize(kCommitHeader + 9 * kPadWireSize); EXPECT(refused(m)); }   // history before frame 1
  { auto m = good; m[14] = 99; EXPECT(refused(m)); }                     // acknowledges a frame never sent
  { std::vector<uint8_t> m = {0x7E, 1, 2, 3}; EXPECT(refused(m)); }      // unknown type
  { std::vector<uint8_t> m = {kMsgAck, 1}; EXPECT(refused(m)); }
  EXPECT(refused(std::vector<uint8_t>(5000, kMsgCommit)));
  EXPECT(b.stats().malformed >= 9);

  // SAMPLE is reserved: the right length is accepted and changes nothing, the wrong one is refused.
  std::vector<uint8_t> sample(kSampleSize, 0);
  sample[0] = kMsgSample;
  EXPECT(b.on_message(sample.data(), sample.size(), 6000, reply) && reply.empty());
  EXPECT(b.stats().samples_ignored == 1 && b.remote_latest() == 2);
  sample.push_back(0);
  EXPECT(refused(sample));

  // A frame committed again with other bytes: counted, and the first commit stands.
  auto changed = good;
  changed[kCommitHeader] ^= 0xFF;
  EXPECT(b.on_message(changed.data(), changed.size(), 7000, reply));
  EXPECT(b.stats().conflicts == 1);
  uint8_t held[kPadWireSize];
  EXPECT(b.remote_pad(2, held) && std::memcmp(held, pad, kPadWireSize) == 0);

  // A history that does not reach what the receiver holds is not taken.
  InputTransport c;
  c.reset(16683);
  for (int f = 1; f <= 5; ++f) EXPECT(c.commit(f, pad));
  c.build_commit(100, message);
  EXPECT(b.on_message(message.data(), message.size(), 8000, reply) && b.remote_latest() == 5);
  std::vector<uint8_t> ack;
  { Writer w(ack); w.u8(kMsgAck); w.i32(5); w.u32(100); }
  EXPECT(c.on_message(ack.data(), ack.size(), 200, reply));
  for (int f = 6; f <= 8; ++f) EXPECT(c.commit(f, pad));
  c.build_commit(300, message);
  EXPECT(message.size() == kCommitHeader + 3 * kPadWireSize);   // only the unacknowledged frames
  InputTransport fresh;
  fresh.reset(16683);
  EXPECT(fresh.on_message(message.data(), message.size(), 400, reply) && fresh.remote_latest() == 0 && fresh.stats().gaps == 1);
}

static void test_handshake_and_match_state() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  const Bytes32 b_key = test_identity(2).public_key;
  EXPECT(a.session.start(d, 0, la, &b_key));
  EXPECT(b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  EXPECT(a.session.descriptor_digest() == b.session.descriptor_digest());
  EXPECT(a.session.descriptor_digest() != digest(d));   // the handshake's keys are part of it
  EXPECT(a.session.descriptor().players[1].identity_key == b_key);

  std::vector<uint8_t> ms;
  a.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize);
  if (ms.size() == kMatchStateReplySize) {
    EXPECT(ms[0] == 4 && ms[1] == 1 && ms[2] == 1 && ms[3] == 0 && ms[4] == 1);
    EXPECT(get_be32(&ms[5]) == 0x12345678 && ms[9] == 2);
    EXPECT(ms[15] == 'L' && ms[170] == 'R');
    EXPECT(ms[170 + 5] == 0x81 && ms[170 + 6] == 0x49);   // '!' as the menu font's full-width form
    const uint8_t* info = &ms[598];
    EXPECT(info[0x0E] == 0x00 && info[0x0F] == 0x20);
    EXPECT(get_be32(info + 0x10) == 480);
    EXPECT((info[0x02] & 0x08) == 0);                      // pause allowed by the descriptor
    EXPECT(info[0x60] == 0x02 && info[0x61] == 0 && info[0x62] == 4 && info[0x63] == 1 && info[0x67] == 0);
    EXPECT(info[0x84] == 0x02 && info[0x84 + 7] == 1);     // same character and color: the next shade
    EXPECT(info[0x60 + 2 * 0x24 + 1] == 3 && info[0x60 + 3 * 0x24 + 1] == 3);
    EXPECT(ms[961] == 0);
  }
  b.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 4 && ms[3] == 1 && ms[4] == 0);

  // Garbage on both channels, of every size class, straight onto the wire: refused and survived.
  uint32_t rng = 12345;
  for (int i = 0; i < 300; ++i) {
    rng = rng * 1664525u + 1013904223u;
    const size_t sizes[] = {0, 1, 9, 25, 26, 27, 160, 500, kMaxPacket, kMaxPacket + 1, 5000};
    std::vector<uint8_t> junk(sizes[i % 11]);
    for (auto& byte : junk) { rng = rng * 1664525u + 1013904223u; byte = (uint8_t)(rng >> 24); }
    if (i % 3 == 0 && junk.size() > 2) { junk[0] = kPacketVersion; junk[1] = (uint8_t)(i & 1); }
    if (i % 7 == 0 && junk.size() > 4) std::memcpy(junk.data(), "MUNH", 4);
    wire.to_a.emplace_back((uint8_t)(i % 3), junk);   // channel 2 does not exist
  }
  a.session.tick();
  EXPECT(a.session.state() == SessionState::Ready);
  EXPECT(a.session.counters().packets_rejected >= 250);

  // The peer leaving before the match is a failure the game can show.
  b.session.stop();
  a.session.tick();
  EXPECT(a.session.state() == SessionState::Failed);
  a.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 5 && ms[357] != 0);
  std::vector<uint8_t> reply;
  uint8_t pad[kPadGameSize] = {};
  a.session.on_inputs(1, 0, 0, 2, pad, reply);
  EXPECT(reply.size() == kInputsReplySize && reply[0] == kInputsDisconnected);
  a.session.on_inputs(pad, 3, reply);   // a short payload
  EXPECT(reply.size() == kInputsReplySize && reply[0] == kInputsDisconnected);
}

static void test_wrong_expected_key() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1, false, 300000);
  b.setup(clock, 2, false, 300000);
  const SessionDescriptor d = test_descriptor();
  const Bytes32 somebody_else = test_identity(9).public_key;
  EXPECT(a.session.start(d, 0, la, &somebody_else));
  EXPECT(b.session.start(d, 1, lb));
  EXPECT(!settle(a, b, clock, SessionState::Ready, 100));
  EXPECT(a.session.state() != SessionState::Ready && b.session.state() != SessionState::Ready);
  settle(a, b, clock, SessionState::Failed, 600);
  EXPECT(a.session.state() == SessionState::Failed);
  EXPECT(b.session.state() == SessionState::Failed);
  EXPECT(a.session.failure_text().find("not the one") != std::string::npos);
  EXPECT(a.logged("another identity"));
}

static void test_descriptor_mismatch() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  SessionDescriptor da = test_descriptor(), db = test_descriptor();
  db.stage = 0x1F;
  EXPECT(a.session.start(da, 0, la));
  EXPECT(b.session.start(db, 1, lb));
  EXPECT(settle(a, b, clock, SessionState::Failed));
  EXPECT(a.session.failure_text().find("do not agree") != std::string::npos);
  EXPECT(b.session.failure_text().find("do not agree") != std::string::npos);
  std::vector<uint8_t> reply;
  uint8_t pad[kPadGameSize] = {};
  a.session.on_inputs(1, 0, 0, 2, pad, reply);
  EXPECT(reply.size() == kInputsReplySize && reply[0] == kInputsDisconnected);   // no frame 1 without one digest

  // A descriptor this build cannot play never opens a connection at all.
  Peer c;
  c.setup(clock, 3);
  SessionDescriptor bad = test_descriptor();
  bad.features |= kFeatureSubframeSamples;
  Wire other;
  MemoryLink lc(other, true);
  EXPECT(!c.session.start(bad, 0, lc));
  EXPECT(c.session.state() == SessionState::Failed && other.to_b.empty());
}

static void test_stall_rule() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;

  // The peer has sent nothing: frames 1 to 7 run on prediction, frame 8 is held.
  for (int f = 1; f <= 7; ++f) { EXPECT(ga.step() == kInputsNormal); clock.us += 16683; }
  EXPECT(ga.frame == 8);
  for (int i = 0; i < 5; ++i) { EXPECT(ga.step() == kInputsSkip); clock.us += 16683; }
  EXPECT(ga.frame == 8);
  EXPECT(a.session.counters().waits == 1 && a.session.counters().stalls == 5);
  EXPECT(a.logged("waiting for the other player's inputs on frame 8"));

  // The peer plays its frame 1, which commits frames 1 to 3 (delay 2): frames 8, 9 and 10 are
  // released (3 >= frame - 7) and frame 11 is held again.
  EXPECT(gb.step() == kInputsNormal);
  EXPECT(ga.step() == kInputsNormal && ga.step() == kInputsNormal && ga.step() == kInputsNormal);
  EXPECT(ga.frame == 11);
  EXPECT(a.logged("inputs arrived on frame 8"));
  EXPECT(ga.step() == kInputsSkip && ga.frame == 11);
  EXPECT(a.session.counters().waits == 2);
  EXPECT(a.session.counters().remote_frame == 3);

  // Seven seconds of that and the match ends as disconnected, with a full-size answer.
  clock.us += 6900000;
  EXPECT(ga.step() == kInputsSkip);
  clock.us += 200000;
  EXPECT(ga.step() == kInputsDisconnected);
  EXPECT(a.session.counters().peer_gone);
  EXPECT(a.logged("no inputs from the other player"));
}

static void test_checksum_desync() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;
  for (int i = 0; i < 40; ++i) { ga.step(); gb.step(); clock.us += 16683; }
  EXPECT(a.session.counters().checksums_compared > 10);
  EXPECT(!a.session.counters().desync && !b.session.counters().desync);
  EXPECT(a.session.counters().checksum_mismatches == 0);
  gb.checksum_bias = 0x00000100;   // from here the two games disagree about their state
  for (int i = 0; i < 20; ++i) { ga.step(); gb.step(); clock.us += 16683; }
  EXPECT(a.session.counters().desync && b.session.counters().desync);
  EXPECT(a.session.counters().checksum_mismatches > 0);
  EXPECT(a.logged("DESYNC"));
}

static void test_lossy_two_thousand_frames() {
  Clock clock;
  Wire wire;
  MemoryLink ma(wire, true), mb(wire, false);
  FaultPlan plan_a, plan_b;
  plan_a.seed = 0xA11CE; plan_a.drop_per_1000 = 120; plan_a.duplicate_per_1000 = 60; plan_a.reorder_per_1000 = 120; plan_a.max_hold = 4;
  plan_b.seed = 0xB0B; plan_b.drop_per_1000 = 200; plan_b.duplicate_per_1000 = 30; plan_b.reorder_per_1000 = 80; plan_b.max_hold = 3;
  FaultLink la(ma, plan_a), lb(mb, plan_b);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;

  const int kFrames = 2000, kLast = kFrames + 30;
  bool faults_on = true;
  for (int i = 0; i < 40000 && (ga.frame <= kLast || gb.frame <= kLast); ++i) {
    if (faults_on && (ga.frame > kFrames || gb.frame > kFrames)) {
      // The tail runs clean so both sides end holding every frame of the other.
      la.set_plan(FaultPlan());
      lb.set_plan(FaultPlan());
      faults_on = false;
    }
    if (ga.frame <= kLast) EXPECT(ga.step() != kInputsDisconnected);
    // The second game misses a tick now and then: one side runs ahead, predicts, and is held.
    if (gb.frame <= kLast && (i % 23 != 0 || !faults_on)) EXPECT(gb.step() != kInputsDisconnected);
    clock.us += 16683;
  }
  EXPECT(ga.frame == kLast + 1 && gb.frame == kLast + 1);
  for (int i = 0; i < 8; ++i) { a.session.tick(); b.session.tick(); clock.us += 1000; }
  EXPECT(la.dropped > 100 && la.duplicated > 50 && la.reordered > 100 && lb.dropped > 200);

  const SessionCounters ca = a.session.counters(), cb = b.session.counters();
  EXPECT(ca.local_frame == kLast + 2 && cb.local_frame == kLast + 2);
  EXPECT(ca.remote_frame == kLast + 2 && cb.remote_frame == kLast + 2);
  EXPECT(ca.transcript_frames == kLast + 2 && cb.transcript_frames == kLast + 2);
  EXPECT(ca.transport.conflicts == 0 && cb.transport.conflicts == 0);
  EXPECT(ca.transport.malformed == 0 && cb.transport.malformed == 0);
  EXPECT(ca.packets_rejected > 0);   // the duplicates, refused by the replay window
  EXPECT(!ca.desync && !cb.desync && ca.checksums_compared > 100);

  GameResult ra, rb;
  EXPECT(a.session.result(ra) && b.session.result(rb));
  EXPECT(ra.transcript_complete && rb.transcript_complete);
  EXPECT(ra.transcript_frames == rb.transcript_frames);
  EXPECT(ra.transcript_digest == rb.transcript_digest);
  EXPECT(!all_zero(ra.transcript_digest));
  EXPECT(ra.descriptor_digest == rb.descriptor_digest);

  // The game ends: outcome parsed, digests exchanged, both sides see agreement.
  uint8_t end[kGameEndPayloadSize] = {};
  end[0] = 9; end[4] = 0x10; end[13] = 1; end[14] = 2; end[15] = 0xFF;
  end[20] = 0; end[21] = 0; end[29] = 0; end[30] = 2;
  end[22] = 0x42; end[23] = 0xC8;   // 100.0f damage done
  a.session.on_game_end(end, sizeof end);
  b.session.on_game_end(end, sizeof end);
  for (int i = 0; i < 4; ++i) { a.session.tick(); b.session.tick(); clock.us += 1000; }
  EXPECT(a.session.state() == SessionState::Ended);
  EXPECT(a.session.result(ra) && b.session.result(rb));
  EXPECT(ra.has_outcome && ra.winner == 1 && ra.end_method == 2 && ra.lras_initiator == -1 && ra.frame_length == 0x10);
  EXPECT(ra.players[0].damage_done == 100.0f && ra.players[1].stocks == 2);
  EXPECT(ra.peer_agreement == 1 && rb.peer_agreement == 1);
  std::vector<uint8_t> ms;
  a.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 4 && ms[1] == 0);   // one game a session: not ready again

  // The signed result file, and that touching it breaks the signature.
  const auto dir = std::filesystem::temp_directory_path() / "mu_net_test";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "result.json";
  std::string error;
  EXPECT(a.session.write_result_file(path.u8string(), &error));
  std::ifstream in(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  Bytes32 signer{};
  EXPECT(verify_result_text(text, &signer) && signer == test_identity(1).public_key);
  EXPECT(text.find("\"peer_agreement\":\"match\"") != std::string::npos);
  EXPECT(text.find("\"damage_done\":100.000") != std::string::npos);
  std::string tampered = text;
  const size_t at = tampered.find("\"winner\":1");
  EXPECT(at != std::string::npos);
  if (at != std::string::npos) { tampered[at + 9] = '0'; EXPECT(!verify_result_text(tampered, nullptr)); }
  EXPECT(!verify_result_text(text.substr(0, text.size() / 2), nullptr));
  EXPECT(!verify_result_text("", nullptr));
  std::filesystem::remove(path, ec);
}

// A small copy of what the game does with the B0 answer, so that frame numbering is tested and
// not only delivery: the local pad goes through a delay buffer (applied `delay` asked frames
// later), the peer's pad for frame f is read at (latest - f) in the seven entries, a frame whose
// pad has not arrived is predicted with the newest pad and re-simulated when the real one
// differs, and the checksum of a frame is a hash of the state that frame starts from. Two of these
// stay equal only if each side applies every pad on the frame the other side applied it.
struct MiniGame {
  RollbackSession* session = nullptr;
  int slot = 0, delay = 2;
  int frame = 1, confirmed = 0, stable_finalized = 0;
  std::vector<uint64_t> state;                              // state[f]: what frame f starts from
  std::vector<std::array<uint8_t, kPadWireSize>> local_used, remote_used;
  std::array<uint8_t, kPadWireSize> buffer[kMaxInputDelay] = {};
  int buffer_at = 0;
  int rollbacks = 0, max_depth = 0, window_misses = 0, pairs_compared = 0, pair_mismatches = 0, skips = 0;

  void reset(int frames) {
    state.assign((size_t)frames + 8, 0);
    state[1] = 0x9E3779B97F4A7C15ull;
    const std::array<uint8_t, kPadWireSize> none = {};
    local_used.assign((size_t)frames + 8, none);
    remote_used.assign((size_t)frames + 8, none);
  }
  static void pad_of(int slot, int f, uint8_t pad[kPadGameSize]) {
    // Changes every three frames and is often zero: predictions are right and wrong in turn.
    std::memset(pad, 0, kPadGameSize);
    uint32_t x = (uint32_t)(f / 3) * 2654435761u ^ (uint32_t)(slot + 1) * 40503u;
    x ^= x >> 15;
    if ((x & 3) == 0) return;
    for (int i = 0; i < kPadWireSize; ++i) pad[i] = (uint8_t)(x >> (i * 3));
  }
  uint32_t checksum(int f) const {
    const uint32_t c = (uint32_t)(state[f] ^ (state[f] >> 32));
    return c ? c : 1;
  }
  void simulate(int f) {
    const uint8_t* p0 = slot == 0 ? local_used[f].data() : remote_used[f].data();
    const uint8_t* p1 = slot == 0 ? remote_used[f].data() : local_used[f].data();
    uint64_t s = state[f] ^ (uint64_t)f;
    for (int i = 0; i < kPadWireSize; ++i) s = (s ^ p0[i]) * 1099511628211ull;
    for (int i = 0; i < kPadWireSize; ++i) s = (s ^ p1[i]) * 1099511628211ull;
    state[f + 1] = s;
  }
  int step() {
    uint8_t pad[kPadGameSize];
    pad_of(slot, frame, pad);
    std::vector<uint8_t> reply;
    const int sent_finalized = stable_finalized;
    session->on_inputs(frame, sent_finalized, sent_finalized > 0 ? checksum(sent_finalized) : 0, (uint8_t)delay, pad, reply);
    EXPECT(reply.size() == kInputsReplySize);
    if (reply.size() != kInputsReplySize) return kInputsDisconnected;
    const int result = reply[0];
    if (result == kInputsSkip) { ++skips; return result; }   // nothing is buffered, the frame is asked again
    if (result == kInputsDisconnected) return result;
    // The local pad: the one buffered `delay` asked frames ago is applied, this one is buffered.
    local_used[frame] = buffer[buffer_at];
    std::memcpy(buffer[buffer_at].data(), pad, kPadWireSize);
    buffer_at = (buffer_at + 1) % delay;

    const int latest = (int)get_be32(&reply[26]);
    const uint8_t* inputs = &reply[42];
    EXPECT(latest <= frame);
    // Pads that arrived for frames not yet confirmed, oldest first.
    int rollback_from = 0;
    for (int f = confirmed + 1; f <= latest; ++f) {
      const int index = latest - f;
      if (index > 6) { ++window_misses; break; }   // the game would read past the seven entries here
      const uint8_t* actual = inputs + index * kPadGameSize;
      if (f < frame && !rollback_from && std::memcmp(actual, remote_used[f].data(), kPadWireSize) != 0) rollback_from = f;
      std::memcpy(remote_used[f].data(), actual, kPadWireSize);
      confirmed = f;
    }
    if (rollback_from) {
      ++rollbacks;
      if (frame - rollback_from > max_depth) max_depth = frame - rollback_from;
      for (int f = rollback_from; f < frame; ++f) {
        if (f > confirmed) std::memcpy(remote_used[f].data(), inputs, kPadWireSize);   // predicted again from the newest pad
        simulate(f);
      }
    }
    if (frame > confirmed) std::memcpy(remote_used[frame].data(), inputs, kPadWireSize);   // prediction
    simulate(frame);
    const int finalized = confirmed < frame ? confirmed : frame;
    if (finalized > stable_finalized) stable_finalized = finalized;
    // The peer's checksum, compared as the game does: only for a frame this side has finalized.
    const int their_frame = (int)get_be32(&reply[2]);
    const uint32_t theirs = get_be32(&reply[6]);
    if (their_frame > 0 && their_frame <= stable_finalized && theirs != 0) {
      ++pairs_compared;
      if (theirs != checksum(their_frame)) ++pair_mismatches;
    }
    ++frame;
    return result;
  }
};

static void run_fake_game_states(bool lossy) {
  Clock clock;
  Wire wire;
  MemoryLink ma(wire, true), mb(wire, false);
  FaultPlan plan_a, plan_b;
  if (lossy) {
    plan_a.seed = 77; plan_a.drop_per_1000 = 150; plan_a.duplicate_per_1000 = 50; plan_a.reorder_per_1000 = 100; plan_a.max_hold = 4;
    plan_b.seed = 78; plan_b.drop_per_1000 = 100; plan_b.duplicate_per_1000 = 50; plan_b.reorder_per_1000 = 150; plan_b.max_hold = 3;
  }
  FaultLink la(ma, plan_a), lb(mb, plan_b);
  Peer a, b;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  const int kFrames = 1500, kLast = kFrames + 40;
  MiniGame ga, gb;
  ga.session = &a.session; ga.slot = 0; ga.reset(kLast);
  gb.session = &b.session; gb.slot = 1; gb.reset(kLast);

  bool faults_on = true;
  for (int i = 0; i < 40000 && (ga.frame <= kLast || gb.frame <= kLast); ++i) {
    if (faults_on && (ga.frame > kFrames || gb.frame > kFrames)) {
      la.set_plan(FaultPlan());
      lb.set_plan(FaultPlan());
      faults_on = false;
    }
    if (ga.frame <= kLast) EXPECT(ga.step() != kInputsDisconnected);
    // The second game hitches for twelve ticks now and then (the first runs to the stall edge and
    // is held), then catches up in one burst: its frames reach the first game eight or more at a
    // time, the case that needs the window anchored at the finalized frame. In between it drops
    // a tick here and there, so the first game is usually predicting.
    const int phase = i % 300;
    int steps = 1;
    if (faults_on && phase >= 200 && phase < 212) steps = 0;
    else if (faults_on && phase == 212) steps = 13;
    else if (faults_on && i % 29 == 0) steps = 0;
    for (int s = 0; s < steps && gb.frame <= kLast; ++s) {
      const int result = gb.step();
      EXPECT(result != kInputsDisconnected);
      if (result == kInputsSkip) break;
    }
    clock.us += 16683;
  }
  EXPECT(ga.frame == kLast + 1 && gb.frame == kLast + 1);
  for (int i = 0; i < 8; ++i) { a.session.tick(); b.session.tick(); clock.us += 1000; }

  EXPECT(ga.window_misses == 0 && gb.window_misses == 0);
  EXPECT(ga.rollbacks > 20);                    // the leading game really did predict and correct
  EXPECT(ga.max_depth >= 5 && ga.max_depth <= 7);
  EXPECT(ga.pairs_compared > 100 && gb.pairs_compared > 100);
  EXPECT(ga.pair_mismatches == 0 && gb.pair_mismatches == 0);
  const int common = ga.confirmed < gb.confirmed ? ga.confirmed : gb.confirmed;
  EXPECT(common >= kFrames);
  int differing = 0;
  for (int f = 1; f <= common; ++f) differing += ga.state[f] != gb.state[f];
  EXPECT(differing == 0);                       // every frame started from the same state on both sides
  for (int f = 1; f <= common; ++f) {
    // And the reason: each pad was applied on one and the same frame by its owner and by the peer.
    if (std::memcmp(ga.local_used[f].data(), gb.remote_used[f].data(), kPadWireSize) != 0 ||
        std::memcmp(gb.local_used[f].data(), ga.remote_used[f].data(), kPadWireSize) != 0) { EXPECT(!"pad applied on different frames"); break; }
  }
  const SessionCounters ca = a.session.counters(), cb = b.session.counters();
  EXPECT(ca.checksums_compared > 100 && cb.checksums_compared > 100);
  EXPECT(ca.checksum_mismatches == 0 && cb.checksum_mismatches == 0);
  EXPECT(!ca.desync && !cb.desync);
  EXPECT(ca.transport.conflicts == 0 && cb.transport.conflicts == 0);
}

static void test_fake_game_states() {
  run_fake_game_states(false);
  run_fake_game_states(true);
}

static void test_time_sync_decisions() {
  // One side's game runs 2 % slow against the shared clock. With time sync on, the fast side is
  // told to slow down or shed frames and the slow side to speed up; nobody is disconnected.
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.setup(clock, 1, true);
  b.setup(clock, 2, true);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;
  uint64_t next_a = clock.us, next_b = clock.us;
  for (int i = 0; i < 200000 && ga.frame < 900; ++i) {
    if (clock.us >= next_a) { EXPECT(ga.step() != kInputsDisconnected); next_a += 16683; }
    if (clock.us >= next_b) { EXPECT(gb.step() != kInputsDisconnected); next_b += 17017; }
    clock.us += 500;
  }
  EXPECT(ga.frame >= 900);
  const SessionCounters ca = a.session.counters(), cb = b.session.counters();
  EXPECT(ca.sync_skips + ca.stalls > 0);           // the fast side gave frames back
  EXPECT(a.speed < 1.0);                           // and was told to run slower than the host's tick
  EXPECT(b.speed > a.speed);
  EXPECT(ca.local_frame - cb.local_frame < 12 && cb.local_frame - ca.local_frame < 12);
}

static void test_identity_file() {
  const auto dir = std::filesystem::temp_directory_path() / "mu_net_test";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "identity.json";
  std::filesystem::remove(path, ec);
  PeerIdentity first, second;
  std::string error;
  EXPECT(load_or_create_identity(path.u8string(), first, &error) == IdentityFile::Created);
  EXPECT(load_or_create_identity(path.u8string(), second, &error) == IdentityFile::Loaded);
  EXPECT(first.valid && second.valid && first.public_key == second.public_key);

  // The launcher lobby's file: more fields than ours, the same key name.
  Bytes32 seed;
  seed.fill(0x5A);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "{\n  \"ed_seed\": \"" << to_hex(seed.data(), seed.size()) << "\",\n  \"friends\": [],\n  \"profile\": {\"name\": \"x\"},\n  \"x_secret\": \""
        << to_hex(seed.data(), seed.size()) << "\"\n}";
  }
  PeerIdentity lobby;
  EXPECT(load_or_create_identity(path.u8string(), lobby, &error) == IdentityFile::Loaded);
  EXPECT(lobby.public_key == identity_from_seed(seed).public_key);

  // A raw 32-byte seed.
  { std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(seed.data()), 32); }
  PeerIdentity raw;
  EXPECT(load_or_create_identity(path.u8string(), raw, &error) == IdentityFile::Loaded && raw.public_key == lobby.public_key);

  // A damaged file is an error and is left as it was.
  { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << "{\"ed_seed\": \"zz\"}"; }
  PeerIdentity broken;
  EXPECT(load_or_create_identity(path.u8string(), broken, &error) == IdentityFile::Failed && !broken.valid);
  EXPECT(std::filesystem::file_size(path, ec) == 17);
  std::filesystem::remove(path, ec);

  Bytes64 signature{};
  const uint8_t message[] = "hello";
  sign(first, message, sizeof message, signature);
  EXPECT(verify(first.public_key, message, sizeof message, signature));
  signature[5] ^= 1;
  EXPECT(!verify(first.public_key, message, sizeof message, signature));
}

static bool wait_for(const std::function<bool()>& done, int milliseconds) {
  for (int i = 0; i < milliseconds; ++i) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

static void test_loopback_sockets() {
  // Real UDP on 127.0.0.1, both sides dialing each other at once: two ENet connections come up
  // and the sessions agree on one.
  PeerConnector ca, cb;
  std::string error;
  EXPECT(ca.open(0, &error) && cb.open(0, &error));
  EXPECT(ca.local_port() != 0 && cb.local_port() != 0 && ca.local_port() != cb.local_port());
  const auto now = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  EXPECT(ca.connect({Endpoint{"127.0.0.1", cb.local_port()}}, now(), &error));
  EXPECT(cb.connect({Endpoint{"127.0.0.1", ca.local_port()}}, now(), &error));
  {
    // Names are refused, not looked up: a lookup could hold the caller for seconds.
    PeerConnector named;
    EXPECT(named.open(0, &error));
    EXPECT(!named.connect({Endpoint{"example.invalid", 1}}, now(), &error) && named.failure() == ConnectFailure::BadAddress);
  }

  RollbackSession a, b;
  SessionOptions opt;
  opt.pump_thread = false;
  opt.time_sync = false;
  std::vector<std::string> log_a;
  SessionCallbacks cba;
  cba.log = [&log_a](const char* line) { log_a.push_back(line); };
  a.configure(cba, opt);
  b.configure(SessionCallbacks(), opt);
  a.set_identity(test_identity(1));
  b.set_identity(test_identity(2));
  const SessionDescriptor d = test_descriptor();
  const Bytes32 a_key = test_identity(1).public_key;
  EXPECT(a.start(d, 0, ca));
  EXPECT(b.start(d, 1, cb, &a_key));
  EXPECT(wait_for([&] { a.tick(); b.tick(); return a.state() == SessionState::Ready && b.state() == SessionState::Ready; }, 5000));
  EXPECT(a.descriptor_digest() == b.descriptor_digest());
  EXPECT(!ca.peer_address().empty());

  FakeGame ga, gb;
  ga.session = &a; ga.slot = 0;
  gb.session = &b; gb.slot = 1;
  EXPECT(wait_for([&] {
    if (ga.frame <= 200) ga.step();
    if (gb.frame <= 200) gb.step();
    return ga.frame > 200 && gb.frame > 200;
  }, 5000));
  EXPECT(wait_for([&] { a.tick(); b.tick(); return a.counters().transcript_frames == 202 && b.counters().transcript_frames == 202; }, 2000));
  GameResult ra, rb;
  EXPECT(a.result(ra) && b.result(rb) && ra.transcript_digest == rb.transcript_digest);
  EXPECT(a.counters().transport.acks_received > 0 && a.counters().ping_us < 500000);
  b.stop();
  EXPECT(wait_for([&] { ga.step(); return ga.last_result == kInputsDisconnected; }, 3000));
  a.stop();
  ca.close();
  cb.close();

  // Nobody there: a clear failure inside the limit, not a hang.
  PeerConnector alone;
  EXPECT(alone.open(0, &error));
  alone.set_limit_us(300000);
  RollbackSession c;
  SessionOptions quick = opt;
  quick.connect_limit_us = 300000;
  c.configure(SessionCallbacks(), quick);
  c.set_identity(test_identity(3));
  EXPECT(alone.connect({Endpoint{"127.0.0.1", 9}}, now(), &error));
  EXPECT(c.start(d, 0, alone));
  EXPECT(wait_for([&] { c.tick(); return c.state() == SessionState::Failed; }, 3000));
  EXPECT(c.failure_text().find("could not be reached") != std::string::npos);
  c.stop();
}

static void test_pump_thread_session() {
  // The session as a host uses it: its own socket and its own pump thread; the host only asks.
  RollbackSession a, b;
  a.configure(SessionCallbacks(), SessionOptions());
  b.configure(SessionCallbacks(), SessionOptions());
  a.set_identity(test_identity(1));
  b.set_identity(test_identity(2));
  const SessionDescriptor d = test_descriptor();
  EndpointInfo listen;   // no candidates: wait for the peer to dial
  EXPECT(a.start(d, 0, listen));
  EXPECT(a.local_port() != 0);
  EndpointInfo dial;
  dial.candidates.push_back(Endpoint{"127.0.0.1", a.local_port()});
  dial.has_expected_peer = true;
  dial.expected_peer = test_identity(1).public_key;
  EXPECT(b.start(d, 1, dial));
  EXPECT(wait_for([&] { return a.state() == SessionState::Ready && b.state() == SessionState::Ready; }, 5000));
  std::vector<uint8_t> ms;
  a.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 4 && ms[1] == 1);
  FakeGame ga, gb;
  ga.session = &a; ga.slot = 0;
  gb.session = &b; gb.slot = 1;
  EXPECT(wait_for([&] {
    if (ga.frame <= 120) ga.step();
    if (gb.frame <= 120) gb.step();
    return ga.frame > 120 && gb.frame > 120;
  }, 5000));
  EXPECT(wait_for([&] { return a.counters().ping_us > 0 || a.counters().transport.acks_received > 0; }, 1000));
  a.stop();
  b.stop();
  EXPECT(a.state() == SessionState::Idle && b.state() == SessionState::Idle);
}

// ---------------------------------------------------------------- more than one game in a session
static const std::vector<uint16_t> kTestPool = {0x1F, 0x20, 0x1C, 0x08, 0x02, 0x03};

// Both fake games run to `frames`, then the sessions are pumped until each holds every frame.
static void play_frames(Peer& a, Peer& b, FakeGame& ga, FakeGame& gb, Clock& clock, int frames) {
  for (int i = 0; i < frames * 8 && (ga.frame <= frames || gb.frame <= frames); ++i) {
    if (ga.frame <= frames) EXPECT(ga.step() != kInputsDisconnected);
    if (gb.frame <= frames) EXPECT(gb.step() != kInputsDisconnected);
    clock.us += 16683;
  }
  EXPECT(ga.frame == frames + 1 && gb.frame == frames + 1);
  for (int i = 0; i < 8; ++i) { a.session.tick(); b.session.tick(); clock.us += 1000; }
}

// The game's end report: a finished game (method 2) unless told otherwise.
static void report_end(Peer& p, uint8_t method = 2) {
  uint8_t end[kGameEndPayloadSize] = {};
  end[0] = 2; end[4] = 0x10; end[13] = 1; end[14] = method; end[15] = 0xFF;
  p.session.on_game_end(end, sizeof end);
}

static void pump(Peer& a, Peer& b, Clock& clock, int rounds = 6) {
  for (int i = 0; i < rounds; ++i) { a.session.tick(); b.session.tick(); clock.us += 1000; }
}

static void test_next_descriptor() {
  const SessionDescriptor d = test_descriptor();
  const Bytes32 dd = digest(d);
  const SessionDescriptor n2 = next_descriptor(d, dd, 2, kTestPool), again = next_descriptor(d, dd, 2, kTestPool);
  std::string why;
  EXPECT(validate(n2, &why));
  EXPECT(digest(n2) == digest(again));                 // made from the descriptor and its digest alone
  EXPECT(digest(n2) != dd && n2.match_id != d.match_id);
  EXPECT(digest(next_descriptor(d, dd, 3, kTestPool)) != digest(n2));   // the game index is in the draw
  Bytes32 other = dd;
  other[0] ^= 1;
  EXPECT(digest(next_descriptor(d, other, 2, kTestPool)) != digest(n2));
  bool in_pool = false;
  for (uint16_t s : kTestPool) in_pool = in_pool || s == n2.stage;
  EXPECT(in_pool);
  EXPECT(next_descriptor(d, dd, 2, {}).stage == d.stage);   // no pool: the stage stays
  EXPECT(n2.players[0].character == d.players[0].character && n2.players[1].name == d.players[1].name);
  EXPECT(n2.input_delay == d.input_delay && n2.stocks == d.stocks && n2.features == d.features);
  // Over many games the draw reaches more than one stage.
  SessionDescriptor walk = d;
  int changes = 0;
  for (uint32_t g = 2; g < 40; ++g) {
    const SessionDescriptor next = next_descriptor(walk, digest(walk), g, kTestPool);
    changes += next.stage != walk.stage;
    walk = next;
  }
  EXPECT(changes > 5);
}

static void test_transport_game_base() {
  // The same messages, numbered for the second game: taken by a transport in that game, dropped
  // whole by one in another game, and never counted as malformed.
  InputTransport a, b, first;
  a.reset(16683, kGameFrameSpan);
  b.reset(16683, kGameFrameSpan);
  first.reset(16683);
  std::vector<uint8_t> message, reply;
  uint8_t pad[kPadWireSize] = {9, 8, 7, 6, 5, 4, 3, 2};
  EXPECT(a.commit(1, pad) && a.commit(2, pad));
  a.set_checksum(1, 0xABCD0001u);
  a.build_commit(1000, message);
  EXPECT(message.size() == kCommitHeader + 2 * kPadWireSize);
  { Reader r(message.data() + 1, 4); EXPECT(r.i32() == kGameFrameSpan + 2); }
  EXPECT(b.on_message(message.data(), message.size(), 2000, reply));
  EXPECT(b.remote_latest() == 2 && b.remote_checksum_frame() == 1 && b.remote_checksum() == 0xABCD0001u);
  EXPECT(reply.size() == 9 && reply[0] == kMsgAck);
  { Reader r(reply.data() + 1, 4); EXPECT(r.i32() == kGameFrameSpan + 2); }
  EXPECT(a.on_message(reply.data(), reply.size(), 3000, message) && a.peer_ack() == 2);
  // The second game's COMMIT at a transport still in the first game.
  a.build_commit(4000, message);
  EXPECT(first.on_message(message.data(), message.size(), 5000, reply) && reply.empty());
  EXPECT(first.remote_latest() == 0 && first.stats().other_game == 1 && first.stats().malformed == 0);
  // A first game's COMMIT and ACK, late, at a transport in the second game.
  InputTransport old;
  old.reset(16683);
  for (int f = 1; f <= 5; ++f) EXPECT(old.commit(f, pad));
  old.set_checksum(4, 0x11112222u);
  old.build_commit(6000, message);
  EXPECT(b.on_message(message.data(), message.size(), 7000, reply) && reply.empty());
  EXPECT(b.remote_latest() == 2 && b.remote_checksum_frame() == 1 && b.stats().other_game == 1 && b.stats().conflicts == 0);
  std::vector<uint8_t> ack;
  { Writer w(ack); w.u8(kMsgAck); w.i32(5); w.u32(100); }
  EXPECT(a.on_message(ack.data(), ack.size(), 8000, reply) && a.peer_ack() == 2 && a.stats().other_game == 1);
}

static void test_two_games() {
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.max_games = 2; a.pool = kTestPool;
  b.max_games = 0; b.pool = kTestPool;   // no limit on this side: the session plays the smaller number
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  EXPECT(a.session.game_index() == 1 && a.session.next_game() == NextGame::None);
  EXPECT(!a.session.begin_next_game());
  const Bytes32 first_digest = a.session.descriptor_digest();
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;
  play_frames(a, b, ga, gb, clock, 120);
  GameResult ra1, rb1;
  EXPECT(a.session.result(ra1) && b.session.result(rb1) && ra1.transcript_digest == rb1.transcript_digest);

  // One side's game ends first: it waits. Then the other's: both hold the same next descriptor.
  report_end(a);
  pump(a, b, clock);
  EXPECT(a.session.state() == SessionState::Ended && a.session.next_game() == NextGame::Waiting);
  EXPECT(b.session.next_game() == NextGame::None);
  std::vector<uint8_t> ms;
  a.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 4 && ms[1] == 0);   // connected, not ready
  report_end(b);
  pump(a, b, clock);
  EXPECT(a.session.next_game() == NextGame::Agreed && b.session.next_game() == NextGame::Agreed);
  EXPECT(a.session.result(ra1) && ra1.peer_agreement == 1 && !ra1.disconnected);

  // The first side begins the next game and already sends; the second is still in the old one and
  // drops those frames whole. Nothing is lost: they are sent again until acknowledged.
  EXPECT(a.session.begin_next_game());
  EXPECT(!a.session.begin_next_game());
  EXPECT(a.session.state() == SessionState::Ready && a.session.game_index() == 2);
  FakeGame ga2, gb2;
  ga2.session = &a.session; ga2.slot = 0;
  gb2.session = &b.session; gb2.slot = 1;
  for (int i = 0; i < 3; ++i) { EXPECT(ga2.step() == kInputsNormal); clock.us += 16683; }
  b.session.tick();
  EXPECT(b.session.counters().transport.other_game > 0);
  EXPECT(b.session.counters().transport.malformed == 0);
  EXPECT(b.session.begin_next_game());
  const SessionDescriptor da = a.session.descriptor(), db = b.session.descriptor();
  EXPECT(a.session.descriptor_digest() == b.session.descriptor_digest());
  EXPECT(a.session.descriptor_digest() != first_digest);
  EXPECT(da.match_id == db.match_id && da.rng_seed == db.rng_seed && da.stage == db.stage && da.rng_seed != d.rng_seed);
  EXPECT(da.players[0].character == d.players[0].character && da.players[1].identity_key == test_identity(2).public_key);
  b.session.match_state_reply(ms);
  EXPECT(ms.size() == kMatchStateReplySize && ms[0] == 4 && ms[1] == 1 && ms[2] == 1);
  if (ms.size() == kMatchStateReplySize) {
    EXPECT(get_be32(&ms[5]) == db.rng_seed);
    EXPECT(((ms[598 + 0x0E] << 8) | ms[598 + 0x0F]) == db.stage);
  }
  GameResult none;
  EXPECT(!b.session.result(none));   // nothing of the next game was played yet

  // Game 2: frames count from 1 again, with a transcript and checksums of its own.
  play_frames(a, b, ga2, gb2, clock, 200);
  const SessionCounters ca = a.session.counters(), cb = b.session.counters();
  EXPECT(ca.transcript_frames == 202 && cb.transcript_frames == 202);
  EXPECT(ca.checksums_compared > 20 && ca.checksum_mismatches == 0 && !ca.desync && !cb.desync);
  EXPECT(ca.transport.conflicts == 0 && cb.transport.conflicts == 0);
  GameResult ra2, rb2;
  EXPECT(a.session.result(ra2) && b.session.result(rb2));
  EXPECT(ra2.transcript_digest == rb2.transcript_digest && ra2.descriptor_digest == rb2.descriptor_digest);
  EXPECT(ra2.descriptor_digest != ra1.descriptor_digest && ra2.transcript_digest != ra1.transcript_digest);
  EXPECT(ra2.match_id != ra1.match_id);

  // The limit (2, the smaller of the two sides') is reached: no NEXT, the session is over.
  report_end(a);
  report_end(b);
  pump(a, b, clock);
  std::string why;
  EXPECT(a.session.next_game(&why) == NextGame::Over && why.find("last game") != std::string::npos);
  EXPECT(b.session.next_game() == NextGame::Over);
  EXPECT(a.session.result(ra2) && ra2.peer_agreement == 1);
  EXPECT(!a.session.begin_next_game());
}

static void test_next_digest_mismatch() {
  // The two sides draw the next stage from different pools: the NEXT digests differ, nobody
  // starts a second game, and the finished game's record stands.
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.max_games = 0; a.pool = {0x1F};
  b.max_games = 0; b.pool = {0x08};
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;
  play_frames(a, b, ga, gb, clock, 60);
  report_end(a);
  report_end(b);
  pump(a, b, clock);
  std::string why_a, why_b;
  EXPECT(a.session.next_game(&why_a) == NextGame::Over && b.session.next_game(&why_b) == NextGame::Over);
  EXPECT(why_a.find("do not agree") != std::string::npos && why_b.find("do not agree") != std::string::npos);
  EXPECT(!a.session.begin_next_game() && !b.session.begin_next_game());
  EXPECT(a.session.state() == SessionState::Ended);
  GameResult ra;
  EXPECT(a.session.result(ra) && ra.peer_agreement == 1 && !ra.disconnected);
  std::vector<uint8_t> reply;
  uint8_t pad[kPadGameSize] = {};
  a.session.on_inputs(1, 0, 0, 2, pad, reply);   // a game that restarts anyway gets no frame 1
  EXPECT(reply.size() == kInputsReplySize && reply[0] == kInputsDisconnected);
}

static void test_previous_revision_peer() {
  // A peer of revision 0: game 1 is played exactly as before, then the session is over on both
  // sides without a NEXT, and the new messages it was sent did it no harm.
  Clock clock;
  Wire wire;
  MemoryLink la(wire, true), lb(wire, false);
  Peer a, b;
  a.max_games = 0; a.pool = kTestPool;
  b.minor = 0;
  a.setup(clock, 1);
  b.setup(clock, 2);
  const SessionDescriptor d = test_descriptor();
  EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
  EXPECT(settle(a, b, clock));
  FakeGame ga, gb;
  ga.session = &a.session; ga.slot = 0;
  gb.session = &b.session; gb.slot = 1;
  play_frames(a, b, ga, gb, clock, 100);
  EXPECT(b.session.counters().messages_malformed == 1);   // the INFO it does not know: counted, ignored
  EXPECT(a.session.counters().checksum_mismatches == 0 && a.session.counters().transport.other_game == 0);
  report_end(a);
  report_end(b);
  pump(a, b, clock);
  std::string why;
  EXPECT(a.session.next_game(&why) == NextGame::Over && why.find("one game per session") != std::string::npos);
  EXPECT(b.session.next_game() == NextGame::Over);
  EXPECT(b.session.counters().messages_malformed == 1);   // and no NEXT was sent to it
  GameResult ra, rb;
  EXPECT(a.session.result(ra) && b.session.result(rb));
  EXPECT(ra.peer_agreement == 1 && rb.peer_agreement == 1 && ra.transcript_digest == rb.transcript_digest);
  EXPECT(a.session.state() == SessionState::Ended && b.session.state() == SessionState::Ended);
}

static void test_bye_between_games() {
  // One side is ready for the next game and the other leaves: a clean end, and the finished game
  // is not marked as cut short.
  {
    Clock clock;
    Wire wire;
    MemoryLink la(wire, true), lb(wire, false);
    Peer a, b;
    a.max_games = 3; b.max_games = 3;
    a.setup(clock, 1);
    b.setup(clock, 2);
    const SessionDescriptor d = test_descriptor();
    EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
    EXPECT(settle(a, b, clock));
    FakeGame ga, gb;
    ga.session = &a.session; ga.slot = 0;
    gb.session = &b.session; gb.slot = 1;
    play_frames(a, b, ga, gb, clock, 60);
    report_end(a);
    report_end(b);
    pump(a, b, clock);
    EXPECT(a.session.next_game() == NextGame::Agreed);
    b.session.stop();   // BYE
    pump(a, b, clock);
    std::string why;
    EXPECT(a.session.next_game(&why) == NextGame::Over && why.find("left") != std::string::npos);
    EXPECT(!a.session.begin_next_game());
    EXPECT(a.session.state() == SessionState::Ended && a.session.failure_text().empty());
    GameResult ra;
    EXPECT(a.session.result(ra) && !ra.disconnected && ra.peer_agreement == 1);
    EXPECT(a.logged("left the session"));
  }
  // A quit from the pause screen ends the session on both sides without any NEXT.
  {
    Clock clock;
    Wire wire;
    MemoryLink la(wire, true), lb(wire, false);
    Peer a, b;
    a.max_games = 0; b.max_games = 0;
    a.setup(clock, 1);
    b.setup(clock, 2);
    const SessionDescriptor d = test_descriptor();
    EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
    EXPECT(settle(a, b, clock));
    FakeGame ga, gb;
    ga.session = &a.session; ga.slot = 0;
    gb.session = &b.session; gb.slot = 1;
    play_frames(a, b, ga, gb, clock, 40);
    report_end(a, 7);
    report_end(b, 7);
    pump(a, b, clock);
    std::string why;
    EXPECT(a.session.next_game(&why) == NextGame::Over && why.find("quit") != std::string::npos);
    EXPECT(b.session.next_game() == NextGame::Over);
  }
  // The other game never says NEXT (it hangs in its results): this side stops waiting.
  {
    Clock clock;
    Wire wire;
    MemoryLink la(wire, true), lb(wire, false);
    Peer a, b;
    a.max_games = 0; b.max_games = 0;
    a.next_limit_us = 3 * 1000000;
    a.setup(clock, 1);
    b.setup(clock, 2);
    const SessionDescriptor d = test_descriptor();
    EXPECT(a.session.start(d, 0, la) && b.session.start(d, 1, lb));
    EXPECT(settle(a, b, clock));
    FakeGame ga, gb;
    ga.session = &a.session; ga.slot = 0;
    gb.session = &b.session; gb.slot = 1;
    play_frames(a, b, ga, gb, clock, 40);
    report_end(a);
    pump(a, b, clock);
    EXPECT(a.session.next_game() == NextGame::Waiting);
    clock.us += 2 * 1000000;
    pump(a, b, clock);
    EXPECT(a.session.next_game() == NextGame::Waiting);
    clock.us += 2 * 1000000;
    pump(a, b, clock);
    std::string why;
    EXPECT(a.session.next_game(&why) == NextGame::Over && why.find("did not answer") != std::string::npos);
  }
}

// ---------------------------------------------------------------- NAT help (nat_discovery) and the punch list
static void test_stun_codec() {
  // The request: Binding, no attributes, the magic cookie, the transaction id.
  StunId id;
  for (size_t i = 0; i < id.size(); ++i) id[i] = (uint8_t)i;
  uint8_t request[kStunRequestSize];
  stun_write_request(id, request);
  const uint8_t expected_request[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  EXPECT(std::memcmp(request, expected_request, sizeof request) == 0);
  EXPECT(stun_looks_like(request, sizeof request));

  // RFC 5769, section 2.2: the sample IPv4 response. It maps 192.0.2.1 port 32853. The integrity
  // and fingerprint attributes are walked over, not checked.
  const uint8_t sample[80] = {
      0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae,
      0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
      0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
      0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3, 0x8c, 0x74, 0x89, 0xf9, 0x2a, 0xf9, 0xba, 0x53, 0xf0, 0x6b, 0xe7, 0xd7,
      0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96};
  const StunId sample_id = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};
  NatEndpoint mapped;
  EXPECT(stun_read_response(sample, sizeof sample, sample_id, mapped) == StunResult::Mapped);
  EXPECT(mapped.ip == 0xC0000201u && mapped.port == 32853);
  EXPECT(endpoint_text(mapped) == "192.0.2.1:32853");
  // Someone else's answer, a cut datagram, a datagram with a byte too many.
  EXPECT(stun_read_response(sample, sizeof sample, id, mapped) == StunResult::OtherTransaction);
  EXPECT(stun_read_response(sample, sizeof sample - 1, sample_id, mapped) == StunResult::NotStun);
  uint8_t longer[81];
  std::memcpy(longer, sample, sizeof sample);
  longer[80] = 0;
  EXPECT(stun_read_response(longer, sizeof longer, sample_id, mapped) == StunResult::NotStun);

  // The plain MAPPED-ADDRESS of an old server: 198.51.100.7 port 40000.
  uint8_t plain[32] = {0x01, 0x01, 0x00, 0x0c, 0x21, 0x12, 0xA4, 0x42, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                       0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x9C, 0x40, 198, 51, 100, 7};
  EXPECT(stun_read_response(plain, sizeof plain, id, mapped) == StunResult::Mapped);
  EXPECT(mapped.ip == 0xC6336407u && mapped.port == 40000);
  // The XOR form wins when both are there: the same address written both ways, the plain one wrong.
  uint8_t both[44];
  std::memcpy(both, plain, 32);
  both[3] = 0x18;
  both[28] = 10;   // the plain one now says 10.51.100.7
  const uint8_t xor_attr[12] = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0x9C ^ 0x21, 0x40 ^ 0x12, 198 ^ 0x21, 51 ^ 0x12, 100 ^ 0xA4, 7 ^ 0x42};
  std::memcpy(both + 32, xor_attr, 12);
  EXPECT(stun_read_response(both, sizeof both, id, mapped) == StunResult::Mapped && mapped.ip == 0xC6336407u && mapped.port == 40000);

  // An attribute that says it is longer than the datagram.
  uint8_t overrun[32];
  std::memcpy(overrun, plain, 32);
  overrun[23] = 0x10;
  EXPECT(stun_read_response(overrun, sizeof overrun, id, mapped) == StunResult::Malformed);
  // An IPv6 address only, a Binding error, a request echoed back.
  uint8_t v6[32];
  std::memcpy(v6, plain, 32);
  v6[25] = 0x02;
  EXPECT(stun_read_response(v6, sizeof v6, id, mapped) == StunResult::NoAddress);
  uint8_t error[20];
  std::memcpy(error, request, 20);
  error[0] = 0x01; error[1] = 0x11;
  EXPECT(stun_read_response(error, sizeof error, id, mapped) == StunResult::NoAddress);
  EXPECT(stun_read_response(request, sizeof request, id, mapped) == StunResult::Malformed);
  // A mapped address nothing can be sent to (0.0.0.0, or port 0).
  uint8_t zero[32];
  std::memcpy(zero, plain, 32);
  zero[28] = zero[29] = zero[30] = zero[31] = 0;
  EXPECT(stun_read_response(zero, sizeof zero, id, mapped) == StunResult::NoAddress);
  // Not STUN at all: a lobby packet, a DHT packet, nothing.
  const char lobby[] = "MUL1{\"t\":\"hello\",\"pad\":0}";
  const char dht[] = "d1:ad2:id20:abcdefghij0123456789e1:q4:ping1:t2:aa1:y1:qe";
  EXPECT(!stun_looks_like(reinterpret_cast<const uint8_t*>(lobby), sizeof lobby - 1));
  EXPECT(!stun_looks_like(reinterpret_cast<const uint8_t*>(dht), sizeof dht - 1));
  EXPECT(!stun_looks_like(nullptr, 0) && !stun_looks_like(request, 19));

  // A round of two requests on a socket the caller owns.
  StunProbe probe;
  EXPECT(probe.request(0) == nullptr && probe.answers() == 0 && probe.mapping() == NatMapping::Unknown);
  EXPECT(probe.begin(2) && probe.servers() == 2);
  EXPECT(probe.request(0) && probe.request(1) && !probe.request(2));
  EXPECT(std::memcmp(probe.request(0) + 8, probe.request(1) + 8, 12) != 0);   // two random ids
  auto answer_for = [&](int server, uint16_t port) {
    std::vector<uint8_t> a(plain, plain + 32);
    std::memcpy(a.data() + 8, probe.request(server) + 8, 12);
    a[26] = (uint8_t)(port >> 8);
    a[27] = (uint8_t)port;
    return a;
  };
  EXPECT(!probe.on_datagram(reinterpret_cast<const uint8_t*>(lobby), sizeof lobby - 1));   // left for the lobby
  EXPECT(probe.on_datagram(sample, sizeof sample) && probe.answers() == 0);                // STUN, but nobody's answer
  std::vector<uint8_t> a0 = answer_for(0, 40000), a1 = answer_for(1, 40000);
  EXPECT(probe.on_datagram(a0.data(), a0.size()) && probe.answers() == 1 && probe.mapping() == NatMapping::Unknown);
  EXPECT(probe.mapped(mapped) && mapped.port == 40000);
  EXPECT(probe.on_datagram(a1.data(), a1.size()) && probe.answers() == 2 && probe.mapping() == NatMapping::Stable);
  // A second answer to the same request changes nothing.
  std::vector<uint8_t> late = answer_for(1, 40777);
  EXPECT(probe.on_datagram(late.data(), late.size()) && probe.mapping() == NatMapping::Stable);
  // A router that gives each destination its own port.
  EXPECT(probe.begin(2));
  a0 = answer_for(0, 40000);
  a1 = answer_for(1, 40001);
  EXPECT(probe.on_datagram(a0.data(), a0.size()) && probe.on_datagram(a1.data(), a1.size()));
  EXPECT(probe.mapping() == NatMapping::PerDestination);

  // The server list: the default, the player's own, none.
  EXPECT(stun_server_list(nullptr).size() == 3 && stun_server_list("  ").size() == 3);
  EXPECT(stun_server_list("off").empty() && stun_server_list("NONE").empty());
  const std::vector<std::string> own = stun_server_list("a.example:1, b.example ,bad host,c.example:99999,d.example:x, e.example,b.example:3478");
  EXPECT(own.size() == 3 && own[0] == "a.example:1" && own[1] == "b.example:3478" && own[2] == "e.example:3478");
  EXPECT(stun_server_list("a:1,b:1,c:1,d:1,e:1,f:1").size() == (size_t)kMaxStunServers);
}

static void test_nat_addresses_and_mapping_text() {
  uint32_t ip = 0;
  NatEndpoint e;
  EXPECT(parse_ip("192.168.1.20", ip) && ip == 0xC0A80114u && ip_text(ip) == "192.168.1.20");
  EXPECT(!parse_ip("192.168.1", ip) && !parse_ip("192.168.1.256", ip) && !parse_ip("a.b.c.d", ip) && !parse_ip("1.2.3.4.5", ip) && !parse_ip("", ip));
  EXPECT(parse_endpoint("10.0.0.5:41000", e) && e.ip == 0x0A000005u && e.port == 41000);
  EXPECT(!parse_endpoint("10.0.0.5", e) && !parse_endpoint("10.0.0.5:0", e) && !parse_endpoint("10.0.0.5:65536", e) &&
         !parse_endpoint("example.invalid:1", e) && !parse_endpoint("10.0.0.5:12x", e));
  EXPECT(ip_is_private(0x0A000005u) && ip_is_private(0xAC100001u) && ip_is_private(0xAC1FFFFFu) && !ip_is_private(0xAC200001u));
  EXPECT(ip_is_private(0xC0A80101u) && ip_is_private(0xA9FE0101u) && ip_is_loopback(0x7F000001u));
  EXPECT(ip_is_shared(0x64400001u) && ip_is_shared(0x647FFFFFu) && !ip_is_shared(0x64800001u) && !ip_is_shared(0x643F0001u));
  EXPECT(ip_is_public(0xC6336407u) && !ip_is_public(0x64400001u) && !ip_is_public(0x7F000001u) && !ip_is_public(0xC0A80101u));
  EXPECT(!ip_is_public(0) && !ip_is_public(0xE0000001u) && !ip_is_public(0xFFFFFFFFu));

  // NAT-PMP: map UDP 40000 for an hour, and the router's answer giving 40001.
  uint8_t request[kNatPmpMapRequestSize];
  natpmp_write_map(40000, 40000, 3600, request);
  const uint8_t expected_request[12] = {0, 1, 0, 0, 0x9C, 0x40, 0x9C, 0x40, 0, 0, 0x0E, 0x10};
  EXPECT(std::memcmp(request, expected_request, sizeof request) == 0);
  uint8_t answer[16] = {0, 129, 0, 0, 0, 0, 0x12, 0x34, 0x9C, 0x40, 0x9C, 0x41, 0, 0, 0x0E, 0x10};
  uint16_t external = 0;
  uint32_t lease = 0;
  EXPECT(natpmp_read_map(answer, sizeof answer, 40000, external, lease) && external == 40001 && lease == 3600);
  EXPECT(!natpmp_read_map(answer, sizeof answer, 40002, external, lease));   // an answer about another port
  EXPECT(!natpmp_read_map(answer, 15, 40000, external, lease));
  answer[3] = 2;   // result code: refused
  EXPECT(!natpmp_read_map(answer, sizeof answer, 40000, external, lease));
  const uint8_t address[12] = {0, 128, 0, 0, 0, 0, 0x12, 0x34, 192, 0, 2, 33};
  EXPECT(natpmp_read_address(address, sizeof address, ip) && ip == 0xC0000221u);
  EXPECT(!natpmp_read_address(address, 11, ip) && !natpmp_read_address(answer, sizeof answer, ip));

  // UPnP: the search answer, the description, the control call.
  std::string url, path, type, control, body;
  EXPECT(ssdp_location("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                       "Location:  http://192.168.1.1:5000/rootDesc.xml \r\nSERVER: x\r\n\r\n", url));
  EXPECT(url == "http://192.168.1.1:5000/rootDesc.xml");
  EXPECT(!ssdp_location("HTTP/1.1 200 OK\r\nST: x\r\n\r\n", url));
  EXPECT(http_url(url, e, path) && e.ip == 0xC0A80101u && e.port == 5000 && path == "/rootDesc.xml");
  EXPECT(http_url("HTTP://192.168.1.1", e, path) && e.port == 80 && path == "/");
  EXPECT(!http_url("http://router.local/desc.xml", e, path) && !http_url("https://192.168.1.1/desc.xml", e, path) &&
         !http_url("http://192.168.1.1/a b", e, path) && !http_url("http://0.0.0.0/x", e, path));
  int status = 0;
  EXPECT(http_split("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", status, body) && status == 200 && body == "hello");
  EXPECT(http_split("HTTP/1.1 500 Internal Server Error\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n4;x=y\r\ndefg\r\n0\r\n\r\n", status, body) &&
         status == 500 && body == "abcdefg");
  EXPECT(!http_split("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n", status, body) && !http_split("garbage\r\n\r\n", status, body));
  const std::string description =
      "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
      "<controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList><deviceList><device><serviceList>"
      "<service>\r\n<serviceType> urn:schemas-upnp-org:service:WANPPPConnection:1 </serviceType><controlURL>/ctl/PPPConn</controlURL></service>"
      "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/ctl/IPConn</controlURL>"
      "<eventSubURL>/evt/IPConn</eventSubURL></service></serviceList></device></deviceList></device></root>";
  EXPECT(upnp_find_control(description, type, control) && type == "urn:schemas-upnp-org:service:WANIPConnection:1" && control == "/ctl/IPConn");
  EXPECT(!upnp_find_control("<root><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
                            "<controlURL>/x</controlURL></service></root>", type, control));
  EXPECT(xml_value("<a><errorCode> 725 </errorCode></a>", "errorCode") == "725" && xml_value("<a></a>", "errorCode").empty());
  const std::string soap = upnp_soap(type, "AddPortMapping", {{"NewExternalPort", "40000"}, {"NewPortMappingDescription", "a<b&c"}});
  EXPECT(soap.find("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">") != std::string::npos);
  EXPECT(soap.find("<NewExternalPort>40000</NewExternalPort>") != std::string::npos);
  EXPECT(soap.find("<NewPortMappingDescription>a&lt;b&amp;c</NewPortMappingDescription></u:AddPortMapping>") != std::string::npos);

  // The mapper does nothing until asked, and stopping it when idle returns at once.
  PortMapper mapper;
  EXPECT(mapper.status().state == MapState::Idle);
  mapper.request(0);
  mapper.stop();
  EXPECT(mapper.status().state == MapState::Idle);
}

static void test_candidate_order() {
  // What a launcher adds to its half of the setup.
  PortMapping mapped;
  mapped.state = MapState::Mapped;
  mapped.external.ip = 0xC6336407u;   // 198.51.100.7
  mapped.external.port = 41001;
  NatEndpoint stun;
  stun.ip = 0xC6336407u;
  stun.port = 52000;
  std::vector<std::string> extra = extra_candidates(mapped, true, stun, 0);
  EXPECT(extra.size() == 2 && extra[0] == "198.51.100.7:41001" && extra[1] == "198.51.100.7:52000");
  // A router behind another router: its mapping is not reachable from outside, the STUN answer may be.
  mapped.external.ip = 0x64400009u;   // 100.64.0.9
  extra = extra_candidates(mapped, true, stun, 0);
  EXPECT(extra.size() == 1 && extra[0] == "198.51.100.7:52000");
  mapped.external.ip = 0xC0A80002u;   // 192.168.0.2
  EXPECT(extra_candidates(mapped, false, stun, 0xC6336407u).empty());
  // A router that mapped the port but did not say its address: the STUN address, else the one another launcher saw.
  mapped.external.ip = 0;
  extra = extra_candidates(mapped, false, stun, 0xCB007109u);   // 203.0.113.9
  EXPECT(extra.size() == 1 && extra[0] == "203.0.113.9:41001");
  EXPECT(extra_candidates(mapped, false, stun, 0).empty());
  extra = extra_candidates(mapped, true, stun, 0xCB007109u);
  EXPECT(extra.size() == 2 && extra[0] == "198.51.100.7:41001");
  // No mapping, and a STUN answer that is itself a carrier-shared address: nothing worth naming.
  stun.ip = 0x64400009u;
  EXPECT(extra_candidates(PortMapping(), true, stun, 0).empty());
  // The same port mapped and seen by STUN is named once.
  stun.ip = 0xC6336407u;
  stun.port = 41001;
  mapped.external.ip = 0xC6336407u;
  EXPECT(extra_candidates(mapped, true, stun, 0).size() == 1);

  // What the other launcher dials: the address it hears us from, the public extras, the LAN addresses.
  const std::vector<std::string> order = order_candidates(
      "203.0.113.9:41000",
      {"203.0.113.9:41000", "198.51.100.4:50000", "192.168.1.9:1", "198.51.100.5:1", "198.51.100.6:1"},
      {"192.168.1.20:41000", "10.0.0.5:41000", "not an address", "192.168.56.1:41000", "192.168.57.1:41000"});
  EXPECT(order.size() == 6);
  if (order.size() == 6) {
    EXPECT(order[0] == "203.0.113.9:41000" && order[1] == "198.51.100.4:50000" && order[2] == "198.51.100.5:1");
    EXPECT(order[3] == "192.168.1.20:41000" && order[4] == "10.0.0.5:41000" && order[5] == "192.168.56.1:41000");
  }
  // An older launcher names no extras: the list is what it always was.
  const std::vector<std::string> old = order_candidates("127.0.0.1:41000", {}, {"192.168.1.20:41000", "127.0.0.1:41000"});
  EXPECT(old.size() == 2 && old[0] == "127.0.0.1:41000" && old[1] == "192.168.1.20:41000");
  // Nothing usable in, nothing out; and never more than asked for.
  EXPECT(order_candidates("", {"0.0.0.0:5", "224.0.0.1:5", "1.2.3.4:0"}, {"x"}).empty());
  EXPECT(order_candidates("203.0.113.9:41000", {"198.51.100.4:50000"}, {"192.168.1.20:41000"}, 2).size() == 2);
}

static void test_port_prediction() {
  // Neighbours of the public candidates only: +1, +2, +3, -1, never a candidate, never one twice.
  const std::vector<Endpoint> candidates = {
      Endpoint{"203.0.113.7", 40000}, Endpoint{"192.168.1.5", 40000}, Endpoint{"198.51.100.2", 65535}, Endpoint{"203.0.113.7", 40001}};
  const std::vector<Endpoint> predicted = predicted_endpoints(candidates);
  const std::vector<Endpoint> expected = {
      Endpoint{"203.0.113.7", 40002}, Endpoint{"203.0.113.7", 40003}, Endpoint{"203.0.113.7", 39999},
      Endpoint{"198.51.100.2", 65534}, Endpoint{"203.0.113.7", 40004}};
  EXPECT(predicted.size() == expected.size());
  for (size_t i = 0; i < predicted.size() && i < expected.size(); ++i)
    EXPECT(predicted[i].address == expected[i].address && predicted[i].port == expected[i].port);
  // No router to guess on a LAN, on this PC, or inside a carrier's shared range; port 1 has no port below it.
  EXPECT(predicted_endpoints({Endpoint{"127.0.0.1", 40000}, Endpoint{"10.0.0.5", 40000}, Endpoint{"100.64.0.9", 40000},
                              Endpoint{"169.254.3.3", 40000}}).empty());
  EXPECT(predicted_endpoints({Endpoint{"203.0.113.7", 1}}).size() == 3);
  EXPECT(predicted_endpoints({Endpoint{"example.invalid", 40000}, Endpoint{"203.0.113.7", 0}}).empty());
  // Bounded however many public candidates there are.
  std::vector<Endpoint> many;
  for (int i = 1; i <= kMaxCandidates; ++i) many.push_back(Endpoint{"203.0.113." + std::to_string(i), 40000});
  const std::vector<Endpoint> capped = predicted_endpoints(many);
  EXPECT((int)capped.size() == kMaxPredicted && capped[0].address == "203.0.113.1" && capped[7].address == "203.0.113.2" && capped[7].port == 39999);
}

static void test_connect_diagnosis() {
  const auto now = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  const auto nothing = [](uint8_t, const uint8_t*, size_t) {};
  std::string error;
  // Nobody answers at all.
  {
    PeerConnector alone;
    std::vector<std::string> lines;
    alone.set_log([&lines](const char* line) { lines.push_back(line); });
    EXPECT(alone.open(0, &error));
    alone.set_limit_us(200000);
    EXPECT(alone.connect({Endpoint{"127.0.0.1", 9}}, now(), &error));
    EXPECT(wait_for([&] { alone.poll(now(), nothing); return alone.state() == ConnectState::Failed; }, 2000));
    EXPECT(alone.failure() == ConnectFailure::TimedOut && alone.diagnosis() == ConnectDiagnosis::NoReply);
    EXPECT(std::string(alone.failure_text()).find("no reply from any address") != std::string::npos);
    EXPECT(std::strlen(alone.failure_text()) <= 120);
    EXPECT(lines.size() == 2 && lines[0].find("no path after") != std::string::npos && lines[1] == alone.failure_text());
    EXPECT(alone.path_text().empty());
  }
  // The other player's address answers, but from a port nobody named: the sign of a router that
  // gives every destination its own port. One byte from a plain socket stands in for their punch.
  {
    PeerConnector alone;
    EXPECT(alone.open(0, &error));
    alone.set_limit_us(300000);
    EXPECT(alone.connect({Endpoint{"127.0.0.1", 9}}, now(), &error));
    const ENetSocket other = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    EXPECT(other != ENET_SOCKET_NULL);
    ENetAddress to;
    to.host = 0;
    const uint8_t loopback[4] = {127, 0, 0, 1};
    std::memcpy(&to.host, loopback, 4);
    to.port = alone.local_port();
    uint8_t zero = 0;
    ENetBuffer buffer;
    buffer.data = &zero;
    buffer.dataLength = 1;
    int sent = 0;
    EXPECT(wait_for([&] {
      if (sent < 5 && other != ENET_SOCKET_NULL) { enet_socket_send(other, &to, &buffer, 1); ++sent; }
      alone.poll(now(), nothing);
      return alone.state() == ConnectState::Failed;
    }, 2000));
    EXPECT(alone.diagnosis() == ConnectDiagnosis::PortChanges);
    EXPECT(std::string(alone.failure_text()).find("different port") != std::string::npos && std::strlen(alone.failure_text()) <= 120);
    if (other != ENET_SOCKET_NULL) enet_socket_destroy(other);
    // Every failure sentence fits the game's message box, and a new attempt starts clean.
    alone.close();
    EXPECT(alone.diagnosis() == ConnectDiagnosis::None);
  }
  // A path that comes up is named in the log, with which of the addresses it was.
  {
    PeerConnector a, b;
    std::vector<std::string> lines;
    a.set_log([&lines](const char* line) { lines.push_back(line); });
    EXPECT(a.open(0, &error) && b.open(0, &error));
    EXPECT(a.connect({Endpoint{"127.0.0.1", 9}, Endpoint{"127.0.0.1", b.local_port()}}, now(), &error));
    EXPECT(b.connect({}, now(), &error));   // only listens
    EXPECT(wait_for([&] { a.poll(now(), nothing); b.poll(now(), nothing); return a.connected() && b.connected(); }, 3000));
    bool named = false;
    for (const auto& line : lines) if (line.find("path up") != std::string::npos && line.find("address 2 of the 2 given, dialed from here") != std::string::npos) named = true;
    EXPECT(named);
  }
}

int main() {
  test_stun_codec();
  test_nat_addresses_and_mapping_text();
  test_candidate_order();
  test_port_prediction();
  test_descriptor_digest();
  test_security_packets();
  test_transport_parsing();
  test_handshake_and_match_state();
  test_wrong_expected_key();
  test_descriptor_mismatch();
  test_stall_rule();
  test_checksum_desync();
  test_lossy_two_thousand_frames();
  test_fake_game_states();
  test_time_sync_decisions();
  test_identity_file();
  test_loopback_sockets();
  test_connect_diagnosis();
  test_pump_thread_session();
  test_next_descriptor();
  test_transport_game_base();
  test_two_games();
  test_next_digest_mismatch();
  test_previous_revision_peer();
  test_bye_between_games();
  if (g_failures) { std::printf("mu_net_test: %d check(s) failed\n", g_failures); return 1; }
  std::printf("mu_net_test: all checks passed\n");
  return 0;
}
