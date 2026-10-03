/* Native form of the code set Legacy runs on every boot: Slippi's "General Codes" (unlocks, default
 * rules, menu codes, NeutralSpawn, UCF 0.84 and the rest) and "Lagless FoD". Legacy's bootloader
 * fetches that table once and applies it once, so the native game decides once too (at boot, from
 * the host's MU_GAME_OPTION_VANILLA). Each patched site carries its own MU_NATIVE branch; this file
 * holds the latch and the UCF code's shared state, which Legacy keeps in the pad-buffer code's data.
 *
 * Arithmetic follows the translated code: single-precision fused ops are the double product and
 * sum rounded to float, compares keep the PowerPC branch sense for NaN, and fctiwz truncates. */
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

unsigned int mu_game_options(void);
void mu_gecko_default_rules(void);   /* gm/gmmain_lib.c */
int mu_online_active(void);          /* shim/mu_online.c: a network match is running */
char* getenv(const char* name);

static int general_codes = -1;

/* M9 evidence, off by default: MELEE_TRACE_GECKO logs each time a UCF code changes an outcome, so a
 * paired run can show which paths its exact digests actually covered (frame = the match frame the
 * state digest reports). */
static int trace_gecko = -1;
static void ucf_trace(const char* what, const Fighter* fp)
{
    if (trace_gecko < 0)
        trace_gecko = getenv("MELEE_TRACE_GECKO") != NULL;
    if (trace_gecko)
        OSReport("[gecko] %s port=%d motion=%d frame=%u\n", (char*) what, fp->pad_port,
                 (int) fp->motion_id, (unsigned) gm_GetFrameCount());
}

void mu_general_codes_boot(void)
{
#ifdef MU_NO_SLIPPI
    /* This build is the retail game, whatever the host passes and also in a network match: no
     * General Codes and, through them, no UCF (mu_ucf_enabled). */
    general_codes = 0;
#else
    general_codes = (mu_game_options() & MU_OPTION_VANILLA) == 0;
#endif
    if (general_codes)
        mu_gecko_default_rules();
}

int mu_general_codes(void)
{
    if (general_codes < 0)
        mu_general_codes_boot();
#ifdef MU_NO_SLIPPI
    return 0;
#endif
    /* Online opponents always play with the General Codes, so an online match does too. */
    return general_codes || mu_online_codes() != 0;
}

/* Slippi's optional "Widescreen 16:9" code, one MU_NATIVE branch per patched site with the code's own
 * values: cobj.c (camera aspect x 320/219 as it loads), lbbgflash.c (flash camera), ftdrawcommon.c
 * (fighters in the added sides draw), ifmagnify.c (bubbles) and ifnametag.c (tags). Read live, so like
 * the code it takes effect as each camera loads. Unlike the code, everything the game reads stays
 * Melee's own: the on-screen test (camera.c) and Onett's cars measure with the 73:60 camera, and a
 * fighter drawn only for widescreen gets its state put back (the draw sets up bone matrices that
 * gameplay reads). So the option changes only the picture, online and in replays. */
int mu_widescreen(void)
{
    return (mu_game_options() & MU_OPTION_WIDESCREEN) != 0;
}

/* Either widescreen mode shows more to each side than Melee's own camera sees. Under True 16:9 the
 * game's cameras are untouched, so the fighter draw (ftdrawcommon.c) is the only place that needs to
 * know: without it a fighter in the added sides was not drawn at all, only the bubble. */
int mu_wide_picture(void)
{
    return (mu_game_options() & (MU_OPTION_WIDESCREEN | MU_OPTION_TRUE_WIDESCREEN)) != 0;
}

/* The player's language choice from the PC settings (see mu_native.h). */
int mu_language_override(void)
{
    const unsigned int choice = (mu_game_options() & MU_OPTION_LANGUAGE_MASK) >> MU_OPTION_LANGUAGE_SHIFT;
    return choice == 1 ? 0 : choice == 2 ? 1 : -1;
}

/* UCF 0.84 is part of the General Codes; in a replay it runs only when the replay carries it.
 * Exported for the tumble wiggle site (ftCo_DamageFall.c). */
int mu_ucf_enabled(void)
{
    return mu_general_codes() && mu_replay_allows(MU_RC_UCF084);
}

/* The pad-buffer code's data, per controller port: the raw stick of the last four fighter-input
 * frames as (x, y), the index of the newest sample, and the fast-down-flick counter. Humans only
 * (the code skips CPU-controlled fighters), so ports 0-3. */
