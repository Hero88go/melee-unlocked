/* Akaneia's Sonic: the m-ex ftFunction table, move_logic and the per-fighter callbacks
 * (OnLoad, OnFrame, item visibility, eye textures, the mouth side and the result-screen voice).
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "sonic.h"

#include <string.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/gm/gmresultplayer.static.h>
#include <melee/gm/gmvs.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <melee/lb/lb_013B.h>
#include <melee/lb/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>

MuAkSonicHooks mu_ak_sonic_hooks;

/* ---------------------------------------------------------------------------------------------
 * move_logic
 * ------------------------------------------------------------------------------------------- */

/* The x4 flags word of every Sonic state is ftCo_MF_Special plus Ft_MF_SkipThrowException, with
 * the per-move bits below (console words 0x00340111..0x00340114). */
#define ftSn_MF_Special (ftCo_MF_Special | Ft_MF_SkipThrowException)
#define ftSn_MF_SpecialN (ftSn_MF_Special | Ft_MF_KeepFastFall)
#define ftSn_MF_SpecialS (ftSn_MF_Special | Ft_MF_KeepGfx)
#define ftSn_MF_SpecialHi (ftSn_MF_Special | Ft_MF_KeepFastFall | Ft_MF_KeepGfx)
#define ftSn_MF_SpecialLw (ftSn_MF_Special | Ft_MF_KeepColAnimHitStatus)

#define ftSn_STATE(anim, flags, move, name)                                                    \
    {                                                                                         \
        (anim), (flags), { (move) << 24 }, ftSn_##name##_Anim, ftSn_##name##_IASA,             \
            ftSn_##name##_Phys, ftSn_##name##_Coll, ftCamera_UpdateCameraBox,                  \
    }

static const MotionState ftSn_MotionStateTable[ftSn_MS_SelfCount] = {
    /* 341 */ ftSn_STATE(295, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNStart),
    /* 342 */ ftSn_STATE(296, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNStart),
    /* 343 */ ftSn_STATE(297, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNCharge),
    /* 344 */ ftSn_STATE(298, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNAttackMiss),
    /* 345 */ ftSn_STATE(298, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNAttack),
    /* 346 */ ftSn_STATE(299, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNCancel),
    /* 347 */ ftSn_STATE(302, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNLanding),
    /* 348 */ ftSn_STATE(301, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNRebound),
    /* 349 */ ftSn_STATE(300, ftSn_MF_SpecialN, FtMoveId_SpecialN, SpecialNRebound),
    /* 350 */ ftSn_STATE(303, ftSn_MF_SpecialHi, FtMoveId_SpecialHi, SpecialHi),
    /* 351 */ ftSn_STATE(304, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialSStart),
    /* 352 */ ftSn_STATE(306, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialSEnd),
    /* 353 */ ftSn_STATE(307, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialAirSStart),
    /* 354 */ ftSn_STATE(309, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialAirSEnd),
    /* 355 */ ftSn_STATE(310, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialSHold),
    /* 356 */ ftSn_STATE(311, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialS),
    /* 357 */ ftSn_STATE(313, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialAirS),
    /* 358 */ ftSn_STATE(312, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialS),
    /* 359 */ ftSn_STATE(314, ftSn_MF_SpecialS, FtMoveId_SpecialS, SpecialAirS),
    /* 360 */ ftSn_STATE(304, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwStart),
    /* 361 */ ftSn_STATE(306, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwEnd),
    /* 362 */ ftSn_STATE(307, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialAirLwStart),
    /* 363 */ ftSn_STATE(309, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialAirLwEnd),
    /* 364 */ ftSn_STATE(305, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwCharge),
    /* 365 */ ftSn_STATE(315, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwRun),
    /* 366 */ ftSn_STATE(316, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwRunTurn),
    /* 367 */ ftSn_STATE(317, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwRunJump),
    /* 368 */ ftSn_STATE(318, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwDive),
    /* 369 */ ftSn_STATE(14, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwRunBrake),
    /* 370 */ ftSn_STATE(319, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwStopWall),
    /* 371 */ ftSn_STATE(320, ftSn_MF_SpecialLw, FtMoveId_SpecialLw, SpecialLwStopWall),
};

/* ---------------------------------------------------------------------------------------------
 * Load, death, destroy
 * ------------------------------------------------------------------------------------------- */

