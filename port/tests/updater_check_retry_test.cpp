// The launcher's update check when GitHub does not answer: asked again quietly, then reported in plain words
// (updater_check_retry.h). No network.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater_check_retry.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace retry = host::updater::check_retry;

int main() {
  // The launcher's own check: two quiet asks again, a short wait then a longer one, then it reports.
  CHECK(retry::delay_ms(1, true) == 3000);
  CHECK(retry::delay_ms(2, true) == 15000);
  CHECK(retry::delay_ms(3, true) < 0);
  CHECK(retry::delay_ms(10, true) < 0);
  // A check the player asked for (Retry): its first failure is reported at once.
  for (int failures = 1; failures <= 3; ++failures) CHECK(retry::delay_ms(failures, false) < 0);
  // The words: short, plain, no HTTP numbers, never the old "Update check failed".
  for (int status : {0, 200, 403, 404, 429, 500, 502}) {
    const std::string text = retry::failed_text(status);
    CHECK(text.rfind("Couldn't check for updates", 0) == 0);
    CHECK(text.find("failed") == std::string::npos);
    CHECK(text.find(std::to_string(status)) == std::string::npos || status == 0);
    CHECK(text.size() <= 48);
  }
  CHECK(retry::failed_text(0).find("no connection") != std::string::npos);
  CHECK(retry::failed_text(403).find("busy") != std::string::npos);
  CHECK(retry::failed_text(429) == retry::failed_text(403));
  CHECK(retry::failed_text(500) == "Couldn't check for updates");
  std::printf("updater_check_retry: all checks passed\n");
  return 0;
}
