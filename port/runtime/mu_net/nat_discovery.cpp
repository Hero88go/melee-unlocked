// STUN message coding, NAT-PMP and UPnP port mapping on a worker thread, and the candidate list of a match setup.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include "nat_discovery.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

// Named here so that no build file has to change for them (every target that uses this is MSVC).
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

namespace mu_net {
namespace {

using Clock = std::chrono::steady_clock;

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

const uint8_t kStunCookie[4] = {0x21, 0x12, 0xA4, 0x42};

char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
std::string lowered(const std::string& text) {
  std::string out = text;
  for (char& c : out) c = lower(c);
  return out;
}
std::string trimmed(const std::string& text) {
  size_t a = 0, b = text.size();
  while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r' || text[a] == '\n')) ++a;
  while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r' || text[b - 1] == '\n')) --b;
  return text.substr(a, b - a);
}
bool all_digits(const std::string& text) {
  if (text.empty()) return false;
  for (char c : text) if (c < '0' || c > '9') return false;
  return true;
}

}  // namespace

// ---------------------------------------------------------------- addresses
bool parse_ip(const std::string& text, uint32_t& ip) {
  if (text.size() > 15) return false;
  unsigned parts[4] = {};
  int part = 0, digits = 0;
  for (char c : text) {
    if (c >= '0' && c <= '9') {
      parts[part] = parts[part] * 10 + (unsigned)(c - '0');
      if (++digits > 3 || parts[part] > 255) return false;
    } else if (c == '.') {
      if (digits == 0 || ++part > 3) return false;
      digits = 0;
    } else {
      return false;
    }
  }
  if (part != 3 || digits == 0) return false;
  ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
  return true;
}

bool parse_endpoint(const std::string& text, NatEndpoint& endpoint) {
  const size_t colon = text.rfind(':');
  if (colon == std::string::npos || text.size() > 21) return false;
  const std::string port = text.substr(colon + 1);
  if (port.size() > 5 || !all_digits(port)) return false;
  const int number = std::atoi(port.c_str());
  uint32_t ip = 0;
  if (number < 1 || number > 65535 || !parse_ip(text.substr(0, colon), ip)) return false;
  endpoint.ip = ip;
  endpoint.port = (uint16_t)number;
  return true;
}

std::string ip_text(uint32_t ip) {
  char text[20];
  std::snprintf(text, sizeof text, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255), (unsigned)(ip & 255));
  return text;
}

std::string endpoint_text(const NatEndpoint& endpoint) { return ip_text(endpoint.ip) + ":" + std::to_string(endpoint.port); }

// ---------------------------------------------------------------- STUN
void stun_write_request(const StunId& id, uint8_t out[kStunRequestSize]) {
  out[0] = 0x00; out[1] = 0x01;   // Binding Request
  out[2] = 0x00; out[3] = 0x00;   // no attributes
  std::memcpy(out + 4, kStunCookie, 4);
  std::memcpy(out + 8, id.data(), id.size());
}

bool stun_looks_like(const uint8_t* data, size_t size) {
  if (!data || size < 20 || size > kStunMaxResponse) return false;
  if ((data[0] & 0xC0) != 0 || std::memcmp(data + 4, kStunCookie, 4) != 0) return false;
  const size_t length = be16(data + 2);
  return (length & 3) == 0 && 20 + length == size;
}

StunResult stun_read_response(const uint8_t* data, size_t size, const StunId& id, NatEndpoint& mapped) {
  if (!stun_looks_like(data, size)) return StunResult::NotStun;
  if (std::memcmp(data + 8, id.data(), id.size()) != 0) return StunResult::OtherTransaction;
  const uint16_t type = be16(data);
  if (type == 0x0111) return StunResult::NoAddress;   // Binding Error
  if (type != 0x0101) return StunResult::Malformed;   // not a Binding Success
  bool have_plain = false, have_xor = false;
  NatEndpoint plain, xored;
  size_t at = 20;
  // An attribute is at least four bytes, so the datagram's own size bounds the walk.
  while (size - at >= 4) {
    const uint16_t kind = be16(data + at);
    const size_t length = be16(data + at + 2);
    if (length > size - at - 4) return StunResult::Malformed;
    const uint8_t* value = data + at + 4;
    // MAPPED-ADDRESS, XOR-MAPPED-ADDRESS, and the number the latter had before the RFC was final.
    if ((kind == 0x0001 || kind == 0x0020 || kind == 0x8020) && length >= 8 && value[1] == 0x01) {
      NatEndpoint e;
      e.port = be16(value + 2);
      e.ip = be32(value + 4);
      if (kind == 0x0001) {
        if (!have_plain) { plain = e; have_plain = true; }
      } else {
        e.port = (uint16_t)(e.port ^ 0x2112);
        e.ip ^= 0x2112A442u;
        if (!have_xor) { xored = e; have_xor = true; }
      }
    }
    at += 4 + ((length + 3) & ~(size_t)3);
    if (at > size) return StunResult::Malformed;   // the padding ran past the end
  }
  // The XOR form first: some routers rewrite a plain address they find inside a datagram.
  const NatEndpoint* found = have_xor ? &xored : have_plain ? &plain : nullptr;
  if (!found || found->port == 0 || !ip_is_usable(found->ip)) return StunResult::NoAddress;
  mapped = *found;
  return StunResult::Mapped;
}