static struct {
    signed char s[4][2];
    unsigned char idx;
    unsigned char flick;
} ucf_ring[4];

static int ucf_port(const Fighter* fp)
{
    return fp->pad_port < 4 ? fp->pad_port : -1;
}

/* (newest - two samples earlier) squared, for one axis: the codes' "fast movement" test. */
static int ucf_delta2(int port, int axis)
{
    int idx = ucf_ring[port].idx;
    int d = ucf_ring[port].s[idx][axis] - ucf_ring[port].s[(idx - 2) & 3][axis];
    return d * d;
}

/* fctiwz(fmsubs(|v|, 80, 0.0001)) + 2: a normalized axis in stick units, as the codes square it. */
static int ucf_units(float v)
{
    float mag = __builtin_fabsf(v);
    float units = (float) ((double) mag * 80.0 - (double) 0.0001f);
    return (int) units + 2;
}

/* 1.0 cardinals: a raw axis at 80 or beyond with the other axis within 6 becomes exactly +-1, 0. */
static void ucf_cardinal(int x, int y, Vec2* out)
{
    if ((unsigned char) (x + 79) > 158) {
        if ((unsigned char) (y + 6) > 12)
            return;
        out->x = x < 0 ? -1.0f : 1.0f;
        out->y = 0.0f;
        return;
    }
    if ((unsigned char) (y + 79) <= 158)
        return;
    if ((unsigned char) (x + 6) > 12)
        return;
    out->x = 0.0f;
    out->y = y < 0 ? -1.0f : 1.0f;
}

/* UCF Pad Buffer + 1.0 Cardinals, in Fighter_Spaghetti_8006AD10 before active_duration.trigger
 * counts up (where Legacy's hook sits). Reads the raw pad the frame's input came from. */
void mu_ucf_pad_buffer(Fighter* fp)
{
    const PADStatus* raw;
    int port, qread, idx, flick;

    if (!mu_ucf_enabled() || ftCo_IsCpuControlled(fp) || (port = ucf_port(fp)) < 0)
        return;
    qread = HSD_PadLibData.qread != 0 ? HSD_PadLibData.qread - 1 : 4;
    raw = &HSD_PadLibData.queue[qread].stat[port];
    idx = (ucf_ring[port].idx + 1) & 3;
    ucf_ring[port].idx = (unsigned char) idx;
    ucf_ring[port].s[idx][0] = raw->stickX;
    ucf_ring[port].s[idx][1] = raw->stickY;

    /* Not during Zelda's up special start, which reads the stick angle as given. */
    if (!(fp->kind == 19 && fp->motion_id == 349)) {
        Vec2 before = fp->input.lstick[0];
        ucf_cardinal(raw->stickX, raw->stickY, &fp->input.lstick[0]);
        ucf_cardinal(raw->substickX, raw->substickY, &fp->input.cstick[0]);
        if (before.x != fp->input.lstick[0].x || before.y != fp->input.lstick[0].y)
            ucf_trace("cardinal", fp);
    }

    flick = 0;
    if (!(fp->input.lstick[0].y > -0.609375f)) {
        int ux = ucf_units(fp->input.lstick[0].x);
        int uy = ucf_units(fp->input.lstick[0].y);
        if (ux * ux + uy * uy > 6400) {
            flick = ucf_ring[port].flick;
            if (flick != 0) {
                flick = (unsigned char) (flick + 1);
            } else if (fp->active_timer.lstick.y <= 1 && ucf_delta2(port, 1) > 1936) {
                flick = 1;
                ucf_trace("flick", fp);
            }
        }
    }
    ucf_ring[port].flick = (unsigned char) flick;
}

/* UCF Dashback, in ftCo_Turn_IASA right after the turn flips facing_dir: on the turn's second
 * frame, a stick that crossed the dash threshold within a frame and moved fast becomes a dash. */
