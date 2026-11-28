// Room relay: the match's ENet datagrams over the room's own msg relay, for an app whose links helper
// could not start (directAttach/directOpen answered no_direct, or never answered). yg_enet_socket.c's
// relay backend hands each outgoing datagram to Relay::queue; a sender thread flushes about every
// 16 ms as ONE room msg with no `to` (Rooms relays it to the match's other seats):
//   {t:"msg", d:{k:"mu.relay", s:<my port>, p:[[<to port>,"<base64>"],...]}}
// split so no msg passes 12 KiB, and paced to Rooms' per-socket limits (120 msg/s, 256 KiB/s): at most
// 60 msg/s and 160 KiB/s here, the rest of the socket's budget left to the room's own traffic. A datagram
// over budget is dropped like UDP loss; ENet resends what is reliable. Arriving frames give each entry
// addressed to this seat to the backend under the link of the sender's seat. Seats are in-game ports
// (1-4): they survive a room reconnect, a connection id may not.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yougame_ranked.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yougame::room_relay {
using ranked::json;
constexpr const char* kKind = "mu.relay";
constexpr const char* kLog = "yougame: no direct links helper, using the room relay";
constexpr unsigned kLink0 = 0xFE00;          // yg_enet_socket.h YG_RELAY_LINK0
constexpr unsigned kMaxLinks = 64;           // YG_RELAY_MAX
constexpr int kFlushMs = 16;
constexpr double kMsgsPerSec = 60, kMsgBurst = 6;
constexpr double kBytesPerSec = 160.0 * 1024, kByteBurst = 32.0 * 1024;
constexpr size_t kMaxFrame = 12 * 1024;      // one room msg, envelope included; Rooms takes 16 KiB
constexpr size_t kMaxQueued = 512;           // datagrams waiting for a flush; the oldest goes first
constexpr size_t kMaxDatagram = 1200;

inline std::string b64(const void* data, size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto* p = static_cast<const unsigned char*>(data);
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    out += t[v >> 18 & 63];
    out += t[v >> 12 & 63];
    out += i + 1 < n ? t[v >> 6 & 63] : '=';
    out += i + 2 < n ? t[v & 63] : '=';
  }
  return out;
}
// Strict: padded, no whitespace, at most `max` bytes out.
inline bool unb64(const std::string& s, std::string& out, size_t max = kMaxDatagram) {
  out.clear();
  if (s.empty() || s.size() % 4 || s.size() / 4 * 3 > max + 2) return false;
  const auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  for (size_t i = 0; i < s.size(); i += 4) {
    const bool last = i + 4 == s.size();
    const int a = val(s[i]), b = val(s[i + 1]);
    const int c = s[i + 2] == '=' && last ? -2 : val(s[i + 2]);
    const int d = s[i + 3] == '=' && last ? -2 : val(s[i + 3]);
    if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2)) return false;
    out += (char)(a << 2 | b >> 4);
    if (c >= 0) out += (char)((b & 15) << 4 | c >> 2);
    if (d >= 0) out += (char)((c & 3) << 6 | d);
  }
  return out.size() <= max;
}

// Local link ids for the other seats: ports ascending (the ring's seat order), kLink0 upward. Each side
// numbers its own links; the wire names seats, never links.
inline std::map<int, unsigned> links_for(int me, std::vector<int> ports) {
  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  std::map<int, unsigned> out;
  unsigned next = kLink0;
  for (int p : ports)
    if (p != me && p >= 1 && p <= 6 && next < kLink0 + kMaxLinks) out[p] = next++;   // seats 1-6 (Triples)
  return out;
}

struct Datagram { int to = 0; std::string data; };

// One flush: room msgs of at most kMaxFrame bytes, no more than `max_msgs` of them; what does not fit
// stays queued for the next flush (the queue's own bound drops the oldest).
inline std::vector<json> frames(int me, std::deque<Datagram>& queue, size_t max_msgs) {
  std::vector<json> out;
  // {"t":"msg","d":{"k":"mu.relay","s":4,"p":[]}} plus slack for the JSON numbers
  constexpr size_t kEnvelope = 64;
  while (!queue.empty() && out.size() < max_msgs) {
    json p = json::array();
    size_t size = kEnvelope;
    while (!queue.empty()) {
      const auto& g = queue.front();
      const size_t entry = (g.data.size() + 2) / 3 * 4 + 8;   // [4,"..."],
      if (!p.empty() && size + entry > kMaxFrame) break;
      p.push_back(json::array({g.to, b64(g.data.data(), g.data.size())}));
      size += entry;
      queue.pop_front();
    }
    out.push_back({{"t", "msg"}, {"d", {{"k", kKind}, {"s", me}, {"p", std::move(p)}}}});
  }
  return out;
}

inline bool is_frame(const json& m) {
  if (!m.is_object() || ranked::str(m, "t") != "msg") return false;
  const auto d = m.find("d");
  return d != m.end() && d->is_object() && ranked::str(*d, "k") == kKind;
}

