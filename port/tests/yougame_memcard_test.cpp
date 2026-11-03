// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_memcard.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  using namespace yougame::memcard;
  for (unsigned active = 0; active != 2; ++active) {
    for (unsigned channel = 0; channel != 3; ++channel) {
      for (unsigned decision = 0; decision != 24; ++decision) {
        CHECK(confirm(active, channel, decision) ==
              (active && channel == 0 && (decision == 5 || decision == 7)));
      }
      for (unsigned msg = 0; msg != 25; ++msg) {
        CHECK(hide_message(active, channel, msg) ==
              (active && channel == 0 && (msg == 10 || msg == 12)));
      }
    }
  }
  CHECK(confirm(true, 0, 5));   // Missing save: Yes.
  CHECK(confirm(true, 0, 7));   // Creation succeeded: acknowledge.
  CHECK(!confirm(true, 0, 6));  // Let the game create and write its own data.
  CHECK(!confirm(true, 0, 8));  // Creation failed: preserve the error screen.
  CHECK(!confirm(true, 0, 3));  // Never consent to formatting.
  CHECK(!confirm(true, 0, 18)); // Never continue without saving on the user's behalf.
  CHECK(!hide_message(true, 0, 13)); // Keep creation errors visible.
  CHECK(!hide_message(true, 0, 11)); // Keep the write-in-progress notice.
  CHECK(!confirm(true, 0, ~0u));
  CHECK(!hide_message(true, 0, ~0u));
  std::puts("YouGame memory-card boot policy passed");
}