static void ftSn_ProcessMouth(HSD_GObj* gobj);
static void ftSn_CheckWinAudio(HSD_GObj* gobj);

/* OnLoad */
void ftSn_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_FighterVars* fv;
    ftSn_DiscSlot* items = (ftSn_DiscSlot*) DP(fp->ft_data->x48_items);

    PUSH_ATTRS(fp, ftSonic_DatAttrs);

    /* Register the spring as this fighter's article 0. */
    if (mu_ak_sonic_hooks.index_fighter_item != NULL) {
        mu_ak_sonic_hooks.index_fighter_item(fp->kind, DP(items[0]), 0);
    }

    /* Sonic draws through his own GX link so the homing attack's debug circles can follow the
     * normal fighter draw. */
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, ftSn_GXLink, 5, 0);

    fv = ftSn_FV(fp);
    fv->spring_gobj = NULL;
    fv->color = NULL;
    fv->specials_charge = 0;
    fv->run_shoes_spawned = 0;

    if (mu_ak_sonic_hooks.costume_archive != NULL) {
        HSD_Archive* archive = mu_ak_sonic_hooks.costume_archive(fp->kind, fp->costume_id);
        if (archive != NULL) {
            fv->color = (ftSonic_ColorData*) HSD_ArchiveGetPublicAddress(archive, "PlySonicColor");
        }
    }

    if (fp->x1C_actionStateList == ftData_803C52A0) {
        /* A result-screen/demo fighter: play the win line the pose's script asks for. */
        HSD_GObj_SetupProc(gobj, ftSn_CheckWinAudio, 9);
        fp->cmd_vars[0] = 0;
        fp->cmd_vars[1] = 0;
    } else {
        HSD_GObj_SetupProc(gobj, ftSn_ProcessMouth, 15);
    }
}

/* OnDeath (m-ex debug name OnRespawn) */
void ftSn_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftSn_FV(fp)->specials_charge = 0;
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 0);
    ftParts_80074A4C(gobj, 3, -1);
}

/* OnDestroy (m-ex slot "onunknown"): take the spring with us. */
void ftSn_Init_OnDestroy(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_FighterVars* fv = ftSn_FV(fp);

    if (fv->spring_gobj != NULL) {
        Item_8026A8EC(fv->spring_gobj);
        fv->spring_gobj = NULL;
    }
}

/* ResetAttributes (m-ex slot "onrespawn") */
void ftSn_Init_ResetAttributes(HSD_GObj* gobj)
{
    COPY_ATTRS(gobj, ftSonic_DatAttrs);
}

/* ---------------------------------------------------------------------------------------------
 * Items held in the hand (the standard behavior with both hand flags set)
 * ------------------------------------------------------------------------------------------- */

void ftSn_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

void ftSn_Init_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* The m-ex code calls the hide function here too (ftAnim_80070CC4, not ftAnim_80070C48).
 * Kept as shipped; see NOTES.md. */
void ftSn_Init_OnItemVisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

void ftSn_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

void ftSn_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item)
{
    ftSn_Init_OnItemPickup(gobj, catch_item);
}

void ftSn_Init_OnUnknownItemRelated(HSD_GObj* gobj, bool drop_item)
{
    ftSn_Init_OnItemDrop(gobj, drop_item);
}

/* ---------------------------------------------------------------------------------------------
 * Eyes
 * ------------------------------------------------------------------------------------------- */

/* onhit: the hurt eyes */
void ftSn_Init_EyeTextureDamaged(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0F);
    ftAnim_800704F0(gobj, 1, 3.0F);
}

/* onunknowneyetexturerelated: back to normal */
void ftSn_Init_EyeTextureNormal(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0F);
    ftAnim_800704F0(gobj, 1, 0.0F);
}

/* ---------------------------------------------------------------------------------------------
 * Every frame
 * ------------------------------------------------------------------------------------------- */

