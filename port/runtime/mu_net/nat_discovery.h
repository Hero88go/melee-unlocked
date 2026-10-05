// Help with home routers for the launcher's lobby socket: what the socket looks like from outside (STUN), a port mapping asked of the router (NAT-PMP, UPnP), and the candidate list built from both.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This file stands alone: it uses no other mu_net header, no ENet and no key code, so the launcher
// can compile nat_discovery.cpp by itself. Nothing in it runs unless the launcher's lobby asks, and
// the lobby is closed while a game runs: none of this happens during an offline game or a match.
//
// Every answer found here is only a place to send handshake datagrams to. Who is at that place is
// decided by the session handshake (the expected identity key), never by anything in this file.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mu_net {

// ---------------------------------------------------------------- addresses
// An IPv4 address here is the number a.b.c.d reads as: (a << 24) | (b << 16) | (c << 8) | d.
struct NatEndpoint {
  uint32_t ip = 0;
  uint16_t port = 0;
};
inline bool operator==(const NatEndpoint& a, const NatEndpoint& b) { return a.ip == b.ip && a.port == b.port; }
inline bool operator!=(const NatEndpoint& a, const NatEndpoint& b) { return !(a == b); }

inline bool ip_is_loopback(uint32_t ip) { return (ip >> 24) == 127; }
// 10/8, 172.16/12, 192.168/16 and the link-local 169.254/16.
inline bool ip_is_private(uint32_t ip) {
  return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE;
}
// 100.64/10: what a provider gives a customer who shares a public address with others (carrier-grade NAT).
inline bool ip_is_shared(uint32_t ip) { return (ip >> 22) == 0x191; }
// Something a datagram can be sent to: not 0.x, not multicast, not the reserved top block.
inline bool ip_is_usable(uint32_t ip) { return (ip >> 24) != 0 && (ip >> 28) < 0xE; }
inline bool ip_is_public(uint32_t ip) {
  return ip_is_usable(ip) && !ip_is_loopback(ip) && !ip_is_private(ip) && !ip_is_shared(ip);
}

bool parse_ip(const std::string& text, uint32_t& ip);                  // "a.b.c.d", digits and dots only
bool parse_endpoint(const std::string& text, NatEndpoint& endpoint);   // "a.b.c.d:port", port 1 to 65535
std::string ip_text(uint32_t ip);
std::string endpoint_text(const NatEndpoint& endpoint);

// ---------------------------------------------------------------- STUN (RFC 5389, Binding only)
// The launcher sends a Binding Request from its lobby socket to a public STUN server and reads, in
// the answer, the address and port its router gave that socket. The game later binds the same
// local port, so that is the mapping the other player has to reach. No attribute is sent, no
// authentication is used and the answer's integrity attributes are not checked: an answer is
// matched by its 96-bit random transaction id alone, and what it says is only ever a candidate.
using StunId = std::array<uint8_t, 12>;
constexpr size_t kStunRequestSize = 20;
constexpr size_t kStunMaxResponse = 576;
void stun_write_request(const StunId& id, uint8_t out[kStunRequestSize]);
// True for a datagram with a STUN header: the two top bits clear, the magic cookie, and a length
// field that is a multiple of four and exactly the rest of the datagram.
bool stun_looks_like(const uint8_t* data, size_t size);
enum class StunResult : uint8_t {
  NotStun,            // not a STUN message (or longer than kStunMaxResponse)
  OtherTransaction,   // a STUN message that does not answer `id`
  Malformed,          // answers `id` but an attribute runs past the end, or it is not a Binding answer
  NoAddress,          // a Binding error, or a success without a usable IPv4 mapped address
  Mapped,
};
StunResult stun_read_response(const uint8_t* data, size_t size, const StunId& id, NatEndpoint& mapped);

constexpr int kMaxStunServers = 4;
// The servers to ask, as "host:port". `configured` is the player's own list (comma separated,
// ":3478" added where no port is given); null or empty gives the default list; "off" gives none.
// Never more than kMaxStunServers; an entry that is not a plain host name is left out.
std::vector<std::string> stun_server_list(const char* configured);

// Whether the router gives the socket one outside port for every destination (the punch works) or
// a different one per destination (the other player cannot know which port to send to).
enum class NatMapping : uint8_t { Unknown, Stable, PerDestination };

// One round of requests on a socket the caller owns. Nothing here touches the network: the caller
// sends request(i) to server i and hands every received datagram to on_datagram().
class StunProbe {
 public:
  bool begin(int servers);                                 // new transaction ids; false when the system has no random bytes
  int servers() const { return count_; }
  const uint8_t* request(int server) const;                // kStunRequestSize bytes; null for a server not begun
  // True when the datagram is a STUN message and so not for anyone else. An answer to one of this
  // round's requests is recorded once.
  bool on_datagram(const uint8_t* data, size_t size);
  int answers() const;
  bool mapped(NatEndpoint& out) const;                     // the first server (in order) that answered
  // Unknown with fewer than two answers. The servers must be at different addresses for this to
  // mean anything (NameLookup removes doubles).
  NatMapping mapping() const;