std::vector<std::string> stun_server_list(const char* configured) {
  std::vector<std::string> out;
  const std::string text = configured ? trimmed(configured) : std::string();
  if (text.empty()) {
    // Two operators, so that two answers come from two different addresses (NatMapping).
    out.push_back("stun.l.google.com:19302");
    out.push_back("stun.cloudflare.com:3478");
    out.push_back("stun1.l.google.com:19302");
    return out;
  }
  const std::string low = lowered(text);
  if (low == "off" || low == "none" || low == "0" || text.size() > 512) return out;
  size_t start = 0;
  while (start <= text.size() && out.size() < (size_t)kMaxStunServers) {
    size_t end = text.find(',', start);
    if (end == std::string::npos) end = text.size();
    std::string entry = trimmed(text.substr(start, end - start));
    start = end + 1;
    if (entry.empty()) continue;
    const size_t colon = entry.rfind(':');
    std::string host = colon == std::string::npos ? entry : entry.substr(0, colon);
    const std::string port = colon == std::string::npos ? std::string("3478") : entry.substr(colon + 1);
    if (host.empty() || host.size() > 80 || port.size() > 5 || !all_digits(port)) continue;
    const int number = std::atoi(port.c_str());
    if (number < 1 || number > 65535) continue;
    bool plain = true;
    for (char c : host) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
      if (!ok) { plain = false; break; }
    }
    if (!plain) continue;
    entry = host + ":" + std::to_string(number);
    if (std::find(out.begin(), out.end(), entry) == out.end()) out.push_back(entry);
  }
  return out;
}

