// YouGame friends protocol and presentation state, shared with window-free tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace yougame::friends {
using json = nlohmann::json;
inline constexpr const char* kHint = "Leave empty to invite a YouGame friend";
inline constexpr const char* kWaiting = "Waiting for your friend...";
inline constexpr int kPickerTimeout = 600000;

// The game's Direct code field is Shift-JIS: ASCII letters and digits with the # as the full-width
// 0x81 0x94 (convert_connect_code_for_game), and the on-screen keyboard can also give full-width
// letters, digits or spaces. YouGame's search is UTF-8 JSON, so the code goes in as plain ASCII
// ("LAIN#504"): the raw bytes made the request's dump() throw type_error.316, shown on CSS.
// Anything outside that set returns false so the search fails with a readable message.
inline bool direct_code_from_game(const std::string& sj, std::string* out) {
  std::string code;
  for (size_t i = 0; i < sj.size(); ++i) {
    const auto c = static_cast<unsigned char>(sj[i]);
    if (c == 0) break;
    if (c < 0x80) {
      if (c >= 'a' && c <= 'z') code += static_cast<char>(c - 'a' + 'A');
      else if (c != ' ') code += static_cast<char>(c);
      continue;
    }
    if (i + 1 >= sj.size()) return false;
    const auto d = static_cast<unsigned char>(sj[++i]);
    if (c == 0x81 && d == 0x94) code += '#';
    else if (c == 0x81 && d == 0x40) continue;
    else if (c == 0x82 && d >= 0x4F && d <= 0x58) code += static_cast<char>('0' + d - 0x4F);
    else if (c == 0x82 && d >= 0x60 && d <= 0x79) code += static_cast<char>('A' + d - 0x60);
    else if (c == 0x82 && d >= 0x81 && d <= 0x9A) code += static_cast<char>('A' + d - 0x81);
    else return false;
  }
  for (const char ch : code)
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '#')) return false;
  if (out) *out = code;
  return true;
}

