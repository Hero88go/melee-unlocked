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
#include <melee/pl/player.h>

int mu_nametag_position(int slot, float* x, float* y);   /* if/ifnametag.c */
void mu_hud_player_abi(int slot, int present, int damage, int stocks, float tag_x, float tag_y,
                       int tag_visible);                   /* mu_online_abi.c */

void mu_hud_report(void)
{
    int slot;
    for (slot = 0; slot < 4; slot++) {
        float x = 0.0f, y = 0.0f;
        const int present = Player_GetPlayerSlotType(slot) != Gm_PKind_NA;
        const int visible = present && mu_nametag_position(slot, &x, &y);
        mu_hud_player_abi(slot, present, present ? ifStatus_HudInfo.players[slot].damage_percent : 0,
                          present ? Player_GetStocks(slot) : 0, x, y, visible);
    }
}
