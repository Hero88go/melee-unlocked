/* ACE's Metal Sonic: load, death, respawn, the CPU helper, the result screen voice, the common
 * rows he rewrites and the state table (m-ex "ftFunction" of PlNm.dat).
 *
 * PlNm.dat exports 10 ftFunction slots: 0 onload, 1 ondeath, 3 move_logic, 4 and 5 the neutral
 * entries, 8 to 11 the up and down entries, 25 onrespawn. Every other slot MxDt sets is Fox's
 * function (the side special entries ftFx_SpecialSStart_Enter / ftFx_SpecialAirSStart_Enter,
 * the item events, the knockback eye textures, the common double jump), which the registry
 * fills from Fox's rows because the fields are left NULL here
 * (run-source/rel09-ace-native/metalsonic/mxdt.txt). */
#include "metalsonic.h"

#include <dolphin/os.h>

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftFox/ftfox.h>
#include <melee/ft/kinds/ftFox/ftfoxappeals.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialhi.h>
#include <melee/ft/kinds/ftFox/ftfoxspeciallw.h>
#include <melee/ft/kinds/ftFox/ftfoxspecials.h>
#include <melee/ft/types.h>
#include <melee/gm/gmresultplayer.static.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

_Static_assert(sizeof(ftNm_FighterVars) <= sizeof(((Fighter*) 0)->u),
               "his fighter variables fit the fighter union");

/* ---- CPU players ---------------------------------------------------------------------------
 * The file carries m-ex's "MexCPU" helper without symbols. Its custom data pointer (+0944) is a
 * word of the file that is 0 and that nothing writes (every reference is a read: +00C8, +0990,
 * +5100, +55F8, +5664), so the custom proc (+50A8) is never installed and the custom branch of
 * the step (+55FC to +5678) is never taken: neither is carried over. The spoof word (+09E4) only
 * ever holds 1, stored by [onload] right before it installs the proc.
 *
 * 1 is read as a retail fighter kind, as the Sonic BM, B. Falcon, Blastoise, Giga Bowser and Zero
 * ports read theirs: Ft_Kind_Fox. The question of which reading m-ex means is open for every port
 * (NOTES.md, "Unproven"); for this file both readings give Fox (m-ex internal 1 is Fox too). */
_Static_assert(FTNM_CPU_SPOOF_KIND == 1, "the kind PlNm.dat stores at +09E4");

/* +5260 (proc, priority 2): Fighter_procCpu with fp->kind swapped for the step. The step at
 * +55D0 is ftCo_800B33B0, ftCo_800B2AFC, ftCo_800B2790, ftCo_800B3E04 and the counter at
 * fp+1B04, which is mu_ak_cpu_process with no custom code. */
static void ftNm_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved = fp->kind;
    fp->kind = FTNM_CPU_SPOOF_KIND;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = saved;
}

/* +0948 (proc, priority 2): swap the fighter's Fighter_procCpu proc (0x8006ABA0) for the posing
 * one. It stays installed and finds nothing to swap on later frames, as on the console. */
static void ftNm_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            /* +0990: the custom data word is 0, so +5260 (never +50A8) */
            HSD_GObj_SetupProc(gobj, ftNm_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- the result screen voice ------------------------------------------------------------------ */

/* +52EC: true when some other present player placed below `slot` and is the fighter named `name`
 * ("Sonic", the string at +52E4). The byte compared is the result screen's per-player byte at
 * 0x8046E431 + i * 0xA8, match_end.player_standings[i] + 5; the name is m-ex's name table entry of
 * the player's character (m-ex service at 0x803D7094 with 0xC), here mu_ak_fighter_name. The
 * same test as the Akaneia Sonic's (with "Tails") and Knuckles' ports; written again here (their
 * copies are static). */
static bool ftNm_DidHeLose(int slot, const char* name)
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
        if (my_rank >= lbl_8046E3AC.match_end.player_standings[i].is_big_loser) {
            continue;
        }
        other = mu_ak_fighter_name(Player_GetPlayerCharacter(i));
        /* The console's strcmp reads the table entry without a check. */
        if (other != NULL && strcmp(name, other) == 0) {
            return true;
        }
    }
    return false;
}

/* +09E8 (proc, priority 9, result screen and demo fighters only): in demo motions 0, 2 and 5
 * (the mask 0x25), a voice id the pose's script put in cmd_vars[0] is played, or the alternate in
 * cmd_vars[1] when a Sonic placed below him; then cmd_vars[0] is cleared. */
