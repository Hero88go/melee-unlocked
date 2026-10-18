/* ACE's Fay: load, the HUD object, the CPU spoof, the events the file exports, the common
 * aerial callbacks onframe installs, and the state table (m-ex "ftFunction" of PlFy.dat).
 *
 * PlFy.dat exports 13 ftFunction slots: 0 onload, 3 move_logic, 4 to 11 the special entries,
 * 23 onframe, 24 onactionstatechange, 34 onlanding. Every other slot MxDt sets is Fox's
 * function (ondeath, the item and knockback events, the attribute reload of slot 25, the
 * common double jump), which the registry fills from Fox's rows because the fields are left
 * NULL here (run-source/rel09-ace-native/fay/mxdt.txt). */
#include "fay.h"

#include <dolphin/os.h>

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialhi.h>
#include <melee/ft/kinds/ftFox/ftfoxspeciallw.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialn.h>
#include <melee/ft/types.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmtoulib.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/object.h>

_Static_assert(sizeof(ftFy_FighterVars) <= sizeof(((Fighter*) 0)->u),
               "her fighter variables fit the fighter union");
_Static_assert(offsetof(ftFy_FighterVars, hi_angle) == 0x22DC - 0x222C, "console offset");
_Static_assert(offsetof(ftFy_FighterVars, s_lock) == 0x22EC - 0x222C, "console offset");
_Static_assert(sizeof(itFy_ItemVars) <= sizeof(((Item*) 0)->xDD4_itemVar),
               "her item variables fit the item union");

/* ---- CPU players ---------------------------------------------------------------------------
 * The file carries m-ex's "MexCPU" helper without symbols. Its custom data pointer (+0C78) is a
 * word of the file that is 0 and that nothing writes (every reference is a read: +00C0, +0D3C,
 * +37D4, +4688, +46F4), so the custom proc (+377C) is never installed and the custom branch of
 * the step (+4694 to +4708) is never taken: neither is carried over. The spoof word (+0D88)
 * only ever holds 1, stored by [onload] right before it installs the proc.
 *
 * 1 is read as a retail fighter kind, as the Sonic BM, B. Falcon, Blastoise, Giga Bowser,
 * Dr. Luigi and Metal Mario ports read theirs: Ft_Kind_Fox. Unproven (NOTES.md): if m-ex
 * stores an m-ex internal id there instead, internal 1 is also Fox (m-ex internal ids 0 to 26
 * are the retail kinds; mxdt.txt shows her Fox rows equal to internal 1's), so for her the two
 * readings give the same fighter. */
_Static_assert(FTFY_CPU_SPOOF_KIND == 1, "the kind PlFy.dat stores at +0D88");

/* +3934 (proc, priority 2): Fighter_procCpu with fp->kind swapped for the step. The step at
 * +4660 is ftCo_800B33B0, ftCo_800B2AFC, ftCo_800B2790, ftCo_800B3E04 and the counter at
 * fp+1B04, which is mu_ak_cpu_process with no custom code. */
static void ftFy_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved = fp->kind;
    fp->kind = FTFY_CPU_SPOOF_KIND;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = saved;
}

/* +0CEC (proc, priority 2): swap the fighter's Fighter_procCpu proc for the posing one. It stays
 * installed and finds nothing to swap on later frames, as on the console. */