/* OnFrame: a trail behind the dash attack, shoe blur while running. */
void ftSn_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_FighterVars* fv = ftSn_FV(fp);
    EF_Effect* effect;
    HSD_JObj* jobj;

    if (fp->motion_id == ftCo_MS_AttackDash) {
        if (fp->accessory4_cb == NULL) {
            fp->accessory4_cb = ftSn_GFXTrail;
        }
        fv->run_shoes_spawned = 0;
        return;
    }
    if (fp->motion_id != ftCo_MS_Run) {
        fv->run_shoes_spawned = 0;
        return;
    }
    if (fv->run_shoes_spawned) {
        return;
    }

    effect = efSync_Spawn(ftSn_Ef_RunShoes, gobj, fp->parts[FtPart_TransN].joint, NULL);
    if (effect == NULL) {
        return;
    }
    effect->update = ftSn_RunEffectCallback;
    effect->user_data = gobj;
    ftSn_RunEffectCallback(effect);
    fp->x21EC = efLib_DestroyAll;

    /* The m-ex code compares the effect jobj against the address of an empty function, which
     * is never equal: the shoes are always recolored. */
    jobj = effect->gobj->hsd_obj;
    if (jobj != NULL) {
        ftSonic_ColorData* color = fv->color;
        ftSn_ColorShoes(jobj, 2, color);
        ftSn_ColorShoes(jobj, 3, color);
        ftSn_ColorShoes(jobj, 7, color);
        ftSn_ColorShoes(jobj, 9, color);
    }
    fv->run_shoes_spawned = 1;
}

/* OnActionStateChange: a stored full spin dash charge keeps its color overlay. */
void ftSn_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSn_FV(fp)->specials_charge >= ftSn_DA(fp)->specials_max_charge) {
        ftSn_DiscSlot* items = (ftSn_DiscSlot*) DP(fp->ft_data->x48_items);
        lb_800144C8(&fp->x488, (struct Fighter_804D653C_t*) DP(items[1]), 0, 0);
    }
}

/* EnterDoubleJump */
void ftSn_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* OnSmashHi: the up smash always starts from frame 0 and leaves a trail. */
void ftSn_Init_OnSmashHi(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->allow_interrupt = false;
    Fighter_ChangeMotionState(gobj, ftCo_MS_AttackHi4, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftSn_GFXTrail;
}

/* ---------------------------------------------------------------------------------------------
 * Processes
 * ------------------------------------------------------------------------------------------- */

/* Sonic's mouth is one of two model groups (2 and 3), shown on the side facing the camera.
 * Group 0 on model 1 hides both. The worn items in fp->x197C / fp->x1980 (bunny hood etc.) get
 * xDAA_flag.b7 = "group 0 is on model 0". Runs every frame at proc priority 15. */
static void ftSn_ProcessMouth(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x197C != NULL) {
        Item* hat = GET_ITEM(fp->x197C);
        hat->xDAA_flag.b7 = fp->x5F4_arr[0].idx == 0;
    }
    if (fp->x1980 != NULL) {
        Item* hat = GET_ITEM(fp->x1980);
        hat->xDAA_flag.b7 = fp->x5F4_arr[0].idx == 0;
    }

    if (fp->x5F4_arr[0].idx == 1) {
        fp->x5F4_arr[2].idx = -1;
        fp->x5F4_arr[3].idx = -1;
        return;
    }

    if (fp->facing_dir == 1.0F) {
        /* facing right: move the mouth from group 2 to group 3 */
        if (fp->x5F4_arr[2].idx != -1) {
            fp->x5F4_arr[3].idx = fp->x5F4_arr[2].idx;
        } else if (fp->x5F4_arr[3].idx == -1) {
            fp->x5F4_arr[3].idx = fp->x5F4_arr[2].prev;
        }
        fp->x5F4_arr[2].idx = -1;
    } else {
        /* facing left: group 2 shows it */
        if (fp->x5F4_arr[2].idx == -1) {
            fp->x5F4_arr[2].idx = fp->x5F4_arr[2].prev;
        }
        fp->x5F4_arr[3].idx = -1;
    }
}

/* True when some other present player placed below `slot` and is the fighter named `name`.
 * The result screen's per-player byte at match_end.player_standings[i] + 5 is compared as a
 * rank (larger = placed lower). */