bool StunProbe::begin(int servers) {
  count_ = 0;
  if (servers < 1) return false;
  if (servers > kMaxStunServers) servers = kMaxStunServers;
  for (int i = 0; i < servers; ++i) {
    if (BCryptGenRandom(nullptr, ids_[i].data(), (ULONG)ids_[i].size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    stun_write_request(ids_[i], requests_[i]);
    answered_[i] = false;
    mapped_[i] = NatEndpoint();
  }
  count_ = servers;
  return true;
}

const uint8_t* StunProbe::request(int server) const {
  return server >= 0 && server < count_ ? requests_[server] : nullptr;
}

bool StunProbe::on_datagram(const uint8_t* data, size_t size) {
  if (!stun_looks_like(data, size)) return false;
  for (int i = 0; i < count_; ++i) {
    NatEndpoint mapped;
    const StunResult result = stun_read_response(data, size, ids_[i], mapped);
    if (result == StunResult::OtherTransaction) continue;
    if (result == StunResult::Mapped && !answered_[i]) { answered_[i] = true; mapped_[i] = mapped; }
    break;
  }
  return true;
}

int StunProbe::answers() const {
  int n = 0;
  for (int i = 0; i < count_; ++i) if (answered_[i]) ++n;
  return n;
}

bool StunProbe::mapped(NatEndpoint& out) const {
  for (int i = 0; i < count_; ++i) if (answered_[i]) { out = mapped_[i]; return true; }
  return false;
}

NatMapping StunProbe::mapping() const {
  int first = -1;
  bool second = false;
  for (int i = 0; i < count_; ++i) {
    if (!answered_[i]) continue;
    if (first < 0) { first = i; continue; }
    second = true;
    if (mapped_[i] != mapped_[first]) return NatMapping::PerDestination;
  }
  return second ? NatMapping::Stable : NatMapping::Unknown;
}

// ---------------------------------------------------------------- NAT-PMP
void natpmp_write_map(uint16_t internal_port, uint16_t external_port, uint32_t lifetime_s, uint8_t out[kNatPmpMapRequestSize]) {
  out[0] = 0;   // version
  out[1] = 1;   // map UDP
  out[2] = 0; out[3] = 0;
  put16(out + 4, internal_port);
  put16(out + 6, external_port);
  put32(out + 8, lifetime_s);
}

bool natpmp_read_map(const uint8_t* data, size_t size, uint16_t internal_port, uint16_t& external_port, uint32_t& lifetime_s) {
  if (!data || size != 16 || data[0] != 0 || data[1] != 129 || be16(data + 2) != 0) return false;
  if (be16(data + 8) != internal_port) return false;
  external_port = be16(data + 10);
  lifetime_s = be32(data + 12);
  return true;
}

bool natpmp_read_address(const uint8_t* data, size_t size, uint32_t& ip) {
  if (!data || size != 12 || data[0] != 0 || data[1] != 128 || be16(data + 2) != 0) return false;
  ip = be32(data + 8);
  return true;
}

// ---------------------------------------------------------------- UPnP text
bool ssdp_location(const std::string& response, std::string& url) {
  const size_t limit = response.size() < 2048 ? response.size() : 2048;
  size_t start = 0;
  while (start < limit) {
    size_t end = response.find('\n', start);
    if (end == std::string::npos || end > limit) end = limit;
    const std::string line = response.substr(start, end - start);
    start = end + 1;
    if (line.size() < 10 || lowered(line.substr(0, 9)) != "location:") continue;
    const std::string value = trimmed(line.substr(9));
    if (value.size() < 8 || value.size() > 256) return false;
    url = value;
    return true;
  }
  return false;
}

bool http_url(const std::string& url, NatEndpoint& host, std::string& path) {
  if (url.size() < 8 || url.size() > 600 || lowered(url.substr(0, 7)) != "http://") return false;
  const size_t slash = url.find('/', 7);
  const std::string authority = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
  std::string rest = slash == std::string::npos ? std::string("/") : url.substr(slash);
  if (rest.size() > 512) return false;
  for (unsigned char c : rest) if (c <= 32 || c >= 127) return false;
  NatEndpoint e;
  if (authority.find(':') == std::string::npos) {
    if (!parse_ip(authority, e.ip)) return false;
    e.port = 80;
  } else if (!parse_endpoint(authority, e)) {
    return false;
  }
  if (!ip_is_usable(e.ip)) return false;
  host = e;
  path = rest;
  return true;
}

bool http_split(const std::string& response, int& status, std::string& body) {
  const size_t head_end = response.find("\r\n\r\n");
  if (head_end == std::string::npos || response.size() < 12 || response.compare(0, 5, "HTTP/") != 0) return false;
  const size_t space = response.find(' ');
  if (space == std::string::npos || space > 12 || space + 4 > response.size()) return false;
  const std::string code = response.substr(space + 1, 3);
  if (!all_digits(code)) return false;
  status = std::atoi(code.c_str());
  const std::string head = lowered(response.substr(0, head_end));
  const size_t encoding = head.find("transfer-encoding:");
  const bool chunked = encoding != std::string::npos && head.find("chunked", encoding) != std::string::npos &&
                       head.find("chunked", encoding) < (head.find('\n', encoding) == std::string::npos ? head.size() : head.find('\n', encoding));
  if (!chunked) {
    body = response.substr(head_end + 4);
    return true;
  }
  body.clear();
  size_t at = head_end + 4;
  for (int chunks = 0; chunks < 4096; ++chunks) {
    const size_t line_end = response.find("\r\n", at);
    if (line_end == std::string::npos || line_end - at > 16) break;
    size_t length = 0;
    bool any = false;
    for (size_t i = at; i < line_end; ++i) {
      const char c = lower(response[i]);
      int digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else break;   // a chunk extension follows the size
      if (length > 0x00FFFFFF) return false;
      length = length * 16 + (size_t)digit;
      any = true;
    }
    if (!any) break;
    if (length == 0) return true;
    at = line_end + 2;
    if (length > response.size() - at) { body.append(response, at, std::string::npos); return true; }   // cut short: what there is
    body.append(response, at, length);
    at += length + 2;
    if (at > response.size()) return true;
  }
  return true;
}

std::string xml_value(const std::string& xml, const std::string& tag) {
  const std::string open = "<" + tag + ">", close = "</" + tag + ">";
  const size_t a = xml.find(open);
  if (a == std::string::npos) return {};
  const size_t b = xml.find(close, a + open.size());
  if (b == std::string::npos || b - a - open.size() > 256) return {};
  return trimmed(xml.substr(a + open.size(), b - a - open.size()));
}

bool upnp_find_control(const std::string& xml, std::string& service_type, std::string& control_url) {
  static const char* const wanted[3] = {
      "urn:schemas-upnp-org:service:WANIPConnection:2",
      "urn:schemas-upnp-org:service:WANIPConnection:1",
      "urn:schemas-upnp-org:service:WANPPPConnection:1",
  };
  int best = 3;
  size_t at = 0;
  for (int blocks = 0; blocks < 64; ++blocks) {
    const size_t a = xml.find("<service>", at);
    if (a == std::string::npos) break;
    const size_t b = xml.find("</service>", a);
    if (b == std::string::npos) break;
    at = b + 10;
    if (b - a > 4096) continue;
    const std::string block = xml.substr(a, b - a);
    const std::string type = xml_value(block, "serviceType");
    const std::string control = xml_value(block, "controlURL");
    if (control.empty()) continue;
    for (int rank = 0; rank < best; ++rank) {
      if (type != wanted[rank]) continue;
      best = rank;
      service_type = type;
      control_url = control;
    }
  }
  return best < 3;
}

std::string upnp_soap(const std::string& service_type, const std::string& action,
                      const std::vector<std::pair<std::string, std::string>>& arguments) {
  auto escaped = [](const std::string& text) {
    std::string out;
    for (char c : text) {
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else out += c;
    }
    return out;
  };
  std::string body = "<?xml version=\"1.0\"?>\r\n"
                     "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                     "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
                     action + " xmlns:u=\"" + escaped(service_type) + "\">";
  for (const auto& argument : arguments) body += "<" + argument.first + ">" + escaped(argument.second) + "</" + argument.first + ">";
  body += "</u:" + action + "></s:Body></s:Envelope>\r\n";
  return body;
}

// ---------------------------------------------------------------- candidates
std::vector<std::string> extra_candidates(const PortMapping& mapping, bool have_stun, const NatEndpoint& stun, uint32_t seen_ip) {
  std::vector<std::string> out;
  auto add = [&](const NatEndpoint& e) {
    if (out.size() >= kMaxExtraCandidates || e.port == 0 || !ip_is_public(e.ip)) return;
    const std::string text = endpoint_text(e);
    if (std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
  };
  const bool stun_ok = have_stun && ip_is_public(stun.ip);
  if (mapping.state == MapState::Mapped && mapping.external.port != 0 &&
      (mapping.external.ip == 0 || ip_is_public(mapping.external.ip))) {
    NatEndpoint e;
    e.ip = mapping.external.ip ? mapping.external.ip : stun_ok ? stun.ip : seen_ip;
    e.port = mapping.external.port;
    add(e);
  }
  if (stun_ok) add(stun);
  return out;
}

std::vector<std::string> order_candidates(const std::string& seen, const std::vector<std::string>& extra,
                                          const std::vector<std::string>& lan, size_t max) {
  std::vector<std::string> out;
  auto add = [&](const std::string& text, bool only_public) {
    NatEndpoint e;
    if (out.size() >= max || !parse_endpoint(text, e) || !ip_is_usable(e.ip)) return false;
    if (only_public && !ip_is_public(e.ip)) return false;
    const std::string canonical = endpoint_text(e);
    if (std::find(out.begin(), out.end(), canonical) != out.end()) return false;
    out.push_back(canonical);
    return true;
  };
  add(seen, false);
  size_t extras = 0, looked = 0;
  for (const auto& e : extra) {
    if (++looked > 8 || extras >= kMaxExtraCandidates) break;
    if (add(e, true)) ++extras;
  }
  looked = 0;
  for (const auto& l : lan) {
    if (++looked > 8) break;
    add(l, false);
  }
  return out;
}

// ---------------------------------------------------------------- the network side
namespace {

bool winsock_ready() {
  static std::once_flag once;
  static bool ok = false;
  std::call_once(once, [] { WSADATA data; ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; });
  return ok;
}

struct Socket {
  SOCKET s = INVALID_SOCKET;
  Socket() = default;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  ~Socket() { if (s != INVALID_SOCKET) closesocket(s); }
};

sockaddr_in make_address(uint32_t ip, uint16_t port) {
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  a.sin_port = htons(port);
  return a;
}

long ms_left(Clock::time_point deadline) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return left > 0 ? (long)left : 0;
}

// Waits until the socket can be read (or written). False when the time is up.
bool wait_socket(SOCKET s, bool for_write, long ms) {
  if (ms <= 0) return false;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return select(0, for_write ? nullptr : &set, for_write ? &set : nullptr, nullptr, &tv) > 0;
}

// The router this PC sends through and this PC's own address toward it. Only a router on a
// private (or carrier-shared) address is ever asked for a mapping: a PC whose next hop is a public
// address has no home router to ask.
struct Route { uint32_t gateway = 0, local = 0; };
bool find_route(Route& route) {
  MIB_IPFORWARDROW row;
  std::memset(&row, 0, sizeof row);
  if (GetBestRoute(htonl(0x08080808u), 0, &row) != NO_ERROR) return false;
  route.gateway = ntohl(row.dwForwardNextHop);
  if (!ip_is_private(route.gateway) && !ip_is_shared(route.gateway)) return false;
  Socket probe;
  probe.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (probe.s == INVALID_SOCKET) return false;
  // Connecting a UDP socket sends nothing; it only makes the system choose the local address.
  const sockaddr_in to = make_address(route.gateway, 5351);
  if (connect(probe.s, reinterpret_cast<const sockaddr*>(&to), sizeof to) != 0) return false;
  sockaddr_in me;
  int size = sizeof me;
  if (getsockname(probe.s, reinterpret_cast<sockaddr*>(&me), &size) != 0) return false;
  route.local = ntohl(me.sin_addr.s_addr);
  return ip_is_usable(route.local) && !ip_is_loopback(route.local);
}

// One request and its answer from exactly the address asked. -1: nothing in time. -2: the
// address refused (nothing listens there).
int udp_ask(SOCKET s, const sockaddr_in& to, const uint8_t* request, int request_size, uint8_t* answer, int capacity, long ms) {
  if (sendto(s, reinterpret_cast<const char*>(request), request_size, 0, reinterpret_cast<const sockaddr*>(&to), sizeof to) != request_size) return -1;
  const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(ms);
  for (int tries = 0; tries < 16; ++tries) {
    if (!wait_socket(s, false, ms_left(deadline))) return -1;
    sockaddr_in from;
    int from_size = sizeof from;
    const int got = recvfrom(s, reinterpret_cast<char*>(answer), capacity, 0, reinterpret_cast<sockaddr*>(&from), &from_size);
    if (got == SOCKET_ERROR) return WSAGetLastError() == WSAECONNRESET ? -2 : -1;
    if (from.sin_addr.s_addr == to.sin_addr.s_addr && from.sin_port == to.sin_port) return got;
  }
  return -1;
}

// What is mapped, and how to take it away again.
struct Active {
  bool mapped = false;
  bool upnp = false;
  uint16_t internal_port = 0, external_port = 0;
  uint32_t external_ip = 0, lease_s = 0;
  uint32_t gateway = 0;        // NAT-PMP
  NatEndpoint control_host;    // UPnP
  std::string control_path, service_type;
  std::string detail;
};

bool natpmp_map(const Route& route, uint16_t port, uint32_t lifetime_s, Active& out) {
  Socket udp;
  udp.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp.s == INVALID_SOCKET) return false;
  const sockaddr_in to = make_address(route.gateway, 5351);
  uint8_t request[kNatPmpMapRequestSize], answer[64];
  natpmp_write_map(port, lifetime_s ? port : 0, lifetime_s, request);
  uint16_t external = 0;
  uint32_t lease = 0;
  bool ok = false;
  // 250 ms, then 500 ms: the RFC's first two waits. A router that has not answered by then has no NAT-PMP.
  for (long wait : {250L, 500L}) {
    const int got = udp_ask(udp.s, to, request, (int)sizeof request, answer, (int)sizeof answer, wait);
    if (got == -2) return false;
    if (got > 0 && natpmp_read_map(answer, (size_t)got, port, external, lease)) { ok = true; break; }
    if (got > 0) return false;   // it answered, and the answer is a refusal
  }
  if (!ok) return false;
  if (lifetime_s == 0) return true;   // a removal
  if (external == 0) return false;
  out = Active();
  out.mapped = true;
  out.internal_port = port;
  out.external_port = external;
  out.lease_s = lease;
  out.gateway = route.gateway;
  const uint8_t ask_address[2] = {0, 0};
  const int got = udp_ask(udp.s, to, ask_address, 2, answer, (int)sizeof answer, 300);
  uint32_t ip = 0;
  if (got > 0 && natpmp_read_address(answer, (size_t)got, ip)) out.external_ip = ip;
  out.detail = "the router mapped the port (NAT-PMP)";
  return true;
}

// One HTTP request on a new connection, the whole answer read (to `capacity` at most).
bool http_exchange(const NatEndpoint& host, const std::string& request, std::string& response, size_t capacity, Clock::time_point deadline) {
  response.clear();
  Socket tcp;
  tcp.s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (tcp.s == INVALID_SOCKET) return false;
  u_long nonblocking = 1;
  ioctlsocket(tcp.s, FIONBIO, &nonblocking);
  const sockaddr_in to = make_address(host.ip, host.port);
  if (connect(tcp.s, reinterpret_cast<const sockaddr*>(&to), sizeof to) != 0) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
    const long wait = ms_left(deadline) < 1500 ? ms_left(deadline) : 1500;
    if (!wait_socket(tcp.s, true, wait)) return false;
    int error = 0, size = sizeof error;
    if (getsockopt(tcp.s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error != 0) return false;
  }
  size_t sent = 0;
  while (sent < request.size()) {
    const int n = send(tcp.s, request.data() + sent, (int)(request.size() - sent), 0);
    if (n > 0) { sent += (size_t)n; continue; }
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK && wait_socket(tcp.s, true, ms_left(deadline))) continue;
    return false;
  }
  size_t body_at = std::string::npos, body_size = std::string::npos;
  char buffer[4096];
  while (response.size() < capacity) {
    if (!wait_socket(tcp.s, false, ms_left(deadline))) break;
    const size_t room = capacity - response.size();
    const int n = recv(tcp.s, buffer, (int)(room < sizeof buffer ? room : sizeof buffer), 0);
    if (n == 0) break;
    if (n < 0) { if (WSAGetLastError() == WSAEWOULDBLOCK) continue; break; }
    response.append(buffer, (size_t)n);
    if (body_at == std::string::npos) {
      const size_t head_end = response.find("\r\n\r\n");
      if (head_end != std::string::npos) {
        body_at = head_end + 4;
        // A server that keeps the connection open says how long the body is.
        const std::string head = lowered(response.substr(0, head_end));
        const size_t field = head.find("content-length:");
        if (field != std::string::npos) body_size = (size_t)std::strtoul(head.c_str() + field + 15, nullptr, 10);
      }
    }
    if (body_at != std::string::npos && body_size != std::string::npos && response.size() - body_at >= body_size) break;
  }
  return body_at != std::string::npos;
}

struct Igd {
  NatEndpoint host;
  std::string path;   // of the device description
};

// The gateway devices that answer a search on this network: at most four, the default router first.
std::vector<Igd> ssdp_search(const Route& route, Clock::time_point deadline, const std::atomic<bool>& cancel) {
  std::vector<Igd> found;
  Socket udp;
  udp.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp.s == INVALID_SOCKET) return found;
  // From the adapter that leads to the router, not whichever the system lists first (a VPN, a virtual switch).
  sockaddr_in local = make_address(route.local, 0);
  if (bind(udp.s, reinterpret_cast<const sockaddr*>(&local), sizeof local) != 0) return found;
  in_addr out_if;
  out_if.s_addr = htonl(route.local);
  setsockopt(udp.s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&out_if), sizeof out_if);
  DWORD ttl = 2;
  setsockopt(udp.s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
  const sockaddr_in group = make_address(0xEFFFFFFAu, 1900);   // 239.255.255.250
  static const char* const targets[2] = {
      "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
      "urn:schemas-upnp-org:service:WANIPConnection:1",
  };
  auto search = [&] {
    for (const char* target : targets) {
      const std::string message = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ") +
                                  target + "\r\n\r\n";
      sendto(udp.s, message.data(), (int)message.size(), 0, reinterpret_cast<const sockaddr*>(&group), sizeof group);
    }
  };
  search();
  const Clock::time_point again = Clock::now() + std::chrono::milliseconds(400);
  bool repeated = false;
  for (int rounds = 0; rounds < 96 && found.size() < 4; ++rounds) {
    if (!repeated && Clock::now() >= again) { search(); repeated = true; }
    // Never a wait of more than 400 ms, so a stop() is noticed soon.
    const long until_repeat = repeated ? 400 : ms_left(again) + 1;
    const long wait = ms_left(deadline) < until_repeat ? ms_left(deadline) : until_repeat;
    if (ms_left(deadline) <= 0 || cancel.load()) break;
    if (!wait_socket(udp.s, false, wait)) continue;
    char buffer[2049];
    sockaddr_in from;
    int from_size = sizeof from;
    const int got = recvfrom(udp.s, buffer, (int)sizeof buffer - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_size);
    if (got <= 0) continue;
    const uint32_t source = ntohl(from.sin_addr.s_addr);
    // A gateway is on this network. Its description must be on the gateway itself: an answer
    // that points anywhere else is not followed.
    if (!ip_is_private(source) && !ip_is_shared(source)) continue;
    std::string url;
    Igd igd;
    if (!ssdp_location(std::string(buffer, (size_t)got), url) || !http_url(url, igd.host, igd.path) || igd.host.ip != source) continue;
    bool known = false;
    for (const auto& f : found) if (f.host == igd.host && f.path == igd.path) known = true;
    if (known) continue;
    if (source == route.gateway) { found.insert(found.begin(), igd); break; }   // the router itself answered: enough
    found.push_back(igd);
  }
  return found;
}

