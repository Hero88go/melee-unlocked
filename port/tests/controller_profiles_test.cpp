// Controller profiles: saved and loaded exactly, kept to their own kind of device, and safe names.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "controller_profiles.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
}

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "melee_unlocked_profile_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  host::profiles_set_folder((root / "port-settings.ini").string());
  CHECK(fs::path(host::profiles_folder()) == root / "ControllerProfiles");

  // Names: what is safe in a file name survives, the rest goes.
  CHECK(host::profile_clean_name("My Box 2") == "My Box 2");
  CHECK(host::profile_clean_name("  ../../evil  ") == "evil");
  CHECK(host::profile_clean_name("a/b\\c:d*e?f") == "abcdef");
  CHECK(host::profile_clean_name("...") == "");
  CHECK(host::profile_clean_name(std::string(60, 'x')).size() == 40);

  // Round trip, including values only a HID pad or the keyboard would use.
  host::ProfileBindings box{};
  for (int i = 0; i < host::kProfileActions; ++i) box[i] = 1u << (i + 18);
  box[4] = 0;   // an unbound action stays unbound
  CHECK(host::profile_save(host::ProfileDevice::Hid, "B0XX tournament", box));
  host::ProfileBindings read{};
  CHECK(host::profile_load(host::ProfileDevice::Hid, "B0XX tournament", read));
  CHECK(read == box);

  // Never loads onto, is never listed for, and is never deleted by another kind of device.
  host::ProfileBindings untouched{};
  untouched.fill(7);
  CHECK(!host::profile_load(host::ProfileDevice::XInput, "B0XX tournament", untouched));
  CHECK(untouched[0] == 7);
  CHECK(host::profile_list(host::ProfileDevice::XInput).empty());
  CHECK(!host::profile_delete(host::ProfileDevice::XInput, "B0XX tournament"));
  CHECK(host::profile_list(host::ProfileDevice::Hid).size() == 1);

  // Several profiles per device come back sorted, case-insensitively.
  host::ProfileBindings keys{};
  keys[0] = 'X';
  CHECK(host::profile_save(host::ProfileDevice::Keyboard, "zed", keys));
  CHECK(host::profile_save(host::ProfileDevice::Keyboard, "Alpha", keys));
  const auto listed = host::profile_list(host::ProfileDevice::Keyboard);
  CHECK(listed.size() == 2 && listed[0] == "Alpha" && listed[1] == "zed");

  // Saving over a profile replaces it; a name that is not already clean is refused, not rewritten.
  keys[0] = 'Y';
  CHECK(host::profile_save(host::ProfileDevice::Keyboard, "Alpha", keys));
  CHECK(host::profile_load(host::ProfileDevice::Keyboard, "Alpha", read) && read[0] == 'Y');
  CHECK(!host::profile_save(host::ProfileDevice::Keyboard, "../escape", keys));
  CHECK(!fs::exists(root / "escape.profile"));

  // A hand-edited file with junk lines still loads what it can; a missing action is unbound.
  { std::ofstream f(root / "ControllerProfiles" / "hand.profile"); f << "# comment\ndevice gcadapter\nA 256\nnonsense\nB notanumber\n"; }
  CHECK(host::profile_load(host::ProfileDevice::GCAdapter, "hand", read));
  CHECK(read[0] == 256 && read[1] == 0);

  CHECK(host::profile_delete(host::ProfileDevice::Hid, "B0XX tournament"));
  CHECK(host::profile_list(host::ProfileDevice::Hid).empty());

  fs::remove_all(root, ec);
  std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "all controller profile checks passed", g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