static void ftNm_CheckWinAudio(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 msid = (u32) fp->motion_id;

    if (msid > 5) {
        return;
    }
    if (((0x25u >> msid) & 1) == 0) {
        return;
    }
    if (fp->cmd_vars[0] == 0) {
        return;
    }
    if (ftNm_DidHeLose(fp->player_idx, "Sonic")) {
        ft_800881D8(fp, fp->cmd_vars[1], 0x7F, 0x40);
    } else {
        ft_800881D8(fp, fp->cmd_vars[0], 0x7F, 0x40);
    }
    fp->cmd_vars[0] = 0;
}

/* ---- the common state rows he rewrites -------------------------------------------------------- */

/* +3530: the back air's physics (common row 67, AttackAirB). Fall with 0.17 and 2.4; at frames 11
 * to 12 (both inclusive, ordered) the vertical speed is set to 0.1 and the horizontal one to his
 * facing times 0.6 (a double constant, the product rounded to single), in that order. */
static void ftNm_AttackAirB_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, 0.17f, 2.4f);
    if (!(fp->cur_anim_frame >= 11.0f)) {
        return;
    }
    if (!(fp->cur_anim_frame <= 12.0f)) {
        return;
    }
    fp->self_vel.y = 0.1f;
    fp->self_vel.x = (float) ((double) fp->facing_dir * 0.6);
}

/* +35C0: the physics of the down smash (common row 64, AttackLw4) and of the back throw (row
 * 220, ThrowB). The animation's own movement (ft_80085134); from frame 44 on (ordered) the ground
 * speed is his facing times 0.0 (a signed zero). */
static void ftNm_AnimMove_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80085134(gobj);
    if (!(fp->cur_anim_frame >= 44.0f)) {
        return;
    }
    fp->gr_vel = fp->facing_dir * 0.0f;
}

/* +0148, from [onload]: when the fighter's common table is the retail one (ftData_MotionStateList,
 * 0x803C2800), the file copies it (0x2AA0 bytes, the 341 rows) into a block inside itself
 * (+0A90 to +3530), writes three physics callbacks and puts the copy in fp->x1C. One block per
 * file: every Metal Sonic shares it and each load copies it again (as here: one static table).
 * Natively the rows are MotionState records (pointers are 8 bytes); the whole table is copied. */
static MotionState ftNm_CommonStates[ftCo_MS_Count];

static void ftNm_Init_CopyMotionStates(Fighter* fp)
{
    memcpy(ftNm_CommonStates, fp->x1C_actionStateList, sizeof(ftNm_CommonStates));
    fp->x1C_actionStateList = ftNm_CommonStates;
    ftNm_CommonStates[ftCo_MS_AttackAirB].phys_cb = ftNm_AttackAirB_Phys; /* +0x874 */
    ftNm_CommonStates[ftCo_MS_ThrowB].phys_cb = ftNm_AnimMove_Phys;       /* +0x1B94 */
    ftNm_CommonStates[ftCo_MS_AttackLw4].phys_cb = ftNm_AnimMove_Phys;    /* +0x814 */
}

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftNm_ArticleSlot;

/* +0000 (slot 0, onload). In the file's order:
 *   model parts 0 and 1 set to 0 (ftParts_80074A4C);
 *   0xD4 bytes of ftData->ext_attr copied to fp+2D8;
 *   Fox's load (ftFx_Init_OnLoad, 0x800E57AC, called through a register): the wall jump bit,
 *     the same copy again, and the first three articles of his list handed to Fox's retail item
 *     kinds (laser, blaster, illusion);
 *   fp+2D4 = fp+2D8; model parts 0 and 1 set to 0 again;
 *   the common table copy (+0148) when fp->x1C is the retail table;
 *   article 0 handed to the item code by index (MEX_IndexFighterItem, 0x803D7058);
 *   a result screen or demo fighter (fp->x1C is ftData_803C52A0): the voice proc +09E8 at
 *     priority 9 and cmd_vars[0], [1] cleared;
 *   with the custom CPU word +0944 zero (always): the spoof word +09E4 = 1 and the proc +0948 at
 *     priority 2.
 *
 * Mod behaviour kept: Fox's load gives his first three articles to Fox's item kinds. Two kinds
 * have one article each in the item code, so in a match with Fox, whichever loaded last owns
 * Fox's laser, blaster and illusion data (the same on the console). */
