// Named controller profiles: a saved set of the twelve GameCube action bindings for one kind of
// device, so a player can keep one layout for a box controller and another for a pad, or share one,
// and switch without rebinding every button.
//
// A profile is a small text file in a ControllerProfiles folder beside port-settings.ini:
//
//   # Melee Unlocked controller profile
//   device hid
//   A 1
//   B 2
//   ...
//
// The numbers are what the settings file already stores for that device (a virtual-key code for the
// keyboard, a button mask for every pad), so a profile means exactly what the live binding meant.
// A profile is only offered to, and only loads onto, the kind of device it was saved from: the same
// number is a different button on a different kind of controller.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace host {

enum class ProfileDevice : uint8_t { Keyboard, XInput, PlayStation, GCAdapter, SwitchPro, Hid, Count };

constexpr int kProfileActions = 16;   // BindAction::Count, in BindAction order (C-stick directions last)
using ProfileBindings = std::array<uint32_t, kProfileActions>;

// The folder profiles live in, from the settings file's own path.
void profiles_set_folder(const std::string& settings_path);
std::string profiles_folder();

// Keeps what is safe in a file name: letters, digits, spaces, '-', '_' and '.', trimmed, at most 40.
// Empty when nothing usable is left.
std::string profile_clean_name(const std::string& name);

// Profile names saved for this kind of device, sorted.
std::vector<std::string> profile_list(ProfileDevice device);
bool profile_save(ProfileDevice device, const std::string& name, const ProfileBindings& bindings);
// False, with `bindings` untouched, if the file is missing, unreadable or for another kind of device.
bool profile_load(ProfileDevice device, const std::string& name, ProfileBindings& bindings);
bool profile_delete(ProfileDevice device, const std::string& name);

const char* profile_device_key(ProfileDevice device);   // "keyboard", "xinput", ... as written in the file

}  // namespace host
