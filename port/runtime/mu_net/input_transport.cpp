// Builds and parses the input-channel messages, keeps both players' committed pads, and estimates ping and frame-clock offset.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_transport.h"
#include <algorithm>

namespace mu_net {

// ---------------------------------------------------------------- fault shim
uint32_t FaultLink::roll() {
  // xorshift32: small, and the same on every compiler, which is the point of a seeded test fault.
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return rng_;
}

bool FaultLink::send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) {
  if (reliable) return inner_.send(channel, data, size, true);
  if (plan_.blackout) { ++dropped; return true; }
  // Held packets count sends, not time: the result does not depend on how fast the test runs.
  std::vector<Held> due;
  for (size_t i = 0; i < held_.size();) {
    if (--held_[i].sends_left == 0) { due.push_back(std::move(held_[i])); held_.erase(held_.begin() + i); }
    else ++i;
  }
  bool ok = true;
  const uint32_t r = roll() % 1000;
  if (r < plan_.drop_per_1000) {
    ++dropped;
  } else if (r < plan_.drop_per_1000 + plan_.reorder_per_1000 && held_.size() < 64) {
    Held h;
    h.sends_left = 1 + roll() % (plan_.max_hold ? plan_.max_hold : 1);
    h.channel = channel;
    h.bytes.assign(data, data + size);
    held_.push_back(std::move(h));
    ++reordered;
  } else {
    ok = inner_.send(channel, data, size, false);
    if (roll() % 1000 < plan_.duplicate_per_1000) { inner_.send(channel, data, size, false); ++duplicated; }
  }
  for (auto& h : due) inner_.send(h.channel, h.bytes.data(), h.bytes.size(), false);
  return ok;
}

// ---------------------------------------------------------------- transport
void InputTransport::reset(int32_t frame_us, int32_t base) {
  *this = InputTransport();
  frame_us_ = frame_us > 0 ? frame_us : 16683;
  base_ = base > 0 ? base : 0;
}

bool InputTransport::commit(int32_t frame, const uint8_t pad[kPadWireSize]) {
  // Final and in order: a frame is committed exactly once, so what the peer simulates can never
  // be changed after the fact by a later call for the same frame.
  if (frame != local_latest_ + 1 || frame >= kGameFrameSpan) return false;   // a game never leaves its own range of wire numbers
  Slot& s = local_[index(frame)];
  s.frame = frame;
  std::memcpy(s.pad, pad, kPadWireSize);
  local_latest_ = frame;
  return true;
}

void InputTransport::set_checksum(int32_t frame, uint32_t checksum) {
  // Verbatim, every time: the pair that travels is the one the game gave with its newest pad.
  // Nothing here judges it (not even a zero), so the peer sees exactly what the game said.
  checksum_frame_ = frame;
  checksum_ = checksum;
}

void InputTransport::build_commit(uint64_t now_us, std::vector<uint8_t>& out) {
  out.clear();
  if (local_latest_ < 1) return;
  int32_t first = peer_ack_ + 1;
  // Everything acknowledged: the newest frame still goes out, alone. It carries the frame clock,
  // our acknowledgment and the checksum, and it is the heartbeat the peer's stall timer watches.
  if (first > local_latest_) first = local_latest_;
  if (local_latest_ - first + 1 > kMaxCommitHistory) first = local_latest_ - kMaxCommitHistory + 1;
  const int count = local_latest_ - first + 1;
  Writer w(out);
  w.u8(kMsgCommit);
  w.i32(base_ + local_latest_);
  w.u8((uint8_t)count);
  w.i32(checksum_frame_ > 0 ? base_ + checksum_frame_ : 0);   // 0 stays "none" in every game
  w.u32(checksum_);
  w.i32(base_ + remote_latest_);
  w.u32((uint32_t)now_us);
  static const uint8_t zero[kPadWireSize] = {};
  for (int32_t f = local_latest_; f >= first; --f) {
    const Slot& s = local_[index(f)];
    w.bytes(s.frame == f ? s.pad : zero, kPadWireSize);
  }
  clock_set_ = true;
  clock_frame_ = local_latest_;
  clock_us_ = now_us;
  ++stats_.commits_sent;
}

