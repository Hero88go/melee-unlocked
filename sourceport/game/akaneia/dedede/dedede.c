/* King Dedede (Akaneia), native: the fighter description, load/respawn and the general callbacks.
 *
 * Source: PlDe.dat "ftFunction" (m-ex), rewritten by hand. The routine names in comments are the
 * debug symbols the m-ex build left in the file. */
#include "ftDe.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/memory.h>

_Static_assert(sizeof(ftDe_FighterVars) <= sizeof(((Fighter*) 0)->u), "Dedede fighter vars");
_Static_assert(sizeof(ftDe_MotionVars) <= sizeof(((Fighter*) 0)->mv), "Dedede motion vars");
_Static_assert(sizeof(ftDe_DatAttrs) == 0x17C, "Dedede attributes are 0x17C bytes on the disc");

/* ---------------------------------------------------------------- load, respawn, destroy */

/* m-ex 0x803D706C allocates zeroed memory. */
static void* ftDe_Calloc(u32 size)
{
    void* p = HSD_MemAlloc(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

/* OnLoad */
static void ftDe_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);
    DISC_PTR(Article)* articles = (void*) DP(fp->ft_data->x48_items);
    int i;

    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftDe_DatAttrs));
    fp->dat_attrs = fp->dat_attrs_backup;

    for (i = 0; i < ftDe_Article_Count; i++) {
        mu_ak_register_article(fp->kind, DP(articles[i]), i);
    }

    fp->can_multijump = true;
    fp->x2D0 = (Fighter_x2D0_t*) ((u8*) fp->dat_attrs + 0x148);

    fv->scratch_30 = ftDe_Calloc(0x30);
    fv->scratch_C0 = ftDe_Calloc(0xC0);
    fv->ecb_joint = fp->parts[5].joint;
    fv->ecb_joint_dair = fp->parts[46].joint;
}

/* OnRespawn (exported in the "ondeath" slot) */
static void ftDe_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 0);
    ftDe_Vars(fp)->held_gordo = NULL;
}

/* OnDestroy (exported in the "onunknown" slot) */
static void ftDe_OnDestroy(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_Free(ftDe_Vars(fp)->scratch_C0);
    HSD_Free(ftDe_Vars(fp)->scratch_30);
}

/* ResetAttributes (exported in the "onrespawn" slot) */
static void ftDe_ResetAttributes(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftDe_DatAttrs));
}

/* ---------------------------------------------------------------- specials */

static void ftDe_SpecialN(HSD_GObj* gobj)
{
    ftDe_SpecialN_Enter(gobj);
}

static void ftDe_SpecialS(HSD_GObj* gobj)
{
    ftDe_SpecialS_Enter(gobj);
}

static void ftDe_SpecialHi(HSD_GObj* gobj)
{
    ftDe_SpecialHi_Enter(gobj, 0.0f);
}

static void ftDe_SpecialLw(HSD_GObj* gobj)
{
    ftDe_SpecialLw_Enter(gobj);
}

static void ftDe_SpecialAirLw(HSD_GObj* gobj)
{
    ftDe_SpecialAirLw_Enter(gobj);
}

/* ---------------------------------------------------------------- items */

/* OnItemPickup. The table slot is a MuAkEvent; the game calls it with the "catch" flag as a
 * second argument, like every ftData_OnItemPickup entry. */
static void ftDe_OnItemPickup(HSD_GObj* gobj, bool catch_anim)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (itIsHeavy(fp->item_gobj)) {
        return;
    }
    switch (itGetHoldKind(fp->item_gobj)) {
    case 1:
        ftAnim_80070FB4(gobj, 0, 1);
        break;
    case 2:
        ftAnim_80070FB4(gobj, 0, 0);
        break;
    case 3:
        ftAnim_80070FB4(gobj, 0, 2);
        break;
    case 4:
        ftAnim_80070FB4(gobj, 0, 3);
        break;
    default:
        break;
    }
    if (catch_anim) {
        ftAnim_80070C48(gobj, 0);
    }
}

