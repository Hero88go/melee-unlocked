#pragma once

namespace host {

// Latency trace (MELEE_TRACE_LATENCY=1): when a simulation tick was due and when it woke, when the
// game first read its pads in it, and how old the newest adapter report was at that read (-1 when
// no adapter report was read). Seconds on the now_seconds clock, except pad_age in milliseconds.
// Written by the simulation thread and copied into the frame it produces.
struct TickTiming { double target = 0, wake = 0, pad = 0, pad_age = -1; };

}  // namespace host