 private:
  int count_ = 0;
  StunId ids_[kMaxStunServers]{};
  uint8_t requests_[kMaxStunServers][kStunRequestSize]{};
  bool answered_[kMaxStunServers]{};
  NatEndpoint mapped_[kMaxStunServers]{};
};

// Host names to addresses on a thread of its own: a lookup can block for seconds, and the lobby
// must not wait for it. At most kMaxStunServers names; the same address is given once.
class NameLookup {
 public:
  void start(const std::vector<std::string>& host_ports);   // returns at once; a second start replaces the first
  bool started() const { return state_ != nullptr; }
  bool done() const;
  std::vector<NatEndpoint> results() const;                 // empty until done, and when nothing resolved

 private:
  struct State;
  std::shared_ptr<State> state_;
};

// ---------------------------------------------------------------- port mapping: the parts that need no network
// NAT-PMP (RFC 6886). All integers big-endian.
constexpr size_t kNatPmpMapRequestSize = 12;
void natpmp_write_map(uint16_t internal_port, uint16_t external_port, uint32_t lifetime_s, uint8_t out[kNatPmpMapRequestSize]);
// The answer to a UDP map request for `internal_port`: 16 bytes, result code 0.
bool natpmp_read_map(const uint8_t* data, size_t size, uint16_t internal_port, uint16_t& external_port, uint32_t& lifetime_s);
bool natpmp_read_address(const uint8_t* data, size_t size, uint32_t& ip);   // the answer to the two-byte address request

// UPnP Internet Gateway Device.
// The LOCATION header of an SSDP answer (at most 2048 bytes are looked at, the URL at most 256).
bool ssdp_location(const std::string& response, std::string& url);
// "http://a.b.c.d[:port][/path]" only: no names (nothing is looked up), no other scheme.
bool http_url(const std::string& url, NatEndpoint& host, std::string& path);
// Status and body of a whole HTTP response; a chunked body is put together.
bool http_split(const std::string& response, int& status, std::string& body);
// The text between <tag> and </tag>, without surrounding white space; "" when absent or over 256 bytes.
std::string xml_value(const std::string& xml, const std::string& tag);
// The WAN connection service of a device description and its control URL (as written there).
bool upnp_find_control(const std::string& xml, std::string& service_type, std::string& control_url);
std::string upnp_soap(const std::string& service_type, const std::string& action,
                      const std::vector<std::pair<std::string, std::string>>& arguments);

// ---------------------------------------------------------------- port mapping: the worker
enum class MapState : uint8_t { Idle, Working, Mapped, Failed };
struct PortMapping {
  MapState state = MapState::Idle;
  std::string method;          // "NAT-PMP" or "UPnP" once mapped
  uint16_t internal_port = 0;
  NatEndpoint external;        // the router's outside port; ip is 0 when the router did not say
  uint32_t lease_s = 0;        // 0: until removed
  std::string detail;          // one sentence for a log; never an address
};

// Asks the router of this network to send a UDP port to this PC. Everything happens on a thread
// of its own with short timeouts (NAT-PMP under a second, UPnP a few more), so a caller never
// waits; a router that knows neither simply leaves the state at Failed. The mapping is renewed at
// half its lease and removed by stop().
class PortMapper {
 public:
  PortMapper();
  ~PortMapper();   // stop()
  PortMapper(const PortMapper&) = delete;
  PortMapper& operator=(const PortMapper&) = delete;
  // Returns at once. The same port again does nothing; another port replaces the mapping; 0 removes it.
  void request(uint16_t udp_port);
  PortMapping status() const;
  // Removes the mapping and ends the thread. Waits for it: at once when nothing is in flight, about
  // four seconds at the worst (the step in flight, then the removal). Call it from one thread only,
  // the one that calls request().
  void stop();

 private:
  struct State;
  std::unique_ptr<State> state_;
};
// The one mapper of the process (the launcher's): it outlives the lobby, which is closed and
// opened again around every match while the mapping has to stay.
PortMapper& port_mapper();

// ---------------------------------------------------------------- candidates
constexpr size_t kMaxExtraCandidates = 2;
// What a launcher adds to its half of a match setup (the "ext" field), best first, each as
// "a.b.c.d:port" on a public address:
//   1. the mapped port, when the router mapped one and its outside address is public (taken from
//      the router, else from STUN, else `seen_ip`, which is how another launcher saw this one);
//   2. the STUN answer, when it is a public address.
// A mapping on a router whose own outside address is private or carrier-shared is left out: a
// second router stands in front of it and the port is not open there.
std::vector<std::string> extra_candidates(const PortMapping& mapping, bool have_stun, const NatEndpoint& stun, uint32_t seen_ip);
// The addresses a game dials, in order, never more than `max`, never one twice:
//   1. `seen`: where this launcher hears the other one from (the one path known to work);
//   2. the other side's "ext" entries, public addresses only, at most kMaxExtraCandidates;
//   3. the other side's own (LAN) addresses.
// An entry that is not "a.b.c.d:port" is left out.
std::vector<std::string> order_candidates(const std::string& seen, const std::vector<std::string>& extra,
                                          const std::vector<std::string>& lan, size_t max = 6);

}  // namespace mu_net
