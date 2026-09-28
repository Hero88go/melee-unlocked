#include <string.h>

/* Include the production implementation so this test can inspect its DSP PB. */
#include "../../extern/melee/libs/dolphin/src/dolphin/ax/AXVPB.c"

/* Other exported routines in AXVPB.c are outside this test's call graph. */
BOOL OSDisableInterrupts(void) { return FALSE; }
BOOL OSRestoreInterrupts(BOOL level) { return level; }

static int addr_equal(const AXPBADDR* a, const AXPBADDR* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

int main(void)
{
    AXVPB voice;
    AXPBADDR full = {
        .loopFlag = 1,
        .format = 2,
        .loopAddressHi = 0x1234,
        .loopAddressLo = 0x5678,
        .endAddressHi = 0x2345,
        .endAddressLo = 0x6789,
        .currentAddressHi = 0x3456,
        .currentAddressLo = 0x789A,
    };
    AXPBADDR partial = {
        .loopFlag = 0,
        .format = 7,
        .loopAddressHi = 0xABCD,
        .loopAddressLo = 0xEF01,
        .endAddressHi = 0xBCDE,
        .endAddressLo = 0xF012,
        .currentAddressHi = 0xCDEF,
        .currentAddressLo = 0x0123,
    };

    memset(&voice, 0, sizeof(voice));
    memset(&__AXPB[0], 0, sizeof(__AXPB[0]));
    voice.index = 0;
    voice.pb.addr = full;
    voice.sync = AX_SYNC_FLAG_COPYADDR | AX_SYNC_FLAG_COPYLOOPADDR |
                 AX_SYNC_FLAG_COPYENDADDR | AX_SYNC_FLAG_COPYCURADDR |
                 AX_SYNC_FLAG_COPYLOOP | AX_SYNC_FLAG_COPYADPCM;
    __AXServiceVPB(&voice);
    if (!addr_equal(&__AXPB[0].addr, &full)) {
        return 1;
    }

    memset(&voice, 0, sizeof(voice));
    memset(&__AXPB[0], 0, sizeof(__AXPB[0]));
    voice.index = 0;
    voice.pb.addr = partial;
    __AXPB[0].addr.format = 3;
    __AXPB[0].addr.endAddressHi = 0x1111;
    __AXPB[0].addr.endAddressLo = 0x2222;
    __AXPB[0].addr.currentAddressHi = 0x3333;
    __AXPB[0].addr.currentAddressLo = 0x4444;
    voice.sync = AX_SYNC_FLAG_COPYLOOP | AX_SYNC_FLAG_COPYLOOPADDR;
    __AXServiceVPB(&voice);
    if (__AXPB[0].addr.loopFlag != partial.loopFlag ||
        __AXPB[0].addr.loopAddressHi != partial.loopAddressHi ||
        __AXPB[0].addr.loopAddressLo != partial.loopAddressLo ||
        __AXPB[0].addr.format != 3 ||
        __AXPB[0].addr.endAddressHi != 0x1111 ||
        __AXPB[0].addr.endAddressLo != 0x2222 ||
        __AXPB[0].addr.currentAddressHi != 0x3333 ||
        __AXPB[0].addr.currentAddressLo != 0x4444) {
        return 2;
    }
    return 0;
}
