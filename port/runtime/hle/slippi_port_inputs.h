// Human ports and their owning machines. Transport, ACKs and clocks stay per machine;
// confirmed input history and rollback stay per port.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "slippi_netplay_receive.h"
#include <algorithm>

namespace slippi {
inline constexpr uint8_t kMachinePadMessage = 0xE5;
// 2: Slippi 3.7.0's pads, PAD_WIRE_SIZE bytes each (the inputs and the RNG bits byte). A version 1
// sender (8-byte pads) is refused, as it plays a different simulation anyway.
inline constexpr uint8_t kMachinePadVersion = 2;
inline constexpr size_t kDropHistoryMax = 64;   // SLIPPI_DROP_REPORT_INPUT_COUNT

struct PortInputs {
  int main = 0, humans = 2;
  std::array<int, PLAYER_COUNT_MAX> owner{{0,1,2,3,4,5}};
  std::deque<std::unique_ptr<Pad>> pads[PLAYER_COUNT_MAX];
  std::deque<std::unique_ptr<Pad>> partner_send; // ACK retention is independent of the rollback frontier
  int port(int remote) const { return remote >= main ? remote + 1 : remote; }
  int index(int p) const { return p > main ? p - 1 : p; }
  int partner(int p) const {
    for (int i = 0; i < humans; ++i) if (i != p && owner[i] == p) return i;
    return -1;
  }
  bool virtual_local(int remote) const { return owner[port(remote)] == main; }
  template<class Active> bool active(int p, Active is_active) const {
    return p >= 0 && p < humans && (owner[p] == main || is_active(owner[p]));
  }
  // The owning machine inserts exact confirmed pads before the reply is read. It never predicts
  // its partner: both pads have the same target frame and delay, and survive rollback together.
  void local_partner(const Pad& pad) {
    const int p = partner(main);
    if (p < 0) return;
    auto& q = pads[p];
    if (q.empty() || q.front()->frame < pad.frame) q.push_front(std::make_unique<Pad>(pad));
    if (partner_send.empty() || partner_send.front()->frame < pad.frame) partner_send.push_front(std::make_unique<Pad>(pad));
  }
  RemotePadOutput local_output(int remote, int32_t frame, int limit) const {
    RemotePadOutput out;
    const int p = port(remote);
    out.player_idx = uint8_t(p); out.checksum_primary = false;
    const auto& queue = pads[p];
    if (!queue.empty()) { out.checksum_frame = queue.front()->checksum_frame; out.checksum = queue.front()->checksum; }
    // Read back from the requested frame, excluding future delayed pads. Starting at the oldest
    // retained frame can otherwise predict our own partner when the rollback window is full.
    for (const auto& pad : queue) {
      if (pad->frame > frame) continue;
      if (out.data.empty()) out.latest_frame = pad->frame;
      out.data.insert(out.data.end(),pad->buf,pad->buf + PAD_FULL_SIZE);
      if (int(out.data.size()) >= limit * PAD_FULL_SIZE) break;
    }
    return out;
  }
  static void u32(std::vector<uint8_t>& out, uint32_t n) {
    for (int shift : {24,16,8,0}) out.push_back(uint8_t(n >> shift));
  }
  static uint32_t u32(const uint8_t* b) { return uint32_t(b[0])<<24 | uint32_t(b[1])<<16 | uint32_t(b[2])<<8 | b[3]; }
  // Fixed, versioned header followed by newest-first, interleaved main/partner pads. Even a
  // single-pad sender in a paired match uses this message; old 0x80 packets are never accepted.
  std::vector<uint8_t> packet(const std::deque<std::unique_ptr<Pad>>& main_pads) const {
    if (main_pads.empty()) return {};
    const int second = partner(main);
    const auto& head = *main_pads.front();
    std::vector<uint8_t> out{kMachinePadMessage,'M','U','L',kMachinePadVersion,uint8_t(main),uint8_t(second < 0 ? 255 : second)};
    u32(out,head.frame); u32(out,head.checksum_frame); u32(out,head.checksum);
    const size_t count = std::min<size_t>(main_pads.size(),128);
    out.push_back(uint8_t(count));
    size_t copied = 0;
    for (const auto& pad : main_pads) {
      if (copied++ == count) break;
      out.insert(out.end(),pad->buf,pad->buf + PAD_WIRE_SIZE);
      if (second >= 0) {
        const auto& q = partner_send;
        const auto it = std::find_if(q.begin(),q.end(),[&](const auto& p) { return p->frame == pad->frame; });
        if (it == q.end()) return {}; // never send a main frame without its partner
        out.insert(out.end(),(*it)->buf,(*it)->buf + PAD_WIRE_SIZE);
      }
    }
    return out;
  }
  void trim_send(int32_t oldest) {
    while (!partner_send.empty() && partner_send.back()->frame < oldest) partner_send.pop_back();
  }
  struct Received { bool ok = false; int32_t frame = 0, checksum_frame = 0; uint32_t checksum = 0; };
  // history (by port, may be null) also gets every pad taken, newest first, for player drop reports.
  Received receive(const uint8_t* b, size_t n, int sender, std::deque<Pad>* history = nullptr) {
    if (n < 20 || b[0] != kMachinePadMessage || b[1] != 'M' || b[2] != 'U' || b[3] != 'L' ||
        b[4] != kMachinePadVersion || sender < 0 || sender >= humans || owner[sender] != sender || sender == main || b[5] != sender)
      return {};
    const int second = partner(sender);
    if (b[6] != (second < 0 ? 255 : second)) return {};
    const int count = b[19], stride = PAD_WIRE_SIZE * (second < 0 ? 1 : 2);
    if (!count || count > 128 || n != size_t(20 + count * stride)) return {};
    const int32_t frame = int32_t(u32(b+7));
    if (frame < count || frame <= 0) return {};
    // Validate both histories before changing either. A shared ACK covers both queues.
    for (int p : {sender,second}) {
      if (p < 0) continue;
      const int head = pads[p].empty() ? 0 : pads[p].front()->frame;
      if (frame > head && int64_t(frame) - head > count) return {};
    }
    for (int which = 0; which < (second < 0 ? 1 : 2); ++which) {
      auto& q = pads[which ? second : sender];
      const int head = q.empty() ? 0 : q.front()->frame;
      const int p = which ? second : sender;
      for (int64_t f = std::max<int64_t>(int64_t(head) + 1, int64_t(frame) - count + 1); f <= frame; ++f) {
        const uint8_t* pad = b+20+(frame-f)*stride+which*PAD_WIRE_SIZE;
        q.push_front(std::make_unique<Pad>(int32_t(f),pad));
        if (history) {
          history[p].emplace_front(int32_t(f),pad);
          if (history[p].size() > kDropHistoryMax) history[p].pop_back();
        }
      }
    }
    return {true,frame,int32_t(u32(b+11)),u32(b+15)};
  }
};
} // namespace slippi