std::string host_header(const NatEndpoint& host) { return endpoint_text(host); }

// 0 on success, the UPnP error code when the device refused, -1 when it could not be asked.
int soap_call(const Active& target, const std::string& action, const std::vector<std::pair<std::string, std::string>>& arguments,
              std::string& answer, Clock::time_point deadline) {
  const std::string body = upnp_soap(target.service_type, action, arguments);
  const std::string request = "POST " + target.control_path + " HTTP/1.1\r\nHost: " + host_header(target.control_host) +
                              "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + target.service_type + "#" + action +
                              "\"\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  std::string response;
  const Clock::time_point limit = Clock::now() + std::chrono::milliseconds(2000);
  if (!http_exchange(target.control_host, request, response, 16384, limit < deadline ? limit : deadline)) return -1;
  int status = 0;
  if (!http_split(response, status, answer)) return -1;
  if (status == 200) return 0;
  const std::string code = xml_value(answer, "errorCode");
  const int number = all_digits(code) && code.size() <= 4 ? std::atoi(code.c_str()) : 0;
  return number > 0 ? number : -1;
}

bool upnp_map(const Route& route, uint16_t port, Active& out, Clock::time_point deadline, const std::atomic<bool>& cancel) {
  const Clock::time_point search_until = Clock::now() + std::chrono::milliseconds(1800);
  const std::vector<Igd> devices = ssdp_search(route, search_until < deadline ? search_until : deadline, cancel);
  for (const Igd& device : devices) {
    if (ms_left(deadline) <= 0 || cancel.load()) break;
    std::string response, xml;
    const std::string get = "GET " + device.path + " HTTP/1.1\r\nHost: " + host_header(device.host) +
                            "\r\nConnection: close\r\nUser-Agent: MeleeUnlocked UPnP/1.1\r\n\r\n";
    const Clock::time_point limit = Clock::now() + std::chrono::milliseconds(2000);
    int status = 0;
    if (!http_exchange(device.host, get, response, 262144, limit < deadline ? limit : deadline) ||
        !http_split(response, status, xml) || status != 200) continue;
    Active target;
    std::string control;
    if (cancel.load() || !upnp_find_control(xml, target.service_type, control)) continue;
    // The control URL: a full URL, or a path on the description's host (or on URLBase, where an
    // older device names one). Whatever it says, only the device's own address is ever called.
    target.control_host = device.host;
    NatEndpoint named;
    std::string named_path;
    if (http_url(control, named, named_path)) {
      if (named.ip != device.host.ip) continue;
      target.control_host = named;
      target.control_path = named_path;
    } else {
      const std::string base = xml_value(xml, "URLBase");
      if (!base.empty() && http_url(base, named, named_path) && named.ip == device.host.ip) target.control_host = named;
      target.control_path = control.empty() || control[0] != '/' ? "/" + control : control;
      bool clean = target.control_path.size() <= 512;
      for (unsigned char c : target.control_path) if (c <= 32 || c >= 127) clean = false;
      if (!clean) continue;
    }
    uint32_t lease = 3600;
    uint16_t external = port;
    int error = -1;
    for (int attempt = 0; attempt < 4 && ms_left(deadline) > 0 && !cancel.load(); ++attempt) {
      std::string answer;
      error = soap_call(target, "AddPortMapping",
                        {{"NewRemoteHost", ""}, {"NewExternalPort", std::to_string(external)}, {"NewProtocol", "UDP"},
                         {"NewInternalPort", std::to_string(port)}, {"NewInternalClient", ip_text(route.local)},
                         {"NewEnabled", "1"}, {"NewPortMappingDescription", "Melee Unlocked"},
                         {"NewLeaseDuration", std::to_string(lease)}},
                        answer, deadline);
      if (error == 0) break;
      if (error == 725 && lease != 0) { lease = 0; continue; }   // this router only makes mappings without an end
      if (error == 718 && external < 65535) { ++external; continue; }   // the outside port is taken: the next one
      break;
    }
    if (error != 0) continue;
    out = target;
    out.mapped = true;
    out.upnp = true;
    out.internal_port = port;
    out.external_port = external;
    out.lease_s = lease;
    std::string answer;
    uint32_t ip = 0;
    if (soap_call(out, "GetExternalIPAddress", {}, answer, deadline) == 0 && parse_ip(xml_value(answer, "NewExternalIPAddress"), ip))
      out.external_ip = ip;
    out.detail = "the router mapped the port (UPnP)";
    return true;
  }
  return false;
}

