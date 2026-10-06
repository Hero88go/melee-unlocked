// Input delay changes the sampled controls, never the simulation clock.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "offline_input_delay.h"
#include "../gx/render_options.h"
#include "netplay_state.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"
#else
#include "slippi_online.h"
#include "slippi_playback.h"
#endif

namespace host::offline_delay {
void apply(PadState pads[4], bool gameplay) {
  static Queue queue;
#ifdef MELEE_NO_SLIPPI
  const bool offline = host::netplay::session_mode() < 0;
  const bool replay = false;
  const int delay = host::netplay::config().delay;
#else
  const bool offline = slippi::online::session_mode() < 0;
  const bool replay = slippi::playback::enabled();
  const int delay = slippi::online::config().delay;
#endif
  uint32_t major = 0, minor = 0, frame = 0;
  const bool active = gx::RenderOptions::live_offline_delay() && offline && !replay && gameplay;
  if (active) current_scene(&major, &minor, &frame);
  queue.apply(pads, active, delay, retrace_count(), (major << 16) | minor);
}
}  // namespace host::offline_delay
