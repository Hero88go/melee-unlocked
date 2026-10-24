// The launcher's engine choice (launcher_engine.h): Static Recomp by default, the Source Port only
// when chosen and both of its files are present.
#include "launcher_engine.h"
#include <cstdio>
#include <set>
#include <string>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

launcher::EngineInputs release(const std::set<std::string>& files) {
  launcher::EngineInputs in;
  in.dir = "C:\\Games\\MeleeUnlocked";
  in.exists = [files](const std::string& p) { return files.count(p) != 0; };
  return in;
}
}  // namespace

int main() {
  const std::string d = "C:\\Games\\MeleeUnlocked\\";
  const std::set<std::string> full = {d + "melee_port.exe", d + "melee_port_compat.exe",
                                      d + "melee_source.exe", d + "melee_game.dll"};

  // A fresh install has no launcher.ini: nothing parsed, so the engine stays Legacy.
  int engine = launcher::ENGINE_LEGACY;
  engine = launcher::parse_engine_line("iso=C:\\melee.iso", engine);
  CHECK(engine == launcher::ENGINE_LEGACY);
  // Choosing Source is read from the ini, and is offered when both of its files are there.
  engine = launcher::parse_engine_line("engine=1", engine);
  CHECK(engine == launcher::ENGINE_SOURCE);
  CHECK(launcher::source_exe_dir(release(full)) == "C:\\Games\\MeleeUnlocked");

  // Out-of-range values keep the current choice.
  CHECK(launcher::parse_engine_line("engine=7", launcher::ENGINE_LEGACY) == launcher::ENGINE_LEGACY);

  // The DLL is gone (an update dropped it): not offered, so the launcher falls back to Static Recomp.
  CHECK(launcher::source_exe_dir(release({d + "melee_port.exe", d + "melee_source.exe"})).empty());

  // A source checkout offers its build-sourceport output.
  auto checkout = release({"C:\\repo\\build-sourceport\\port\\Release\\melee_source.exe",
                           "C:\\repo\\build-sourceport\\port\\Release\\melee_game.dll"});
  checkout.repo_root = "C:\\repo";
  CHECK(launcher::source_exe_dir(checkout) == "C:\\repo\\build-sourceport\\port\\Release");

  // Vanilla: launcher.ini vanilla=1 and vanillaowncode=1 are read on their own, unknown lines are
  // not; the launch flags are the retail boot plus the session lockout, and the game's own code only
  // when asked. Nothing is added while Vanilla is off, whatever the own-code choice.
  launcher::VanillaChoice choice;
  CHECK(!launcher::parse_vanilla_line("engine=1", choice));
  CHECK(!launcher::parse_vanilla_line("vanilla=0", choice));
  CHECK(!choice.vanilla && !choice.own_code);
  CHECK(launcher::vanilla_launch_args(choice).empty());
  CHECK(launcher::parse_vanilla_line("vanillaowncode=1", choice));
  CHECK(choice.own_code && !choice.vanilla);
  CHECK(launcher::vanilla_launch_args(choice).empty());
  CHECK(launcher::parse_vanilla_line("vanilla=1", choice));
  CHECK(launcher::vanilla_launch_args(choice) == " --slippi-menus off --vanilla-only --vanilla-game");
  choice.own_code = false;
  CHECK(launcher::vanilla_launch_args(choice) == " --slippi-menus off --vanilla-only");

  if (g_failures == 0) std::printf("launcher engine: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