void unmap(const Active& active) {
  if (!active.mapped) return;
  const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(2000);
  if (active.upnp) {
    std::string answer;
    soap_call(active, "DeletePortMapping",
              {{"NewRemoteHost", ""}, {"NewExternalPort", std::to_string(active.external_port)}, {"NewProtocol", "UDP"}},
              answer, deadline);
  } else {
    Route route;
    route.gateway = active.gateway;
    Active ignored;
    natpmp_map(route, active.internal_port, 0, ignored);
  }
}

// The whole attempt: NAT-PMP (quick to say no), then UPnP. Ten seconds at the very worst; a
// cancel is noticed between two steps, none of which waits more than two seconds.
Active map_port(uint16_t port, const std::atomic<bool>& cancel) {
  Active active;
  if (!winsock_ready()) { active.detail = "Windows networking could not be started"; return active; }
  Route route;
  if (!find_route(route)) { active.detail = "no home router was found to ask"; return active; }
  if (natpmp_map(route, port, 3600, active)) return active;
  if (!cancel.load() && upnp_map(route, port, active, Clock::now() + std::chrono::milliseconds(8000), cancel)) return active;
  active = Active();
  active.detail = "the router answered neither NAT-PMP nor UPnP";
  return active;
}

}  // namespace

// ---------------------------------------------------------------- NameLookup
struct NameLookup::State {
  std::mutex mutex;
  bool done = false;
  std::vector<NatEndpoint> results;
};

