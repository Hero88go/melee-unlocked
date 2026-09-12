// Preserve ordered resource execution while guaranteeing presentation progress.
#pragma once
#include <cstddef>
namespace gx {
class DrainPolicy {
  unsigned consecutive_ = 0;
public:
  static constexpr unsigned maximum_consecutive = 2;
  bool drain(std::size_t pending) {
    if (pending >= 3 && consecutive_ < maximum_consecutive) { ++consecutive_; return true; }
    consecutive_ = 0;
    return false;
  }
};
}
