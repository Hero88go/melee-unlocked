// Linux WinHTTP WebSockets over a libcurl TCP/TLS connection (no curl WebSocket support needed).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include "winhttp_ws_internal.h"
#include "winhttp_ws_protocol.h"
#include <curl/curl.h>
#ifdef __APPLE__
#include <Security/SecRandom.h>
#else
#include <openssl/rand.h>
#endif
#include <poll.h>
#include <sys/socket.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace {
#ifdef __APPLE__
bool random_bytes(uint8_t* data, size_t size) {
  return SecRandomCopyBytes(kSecRandomDefault, size, data) == errSecSuccess;
}
#endif
constexpr DWORD io_error = 12030, timeout_error = 12002, invalid_parameter = 87;
using Clock = std::chrono::steady_clock;
struct Deadline {
  explicit Deadline(int ms) : forever(ms <= 0), end(Clock::now() + std::chrono::milliseconds(ms)) {}
  bool forever;
  Clock::time_point end;
  int remaining() const {
    if (forever) return -1;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
    return ms <= 0 ? 0 : int(ms);
  }
};
struct WebSocket {
  CURL* curl = nullptr;
  curl_socket_t fd = CURL_SOCKET_BAD;
  std::atomic<bool> closed{false};
  std::atomic<int> receive_ms{0};
  std::mutex send_mutex, io_mutex;
  winhttp_ws::Decoder decoder; // reader only
  ~WebSocket() { if (curl) curl_easy_cleanup(curl); }

  DWORD wait(short events, const Deadline& deadline) {
    while (!closed) {
      pollfd p{fd, events, 0};
      const int n = poll(&p, 1, deadline.remaining());
      if (n > 0) return (p.revents & events) ? NO_ERROR : io_error;
      if (!n) return timeout_error;
      if (errno != EINTR) break;
    }
    return io_error;
  }
  DWORD read(uint8_t* data, size_t capacity, size_t& got, const Deadline& deadline) {
    for (;;) {
      CURLcode rc;
      {
        // libcurl forbids concurrent use of one easy handle, even for send/recv. Only the
        // nonblocking call holds this lock; poll never holds it, so writers can always run.
        std::lock_guard<std::mutex> lock(io_mutex);
        if (closed) return io_error;
        rc = curl_easy_recv(curl, data, capacity, &got);
      }
      if (rc == CURLE_OK) return got ? NO_ERROR : io_error;
      if (rc != CURLE_AGAIN) return io_error;
      const DWORD error = wait(POLLIN, deadline);
      if (error) return error;
    }
  }
  DWORD write(const uint8_t* data, size_t size, const Deadline& deadline) {
    while (size) {
      size_t sent = 0; CURLcode rc;
      {
        std::lock_guard<std::mutex> lock(io_mutex);
        if (closed) return io_error;
        rc = curl_easy_send(curl, data, size, &sent);
      }
      if (rc != CURLE_OK && rc != CURLE_AGAIN) return io_error;
      data += sent; size -= sent;
      if (size && !sent) {
        const DWORD error = wait(POLLOUT, deadline);
        if (error) return error;
      }
    }
    return NO_ERROR;
  }
  DWORD send(uint8_t opcode, const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(send_mutex);
    std::array<uint8_t, 4> mask;
#ifdef __APPLE__
    if (!random_bytes(mask.data(), mask.size())) return io_error;
#else
    if (RAND_bytes(mask.data(), (int)mask.size()) != 1) return io_error;
#endif
    const auto frame = winhttp_ws::encode(opcode, data, size, mask);
    return write(frame.data(), frame.size(), Deadline(15000));
  }
};

