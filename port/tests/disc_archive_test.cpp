// Reject damaged startup assets before native archive parsing touches them.
#include "disc_archive.h"
#include <array>
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  std::array<uint8_t, 32> h{};
  std::string error;
  const auto put = [&](size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) h[at + i] = uint8_t(value >> (24 - i * 8));
  };
  // The reported LbRb.dat is 0x415 bytes but has a zero header.
  CHECK(!host::disc_archive_header(h.data(), h.size(), 0x415, &error));
  CHECK(error.find("length is 0") != std::string::npos);
  CHECK(error.find("1045") != std::string::npos);
  put(0, 0x415); put(4, 0x340); put(8, 16); put(12, 1);
  CHECK(host::disc_archive_header(h.data(), h.size(), 0x415, &error));
  CHECK(error.empty());
  put(8, 0xffffffffu); put(12, 0xffffffffu); put(16, 0xffffffffu);
  CHECK(!host::disc_archive_header(h.data(), h.size(), 0x415, &error));
  CHECK(!host::disc_archive_header(h.data(), 31, 0x415, &error));
  CHECK(!host::disc_archive_header(nullptr, 32, 0x415, &error));
  CHECK(!host::disc_archive_header(h.data(), 32, 0, &error));
  std::puts("disc_archive: zeroed headers, truncation and table overflow rejected");
  return 0;
}
