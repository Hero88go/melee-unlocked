/* ACE's Blastoise: load, per-fighter events, the state table and the CPU helper (m-ex
 * "ftFunction" of PlBl.dat).
 *
 * PlBl.dat exports 22 ftFunction slots. The one non-empty slot it leaves to MxDt is 23, the frame
 * event, which is Bowser's ftKp_Init_UnkMotionStates3: the registry fills it from Bowser's row
 * because the field is left empty here. */
#include "blastoise.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspecialhi.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspeciallw.h>
#include <melee/ft/kinds/ftKoopa/ftkoopaspecials.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU: m-ex's "MexCPU" helper, linked into every fighter file built with symbols ----------
 * +15A4 ".bss.mexcpu_data" is never written in PlBl.dat (its five relocations are all reads), so
 * +187C "MexCPU_ProcCustom" is never installed and the custom branch of +1D14 "MexCPU_Process"
 * is never taken: neither is carried over. +15A8 ".bss.mexcpu_spoof" only ever holds [OnLoad]'s
 * 0x1F, which is ftBl_CpuSpoofKind here, so the port keeps no state. */

/* +1A3C "MexCPU_ProcSpoof" (proc, priority 2): Fighter_procCpu with fp->kind swapped for the
 * step. The step (+1D14 with no custom table) is ftCo_800B33B0, ftCo_800B2AFC, ftCo_800B2790,
 * ftCo_800B3E04 and the counter at fp+1B04, which is mu_ak_cpu_process with no custom code. */
static void ftBl_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftBl_CpuSpoofKind;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +15AC "MexCPU_InitProc" (proc, priority 2): runs every frame; when it finds the game's CPU proc
 * on the fighter it removes it and puts the posing proc at the same priority. It stays installed
 * and finds nothing on later frames, as on the console. */
static void ftBl_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftBl_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* +0618 "MexCPU_InitSpoofData". The console returns at once when mexcpu_data is set, which it
 * never is. */
static void ftBl_Cpu_InitSpoofData(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftBl_Cpu_InitProc, 2);
}

/* ---- ftFunction ---- */

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftBl_ArticleSlot;

/* +05C4 "ResetAttributes" (slot 25): his attribute block again, 0x58 bytes, into the fighter's
 * own copy (fp+2D8, which [OnLoad] also makes fp+2D4). */
static void ftBl_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_DatAttrs* src = (ftBl_DatAttrs*) DP(fp->ft_data->ext_attr);

    *(ftBl_DatAttrs*) fp->dat_attrs_backup = *src;
}

/* +0000 "OnLoad" (slot 0). The attribute copy, both articles handed to the item code by index
 * (m-ex MEX_IndexFighterItem at 0x803D7058), and the CPU helper. Unlike Bowser's load it does
 * not set fp->x2226_b1. */
static void ftBl_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_ArticleSlot* items;

    ftBl_Init_LoadSpecialAttrs(gobj);
    fp->dat_attrs = fp->dat_attrs_backup;

    items = (ftBl_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[ftBl_Article_HydroPump]),
                           ftBl_Article_HydroPump);
    mu_ak_register_article(fp->kind, DP(items[ftBl_Article_Bubble]),
                           ftBl_Article_Bubble);

    ftBl_Cpu_InitSpoofData(gobj);
}

/* +0080 "OnRespawn" (slot 1, the ondeath slot). Not Bowser's ftKp_Init_OnDeath, which sets his
 * armor and two reserves from his attributes: the first model part override cleared and two
 * words zeroed. */
static void ftBl_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBl_FighterVars* fv = ftBl_Vars(fp);

    ftParts_80074A4C(gobj, 0, 0);
    fv->x2230 = 0;
    fv->x2234 = 0;
}

/* +00C0 "OnDestroy" (slot 2): empty. */
static void ftBl_Init_OnUserDataRemove(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +0304 "OnItemPickup" (slot 13): the common hold-kind hand poses, same as retail
 * ftKp_Init_OnItemPickup. */
static void ftBl_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* +03EC "OnSetItemInvisible" (slot 14), same as retail ftKp_Init_OnItemInvisible. */
static void ftBl_Init_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* +0438 "OnSetItemVisible" (slot 15). Not the retail routine: the file calls ftAnim_80070CC4 here
 * too (the call of the "invisible" event) where Bowser's ftKp_Init_OnItemVisible calls
 * ftAnim_80070C48. Kept as the file has it. */
static void ftBl_Init_OnItemVisible(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* +0484 "OnItemRelease" (slot 16), same as retail ftKp_Init_OnItemDrop. */
static void ftBl_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* +04DC "OnItemCatch" (slot 17): the pickup handler with the caller's flag. */
static void ftBl_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item)
{
    ftBl_Init_OnItemPickup(gobj, catch_item);
}

/* +04FC "OnUnknownItemRelated" (slot 18): the drop handler with the caller's flag. */
static void ftBl_Init_OnItemUnknown(HSD_GObj* gobj, bool drop_item)
{
    ftBl_Init_OnItemDrop(gobj, drop_item);
}

/* +051C "EyeTextureDamaged" (slot 21): retail Fighter_OnKnockbackEnter(gobj, true) with the two
 * calls in the other order (texture 0 first). */
static void ftBl_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0f);
    ftAnim_800704F0(gobj, 1, 3.0f);
}

/* +0570 "EyeTextureNormal" (slot 22): retail Fighter_OnKnockbackExit(gobj, true), texture 0
 * first. */
static void ftBl_Init_OnKnockbackExit(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0f);
    ftAnim_800704F0(gobj, 1, 0.0f);
}

