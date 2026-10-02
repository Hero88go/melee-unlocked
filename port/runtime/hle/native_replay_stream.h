// Native Slippi event stream, independent of the console EXI device.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace slippi {
class NativeReplayStream {
 public:
  // Starts a fresh match. Payloads exclude the command byte. The Gecko list is
  // required even when empty: modern Slippi readers wait for this event.
  bool begin(const std::vector<uint8_t>& game_start,
             const std::vector<uint8_t>& gecko_codes);
  // A recording laid out as Slippi's recording codes and Dolphin's writer make it: the command
  // table SendGameInfo declares (every event it can send, in its order), the Gecko list always
  // through the message splitter, and Dolphin's metadata (start time, last frame, per port the
  // frames played by each character, playedOn). start_time is the UTC second the game started.
  bool begin_slippi(const std::vector<uint8_t>& game_start,
                    const std::vector<uint8_t>& gecko_codes, int64_t start_time);
  bool append(uint8_t command, const uint8_t* payload, uint32_t size);
  std::vector<uint8_t> encode() const;
  const std::string& error() const { return error_; }
  bool active() const { return active_; }
  bool ended() const { return ended_; }
  int64_t start_time() const { return start_time_; }   // begin_slippi only, else 0   // the Game End event arrived: the file is complete
  int32_t last_frame() const { return last_frame_; }
  // Content identity of the mod layers the match ran with (metadata key "modProfile"); empty for
  // the retail game, which writes no key. Readers that do not know the key ignore it.
  void set_mod_profile(std::string fingerprint) { mod_profile_ = std::move(fingerprint); }
  // Gameplay options the match ran with (20XX TE features; metadata key "muOptions"); 0 writes no
  // key. Playback runs with exactly these, whatever the player has switched on now.
  void set_feature_options(uint32_t options) { feature_options_ = options; }
  void set_feature_options2(uint32_t options) { feature_options2_ = options; }   // "muOptions2"
  void set_feature_options3(uint32_t options) { feature_options3_ = options; }   // "muOptions3"
  // A replay viewer's jump back: the stream as it stood at an earlier point of the same match, so
  // the frames played again are recorded once.
  struct Mark {
    size_t events = 0;
    int32_t last_frame = -124;
    bool ended = false;
    std::map<uint8_t, std::map<uint8_t, uint32_t>> char_usage;
  };
  Mark mark() const { return Mark{events_.size(), last_frame_, ended_, char_usage_}; }
  void rewind(const Mark& to) {
    if (!active_ || to.events > events_.size()) return;
    events_.resize(to.events);
    last_frame_ = to.last_frame;
    ended_ = to.ended;
    char_usage_ = to.char_usage;
  }
 private:
  std::string mod_profile_;
  uint32_t feature_options_ = 0;
  uint32_t feature_options2_ = 0;
  uint32_t feature_options3_ = 0;
  bool fail(const char* reason);
  std::map<uint8_t, uint16_t> sizes_;
  std::vector<std::pair<uint8_t, uint16_t>> table_;   // Slippi's declared command table, in order
  std::map<uint8_t, std::map<uint8_t, uint32_t>> char_usage_;   // port -> character -> frames
  int64_t start_time_ = 0;
  bool slippi_ = false;
  std::vector<uint8_t> events_;
  std::string error_;
  int32_t last_frame_ = -124;
  bool active_ = false, ended_ = false;
};
} // namespace slippi
