/* Community codes (HUD tab): native versions of the Gecko codes players asked for, each behind one
 * bit of the display options (MU_DISPLAY_OPTION_* in mu_host.h). Picture and sound only: none of
 * them changes what the match does, so they are each player's own, online and in replays too. The
 * draw-side pieces live where the codes hooked the game (if_2F6E.c, ftCo_Damage.c, ftdrawcommon.c,
 * grizumi.c, ifstatus.c), behind mu_code_feature(). */
#include <mu_native.h>

#include <math.h>

#include <dolphin/gx/GXStruct.h>
#include <melee/ft/types.h>
#include <melee/lb/lbcollision.h>
#include <melee/lb/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>

int mu_code_feature(unsigned int bit)
{
    static unsigned int logged = ~0u;
    const unsigned int on = mu_display_options() & MU_DISPLAY_CODES_ALL;
    if (on != logged) {
        logged = on;
        OSReport("[codes] community codes %06X\n", on);
    }
    return (on & bit) != 0;
}

/* The offline codes: the host leaves their bits out in any network session. */
static int mu_offline_code(unsigned int bit)
{
    static unsigned int logged = ~0u;
    const unsigned int on = mu_display_options() & (MU_DISPLAY_INSTANT_MELEE | MU_DISPLAY_FREE_PAUSE_CAMERA | MU_DISPLAY_BOOT_TO_CSS);
    if (on != logged) {
        logged = on;
        OSReport("[codes] offline codes %06X\n", on);
    }
    return (on & bit) != 0;
}
int mu_instant_melee(void) { return mu_offline_code(MU_DISPLAY_INSTANT_MELEE); }
int mu_free_pause_camera(void) { return mu_offline_code(MU_DISPLAY_FREE_PAUSE_CAMERA); }
int mu_boot_to_css(void) { return mu_offline_code(MU_DISPLAY_BOOT_TO_CSS); }

/* Hitboxes Display Knockback Angle (rmn, PKFreeZZy): a line from the bubble's centre to its edge
 * along the hit's knockback angle, mirrored for a fighter facing left. The Sakurai angle (361) is
 * drawn at 45 degrees, its angle in the air; angles past 361 (set by the hit itself) draw nothing.
 * Called right after the bubble was drawn, in the same pass. */
void mu_hitbox_angle(HitCapsule* hit, float fighter_scale, float facing)
{
    static GXColor inner = { 0xFF, 0xFF, 0xFF, 0xDD };
    static GXColor outer = { 0x30, 0x30, 0x30, 0xDD };
    Vec3 tip;
    float radius;
    float rad;

    if (!mu_code_feature(MU_DISPLAY_HITBOX_ANGLES) || hit->kb_angle > 361) {
        return;
    }
    radius = hit->x43_b1 ? hit->scale : hit->scale * fighter_scale;
    rad = (hit->kb_angle == 361 ? 45.0f : (float) hit->kb_angle) * 3.14159265f / 180.0f;
    tip.x = hit->x4C.x + cosf(rad) * radius * (facing < 0.0f ? -1.0f : 1.0f);
    tip.y = hit->x4C.y + sinf(rad) * radius;
    tip.z = hit->x4C.z;
    lbColl_80008FC8(hit->x4C, tip, &inner, &outer, radius * 0.08f);
}

static void mu_tint(HSD_JObj* jobj, GXColor diffuse, GXColor ambient)
{
    for (; jobj != NULL; jobj = jobj->next) {
        if (!(jobj->flags & 0x4020)) {   /* no spline or particle in place of the model list */
            HSD_DObj* dobj;
            for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
                if (dobj->mobj != NULL && dobj->mobj->mat != NULL) {
                    dobj->mobj->mat->diffuse = diffuse;
                    dobj->mobj->mat->ambient = ambient;
                }
            }
        }
        mu_tint(jobj->child, diffuse, ambient);
    }
}

/* Stock-Dependent Angel Platform Colors (Achilles1515): the respawn platform red with one stock
 * left, orange with two, green with three, the game's own above that. Its materials are the
 * platform's own copies, so only this platform changes. */
void mu_angel_platform_colors(Fighter* fp)
{
    static const GXColor diffuse[3] = { { 0xFF, 0x00, 0x00, 0xFF }, { 0xFF, 0x99, 0x00, 0xFF }, { 0x00, 0xFF, 0x00, 0xFF } };
    static const GXColor ambient[3] = { { 0x90, 0x00, 0x00, 0xFF }, { 0x9F, 0x5F, 0x00, 0xFF }, { 0x00, 0x59, 0x00, 0xFF } };
    int stocks;
    if (!mu_code_feature(MU_DISPLAY_ANGEL_STOCK_COLORS) || fp == NULL || fp->x20A0_accessory == NULL) {
        return;
    }
    stocks = Player_GetStocks(fp->player_idx);
    if (stocks >= 1 && stocks <= 3) {
        mu_tint(fp->x20A0_accessory, diffuse[stocks - 1], ambient[stocks - 1]);
    }
}
