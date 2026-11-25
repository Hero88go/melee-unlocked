/* Load the agreed session card without entering the memory-card scene. */
#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__)
#error "CSS origin scene continuation requires GCC/Clang builtin setjmp/longjmp on x86-64 or AArch64; port both ends together"
#endif
#include <mu_native.h>
#include <melee/gm/gmmain_lib.h>
#include "mu_doubles_game.h"
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <sysdolphin/baselib/debug.h>

int mu_console_active(void);
int mu_css_origin_host(int operation);
int mu_console_has_save(void);
void mu_console_log(const char* text);

void mu_console_load_save(void)
{
    lbCardResult result;
    if ((!mu_console_active() && !mu_css_origin_host(32)) || !mu_console_has_save()) {
        return;
    }
    /* gmmain has initialized card/save state already. Reuse GS_MEMCARD's work
     * allocation, synchronous read (including native endian conversion and name
     * banks), and per-entry repair/audio-video setup, in that order. No archives,
     * prompt, scene body, input, or frame-zero barrier runs here. Native CARD
     * callbacks complete through the event queue, without a timed wait. */
    lbCardNew_AllocWorkArea();
    result = lb_8001CBBC();
    /* As GS_MEMCARD does (gmscmemcard.c:317): finalize with whatever the read returned; an
     * unreadable save leaves defaults. Every seat reads the same agreed bytes, and a seat that
     * still differs fails the frame-0 digest instead of crashing the game here. */
    if (result != LbCardResult_Ready && result != LbCardResult_2) {
        mu_console_log("console: the session save could not be read; starting with defaults");
    }
    gmMainLib_8015FA34(result);
}

/* The context belongs to gm_801A4510, which stays live across every scene. Never longjmp
 * into a returned CSS frame. The host returns before the C-only unwind begins. */
static void** mu_css_origin_context;
MU_EXCLUSIONS(console_boot, MU_EXCLUDE(mu_css_origin_context))
int mu_css_origin_host(int operation);
void mu_css_origin_arm(void** context) { mu_css_origin_context = context; }
int mu_css_origin_resume(int bit) { return mu_css_origin_host(bit); }
int mu_css_origin_skip_draw(void) { return mu_css_origin_host(8); }
void mu_css_origin_boot(unsigned char* mode)
{
    if (mu_css_origin_host(0)) {
        if (!mu_console_active()) mu_console_load_save();
#ifdef MELEE_DOUBLES
        mu_doubles_boot();
#endif
        *mode = 2;
    } /* canonical origin */
    /* GM_VS, same first scene as console relaunch */
}
void mu_css_origin_restart(void)
{
    if (!mu_css_origin_context) __builtin_trap();
    __builtin_longjmp(mu_css_origin_context, 1);
}
