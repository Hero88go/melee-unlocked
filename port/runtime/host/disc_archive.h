// A native archive must contain its header before the game parses or relocates it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace host {
inline bool disc_archive_header(const uint8_t* header, size_t bytes, uint32_t file_size,
                                std::string* error) {
  if (!header || bytes < 0x20 || file_size < 0x20) {
    *error = "archive header is missing or truncated";
    return false;
  }
  const auto word = [&](size_t offset) {
    return uint32_t(header[offset]) << 24 | uint32_t(header[offset + 1]) << 16 |
           uint32_t(header[offset + 2]) << 8 | header[offset + 3];
  };
  if (word(0) != file_size) {
    char message[112];
    std::snprintf(message, sizeof message, "archive header length is %u, but the disc file is %u bytes",
                  word(0), file_size);
    *error = message;
    return false;
  }
  const uint64_t tables_end = 0x20ull + word(4) + uint64_t(word(8)) * 4 +
                             (uint64_t(word(12)) + word(16)) * 8;
  if (tables_end > file_size) {
    *error = "archive data or relocation tables extend beyond the disc file";
    return false;
  }
  error->clear();
  return true;
}
}  // namespace host