/* +05F8 "EnterDoubleJump" (slot 32): the common double jump. */
static void ftBl_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* +00C4 "move_logic" (slot 3): nine states. The animation ids (295 to 303), flags and move ids
 * are the words of the file; the flags are written with Bowser's names for the same values
 * (00340011, 00340411, 00340012, 00340412, 00340213, 00340613, 00340214, 00340614, 00340214).
 * Callbacks without an offset are retail functions the file names by address. */
const MotionState ftBl_Init_MotionStateTable[ftBl_MS_SelfCount] = {
    {
        // ftBl_MS_SpecialN = 341
        ftBl_SM_SpecialN,
        ftKp_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftBl_SpecialN_Anim, /* +065C */
        NULL,
        ftBl_SpecialN_Phys, /* +069C */
        ftBl_SpecialN_Coll, /* +06BC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialAirN = 342
        ftBl_SM_SpecialAirN,
        ftKp_MF_SpecialNStart,
        FtMoveId_SpecialN << 24,
        ftBl_SpecialAirN_Anim, /* +06FC */
        NULL,
        ftBl_SpecialAirN_Phys, /* +073C */
        ftBl_SpecialAirN_Coll, /* +075C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialS = 343
        ftBl_SM_SpecialS,
        ftKp_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftBl_SpecialS_Anim, /* +07B8 */
        NULL,
        ftBl_SpecialS_Phys, /* +07F8 */
        ftBl_SpecialS_Coll, /* +0878 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialAirS = 344
        ftBl_SM_SpecialAirS,
        ftKp_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftBl_SpecialAirS_Anim, /* +08E8 */
        NULL,
        ftKp_SpecialAirSStart_Phys, /* 0x80134244 */
        ftBl_SpecialAirS_Coll, /* +0928 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialHi = 345
        ftBl_SM_SpecialHi,
        ftKp_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftBl_SpecialHi_Anim, /* +0984 */
        ftBl_SpecialHi_IASA, /* +09C4 */
        ftBl_SpecialHi_Phys, /* +09C8 */
        ftBl_SpecialHi_Coll, /* +0CB0 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialAirHi = 346
        ftBl_SM_SpecialAirHi,
        ftKp_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftBl_SpecialAirHi_Anim, /* +0D20 */
        ftKp_SpecialAirHi_IASA, /* 0x80135D7C */
        ftBl_SpecialAirHi_Phys, /* +0D78 */
        ftBl_SpecialAirHi_Coll, /* +10EC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialLw = 347
        ftBl_SM_SpecialLw,
        ftKp_MF_SpecialLwStart,
        FtMoveId_SpecialLw << 24,
        ftBl_SpecialLw_Anim, /* +1114 */
        ftBl_SpecialLw_IASA, /* +1154 */
        ftBl_SpecialLw_Phys, /* +11EC */
        ftBl_SpecialLw_Coll, /* +120C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialAirLw = 348
        ftBl_SM_SpecialAirLw,
        ftKp_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftBl_SpecialAirLw_Anim, /* +124C */
        ftBl_SpecialAirLw_IASA, /* +128C */
        ftBl_SpecialAirLw_Phys, /* +1324 */
        ftBl_SpecialAirLw_Coll, /* +1344 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBl_MS_SpecialLwLanding = 349: Bowser's row of the same name but for the animation
        ftBl_SM_SpecialLwLanding,
        ftKp_MF_SpecialLwStart,
        FtMoveId_SpecialLw << 24,
        ftKp_SpecialLwLanding_Anim, /* 0x801349C4 */
        NULL,
        ftKp_SpecialLwLanding_Phys, /* 0x80134A00 */
        ftKp_SpecialLwLanding_Coll, /* 0x80134A20 */
        ftCamera_UpdateCameraBox,
    },
};

#define FTBL_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (fn))

/* The 22 slots PlBl.dat exports. onframe (23) is left empty and takes the m-ex default, Bowser's
 * frame event. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_blastoise = {
    .name = "Blastoise",
    .file = "PlBl.dat",

    .onload = ftBl_Init_OnLoad,
    .ondeath = ftBl_Init_OnDeath,
    .onunknown = ftBl_Init_OnUserDataRemove,
    .specialn = ftBl_SpecialN_Enter,
    .specialairn = ftBl_SpecialAirN_Enter,
    .specials = ftBl_SpecialS_Enter,
    .specialairs = ftBl_SpecialAirS_Enter,
    .specialhi = ftBl_SpecialHi_Enter,
    .specialairhi = ftBl_SpecialAirHi_Enter,
    .speciallw = ftBl_SpecialLw_Enter,
    .specialairlw = ftBl_SpecialAirLw_Enter,
    .onitempickup = FTBL_ITEM_EVENT(ftBl_Init_OnItemPickup),
    .onmakeiteminvisible = ftBl_Init_OnItemInvisible,
    .onmakeitemvisible = ftBl_Init_OnItemVisible,
    .onitemdrop = FTBL_ITEM_EVENT(ftBl_Init_OnItemDrop),
    .onitemcatch = FTBL_ITEM_EVENT(ftBl_Init_OnItemCatch),
    .onunknownitemrelated = FTBL_ITEM_EVENT(ftBl_Init_OnItemUnknown),
    .onhit = ftBl_Init_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftBl_Init_OnKnockbackExit,
    .onrespawn = ftBl_Init_LoadSpecialAttrs,
    .enterdoublejump = ftBl_Init_EnterDoubleJump,

    .move_logic = ftBl_Init_MotionStateTable,
    .move_logic_count = ftBl_MS_SelfCount,

    .flags = 0,
    .articles = itBl_Articles,
    .article_count = ftBl_Article_Count,
};
