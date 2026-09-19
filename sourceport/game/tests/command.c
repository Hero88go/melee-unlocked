#include <melee/lb/lbcommand.h>
#include <melee/lb/types.h>

/* This test never emits a background command. */
void lbBgFlash_80021C48(u32 a, u32 b) { (void) a; (void) b; }

int main(void)
{
    /* Repeat a five-frame timer three times, then terminate. */
    _Alignas(CmdUnion) unsigned char bytes[] = {
        0x0C, 0x00, 0x00, 0x03,
        0x04, 0x00, 0x00, 0x05,
        0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    CommandInfo info = {0};
    int steps = 0;
    info.u = (CmdUnion*) bytes;
    info.frame_count = 123.0f;
    while (info.u && steps++ < 12) {
        if (!Command_Execute(&info, info.u->Command_00.code)) return 1;
    }
    if (info.u || steps != 8 || info.timer != 15.0f ||
        info.loop_count != 0 || info.frame_count != 123.0f) return 2;
    return 0;
}
