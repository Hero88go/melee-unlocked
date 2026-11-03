// Offline tests for the Linux WinHTTP WebSocket protocol. No libcurl required.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../winhttp_ws_protocol.h"
#include <cstdlib>
#include <iostream>

using namespace winhttp_ws;
namespace {
void check(bool ok, const char* description) {
  if (!ok) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
std::vector<uint8_t> frame(uint8_t opcode, const std::vector<uint8_t>& payload, bool fin = true) {
  std::vector<uint8_t> out{uint8_t((fin ? 128 : 0) | opcode)};
  if (payload.size() < 126) out.push_back(uint8_t(payload.size()));
  else if (payload.size() <= 65535) {
    out.insert(out.end(), {126, uint8_t(payload.size() >> 8), uint8_t(payload.size())});
  } else {
    out.push_back(127);
    for (int i = 7; i >= 0; --i) out.push_back(uint8_t(uint64_t(payload.size()) >> (i * 8)));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}
void feed(Decoder& decoder, const std::vector<uint8_t>& bytes) { decoder.feed(bytes.data(), bytes.size()); }
void test_mask() {
  const uint8_t hello[] = {'H', 'e', 'l', 'l', 'o'};
  check(encode(1, hello, sizeof hello, {0x37, 0xfa, 0x21, 0x3d}) ==
        std::vector<uint8_t>({0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}),
        "RFC masked Hello frame");
  for (size_t size : {size_t(0), size_t(125), size_t(126), size_t(65535), size_t(65536)}) {
    const std::vector<uint8_t> payload(size, 0xab);
    const std::array<uint8_t, 4> mask{1, 2, 3, 4};
    const auto encoded = encode(2, payload.data(), size, mask);
    const size_t offset = size < 126 ? 2 : size <= 65535 ? 4 : 10;
    check(encoded[0] == 0x82 && (encoded[1] & 128), "FIN binary frame is masked");
    uint64_t length = encoded[1] & 127;
    if (offset > 2) {
      length = 0;
      for (size_t i = 2; i < offset; ++i) length = (length << 8) | encoded[i];
    }
    check(length == size && encoded.size() == offset + 4 + size, "client frame length");
    check(std::equal(mask.begin(), mask.end(), encoded.begin() + offset), "mask in header");
    for (size_t i = 0; i < size; ++i) check((encoded[offset + 4 + i] ^ mask[i % 4]) == payload[i], "masked payload");
  }
}
void test_lengths() {
  for (uint8_t opcode : {1, 2}) {
    for (size_t size : {size_t(0), size_t(125), size_t(126), size_t(65535), size_t(65536)}) {
      std::vector<uint8_t> payload(size);
      for (size_t i = 0; i < size; ++i) payload[i] = uint8_t(i);
      const auto bytes = frame(opcode, payload);
      Decoder decoder;
      std::vector<uint8_t> assembled, control;
      uint8_t output[257]; size_t got = 0;
      Type type{}; bool complete = false;
      // Exercise split headers, payloads, and 16/64-bit length fields.
      for (size_t at = 0; at < bytes.size();) {
        const size_t count = std::min<size_t>(at < 10 ? 1 : 113, bytes.size() - at);
        decoder.feed(bytes.data() + at, count); at += count;
        for (;;) {
          const auto result = decoder.next(output, sizeof output, got, type, control);
          if (result == Result::NeedData) break;
          check(result == Result::Data, "length test returns data");
          assembled.insert(assembled.end(), output, output + got); got = 0;
          complete = type == (opcode == 1 ? Type::TextMessage : Type::BinaryMessage);
          if (complete) break;
          check(type == (opcode == 1 ? Type::TextFragment : Type::BinaryFragment), "partial buffer type");
        }
      }
      check(complete && assembled == payload, "length round trip");
    }
  }
}
void test_fragmentation() {
  Decoder decoder;
  feed(decoder, frame(1, {'H', 'e'}, false));
  feed(decoder, frame(9, {'p'}));
  feed(decoder, frame(10, {'q'}));
  feed(decoder, frame(0, {'l'}, false));
  feed(decoder, frame(0, {'l', 'o'}));
  uint8_t out[4]; size_t got = 0; Type type{}; std::vector<uint8_t> control;
  check(decoder.next(out, sizeof out, got, type, control) == Result::Ping && control == std::vector<uint8_t>({'p'}) && got == 2,
        "interleaved ping preserves partial text");
  check(decoder.next(out, sizeof out, got, type, control) == Result::Pong && control == std::vector<uint8_t>({'q'}), "pong event");
  check(decoder.next(out, sizeof out, got, type, control) == Result::Data && type == Type::TextFragment && got == 4 &&
        std::string(reinterpret_cast<char*>(out), got) == "Hell", "continuations fill caller buffer");
  got = 0;
  check(decoder.next(out, sizeof out, got, type, control) == Result::Data && type == Type::TextMessage && got == 1 && out[0] == 'o',
        "last continuation is a complete text message");
  feed(decoder, frame(2, {}, false)); feed(decoder, frame(0, {42}));
  got = 0;
  check(decoder.next(out, sizeof out, got, type, control) == Result::Data && type == Type::BinaryMessage && got == 1 && out[0] == 42,
        "binary continuation after empty first fragment");
  feed(decoder, frame(1, {'x'})); got = 0;
  check(decoder.next(out, 1, got, type, control) == Result::Data && type == Type::TextMessage && got == 1,
        "exact buffer fit is a message");
  feed(decoder, frame(8, {3, 232})); got = 0;
  check(decoder.next(out, sizeof out, got, type, control) == Result::Close && type == Type::Close && got == 0 &&
        control == std::vector<uint8_t>({3, 232}), "close status 1000");
}
void test_byte_stream() {
  Decoder decoder;
  const auto bytes = frame(1, {'o', 'k'});
  uint8_t out[8]; size_t got = 0; Type type{}; std::vector<uint8_t> control;
  for (size_t i = 0; i < bytes.size(); ++i) {
    decoder.feed(&bytes[i], 1);
    check(decoder.next(out, sizeof out, got, type, control) ==
          (i + 1 == bytes.size() ? Result::Data : Result::NeedData), "one byte TCP chunks");
  }
  check(got == 2 && type == Type::TextMessage && out[0] == 'o' && out[1] == 'k', "byte stream output");
  feed(decoder, frame(8, {})); got = 0;
  check(decoder.next(out, sizeof out, got, type, control) == Result::Close && control.empty(), "empty close");

  // Control payloads may also span reads. A ping must be returned only once it is complete.
  Decoder ping_decoder;
  const auto ping = frame(9, {'h', 'i'});
  got = 0;
  for (size_t i = 0; i < ping.size(); ++i) {
    ping_decoder.feed(&ping[i], 1);
    check(ping_decoder.next(out, sizeof out, got, type, control) ==
          (i + 1 == ping.size() ? Result::Ping : Result::NeedData), "split ping");
  }
  check(control == std::vector<uint8_t>({'h', 'i'}), "split ping payload");
  const auto pong = encode(10, control.data(), control.size(), {1, 2, 3, 4});
  check(pong == std::vector<uint8_t>({0x8a, 0x82, 1, 2, 3, 4, uint8_t('h' ^ 1), uint8_t('i' ^ 2)}), "masked pong");
  const uint8_t status[] = {3, 232};
  check(encode(8, status, sizeof status, {1, 2, 3, 4}) ==
        std::vector<uint8_t>({0x88, 0x82, 1, 2, 3, 4, 2, 234}), "masked close status 1000");
}
void test_invalid_frames() {
  const std::vector<std::vector<uint8_t>> invalid = {
    {0xc1, 0}, {0x81, 0x80}, {0x80, 0}, {0x83, 0}, {0x89, 126, 0, 126}, {0x09, 0},
    {0x88, 1}, {0x8b, 0}, {0x81, 126, 0, 1}, {0x82, 127, 0x80, 0, 0, 0, 0, 1, 0, 0},
    {0x82, 127, 0, 0, 0, 0, 0, 0, 0xff, 0xff}, {0x01, 0, 0x81, 0}
  };
  for (const auto& bytes : invalid) {
    Decoder decoder; feed(decoder, bytes);
    uint8_t out[8]; size_t got = 0; Type type{}; std::vector<uint8_t> control;
    check(decoder.next(out, sizeof out, got, type, control) == Result::Error, "malformed frame rejected");
  }
}
} // namespace
int main() {
  check(accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 Accept key");
  test_mask(); test_lengths(); test_fragmentation(); test_byte_stream(); test_invalid_frames();
  std::cout << "ws_frame_test: all tests passed\n";
}
