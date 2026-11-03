// Pre-start inputs already ACKed to the sender must survive late selections.
#include "slippi_netplay_receive.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
using namespace slippi;
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) { std::printf("FAILED: %s\n", name); ++failures; }
}

PlayerSelections selection() {
  PlayerSelections s;
  s.player_idx = 1; s.character_id = 2; s.character_color = 3;
  s.is_character_selected = s.is_stage_selected = true;
  s.stage_id = 8; s.rng_offset = 0xDEDD;
  return s;
}

// SendSlippiPad retains the ACK frame and trims all older frames. This models
// the sender's newest-first wire payload; all eight bytes identify their frame.
std::vector<uint8_t> pad_packet(int32_t frame, int32_t ack) {
  std::vector<uint8_t> packet(14, 0);
  packet[0] = NP_MSG_SLIPPI_PAD;
  for (int i = 0; i < 4; ++i) packet[1 + i] = (uint8_t)((uint32_t)frame >> (24 - 8 * i));
  for (int32_t f = frame; f >= std::max(ack, 1); --f)
    packet.insert(packet.end(), PAD_DATA_SIZE, (uint8_t)f);
  return packet;
}

struct Receiver {
  PlayerSelections merged;
  bool running = false;
  int32_t sender_ack = 0;
  std::deque<std::unique_ptr<Pad>> pads;
  host::early_input::Buffer early;

  void select() { receive::Selections(selection(), merged, running, pads, early); }
  receive::PadResult pad(int32_t frame) {
    const auto packet = pad_packet(frame, sender_ack);
    const auto result = receive::PadInputs(frame, packet.data(), packet.size(), pads);
    // OnData emits PAD_ACK(frame, player_idx) for successfully copied new inputs;
    // the sender's PAD_ACK handler advances last_frame_acked_ monotonically.
    if (result.error == receive::PadError::None && result.inputs > 0)
      sender_ack = std::max(sender_ack, frame);
    return result;
  }
  void speculative(int32_t target) {
    uint8_t data[PAD_DATA_SIZE] = {}; data[0] = (uint8_t)target;
    early.add(target, data);
  }
  void start() {
    // StartSlippiGame resets timing/selections and unacknowledged MU predictions,
    // but preserves the confirmed remote queue for the game thread to read.
    running = false; merged.Reset(); early.clear();
  }
};

void late_prestart_selection() {
  Receiver r;
  check(r.pad(3).error == receive::PadError::None && r.sender_ack == 3,
        "pre-start confirmed frames 1..3 are received and ACKed");
  r.speculative(4);
  r.select();
  check(!r.running && r.merged.is_character_selected && r.merged.rng_offset == selection().rng_offset,
        "late selection merges before local match starts");
  check(r.pads.size() == 3 && r.early.entries().size() == 1,
        "pre-start selection preserves confirmed and speculative buffers");
  const auto resend = r.pad(3);
  check(resend.error == receive::PadError::None && resend.inputs == 0,
        "22-byte ACK-trimmed frame 3 resend accepted after late selection");
  r.start();
  check(r.pads.size() == 3, "confirmed pre-start inputs survive StartSlippiGame");
  r.running = true; // SendSlippiPad marks the local game started.
  check(r.pad(12).error == receive::PadError::None && r.sender_ack == 12 && r.pads.size() == 12,
        "94-byte frame 12 packet advances from preserved head 3");
  for (const auto& p : r.pads)
    check(p->buf[0] == p->frame && p->buf[7] == p->frame,
          "game can consume all confirmed frames in their original order");
}

void legacy_failure_proof() {
  Receiver r;
  r.pad(3);
  // The former unconditional selection reset loses acknowledged frames 1 and 2.
  r.pads.clear();
  const auto first = r.pad(3), later = r.pad(12);
  check(pad_packet(3, r.sender_ack).size() == 22 && 14 + first.inputs * PAD_DATA_SIZE == 38,
        "old reset: frame 3 has 22 bytes but receiver requires 38");
  check(pad_packet(12, r.sender_ack).size() == 94 && 14 + later.inputs * PAD_DATA_SIZE == 110,
        "old reset: frame 12 has 94 bytes but receiver requires 110");
  check(first.error == receive::PadError::TooSmall && later.error == receive::PadError::TooSmall &&
        r.sender_ack == 3 && r.pads.empty(), "old reset: refusal persists and ACK cannot advance");
}

void identical_rematch_clears_stale() {
  Receiver r;
  r.select(); r.start(); r.running = true;
  r.pad(90); r.speculative(91);
  // No game-end hook resets running; it remains true until the next selection.
  r.select(); // Exactly the same wire selections as the previous game.
  check(!r.running && r.merged.is_character_selected && r.pads.empty() && r.early.entries().empty(),
        "identical rematch selection clears both stale queues after a finished game");
  r.sender_ack = 0; // Opponent's StartSlippiGame resets its sender/ACK epoch.
  check(r.pad(3).error == receive::PadError::None && r.pads.size() == 3,
        "rematch accepts new frame numbering from 1");
  r.select();
  check(r.pads.size() == 3, "another pre-start selection preserves new rematch inputs");
}

void normal_order() {
  Receiver r;
  r.select(); r.start(); r.running = true;
  check(r.pad(3).error == receive::PadError::None && r.sender_ack == 3, "normal order accepts initial inputs");
  check(r.pad(4).inputs == 1 && r.sender_ack == 4 && r.pads.size() == 4,
        "normal order accepts sender's trimmed incremental packet");
  check(r.pad(2).inputs < 0 && r.sender_ack == 4 && r.pads.front()->frame == 4,
        "old packets neither ACK backwards nor replace newer inputs");
  Receiver limit;
  check(limit.pad(128).error == receive::PadError::None, "128-frame receive bound unchanged");
  Receiver oversized;
  check(oversized.pad(129).error == receive::PadError::TooMany && oversized.pads.empty() && oversized.sender_ack == 0,
        "oversized input batch is refused without ACK");
}
} // namespace

int main() {
  legacy_failure_proof();
  late_prestart_selection();
  identical_rematch_clears_stale();
  normal_order();
  if (!failures) std::puts("PASS: legacy_failure_proof, late_prestart_selection, identical_rematch_clears_stale, normal_order");
  return failures ? 1 : 0;
}
