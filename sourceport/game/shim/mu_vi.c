/* The video interface: the game says which framebuffer to show and waits for the retrace. The host
 * owns the real display, so this layer forwards the requests and runs the game's retrace callbacks
 * when the host reports a retrace. */
#include <dolphin/vi.h>
#include <dolphin/gx.h>

#include "mu_shim.h"

static VIRetraceCallback mu_pre_retrace, mu_post_retrace;

void VIInit(void) {}

void VIConfigure(GXRenderModeObj* rm)
{
    /* The mode's low two bits: 0 interlaced, 1 double-strike, 2 progressive. */
    mu_host->vi_configure(rm->fbWidth, rm->xfbHeight, (rm->viTVmode & 3) == 0);
}

void VIFlush(void) { mu_host->vi_flush(); }
void VISetNextFrameBuffer(void* fb) { mu_host->vi_set_next_framebuffer(fb); }
void VISetBlack(BOOL black) { mu_host->vi_set_black(black); }
u32 VIGetRetraceCount(void) { return mu_host->vi_retrace_count(); }
u32 VIGetNextField(void) { return mu_host->vi_next_field(); }
u32 VIGetTvFormat(void) { return 0; }   /* VI_NTSC: the build is NTSC 1.02 */
/* No component cable: the progressive-scan question is the host's setting, not the game's. */
u32 VIGetDTVStatus(void) { return 0; }

void VIWaitForRetrace(void)
{
    mu_host->vi_wait_retrace();
    mu_deliver_pending();
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback)
{
    VIRetraceCallback previous = mu_pre_retrace;
    mu_pre_retrace = callback;
    return previous;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback)
{
    VIRetraceCallback previous = mu_post_retrace;
    mu_post_retrace = callback;
    return previous;
}

static void mu_retrace_event(void* a, intptr_t count)
{
    (void) a;
    if (mu_pre_retrace)
        mu_pre_retrace((u32) count);
    if (mu_post_retrace)
        mu_post_retrace((u32) count);
}

/* The host's retrace: queued like the interrupt it replaces, so it waits for interrupts to be on. */
void mu_vi_retrace(void)
{
    mu_post(mu_retrace_event, 0, (intptr_t) mu_host->vi_retrace_count());
}
