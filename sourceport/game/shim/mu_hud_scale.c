/* Stock icon and damage number sizes (the HUD sliders in the settings panel). The Static Recomp
 * rescales the same HUD roots in guest memory (port/runtime/gx/gx_core.cpp, scale_hud_root); the
 * native game does it from the HUD's draw callbacks, so a re-simulated rollback frame, which draws
 * nothing, never touches it. Display only. */
#include "mu_native.h"

#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

unsigned int mu_hud_scales_abi(void);   /* mu_online_abi.c: stock | damage << 8, percent */

typedef struct MuHudRoot {
    HSD_JObj* root;
    float base_x, base_y;       /* the game's own scale */
    float wrote_x, wrote_y;     /* what was last written, to see the game change it */
} MuHudRoot;

static MuHudRoot mu_hud_roots[16];
static unsigned int mu_hud_next;

void mu_hud_scale(HSD_GObj* gobj, int stock)
{
    unsigned int packed = mu_hud_scales_abi();
    int percent = stock ? (int) (packed & 0xFF) : (int) ((packed >> 8) & 0xFF);
    HSD_JObj* root;
    MuHudRoot* c = NULL;
    float factor;
    unsigned int i;

    if (gobj == NULL || (root = gobj->hsd_obj) == NULL) {
        return;
    }
    mu_hud_fade(gobj);
    if (stock && (mu_game_options() & MU_OPTION_PAL_STOCK_ICONS)) {
        percent = 100;   /* PAL stock icons set their own size */
    }
    for (i = 0; i < sizeof(mu_hud_roots) / sizeof(mu_hud_roots[0]); i++) {
        if (mu_hud_roots[i].root == root) {
            c = &mu_hud_roots[i];
            break;
        }
    }
    if (c == NULL) {
        if (percent == 0 || percent == 100) {
            return;   /* never scaled: nothing to do */
        }
        c = &mu_hud_roots[mu_hud_next++ % (sizeof(mu_hud_roots) / sizeof(mu_hud_roots[0]))];
        c->root = root;
        c->base_x = root->scale.x;
        c->base_y = root->scale.y;
    } else if (root->scale.x != c->wrote_x || root->scale.y != c->wrote_y) {
        c->base_x = root->scale.x;   /* the game set a new scale since (a new match, an anim) */
        c->base_y = root->scale.y;
    }
    if (percent < 75 || percent > 175) {
        percent = percent == 0 ? 100 : percent < 75 ? 75 : 175;
    }
    factor = (float) percent / 100.0f;
    c->wrote_x = c->base_x * factor;
    c->wrote_y = c->base_y * factor;
    if (root->scale.x != c->wrote_x || root->scale.y != c->wrote_y) {
        HSD_JObjSetScaleX(root, c->wrote_x);
        HSD_JObjSetScaleY(root, c->wrote_y);
    }
}

/* ---- player tags for the host's overlays ---- */
#include <melee/if/ifstatus.h>
#include <melee/if/types.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/forward.h>

int mu_nametag_position(int slot, float* x, float* y);   /* if/ifnametag.c */
void mu_hud_player_abi(int slot, int present, int damage, int stocks, float tag_x, float tag_y,
                       int tag_visible);                   /* mu_online_abi.c */
void mu_hud_combo_abi(int slot, int link, float percent, int stunned);   /* mu_online_abi.c */
void mu_hud_extras_abi(int slot, int nana_damage, int team);             /* mu_online_abi.c */

static int mu_combo_stun_action(unsigned int a)
{
    return a == 38 || (a >= 75 && a <= 91) || (a >= 183 && a <= 204) || (a >= 223 && a <= 232) ||
           (a >= 239 && a <= 243) || (a >= 266 && a <= 296);
}

/* A fighter object that is alive now: on the game's fighter list. Right after a match the player slots
 * can still name fighters already freed (with the results screen off the next scene starts at once),
 * and reading one of those crashed the game at the end of the match. */
static int mu_live_fighter(const HSD_GObj* gobj)
{
    const HSD_GObj* cur;
    if (gobj == NULL || HSD_GObjPLinkHead == NULL) return 0;
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        if (cur == gobj) return 1;
    }
    return 0;
}