void mu_ucf_dashback(Fighter* fp)
{
    unsigned int frame_bits;
    float toward;
    int port;
    HSD_GObj* partner;

    if (!mu_ucf_enabled() || fp->is_sub_fighter)
        return;
    __builtin_memcpy(&frame_bits, &fp->cur_anim_frame, sizeof frame_bits);
    if (frame_bits != 0x40000000u)
        return;
    toward = fp->facing_dir * fp->input.lstick[0].x;
    if (toward < p_ftCommonData->dash_smash_stick_threshold)
        return;
    if (fp->active_timer.lstick.x > 1 || (port = ucf_port(fp)) < 0)
        return;
    if (ucf_delta2(port, 0) <= 5625)
        return;
    fp->mv.co.turn.has_turned = true;
    fp->mv.co.turn.just_turned = true;
    ucf_trace("dashback", fp);
    /* Ice Climbers: the partner copies the leader's recorded input, so turn its record too. */
    partner = Player_GetEntityAtIndex(fp->player_idx, 1);
    if (partner != NULL) {
        Fighter* nana = partner->user_data;
        unsigned int facing_bits;
        __builtin_memcpy(&facing_bits, &fp->facing_dir, sizeof facing_bits);
        if (nana != NULL && nana->cpu.x444 != NULL) {   /* the console would fault instead */
            nana->cpu.x444->facing_dir = fp->facing_dir;
            nana->cpu.x444->lstick.x = (s8) ((facing_bits >> 31) + 127);
        }
    }
}

/* UCF DBOOC SquatRv Fix: the threshold ftCo_SquatRv_CheckInput compares the stick with. */
float mu_ucf_squatrv_threshold(Fighter* fp, float vanilla)
{
    int ux, uy;
    if (!mu_ucf_enabled() || fp->active_timer.lstick.x >= 1)
        return vanilla;
    ux = ucf_units(fp->input.lstick[0].x);
    uy = ucf_units(fp->input.lstick[0].y);
    if (ux * ux + uy * uy > 6400) {
        /* The outcome changes when the vanilla threshold would stand up and 0.59 does not. */
        if (fp->input.lstick[0].y > -vanilla && !(fp->input.lstick[0].y > -0.59f))
            ucf_trace("squatrv", fp);
        return 0.59f;
    }
    return vanilla;
}

/* UCF SDI: a second chance when the vanilla window has passed, for a stick that was inside the SDI
 * deadzone last frame and moved fast since. Nonzero means SDI. */
int mu_ucf_sdi(Fighter* fp)
{
    float yy, prev, th;
    int port;
    if (!mu_ucf_enabled())
        return 0;
    if (fp->active_sticky.lstick.x > 1 && fp->active_sticky.lstick.y > 1)
        return 0;
    yy = fp->input.lstick[1].y * fp->input.lstick[1].y;
    prev = (float) ((double) fp->input.lstick[1].x * (double) fp->input.lstick[1].x + (double) yy);
    th = p_ftCommonData->sdi_min_stick_mag * p_ftCommonData->sdi_min_stick_mag;
    if (th <= prev || (port = ucf_port(fp)) < 0)
        return 0;
    if (ucf_delta2(port, 0) + ucf_delta2(port, 1) <= 3844)
        return 0;
    ucf_trace("sdi", fp);
    return 1;
}

/* UCF Shield SDI: the same second chance for shield SDI, on the X axis. Nonzero means SDI. */
int mu_ucf_shield_sdi(Fighter* fp)
{
    int port;
    if (!mu_ucf_enabled() || fp->active_sticky.lstick.x > 1)
        return 0;
    if (fp->input.lstick[1].x >= p_ftCommonData->sdi_min_stick_mag || (port = ucf_port(fp)) < 0)
        return 0;
    if (ucf_delta2(port, 0) <= 3844)
        return 0;
    ucf_trace("shield-sdi", fp);
    return 1;
}

/* UCF Shield Drop, at the start of the spot dodge from shield: on a platform, a control-stick input
 * past the stick ring that is not a straight-down spot dodge input is left to the shield drop
 * check instead. Nonzero means cancel the spot dodge (the caller then reports no spot dodge). */
int mu_ucf_shield_drop(Fighter* fp)
{
    int ux, uy;
    if (!mu_ucf_enabled())
        return 0;
    if (fp->input.cstick[0].y <= p_ftCommonData->x314)
        return 0;
    if (fp->active_timer.lstick.x < p_ftCommonData->x320)
        return 0;
    if (fp->input.lstick[0].y <= -0.8f)
        return 0;
    if (fp->coll_data.floor.index == -1 || !(fp->coll_data.floor.flags & 0x100))
        return 0;
    uy = ucf_units(fp->input.lstick[0].y);
    ux = ucf_units(fp->input.lstick[0].x);
    if (uy * uy + ux * ux <= 6400)
        return 0;
    ucf_trace("shield-drop", fp);
    return 1;
}