inline std::string text(const json& value, const char* key, size_t max) {
  if (!value.is_object()) return {};
  const auto it = value.find(key);
  if (it == value.end() || !it->is_string()) return {};
  const auto& s = it->get_ref<const std::string&>();
  if (s.empty() || s.size() > max * 4) return {};
  size_t count = 0;
  for (size_t i = 0; i < s.size();) {
    const auto lead = static_cast<unsigned char>(s[i++]);
    uint32_t cp = lead;
    int tails = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { cp &= 0x1F; tails = 1; }
    else if (lead >= 0xE0 && lead <= 0xEF) { cp &= 0x0F; tails = 2; }
    else if (lead >= 0xF0 && lead <= 0xF4) { cp &= 0x07; tails = 3; }
    else if (lead >= 0x80) return {};
    if (i + tails > s.size()) return {};
    for (int n = 0; n < tails; ++n) {
      const auto c = static_cast<unsigned char>(s[i++]);
      if ((c & 0xC0) != 0x80) return {};
      cp = (cp << 6) | (c & 0x3F);
    }
    if ((tails == 1 && cp < 0x80) || (tails == 2 && cp < 0x800) ||
        (tails == 3 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp < 32 || (cp >= 127 && cp <= 159) ||
        ++count > max) return {};
  }
  return s;
}
inline std::string room_code(const json& value, const char* key) {
  auto s = text(value, key, 16);
  if (s.size() < 4 || s.size() > 16) return {};
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return {};
  return s;
}
inline std::string room(const json& value) { return room_code(value, "room"); }
inline std::string picker_room(const json& value) { return room_code(value, "code"); }
inline bool empty_code(bool yougame, int mode, const std::string& code) {
  return yougame && mode == 2 && code.empty();
}
// The picker supplies the host code; invitees repeat it in the `room` field.
inline json ticket(const std::string& compatibility, const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  return {{"type", "mpTicket"}, {"players", 2}, {"mode", "singles"},
          {"room", code}, {"ranked", false},
          {"lobby", {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}}};
}
inline bool native_ready(bool yougame, int mode, const std::string& code, const std::string& invite) {
  return !(yougame && (!invite.empty() || empty_code(true, mode, code)));
}
// The ordinary Direct winner path locks in on START without another code/stage screen.
// A random first stage is already supplied by native_start_match, as for other searches.
inline constexpr int kFirstCssWinner = 1;
inline json picker() { return {{"type", "mpFriend"}, {"mode", "singles"}, {"players", 2}}; }
// Shared request path: no ticket can be sent before a valid picker answer (or invite).
template <class Send, class Cancelled>
std::string resolve_room(const std::string& invite, Send send, Cancelled cancelled) {
  if (cancelled()) throw std::runtime_error("Cancelled");
  const auto code = invite.empty() ? picker_room(send(picker(), kPickerTimeout))
                                   : room(json{{"room", invite}});
  if (cancelled()) throw std::runtime_error("Cancelled");
  if (code.empty()) throw std::runtime_error("Invalid YouGame friends room");
  return code;
}
template <class Send, class Cancelled, class Starting>
json request_ticket(const std::string& compatibility, const std::string& invite,
                    json& request, Send send, Cancelled cancelled, Starting starting) {
  const auto code = resolve_room(invite, send, cancelled);
  request = ticket(compatibility, code);
  starting(code);  // track our room before the page can push its own URL's join
  return send(request, 30000);
}
// Restrict scene changes to the initialized main menu or online CSS/stage/results menus.
// Every match (including local play), replay and other scene remains untouched.
inline bool menu_safe(int major, int minor) { return major == 1 || (major == 8 && (minor == 0 || minor == 1 || minor == 3)); }

class Events {
 public:
  void hello(const json& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hello_seen_) return;
    hello_seen_ = true;
    const auto code = room(value);
    if (pending_.empty() && code != current_room_) pending_ = code;  // a newer push wins over the launch invite
  }
  bool push(const json& value, uint64_t now) {
    const auto type = text(value, "type", 16);
    if (type == "join") {
      const auto code = room(value);
      if (code.empty()) return false;
      std::lock_guard<std::mutex> lock(mutex_);
      if (code == current_room_) return false;
      pending_ = code;
      return true;
    }
    if (type != "notice" || text(value, "kind", 16) != "invite") return false;
    const auto from = text(value, "from", 40), line = text(value, "text", 80);
    if (from.empty() || line.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    toast_ = from + " " + line;
    toast_until_ = now + 6000;
    return true;
  }
  std::string take_join(bool safe) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe) return {};
    return std::exchange(pending_, {});
  }
  std::string toast(uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now < toast_until_ ? toast_ : std::string();
  }
  void current_room(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_room_ = code;
    if (!code.empty() && pending_ == code) pending_.clear();
  }
  void picker_error(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    picker_error_ = std::move(error);
  }
  std::string picker_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return picker_error_;
  }
  void request_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (code_screen_ && code_empty_) { host_requested_ = true; picker_error_.clear(); }
  }
  bool take_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(host_requested_, false);
  }
  void code_empty(bool empty) { std::lock_guard<std::mutex> lock(mutex_); code_empty_ = empty; }
  bool code_empty() const { std::lock_guard<std::mutex> lock(mutex_); return code_empty_; }
  void code_screen(bool visible) {
    std::lock_guard<std::mutex> lock(mutex_);
    code_screen_ = visible;
    if (!visible) { host_requested_ = false; picker_error_.clear(); }
  }
  bool code_screen() const { std::lock_guard<std::mutex> lock(mutex_); return code_screen_; }
 private:
  mutable std::mutex mutex_;
  bool hello_seen_ = false, code_screen_ = false, code_empty_ = false, host_requested_ = false;
  std::string pending_, toast_, current_room_, picker_error_;
  uint64_t toast_until_ = 0;
};
inline Events events;
}  // namespace yougame::friends