static void ftNm_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNm_ArticleSlot* items;

    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), FTNM_ATTRS_SIZE);
    ftFx_Init_OnLoad(gobj);
    fp->dat_attrs = fp->dat_attrs_backup;
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);

    if (fp->x1C_actionStateList == ftData_MotionStateList) {
        ftNm_Init_CopyMotionStates(fp);
    }

    items = (ftNm_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[ftNm_Article_Shot]), ftNm_Article_Shot);

    if (fp->x1C_actionStateList == ftData_803C52A0) {
        HSD_GObj_SetupProc(gobj, ftNm_CheckWinAudio, 9);
        fp->cmd_vars[0] = 0;
        fp->cmd_vars[1] = 0;
    }

    /* +00C8: *(+0944) == 0, always: +00F0 stores 1 at +09E4 and installs the proc. */
    HSD_GObj_SetupProc(gobj, ftNm_Cpu_InitProc, 2);

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Metal Sonic load: article %d, common rows %s, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, ftNm_Article_Shot),
                     fp->x1C_actionStateList == ftNm_CommonStates ? "copied" : "not copied",
                     (int) fp->can_walljump);
        }
    }
}

/* +0184 (slot 1, ondeath): model part 0 set to 0, fp+2234 cleared, then ftParts_80074A74 for
 * parts 0 and 1 (results unused). Unlike Fox's ondeath it leaves u.fx.x222C_blasterGObj. */
static void ftNm_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftParts_80074A4C(gobj, 0, 0);
    ftNm_FV(fp)->x2234 = 0;
    ftParts_80074A74(gobj, 0);
    ftParts_80074A74(gobj, 1);
}

/* +08E4 (slot 25, onrespawn, the attribute reload slot): model parts 0 and 1 set to 0, then the
 * 0xD4 bytes of ftData->ext_attr copied to fp+2D8 again (fp+2D4 points there since the load). */
static void ftNm_Init_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), FTNM_ATTRS_SIZE);
}

/* ---- move_logic -------------------------------------------------------------------------------- */

/* +01E0 (slot 3, move_logic): 35 rows numbered from 341 (0x460 bytes, to +0640). Every word is
 * the file's (tables.txt). Fox's callbacks are named by the address in the row; every row has
 * ftCamera_UpdateCameraBox as its camera callback. */
#define FTNM_ROW(anim, flags, move, a, i, p, c)                                                   \
    {                                                                                             \
        (anim), (flags), { (move) }, (a), (i), (p), (c), ftCamera_UpdateCameraBox                 \
    }

