// Offline-testable search lifetime and retry policies.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace yougame::search_policy {
inline constexpr const char* kLinkRetry = "We couldn't connect you two. Looking for another opponent...";
inline constexpr const char* kLinkFailed = "Could not connect to your opponent directly";
// Shown under the search for a while when this run first opens its own UDP port on Windows: that bind is
// when Windows Firewall asks, and the prompt alone doesn't say why a game wants the network.
inline constexpr const char* kFirewallNote = "Windows may ask whether Melee Unlocked can use the network.\n"
                                            "Choose Allow so other players can reach you.";
inline constexpr uint64_t kFirewallNoteMs = 20000;
// A search starting this soon after a failed connect is the automatic re-search: it opens on the retry line.
// Later (the player came back to it) it opens on the plain search line.
inline constexpr uint64_t kRetryLineMs = 30000;
// Own UDP (a seat started outside YouGame: no relay) failed twice with the same opponent: the retry line
// plus what the player can do. set_status shows it in place of kLinkRetry while the hint holds.
inline constexpr const char* kOwnUdpRetry = "We couldn't connect you two. Looking for another opponent...\n"
                                           "Your firewall or network may be blocking direct play.\n"
                                           "Allow Melee Unlocked through your firewall.";
class LinkFailures {
 public:
  // Connection ids, seats, display names and connect codes can change between
  // attempts. The roster's account id is stable; id is the legacy roster key.
  bool failed(const nlohmann::json& participants, const std::string& me) {
    std::set<std::string> opponents;
    for (const auto& p : participants) {
      if (p.value("connectionId", "") == me) continue;
      const auto key = [](const nlohmann::json& p, const char* field) {
        const auto it = p.find(field);
        return it != p.end() && it->is_string() ? it->get<std::string>() : std::string();
      };
      auto id = key(p, "user");
      if (id.empty()) id = key(p, "id");
      if (!id.empty()) opponents.insert(id);
    }
    bool repeated = false;
    for (const auto& id : opponents) {
      auto& count = failures_[id];
      if (count < 2) ++count;
      repeated |= count == 2;
    }
    return repeated;
  }
  bool repeated() const {
    return std::any_of(failures_.begin(), failures_.end(), [](const auto& f) { return f.second >= 2; });
  }
  void successful() { failures_.clear(); }
 private:
  std::map<std::string, unsigned> failures_;
};
class Generation {
 public:
  uint64_t begin() { return ++value_; }
  bool current(uint64_t epoch) const { return epoch != 0 && epoch == value_.load(); }
  uint64_t value() const { return value_.load(); }
 private:
  std::atomic<uint64_t> value_{0};
};
inline uint16_t blend_port(uint16_t configured, uint32_t random) {
  const auto port = uint16_t(41000 + random % 10000);
  return port == configured ? uint16_t(port == 50999 ? 41000 : port + 1) : port;
}
inline uint16_t native_port(uint16_t configured, uint16_t avoid, uint32_t random) {
  return configured && configured != avoid ? configured : blend_port(avoid, random);
}
inline uint64_t no_show_deadline(bool vanilla, bool queued, int mode, bool friends, uint64_t now) {
  return vanilla && queued && mode == 1 && !friends ? now + 30000 : 0;
}
inline bool no_show_expired(uint64_t deadline, bool arrived, uint64_t now) {
  return deadline && !arrived && now >= deadline;
}
// What a search says when YouGame kept answering "busy" for the whole retry window: short enough for the
// Doubles panel's note (47 letters).
inline constexpr const char* kBusyGaveUp = "YouGame is busy. Try again in a minute.";
class BusyRetry {
 public:
  // A passing refusal is asked again, shared by every ticket in this search: "busy" is the site's, "notHere" the
  // local channel host's (no page to ask yet, retryAfter 2), and any HTTP 503 (a lookup the site has not finished).
  // The waits back off 2, 4, then 8 s (the answer's own retryAfter when it is longer), until about a minute has
  // gone by; then gave_up() and the search ends with kBusyGaveUp.
  static constexpr int kMaxBackoff = 8, kWindow = 60;
  static bool transient(const nlohmann::json& answer) {
    if (!answer.is_object() || answer.value("ok", false)) return false;
    const auto code = answer.find("code");
    if (code != answer.end() && code->is_string() && (*code == "busy" || *code == "notHere")) return true;
    const auto status = answer.find("status");
    return status != answer.end() && status->is_number_integer() && *status == 503;
  }
  int delay(const nlohmann::json& answer) {
    if (!transient(answer)) return 0;
    const auto read = [](const nlohmann::json& value) -> int {
      if (value.is_number_integer() && value >= 1 && value <= 86400) return value.get<int>();
      if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (!text.empty() && text.size() <= 5 && text.find_first_not_of("0123456789") == std::string::npos)
          return std::stoi(text);
      }
      return 0;
    };
    int seconds = answer.count("retryAfter") ? read(answer["retryAfter"]) : 0;
    if (!seconds && answer.count("headers") && answer["headers"].is_object()) {
      const auto& headers = answer["headers"];
      if (headers.count("Retry-After")) seconds = read(headers["Retry-After"]);
      else if (headers.count("retry-after")) seconds = read(headers["retry-after"]);
    }
    seconds = std::max(seconds, std::min(2 << std::min(tries_, 2), kMaxBackoff));
    if (gave_up_ || waited_ + seconds > kWindow) { gave_up_ = true; return 0; }
    ++tries_;
    waited_ += seconds;
    return seconds;
  }
  int tries() const { return tries_; }
  int waited() const { return waited_; }
  bool gave_up() const { return gave_up_; }
 private:
  int tries_ = 0, waited_ = 0;
  bool gave_up_ = false;
};
template<class Clock, class Pause, class Cancel>
bool wait(uint64_t ms, Clock clock, Pause pause, Cancel cancelled) {
  const auto until = clock() + ms;
  while (clock() < until) {
    if (cancelled()) return false;
    pause((int)std::min<uint64_t>(50, until - clock()));
  }
  return !cancelled();
}
// An assigned casual room has no decline command. Joining and leaving uses the Worker's ordinary
// leave handling (rooms/src/index.ts:4493); queue cancel (1178) cannot release an assigned seat.
template<class Open, class Leave>
void decline_room(Open open, Leave leave) {
  if (open()) leave();
}
} // namespace yougame::search_policy
