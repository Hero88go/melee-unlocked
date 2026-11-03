// Window-free YouGame GC content identity checks; synthetic data only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gc_content_sha1.h"
#include "sysgen.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace fs = std::filesystem;
// The portable existing SHA-1 is sufficient for these tiny test manifests and files.
struct TestSha1 {
  std::vector<uint8_t> bytes;
  void update(const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data); bytes.insert(bytes.end(), p, p + n);
  }
  std::string hex() { return host::sysgen::sha1(bytes); }
};
static void put(std::vector<uint8_t>& v, size_t at, uint32_t n) {
  for (int i = 3; i >= 0; --i) { v[at + i] = uint8_t(n); n >>= 8; }
}
static std::vector<uint8_t> image(bool moved) {
  std::vector<uint8_t> v(moved ? 0x9000 : 0x8000, moved ? 0xee : 0xaa);
  std::fill(v.begin(), v.begin() + 0x2480, 0);
  std::memcpy(v.data(), "GALE01", 6); v[7] = 2; put(v, 0x1c, 0xc2339f3d);
  const size_t dol = moved ? 0x4000 : 0x3000, fst = moved ? 0x5000 : 0x3800, data = moved ? 0x7000 : 0x6000;
  put(v, 0x420, dol); put(v, 0x424, fst); put(v, 0x428, moved ? 90 : 80);
  put(v, 0x2454, 16); put(v, 0x2458, 16);
  std::fill(v.begin() + dol, v.begin() + dol + 0x110, 0);
  put(v, dol, 0x100); put(v, dol + 0x90, 16);
  // Unused section slots must not extend the DOL.
  put(v, dol + 4, 0x70000000); put(v, dol + 0x98, 0x70000000);
  std::memcpy(v.data() + dol + 0x100, "synthetic code!!", 16);
  std::fill(v.begin() + fst, v.begin() + fst + 80, 0);
  put(v, fst, 0x01000000); put(v, fst + 8, 4);
  put(v, fst + 12, 0x01000001); put(v, fst + 20, 3);
  put(v, fst + 24, 5); put(v, fst + 28, data); put(v, fst + 32, 5);
  put(v, fst + 36, 11); put(v, fst + 40, data + 0x100);
  const char names[] = "\0Dir\0Case\xe9\0empty\0";
  std::memcpy(v.data() + fst + 48, names, sizeof names);
  std::memcpy(v.data() + data, "hello", 5);
  return v;
}
int main() {
  const auto root = fs::temp_directory_path() / ("mu-gc-content-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(root));
  const auto path = root / "synthetic.gcm";
  const auto hash = [&](const std::vector<uint8_t>& v) {
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(v.data()), v.size()); CHECK(out.good()); }
    return source_port::mods::gc_content_sha1(path, [] { return TestSha1{}; });
  };
  const auto original = image(false);
  const auto expected = hash(original);
  CHECK(expected == "c1f6194920e81ac8a956312a2bfbf8c16b1e45f3"); // independently generated gcManifest v2 fixture
  CHECK(hash(image(true)) == expected); // moved DOL/FST/files, filler, FST slack and size
  auto v = original; v[0x6002] ^= 1; CHECK(hash(v) != expected);
  v = original; v[0x500] ^= 1; CHECK(hash(v) != expected); // bi2
  v = original; v[0x40] ^= 1; CHECK(hash(v) != expected); // boot outside normalized offsets
  v = original; v[0x430] ^= 1; CHECK(hash(v) != expected); // normalization ends at 0x42c
  v = original; v[0x2470] ^= 1; CHECK(hash(v) != expected); // apploader trailer
  v = original; v[0x3100] ^= 1; CHECK(hash(v) != expected); // DOL
  v = original; v[0x3800 + 48 + 5] ^= 0x20; CHECK(hash(v) != expected); // raw name case
  v = original; put(v, 0x3800 + 20, 4); CHECK(hash(v) != expected); // directory structure
  v = original; put(v, 0x3800 + 28, 0xffffff00); CHECK(hash(v).empty());
  v = original; put(v, 0x428, 0xffffffff); CHECK(hash(v).empty());
  v = original; v.resize(0x100); CHECK(hash(v).empty());
  CHECK(source_port::mods::gc_content_sha1(root / "absent", [] { return TestSha1{}; }).empty());
  CHECK(fs::remove_all(root) == 2);
  std::puts("GC content SHA-1: layout invariance, covered regions, raw names and malformed inputs passed");
}
