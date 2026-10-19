// Direct native Linux USB input. No companion process or compatibility runtime.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_host.h"
namespace linux_gc {
void initialize();
unsigned poll(MuPadStatus out[4]);
void rumble(int port, bool on);
double poll_rate();
void shutdown();
}
