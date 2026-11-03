// Window-free receive rules shared by NetplayClient and its regression test.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "slippi_net.h"

namespace slippi::receive {

// Caller holds selection_mutex_, timing_mutex_ and pad_mutex_. Confirmed pads
// can arrive and be ACKed before our first frame, even before this selection.
// Only a transition out of a started game owns stale inputs to discard.
inline void Selections(const PlayerSelections& incoming, PlayerSelections& merged, bool& running,
    std::deque<std::unique_ptr<Pad>>& pads, host::early_input::Buffer& early) {
  merged.Merge(incoming);
  if (running) {
    pads.clear();
    early.clear();
  }
  running = false;
}

enum class PadError { None, TooSmall, TooMany };
struct PadResult { int64_t inputs; PadError error; };

// Preserve the receive path's size checks and frame ordering. Header parsing, checksums,
// timing and ACK emission remain in NetplayClient::OnData.
inline PadResult PadInputs(int32_t frame, const uint8_t* data, size_t size,
    std::deque<std::unique_ptr<Pad>>& pads) {
  constexpr int offset = 14;
  const int32_t head = pads.empty() ? 0 : pads.front()->frame;
  const int64_t count = (int64_t)frame - head;
  if (offset + count * PAD_DATA_SIZE > (int64_t)size) return {count, PadError::TooSmall};
  if (count > 128) return {count, PadError::TooMany};
  for (int64_t i = count - 1; i >= 0; --i)
    pads.push_front(std::make_unique<Pad>((int32_t)(frame - i), &data[offset + i * PAD_DATA_SIZE]));
  return {count, PadError::None};
}

} // namespace slippi::receive
