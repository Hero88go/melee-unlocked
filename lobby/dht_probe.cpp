// Feasibility probe for the no-operator-server design in DECENTRALIZED_NETWORK.md.
// This is a diagnostic, not the lobby transport. It sends only BEP 5 DHT traffic.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <ctime>
#include <dht.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {
SOCKET socket_handle = INVALID_SOCKET;
std::set<std::string> found;
bool search_done = false;

std::array<unsigned char, 20> sha1(const std::string& input) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::array<unsigned char, 20> output{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0 ||
      BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
      BCryptHashData(hash, (PUCHAR)input.data(), (ULONG)input.size(), 0) < 0 ||
      BCryptFinishHash(hash, output.data(), (ULONG)output.size(), 0) < 0) {
    std::fprintf(stderr, "Windows SHA-1 failed\n");
    std::exit(2);
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return output;
}

bool address(const std::string& spec, sockaddr_in* out) {
  const auto colon = spec.rfind(':');
  if (colon == std::string::npos) return false;
  auto host = spec.substr(0, colon), port = spec.substr(colon + 1);
  addrinfo hints{}, *result = nullptr;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) return false;
  *out = *(sockaddr_in*)result->ai_addr;
  freeaddrinfo(result);
  return true;
}

void result(void*, int event, const unsigned char*, const void* data, size_t size) {
  if (event == DHT_EVENT_SEARCH_DONE) {
    search_done = true;
    std::printf("SEARCH_DONE\n");
    std::fflush(stdout);
  }
  if (event != DHT_EVENT_VALUES || !data) return;
  const auto* compact = (const unsigned char*)data;
  for (size_t offset = 0; offset + 6 <= size; offset += 6) {
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    std::memcpy(&peer.sin_addr, compact + offset, 4);
    std::memcpy(&peer.sin_port, compact + offset + 4, 2);
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
    const std::string value = std::string(ip) + ":" + std::to_string(ntohs(peer.sin_port));
    if (found.insert(value).second) std::printf("PEER %s\n", value.c_str());
  }
}
} // namespace

extern "C" int dht_gettimeofday(struct timeval* out, struct timezone*) {
  out->tv_sec = (long)std::time(nullptr);
  out->tv_usec = 0;
  return 0;
}
extern "C" int dht_sendto(int, const void* data, int length, int flags,
                            const sockaddr* destination, int destination_length) {
  return sendto(socket_handle, (const char*)data, length, flags, destination, destination_length);
}
extern "C" int dht_blacklisted(const sockaddr*, int) { return 0; }
extern "C" void dht_hash(void* output, int bytes, const void* a, int alen,
                           const void* b, int blen, const void* c, int clen) {
  std::string input;
  if (a && alen > 0) input.append((const char*)a, alen);
  if (b && blen > 0) input.append((const char*)b, blen);
  if (c && clen > 0) input.append((const char*)c, clen);
  auto digest = sha1(input);
  std::memcpy(output, digest.data(), (size_t)std::min(bytes, (int)digest.size()));
}
extern "C" int dht_random_bytes(void* output, size_t bytes) {
  return BCryptGenRandom(nullptr, (PUCHAR)output, (ULONG)bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 ? 0 : -1;
}

int main(int argc, char** argv) {
  int port = 0, seconds = 25;
  bool announce = false, debug = false;
  std::string bootstrap, topic = "melee-unlocked-lobby-probe-v1";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (arg == "--seconds" && i + 1 < argc) seconds = std::atoi(argv[++i]);
    else if (arg == "--bootstrap" && i + 1 < argc) bootstrap = argv[++i];
    else if (arg == "--topic" && i + 1 < argc) topic = argv[++i];
    else if (arg == "--announce") announce = true;
    else if (arg == "--debug") debug = true;
    else { std::fprintf(stderr, "usage: dht_probe --port N --bootstrap host:port [--announce] [--topic text] [--seconds N]\n"); return 2; }
  }
  if (port < 1 || port > 65535 || seconds < 1 || seconds > 300 || bootstrap.empty()) return 2;
  WSADATA ws{};
  if (WSAStartup(MAKEWORD(2, 2), &ws)) return 2;
  socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons((u_short)port);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (socket_handle == INVALID_SOCKET || bind(socket_handle, (sockaddr*)&local, sizeof local) != 0) {
    std::fprintf(stderr, "Could not listen on UDP %d\n", port); return 2;
  }
  u_long nonblocking = 1;
  ioctlsocket(socket_handle, FIONBIO, &nonblocking);
  std::array<unsigned char, 20> id{};
  if (debug) dht_debug = stderr;
  if (dht_random_bytes(id.data(), id.size()) < 0 || dht_init(1, -1, id.data(), (const unsigned char*)"MU01") < 0) return 2;
  sockaddr_in seed{};
  if (!address(bootstrap, &seed)) { std::fprintf(stderr, "Could not resolve bootstrap\n"); return 2; }
  auto hash = sha1(topic);
  ULONGLONG started = GetTickCount64(), last_ping = 0, last_search = 0;
  std::printf("LISTEN %d\nTOPIC %s\n", port, topic.c_str());
  std::fflush(stdout);
  while (GetTickCount64() - started < (ULONGLONG)seconds * 1000) {
    const ULONGLONG now = GetTickCount64();
    if (now - last_ping > 2000) {
      dht_ping_node((sockaddr*)&seed, sizeof seed);
      last_ping = now;
    }
    int good = 0, dubious = 0;
    dht_nodes(AF_INET, &good, &dubious, nullptr, nullptr);
    if (good + dubious > 0 && now - started > 1500 &&
        (last_search == 0 || (search_done && now - last_search > 5000))) {
      search_done = false;
      if (dht_search(hash.data(), announce ? port : 0, AF_INET, result, nullptr) >= 0)
        last_search = now;
    }
    sockaddr_storage from{}; int length = sizeof from;
    char buffer[4097];
    const int received = recvfrom(socket_handle, buffer, sizeof buffer - 1, 0, (sockaddr*)&from, &length);
    time_t next = 0;
    if (received > 0) {
      buffer[received] = '\0';
      dht_periodic(buffer, (size_t)received, (sockaddr*)&from, length, &next, result, nullptr);
    }
    else dht_periodic(nullptr, 0, nullptr, 0, &next, result, nullptr);
    Sleep(30);
  }
  int good = 0, dubious = 0;
  dht_nodes(AF_INET, &good, &dubious, nullptr, nullptr);
  std::printf("DHT_NODES %d\nPEERS_FOUND %zu\n", good + dubious, found.size());
  dht_uninit();
  closesocket(socket_handle);
  WSACleanup();
  return 0;
}
