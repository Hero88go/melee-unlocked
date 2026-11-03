// Signed 16-bit source samples, stored left-aligned in packed 24-bit little-endian PCM.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace host {
// Mixer samples stay in floating-point 16-bit sample units until the endpoint's final conversion.
inline int32_t audio_quantize_pcm(double sample, int bits) {
  if (!std::isfinite(sample)) sample = 0.0;
  const double scale = std::ldexp(1.0, bits - 16);
  const double minimum = -std::ldexp(1.0, bits - 1);
  const double maximum = std::ldexp(1.0, bits - 1) - 1.0;
  return (int32_t)std::llround(std::clamp(sample * scale, minimum, maximum));
}
inline float audio_pcm_float(double sample) {
  if (!std::isfinite(sample)) return 0.0f;
  return (float)std::clamp(sample / 32768.0, -1.0, 1.0);
}
inline void audio_store_pcm24_le(uint8_t* out, float sample) {
  const uint32_t bits = (uint32_t)audio_quantize_pcm(sample, 24);
  out[0] = (uint8_t)bits; out[1] = (uint8_t)(bits >> 8); out[2] = (uint8_t)(bits >> 16);
}
inline void audio_store_pcm24_le(uint8_t* out, int16_t sample) {
  // Multiplication is defined for every int16_t value. Shifting a negative signed sample is not.
  const uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(sample) * 256);
  out[0] = static_cast<uint8_t>(bits);
  out[1] = static_cast<uint8_t>(bits >> 8);
  out[2] = static_cast<uint8_t>(bits >> 16);
}
}  // namespace host