static bool ftSn_DidHeLose(int slot, const char* name)
{
    u8 my_rank = lbl_8046E3AC.match_end.player_standings[slot].is_big_loser;
    int i;

    for (i = 0; i < 6; i++) {
        const char* other;

        if (i == slot) {
            continue;
        }
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        if (lbl_8046E3AC.match_end.player_standings[i].is_big_loser <= my_rank) {
            continue;
        }
        if (mu_ak_sonic_hooks.fighter_name == NULL) {
            continue;
        }
        other = mu_ak_sonic_hooks.fighter_name(Player_GetPlayerCharacter(i));
        if (other != NULL && strcmp(other, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Result screen: the win poses (demo motions 0, 2 and 5) put a voice id in cmd_vars[0] and an
 * alternate in cmd_vars[1]; the alternate plays when Tails placed below Sonic. Proc priority 9. */
static void ftSn_CheckWinAudio(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int msid = fp->motion_id;

    if ((msid & ~2) != 0 && msid != 5) {
        return;
    }
    if (fp->cmd_vars[0] == 0) {
        return;
    }
    if (ftSn_DidHeLose(fp->player_idx, "Tails")) {
        ft_800881D8(fp, fp->cmd_vars[1], 0x7F, 0x40);
    } else {
        ft_800881D8(fp, fp->cmd_vars[0], 0x7F, 0x40);
    }
    fp->cmd_vars[0] = 0;
}

/* Fighter_CheckSameTeam: in a team match, is player `slot` on `team`? */
bool ftSn_CheckSameTeam(int team, int slot)
{
    if (!gm_8016B168()) {
        return false;
    }
    return Player_GetTeam(slot) == team;
}

/* GXLink_Sonic: the normal fighter draw, then (with the fighter debug display on) the homing
 * attack's search radius while charging and its target while dashing. */
void ftSn_GXLink(HSD_GObj* gobj, intptr_t pass)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDrawCommon_80080E18(gobj, pass);
    if (pass != 2 || !fp->x21FC_flag.b6) {
        return;
    }
    if (fp->motion_id == ftSn_MS_SpecialNCharge) {
        ftSn_GXLink_DrawRadius(gobj);
    }
    if (fp->motion_id == ftSn_MS_SpecialNAttack) {
        ftSn_GXLink_DrawTarget(gobj);
    }
}

/* ---------------------------------------------------------------------------------------------
 * The fighter
 * ------------------------------------------------------------------------------------------- */

const MuAkFighter mu_ak_sonic = {
    .name = "Sonic",
    .file = "PlSn.dat",

    .onload = ftSn_Init_OnLoad,
    .ondeath = ftSn_Init_OnDeath,
    .onunknown = ftSn_Init_OnDestroy,
    .specialn = ftSn_SpecialNStart_Enter,
    .specialairn = ftSn_SpecialNStart_Enter,
    .specials = ftSn_SpecialS_EnterAirOrGround,
    .specialairs = ftSn_SpecialS_EnterAirOrGround,
    .specialhi = ftSn_SpecialHi_Enter,
    .specialairhi = ftSn_SpecialHi_Enter,
    .speciallw = ftSn_SpecialLw_EnterAirOrGround,
    .specialairlw = ftSn_SpecialLw_EnterAirOrGround,
    .onabsorb = NULL,
    /* The item events take (gobj, bool) like the decomp's Fighter_ItemEvent tables. */
    .onitempickup = (MuAkEvent) ftSn_Init_OnItemPickup,
    .onmakeiteminvisible = ftSn_Init_OnItemInvisible,
    .onmakeitemvisible = ftSn_Init_OnItemVisible,
    .onitemdrop = (MuAkEvent) ftSn_Init_OnItemDrop,
    .onitemcatch = (MuAkEvent) ftSn_Init_OnItemCatch,
    .onunknownitemrelated = (MuAkEvent) ftSn_Init_OnUnknownItemRelated,
    .onhit = ftSn_Init_EyeTextureDamaged,
    .onunknowneyetexturerelated = ftSn_Init_EyeTextureNormal,
    .onframe = ftSn_Init_OnFrame,
    .onactionstatechange = ftSn_Init_OnActionStateChange,
    .onrespawn = ftSn_Init_ResetAttributes,
    .enterdoublejump = ftSn_Init_EnterDoubleJump,
    .onsmashhi = ftSn_Init_OnSmashHi,

    .move_logic = ftSn_MotionStateTable,
    .move_logic_count = ftSn_MS_SelfCount,

    .articles = &ftSn_Spring_LogicTable,
    .article_count = 1,

    /* The ability Kirby copies has no articles (PlKbCpSn.dat has no itFunction). */
    .kirby = &ftKbSn_Copy,
};
