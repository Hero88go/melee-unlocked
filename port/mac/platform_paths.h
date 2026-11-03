// Bundle resource paths; no working-directory assumptions or writes to Resources.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <mach-o/dyld.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace mac_host {
inline std::filesystem::path executable() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> path(size);
  if (_NSGetExecutablePath(path.data(), &size)) throw std::runtime_error("Cannot locate executable");
  return std::filesystem::weakly_canonical(path.data());
}
inline std::filesystem::path resources() {
  const auto directory = executable().parent_path();
  return directory.filename() == "MacOS" ? directory.parent_path() / "Resources" : directory;
}
} // namespace mac_host
