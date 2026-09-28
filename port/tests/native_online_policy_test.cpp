// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_online_policy.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using slippi::online::NativeGameplayProfile;
  using slippi::online::native_profile_mode_error;
  for (int mode = 0; mode <= 4; ++mode) {
    CHECK((native_profile_mode_error(NativeGameplayProfile::Vanilla, mode) == nullptr) == (mode != 0));
    CHECK((native_profile_mode_error(NativeGameplayProfile::Akaneia, mode) == nullptr) == (mode == 2));
    // Every gameplay mod is Direct-only (against the same build, checked in netplay).
    CHECK((native_profile_mode_error(NativeGameplayProfile::OtherMod, mode) == nullptr) == (mode == 2));
  }
  // Ranked is refused for every profile, with a message the game can show on its search screen.
  for (auto profile : {NativeGameplayProfile::Vanilla, NativeGameplayProfile::Akaneia, NativeGameplayProfile::OtherMod}) {
    const char* reason = native_profile_mode_error(profile, 0);
    CHECK(reason != nullptr && std::strstr(reason, "Ranked") != nullptr && std::strlen(reason) <= 120);
  }
  CHECK(native_profile_mode_error(NativeGameplayProfile::Vanilla, 5) != nullptr);
  CHECK(std::strstr(native_profile_mode_error(NativeGameplayProfile::Akaneia, 1), "Direct") != nullptr);
  CHECK(std::strstr(native_profile_mode_error(NativeGameplayProfile::OtherMod, 3), "Direct") != nullptr);
}
