/* The match HUD of the added fighters: stock icons and the emblem behind the damage number.
 *
 * The retail HUD file animates both textures over the retail characters only. An m-ex disc's HUD
 * file (IfAll) carries two more symbols: "Stc_icns", a texture animation with one stock icon per
 * fighter and costume, and "Eblm_matanim_joint", one with every emblem. m-ex puts those
 * animations on the HUD's joints and picks the frame from the fighter's ids:
 *
 *     stock icon frame = reserved frames + costume * stride + m-ex internal fighter id
 *     emblem frame     = the insignia id MxDt gives the external fighter id
 *
 * Here only an added character's own HUD joints get the m-ex animations. A retail character keeps
 * the retail animation and frame, so nothing changes for it. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/if/ifall.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/jobj.h>

#include <stddef.h>

#include "mu_disc.h"
#include "../mu_ak_fighter.h"

#ifdef MU_AKANEIA_FIGHTERS

/* "Stc_icns" of an m-ex HUD file. */
typedef struct AkStockIcons {
    u16 reserved;   /* frames before the fighters' icons */
    u16 stride;     /* frames per costume row */
    DISC_PTR(HSD_MatAnimJoint) matanim;
} DISC_STRUCT AkStockIcons;

/* The frame layout of the stock icon animation last put on a HUD (kept as numbers: the HUD file
 * itself goes away with the match). stride 0 = none seen. */
static int ak_stock_reserved, ak_stock_stride;

static void* hud_symbol(const char* name)
{
    HSD_Archive** archive = ifAll_GetArchive();
    return archive != NULL && *archive != NULL ? HSD_ArchiveGetPublicAddress(*archive, name) : NULL;
}

/* The m-ex internal fighter id behind an added character kind, -1 when it has none. */
static int internal_of_ckind(int ckind)
{
    return MU_AK_CKIND(ckind) ?
               mu_ak_mex_internal(MU_AK_KIND_BASE + (ckind - MU_AK_CKIND_BASE)) : -1;
}

/* ifStock_802F98E8: `root` is the stock row a match HUD just created for `player`. An added
 * character's seven icon joints (joints 1 to 7 of the row) take the m-ex icon animation. */
void mu_ak_hud_stock_icons(HSD_JObj* root, int player)
{
    AkStockIcons* icons;
    HSD_MatAnimJoint* matanim;
    int i;
    if (!MU_AK_CKIND(Player_GetPlayerCharacter(player))) {
        return;
    }
    icons = hud_symbol("Stc_icns");
    matanim = icons != NULL ? DP(icons->matanim) : NULL;
    if (matanim == NULL) {
        OSReport("[ak] the HUD file has no Stc_icns: player %d keeps the retail stock icons\n",
                 player);
        return;
    }
    ak_stock_reserved = icons->reserved;
    ak_stock_stride = icons->stride;
    for (i = 0; i < 7; i++) {
        HSD_JObj* icon = NULL;
        lb_80011E24(root, &icon, i + 1, -1);
        if (icon != NULL) {
            HSD_JObjAddAnimAll(icon, NULL, matanim, NULL);
        }
    }
}

/* gm_80168B34: the stock icon frame of an added character kind. */
float mu_ak_hud_stock_frame(int ckind, int costume)
{
    const int internal = internal_of_ckind(ckind);
    if (internal < 0 || ak_stock_stride == 0) {
        return 0.0f;
    }
    return (float) (ak_stock_reserved + costume * ak_stock_stride + internal);
}

/* ifStatus_802F61FC: `mark` is the emblem model of an added character's damage display. Puts
 * the m-ex emblem animation on it and gives the emblem's frame. Returns 0 when the HUD file has
 * no such animation (the caller keeps the retail frame). */
int mu_ak_hud_emblem(HSD_JObj* mark, int ckind, float* frame)
{
    HSD_MatAnimJoint* matanim = hud_symbol("Eblm_matanim_joint");
    HSD_JObj* joint = NULL;
    if (!MU_AK_CKIND(ckind) || matanim == NULL) {
        return 0;
    }
    lb_80011E24(mark, &joint, 1, -1);
    if (joint == NULL) {
        return 0;
    }
    HSD_JObjAddAnimAll(joint, NULL, matanim, NULL);
    *frame = (float) mu_mex_insignia(mu_ak_mex_external(ckind));
    return 1;
}

