// Shader caches of earlier builds: removed so an installation does not grow with every update.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>

namespace gx {

// Each build keeps its compiled shaders in <root>/<64-hex shader hash>; an update brings a new hash
// and the old folder was never read again (a report, 11-11: the game folder grew with every update,
// up to 4 GB). Only folders named like a shader hash are removed, never the current one; any error
// leaves the folder in place. Returns the bytes freed.
inline unsigned long long prune_old_shader_caches(const std::string& root, const std::string& current) {
  namespace fs = std::filesystem;
  const auto is_hash = [](const std::string& name) {
    if (name.size() != 64) return false;
    for (char c : name) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
  };
  unsigned long long freed = 0;
  std::error_code ec;
  for (fs::directory_iterator it(fs::u8path(root), ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_directory(e2) || e2) continue;
    const std::string name = it->path().filename().u8string();
    if (name == current || !is_hash(name)) continue;
    unsigned long long size = 0;
    for (fs::recursive_directory_iterator f(it->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
      std::error_code e3;
      if (f->is_regular_file(e3) && !e3) size += f->file_size(e3);
    }
    if (fs::remove_all(it->path(), e2) != static_cast<std::uintmax_t>(-1) && !e2) freed += size;
  }
  return freed;
}

}  // namespace gx
