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

  if (g_failures == 0) std::printf("launcher engine: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