static void ftFy_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            /* +0D3C: *(+0C78) is 0, always: the posing proc, not the custom one */
            HSD_GObj_SetupProc(gobj, ftFy_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- the HUD object ------------------------------------------------------------------------- */

/* The object's user data. The console allocates 8 bytes through the m-ex service at 0x803D706C
 * (taken as HSD_MemAlloc, as the Toad and Sonic BM ports) and frees them with HSD_Free. */
typedef struct ftFy_HudData {
    /* +0 */ HSD_GObj* fighter;
    /* +4 */ s32 player;
} ftFy_HudData;

/* "WpInd_scene_models" of PlFyHUD.dat: a list of model descriptions; the first is used. */
typedef DISC_PTR(DynamicModelDesc) ftFy_HudModelSlot;

/* +3664: the HUD object's per-frame routine. Hidden while the fighter is gone, out of stocks or
 * the game is paused; else at the player's damage display, its animation at the frame of the
 * neutral special's mode (fp+22F8), stopped there.
 * The console compares the pause routine's result with 2 (the bit of flag 1); the decomp's
 * routine returns a bool, so the test is its truth, as in the Toad port. */
static void ftFy_Hud_Proc(HSD_GObj* gobj)
{
    ftFy_HudData* data = gobj->user_data;
    HSD_JObj* jobj = gobj->hsd_obj;
    Fighter* fp;
    Vec3* hud;

    if (data->fighter == NULL || Player_GetStocks(data->player) <= 0 || gm_GetDbPauseFlag(1)) {
        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        return;
    }
    fp = GET_FIGHTER(data->fighter);
    hud = ifAll_GetPlayerHUDPosition(fp->player_idx);
    /* +36E0 to +3704: the three words copied */
    jobj->translate.x = hud->x;
    jobj->translate.y = hud->y;
    jobj->translate.z = hud->z;
    HSD_JObjReqAnimAll(jobj, (float) ftFy_FV(fp)->n_mode);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, JOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
    HSD_JObjAnimAll(jobj);
    HSD_JObjClearFlagsAll(jobj, JOBJ_HIDDEN);
    HSD_JObjSetMtxDirtySub(jobj);
}

/* +0BC0: the HUD object. PlFyHUD.dat is loaded (again for each Fay), the first model of its
 * public symbol "WpInd_scene_models" becomes an object (the retail helper at 0x8019035C: draw
 * link 11, the routine above), and its user data names the fighter and her player slot.
 * The console tests neither the file nor the symbol; here either one missing gives no object
 * and one log line (a native guard). */
static HSD_GObj* ftFy_Hud_Create(HSD_GObj* fighter_gobj)
{
    Fighter* fp = GET_FIGHTER(fighter_gobj);
    HSD_Archive* archive;
    ftFy_HudModelSlot* models;
    DynamicModelDesc* model;
    HSD_GObj* hud;
    ftFy_HudData* data;

    archive = lbArchive_LoadArchive("PlFyHUD.dat");
    models = archive != NULL
                 ? (ftFy_HudModelSlot*) HSD_ArchiveGetPublicAddress(archive, "WpInd_scene_models")
                 : NULL;
    model = models != NULL ? DP(models[0]) : NULL;
    if (model == NULL) {
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Fay HUD: %s; no mode display\n",
                     archive == NULL ? "PlFyHUD.dat was not loaded"
                                     : "WpInd_scene_models was not found");
        }
        return NULL;
    }

    hud = fn_8019035C(false, model, 0, 0, 11, true, ftFy_Hud_Proc, 0.0f);
    data = HSD_MemAlloc(sizeof(ftFy_HudData));
    GObj_InitUserData(hud, 4, HSD_Free, data);
    data->fighter = fighter_gobj;
    data->player = fp->player_idx;
    return hud;
}

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftFy_ArticleSlot;

/* +0000 (slot 0, onload). In the file's order:
 *   the file's last loaded fighter object into the word +0BBC (no reader: not kept);
 *   the attributes: fp+2D8 is set to ftData->ext_attr (the block in PlFy.dat, not a copy) and
 *     fp+2D4 to fp+2D8;
 *   the HUD object (+0BC0) into fp+22FC;
 *   articles 0 to 6 handed to the item code by index (MEX_IndexFighterItem, 0x803D7058);
 *   the CPU setup while the custom data word is 0 (always), with the spoof kind 1;
 *   eight fighter variables cleared (22E4, 22DC, 22E0, 22F4, 22EC, 22F0, 22F8, 22E8).
 *
 * Native differences, neither in the file (as the Sonic BM port):
 *   1. fp->dat_attrs_backup is left as the game allocated it and only fp->dat_attrs points at the
 *      block. On the console fp+2D8 is overwritten with the file's address, and the game frees
 *      fp+2D8 into its attribute pool at fighter destroy: a disc address on the pool's free list
 *      (a mod bug not reproduced). MxDt's slot 25 is Fox's ftFx_Init_LoadSpecialAttrs, which
 *      copies ftData->ext_attr onto fp->dat_attrs: the same block onto itself, here as there.
 *   2. The wall jump bit, as the Toad, Metal Mario and Sonic BM ports: MxDt's byte for
 *      internal 41 is 1 and the file does not set fp->can_walljump. */
