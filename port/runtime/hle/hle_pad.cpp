// PAD HLE: controller state from the host input layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstring>

static uint32_t s_spec = 5;

HLE(PADInit) { RET(1); }
HLE(PADReset) { RET(1); }
HLE(PADRecalibrate) { RET(1); }
HLE(PADControlMotor) {}
HLE(PADControlAllMotors) {}
HLE(PADSetSpec) { s_spec = ARG0; }
HLE(PADGetSpec) { RET(s_spec); }
HLE(PADGetType) { if (ARG1) host::wr32(ARG1, 0x08000000); RET(1); }
HLE(PADSync) { RET(1); }
HLE(PADSetAnalogMode) {}
HLE(PADSetSamplingRate) {}

// u32 PADRead(PADStatus* status[4]) -> bitmask of channels with fresh data
HLE(PADRead) {
  host::pump_completions();
  static int reported = 0;
  if (host::options.trace_calls && reported++ < 10) host::log("[pad] PADRead(%08X)", ARG0);
  host::PadState pads[4];
  host::input_poll(pads);
  uint32_t base = ARG0, mask = 0;
  for (int i = 0; i < 4; ++i) {
    uint32_t p = base + i * 12;
    host::wr16(p + 0, pads[i].button);
    host::wr8(p + 2, (uint8_t)pads[i].stick_x);
    host::wr8(p + 3, (uint8_t)pads[i].stick_y);
    host::wr8(p + 4, (uint8_t)pads[i].sub_x);
    host::wr8(p + 5, (uint8_t)pads[i].sub_y);
    host::wr8(p + 6, pads[i].trig_l);
    host::wr8(p + 7, pads[i].trig_r);
    host::wr8(p + 8, pads[i].analog_a);
    host::wr8(p + 9, pads[i].analog_b);
    host::wr8(p + 10, (uint8_t)pads[i].err);
    host::wr8(p + 11, 0);
    if (pads[i].err == 0) mask |= 0x80000000u >> i;
  }
  RET(mask);
}
