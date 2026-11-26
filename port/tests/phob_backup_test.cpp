// PhobGCC settings backups (phob_store.cpp): a start keeps a copy, a reset or setup load can be undone,
// and only the newest 20 copies per port stay.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host.h"
#include "phob.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace host {
// The adapter side is not part of this test.
void gcadapter_phob_set(int, const phob::Settings&) {}
std::string profile_clean_name(const std::string& name) { return name; }   // setups are not tested here
}

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
}

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "mu_phob_backup_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "PhobGCC", ec);
  // The player's own setup on port 1: calibrated, snapback 7.
  host::phob::Settings mine;
  mine.enabled = true;
  mine.x_snapback = 7;
  mine.a_cal.measured = true;
  mine.a_cal.points_x[3] = 0.25f;
  { std::ofstream(root / "PhobGCC" / "port1.ini") << host::phob::serialize(mine); }

  host::phob_store_load((root / "port-settings.ini").u8string());   // the start keeps a copy
  int copies = 0;
  for (const auto& e : fs::directory_iterator(root / "PhobGCC" / "Backups", ec)) copies += e.path().filename().u8string().rfind("port1-", 0) == 0;
  CHECK(copies == 1);

  // A hard reset wipes it (the menu saves the reset settings right after).
  host::phob_backup(0);
  host::phob::Settings wiped = mine;
  host::phob::hard_reset(wiped);
  CHECK(host::phob_store_save(0, wiped));

  // Restore brings back the player's setup, calibration included.
  host::phob::Settings back = wiped;
  CHECK(host::phob_backup_restore(0, back));
  CHECK(back.x_snapback == 7 && back.a_cal.measured && back.a_cal.points_x[3] == 0.25f);

  // Nothing different to restore: false, settings untouched.
  host::phob::Settings same = mine;
  CHECK(host::phob_store_save(0, same));
  fs::remove_all(root / "PhobGCC" / "Backups", ec);
  host::phob_backup(0);
  CHECK(!host::phob_backup_restore(0, same));
  CHECK(same.x_snapback == 7);

  // At most 20 copies per port.
  for (int i = 0; i < 30; ++i) {
    host::phob::Settings v = mine;
    v.y_snapback = (i % 20) - 10;
    v.tourney = i % 6;
    v.a_scaler = 90 + i % 21;
    CHECK(host::phob_store_save(0, v));
    host::phob_backup(0);
  }
  copies = 0;
  for (const auto& e : fs::directory_iterator(root / "PhobGCC" / "Backups", ec)) copies += e.path().filename().u8string().rfind("port1-", 0) == 0;
  CHECK(copies <= 20 && copies >= 1);

  fs::remove_all(root, ec);
  if (g_failures) std::printf("%d failure(s)\n", g_failures);
  else std::printf("phob backups: all checks passed\n");
  return g_failures ? 1 : 0;
}