/* ---- the results screen ---- */

/* Whether this results screen's panels took the HUD file's stock icon and emblem animations. */
static int ak_results_stock, ak_results_emblem;

/* fn_80175DC8: `root` is the results panel model, its animations added but not yet started.
 * The disc's results file has placeholder stock icon and emblem textures: as m-ex does, the
 * four panels' stock icon joints (25 to 28, second display object) and emblem joints (66 to 69)
 * take the HUD file's animations, for every character. The HUD file is not this scene's own:
 * it is used only if the game still holds it loaded. */
void mu_ak_hud_results_build(HSD_JObj* root)
{
    HSD_Archive* archive;
    AkStockIcons* icons;
    HSD_MatAnimJoint* joint;
    int i;
    ak_results_stock = 0;
    ak_results_emblem = 0;
    if (!mu_mex_active()) {
        return;
    }
    archive = lbDvd_8001819C("IfAll");
    if (archive == NULL) {
        OSReport("[ak] results: the HUD file is not loaded; retail stock icons and emblems\n");
        return;
    }
    icons = HSD_ArchiveGetPublicAddress(archive, "Stc_icns");
    joint = icons != NULL ? DP(icons->matanim) : NULL;
    if (joint != NULL && DP(joint->matanim) != NULL) {
        ak_stock_reserved = icons->reserved;
        ak_stock_stride = icons->stride;
        for (i = 0; i < 4; i++) {
            HSD_JObj* stock = NULL;
            lb_80011E24(root, &stock, 25 + i, -1);
            if (stock != NULL && stock->u.dobj != NULL && stock->u.dobj->next != NULL) {
                HSD_DObjAddAnimAll(stock->u.dobj->next, DP(joint->matanim), NULL);
                ak_results_stock = 1;
            }
        }
    }
    joint = HSD_ArchiveGetPublicAddress(archive, "Eblm_matanim_joint");
    if (joint != NULL && DP(joint->matanim) != NULL) {
        for (i = 0; i < 4; i++) {
            HSD_JObj* emblem = NULL;
            lb_80011E24(root, &emblem, 66 + i, -1);
            if (emblem != NULL && emblem->u.dobj != NULL) {
                HSD_DObjAddAnimAll(emblem->u.dobj, DP(joint->matanim), NULL);
                ak_results_emblem = 1;
            }
        }
    }
    OSReport("[ak] results: stock icons %s, emblems %s (reserved %d, stride %d)\n",
             ak_results_stock ? "from the HUD file" : "retail",
             ak_results_emblem ? "from the HUD file" : "retail", ak_stock_reserved,
             ak_stock_stride);
}

/* The stock icon frame of native fighter kind `ftkind` in costume `costume` on a results
 * screen that took the HUD file's icon animation: reserved + costume * stride + the m-ex
 * internal id, and the fixed frames m-ex gives the special fighters. Returns 0 (frame left
 * alone) when this results screen kept the retail animation. */
int mu_ak_hud_results_stock(int ftkind, int costume, float* frame)
{
    int internal;
    if (!ak_results_stock) {
        return 0;
    }
    switch (ftkind) {
    case Ft_Kind_MasterH: *frame = 3.0f; return 1;
    case Ft_Kind_CrezyH: *frame = 2.0f; return 1;
    case Ft_Kind_Boy:
    case Ft_Kind_Girl: *frame = 1.0f; return 1;
    case Ft_Kind_GKoops: *frame = 5.0f; return 1;
    case Ft_Kind_Sandbag: *frame = 6.0f; return 1;
    default: break;
    }
    internal = mu_ak_mex_internal(ftkind);
    if (internal < 0) {
        return 0;
    }
    *frame = (float) (ak_stock_reserved + costume * ak_stock_stride + internal);
    return 1;
}

/* The emblem frame of character kind `ckind` on such a results screen: the insignia MxDt gives
 * its external id. Returns 0 when this results screen kept the retail animation. */
int mu_ak_hud_results_emblem(int ckind, float* frame)
{
    int ext;
    if (!ak_results_emblem) {
        return 0;
    }
    ext = mu_ak_mex_external(ckind);
    if (ext < 0) {
        return 0;
    }
    *frame = (float) mu_mex_insignia(ext);
    return 1;
}

#endif /* MU_AKANEIA_FIGHTERS */