static void ftFy_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFy_FighterVars* fv = ftFy_FV(fp);
    ftFy_ArticleSlot* items;
    HSD_GObj* hud;
    int i;

    fp->dat_attrs = DP(fp->ft_data->ext_attr);
    hud = ftFy_Hud_Create(gobj);
    fv->hud = hud;

    items = (ftFy_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    for (i = 0; i < ftFy_Article_Count; i++) {
        mu_ak_register_article(fp->kind, DP(items[i]), i);
    }

    /* +00BC: *(+0C78) == 0, always: +0114 installs the proc and stores 1 at +0D88. */
    HSD_GObj_SetupProc(gobj, ftFy_Cpu_InitProc, 2);

    fv->lw_turn = 0;
    fv->hi_angle = 0.0f;
    fv->x22E0 = 0.0f;
    fv->x22F4 = 0.0f;
    fv->s_lock = 0;
    fv->s2_shot = NULL;
    fv->n_mode = 0;
    fv->hi_frames = 0;

    if (mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind)) > 0) {
        fp->can_walljump = true;
    }

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Fay load: articles %d to %d, hud %s, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, 0),
                     mu_ak_item_kind(fp->kind, ftFy_Article_Count - 1),
                     hud != NULL ? "made" : "NOT made", (int) fp->can_walljump);
        }
    }
}

/* ---- onframe and the common callbacks it installs ----------------------------------------- */

/* +3420: the special fall's physics replaced: the common fall at her gravity and terminal speed,
 * then the normal air drift (not FallSpecial's reduced one). */
static void ftFy_FallSpecial_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Drift(fp);
}

/* +3454: the hitlag callback onframe installs while fp+22EC is 1: clears it. */
static void ftFy_PreHitlag_ClearLock(HSD_GObj* gobj)
{
    ftFy_FV(GET_FIGHTER(gobj))->s_lock = 0;
}

/* +3464 and +34F4: the back air's and the up and down airs' animation callbacks: once (the
 * command variable 2 as the flag) the held article 4 in its state 2 or 1 at her position, then
 * the common fall at the animation's end. */
static void ftFy_AttackAir_AnimBlade(HSD_GObj* gobj, s32 msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[2] != 1) {
        Vec3 pos;

        fp->cmd_vars[2] = 1;
        pos = fp->cur_pos;
        itFy_Blade_Spawn(gobj, &pos, msid, fp->facing_dir);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +3464 */
static void ftFy_AttackAirB_Anim(HSD_GObj* gobj)
{
    ftFy_AttackAir_AnimBlade(gobj, 2);
}

/* +34F4 (and +35D0, which is one branch to +34F4) */
static void ftFy_AttackAirHi_Anim(HSD_GObj* gobj)
{
    ftFy_AttackAir_AnimBlade(gobj, 1);
}

/* +3584: the up air's physics: the common aerial physics (ft_80084DB0), and when the script
 * sets command variable 3, a drop of the vertical speed to -0.5. */
static void ftFy_AttackAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084DB0(gobj);
    if (fp->cmd_vars[3] == 1) {
        fp->cmd_vars[3] = 0;
        fp->self_vel.y = -0.5f;
    }
}

/* +35D4: the down air's physics: as above, but the vertical speed is raised to 1.0 when it is
 * not over 1.0 already (the console's compare also leaves a NaN alone). */
static void ftFy_AttackAirLw_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ft_80084DB0(gobj);
    if (fp->cmd_vars[3] == 1) {
        fp->cmd_vars[3] = 0;
        if (fp->self_vel.y <= 1.0f) {
            fp->self_vel.y = 1.0f;
        }
    }
}

/* +0ADC (slot 23, onframe). In the first two animation frames of these common states, the live
 * callbacks are replaced:
 *   FallSpecial, FallSpecialF, FallSpecialB (0x23 to 0x25)  physics +3420
 *   AttackAirB (0x43)                                       animation +3464
 *   AttackAirHi (0x44)                                      animation +34F4, physics +3584
 *   AttackAirLw (0x45)                                      animation +35D0, physics +35D4
 * Then, every frame, the hitlag callback +3454 while fp+22EC is 1 (never: nothing sets it).
 *
 * The file's jump table (+3394) holds offsets from a table address that is not the one its
 * relocation gives: on the console the jump lands in a second copy of this routine (+45AC,
 * word for word the same, check.txt), at the case of the same number. The behaviour is the
 * switch below. */
_Static_assert(ftCo_MS_FallSpecial == 0x23 && ftCo_MS_FallSpecialB == 0x25,
               "the onframe cases");
_Static_assert(ftCo_MS_AttackAirB == 0x43 && ftCo_MS_AttackAirHi == 0x44 &&
                   ftCo_MS_AttackAirLw == 0x45,
               "the onframe cases");