void InputTransport::build_ping(uint64_t now_us, std::vector<uint8_t>& out) const {
  out.clear();
  Writer w(out);
  w.u8(kMsgPing);
  w.u32((uint32_t)now_us);
}

bool InputTransport::local_pad(int32_t frame, uint8_t out[kPadWireSize]) const {
  const Slot& s = local_[index(frame)];
  if (frame < 1 || s.frame != frame) { std::memset(out, 0, kPadWireSize); return false; }
  std::memcpy(out, s.pad, kPadWireSize);
  return true;
}

bool InputTransport::remote_pad(int32_t frame, uint8_t out[kPadWireSize]) const {
  const Slot& s = remote_[index(frame)];
  if (frame < 1 || s.frame != frame) { std::memset(out, 0, kPadWireSize); return false; }
  std::memcpy(out, s.pad, kPadWireSize);
  return true;
}

bool InputTransport::on_commit(Reader& r, size_t size, uint64_t now_us, std::vector<uint8_t>& reply) {
  const int32_t wire_frame = r.i32();
  const int count = r.u8();
  const int32_t wire_checksum_frame = r.i32();
  const uint32_t checksum = r.u32();
  const int32_t wire_ack = r.i32();
  const uint32_t stamp = r.u32();
  // Every number is checked against what can be true before a byte of pad data is read: the
  // length must be exact, the history cannot reach before frame 1, and the peer cannot have
  // acknowledged a frame this side never sent.
  if (!r.ok || count < 1 || count > kMaxCommitHistory || size != kCommitHeader + (size_t)count * kPadWireSize ||
      wire_frame < count || wire_checksum_frame < 0 || wire_ack < 0) {
    ++stats_.malformed;
    return false;
  }
  // Numbered for another game of this session: a message still in flight from the game before, or
  // the first of the next game from a peer that got there first. Nothing of it is taken, not the
  // acknowledgment and not the checksum, and it is not answered; the peer sends its frames again.
  if (wire_frame <= base_ || wire_frame - base_ >= kGameFrameSpan || wire_ack < base_ || wire_ack - base_ >= kGameFrameSpan) {
    ++stats_.other_game;
    return true;
  }
  const int32_t frame = wire_frame - base_, ack = wire_ack - base_;
  const int32_t checksum_frame = wire_checksum_frame > base_ ? wire_checksum_frame - base_ : 0;
  if (frame < count || ack > local_latest_) {
    ++stats_.malformed;
    return false;
  }
  ++stats_.commits_received;
  if (ack > peer_ack_) peer_ack_ = ack;

  const int32_t oldest = frame - count + 1;
  const int32_t limit = floor_ + kMaxRemoteLead;
  bool fresh = false;
  if (oldest > remote_latest_ + 1) {
    // The history does not reach what we hold (the peer believes we have more than we do, which
    // only a stale acknowledgment in flight explains). Nothing is taken; our next message tells it.
    ++stats_.gaps;
  } else {
    const uint8_t* pads = r.data + kCommitHeader;
    for (int32_t f = oldest; f <= frame; ++f) {
      const uint8_t* pad = pads + (size_t)(frame - f) * kPadWireSize;   // newest first
      if (f <= remote_latest_) {
        const Slot& held = remote_[index(f)];
        if (held.frame == f && std::memcmp(held.pad, pad, kPadWireSize) != 0) ++stats_.conflicts;
        continue;   // the first commit of a frame stands
      }
      if (f > limit) { ++stats_.too_far_ahead; break; }
      Slot& s = remote_[index(f)];
      s.frame = f;
      std::memcpy(s.pad, pad, kPadWireSize);
      remote_latest_ = f;
      fresh = true;
    }
  }

  // The pair of the newest COMMIT replaces the one held, whatever it says. Only a reordered older
  // COMMIT is passed over, so the pair never steps back to one the peer has since replaced.
  if (frame >= newest_remote_seen_ && frame <= limit) {
    remote_checksum_frame_ = checksum_frame;
    remote_checksum_ = checksum;
  }
  if (frame > newest_remote_seen_ && frame <= limit) {
    newest_remote_seen_ = frame;
    // One sample per new frame, on its first arrival: when the peer sent frame F (now, less half
    // the round trip) against when this side sent its own latest frame. A late duplicate of an old
    // frame would only measure the network's delay, so it is not sampled.
    if (clock_set_) {
      int64_t offset = (int64_t)now_us - (int64_t)(ping_us_ / 2) - (int64_t)clock_us_ + (int64_t)frame_us_ * ((int64_t)clock_frame_ - frame);
      offset = std::max<int64_t>(-(1 << 30), std::min<int64_t>(1 << 30, offset));
      offsets_[offset_next_] = (int32_t)offset;
      offset_next_ = (offset_next_ + 1) % kTimeSyncSamples;
      if (offset_count_ < kTimeSyncSamples) ++offset_count_;
    }
  }
  if (fresh) {
    last_progress_us_ = now_us;
    Writer w(reply);
    w.u8(kMsgAck);
    w.i32(base_ + remote_latest_);
    w.u32(stamp);
    ++stats_.acks_sent;
  }
  return true;
}

