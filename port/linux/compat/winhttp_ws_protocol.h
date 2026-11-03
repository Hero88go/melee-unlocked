// Pure RFC 6455 framing and handshake helpers. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace winhttp_ws {
inline std::string base64(const uint8_t* data, size_t size) {
#ifdef __APPLE__
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(4 * ((size + 2) / 3));
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t bits = (uint32_t(data[i]) << 16) |
        (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) |
        (i + 2 < size ? data[i + 2] : 0);
    out.push_back(alphabet[(bits >> 18) & 63]);
    out.push_back(alphabet[(bits >> 12) & 63]);
    out.push_back(i + 1 < size ? alphabet[(bits >> 6) & 63] : '=');
    out.push_back(i + 2 < size ? alphabet[bits & 63] : '=');
  }
#else
  std::string out(4 * ((size + 2) / 3), '\0');
  // EVP also writes a trailing NUL.
  out.resize(out.size() + 1);
  const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]), data, (int)size);
  out.resize(n);
#endif
  return out;
}
inline std::string accept_key(const std::string& key) {
  const std::string text = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
#ifdef __APPLE__
  unsigned char digest[CC_SHA1_DIGEST_LENGTH];
  if (!CC_SHA1(text.data(), static_cast<CC_LONG>(text.size()), digest)) return {};
  return base64(digest, sizeof digest);
#else
  unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int size = 0;
  if (!EVP_Digest(text.data(), text.size(), digest, &size, EVP_sha1(), nullptr)) return {};
  return base64(digest, size);
#endif
}
inline std::vector<uint8_t> encode(uint8_t opcode, const uint8_t* data, size_t size,
                                   const std::array<uint8_t, 4>& mask) {
  std::vector<uint8_t> out{uint8_t(0x80 | opcode)};
  if (size < 126) out.push_back(uint8_t(0x80 | size));
  else {
    out.push_back(size <= 65535 ? 0xfe : 0xff);
    for (int shift = size <= 65535 ? 8 : 56; shift >= 0; shift -= 8)
      out.push_back(uint8_t(uint64_t(size) >> shift));
  }
  out.insert(out.end(), mask.begin(), mask.end());
  for (size_t i = 0; i < size; ++i) out.push_back(data[i] ^ mask[i % 4]);
  return out;
}

enum class Result { NeedData, Data, Ping, Pong, Close, Error };
enum class Type { BinaryMessage = 0, BinaryFragment = 1, TextMessage = 2, TextFragment = 3, Close = 4 };

// One reader owns this state. feed() accepts arbitrary TCP chunks; next() assembles across
// continuation frames, leaving control frames visible to the caller so it can answer pings.
class Decoder {
 public:
  void feed(const uint8_t* data, size_t size) { bytes_.insert(bytes_.end(), data, data + size); }
  Result next(uint8_t* out, size_t capacity, size_t& got, Type& type, std::vector<uint8_t>& control) {
    for (;;) {
      if (!frame_) {
        if (bytes_.size() < 2) return Result::NeedData;
        const uint8_t first = bytes_[0], second = bytes_[1];
        opcode_ = first & 15; fin_ = (first & 0x80) != 0;
        if ((first & 0x70) || (second & 0x80)) return Result::Error; // server frames are unmasked
        const uint8_t length = second & 127;
        size_t header = length < 126 ? 2 : length == 126 ? 4 : 10;
        if (bytes_.size() < header) return Result::NeedData;
        remaining_ = length < 126 ? length : 0;
        for (size_t i = 2; i < header; ++i) remaining_ = (remaining_ << 8) | bytes_[i];
        if ((length == 126 && remaining_ < 126) || (length == 127 && (remaining_ < 65536 || (bytes_[2] & 128))))
          return Result::Error;
        if (opcode_ >= 8) {
          if ((opcode_ != 8 && opcode_ != 9 && opcode_ != 10) || !fin_ || remaining_ > 125 ||
              (opcode_ == 8 && remaining_ == 1)) return Result::Error;
        } else if (opcode_ == 0) {
          if (!message_) return Result::Error;
        } else if ((opcode_ != 1 && opcode_ != 2) || message_) return Result::Error;
        else message_ = opcode_;
        bytes_.erase(bytes_.begin(), bytes_.begin() + header);
        frame_ = true;
      }
      if (opcode_ >= 8) {
        if (bytes_.size() < remaining_) return Result::NeedData;
        control.assign(bytes_.begin(), bytes_.begin() + size_t(remaining_));
        bytes_.erase(bytes_.begin(), bytes_.begin() + size_t(remaining_));
        frame_ = false;
        if (opcode_ == 8) { got = 0; type = Type::Close; return Result::Close; }
        return opcode_ == 9 ? Result::Ping : Result::Pong;
      }
      const size_t n = size_t(std::min<uint64_t>(remaining_, std::min(bytes_.size(), capacity - got)));
      if (n) std::memcpy(out + got, bytes_.data(), n);
      bytes_.erase(bytes_.begin(), bytes_.begin() + n);
      got += n; remaining_ -= n;
      if (!remaining_) {
        frame_ = false;
        if (fin_) {
          type = message_ == 1 ? Type::TextMessage : Type::BinaryMessage;
          message_ = 0;
          return Result::Data;
        }
      }
      if (got == capacity) {
        type = message_ == 1 ? Type::TextFragment : Type::BinaryFragment;
        return Result::Data;
      }
      if (frame_) return Result::NeedData;
    }
  }
 private:
  std::vector<uint8_t> bytes_;
  uint64_t remaining_ = 0;
  uint8_t opcode_ = 0, message_ = 0;
  bool frame_ = false, fin_ = false;
};
} // namespace winhttp_ws