void NameLookup::start(const std::vector<std::string>& host_ports) {
  auto state = std::make_shared<State>();
  state_ = state;
  std::vector<std::string> names;
  for (const auto& name : host_ports) if (names.size() < (size_t)kMaxStunServers && name.size() <= 96) names.push_back(name);
  try {
    // Detached: a lookup that hangs must not hold the launcher's exit. It owns its state.
    std::thread([state, names] {
      std::vector<NatEndpoint> found;
      if (winsock_ready()) {
        for (const auto& name : names) {
          const size_t colon = name.rfind(':');
          if (colon == std::string::npos || colon == 0) continue;
          addrinfo hint;
          std::memset(&hint, 0, sizeof hint);
          hint.ai_family = AF_INET;
          hint.ai_socktype = SOCK_DGRAM;
          addrinfo* list = nullptr;
          if (getaddrinfo(name.substr(0, colon).c_str(), name.substr(colon + 1).c_str(), &hint, &list) != 0 || !list) continue;
          const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(list->ai_addr);
          NatEndpoint e;
          e.ip = ntohl(in->sin_addr.s_addr);
          e.port = ntohs(in->sin_port);
          freeaddrinfo(list);
          if (e.port != 0 && ip_is_usable(e.ip) && std::find(found.begin(), found.end(), e) == found.end()) found.push_back(e);
        }
      }
      std::lock_guard<std::mutex> lock(state->mutex);
      state->results = found;
      state->done = true;
    }).detach();
  } catch (...) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->done = true;
  }
}

