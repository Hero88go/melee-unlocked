#include "yougame_udp.h"
#include <cstdio>
#include <vector>
using namespace yougame::udp;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", name); } }
void env() {
  const auto unset = port(nullptr);
  check(!unset.set && unset.valid, "unset port preserves default");
  for (const char* s : {"1", "51600", "51699", "65535", "00080"})
    check(port(s).set && port(s).valid, "valid decimal port");
  for (const char* s : {"", "0", "65536", "-1", "+80", " 80", "80 ", "80x", "1.0", "0x50", "99999999999999999999", "80\n"})
    check(port(s).set && !port(s).valid, "invalid decimal port");
  check(port("51600").value == 51600 && port("65535").value == 65535, "port parsed exactly");
  for (const char* s : {"10.0.0.0", "10.255.255.255", "172.16.0.0", "172.31.255.255", "192.168.0.0", "192.168.255.255"})
    check(private_ipv4(s), "RFC1918 IPv4");
  check(!private_ipv4(nullptr), "unset LAN override");
  for (const char* s : {"", "172.15.255.255", "172.32.0.0", "192.169.0.1", "198.18.0.2", "8.8.8.8", "100.64.0.1", "127.0.0.1",
                       "169.254.1.1", "0.0.0.0", "255.255.255.255", "224.0.0.1", "10.256.1.1", "10.1.1", "10.1.1.1.1",
                       "10.01.1.1", "10.1.1.-1", "10.1.1.1:51600", " 10.1.1.1", "10.1.1.1\n", "::ffff:10.1.1.1", "localhost"})
    check(!private_ipv4(s), "invalid or non-private LAN override");
  int lookups = 0;
  std::vector<std::string> logs;
  auto local = [&] { ++lookups; return std::string("198.18.0.2"); };
  auto log = [&](const char* s) { logs.emplace_back(s); };
  check(lan_candidate("192.168.1.9",51642,local,log) == "192.168.1.9:51642" && lookups == 0 && logs.empty(), "LAN override advertises actual bound port");
  check(lan_candidate(nullptr,42000,local,log) == "198.18.0.2:42000" && lookups == 1 && logs.empty(), "unset LAN uses original interface");
  for (const char* s : {"garbage", "8.8.8.8", ""}) {
    check(lan_candidate(s,51642,local,log) == "198.18.0.2:51642", "invalid LAN falls back");
    check(logs.back() == "yougame: host LAN address ignored (not a private IPv4)", "LAN log never includes supplied value");
  }
}
void binding() {
  std::vector<uint16_t> attempts;
  std::vector<std::string> logs;
  const auto log = [&](const char* s) { logs.emplace_back(s); };
  auto bind = [&](uint16_t p) { attempts.push_back(p); return true; };
  check(bind_port(port("51642"), 42000, bind, log) && attempts == std::vector<uint16_t>{51642} && logs.empty(), "bind exact override once");
  attempts.clear();
  check(bind_port(port(nullptr), 42000, bind, log) && attempts == std::vector<uint16_t>{42000}, "unset binds original port");
  attempts.clear();
  check(!bind_port(port("65536"), 42000, bind, log) && attempts.empty(), "invalid never binds");
  check(logs.back() == "yougame: own UDP port invalid unavailable (invalid decimal port)", "safe invalid port log");
  logs.clear();
  check(!bind_port(port("51642"), 42000, [&](uint16_t p) { attempts.push_back(p); return false; }, log) && attempts == std::vector<uint16_t>{51642}, "occupied port never retries");
  check(logs == std::vector<std::string>{"yougame: own UDP port 51642 unavailable (bind failed)"}, "bind failure log");
}
void ingress() {
  Ingress filter;
  const Endpoint peer{0x12345678, 51642}, stranger{0x12345679, 51642};
  filter.allow(peer);
  std::vector<std::string> logs;
  int parsed = 0;
  const auto receive = [&](Endpoint from, uint64_t now) {
    if (!filter.accept(from, now, [&](const char* s) { logs.emplace_back(s); })) return;
    ++parsed;
  };
  receive(peer, 0); check(parsed == 1 && logs.empty(), "known accepted");
  receive(stranger, 0); check(parsed == 1 && logs.size() == 1, "unknown dropped before parser");
  receive({peer.host, uint16_t(peer.port + 1)}, 1); check(parsed == 1, "same IP wrong port dropped");
  receive(stranger, 9999); check(logs.size() == 1, "no log within ten seconds");
  receive(stranger, 10000);
  check(logs == std::vector<std::string>{"yougame: dropped 1 datagrams from unknown senders", "yougame: dropped 3 datagrams from unknown senders"}, "log aggregates exact window count");
  filter.clear(); receive(peer, 10001); check(parsed == 1, "retired peer rejected");
  filter.allow(stranger); receive(stranger, 10002); check(parsed == 2, "new pairing accepted");
  DropCounter shared;
  Ingress punch(&shared), gameplay(&shared);
  logs.clear();
  auto log = [&](const char* s) { logs.emplace_back(s); };
  punch.accept(stranger, 20000, log); gameplay.accept(stranger, 20001, log);
  check(logs.size() == 1, "socket handoff retains ten-second budget");
  gameplay.accept(stranger, 30000, log);
  check(logs.back() == "yougame: dropped 2 datagrams from unknown senders", "socket handoff retains pending count");
}
void stun_ingress() {
  DropCounter shared;
  StunIngress filter(&shared);
  Ingress gameplay(&shared);
  const Endpoint server{0x12345678, 3478};
  uint8_t req[20] = {0,1,0,0,0x21,0x12,0xa4,0x42};
  req[8] = 7; req[19] = 9;
  uint8_t reply[32] = {1,1,0,12,0x21,0x12,0xa4,0x42};
  std::memcpy(reply + 8, req + 8, 12);
  reply[21] = 1; reply[23] = 8; reply[25] = 1; reply[26] = 0xc9; reply[27] = 0xa0; reply[28] = 10; reply[31] = 1;
  std::vector<std::string> logs;
  auto log = [&](const char* s) { logs.emplace_back(s); };
  int parsed = 0;
  std::string mapped;
  auto receive = [&](Endpoint from, size_t size, uint64_t now) {
    if (!filter.accept(from, reply, size, now, log)) return false;
    ++parsed;
    return stun_reply(reply, size, req, mapped);
  };
  check(!receive(server, sizeof reply, 0) && parsed == 0, "STUN without open request dropped before parser");
  filter.open(server, req, 500);
  check(receive(server, sizeof reply, 1) && parsed == 1 && mapped == "10.0.0.1:51616", "STUN matching endpoint and ID reaches parser");
  for (size_t i = 8; i < 20; ++i) {
    reply[i] ^= 1;
    check(!receive(server, sizeof reply, 2) && parsed == 1, "STUN every wrong ID byte dropped before parser");
    reply[i] ^= 1;
  }
  check(!filter.accept({server.host + 1, server.port}, nullptr, 20, 3, log), "STUN other address dropped before ID read");
  check(!filter.accept({server.host, uint16_t(server.port + 1)}, nullptr, 20, 4, log), "STUN other port dropped before ID read");
  check(!receive(server, 19, 5) && parsed == 1, "STUN short ID dropped before parser");
  filter.close();
  check(!receive(server, sizeof reply, 6) && parsed == 1, "STUN after completion dropped before parser");
  filter.open(server, req, 500);
  check(!receive(server, sizeof reply, 500) && parsed == 1, "STUN at timeout dropped before parser");
  check(!receive(server, sizeof reply, 501) && parsed == 1, "STUN after timeout stays closed");
  filter.open({server.host + 1, server.port}, req, 1000);
  check(!receive(server, sizeof reply, 502) && parsed == 1, "STUN previous server retired");
  check(receive({server.host + 1, server.port}, sizeof reply, 503) && parsed == 2, "STUN new open server accepted");
  gameplay.accept(server, 10000, log);
  check(logs == std::vector<std::string>{"yougame: dropped 1 datagrams from unknown senders", "yougame: dropped 20 datagrams from unknown senders"},
      "STUN rejected endpoint and ID share gameplay rate-limited counter");
}
void parsers() {
  uint8_t p[32] = {'Y','G','P','2',0}; p[13] = 1;
  Probe out{};
  check(probe(p,14,out) && out.player == 1 && out.type == 0, "valid punch");
  for (size_t n = 0; n <= 32; ++n) if (n != 14) check(!probe(p,n,out), "punch exact size");
  p[4] = 2; check(!probe(p,14,out), "unknown punch type");
  p[4] = 0; p[13] = 6; check(probe(p,14,out) && out.player == 6, "six-seat punch player");   // a six-seat Friends room
  p[13] = 7; check(!probe(p,14,out), "invalid punch player");
  p[13] = 0; check(!probe(p,14,out), "invalid punch player 0");
  uint8_t req[20] = {0,1,0,0,0x21,0x12,0xa4,0x42};
  uint8_t reply[32] = {1,1,0,12,0x21,0x12,0xa4,0x42};
  reply[21] = 1; reply[23] = 8; reply[25] = 1; reply[26] = 0xc9; reply[27] = 0xa0; reply[28] = 10; reply[31] = 1;
  std::string mapped;
  check(stun_reply(reply,32,req,mapped) && mapped == "10.0.0.1:51616", "bounded STUN mapped address");
  for (size_t n=0; n<32; ++n) check(!stun_reply(reply,n,req,mapped), "truncated STUN");
  reply[23] = 255; check(!stun_reply(reply,32,req,mapped), "STUN length exceeds datagram");
  reply[23] = 8; reply[8] = 1; check(!stun_reply(reply,32,req,mapped), "STUN transaction mismatch");
}
}
int main() {
  env(); binding(); ingress(); stun_ingress(); parsers();
  std::printf("yougame_udp: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
