/* Console-style sessions: the game side of the pad scheduler; shim/mu_pad.c has the host side. Apart from
 * mu_pad.c because these need the game's controller and VI headers, and the game's C library collides with
 * the host's stdint (ssize_t) in a file that also includes mu_shim.h. */
#include <string.h>
#include <dolphin/vi.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>

int mu_console_active(void);
int mu_console_host_frame_begin(void);
int mu_console_rollback_active(void);
int mu_console_rumble_owned(void);
int mu_online_pad_renew(PADStatus*);

void mu_console_replace_raw(void)
{
    PadLibData* p = &HSD_PadLibData;
    if (mu_console_rollback_active()) {
        HSD_PadData now = {0};
        PADStatus pads[6] = {0};   /* the online renew fills six ports (Triples); a console session uses four */
        /* History injection is deliberately independent of SDK renewal: no rumble, polling,
         * reset/recalibration or other hardware side effects can precede the checkpoint. */
        if (mu_online_pad_renew(pads)) return;
        memcpy(now.stat, pads, sizeof now.stat);
        p->qread = p->qwrite;
        p->queue[p->qwrite] = now;
        p->qwrite = (p->qwrite + 1) % p->qnum;
        p->qcount = 1;
        return;
    }
    p->qread = p->qwrite;
    p->qcount = 0;
    HSD_PadRenewRawStatus(0);
}
void mu_console_rumble_step(void)
{
    HSD_PadRumbleInterpret();
}
/* The common scene loop asks once before its body-count loop, in every scene and
 * during pause. Loading may have left any number of stale alarm samples. Request
 * one VI now, then give the loop exactly one ticket. No host deadline drives this. */
int mu_console_engine_gate(int* count)
{
    static u32 body_retrace;
    if (!mu_console_active())
        return 0;
    HSD_PadLibData.qread = HSD_PadLibData.qwrite;
    HSD_PadLibData.qcount = 0;
    /* Rendering/loading may already have waited for VI since the previous body.
     * Reuse that retrace rather than adding a second presentation wait. */
    if (VIGetRetraceCount() == body_retrace)
        VIWaitForRetrace();
    body_retrace = VIGetRetraceCount();
    /* Normally the periodic pad alarm supplies the ticket. A scene can change its
     * sampling period or temporarily cancel it; neither may stop the scheduler. */
    if (mu_console_rumble_owned()) {
        /* Only a ticket; the earlier body hook replaces it with historical pads. */
        HSD_PadLibData.qread = HSD_PadLibData.qwrite;
        HSD_PadLibData.qcount = 1;
    } else if (HSD_PadLibData.qcount != 1)
        mu_console_replace_raw();
    *count = 1;
    return 1;
}
/* Called from the existing unconditional engine-body hook in mu_lab.c, before lb_800198E0
 * consumes a raw sample. Retraces and repeated PADRead calls are not simulation frames. */
int mu_console_frame_begin(void)
{
    /* The earlier rollback hook already injected this body's history, including on replay. */
    if (mu_console_rollback_active()) return 0;
    {
        int result = mu_console_host_frame_begin();
#ifdef MELEE_DOUBLES
        if (result == -51) {
            void mu_doubles_restart(void);
            mu_doubles_restart();
        }
#endif
        if (result == -50) {
            void mu_css_origin_restart(void);
            mu_css_origin_restart();
        }
        if (!result) return 0;
    }
    /* The host has composed this body's confirmed pads. Replace the ticket
     * before lb_800198E0 consumes it, including after a connection loss. */
    mu_console_replace_raw();
    return 1;
}