static void ftFy_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cur_anim_frame < 2.0f) {
        switch (fp->motion_id) {
        case ftCo_MS_FallSpecial:
        case ftCo_MS_FallSpecialF:
        case ftCo_MS_FallSpecialB:
            fp->phys_cb = ftFy_FallSpecial_Phys;
            break;
        case ftCo_MS_AttackAirB:
            fp->anim_cb = ftFy_AttackAirB_Anim;
            break;
        case ftCo_MS_AttackAirHi:
            fp->anim_cb = ftFy_AttackAirHi_Anim;
            fp->phys_cb = ftFy_AttackAirHi_Phys;
            break;
        case ftCo_MS_AttackAirLw:
            fp->anim_cb = ftFy_AttackAirHi_Anim;
            fp->phys_cb = ftFy_AttackAirLw_Phys;
            break;
        default:
            break;
        }
    }
    if (ftFy_FV(fp)->s_lock == 1) {
        fp->pre_hitlag_cb = ftFy_PreHitlag_ClearLock;
    }
}

/* +0B90 (slot 24, onactionstatechange): the four command variables cleared. */
static void ftFy_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
}

/* +0BAC (slot 34, onlanding): fp+22EC cleared. */
static void ftFy_Init_OnLanding(HSD_GObj* gobj)
{
    ftFy_FV(GET_FIGHTER(gobj))->s_lock = 0;
}

/* +32DC: the take damage callback the neutral special entries install: command variable 3 (the
 * held article's flag) cleared. */
void ftFy_TakeDamage_ClearShotFlag(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->cmd_vars[3] = 0;
}

/* ---- the state table ------------------------------------------------------------------------ */

/* +0138 (slot 3, move_logic): 29 rows, numbered from 341. Every word is the file's
 * (tables.txt). The retail callbacks are Fox's, named by the address in the row; they run with
 * Fox's numbers for the states they change to (NOTES.md). Every row has the camera callback. */
