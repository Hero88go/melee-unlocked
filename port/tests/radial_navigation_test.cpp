#include "radial_navigation.h"

#include <cstdlib>

static void expect(int got, int want) {
  if (got != want) std::abort();
}

int main() {
  using gx::radial_navigation::update;

  // D-pad traversal wraps through all seven menu categories and keeps the
  // dedicated vertical shortcuts predictable.
  expect(update(0, false, false, false, true, 0, 0), 6);
  expect(update(6, false, true, false, false, 0, 0), 0);
  expect(update(4, true, false, false, false, 0, 0), 0);
  expect(update(0, false, false, true, false, 0, 0), 3);
  expect(update(2, true, true, false, false, 0, 0), 3);
  expect(update(4, false, false, false, false, 0, 0), 4);

  // Stick cardinals land on the matching wheel directions. The straight-down
  // tie resolves to Controls, the same category as D-pad Down.
  expect(update(4, false, false, false, false, 0, 100), 0);
  expect(update(0, false, false, false, false, 100, 0), 2);
  expect(update(0, false, false, false, false, 0, -100), 3);
  expect(update(0, false, false, false, false, -100, 0), 5);

  // Input inside the deadzone leaves the selection alone. The boundary band
  // retains its current wedge to prevent small analog noise from chattering.
  expect(update(5, false, false, false, false, 58, 58), 5);
  expect(update(1, false, false, false, false, 100, 22), 1);
  expect(update(2, false, false, false, false, 100, 22), 2);
}