bool InputTransport::on_message(const uint8_t* data, size_t size, uint64_t now_us, std::vector<uint8_t>& reply) {
  reply.clear();
  if (!data || size < 1 || size > kCommitHeader + (size_t)kMaxCommitHistory * kPadWireSize) { ++stats_.malformed; return false; }
  Reader r(data, size);
  const uint8_t type = r.u8();
  // A round trip over two seconds is not a measurement, it is a stale or invented stamp.
  auto take_round_trip = [&](uint32_t echoed) {
    const uint32_t rtt = (uint32_t)now_us - echoed;
    if (rtt <= 2000000u) { ping_us_ = rtt; has_ping_ = true; }
  };
  switch (type) {
    case kMsgCommit:
      return on_commit(r, size, now_us, reply);
    case kMsgAck: {
      const int32_t wire_ack = r.i32();
      const uint32_t echoed = r.u32();
      if (!r.done() || wire_ack < 0) break;
      if (wire_ack < base_ || wire_ack - base_ >= kGameFrameSpan) { ++stats_.other_game; return true; }   // as for a COMMIT
      const int32_t ack = wire_ack - base_;
      if (ack > local_latest_) break;
      if (ack > peer_ack_) peer_ack_ = ack;
      take_round_trip(echoed);
      ++stats_.acks_received;
      return true;
    }
    case kMsgPing: {
      const uint32_t stamp = r.u32();
      if (!r.done()) break;
      Writer w(reply);
      w.u8(kMsgPong);
      w.u32(stamp);
      return true;
    }
    case kMsgPong: {
      const uint32_t echoed = r.u32();
      if (!r.done()) break;
      take_round_trip(echoed);
      return true;
    }
    case kMsgSample:
      // Reserved. The length is checked so a build that sends them is held to the format, and the
      // content is dropped: only a COMMIT can ever move the confirmed-input watermark.
      if (size != kSampleSize) break;
      ++stats_.samples_ignored;
      return true;
    default:
      break;
  }
  ++stats_.malformed;
  return false;
}

int32_t InputTransport::time_offset_us() const {
  const int n = offset_count_;
  if (n <= 0) return 0;
  int32_t sorted[kTimeSyncSamples];
  std::copy(offsets_, offsets_ + n, sorted);
  std::sort(sorted, sorted + n);
  // The outer thirds are where a delayed packet or a hitch on either side lands.
  const int skip = n / 3, end = n - skip;
  int64_t sum = 0;
  for (int i = skip; i < end; ++i) sum += sorted[i];
  return (int32_t)(sum / (end - skip));
}

}  // namespace mu_net