// The entries of a frame addressed to `me`, with the sender's seat. False for a malformed frame.
inline bool entries_for(const json& m, int me, int& from, std::vector<std::string>& out) {
  out.clear();
  if (!is_frame(m)) return false;
  const json& d = m["d"];
  const auto s = d.find("s"), p = d.find("p");
  if (s == d.end() || !s->is_number_integer() || p == d.end() || !p->is_array()) return false;
  from = s->get<int>();
  if (from < 1 || from > 6 || from == me) return false;
  for (const auto& e : *p) {
    if (!e.is_array() || e.size() != 2 || !e[0].is_number_integer() || !e[1].is_string()) continue;
    if (e[0].get<int>() != me) continue;
    std::string bytes;
    if (unb64(e[1].get<std::string>(), bytes) && !bytes.empty()) out.push_back(std::move(bytes));
  }
  return true;
}

class Bucket {
 public:
  Bucket(double rate, double burst) : rate_(rate), burst_(burst), tokens_(burst) {}
  void refill(uint64_t now) {
    if (last_) tokens_ = std::min(burst_, tokens_ + rate_ * double(now - last_) / 1000.0);
    last_ = now;
  }
  bool take(double n) { if (tokens_ < n) return false; tokens_ -= n; return true; }
  double tokens() const { return tokens_; }
 private:
  double rate_, burst_, tokens_;
  uint64_t last_ = 0;
};

// One match's relay. `send` writes one room msg (false when the socket can't take it); `deliver` hands
// an arriving datagram to the backend (yg_enet_relay_in). Owners stop it before the socket goes away.
class Relay {
 public:
  using Send = std::function<bool(const json&)>;
  using Deliver = std::function<void(unsigned link, const std::string& bytes)>;
  Relay(int me, std::map<int, unsigned> links, Send send, Deliver deliver,
        std::function<uint64_t()> clock = [] {
          return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
        })
      : me_(me), links_(std::move(links)), send_(std::move(send)), deliver_(std::move(deliver)), clock_(std::move(clock)) {
    for (const auto& [port, link] : links_) ports_[link] = port;
  }
  ~Relay() { stop(); }
  Relay(const Relay&) = delete;
  Relay& operator=(const Relay&) = delete;
  int me() const { return me_; }
  const std::map<int, unsigned>& links() const { return links_; }
  void start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable() || stopped_) return;
    thread_ = std::thread([this] { run(); });
  }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
      queue_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
  }
  // A new socket for the sends, or none: returns once no send through the old one is in flight.
  void rebind(Send send) { std::lock_guard<std::mutex> lock(send_mutex_); send_ = std::move(send); }
  // From the backend's callback (ENet's thread): never blocks on the socket.
  void queue(unsigned link, const void* data, size_t n) {
    const auto it = ports_.find(link);
    if (it == ports_.end() || !n || n > kMaxDatagram) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return;
    if (queue_.size() >= kMaxQueued) { queue_.pop_front(); ++dropped_; }
    queue_.push_back({it->second, std::string(static_cast<const char*>(data), n)});
  }
  // A frame from the room. True when it was a relay frame (consumed, whether or not it was for us).
  bool receive(const json& m) {
    int from = 0;
    std::vector<std::string> entries;
    if (!entries_for(m, me_, from, entries)) return is_frame(m);
    const auto link = links_.find(from);
    if (link == links_.end()) return true;
    for (const auto& e : entries) { deliver_(link->second, e); ++received_; }
    return true;
  }
  // One flush now (the thread calls it every kFlushMs; tests call it directly).
  void flush() {
    std::vector<json> out;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const uint64_t now = clock_();
      msgs_.refill(now);
      bytes_.refill(now);
      const size_t allowed = (size_t)std::max(0.0, msgs_.tokens());
      if (!allowed || queue_.empty()) return;
      out = frames(me_, queue_, std::min<size_t>(allowed, 2));
      for (auto it = out.begin(); it != out.end();) {
        const double size = (double)it->dump().size();
        if (!bytes_.take(size)) { dropped_ += (*it)["d"]["p"].size(); it = out.erase(it); continue; }
        msgs_.take(1);
        ++it;
      }
    }
    // Held through the sends: rebind() (a socket going away) waits for one in flight.
    std::lock_guard<std::mutex> lock(send_mutex_);
    for (const auto& m : out) {
      if (send_ && send_(m)) ++sent_;
      else dropped_ += m["d"]["p"].size();
    }
  }
  uint64_t sent() const { return sent_; }
  uint64_t received() const { return received_; }
  uint64_t dropped() const { return dropped_; }

 private:
  void run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
      cv_.wait_for(lock, std::chrono::milliseconds(kFlushMs), [this] { return stopped_; });
      if (stopped_) break;
      lock.unlock();
      flush();
      lock.lock();
    }
  }
  const int me_;
  const std::map<int, unsigned> links_;
  std::map<unsigned, int> ports_;
  Send send_;
  Deliver deliver_;
  std::function<uint64_t()> clock_;
  std::mutex mutex_, send_mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::deque<Datagram> queue_;
  bool stopped_ = false;
  Bucket msgs_{kMsgsPerSec, kMsgBurst}, bytes_{kBytesPerSec, kByteBurst};
  std::atomic<uint64_t> sent_{0}, received_{0}, dropped_{0};
};

}  // namespace yougame::room_relay
