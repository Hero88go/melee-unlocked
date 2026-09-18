// PAD HLE: controller state from the host input layer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <cstdio>
#include <cstring>
#include "lcancel.h"
#include "slippi_online.h"

static uint32_t s_spec = 5;

HLE(PADInit) { RET(1); }
HLE(PADReset) { RET(1); }
// The game asks for the controller's neutral to be re-read. We used to accept and do nothing, so a
// stick deflected when the adapter first reported kept a wrong neutral for the whole session.
HLE(PADRecalibrate) {
  const int port = ARG0 == 0xFFFFFFFFu ? -1 : (int)ARG0;
  host::gcadapter_recalibrate(port);
  host::switchpro_recalibrate(port);   // same story: its neutral also comes from the first report
  RET(1);
}
// PADControlMotor(chan, command): 0 stop, 1 rumble, 2 stop hard.
// Online, the game's ports are the match's slots, not the sockets on the adapter: the local
// player's controller may be in socket 2 while the match has them in slot 1. Only the local slot's
// motor command means anything here, and it goes to whatever controller is feeding the local
// input. Everything else is the opponent's rumble, which is theirs to feel, not ours.
static void rumble(int game_port, bool on) {
  if (slippi::online::is_online_match()) {
    if (game_port == slippi::online::local_player_slot()) host::input_rumble_local(on);
    return;
  }
  host::input_rumble(game_port, on);
}
HLE(PADControlMotor) { rumble((int)ARG0, ARG1 == 1); }
HLE(PADControlAllMotors) { for (int i = 0; i < 4; ++i) rumble(i, host::rd32(ARG0 + 4 * i) == 1); }
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
  // Automatic L-cancel, if the player turned it on: it presses the analog trigger here, one step
  // upstream of everything the game does with the pad, so the press is sampled, recorded into the
  // replay and sent to the opponent exactly like a press the player made.
  lcancel::apply(pads);
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
  if (!host::options.input_log.empty()) {
    static FILE* input_log = std::fopen(host::options.input_log.c_str(), "w");
    if (input_log) {
      static bool header = (std::fputs("retrace,port,buttons,stick_x,stick_y,cstick_x,cstick_y,trigger_l,trigger_r\n", input_log), true);
      (void)header;
      for (int i = 0; i < 4; ++i)
        if (pads[i].err == 0)
          std::fprintf(input_log, "%u,%d,%04X,%d,%d,%d,%d,%u,%u\n", host::retrace_count(), i + 1, pads[i].button, pads[i].stick_x, pads[i].stick_y,
                       pads[i].sub_x, pads[i].sub_y, pads[i].trig_l, pads[i].trig_r);
      std::fflush(input_log);
    }
  }
  RET(mask);
}