bool NameLookup::done() const {
  if (!state_) return false;
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->done;
}

std::vector<NatEndpoint> NameLookup::results() const {
  if (!state_) return {};
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->done ? state_->results : std::vector<NatEndpoint>();
}

// ---------------------------------------------------------------- PortMapper
struct PortMapper::State {
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::thread thread;
  bool quit = false;
  std::atomic<bool> cancel{false};   // quit, as the attempt in flight reads it without the lock
  uint16_t wanted = 0;
  PortMapping status;
};

PortMapper::PortMapper() : state_(new State) {}
PortMapper::~PortMapper() { stop(); }

PortMapping PortMapper::status() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->status;
}

void PortMapper::request(uint16_t udp_port) {
  State* s = state_.get();
  std::lock_guard<std::mutex> lock(s->mutex);
  if (s->wanted == udp_port && (s->thread.joinable() || udp_port == 0)) return;
  s->wanted = udp_port;
  if (s->thread.joinable()) { s->wake.notify_all(); return; }
  if (udp_port == 0) return;
  s->quit = false;
  s->cancel.store(false);
  s->status = PortMapping();
  s->status.state = MapState::Working;
  s->status.internal_port = udp_port;
  try {
    s->thread = std::thread([s] {
      Active active;
      uint16_t tried = 0;   // the port of the last attempt, mapped or not
      Clock::time_point renew_at = Clock::time_point::max();
      for (;;) {
        uint16_t port = 0;
        bool renew = false;
        {
          std::unique_lock<std::mutex> lock(s->mutex);
          auto changed = [&] { return s->quit || s->wanted != tried; };
          if (renew_at == Clock::time_point::max()) s->wake.wait(lock, changed);
          else renew = !s->wake.wait_until(lock, renew_at, changed);
          if (s->quit) break;
          port = s->wanted;
          // A renewal leaves the status as it is: the mapping is still there while it is asked for again.
          if (port != tried) {
            s->status = PortMapping();
            s->status.state = port ? MapState::Working : MapState::Idle;
            s->status.internal_port = port;
          }
        }
        // Outside the lock: everything below talks to the router.
        if (port != tried) { unmap(active); active = Active(); }
        tried = port;
        renew_at = Clock::time_point::max();
        if (port == 0) continue;
        Active next;
        try { next = map_port(port, s->cancel); } catch (...) { next = Active(); next.detail = "the port mapping ran out of memory"; }
        // A renewal that fails keeps what it knows of the old mapping, so that it is still removed
        // at the end, and is tried again in five minutes.
        const bool ok = next.mapped;
        if (ok || !renew) active = next;
        else renew_at = Clock::now() + std::chrono::seconds(300);
        PortMapping status;
        status.internal_port = port;
        status.detail = next.detail;
        if (ok) {
          status.state = MapState::Mapped;
          status.method = active.upnp ? "UPnP" : "NAT-PMP";
          status.external.ip = active.external_ip;
          status.external.port = active.external_port;
          status.lease_s = active.lease_s;
          // Half the lease, between one and thirty minutes; a mapping without an end is looked at every thirty.
          uint32_t half = active.lease_s / 2;
          if (active.lease_s == 0 || half > 1800) half = 1800;
          if (half < 60) half = 60;
          renew_at = Clock::now() + std::chrono::seconds(half);
        } else {
          status.state = MapState::Failed;
        }
        std::lock_guard<std::mutex> lock(s->mutex);
        if (!s->quit && s->wanted == port) s->status = status;
      }
      unmap(active);
    });
  } catch (...) {
    s->status.state = MapState::Failed;
    s->status.detail = "the port mapping thread could not be started";
  }
}

void PortMapper::stop() {
  State* s = state_.get();
  std::thread worker;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    s->quit = true;
    s->cancel.store(true);
    s->wanted = 0;
    worker.swap(s->thread);
  }
  s->wake.notify_all();
  if (worker.joinable()) worker.join();
  std::lock_guard<std::mutex> lock(s->mutex);
  s->status = PortMapping();
}

PortMapper& port_mapper() {
  static PortMapper mapper;
  return mapper;
}

}  // namespace mu_net
