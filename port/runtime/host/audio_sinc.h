// Band-limited stereo sample-rate conversion shared by game audio and streamed music.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace host::audio_resampling {
constexpr int SINC_TAPS = 128, SINC_PHASES = 512;
struct SincTable {
  uint32_t device_rate = 0;
  int taps = SINC_TAPS;
  double cutoff = 0.0;
  std::vector<float> k;

  void build(uint32_t rate, uint32_t source_rate = 32000) {
    device_rate = rate;
    const double ratio = std::min(1.0, (double)rate / source_rate);
    const double next_cutoff = 0.49 * ratio;
    if (cutoff == next_cutoff) return;  // every upsampling rate shares the same kernel
    cutoff = next_cutoff;
    // Keep the transition width relative to the lower Nyquist when downsampling high-rate music.
    taps = SINC_TAPS * (int)std::ceil(1.0 / ratio);
    k.assign((SINC_PHASES + 1) * taps, 0.0f);
    const double beta = 10.0;
    auto bessel0 = [](double x) { double s = 1, t = 1; for (int i = 1; i < 30; ++i) { t *= (x / (2 * i)) * (x / (2 * i)); s += t; } return s; };
    const double denom = bessel0(beta);
    for (int p = 0; p <= SINC_PHASES; ++p) {
      const double frac = (double)p / SINC_PHASES;
      double sum = 0;
      for (int j = 0; j < taps; ++j) {
        const double x = (double)(j - (taps / 2 - 1)) - frac;
        const double r = x / (taps / 2);
        const double w = std::abs(r) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - r * r)) / denom;
        const double s = x == 0.0 ? 2 * cutoff : std::sin(2 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
        k[p * taps + j] = (float)(s * w);
        sum += s * w;
      }
      for (int j = 0; j < taps; ++j) k[p * taps + j] = (float)(k[p * taps + j] / sum);
    }
  }
  const float* phase(double fraction) const {
    return &k[std::min((int)(fraction * SINC_PHASES + 0.5), SINC_PHASES) * taps];
  }
  // Interpolate adjacent phases instead of rounding the fractional delay to 1/512 of a sample.
  // Rounding that delay produces modulation artifacts even when the filter's stopband is clean.
  template<class Sample>
  double sample(double fraction, Sample&& at) const {
    const double position = fraction * SINC_PHASES;
    const int p = std::min((int)position, SINC_PHASES - 1);
    const double blend = position - p;
    const float* a = &k[p * taps];
    const float* b = a + taps;
    double value = 0.0;
    for (int j = 0; j < taps; ++j)
      value += (a[j] + (b[j] - a[j]) * blend) * at(j - (taps / 2 - 1));
    return value;
  }
  template<class StereoSample>
  std::array<double, 2> stereo(double fraction, StereoSample&& at) const {
    const double position = fraction * SINC_PHASES;
    const int p = std::min((int)position, SINC_PHASES - 1);
    const double blend = position - p;
    const float* a = &k[p * taps];
    const float* b = a + taps;
    std::array<double, 2> value{};
    for (int j = 0; j < taps; ++j) {
      const double coefficient = a[j] + (b[j] - a[j]) * blend;
      const auto* frame = at(j - (taps / 2 - 1));
      value[0] += coefficient * frame[0];
      value[1] += coefficient * frame[1];
    }
    return value;
  }
};
}  // namespace host::audio_resampling
