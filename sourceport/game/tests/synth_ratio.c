/* Exercise the exact native source-rate conversion used by the production
 * synthesis callbacks. */
#include "sysdolphin/baselib/synth.c"

extern int printf(const char*, ...);
void mu_native_free(void* ptr) { (void) ptr; }
void OSReport(char* fmt, ...) { (void) fmt; }

int main(void)
{
    HSD_SynthSetPStreamRatio(0x00010000);
    if (HSD_Synth_80407FD8.ratioHi != 1 ||
        HSD_Synth_80407FD8.ratioLo != 0) {
        printf("FAIL: unity rate must encode as ratio 0001:0000, got %04x:%04x\n",
               HSD_Synth_80407FD8.ratioHi, HSD_Synth_80407FD8.ratioLo);
        return 1;
    }

    HSD_SynthSetPStreamRatio(0x23456789);
    if (HSD_Synth_80407FD8.ratioHi != 0x2345 ||
        HSD_Synth_80407FD8.ratioLo != 0x6789) {
        printf("FAIL: fixed-point rate halves were reversed\n");
        return 2;
    }

    HSD_SynthSetPStreamRatio(0);
    if (HSD_Synth_80407FD8.ratioHi != 0 ||
        HSD_Synth_80407FD8.ratioLo != 0) {
        printf("FAIL: zero rate must clear both fields\n");
        return 3;
    }
    return 0;
}
