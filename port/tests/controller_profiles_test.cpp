// Controller profiles: saved and loaded exactly, kept to their own kind of device, and safe names.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "controller_profiles.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

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

  for (int family=0;family<(int)host::ProfileDevice::Count;++family) {
    host::ProfileBindings original{},loaded{};
    for (int action=0;action<host::kProfileActions;++action) original[action]=0x80000000u | (family*100+action+1);
    const std::string name="All actions "+std::to_string(family);
    CHECK(host::profile_save((host::ProfileDevice)family,name,original));
    CHECK(host::profile_load((host::ProfileDevice)family,name,loaded));
    CHECK(original==loaded);
    CHECK(host::profile_delete((host::ProfileDevice)family,name));
  }
  // Names: what is safe in a file name survives, the rest goes.
  CHECK(host::profile_clean_name("My Box 2") == "My Box 2");
  CHECK(host::profile_clean_name("  ../../evil  ") == "evil");
  CHECK(host::profile_clean_name("a/b\\c:d*e?f") == "abcdef");
  CHECK(host::profile_clean_name("...") == "");
  CHECK(host::profile_clean_name(std::string(60, 'x')).size() == 40);

  // Round trip, including values only a HID pad or the keyboard would use.
  host::ProfileBindings box{};
  for (int i = 0; i < host::kProfileActions; ++i) box[i] = (1u << (i % 20 + 12)) | static_cast<uint32_t>(i);
  box[4] = 0;   // an unbound action stays unbound
  CHECK(host::profile_save(host::ProfileDevice::Hid, "B0XX tournament", box));
  host::ProfileBindings read{};
  CHECK(host::profile_load(host::ProfileDevice::Hid, "B0XX tournament", read));
  CHECK(read == box);

  // A pre-modifier file has exactly the original 22 named entries. Its bindings keep their
  // meanings and the five missing actions keep defaults supplied by the caller.
  constexpr const char* legacy_actions[] = {
      "A", "B", "X", "Y", "Z", "Start", "L", "R", "DUp", "DDown", "DLeft", "DRight",
      "CUp", "CDown", "CLeft", "CRight", "SUp", "SDown", "SLeft", "SRight", "LAnalog", "RAnalog"};
  static_assert(std::size(legacy_actions) == static_cast<size_t>(host::BindAction::ModX));
  {
    std::ofstream f(root / "ControllerProfiles" / "legacy.profile");
    f << "# Melee Unlocked controller profile\ndevice keyboard\n";
    for (size_t i = 0; i < std::size(legacy_actions); ++i) f << legacy_actions[i] << " " << 40 + i << "\n";
  }
  for (uint32_t default_binding : {0u, 123u}) {
    host::ProfileBindings defaults{};
    for (size_t i = std::size(legacy_actions); i < defaults.size(); ++i) defaults[i] = default_binding;
    read = defaults;
    CHECK(host::profile_load(host::ProfileDevice::Keyboard, "legacy", read));
    for (size_t i = 0; i < read.size(); ++i)
      CHECK(read[i] == (i < std::size(legacy_actions) ? 40 + i : defaults[i]));
  }
  CHECK(host::profile_delete(host::ProfileDevice::Keyboard, "legacy"));

  // Still older profiles leave the stick keys and trigger migration sentinels untouched.
  { std::ofstream f(root / "ControllerProfiles" / "pre-trigger.profile"); f << "device keyboard\nA 88\nL 65\n"; }
  read.fill(7);
  read[static_cast<int>(host::BindAction::LAnalog)] = host::kProfileActionAbsent;
  read[static_cast<int>(host::BindAction::RAnalog)] = host::kProfileActionAbsent;
  CHECK(host::profile_load(host::ProfileDevice::Keyboard, "pre-trigger", read));
  CHECK(read[0] == 88 && read[1] == 0 && read[static_cast<int>(host::BindAction::L)] == 65);
  CHECK(read[static_cast<int>(host::BindAction::SUp)] == 7);
  CHECK(read[static_cast<int>(host::BindAction::LAnalog)] == host::kProfileActionAbsent);
  CHECK(read[static_cast<int>(host::BindAction::RAnalog)] == host::kProfileActionAbsent);
  CHECK(host::profile_delete(host::ProfileDevice::Keyboard, "pre-trigger"));

  // Extra named actions from a future build are ignored, just as a 22-action reader ignores
  // ModX/ModY/LightShield/MidShield/StickMod in the current format.
  { std::ofstream f(root / "ControllerProfiles" / "B0XX tournament.profile", std::ios::app); f << "FutureAction 12345\n"; }
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
