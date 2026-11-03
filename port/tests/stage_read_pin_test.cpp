// Host-only regression checks for partial stage reads across online-state changes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "stage_read_pin.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

using Form = std::shared_ptr<const std::vector<unsigned char>>;
using host::cosmetics::stage_read_pin::PinState;
using host::cosmetics::stage_read_pin::pick;

static Form form(std::size_t size, unsigned char byte) {
  return std::make_shared<const std::vector<unsigned char>>(size, byte);
}

int main() {
  const auto offline = form(1024, 1);
  const auto next_offline = form(1024, 2);
  const auto online = form(1024, 3);  // Online padding can make the lengths equal.
  const auto next_online = form(1024, 6);
  const auto longer = form(2048, 4);
  PinState<Form> pin;

  CHECK(pick(pin, offline, false, 0, offline->size()) == offline);
  CHECK(pin.form == offline && !pin.online);
  CHECK(pick(pin, next_offline, false, 512, next_offline->size()) == offline);
  CHECK(pick(pin, longer, false, 512, longer->size()) == longer);
  CHECK(pick(pin, next_offline, false, 768, next_offline->size()) == offline);

  // A mid-file switch must discard the offline form even with identical lengths.
  CHECK(pick(pin, online, true, 512, online->size()) == online);
  CHECK(pin.form == online && pin.online);
  CHECK(pick(pin, next_online, true, 768, next_online->size()) == online);
  CHECK(pick(pin, next_offline, false, 768, next_offline->size()) == next_offline);
  CHECK(pin.form == next_offline && !pin.online);
  CHECK(pick(pin, offline, false, 896, offline->size()) == next_offline);

  CHECK(pick(pin, online, true, 0, online->size()) == online);
  CHECK(pick(pin, offline, false, 0, offline->size()) == offline);
  CHECK(pick(pin, next_offline, false, 0, next_offline->size()) == next_offline);

  // A first piece may start mid-file; a switch with a new length must pin too.
  PinState<Form> mid_file;
  CHECK(pick(mid_file, offline, false, 512, offline->size()) == offline);
  CHECK(pick(mid_file, longer, true, 512, longer->size()) == longer);
  CHECK(pick(mid_file, online, true, 768, online->size()) == online);
  CHECK(pick(mid_file, longer, true, 1024, longer->size()) == longer);

  // The runtime pin owns its bytes across runtime-state replacement.
  PinState<Form> owned;
  auto temporary = form(1024, 5);
  std::weak_ptr<const std::vector<unsigned char>> lifetime = temporary;
  CHECK(pick(owned, temporary, false, 0, temporary->size()) == temporary);
  temporary.reset();
  CHECK(!lifetime.expired());
  CHECK(pick(owned, online, true, 512, online->size()) == online);
  CHECK(lifetime.expired());
  std::puts("stage_read_pin: partial reads, state switches and lifetime ok");
}