const MotionState ftFy_Init_MotionStateTable[FTFY_MS_COUNT] = {
    /* 341 SpecialN: retail ftFx_SpecialNStart_Phys (0x800E6B5C), _Coll (0x800E6C1C) */
    { 295, 0x00340111, 0x12000000, ftFy_SpecialN_Anim, NULL, ftFx_SpecialNStart_Phys,
      ftFx_SpecialNStart_Coll, ftCamera_UpdateCameraBox },
    /* 342 SpecialN2: the same two retail callbacks */
    { 296, 0x003C0111, 0x12000000, ftFy_SpecialN2_Anim, NULL, ftFx_SpecialNStart_Phys,
      ftFx_SpecialNStart_Coll, ftCamera_UpdateCameraBox },
    /* 343 SpecialAirN: retail ftFx_SpecialAirNStart_IASA, _Phys, _Coll (0x800E6B08,
     * 0x800E6BBC, 0x800E6C7C) */
    { 297, 0x00340511, 0x12000000, ftFy_SpecialAirN_Anim, ftFx_SpecialAirNStart_IASA,
      ftFx_SpecialAirNStart_Phys, ftFx_SpecialAirNStart_Coll, ftCamera_UpdateCameraBox },
    /* 344 SpecialAirN2: retail ftFx_SpecialAirNLoop_IASA (0x800E6B30) */
    { 298, 0x003C0511, 0x12000000, ftFy_SpecialAirN2_Anim, ftFx_SpecialAirNLoop_IASA,
      ftFy_SpecialAirN2_Phys, ftFy_SpecialAirN2_Coll, ftCamera_UpdateCameraBox },
    /* 345 SpecialS: retail ftFx_SpecialNStart_Coll (0x800E6C1C) */
    { 299, 0x00340212, 0x13000000, ftFy_SpecialS_Anim, ftFy_SpecialS_IASA, ftFy_SpecialS_Phys,
      ftFx_SpecialNStart_Coll, ftCamera_UpdateCameraBox },
    /* 346 SpecialAirS */
    { 300, 0x00340612, 0x13000000, ftFy_SpecialAirS_Anim, ftFy_SpecialAirS_IASA,
      ftFy_SpecialAirS_Phys, ftFy_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 347 SpecialHi: retail ftFx_SpecialHiHold_Phys, _Coll (0x800E741C, 0x800E74A8) */
    { 301, 0x00340213, 0x14000000, ftFy_SpecialHi_Anim, NULL, ftFx_SpecialHiHold_Phys,
      ftFx_SpecialHiHold_Coll, ftCamera_UpdateCameraBox },
    /* 348 SpecialAirHi: the same animation callback */
    { 302, 0x00340613, 0x14000000, ftFy_SpecialHi_Anim, NULL, ftFy_SpecialAirHi_Phys,
      ftFy_SpecialAirHi_Coll, ftCamera_UpdateCameraBox },
    /* 349 SpecialHiLaunch: interrupt +1F00 is a lone blr */
    { 303, 0x00340213, 0x14000000, ftFy_SpecialHiLaunch_Anim, ftFy_SpecialHi_Nothing,
      ftFy_SpecialHiLaunch_Phys, ftFy_SpecialAirHi_Coll, ftCamera_UpdateCameraBox },
    /* 350 SpecialHiUnused: interrupt +2018 and physics +201C are lone blr words */
    { 303, 0x00340613, 0x14000000, ftFy_SpecialHiUnused_Anim, ftFy_SpecialHi_Nothing,
      ftFy_SpecialHi_Nothing, ftFy_SpecialHiUnused_Coll, ftCamera_UpdateCameraBox },
    /* 351 SpecialHiFall: interrupt +208C is a lone blr */
    { 304, 0x00340613, 0x14000000, ftFy_SpecialHiFall_Anim, ftFy_SpecialHi_Nothing,
      ftFy_SpecialHiFall_Phys, ftFy_SpecialHiFall_Coll, ftCamera_UpdateCameraBox },
    /* 352 SpecialLwStart: retail ftFx_SpecialLwStart_Phys, _Coll (0x800E8864, 0x800E88E4) */
    { 305, 0x00341014, 0x15000000, ftFy_SpecialLwStart_Anim, ftFy_SpecialLwStart_IASA,
      ftFx_SpecialLwStart_Phys, ftFx_SpecialLwStart_Coll, ftCamera_UpdateCameraBox },
    /* 353 SpecialLwTurn: retail ftFx_SpecialLwLoop_Phys, _Coll (0x800E8C34, 0x800E8CDC) */
    { 306, 0x003C1014, 0x15000000, ftFy_SpecialLwTurn_Anim, ftFy_SpecialLw_JumpIASA,
      ftFx_SpecialLwLoop_Phys, ftFx_SpecialLwLoop_Coll, ftCamera_UpdateCameraBox },
    /* 354 SpecialLwHit: retail ftFx_SpecialLwHit_IASA, _Phys, _Coll (0x800E9844, 0x800E984C,
     * 0x800E98F4) */
    { 307, 0x00341014, 0x15000000, ftFy_SpecialLwHit_Anim, ftFx_SpecialLwHit_IASA,
      ftFx_SpecialLwHit_Phys, ftFx_SpecialLwHit_Coll, ftCamera_UpdateCameraBox },
    /* 355 SpecialLwEnd: retail ftFx_SpecialLwEnd_IASA, _Phys, _Coll (0x800E9BC8, 0x800E9BD0,
     * 0x800E9C50) */
    { 308, 0x00341014, 0x15000000, ftFy_SpecialLwEnd_Anim, ftFx_SpecialLwEnd_IASA,
      ftFx_SpecialLwEnd_Phys, ftFx_SpecialLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 356 SpecialLwLoop: retail ftFx_SpecialLwTurn_Phys (0x800E9204) */
    { 306, 0x00341014, 0x15000000, ftFy_SpecialLwLoop_Anim, ftFy_SpecialLw_JumpIASA,
      ftFx_SpecialLwTurn_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 357 SpecialAirLwStart: retail ftFx_SpecialAirLwStart_IASA, _Phys (0x800E87D0,
     * 0x800E8884) */
    { 309, 0x00341414, 0x15000000, ftFy_SpecialAirLwStart_Anim, ftFx_SpecialAirLwStart_IASA,
      ftFx_SpecialAirLwStart_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 358 SpecialAirLwTurn */
    { 310, 0x003C1414, 0x15000000, ftFy_SpecialAirLwTurn_Anim, ftFy_SpecialAirLw_JumpIASA,
      ftFy_SpecialAirLw_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 359 SpecialAirLwHit: retail ftFx_SpecialAirLwHit_IASA (0x800E9848) */
    { 311, 0x00341414, 0x15000000, ftFy_SpecialAirLwHit_Anim, ftFx_SpecialAirLwHit_IASA,
      ftFy_SpecialAirLw_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 360 SpecialAirLwEnd: retail ftFx_SpecialAirLwEnd_IASA (0x800E9BCC) */
    { 312, 0x00341414, 0x15000000, ftFy_SpecialAirLwEnd_Anim, ftFx_SpecialAirLwEnd_IASA,
      ftFy_SpecialAirLw_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 361 SpecialAirLwLoop */
    { 310, 0x00341414, 0x15000000, ftFy_SpecialAirLwLoop_Anim, ftFy_SpecialAirLw_JumpIASA,
      ftFy_SpecialAirLw_Phys, ftFy_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 362 SpecialNSwap: retail ftFx_SpecialNEnd_Phys, _Coll (0x800E6B9C, 0x800E6C5C) */
    { 313, 0x00000072, 0x01000000, ftFy_SpecialNSwap_Anim, NULL, ftFx_SpecialNEnd_Phys,
      ftFx_SpecialNEnd_Coll, ftCamera_UpdateCameraBox },
    /* 363 SpecialAirNSwap: retail ftFx_SpecialAirNStart_Phys (0x800E6BBC) */
    { 314, 0x00000072, 0x01000000, ftFy_SpecialAirNSwap_Anim, NULL, ftFx_SpecialAirNStart_Phys,
      ftFy_SpecialAirN2_Coll, ftCamera_UpdateCameraBox },
    /* 364 SpecialS2: retail ftFx_SpecialNStart_Phys (0x800E6B5C) */
    { 315, 0x00340612, 0x13000000, ftFy_SpecialS2_Anim, NULL, ftFx_SpecialNStart_Phys,
      ftFy_SpecialS2_Coll, ftCamera_UpdateCameraBox },
    /* 365 SpecialS2Hold: the side special's physics */
    { 316, 0x00340612, 0x13000000, ftFy_SpecialS2Hold_Anim, NULL, ftFy_SpecialS_Phys,
      ftFy_SpecialS2_Coll, ftCamera_UpdateCameraBox },
    /* 366 SpecialS2End: retail ftFx_SpecialNStart_Phys (0x800E6B5C) */
    { 317, 0x00340612, 0x13000000, ftFy_SpecialS2End_Anim, NULL, ftFx_SpecialNStart_Phys,
      ftFy_SpecialS2_Coll, ftCamera_UpdateCameraBox },
    /* 367 SpecialAirS2 */
    { 318, 0x00340612, 0x13000000, ftFy_SpecialAirS2_Anim, NULL, ftFy_SpecialAirS2_Phys,
      ftFy_SpecialAirS2_Coll, ftCamera_UpdateCameraBox },
    /* 368 SpecialAirS2Hold */
    { 319, 0x00340612, 0x13000000, ftFy_SpecialAirS2Hold_Anim, NULL, ftFy_SpecialAirS2_Phys,
      ftFy_SpecialAirS2_Coll, ftCamera_UpdateCameraBox },
    /* 369 SpecialAirS2End */
    { 320, 0x00340612, 0x13000000, ftFy_SpecialAirS2End_Anim, NULL, ftFy_SpecialAirS2_Phys,
      ftFy_SpecialAirS2_Coll, ftCamera_UpdateCameraBox },
};

/* The 13 slots PlFy.dat exports. Every other field is NULL: the registry takes MxDt's default,
 * Fox's function. No Kirby hat file. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_fay = {
    .name = "Fay",
    .file = "PlFy.dat",

    .onload = ftFy_Init_OnLoad,
    .specialn = ftFy_SpecialN_Enter,
    .specialairn = ftFy_SpecialAirN_Enter,
    .specials = ftFy_SpecialS_Enter,
    .specialairs = ftFy_SpecialAirS_Enter,
    .specialhi = ftFy_SpecialHi_Enter,
    .specialairhi = ftFy_SpecialAirHi_Enter,
    .speciallw = ftFy_SpecialLw_Enter,
    .specialairlw = ftFy_SpecialAirLw_Enter,
    .onframe = ftFy_Init_OnFrame,
    .onactionstatechange = ftFy_Init_OnActionStateChange,
    .onlanding = ftFy_Init_OnLanding,

    .move_logic = ftFy_Init_MotionStateTable,
    .move_logic_count = FTFY_MS_COUNT,

    .flags = 0,
    .articles = itFy_Articles,
    .article_count = ftFy_Article_Count,
    .kirby = NULL,
};
