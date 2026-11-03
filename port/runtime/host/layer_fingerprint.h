// Fingerprint the active overlay bytes on every launch, never cached text.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "source_mod_overlay.h"
#include <algorithm>

namespace source_port {
template<class MakeSha256>
std::string layer_fingerprint(const ModOverlay& overlay, const std::string& profile,
                              MakeSha256 make_sha256) {
  auto total = make_sha256();
  std::vector<uint8_t> buffer(1 << 20);
  for (const auto& file : overlay.files()) {
    if (file.profile != profile) continue;
    total.update(file.path + "\t" + std::to_string(file.length) + "\n");
    for (uint64_t pos = 0; pos < file.length; pos += buffer.size()) {
      const uint32_t n = (uint32_t)std::min<uint64_t>(buffer.size(), file.length - pos);
      if (overlay.read(file.start + (uint32_t)pos, buffer.data(), n) != ModOverlay::Read::Success) return {};
      total.update(buffer.data(), n);
    }
  }
  for (const auto& report : overlay.iso_reports()) {
    if (report.profile != profile) continue;
    for (const auto& run : report.dol_runs)
      total.update("dol " + std::to_string(run.first) + " " + std::to_string(run.second) + "\n");
    for (const auto& gone : report.deleted) total.update("deleted " + gone + "\n");
  }
  return total.hex();
}
} // namespace source_port
