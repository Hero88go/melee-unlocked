// POSIX pad attachment carried by the app's pads push. No JSON/runtime dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>

namespace yougame::pad_mirror {
struct Attachment {
  int fd = -1;
  size_t size = 0;
  bool operator==(const Attachment& other) const { return fd == other.fd && size == other.size; }
};

// Keep the wire object's location here so the app handoff can change independently.
template<class Json> bool attachment(const Json& push, Attachment& out) {
  const auto mirror = push.find("posix");   // {type:"pads", posix:{version:1, fd:N, size:S}} (controller-lead, linux-lead 10-25)
  if (mirror == push.end() || !mirror->is_object()) return false;
  const auto number = [&](const char* key, uint64_t max, uint64_t& value) {
    const auto n = mirror->find(key);
    if (n == mirror->end() || !n->is_number_integer() ||
        (!n->is_number_unsigned() && n->template get<int64_t>() < 0)) return false;
    value = n->template get<uint64_t>();
    return value <= max;
  };
  uint64_t version, fd, size;
  if (!number("version", 1, version) || version != 1 ||
      !number("fd", INT_MAX, fd) || !number("size", SIZE_MAX, size)) return false;
  out = {static_cast<int>(fd), static_cast<size_t>(size)};
  return true; // The reader checks descriptor kind, bounds and mapped header.
}
} // namespace yougame::pad_mirror
