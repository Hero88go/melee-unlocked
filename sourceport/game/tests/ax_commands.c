/* Run disc command bytes through the production interpreter and observe the
 * exact sample/volume sent to synthesis. No disc, renderer or audio device. */
#include "sysdolphin/baselib/axdriver.c"

extern int printf(const char*, ...);
extern void abort(void);
static int calls, sample, volume, priority_seen;
float mu_sqrtf(float value) { return __builtin_sqrtf(value); }

void HSD_Panic(const char* file, u32 line, const char* message) { abort(); }
void __assert(const char* file, u32 line, const char* message) { abort(); }
void OSReport(char* fmt, ...) {}
void HSD_SynthSFXKeyOff(int id) {}
void HSD_SynthSFXSetPriority(int id, int priority) {}
void HSD_SynthSFXSetVolumeFade(int id, u8 vol, int flag) {}
void HSD_SynthSFXSetPan(int id, u8 vol) {}
void HSD_SynthSFXSetPitchRatio(int id, int flag, float ratio) {}
void HSD_SynthSFXSetMix(int id, float main, float a, float b) {}
bool HSD_SynthSFXPlayWithGroup(int id, u8 vol, u8 vol2, u8 pan,
                             int priority, int itd, int group,
                             float p1, float p2, float main, float a, float b)
{
    ++calls;
    sample = id;
    volume = vol;
    priority_seen = priority;
    return 1;
}

int main(void)
{
    _Alignas(u32) unsigned char bytes[] = {
        0x04, 0x00, 0x00, 0x0F, /* priority 15 */
        0x06, 0x00, 0x00, 0x99, /* volume 153, no wait */
        0x01, 0x00, 0x03, 0x9A, /* play sample 922 */
        0x00, 0x00, 0x00, 0x01, /* execute, wait 1 */
        0x0E, 0x00, 0x00, 0x00, /* sleep */
    };
    HSD_SM voice = {0};
    voice.flags = SMSTATE_ACTIVE;
    voice.volume = 240;
    voice.pan = 128;
    voice.cmd_stream = (u32*) bytes;
    AXDriver_804D778C = 0;
    AXDriverInterp(&voice);
    if (calls != 1 || sample != 922 || volume != 153 || priority_seen != 15 ||
        voice.x30 != 1 || voice.cmd_stream != (u32*) bytes + 4) {
        printf("FAIL: calls=%d sample=%d volume=%d priority=%d wait=%d\n",
               calls, sample, volume, priority_seen, voice.x30);
        return 1;
    }
    AXDriver_804D778C = 1;
    AXDriverInterp(&voice);
    if ((voice.flags & SMSTATE_MASK) != SMSTATE_SLEEP || calls != 1) return 2;

    /* Signed byte/halfword operands and a backward loop with timed waits.
     * The loop body runs three times: the initial pass plus two repeats. */
    _Alignas(u32) unsigned char loop_bytes[] = {
        0x02, 0x00, 0x00, 0x02, /* loop count 2 */
        0x07, 0x00, 0x01, 0xFF, /* wait 1, volume -= 1 */
        0x03, 0x00, 0x00, 0x02, /* branch back 2 words, then advance */
        0x05, 0x00, 0x00, 0xFE, /* priority -= 2 */
        0x09, 0x00, 0x00, 0xF6, /* pan -= 10 */
        0x0B, 0x00, 0x00, 0xFD, /* field x1E -= 3 */
        0x0C, 0x00, 0xFB, 0x50, /* pitch = -1200 */
        0x0D, 0x00, 0xFD, 0xA8, /* pitch -= 600 */
        0x11, 0x00, 0x00, 0xFE, /* aux A -= 2 */
        0x13, 0x00, 0x00, 0xFD, /* aux B -= 3 */
        0x00, 0x00, 0x00, 0x01, /* execute, wait 1 */
        0x0E, 0x00, 0x00, 0x00, /* sleep */
    };
    voice = (HSD_SM) {0};
    voice.flags = SMSTATE_ACTIVE;
    voice.vID = 1;
    voice.x1A = 100;
    voice.pri = 15;
    voice.x1C = 128;
    voice.x1E = 20;
    voice.x24[0] = 10;
    voice.x24[1] = 20;
    voice.cmd_stream = (u32*) loop_bytes;
    AXDriver_804D603C = 0; /* allow both auxiliary mix commands */
    for (int tick = 0; tick < 4; ++tick) {
        AXDriver_804D778C = tick;
        AXDriverInterp(&voice);
        if (voice.x30 != tick + 1) return 3;
    }
    if (voice.x2A != 0 || voice.x1A != 97 || voice.pri != 13 ||
        voice.x1C != 118 || voice.x1E != 17 || voice.x20 != -1800 ||
        voice.x24[0] != 8 || voice.x24[1] != 17 || calls != 1 ||
        voice.cmd_stream != (u32*) loop_bytes + 11) return 4;
    AXDriver_804D778C = 4;
    AXDriverInterp(&voice);
    if ((voice.flags & SMSTATE_MASK) != SMSTATE_SLEEP) return 5;
    return 0;
}
