// The update check the launcher makes when it starts, when GitHub does not answer: on a fresh install the first
// ask can come before the network is up, time out, or meet GitHub busy or rate limiting. It is asked again quietly
// ("Checking for updates..." meanwhile) and only then reported, in plain words. Downloads and their checks are not
// touched. Pure, no Windows (port/tests/updater_check_retry_test.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace host::updater::check_retry {

// Milliseconds to wait before asking again after `failures` failed asks (1, 2, ...), or -1: stop and report it.
// quiet: the check the launcher makes by itself. A check the player asked for (Retry) reports its first failure.
inline int delay_ms(int failures, bool quiet) {
  if (!quiet) return -1;
  if (failures == 1) return 3000;
  if (failures == 2) return 15000;
  return -1;
}

// The status line once the check stops asking: short, no HTTP numbers. status: the last answer's HTTP status,
// 0 when nothing answered.
inline std::string failed_text(int status) {
  if (status == 0) return "Couldn't check for updates (no connection)";
  if (status == 403 || status == 429) return "Couldn't check for updates (GitHub is busy)";
  return "Couldn't check for updates";
}

}  // namespace host::updater::check_retry
