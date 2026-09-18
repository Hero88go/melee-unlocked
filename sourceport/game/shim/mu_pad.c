/* Controllers. The host's input layer (every device kind, rebinding, auto L-cancel) produces the four
 * pads; this copies them into the game's structures. */
#include <dolphin/pad.h>

#include "mu_shim.h"

BOOL PADInit(void) { return 1; }
int PADReset(unsigned long mask) { (void) mask; return 1; }
BOOL PADRecalibrate(u32 mask) { (void) mask; return 1; }
void PADSetSamplingRate(unsigned long msec) { (void) msec; }
void PADSetSpec(u32 spec) { (void) spec; }

/* PADControlMotor(chan, command): 0 stop, 1 rumble, 2 stop hard. */
void PADControlMotor(s32 chan, u32 command) { mu_host->pad_rumble(chan, command == 1); }

u32 PADRead(PADStatus* status)
{
    MuPadStatus pads[4];
    u32 mask = 0;
    int i;
    mu_host->pad_read(pads);
    for (i = 0; i < 4; i++) {
        status[i].button = pads[i].button;
        status[i].stickX = pads[i].stick_x;
        status[i].stickY = pads[i].stick_y;
        status[i].substickX = pads[i].sub_x;
        status[i].substickY = pads[i].sub_y;
        status[i].triggerLeft = pads[i].trigger_l;
        status[i].triggerRight = pads[i].trigger_r;
        status[i].analogA = pads[i].analog_a;
        status[i].analogB = pads[i].analog_b;
        status[i].err = pads[i].err;
        if (pads[i].err == 0)
            mask |= 0x80000000u >> i;
    }
    return mask;
}

/* PADClamp is the SDK's own (pad/PadClamp.c), compiled with the game. */
