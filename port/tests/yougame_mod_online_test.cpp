// YouGame Ranked/Unranked mod isolation, extended content and room compatibility regression.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_online_policy.h"
#include "yougame_compatibility.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"
#else
#include "slippi_online.h"
#endif
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using namespace slippi::online;
  // Both real and no-Slippi LocalBuild default to Source, even for a retail content view.
  LocalBuild build;
  CHECK(!build.static_engine);
#ifdef MELEE_NO_SLIPPI
  CHECK(!local_build_storage().static_engine);
  build.static_engine = true;
  for (bool mod_view : {false, true}) {
    build.mod_view = mod_view;
    set_local_build(build);
    CHECK(local_build().static_engine && local_build().mod_view == mod_view);
  }
  set_local_build(LocalBuild{});
  CHECK(!local_build().static_engine);
#endif
  const std::string fp = "0123456789abcdef" + std::string(48, 'a');
  const std::string other = std::string(64, 'b');
  // The room key is a prefilter; the full fingerprint must still match at the peer check.
  std::string same_prefix = fp;
  same_prefix.back() = 'b';
  CHECK(native_yougame_online_builds_match(true, fp, true, fp));
  CHECK(!native_yougame_online_builds_match(true, fp, true, other));
  CHECK(!native_yougame_online_builds_match(true, fp, true, same_prefix));
  CHECK(!native_yougame_online_builds_match(true, fp, false, ""));
  CHECK(!native_yougame_online_builds_match(false, "", true, fp));
  CHECK(native_yougame_online_builds_match(false, "", false, ""));
  for (const std::string invalid : {std::string(), fp.substr(0, 63), fp + "0",
                                   std::string(64, 'g'), std::string(64, 'A')}) {
    CHECK(!native_yougame_online_builds_match(true, invalid, true, fp));
    CHECK(!native_yougame_online_builds_match(true, fp, true, invalid));
    CHECK(!native_yougame_online_builds_match(true, invalid, true, invalid));
    CHECK(yougame::compatibility_key("0.9.18", true, invalid).empty());
    CHECK(yougame::compatibility_key("0.9.18", true, invalid, true).empty());
    for (int mode : {0, 1}) {
      CHECK(!native_yougame_online_mod_content(true, mode, true, true, invalid));
      CHECK(native_yougame_online_build_verdict(true, fp, true, true, invalid, false) == -1);
      CHECK(native_yougame_online_build_verdict(true, invalid, true, true, fp, false) == -1);
    }
  }
  // Only an extended-content mod in YouGame Ranked/Unranked relaxes the roster limits.
  for (bool yougame_mode : {false, true})
    for (int mode = 0; mode <= 4; ++mode)
      for (bool mod_view : {false, true})
        for (bool extended_content : {false, true})
          CHECK(native_yougame_online_mod_content(yougame_mode, mode, mod_view, extended_content, fp) ==
                (yougame_mode && (mode == 0 || mode == 1) && mod_view && extended_content));
  // Both mod modes use this peer verdict, regardless of the Direct unverified-peer setting.
  for (int mode : {0, 1}) {
    CHECK(native_profile_mode_error(NativeGameplayProfile::Akaneia, mode, true) == nullptr);
    CHECK(native_yougame_online_mod_content(true, mode, true, true, fp));
    CHECK(native_yougame_online_build_verdict(true, fp, true, true, fp, false) == 1);
    CHECK(native_yougame_online_build_verdict(true, fp, true, true, fp, true) == 1);
    CHECK(native_yougame_online_build_verdict(true, fp, true, true, other, false) == -1);
    CHECK(native_yougame_online_build_verdict(true, fp, true, true, same_prefix, false) == -1);
    CHECK(native_yougame_online_build_verdict(true, fp, true, false, "", false) == -1);
    CHECK(native_yougame_online_build_verdict(false, "", true, true, fp, false) == -1);
    // No build received: wait before the deadline, refuse after it, even if stale data matches.
    CHECK(native_yougame_online_build_verdict(true, fp, false, false, "", false) == 0);
    CHECK(native_yougame_online_build_verdict(true, fp, false, false, "", true) == -1);
    CHECK(native_yougame_online_build_verdict(true, fp, false, true, fp, true) == -1);
  }
  // Compatibility wrappers share the online policy.
  CHECK(native_yougame_unranked_builds_match(true, fp, true, fp));
  CHECK(native_yougame_unranked_mod_content(true, 0, true, true, fp));
  CHECK(yougame::compatibility_key("0.9.18", false, "") == "mu-0.9.18");
  CHECK(yougame::compatibility_key("dev build/+", false, fp) == "mu-dev-build--");
  CHECK(yougame::compatibility_key(std::string(100, 'v'), false, "") == "mu-" + std::string(61, 'v'));
  const auto mod_key = yougame::compatibility_key("0.9.18", true, fp);
  CHECK(mod_key == "mu-0.9.18-mod-0123456789abcdef");
  CHECK(mod_key != yougame::compatibility_key("0.9.18", false, ""));
  CHECK(mod_key != yougame::compatibility_key("0.9.18", true, other));
  CHECK(mod_key == yougame::compatibility_key("0.9.18", true, same_prefix));
  const auto long_key = yougame::compatibility_key(std::string(100, 'v'), true, fp);
  CHECK(long_key.size() == 64 && long_key.substr(43) == "-mod-0123456789abcdef");
  CHECK(long_key != yougame::compatibility_key(std::string(100, 'v'), true, other));
  CHECK(yougame::compatibility_key("0.9.18", false, "", false) == "mu-0.9.18");
  CHECK(yougame::compatibility_key("0.9.18", true, fp, false) == "mu-0.9.18-mod-0123456789abcdef");
  CHECK(yougame::compatibility_key("0.9.18", false, "", true) == "mu-0.9.18-st");
  CHECK(yougame::compatibility_key("0.9.18", true, fp, true) == "mu-0.9.18-st-mod-0123456789abcdef");
  CHECK(yougame::compatibility_key("dev build/+", false, fp, true) == "mu-dev-build---st");
  // Every truncation boundary reserves the entire engine and mod suffix. Source retains the
  // exact old key at each length; Static must remain separate even when the version fills it.
  for (size_t length = 0; length <= 100; ++length) {
    const std::string version(length, 'v');
    const auto source_retail = yougame::compatibility_key(version, false, "");
    const auto static_retail = yougame::compatibility_key(version, false, "", true);
    const auto source_mod = yougame::compatibility_key(version, true, fp);
    const auto static_mod = yougame::compatibility_key(version, true, fp, true);
    CHECK(source_retail == "mu-" + std::string(std::min(length, size_t{61}), 'v'));
    CHECK(source_mod == "mu-" + std::string(std::min(length, size_t{40}), 'v') + "-mod-0123456789abcdef");
    CHECK(static_retail == "mu-" + std::string(std::min(length, size_t{58}), 'v') + "-st");
    CHECK(static_mod == "mu-" + std::string(std::min(length, size_t{37}), 'v') + "-st-mod-0123456789abcdef");
    CHECK(static_retail.size() <= 64 && static_mod.size() <= 64);
    CHECK(static_retail != source_retail && static_mod != source_mod);
    CHECK(static_mod != yougame::compatibility_key(version, true, other, true));
  }
  // A literal engine-looking suffix in the version must not defeat engine isolation at the cap.
  const std::string tagged_retail_version = std::string(58, 'v') + "-st-extra";
  const std::string tagged_mod_version = std::string(37, 'v') + "-st-extra";
  CHECK(yougame::compatibility_key(tagged_retail_version, false, "") == "mu-" + std::string(58, 'v') + "-st");
  CHECK(yougame::compatibility_key(tagged_retail_version, false, "", true) == "mu-" + std::string(57, 'v') + "-st");
  CHECK(yougame::compatibility_key(tagged_mod_version, true, fp) == "mu-" + std::string(37, 'v') + "-st-mod-0123456789abcdef");
  CHECK(yougame::compatibility_key(tagged_mod_version, true, fp, true) == "mu-" + std::string(36, 'v') + "-st-mod-0123456789abcdef");
  std::puts("yougame_mod_online: build isolation, content gates and room keys ok");
}