/* OnSetItemInvisible and OnSetItemVisible do the same thing in the m-ex code. */
static void ftDe_OnItemVisibility(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, 0);
    }
}

/* OnItemRelease */
static void ftDe_OnItemRelease(HSD_GObj* gobj, bool arg1)
{
    ftAnim_80070FB4(gobj, 0, -1);
    if (arg1) {
        ftAnim_80070CC4(gobj, 0);
    }
}

/* ---------------------------------------------------------------- eyes, state change, jump */

/* EyeTextureDamaged (exported in the "onhit" slot) */
static void ftDe_EyeTextureDamaged(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0f);
    ftAnim_800704F0(gobj, 1, 3.0f);
}

/* EyeTextureNormal */
static void ftDe_EyeTextureNormal(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0f);
    ftAnim_800704F0(gobj, 1, 0.0f);
}

/* OnActionStateChange: the charged Down B keeps its color overlay through its own states, and Down
 * Air uses a different bone for the ECB. */
static void ftDe_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDe_FighterVars* fv = ftDe_Vars(fp);

    if ((u32) (fp->motion_id - ftDe_MS_SpecialLwHold) <= ftDe_MS_SpecialLwLanding - ftDe_MS_SpecialLwHold) {
        DISC_PTR(void)* list = (void*) DP(fp->ft_data->x48_items);
        ftDe_ColAnims* colanims = (ftDe_ColAnims*) DP(list[4]);
        ftDe_OverlaySlot* overlays = DP(colanims->overlays);
        int which = ftDe_MV(fp)->speciallw.charged != 0 ? 1 : 0;
        lb_800144C8(&fp->x488, DP(overlays[which]), 0, 0);
    }

    if (fp->motion_id == FTDE_CO_ATTACKAIRLW) {
        fp->coll_data.ecb_source.x10C_joint[5] = fv->ecb_joint_dair;
    } else {
        fp->coll_data.ecb_source.x10C_joint[5] = fv->ecb_joint;
    }
}

/* OnFrame: empty in the m-ex code. */
static void ftDe_OnFrame(HSD_GObj* gobj) {}

/* EnterDoubleJump */
static void ftDe_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* ---------------------------------------------------------------- the fighter */

const MuAkFighter mu_ak_dedede = {
    .name = "King Dedede",
    .file = "PlDe.dat",

    .onload = ftDe_OnLoad,
    .ondeath = ftDe_OnDeath,
    .onunknown = ftDe_OnDestroy,
    .specialn = ftDe_SpecialN,
    .specialairn = ftDe_SpecialN,
    .specials = ftDe_SpecialS,
    .specialairs = ftDe_SpecialS,
    .specialhi = ftDe_SpecialHi,
    .specialairhi = ftDe_SpecialHi,
    .speciallw = ftDe_SpecialLw,
    .specialairlw = ftDe_SpecialAirLw,
    .onabsorb = NULL,
    /* The item callbacks take (gobj, bool) like the game's own ftData item tables. */
    .onitempickup = (MuAkEvent) ftDe_OnItemPickup,
    .onmakeiteminvisible = ftDe_OnItemVisibility,
    .onmakeitemvisible = ftDe_OnItemVisibility,
    .onitemdrop = (MuAkEvent) ftDe_OnItemRelease,
    .onitemcatch = (MuAkEvent) ftDe_OnItemPickup,
    .onunknownitemrelated = (MuAkEvent) ftDe_OnItemRelease,
    .onhit = ftDe_EyeTextureDamaged,
    .onunknowneyetexturerelated = ftDe_EyeTextureNormal,
    .onframe = ftDe_OnFrame,
    .onactionstatechange = ftDe_OnActionStateChange,
    .onrespawn = ftDe_ResetAttributes,
    .enterdoublejump = ftDe_EnterDoubleJump,

    .move_logic = ftDe_MotionStateTable,
    .move_logic_count = FTDE_MS_COUNT,

    /* One table per article, by pointer: article 0 is a model only and has no code. */
    .article_tables = ftDe_ArticleLogic,
    .article_count = ftDe_Article_TableCount,

    .kirby = &ftKbDe_Copy,
};
