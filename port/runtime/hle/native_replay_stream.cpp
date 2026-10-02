// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_replay_stream.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>

namespace slippi {
namespace {
void put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(value >> shift));
}
int32_t frame_number(const uint8_t* payload) {
  const uint32_t bits = uint32_t(payload[0]) << 24 | uint32_t(payload[1]) << 16 |
                        uint32_t(payload[2]) << 8 | payload[3];
  int32_t result;
  std::memcpy(&result, &bits, sizeof result);
  return result;
}
}
bool NativeReplayStream::fail(const char* reason) {
  error_ = reason;
  return false;
}
bool NativeReplayStream::begin(const std::vector<uint8_t>& start,
                             const std::vector<uint8_t>& codes) {
  std::string mod_profile = std::move(mod_profile_);   // set for the match about to start
  const uint32_t feature_options = feature_options_, feature_options2 = feature_options2_,
                 feature_options3 = feature_options3_;
  *this = NativeReplayStream{};
  mod_profile_ = std::move(mod_profile);
  feature_options_ = feature_options;
  feature_options2_ = feature_options2;
  feature_options3_ = feature_options3;
  if (start.size() < 0x140 || start.size() > UINT16_MAX)
    return fail("invalid Game Start payload length");
  active_ = true;
  if (!append(0x36, start.data(), uint32_t(start.size()))) return false;
  if (codes.size() <= UINT16_MAX)
    return append(0x3D, codes.data(), uint32_t(codes.size()));
  // Slippi's splitter avoids truncating the 16-bit event length of large lists.
  if (codes.size() > 16u * 1024u * 1024u) return fail("Gecko list exceeds 16 MiB");
  for (size_t offset = 0; offset < codes.size(); offset += 512) {
    uint8_t part[516]{};
    const size_t size = std::min<size_t>(512, codes.size() - offset);
    std::memcpy(part, codes.data() + offset, size);
    part[512] = uint8_t(size >> 8); part[513] = uint8_t(size);
    part[514] = 0x3D; part[515] = offset + size == codes.size();
    if (!append(0x10, part, sizeof part)) return false;
  }
  return true;
}
bool NativeReplayStream::begin_slippi(const std::vector<uint8_t>& start,
                                      const std::vector<uint8_t>& codes, int64_t start_time) {
  std::string mod_profile = std::move(mod_profile_);   // set for the match about to start
  const uint32_t feature_options = feature_options_, feature_options2 = feature_options2_,
                 feature_options3 = feature_options3_;
  *this = NativeReplayStream{};
  mod_profile_ = std::move(mod_profile);
  feature_options_ = feature_options;
  feature_options2_ = feature_options2;
  feature_options3_ = feature_options3;
  if (start.size() < 0x140 || start.size() > UINT16_MAX)
    return fail("invalid Game Start payload length");
  if (codes.size() > UINT16_MAX) return fail("Gecko list exceeds the declared 16-bit size");
  slippi_ = true;
  start_time_ = start_time;
  last_frame_ = -123;   // Dolphin's value until the first post-frame event
  // SendGameInfo's command table (Recording/SendGameInfo.asm), in the order it writes it.
  table_ = {{0x36, uint16_t(start.size())}, {0x37, 66}, {0x38, 84}, {0x39, 6}, {0x3A, 12},
            {0x3B, 44}, {0x3C, 8}, {0x3D, uint16_t(codes.size())}, {0x10, 516}, {0x3F, 9},
            {0x40, 5}, {0x41, 8}};
  for (const auto& entry : table_) sizes_[entry.first] = entry.second;
  active_ = true;
  if (!append(0x36, start.data(), uint32_t(start.size()))) return false;
  // The list always goes through the message splitter, 512 bytes per part.
  for (size_t offset = 0; offset < codes.size() || offset == 0; offset += 512) {
    uint8_t part[516]{};
    const size_t size = std::min<size_t>(512, codes.size() - offset);
    if (size) std::memcpy(part, codes.data() + offset, size);
    part[512] = uint8_t(size >> 8); part[513] = uint8_t(size);
    part[514] = 0x3D; part[515] = offset + size == codes.size();
    if (!append(0x10, part, sizeof part)) return false;
    if (codes.empty()) break;
  }
  return true;
}
bool NativeReplayStream::append(uint8_t command, const uint8_t* payload, uint32_t size) {
  if (!active_ || ended_) return fail("event outside an active match");
  if (!error_.empty()) return false;
  if ((size && !payload) || size > UINT16_MAX || command == 0x35)
    return fail("invalid event payload");
  if (command == 0x36 && !events_.empty()) return fail("duplicate Game Start");
  const auto found = sizes_.find(command);
  if (found != sizes_.end() && found->second != size)
    return fail("event payload size changed within a match");
  if (slippi_ && found == sizes_.end()) return fail("event not in the declared command table");
  if (found == sizes_.end() && sizes_.size() >= 84)
    return fail("too many event types for a Slippi payload table");
  if (events_.size() + size + 1 > 256u * 1024u * 1024u)
    return fail("native recording exceeds 256 MiB");
  sizes_[command] = uint16_t(size);
  events_.push_back(command);
  if (size) events_.insert(events_.end(), payload, payload + size);
  if (slippi_) {
    // Dolphin's writer: the last frame and the character usage come from post-frame events.
    if (command == 0x38 && size >= 7) {
      last_frame_ = frame_number(payload);
      ++char_usage_[payload[4]][payload[6]];
    }
  } else if ((command == 0x38 || command == 0x3C) && size >= 4) {
    last_frame_ = frame_number(payload);
  }
  if (command == 0x39) ended_ = true;
  return true;
}
std::vector<uint8_t> NativeReplayStream::encode() const {
  if (!active_ || !error_.empty()) return {};
  std::vector<uint8_t> out = {'{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l'};
  std::vector<std::pair<uint8_t, uint16_t>> table = table_;
  if (!slippi_) table.assign(sizes_.begin(), sizes_.end());
  put32(out, uint32_t(2 + table.size() * 3 + events_.size()));
  out.push_back(0x35); out.push_back(uint8_t(1 + table.size() * 3));
  for (const auto& entry : table) {
    out.push_back(entry.first);
    out.push_back(uint8_t(entry.second >> 8)); out.push_back(uint8_t(entry.second));
  }
  out.insert(out.end(), events_.begin(), events_.end());
  if (slippi_) {
    // CEXISlippi::generateMetadata, as the Legacy writer (exi_slippi.cpp) reproduces it.
    auto text = [&out](const std::string& t) {
      out.push_back('U'); out.push_back(uint8_t(t.size())); out.insert(out.end(), t.begin(), t.end());
    };
    char stamp[32] = {};
    const std::time_t when = std::time_t(start_time_);
    std::tm tm{};
    gmtime_s(&tm, &when);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    text("metadata"); out.push_back('{');
    text("startAt"); out.push_back('S'); text(stamp);
    text("lastFrame"); out.push_back('l'); put32(out, uint32_t(last_frame_));
    text("players"); out.push_back('{');
    for (const auto& player : char_usage_) {
      text(std::to_string(player.first)); out.push_back('{');
      text("names"); out.push_back('{'); out.push_back('}');
      text("characters"); out.push_back('{');
      for (const auto& usage : player.second) {
        text(std::to_string(usage.first)); out.push_back('l'); put32(out, usage.second);
      }
      out.push_back('}'); out.push_back('}');
    }
    out.push_back('}');
    text("playedOn"); out.push_back('S'); text("dolphin");
    if (!mod_profile_.empty()) { text("modProfile"); out.push_back('S'); text(mod_profile_); }
    if (feature_options_) { text("muOptions"); out.push_back('l'); put32(out, feature_options_); }
    if (feature_options2_) { text("muOptions2"); out.push_back('l'); put32(out, feature_options2_); }
    if (feature_options3_) { text("muOptions3"); out.push_back('l'); put32(out, feature_options3_); }
    out.push_back('}'); out.push_back('}');
    return out;
  }
  const char metadata[] = "U\x08metadata{U\x09lastFramel";
  out.insert(out.end(), metadata, metadata + sizeof metadata - 1);
  put32(out, uint32_t(last_frame_));
  if (!mod_profile_.empty()) {
    const std::string key = "modProfile";
    out.push_back('U'); out.push_back(uint8_t(key.size())); out.insert(out.end(), key.begin(), key.end());
    out.push_back('S'); out.push_back('U'); out.push_back(uint8_t(mod_profile_.size()));
    out.insert(out.end(), mod_profile_.begin(), mod_profile_.end());
  }
  if (feature_options_) {
    const std::string key = "muOptions";
    out.push_back('U'); out.push_back(uint8_t(key.size())); out.insert(out.end(), key.begin(), key.end());
    out.push_back('l'); put32(out, feature_options_);
  }
  if (feature_options2_) {
    const std::string key = "muOptions2";
    out.push_back('U'); out.push_back(uint8_t(key.size())); out.insert(out.end(), key.begin(), key.end());
    out.push_back('l'); put32(out, feature_options2_);
  }
  if (feature_options3_) {
    const std::string key = "muOptions3";
    out.push_back('U'); out.push_back(uint8_t(key.size())); out.insert(out.end(), key.begin(), key.end());
    out.push_back('l'); put32(out, feature_options3_);
  }
  out.push_back('}'); out.push_back('}');
  return out;
}
} // namespace slippi
