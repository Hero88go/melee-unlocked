// Own-UDP policy. No socket I/O: shared by the live path and fake-I/O tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace yougame::udp {
struct Port {
  bool set = false, valid = true;
  uint16_t value = 0;
};
inline bool decimal(const char* s, uint32_t max, uint32_t& out) {
  out = 0;
  if (!s || !*s) return false;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9' || out > (max - uint32_t(*s - '0')) / 10) return false;
    out = out * 10 + uint32_t(*s - '0');
  }
  return out <= max;
}
inline Port port(const char* s) {
  if (!s) return {};
  uint32_t n;
  const bool valid = decimal(s, 65535, n) && n != 0;
  return {true, valid, valid ? uint16_t(n) : uint16_t(0)};
}
// Strict dotted decimal, including rejection of ambiguous leading-zero octets.
inline bool private_ipv4(const char* s) {
  if (!s) return false;
  uint32_t octets[4]{};
  for (int i = 0; i < 4; ++i) {
    const char* begin = s;
    while (*s >= '0' && *s <= '9') ++s;
    if (s == begin || s - begin > 3 || (s - begin > 1 && *begin == '0')) return false;
    for (const char* p = begin; p != s; ++p) octets[i] = octets[i] * 10 + (*p - '0');
    if (octets[i] > 255 || (i < 3 ? *s != '.' : *s != '\0')) return false;
    if (i < 3) ++s;
  }
  return octets[0] == 10 || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
         (octets[0] == 192 && octets[1] == 168);
}
template<class LocalIP, class Log>
std::string lan_candidate(const char* override_ip, uint16_t bound_port, LocalIP local_ip, Log log) {
  const bool valid = private_ipv4(override_ip);
  if (override_ip && !valid) log("yougame: host LAN address ignored (not a private IPv4)");
  const std::string ip = valid ? override_ip : local_ip();
  return ip.empty() ? "" : ip + ":" + std::to_string(bound_port);
}
inline std::string port_error(Port p, const char* reason) {
  // Invalid input may contain arbitrary text or an address: never echo it.
  return "yougame: own UDP port " + (p.valid ? std::to_string(p.value) : "invalid") + " unavailable (" + reason + ")";
}
template<class Bind, class Log>
bool bind_port(Port p, uint16_t fallback, Bind bind, Log log) {
  if (!p.valid) { log(port_error(p, "invalid decimal port").c_str()); return false; }
  if (bind(p.set ? p.value : fallback)) return true;
  if (p.set) log(port_error(p, "bind failed").c_str());
  return false; // exactly one attempt; no ephemeral-port retry
}
struct Endpoint {
  uint32_t host; // opaque network-order IPv4 (same representation as ENetAddress.host)
  uint16_t port; // host order
  bool operator==(const Endpoint& b) const { return host == b.host && port == b.port; }
};
class DropCounter {
 public:
  template<class Log> void drop(uint64_t now, Log log) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++drops_;
    if (!logged_ || now - logged_at_ >= 10000) {
      log(("yougame: dropped " + std::to_string(drops_) + " datagrams from unknown senders").c_str());
      drops_ = 0; logged_ = true; logged_at_ = now;
    }
  }
 private:
  std::mutex mutex_;
  uint64_t drops_ = 0, logged_at_ = 0;
  bool logged_ = false;
};
// The socket is handed from STUN/punch to ENet; logs retain one process-wide
// ten-second budget across that handoff and across consecutive searches.
inline DropCounter own_udp_drops;
class Ingress {
 public:
  explicit Ingress(DropCounter* counter = nullptr) : counter_(counter ? counter : &local_counter_) {}
  void allow(Endpoint p) { if (std::find(peers_.begin(), peers_.end(), p) == peers_.end()) peers_.push_back(p); }
  void clear() { peers_.clear(); }
  template<class Log> bool accept(Endpoint from, uint64_t now, Log log) {
    if (std::find(peers_.begin(), peers_.end(), from) != peers_.end()) return true;
    counter_->drop(now, log);
    return false;
  }
 private:
  std::vector<Endpoint> peers_;
  DropCounter local_counter_;
  DropCounter* counter_;
};
// Only one solicited STUN transaction is open at a time. Source validation
// precedes even the transaction-ID read; attributes are parsed only afterwards.
class StunIngress {
 public:
  explicit StunIngress(DropCounter* counter = nullptr) : counter_(counter ? counter : &local_counter_) {}
  void open(Endpoint server, const uint8_t* request, uint64_t deadline) {
    server_ = server;
    std::memcpy(id_, request + 8, sizeof id_);
    deadline_ = deadline;
    open_ = true;
  }
  void close() { open_ = false; }
  template<class Log> bool accept(Endpoint from, const uint8_t* data, size_t size, uint64_t now, Log log) {
    if (now >= deadline_) close();
    if (open_ && from == server_ && size >= 20 && std::memcmp(data + 8, id_, sizeof id_) == 0) return true;
    counter_->drop(now, log);
    return false;
  }
 private:
  Endpoint server_{};
  uint8_t id_[12]{};
  uint64_t deadline_ = 0;
  bool open_ = false;
  DropCounter local_counter_;
  DropCounter* counter_;
};
struct Probe { uint8_t type, player; uint64_t token; };
inline bool probe(const uint8_t* data, size_t size, Probe& out) {
  if (size != 14 || std::memcmp(data, "YGP2", 4) != 0 || data[4] > 1 || data[13] < 1 || data[13] > 6) return false;   // players 1-6 (a six-seat Friends room)
  out.type = data[4]; out.player = data[13];
  std::memcpy(&out.token, data + 5, 8);
  return true;
}
// A solicited STUN response still needs its complete header, matching transaction,
// exact message length and bounded/padded attributes before any attribute reads.
inline bool stun_reply(const uint8_t* data, size_t size, const uint8_t* request, std::string& out) {
  if (size < 20 || data[0] != 1 || data[1] != 1 || std::memcmp(data + 4, request + 4, 16) != 0) return false;
  const size_t body = (size_t(data[2]) << 8) | data[3];
  if (body % 4 || body != size - 20) return false;
  std::string mapped;
  for (size_t pos = 20; pos < size;) {
    if (size - pos < 4) return false;
    const unsigned type = (unsigned(data[pos]) << 8) | data[pos + 1];
    const size_t len = (size_t(data[pos + 2]) << 8) | data[pos + 3];
    pos += 4;
    const size_t padded = (len + 3) & ~size_t(3);
    if (padded > size - pos) return false;
    const uint8_t* v = data + pos;
    if ((type == 0x0020 || type == 0x0001) && len == 8 && v[1] == 1) {
      uint16_t p = (uint16_t(v[2]) << 8) | v[3];
      uint8_t ip[4] = {v[4], v[5], v[6], v[7]};
      if (type == 0x0020) { p ^= 0x2112; for (int i = 0; i < 4; ++i) ip[i] ^= request[4 + i]; }
      if (!p) return false;
      mapped = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." +
               std::to_string(ip[3]) + ":" + std::to_string(p);
    }
    pos += padded;
  }
  if (mapped.empty()) return false;
  out = mapped; return true;
}
} // namespace yougame::udp
