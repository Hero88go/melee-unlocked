// Optional process-local file filter for the Static Recomp's memory card mount.
// Keep the card SDK independent of the app's mod scanner and YouGame bridge.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>

namespace hle::card {
using FileFilter = bool (*)(const std::filesystem::path&);
inline FileFilter file_filter = nullptr;  // Set before the first mount; null keeps standalone behavior.
}  // namespace hle::card
