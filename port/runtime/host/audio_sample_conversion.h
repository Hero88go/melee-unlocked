// Signed 16-bit source samples, stored left-aligned in packed 24-bit little-endian PCM.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace host {
inline void audio_store_pcm24_le(uint8_t* out, int16_t sample) {
  // Multiplication is defined for every int16_t value. Shifting a negative signed sample is not.
  const uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(sample) * 256);
  out[0] = static_cast<uint8_t>(bits);
  out[1] = static_cast<uint8_t>(bits >> 8);
  out[2] = static_cast<uint8_t>(bits >> 16);
}
}  // namespace host