const MotionState ftNm_Init_MotionStateTable[FTNM_MS_COUNT] = {
    /* 341 SpecialNStart */
    FTNM_ROW(295, 0x00340111, 0x12000000, ftNm_SpecialNStart_Anim, ftNm_SpecialNStart_IASA,
             ftNm_SpecialNStart_Phys, ftNm_SpecialN_Coll),
    /* 342 SpecialNLoop: no interrupt */
    FTNM_ROW(296, 0x003C0111, 0x12000000, ftNm_SpecialNLoop_Anim, NULL, ftNm_SpecialN_Phys,
             ftNm_SpecialN_Coll),
    /* 343 SpecialNEnd */
    FTNM_ROW(297, 0x00340111, 0x12000000, ftNm_SpecialNEnd_Anim, NULL, ftNm_SpecialN_Phys,
             ftNm_SpecialN_Coll),
    /* 344 SpecialAirNStart: the interrupt is +3804 again */
    FTNM_ROW(298, 0x00340511, 0x12000000, ftNm_SpecialAirNStart_Anim, ftNm_SpecialNStart_IASA,
             ftNm_SpecialAirNStart_Phys, ftNm_SpecialAirN_Coll),
    /* 345 SpecialAirNLoop */
    FTNM_ROW(299, 0x003C0511, 0x12000000, ftNm_SpecialAirNLoop_Anim, NULL, ftNm_SpecialN_Phys,
             ftNm_SpecialAirNLoop_Coll),
    /* 346 SpecialAirNEnd */
    FTNM_ROW(300, 0x00340511, 0x12000000, ftNm_SpecialAirNEnd_Anim, NULL, ftNm_SpecialN_Phys,
             ftNm_SpecialAirN_Coll),
    /* 347 SpecialSStart */
    FTNM_ROW(301, 0x00340212, 0x13000000, ftNm_SpecialSStart_Anim, ftNm_SpecialSStart_IASA,
             ftNm_SpecialSStart_Phys, ftNm_SpecialSStart_Coll),
    /* 348 SpecialS: no physics callback; Fox's ftFx_SpecialS_Coll (0x800EA5EC) */
    FTNM_ROW(302, 0x00340212, 0x13000000, ftNm_SpecialS_Anim, ftNm_SpecialS_IASA, NULL,
             ftFx_SpecialS_Coll),
    /* 349 SpecialSEnd: Fox's four (0x800EA908, 0x800EA9A4, 0x800EA9AC, 0x800EAB90) */
    FTNM_ROW(303, 0x00340212, 0x13000000, ftFx_SpecialSEnd_Anim, ftFx_SpecialSEnd_IASA,
             ftFx_SpecialSEnd_Phys, ftFx_SpecialSEnd_Coll),
    /* 350 SpecialAirSStart */
    FTNM_ROW(304, 0x00340612, 0x13000000, ftNm_SpecialAirSStart_Anim, ftNm_SpecialAirSStart_IASA,
             ftNm_SpecialAirSStart_Phys, ftNm_SpecialAirSStart_Coll),
    /* 351 SpecialAirS: Fox's ftFx_SpecialAirS_Coll (0x800EA628) */
    FTNM_ROW(305, 0x00340612, 0x13000000, ftNm_SpecialAirS_Anim, ftNm_SpecialAirS_IASA,
             ftNm_SpecialAirS_Phys, ftFx_SpecialAirS_Coll),
    /* 352 SpecialAirSEnd: Fox's four (0x800EA944, 0x800EA9A8, 0x800EAA8C, 0x800EABCC) */
    FTNM_ROW(306, 0x00340612, 0x13000000, ftFx_SpecialAirSEnd_Anim, ftFx_SpecialAirSEnd_IASA,
             ftFx_SpecialAirSEnd_Phys, ftFx_SpecialAirSEnd_Coll),
    /* 353 SpecialHiHold: Fox's four (0x800E7354, 0x800E7414, 0x800E741C, 0x800E74A8) */
    FTNM_ROW(307, 0x00340213, 0x14000000, ftFx_SpecialHiHold_Anim, ftFx_SpecialHiHold_IASA,
             ftFx_SpecialHiHold_Phys, ftFx_SpecialHiHold_Coll),
    /* 354 SpecialHiHoldAir: Fox's four (0x800E73B4, 0x800E7418, 0x800E743C, 0x800E74E4) */
    FTNM_ROW(308, 0x00340613, 0x14000000, ftFx_SpecialHiHoldAir_Anim, ftFx_SpecialHiHoldAir_IASA,
             ftFx_SpecialHiHoldAir_Phys, ftFx_SpecialHiHoldAir_Coll),
    /* 355 SpecialHi: his animation; Fox's 0x800E76D4, 0x800E76DC, 0x800E7800 */
    FTNM_ROW(309, 0x00340213, 0x14000000, ftNm_SpecialHi_Anim, ftFx_SpecialHi_IASA,
             ftFx_SpecialHi_Phys, ftFx_SpecialHi_Coll),
    /* 356 SpecialAirHi: Fox's four (0x800E7684, 0x800E76D8, 0x800E7758, 0x800E78B4) */
    FTNM_ROW(309, 0x00340613, 0x14000000, ftFx_SpecialAirHi_Anim, ftFx_SpecialAirHi_IASA,
             ftFx_SpecialAirHi_Phys, ftFx_SpecialAirHi_Coll),
    /* 357 SpecialHiLanding: Fox's four (0x800E7E3C, 0x800E7ED8, 0x800E7EE0, 0x800E7F40) */
    FTNM_ROW(310, 0x00340213, 0x14000000, ftFx_SpecialHiLanding_Anim, ftFx_SpecialHiLanding_IASA,
             ftFx_SpecialHiLanding_Phys, ftFx_SpecialHiLanding_Coll),
    /* 358 SpecialHiFall: Fox's four (0x800E7E78, 0x800E7EDC, 0x800E7F20, 0x800E7FA0) */
    FTNM_ROW(311, 0x00340613, 0x14000000, ftFx_SpecialHiFall_Anim, ftFx_SpecialHiFall_IASA,
             ftFx_SpecialHiFall_Phys, ftFx_SpecialHiFall_Coll),
    /* 359 SpecialHiBound: Fox's four (0x800E8124, 0x800E81FC, 0x800E8200, 0x800E824C) */
    FTNM_ROW(312, 0x00340613, 0x14000000, ftFx_SpecialHiBound_Anim, ftFx_SpecialHiBound_IASA,
             ftFx_SpecialHiBound_Phys, ftFx_SpecialHiBound_Coll),
    /* 360 SpecialLwStart: no interrupt; Fox's ftFx_SpecialLwStart_Coll (0x800E88E4) */
    FTNM_ROW(313, 0x00341014, 0x15000000, ftNm_SpecialLwStart_Anim, NULL, ftNm_SpecialLw_Phys,
             ftFx_SpecialLwStart_Coll),
    /* 361 SpecialLwLoop: Fox's ftFx_SpecialLwLoop_Coll (0x800E8CDC) */
    FTNM_ROW(314, 0x003C1014, 0x15000000, ftNm_SpecialLwLoop_Anim, NULL, ftNm_SpecialLw_Phys,
             ftFx_SpecialLwLoop_Coll),
    /* 362 SpecialLwHit: Fox's ftFx_SpecialLwHit_Coll (0x800E98F4) */
    FTNM_ROW(315, 0x00341014, 0x15000000, ftNm_SpecialLwHit_Anim, NULL, ftNm_SpecialLw_Phys,
             ftFx_SpecialLwHit_Coll),
    /* 363 SpecialLwEnd: Fox's ftFx_SpecialLwEnd_Coll (0x800E9C50) */
    FTNM_ROW(316, 0x00341014, 0x15000000, ftNm_SpecialLwEnd_Anim, NULL, ftNm_SpecialLwEnd_Phys,
             ftFx_SpecialLwEnd_Coll),
    /* 364 SpecialLwTurn: Fox's ftFx_SpecialLwTurn_Coll (0x800E92AC) */
    FTNM_ROW(314, 0x00341014, 0x15000000, ftNm_SpecialLwTurn_Anim, NULL, ftNm_SpecialLw_Phys,
             ftFx_SpecialLwTurn_Coll),
    /* 365 SpecialAirLwStart: Fox's 0x800E8884, 0x800E8920 */
    FTNM_ROW(317, 0x00341414, 0x15000000, ftNm_SpecialAirLwStart_Anim, NULL,
             ftFx_SpecialAirLwStart_Phys, ftFx_SpecialAirLwStart_Coll),
    /* 366 SpecialAirLwLoop: Fox's 0x800E8C68, 0x800E8D18 */
    FTNM_ROW(318, 0x003C1414, 0x15000000, ftNm_SpecialAirLwLoop_Anim, NULL,
             ftFx_SpecialAirLwLoop_Phys, ftFx_SpecialAirLwLoop_Coll),
    /* 367 SpecialAirLwHit: Fox's 0x800E9880, 0x800E9930 */
    FTNM_ROW(319, 0x00341414, 0x15000000, ftNm_SpecialAirLwHit_Anim, NULL,
             ftFx_SpecialAirLwHit_Phys, ftFx_SpecialAirLwHit_Coll),
    /* 368 SpecialAirLwEnd: Fox's 0x800E9BF0, 0x800E9C8C */
    FTNM_ROW(320, 0x00341414, 0x15000000, ftNm_SpecialAirLwEnd_Anim, NULL,
             ftFx_SpecialAirLwEnd_Phys, ftFx_SpecialAirLwEnd_Coll),
    /* 369 SpecialAirLwTurn: Fox's 0x800E9238, 0x800E92E8 */
    FTNM_ROW(318, 0x00341414, 0x15000000, ftNm_SpecialAirLwTurn_Anim, NULL,
             ftFx_SpecialAirLwTurn_Phys, ftFx_SpecialAirLwTurn_Coll),
    /* 370 to 375: Fox's taunt (0x800E5B18, 0x800E5C38, 0x800E5C3C, 0x800E5C5C) */
    FTNM_ROW(321, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
    FTNM_ROW(322, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
    FTNM_ROW(323, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
    FTNM_ROW(324, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
    FTNM_ROW(325, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
    FTNM_ROW(326, 0x00000072, 0x01000000, ftFx_AppealS_Anim, ftFx_AppealS_IASA, ftFx_AppealS_Phys,
             ftFx_AppealS_Coll),
};

/* The 10 slots PlNm.dat exports. Every other field is NULL: the registry takes MxDt's default,
 * Fox's function. Ready: the 0.9 scripted matches pass. */
const MuAkFighter mu_ak_metalsonic = {
    .name = "Metal Sonic",
    .file = "PlNm.dat",

    .onload = ftNm_Init_OnLoad,
    .ondeath = ftNm_Init_OnDeath,
    .specialn = ftNm_SpecialN_Enter,
    .specialairn = ftNm_SpecialAirN_Enter,
    .specialhi = ftNm_SpecialHi_Enter,
    .specialairhi = ftNm_SpecialAirHi_Enter,
    .speciallw = ftNm_SpecialLw_Enter,
    .specialairlw = ftNm_SpecialAirLw_Enter,
    .onrespawn = ftNm_Init_OnRespawn,

    .move_logic = ftNm_Init_MotionStateTable,
    .move_logic_count = FTNM_MS_COUNT,

    .flags = MU_AK_READY,
    .articles = itNm_Articles,
    .article_count = ftNm_Article_Count,
};
