// The revision schedule of a replay: which entries of the file the player saw and which were
// re-simulations inside a rollback. An online match's replay holds every simulated revision of
// every frame in file order, so the frame numbers alone (one per Frame Start event) tell the story:
// an entry was shown when its frame exceeds every earlier one, and hidden otherwise. A rollback of
// depth d before frame N reads ..., N-1 (shown), N-d ... N-1 (hidden, d entries), N (shown).
// Pure code, no host services, so the unit test links it alone.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace replay_revisions {

struct Entry {
  int32_t frame;
  bool shown;              // false: re-simulated inside one tick, never displayed by itself
  uint8_t hidden_before;   // shown: hidden entries since the previous shown one (rollback depth, 255 at most); hidden: 0
};

// One rollback.
struct Span {
  int32_t first_frame, last_frame;   // the re-simulated frames, both ends included
  int depth;                         // how many: last_frame - first_frame + 1
  size_t position;                   // file position of the shown entry that followed
};

struct Schedule {
  static constexpr size_t npos = (size_t)-1;

  bool valid = false;
  std::string error;              // why not, in plain words; empty when valid
  std::vector<Entry> entries;     // file order; empty when not valid
  std::vector<Span> rollbacks;    // file order
  int32_t first_frame = 0, last_frame = 0;   // the shown range; every frame in it was shown once
  size_t shown_count = 0;
  // Hidden entries at the file's end with no shown entry after them (the game closed inside a
  // rollback). They are in `entries` but in no Span and in no hidden_before.
  size_t trailing_hidden = 0;

  // The last file position holding `frame` (its final revision), or npos.
  size_t final_position(int32_t frame) const;
  // The file position where `frame` was first shown, or npos.
  size_t first_shown_position(int32_t frame) const;

  // How to get from the entry at `position` to the one at `position + 1`:
  //   Normal    both are shown and the next is the following frame: simulate it and present it.
  //   Rollback  the entry at `position` is shown and the next one's frame is not greater: restore
  //             the game to the start of frame `*rollback_target` (the next entry's frame), then
  //             simulate that entry unseen.
  //   Hidden    the entry at `position` is hidden: the next one follows it with no restore, being
  //             either the next frame of the same re-simulation (hidden again) or the shown frame
  //             that ends it. `entries[position + 1].shown` says which, so the caller simulates it
  //             and presents it only when that is set.
  //   End       there is no entry at `position + 1`, or the schedule is not valid.
  // So one rollback reads Rollback once, then Hidden once per remaining hidden entry and once more
  // for the shown entry that closes it; a new rollback can only start from a shown entry.
  // `*rollback_target` is written for Rollback only.
  enum class Next { Normal, Rollback, Hidden, End };
  Next next_kind(size_t position, int32_t* rollback_target = nullptr) const;

  // Per frame from first_frame, for the two lookups above.
  std::vector<size_t> final_positions, shown_positions;
};

// Builds the schedule from the frame numbers in file order. The first entry sets the first frame.
// Not valid (the caller falls back to ordinary playback) when the sequence is empty, when a shown
// frame is not the highest so far plus 1, when a re-simulation starts before the first frame or
// covers more than `max_rollback` frames, or when it skips a frame or stops before the highest
// frame so far (the entry after that one is the next shown frame). A file that ends inside a
// re-simulation is accepted: see trailing_hidden. No repeated frames (an offline replay) is valid
// with no rollbacks.
Schedule build(const std::vector<int32_t>& frames_in_file_order, int max_rollback = 7);

}  // namespace replay_revisions