void mu_hud_report(void)
{
    int slot;
    for (slot = 0; slot < 4; slot++) {
        float x = 0.0f, y = 0.0f;
        const int present = Player_GetPlayerSlotType(slot) != Gm_PKind_NA;
        const int visible = present && mu_nametag_position(slot, &x, &y);
        mu_hud_player_abi(slot, present, present ? ifStatus_HudInfo.players[slot].damage_percent : 0,
                          present ? Player_GetStocks(slot) : 0, x, y, visible);
        /* For the Arcade look's combo counter (read only): the player this one's combo link holds right
         * after a hit (x2094), its percent, and whether it cannot act (hitlag, or a damage, down,
         * grabbed or thrown action; the same list as the host's combo_stun_action). */
        {
            HSD_GObj* gobj = present ? Player_GetEntity(slot) : NULL;
            Fighter* fp = mu_live_fighter(gobj) ? gobj->user_data : NULL;
            Fighter* link = fp != NULL && mu_live_fighter(fp->x2094) ? fp->x2094->user_data : NULL;
            mu_hud_combo_abi(slot, link != NULL ? link->player_idx : -1, fp != NULL ? fp->dmg.x1830_percent : 0.0f,
                             fp != NULL && (fp->dmg.x195c_hitlag_frames > 0.0f || mu_combo_stun_action(fp->motion_id)));
        }
        /* The community codes' overlays (Nana's percent, the teammate marker): read only. */
        {
            HSD_GObj* nana = present ? Player_GetEntityAtIndex(slot, 1) : NULL;
            Fighter* np = mu_live_fighter(nana) ? nana->user_data : NULL;
            mu_hud_extras_abi(slot, np != NULL ? (int) np->dmg.x1830_percent : -1,
                              present && gm_8016B168() ? Player_GetTeam(slot) : -1);
        }
    }
}

/* ---- HUD Transparency (UnclePunch), a community code ----
 * Each HUD part (damage, stock icons, emblem) fades to 40% while a fighter stands behind it and back
 * when it leaves, 8% a frame as in the code. Done from the parts' own draw callbacks, so a re-simulated
 * rollback frame, which draws nothing, never runs it; only the parts' material alpha changes. */
#include <melee/cm/camera.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>

typedef struct MuHudFade {
    HSD_JObj* root;
    float alpha;
} MuHudFade;
static MuHudFade mu_hud_fades[24];
static unsigned int mu_hud_fade_next;

static void mu_hud_set_alpha(HSD_JObj* jobj, float alpha)
{
    for (; jobj != NULL; jobj = jobj->next) {
        if (!(jobj->flags & 0x4020)) {   /* no spline or particle in place of the model list */
            HSD_DObj* dobj;
            for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
                if (dobj->mobj != NULL && dobj->mobj->mat != NULL) {
                    dobj->mobj->mat->alpha = alpha;
                }
            }
        }
        mu_hud_set_alpha(jobj->child, alpha);
    }
}

/* A fighter in front of the part: the part's box on screen (its centre, 50 pixels each side, 40 above
 * and 32 below) crosses the fighter's span from its feet to a head height above them. */
static int mu_hud_covered(HSD_JObj* root)
{
    HSD_GObj* cam = Camera_80030A50();
    HSD_CObj* hud = HSD_CObjGetCurrent();
    HSD_GObj* cur;
    Vec3 world, part;
    if (cam == NULL || cam->hsd_obj == NULL || hud == NULL || HSD_GObjPLinkHead == NULL) {
        return 0;
    }
    lb_8000B1CC(root, NULL, &world);
    lbVector_WorldToScreen(hud, &world, &part, 0);
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        Fighter* fp = cur->user_data;
        Vec3 feet, head, a, b;
        if (fp == NULL) {
            continue;
        }
        feet = fp->cur_pos;
        head = feet;
        head.y += 14.0f * fp->x34_scale.y;
        if (feet.x < -40000.0f || feet.x > 40000.0f || feet.y < -40000.0f || feet.y > 40000.0f) {
            continue;
        }
        lbVector_WorldToScreen(cam->hsd_obj, &feet, &a, 0);
        lbVector_WorldToScreen(cam->hsd_obj, &head, &b, 0);
        if (a.x > part.x - 50.0f && a.x < part.x + 50.0f && b.y < part.y + 32.0f && a.y > part.y - 40.0f) {
            return 1;
        }
    }
    return 0;
}

void mu_hud_fade(HSD_GObj* gobj)
{
    HSD_JObj* root = gobj != NULL ? gobj->hsd_obj : NULL;
    MuHudFade* f = NULL;
    float target;
    unsigned int i;
    if (root == NULL) {
        return;
    }
    for (i = 0; i < sizeof(mu_hud_fades) / sizeof(mu_hud_fades[0]); i++) {
        if (mu_hud_fades[i].root == root) {
            f = &mu_hud_fades[i];
            break;
        }
    }
    target = mu_code_feature(MU_DISPLAY_HUD_FADE) && mu_hud_covered(root) ? 0.4f : 1.0f;
    if (f == NULL) {
        if (target >= 1.0f) {
            return;   /* never faded: the game's own alpha stays untouched */
        }
        f = &mu_hud_fades[mu_hud_fade_next++ % (sizeof(mu_hud_fades) / sizeof(mu_hud_fades[0]))];
        f->root = root;
        f->alpha = 1.0f;
    }
    if (f->alpha == target) {
        if (target >= 1.0f) {
            f->root = NULL;   /* back to full: let go, as if it had never faded */
            return;
        }
    } else {
        f->alpha += target > f->alpha ? 0.08f : -0.08f;
        if ((target > 0.5f) == (f->alpha > target) || f->alpha < 0.4f) {
            f->alpha = target;
        }
    }
    mu_hud_set_alpha(root, f->alpha);
}