/* UCF Shield Drop Extended: the shield drop check also accepts a stick short of the vanilla
 * threshold once the pad buffer has counted a fast downward flick for two frames. */
int mu_ucf_shield_drop_extended(Fighter* fp)
{
    int port = ucf_port(fp);
    if (!(mu_ucf_enabled() && port >= 0 && ucf_ring[port].flick > 1))
        return 0;
    ucf_trace("shield-drop-extended", fp);
    return 1;
}

/* UCF Tumble: whether the wiggle out of tumble happens (the caller asks only with the codes on).
 * Replaces the vanilla timer window: frame 0 wiggles, frame 1 only for a stick that was inside the
 * deadzone last frame and moved fast. */
int mu_ucf_tumble_wiggle(Fighter* fp)
{
    int timer = fp->active_timer.lstick.x, port;
    if (timer == 0)
        return 1;
    if (timer != 1)
        return 0;
    if (__builtin_fabsf(fp->input.lstick[1].x) >= p_ftCommonData->x210 || (port = ucf_port(fp)) < 0)
        return 0;
    if (ucf_delta2(port, 0) <= 5625)
        return 0;
    ucf_trace("tumble", fp);
    return 1;
}

/* ---- the player's own Gecko codes: data writes ----
 * A Gecko write names a console address and carries big-endian bytes. There is no console memory
 * here, so a write can run only where the console object has a native twin with the same layout:
 * the table below, generated by tools/gecko_targets.py (scalars and arrays of fixed-width numbers,
 * never anything holding a pointer). The same table, with the reason for every other symbol, is
 * what the host classifies a code with (port/runtime/host/user_gecko_targets.h). Both builds carry
 * it: the tool leaves out anything defined under a preprocessor conditional. */
#define MU_GECKO_OBJECT(decl, name, size)                                          \
    extern decl;                                                                   \
    _Static_assert(sizeof(name) == (size), "console and native size differ: " #name);
#include "mu_gecko_targets.inc"
#undef MU_GECKO_OBJECT

typedef struct MuGeckoTarget {
    unsigned int addr;    /* console address of the object */
    unsigned int size;
    unsigned char* native;
    const char* layout;   /* widths of the elements of one period, as digits: "4", "224" */
} MuGeckoTarget;

#define MU_GECKO_TARGET(addr, size, name, layout) { addr, size, (unsigned char*) &name, layout },
static const MuGeckoTarget gecko_targets[] = {
#include "mu_gecko_targets.inc"
};
#undef MU_GECKO_TARGET

/* Console byte `offset` of an object sits in the same element natively, at the mirrored position
 * within it: that is the whole byte order conversion, and it holds for a write of any width at any
 * offset (a 32-bit write over two 16-bit fields swaps each half on its own). */
static unsigned int gecko_native_offset(const char* layout, unsigned int offset)
{
    unsigned int period = 0, start = 0, pos;
    const char* c;
    for (c = layout; *c != '\0'; ++c)
        period += (unsigned int) (*c - '0');
    pos = offset % period;
    for (c = layout; *c != '\0'; ++c) {
        unsigned int width = (unsigned int) (*c - '0');
        if (pos < start + width)
            return offset - pos + start + (width - 1 - (pos - start));
        start += width;
    }
    return offset;   /* not reached: pos < period */
}

/* One write of an enabled code, from the host (user_gecko.cpp, once per frame as the pads are
 * read): `len` big-endian bytes for console address `addr`. Returns 1 when stored, 0 when the range
 * is not wholly inside one object of the table (the host's table said otherwise: the two were built
 * from different trees), -1 when skipped because a network match is running (a code the other
 * player does not run would take the match out of sync). */
__declspec(dllexport) int mu_user_gecko_write(unsigned int addr, const unsigned char* bytes, unsigned int len)
{
    unsigned int lo = 0, hi = sizeof gecko_targets / sizeof gecko_targets[0], k;
    const MuGeckoTarget* t;

    if (mu_online_active())
        return -1;
    while (hi - lo > 1) {
        unsigned int mid = lo + (hi - lo) / 2;
        if (gecko_targets[mid].addr <= addr)
            lo = mid;
        else
            hi = mid;
    }
    t = &gecko_targets[lo];
    if (len == 0 || addr < t->addr || addr - t->addr >= t->size || len > t->size - (addr - t->addr))
        return 0;
    for (k = 0; k < len; ++k)
        t->native[gecko_native_offset(t->layout, addr - t->addr + k)] = bytes[k];
    return 1;
}
