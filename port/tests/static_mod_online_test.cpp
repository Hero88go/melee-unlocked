// Static mod Direct routing and complete-content identity regression.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_online_policy.h"
#include "mod_profile.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  namespace fs = std::filesystem;
  using namespace slippi::online;
  using source_port::mods::sha256_file;
  const fs::path root = fs::temp_directory_path() /
      ("mu-static-mod-online-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directories(root));
  // Same length, same fake code/FST metadata, different file payload: the former text+FST-only
  // identity would equate these. Whole-disc hashing must detect the change and refuse the match.
  std::vector<char> first(4096, '\0');
  const std::string header = "same DOL text; same FST offset and length";
  std::copy(header.begin(), header.end(), first.begin());
  first[3072] = 'A';
  auto second = first;
  second[3072] = 'B';
  { std::ofstream f(root / "one.iso", std::ios::binary); f.write(first.data(), first.size()); CHECK(f.good()); }
  { std::ofstream f(root / "copy.iso", std::ios::binary); f.write(first.data(), first.size()); CHECK(f.good()); }
  { std::ofstream f(root / "changed.iso", std::ios::binary); f.write(second.data(), second.size()); CHECK(f.good()); }
  const auto one = sha256_file(root / "one.iso"), copy = sha256_file(root / "copy.iso"), changed = sha256_file(root / "changed.iso");
  CHECK(native_mod_fingerprint_valid(one) && native_mod_fingerprint_valid(changed));
  CHECK(one == copy && one != changed);
  CHECK(native_direct_builds_match(true, one, true, copy));
  CHECK(!native_direct_builds_match(true, one, true, changed));
  CHECK(!native_direct_builds_match(true, one, false, ""));
  CHECK(!native_direct_builds_match(false, "", true, one));
  CHECK(native_direct_builds_match(false, "", false, ""));
  CHECK(!native_direct_builds_match(true, "", true, ""));
  CHECK(!native_direct_builds_match(true, one, true, one.substr(0, 63)));
  CHECK(!native_direct_builds_match(true, one, true, std::string(64, 'g')));
  CHECK(sha256_file(root / "missing.iso").empty());
  // A matched Static mod can select added fighters and stages. Source mod code and vanilla
  // cannot. Test both boundaries so a blanket removal of the limits is caught.
  CHECK(native_direct_selection_supported(true, true, 26, 0x56));
  CHECK(native_direct_selection_supported(true, true, 30, 0x70));
  CHECK(native_direct_selection_supported(true, false, 25, 0x55));
  CHECK(!native_direct_selection_supported(true, false, 26, 0x55));
  CHECK(!native_direct_selection_supported(true, false, 25, 0x56));
  CHECK(!native_direct_selection_supported(false, true, 26, 0x56));
  CHECK(native_direct_selection_supported(false, false, 25, 0x55));
  for (auto profile : {NativeGameplayProfile::Akaneia, NativeGameplayProfile::OtherMod}) {
    CHECK(native_profile_mode_error(profile, 2) == nullptr);
    CHECK(native_profile_mode_error(profile, 3) == nullptr);   // Teams: every player on the same build
    for (int mode : {0, 1, 4}) CHECK(native_profile_mode_error(profile, mode) != nullptr);
  }
  fs::remove_all(root);
  std::puts("Static mod online identity and selection tests passed");
}
