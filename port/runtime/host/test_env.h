// Diagnostic environment overrides never affect YouGame or vanilla-only sessions.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gameplay_policy.h"
#include <cstdlib>
namespace host::test_env {
// Latched from yougame::active() by start(), before diagnostic readers or workers.
inline bool yougame_session = false;
inline const char* getenv(const char* name) {
  return yougame_session || gameplay::vanilla_only() ? nullptr : std::getenv(name);
}
} // namespace host::test_env
