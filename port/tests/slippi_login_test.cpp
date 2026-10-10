// Regression: launcher-visible accounts must also enable the game's online menu.
// Uses production User and EXI/native menu commands with synthetic accounts; no servers.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_account.h"
#include "slippi_net.h"
#include "slippi_online.h"
#include "native_slippi_bridge.h"
#include <cstdio>
#include <fstream>
#include <process.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace fs = std::filesystem;
namespace {
void write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream output(file, std::ios::binary);
  output << text;
  if (!output) throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path& file) {
  std::ifstream input(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string profile(const char* name, const char* code) {
  return nlohmann::json{{"uid", "synthetic-test-user"}, {"playKey", "synthetic-test-key"},
                        {"displayName", name}, {"connectCode", code}}.dump();
}
struct Environment {
  std::string name, previous;
  Environment(const char* key, const fs::path& path) : name(key) {
    char* value = nullptr; size_t size = 0;
    _dupenv_s(&value, &size, key);
    if (value) previous = value;
    free(value);
    _putenv_s(key, path.string().c_str());
  }
  ~Environment() { _putenv_s(name.c_str(), previous.c_str()); }
};
struct OnlineShutdown { ~OnlineShutdown() { slippi::online::shutdown(); } };
}

// A Dolphin build the Slippi Launcher's settings file points at, outside the launcher's folder,
// and the three reasons an account can be missing.
int moved_build_and_reasons(const fs::path& root) {
  const fs::path local = root / "MU" / "User" / "Slippi";
  const fs::path launcher = root / "Roaming" / "Slippi Launcher";
  const std::vector<fs::path> roots{launcher};
  CHECK(slippi::account::why_missing(local, roots) == slippi::account::Missing::Launcher);
  fs::create_directories(launcher);
  CHECK(slippi::account::why_missing(local, roots) == slippi::account::Missing::SignIn);
  const fs::path elsewhere = root / "OtherDrive" / "Dolphin";
  write(launcher / "Settings", nlohmann::json{{"settings", {{"netplayDolphinPath", elsewhere.string()}}}}.dump());
  CHECK(!slippi::account::resolve(local, roots));
  write(elsewhere / "User" / "Slippi" / "user.json", nlohmann::json{{"uid", "synthetic-test-user"}}.dump());
  CHECK(!slippi::account::resolve(local, roots));
  CHECK(slippi::account::why_missing(local, roots) == slippi::account::Missing::ConnectCode);
  write(elsewhere / "User" / "Slippi" / "user.json", profile("Moved test", "MOVE#1"));
  const auto found = slippi::account::resolve(local, roots);
  CHECK(found && slippi::account::text(found.data, "connectCode") == "MOVE#1");
  fs::remove_all(root);
  return 0;
}

int main() {
  const fs::path root = fs::current_path() / ("slippi-login-fixture-" + std::to_string(_getpid()));
  CHECK(!fs::exists(root));
  if (const int failed = moved_build_and_reasons(root / "moved")) return failed;
  CHECK(!fs::exists(root / "moved"));
  fs::remove_all(root);
  const fs::path local = root / "MU" / "User" / "Slippi";
  const fs::path roaming = root / "Roaming" / "Slippi Launcher";
  const fs::path localapp = root / "Local" / "Slippi Launcher";
  Environment appdata("APPDATA", root / "Roaming");
  Environment localappdata("LOCALAPPDATA", root / "Local");
  slippi::Matchmaking::local_peer.enabled = true;

  // Start before signing in, then create a relocated LOCALAPPDATA profile while the game is open.
  slippi::online::config().user_dir = local.string();
  std::vector<uint8_t> reply;
  CHECK(slippi::online::handle(slippi::online::CMD_GET_ONLINE_STATUS, nullptr, 0, reply));
  OnlineShutdown shutdown;
  CHECK(!reply.empty() && reply[0] == 0);
  const fs::path moved = localapp / "custom-build" / "netplay" / "User" / "Slippi" / "user.json";
  const auto shared_data = profile("Shared test", "TEST#123");
  write(moved, shared_data);
  const auto launcher_profile = slippi::account::resolve(local);
  CHECK(launcher_profile && launcher_profile.file == moved);
  CHECK(slippi::online::handle(slippi::online::CMD_OPEN_LOGIN, nullptr, 0, reply));
  CHECK(slippi::online::handle(slippi::online::CMD_GET_ONLINE_STATUS, nullptr, 0, reply));
  CHECK(reply[0] == 1);  // The same logged-in reply used to unlock Unranked.
  CHECK(slippi::online::config().user_dir == local.string());

  // The Source Port native bridge must receive the same state as Static Recomp's EXI call.
  uint8_t native_reply[128] = {};
  uint32_t native_size = 0;
  CHECK(slippi::online::native_command(slippi::online::CMD_GET_ONLINE_STATUS, nullptr, 0,
    native_reply, sizeof native_reply, &native_size, 1, true,
    [](uint8_t cmd, const uint8_t* data, uint32_t size, std::vector<uint8_t>& out) {
      return slippi::online::handle(cmd, data, size, out);
    }) == slippi::online::NATIVE_COMMAND_OK);
  CHECK(native_size == reply.size() && std::equal(reply.begin(), reply.end(), native_reply));

  // Mutable history stays local; authentication and Slippi's own history are never copied/written.
  const auto shared_history = moved.parent_path() / "direct-codes.json";
  write(shared_history, "[\"UNCHANGED#1\"]");
  slippi::DirectCodes codes((local / "direct-codes.json").string());
  codes.AddOrUpdateCode("LOCAL#2");
  CHECK(fs::exists(local / "direct-codes.json"));
  CHECK(read(shared_history) == "[\"UNCHANGED#1\"]");
  CHECK(read(moved) == shared_data && !fs::exists(local / "user.json"));

  // A stale partial local record or malformed preferred launcher profile cannot mask a valid login.
  write(local / "user.json", "{\"displayName\":\"Stale\",\"connectCode\":\"OLD#1\"}");
  write(roaming / "netplay" / "User" / "Slippi" / "user.json", "{broken");
  CHECK(slippi::account::resolve(local).file == moved);
  slippi::User user(local.string());
  CHECK(user.IsLoggedIn() && user.GetUserInfo().connect_code == "TEST#123");
  CHECK(user.dir() == local.string());
  CHECK(user.GetUserChatMessages().size() == 16);

  // A complete portable login takes priority. Logout/re-login and refresh read its current contents.
  const auto portable = profile("Portable test", "LOCAL#123");
  write(local / "user.json", portable);
  CHECK(user.AttemptLogin(true) && user.GetUserInfo().connect_code == "LOCAL#123");
  CHECK(slippi::account::resolve(local).file == local / "user.json");
  user.LogOut();
  CHECK(!user.IsLoggedIn() && user.GetUserInfo().play_key.empty());
  CHECK(user.AttemptLogin() && user.GetUserInfo().connect_code == "LOCAL#123");
  write(local / "user.json", profile("Changed", "LOCAL#456"));
  CHECK(user.AttemptLogin() && user.GetUserInfo().connect_code == "LOCAL#456");

  // Removed/invalid credentials clear the entire cached record and never throw on field types.
  for (const auto& invalid : {"{}", "null", "{broken", "{\"uid\":123,\"playKey\":true,\"connectCode\":[]}"}) {
    write(local / "user.json", invalid);
    CHECK(!user.AttemptLogin());
    CHECK(!user.IsLoggedIn() && user.GetUserInfo().uid.empty() && user.GetUserInfo().play_key.empty());
  }
  CHECK(user.AttemptLogin(true) && user.GetUserInfo().connect_code == "TEST#123");

  // Standard APPDATA netplay and playback fallbacks retain deterministic preference.
  const auto standard = roaming / "netplay" / "User" / "Slippi" / "user.json";
  const auto playback = roaming / "playback" / "User" / "Slippi" / "user.json";
  write(standard, profile("Standard", "STD#123"));
  write(playback, profile("Playback", "PLAY#123"));
  CHECK(slippi::account::resolve(local).file == standard);
  write(standard, "{}");
  CHECK(slippi::account::resolve(local).file == playback);
  CHECK(read(moved) == shared_data && read(shared_history) == "[\"UNCHANGED#1\"]");
  std::puts("Slippi login regression passed: relocated profiles, live Log In, EXI/native status, local history, credential validation");
  return 0;
}