// Active calls retain ownership while CloseHandle removes the public handle and wakes poll.
// Looking up by address also makes a reader's cached handle safe if close won the race.
std::mutex sockets_mutex;
std::unordered_map<HINTERNET, std::shared_ptr<WebSocket>> sockets;
std::shared_ptr<WebSocket> find_socket(HINTERNET handle) {
  std::lock_guard<std::mutex> lock(sockets_mutex);
  const auto it = sockets.find(handle);
  return it == sockets.end() ? nullptr : it->second;
}
std::string lower(std::string text) {
  for (char& c : text) c = (char)std::tolower((unsigned char)c);
  return text;
}
std::string trim(const std::string& text) {
  const auto start = text.find_first_not_of(" \t\r");
  return start == std::string::npos ? "" : text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}
bool has_token(const std::string& text, const std::string& token) {
  std::istringstream in(lower(text)); std::string word;
  while (std::getline(in, word, ',')) if (trim(word) == token) return true;
  return false;
}
} // namespace

BOOL winhttp_ws_open(const std::string& host, INTERNET_PORT port, bool secure, const std::string& path,
                    const std::vector<std::string>& headers, int connect_ms, int receive_ms, long& status, HINTERNET& socket) {
  auto ws = std::make_shared<WebSocket>();
  ws->receive_ms = receive_ms;
  ws->curl = curl_easy_init();
  if (!ws->curl || host.find_first_of("\r\n") != std::string::npos || path.find_first_of("\r\n ") != std::string::npos) return FALSE;
  const bool default_port = port == 0 || port == (secure ? 443 : 80);
  const std::string authority = (host.find(':') == std::string::npos ? host : "[" + host + "]") +
                                (default_port ? "" : ":" + std::to_string(port));
  const std::string url = std::string(secure ? "https://" : "http://") + authority + (path.empty() ? "/" : path);
  curl_easy_setopt(ws->curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_setopt(ws->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(ws->curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
  curl_easy_setopt(ws->curl, CURLOPT_NOSIGNAL, 1L);
  if (connect_ms > 0) curl_easy_setopt(ws->curl, CURLOPT_CONNECTTIMEOUT_MS, (long)connect_ms);
  // libcurl's default peer/hostname verification and system CA store remain enabled.
  if (curl_easy_perform(ws->curl) != CURLE_OK ||
      curl_easy_getinfo(ws->curl, CURLINFO_ACTIVESOCKET, &ws->fd) != CURLE_OK || ws->fd == CURL_SOCKET_BAD) return FALSE;
  std::array<uint8_t, 16> random;
#ifdef __APPLE__
  if (!random_bytes(random.data(), random.size())) return FALSE;
#else
  if (RAND_bytes(random.data(), (int)random.size()) != 1) return FALSE;
#endif
  const std::string key = winhttp_ws::base64(random.data(), random.size());
  std::string request = "GET " + (path.empty() ? "/" : path) + " HTTP/1.1\r\nHost: " + authority +
    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n";
  for (const auto& header : headers) request += header + "\r\n";
  request += "\r\n";
  const Deadline deadline(receive_ms);
  if (ws->write(reinterpret_cast<const uint8_t*>(request.data()), request.size(), deadline)) return FALSE;
  std::string response;
  size_t end;
  while ((end = response.find("\r\n\r\n")) == std::string::npos) {
    uint8_t data[4096]; size_t got = 0;
    if (response.size() >= 65536 || ws->read(data, sizeof data, got, deadline)) return FALSE;
    response.append(reinterpret_cast<const char*>(data), got);
  }
  if (end > 65536) return FALSE;
  std::istringstream in(response.substr(0, end));
  std::string line, version;
  std::getline(in, line);
  std::istringstream status_line(line);
  if (!(status_line >> version >> status) || version != "HTTP/1.1" || status < 100 || status > 599) return FALSE;
  if (status != 101) return TRUE; // preserve HTTP errors for QueryHeaders
  std::string accept, upgrade, connection;
  while (std::getline(in, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return FALSE;
    const std::string name = lower(trim(line.substr(0, colon))), value = trim(line.substr(colon + 1));
    if (name == "sec-websocket-accept") { if (!accept.empty()) return FALSE; accept = value; }
    if (name == "upgrade") upgrade += value + ",";
    if (name == "connection") connection += value + ",";
    if (name == "sec-websocket-extensions" && !value.empty()) return FALSE;
  }
  if (accept != winhttp_ws::accept_key(key) || !has_token(upgrade, "websocket") || !has_token(connection, "upgrade")) return FALSE;
  const size_t body = end + 4;
  ws->decoder.feed(reinterpret_cast<const uint8_t*>(response.data() + body), response.size() - body);
  // The object address is only an opaque registry key, never dereferenced through HINTERNET.
  socket = ws.get();
  std::lock_guard<std::mutex> lock(sockets_mutex);
  sockets.emplace(socket, std::move(ws));
  return TRUE;
}

bool winhttp_ws_close(HINTERNET socket) {
  std::shared_ptr<WebSocket> ws;
  {
    std::lock_guard<std::mutex> lock(sockets_mutex);
    const auto it = sockets.find(socket);
    if (it == sockets.end()) return false;
    ws = std::move(it->second);
    sockets.erase(it);
  }
  ws->closed = true;
  ::shutdown(ws->fd, SHUT_RDWR); // wake a blocked receiver before curl is destroyed
  return true;
}
bool winhttp_ws_timeout(HINTERNET socket, DWORD timeout) {
  auto ws = find_socket(socket);
  if (!ws) return false;
  ws->receive_ms = (int)std::min<DWORD>(timeout, INT_MAX);
  return true;
}
DWORD WinHttpWebSocketSend(HINTERNET socket, WINHTTP_WEB_SOCKET_BUFFER_TYPE type, LPVOID buffer, DWORD size) {
  auto ws = find_socket(socket);
  if (!ws || (!buffer && size)) return invalid_parameter;
  if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
    return invalid_parameter;
  return ws->send(type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ? 1 : 2, static_cast<const uint8_t*>(buffer), size);
}
DWORD WinHttpWebSocketReceive(HINTERNET socket, LPVOID buffer, DWORD size, LPDWORD got, WINHTTP_WEB_SOCKET_BUFFER_TYPE* type) {
  auto ws = find_socket(socket);
  if (!ws || !buffer || !size || !got || !type) return invalid_parameter;
  *got = 0;
  size_t count = 0;
  winhttp_ws::Type decoded_type{};
  std::vector<uint8_t> control;
  const Deadline deadline(ws->receive_ms);
  for (;;) {
    if (ws->closed) return io_error;
    const auto result = ws->decoder.next(static_cast<uint8_t*>(buffer), size, count, decoded_type, control);
    if (result == winhttp_ws::Result::Error) return io_error;
    if (result == winhttp_ws::Result::Data || result == winhttp_ws::Result::Close) {
      *got = (DWORD)count;
      *type = static_cast<WINHTTP_WEB_SOCKET_BUFFER_TYPE>(decoded_type);
      return NO_ERROR;
    }
    if (result == winhttp_ws::Result::Ping) {
      const DWORD error = ws->send(10, control.data(), control.size());
      if (error) return error;
    }
    if (result != winhttp_ws::Result::NeedData) continue;
    uint8_t data[16384]; size_t received = 0;
    const DWORD error = ws->read(data, sizeof data, received, deadline);
    if (error) return error;
    ws->decoder.feed(data, received);
  }
}
DWORD WinHttpWebSocketShutdown(HINTERNET socket, USHORT status, LPVOID reason, DWORD length) {
  auto ws = find_socket(socket);
  if (!ws || length > 123 || (!reason && length)) return invalid_parameter;
  std::vector<uint8_t> data{uint8_t(status >> 8), uint8_t(status)};
  if (length) data.insert(data.end(), static_cast<const uint8_t*>(reason), static_cast<const uint8_t*>(reason) + length);
  return ws->send(8, data.data(), data.size());
}
