/* ACE's Sonic BM: load, the events the file exports, the rewritten common states, the spin ball
 * effect and the state table (m-ex "ftFunction" of PlSc.dat).
 *
 * PlSc.dat exports 14 ftFunction slots: 0 onload, 3 move_logic, 4 to 11 the special entries,
 * 23 onframe, 24 onactionstatechange, 25 onrespawn, 34 onlanding. Every other slot MxDt sets is
 * Mario's function (ondeath, the item and knockback events, the common double jump, the two demo
 * hooks and the demo state table), which the registry fills from Mario's rows because the fields
 * are left NULL here (run-source/rel09-ace-native/sonicbm/mxdt.txt). */
#include "sonicbm.h"

#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftCo_800C70D0.h>
#include <melee/ft/ftCo_800C7178.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_AppealS.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackHi4.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackS4.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallAerial.h>
#include <melee/ft/kinds/ftCommon/ftCo_LandingAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_RunBrake.h>
#include <melee/ft/kinds/ftCommon/ftCo_Thrown.h>
#include <melee/ft/types.h>
#include <melee/lb/lbarchive.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

_Static_assert(sizeof(ftSc_FighterVars) <= sizeof(((Fighter*) 0)->u),
               "his fighter variables fit the fighter union");
_Static_assert(sizeof(ftSc_MotionVars) <= sizeof(((Fighter*) 0)->mv),
               "his state variables fit the state union");
_Static_assert(offsetof(ftSc_MotionVars, hit_done) == 0x2370 - 0x2340, "console offsets");

/* ---- CPU players ---------------------------------------------------------------------------
 * The file carries m-ex's "MexCPU" helper without symbols. Its custom data pointer (+0C74) is a
 * word of the file that is 0 and that nothing writes (every reference is a read: +00F4, +0CC0,
 * +3010, +3B6C, +3BD8), so the custom proc (+2FC0) is never installed and the custom branch of
 * the step (+3B88 to +3BF0) is never taken: neither is carried over. The spoof word (+0D14) only
 * ever holds 0x1C, stored by [onload] right before it installs the proc.
 *
 * 0x1C is read as a retail fighter kind, as the B. Falcon, Blastoise, Giga Bowser, Dr. Luigi and
 * Metal Mario ports read theirs: Ft_Kind_CrezyH. Not proven: if m-ex stores an m-ex internal id
 * there, 28 is ACE's Diddy Kong (native kind 0x23). See NOTES.md. */
#define FTSC_CPU_SPOOF_KIND Ft_Kind_CrezyH
_Static_assert(FTSC_CPU_SPOOF_KIND == 0x1C, "the kind PlSc.dat stores at +0D14");

/* +3178 (proc, priority 2): Fighter_procCpu with fp->kind swapped for the step. The step at
 * +3B48 is ftCo_800B33B0, ftCo_800B2AFC, ftCo_800B2790, ftCo_800B3E04 and the counter at
 * fp+1B04, which is mu_ak_cpu_process with no custom code. */
static void ftSc_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved = fp->kind;
    fp->kind = FTSC_CPU_SPOOF_KIND;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = saved;
}

/* +0C78 (proc, priority 2): swap the fighter's Fighter_procCpu proc for the posing one. It stays
 * installed and finds nothing to swap on later frames, as on the console. */
static void ftSc_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftSc_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- the common state rows he rewrites ------------------------------------------------------ */
static void ftSc_Thrown_Anim(HSD_GObj* gobj);
static void ftSc_AttackS4_Anim(HSD_GObj* gobj);
static void ftSc_Fall_IASA(HSD_GObj* gobj);

/* +0BC8, called from [onload]: his own copy of the common state table (0x2AA0 bytes, the 341
 * common rows), with four kinds of rows changed, put in fp->x1C_actionStateList:
 *   ThrownF, ThrownB, ThrownHi, ThrownLw   animation callback +29E8
 *   AttackS4Hi, AttackS4S, AttackS4Lw      animation callback +2D20, interrupt
 *                                          ftCo_AttackHi4_IASA (0x8008CAD4, the up smash's)
 *   Fall to FallAerialB (six rows)         interrupt callback +2F08
 * The console takes the memory from the m-ex allocator (0x803D706C) and never frees it; here
 * HSD_MemAlloc, as Diddy Kong's port does for its demo rows.
 *
 * One native guard: the console copies 0x2AA0 bytes whatever the list is. A result screen
 * fighter's list (ftData_803C52A0) has 14 rows, so the console copies whatever follows them in
 * main.dol and writes rows past them in the copy. Here only fp->x18 rows (the list's own count)
 * are copied and the rest of the copy is zero; the rows the file writes are written. */
static void ftSc_Init_CopyMotionStates(Fighter* fp)
{
    MotionState* table = HSD_MemAlloc(ftCo_MS_Count * sizeof(MotionState));
    int rows = fp->x18;
    int i;

    if (rows < 0 || rows > ftCo_MS_Count) {
        rows = ftCo_MS_Count;
    }
    memset(table, 0, ftCo_MS_Count * sizeof(MotionState));
    memcpy(table, fp->x1C_actionStateList, rows * sizeof(MotionState));

    table[ftCo_MS_ThrownF].anim_cb = ftSc_Thrown_Anim;
    table[ftCo_MS_ThrownB].anim_cb = ftSc_Thrown_Anim;
    table[ftCo_MS_ThrownHi].anim_cb = ftSc_Thrown_Anim;
    table[ftCo_MS_ThrownLw].anim_cb = ftSc_Thrown_Anim;
    table[ftCo_MS_AttackS4Hi].anim_cb = ftSc_AttackS4_Anim;
    table[ftCo_MS_AttackS4S].anim_cb = ftSc_AttackS4_Anim;
    table[ftCo_MS_AttackS4Lw].anim_cb = ftSc_AttackS4_Anim;
    table[ftCo_MS_AttackS4Hi].input_cb = ftCo_AttackHi4_IASA;
    table[ftCo_MS_AttackS4S].input_cb = ftCo_AttackHi4_IASA;
    table[ftCo_MS_AttackS4Lw].input_cb = ftCo_AttackHi4_IASA;
    for (i = ftCo_MS_Fall; i <= ftCo_MS_FallAerialB; i++) {
        table[i].input_cb = ftSc_Fall_IASA;
    }
    fp->x1C_actionStateList = table;
}

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftSc_ArticleSlot;

/* +0000 (slot 0, onload). In the file's order:
 *   the attributes: fp+2D8 is set to ftData->ext_attr (the block in PlSc.dat, not a copy) and
 *     fp+2D4 to fp+2D8;
 *   PlSc.dat loaded again (lbArchive_LoadArchive), and two public symbols of it by the costume:
 *     "TEV_Costume%d_Spin_Effects" into fp+22E4, "TEV_Costume%d_Shoe_Effects" into fp+22E0;
 *   seven fighter variables cleared (22D4, 22DC, 22EC, 22F0, 22F4, 22F8, 22E8);
 *   his copy of the common state table (+0BC8);
 *   articles 0, 1 and 2 handed to the item code by index (MEX_IndexFighterItem, 0x803D7058);
 *   the CPU setup while the custom data word is 0 (always);
 *   nine model part overrides (parts 1 to 9: 1, 1, 0, 1, 1, 0, 1, 1, 1).
 *
 * Native differences, neither in the file:
 *   1. fp->dat_attrs_backup is left as the game allocated it and only fp->dat_attrs points at the
 *      block. On the console fp+2D8 is overwritten with the file's address, and the game frees
 *      fp+2D8 into its attribute pool at fighter destroy (fighter.c, HSD_ObjFree): a disc address
 *      put on the pool's free list (a mod bug not reproduced). Nothing reads fp+2D8 for him (his
 *      slot 25 is his own [onrespawn], not Mario's attribute reload).
 *   2. The wall jump bit, as Toad's and Metal Mario's ports: MxDt's byte for internal 43 is 1 and
 *      the file does not set fp->can_walljump; on the console the m-ex code reads the byte. */
static void ftSc_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    HSD_Archive* archive;
    ftSc_ArticleSlot* items;
    char name[32];

    fp->dat_attrs = DP(fp->ft_data->ext_attr);

    archive = lbArchive_LoadArchive("PlSc.dat");
    sprintf(name, "TEV_Costume%d_Spin_Effects", fp->costume_id);
    fv->spin_tev = archive != NULL ? HSD_ArchiveGetPublicAddress(archive, name) : NULL;
    sprintf(name, "TEV_Costume%d_Shoe_Effects", fp->costume_id);
    fv->shoe_tev = archive != NULL ? HSD_ArchiveGetPublicAddress(archive, name) : NULL;

    fv->spring_live = 0;
    fv->spin_fx_state = 0;
    fv->side_used = 0;
    fv->neutral_used = 0;
    fv->up_used = 0;
    fv->lw_level = 0;
    fv->x22E8 = 0;

    ftSc_Init_CopyMotionStates(fp);

    items = (ftSc_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[ftSc_Article_Spring]), ftSc_Article_Spring);
    mu_ak_register_article(fp->kind, DP(items[ftSc_Article_Trail]), ftSc_Article_Trail);
    mu_ak_register_article(fp->kind, DP(items[ftSc_Article_Mark]), ftSc_Article_Mark);

    /* +00F4: *(+0C74) == 0, always: +01B0 stores 0x1C at +0D14 and installs the proc. */
    HSD_GObj_SetupProc(gobj, ftSc_Cpu_InitProc, 2);

    ftParts_80074A4C(gobj, 1, 1);
    ftParts_80074A4C(gobj, 2, 1);
    ftParts_80074A4C(gobj, 3, 0);
    ftParts_80074A4C(gobj, 4, 1);
    ftParts_80074A4C(gobj, 5, 1);
    ftParts_80074A4C(gobj, 6, 0);
    ftParts_80074A4C(gobj, 7, 1);
    ftParts_80074A4C(gobj, 8, 1);
    ftParts_80074A4C(gobj, 9, 1);

    if (mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind)) > 0) {
        fp->can_walljump = true;
    }

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Sonic BM load: articles %d %d %d, tev %s %s, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, ftSc_Article_Spring),
                     mu_ak_item_kind(fp->kind, ftSc_Article_Trail),
                     mu_ak_item_kind(fp->kind, ftSc_Article_Mark),
                     fv->spin_tev != NULL ? "spin" : "NO spin",
                     fv->shoe_tev != NULL ? "shoe" : "NO shoe", (int) fp->can_walljump);
        }
    }
}

/* ---- the spin ball effect ------------------------------------------------------------------- */

/* +3ADC: m-ex effect 5007 on his root joint, kept in fp+22D8, its first texture's TEV colors
 * (konst and tev0, the words at +10 and +14) taken from the costume's "Shoe" record (fp+22E0).
 * The console reads every pointer without a check; the native guards skip the painting (one log
 * line) where it would read through NULL. */
void ftSc_SpinFx_Spawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    EF_Effect* effect = efSync_Spawn(FTSC_EFFECT_SPIN, gobj, gobj->hsd_obj);
    HSD_DObj* dobj;
    HSD_TObjTev* tev = NULL;

    fv->spin_fx = effect;
    if (effect != NULL && effect->gobj != NULL && effect->gobj->hsd_obj != NULL) {
        dobj = HSD_JObjGetDObj(effect->gobj->hsd_obj);
        if (dobj != NULL && dobj->mobj != NULL && dobj->mobj->tobj != NULL) {
            tev = dobj->mobj->tobj->tev;
        }
    }
    if (tev == NULL || fv->shoe_tev == NULL) {
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Sonic BM: spin effect %d not painted (effect %p, tev %p, shoe %p)\n",
                     FTSC_EFFECT_SPIN, (void*) effect, (void*) tev, (void*) fv->shoe_tev);
        }
        return;
    }
    tev->konst = fv->shoe_tev->konst;
    tev->tev0 = fv->shoe_tev->tev0;
}

/* +2B1C, from [onframe] in the first two frames of a state: the spin ball's state machine.
 *   1 (armed): in the spin dash charge spawn the ball (2); in the run spawn it (3).
 *   2: in the run spawn it again (3), else as 3.
 *   3: TurnRun, Run and the spin dash charge keep it; GuardOn, Guard and GuardReflect re-arm (1);
 *      any other state destroys his effects and re-arms (the file then tests TurnRun again, which
 *      cannot hold there, for 0). */
static void ftSc_SpinFx_Update(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_FighterVars* fv = ftSc_FV(fp);
    FtMotionId msid;

    switch (fv->spin_fx_state) {
    case 0:
        return;
    case 1:
        msid = fp->motion_id;
        if (msid == ftSc_MS_SpecialSCharge) {
            ftSc_SpinFx_Spawn(gobj);
            fv->spin_fx_state = 2;
        } else if (msid == ftCo_MS_Run) {
            ftSc_SpinFx_Spawn(gobj);
            fv->spin_fx_state = 3;
        }
        return;
    case 2:
        msid = fp->motion_id;
        if (msid == ftCo_MS_Run) {
            ftSc_SpinFx_Spawn(gobj);
            fv->spin_fx_state = 3;
            return;
        }
        break;
    case 3:
        msid = fp->motion_id;
        break;
    default:
        return;
    }

    /* +2B98 */
    if (msid == ftCo_MS_TurnRun || msid == ftCo_MS_Run) {
        return;
    }
    if (msid == ftSc_MS_SpecialSCharge) {
        return;
    }
    if ((u32) (msid - ftCo_MS_GuardOn) <= 1 || msid == ftCo_MS_GuardReflect) {
        fv->spin_fx_state = 1;
        return;
    }
    efLib_DestroyAll(gobj);
    if (fp->motion_id == ftCo_MS_TurnRun) {
        fv->spin_fx_state = 0;
    } else {
        fv->spin_fx_state = 1;
    }
}

/* +2C44, from [onframe] every frame: the side taunt. With D-pad left or right pressed in Wait,
 * the walks, Turn, TurnRun, Run, Squat, SquatWait or SquatRv: state 371 facing right, else 370;
 * then cmd_vars[1] and [2] cleared and efLib_DestroyAll put in fp+21DC and fp+21EC. */
static void ftSc_Appeal_CheckInput(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 n;

    if ((fp->input.pressed_buttons & (HSD_PAD_DPADLEFT | HSD_PAD_DPADRIGHT)) == 0) {
        return;
    }
    n = (u32) (fp->motion_id - ftCo_MS_Wait);
    if (n > 0x1B) {
        return;
    }
    /* 0x0E0000BF: Wait to TurnRun (0xE to 0x13), Run (0x15), Squat to SquatRv (0x27 to 0x29) */
    if (((0x0E0000BFu >> n) & 1) == 0) {
        return;
    }
    if (fp->facing_dir <= 0.0f) {
        Fighter_ChangeMotionState(gobj, ftSc_MS_AppealSL, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftSc_MS_AppealSR, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
    fp->cmd_vars[1] = 0;
    fp->take_dmg_cb = efLib_DestroyAll;
    fp->x21EC = efLib_DestroyAll;
    fp->cmd_vars[2] = 0;
}

/* +0A2C (slot 23, onframe). In the first two frames of a state (frame under 2.0): a thrown
 * state gets the bone fix up as its animation callback for this state too (fp+21A0); Dash arms
 * the spin ball; the side special's start and TurnRun arm it and destroy his effects; then the
 * spin ball's state machine. Every frame: the side taunt check. */
static void ftSc_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid;

    if (fp->cur_anim_frame >= 2.0f) {
        ftSc_Appeal_CheckInput(gobj);
        return;
    }
    msid = fp->motion_id;
    if ((u32) (msid - ftCo_MS_ThrownF) <= 3) {
        fp->anim_cb = ftSc_Thrown_Anim;
    } else if (msid == ftCo_MS_Dash) {
        ftSc_FV(fp)->spin_fx_state = 1;
    } else if (msid == ftSc_MS_SpecialS || msid == ftCo_MS_TurnRun) {
        ftSc_FV(fp)->spin_fx_state = 1;
        efLib_DestroyAll(gobj);
    }
    ftSc_SpinFx_Update(gobj);
    ftSc_Appeal_CheckInput(gobj);
}

/* The three "once per air time" flags. */
static inline void ftSc_ClearUsed(Fighter* fp)
{
    ftSc_FighterVars* fv = ftSc_FV(fp);

    fv->side_used = 0;
    fv->neutral_used = 0;
    fv->up_used = 0;
}

/* +0AFC (slot 24, onactionstatechange): the three flags cleared in DamageHi1 to DamageFlyLw,
 * CliffCatch, CapturePulledHi to CaptureFoot and CaptureCaptain to ThrownKirby. */
static void ftSc_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FtMotionId msid = fp->motion_id;

    if ((u32) (msid - ftCo_MS_DamageHi1) <= 0xE || msid == ftCo_MS_CliffCatch ||
        (u32) (msid - ftCo_MS_CapturePulledHi) <= 9)
    {
        ftSc_ClearUsed(fp);
        return;
    }
    if ((u32) (msid - ftCo_MS_CaptureCaptain) <= 0x11) {
        ftSc_ClearUsed(fp);
    }
}

/* +0B48 (slot 25, onrespawn): the three flags cleared. */
static void ftSc_Init_OnRespawn(HSD_GObj* gobj)
{
    ftSc_ClearUsed(GET_FIGHTER(gobj));
}

/* +0B60 (slot 34, onlanding): the three flags cleared, except in the grounded up special. */
static void ftSc_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->motion_id == ftSc_MS_SpecialHi) {
        return;
    }
    ftSc_ClearUsed(fp);
}

/* +29E8: the thrown states' animation callback. Retail ftCo_ThrownF_Anim (0x800DE69C, called
 * through a register for all four states), then twelve joint rotations of his model turned by
 * fixed angles (rotate.x/y/z, console +1C/+20/+24), each in double and rounded to single:
 * 2.0943951023931953 (2 pi / 3), pi / 2 and pi. */
static void ftSc_Thrown_Anim(HSD_GObj* gobj)
{
    Fighter* fp;
    FighterBone* parts;
    HSD_JObj* j7;
    HSD_JObj* j8;
    HSD_JObj* j9;
    HSD_JObj* j11;
    HSD_JObj* j12;
    HSD_JObj* j13;
    HSD_JObj* j50;
    HSD_JObj* j51;

    ftCo_ThrownF_Anim(gobj);
    fp = GET_FIGHTER(gobj);
    parts = fp->parts;
    j7 = parts[7].joint;
    j8 = parts[8].joint;
    j9 = parts[9].joint;
    j11 = parts[11].joint;
    j12 = parts[12].joint;
    j13 = parts[13].joint;
    j50 = parts[50].joint;
    j51 = parts[51].joint;

    j7->rotate.x = (float) (j7->rotate.x - 0x1.0c152382d7365p+1);
    j8->rotate.z = (float) (j8->rotate.z - 0x1.921fb54442d18p+0);
    j11->rotate.y = (float) (j11->rotate.y - 0x1.921fb54442d18p+0);
    j9->rotate.z = (float) (j9->rotate.z - 0x1.921fb54442d18p+0);
    j11->rotate.x = (float) (j11->rotate.x + 0x1.921fb54442d18p+1);
    j11->rotate.z = (float) (j11->rotate.z - 0x1.921fb54442d18p+0);
    j12->rotate.x = (float) (j12->rotate.x + 0x1.921fb54442d18p+0);
    j13->rotate.y = (float) (j13->rotate.y + 0x1.921fb54442d18p+1);
    j12->rotate.z = (float) (j12->rotate.z + 0x1.921fb54442d18p+0);
    j50->rotate.x = (float) (j50->rotate.x - 0x1.921fb54442d18p+1);
    j50->rotate.z = (float) (j50->rotate.z + 0x1.921fb54442d18p+0);
    j51->rotate.y = (float) (j51->rotate.y - 0x1.921fb54442d18p+1);
}

/* +2D20: the forward smashes' animation callback (AttackS4Hi, AttackS4S, AttackS4Lw).
 * At frame 2.0 or less: the per-move words cleared (hit_done, cmd_vars[0], count; flag = 1).
 * While cmd_vars[1] is 1, the flag is 1 and the frame is under 13.0 (or at once in the first
 * frames): A held goes to the hold state 353, which comes back to this state at frame 3 (+1610).
 * Every frame: each active hitbox, once, gets its damage scaled by (1 + count * 0.006118333)
 * (fused in double, rounded to single, times the base damage in single, truncated). */
static void ftSc_AttackS4_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    bool hold = false;
    int i;

    if (!(fp->cur_anim_frame <= 2.0f)) {
        /* +2E6C (frame over 2.0, or unordered) */
        if (!(fp->cur_anim_frame >= 13.0f) && fp->cmd_vars[1] == 1 && mv->flag == 1) {
            hold = true;
        }
    } else {
        u32 cmd1 = fp->cmd_vars[1];
        ftSc_MV_SetWord2370(fp, 0);
        fp->cmd_vars[0] = 0;
        mv->count = 0;
        mv->flag = 1;
        if (cmd1 == 1) {
            hold = true;
        }
    }
    if (hold && (fp->input.held_buttons[0] & HSD_PAD_A)) {
        /* +2E94 */
        FtMotionId back = fp->motion_id;
        mv->flag = 0;
        mv->count = 0;
        Fighter_ChangeMotionState(gobj, ftSc_MS_AttackS4Hold, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        mv->slot = back;
    }

    /* +2D84 */
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        if (hit->state != HitCapsule_Disabled && mv->hit_done[i] != 1) {
            float mul;
            float base;
            float dmg;
            mv->hit_done[i] = 1;
            mul = (float) __builtin_fma((double) mv->count, 0x1.90f899929c07cp-8, 1.0);
            base = (float) (s32) hit->unk_count;
            dmg = mul * base;
            ftColl_8007ABD0(hit, (u32) (s32) dmg, gobj);
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1610: state 353, the forward smash held. One more hold frame each frame; when A is let go or
 * the animation ends: back to the forward smash he came from (mv->slot) at frame 3, and the
 * smash damage multiplier fp+2120 set to 1.3671. */
void ftSc_AttackS4Hold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSc_MotionVars* mv = ftSc_MV(fp);
    HSD_Pad held = fp->input.held_buttons[0] & HSD_PAD_A;

    mv->count += 1;
    if (held && ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, mv->slot, Ft_MF_None, 3.0f, 1.0f, 0.0f, NULL);
    fp->smash_attrs.x2120_damageMul = 0x1.5dfa44p+0f; /* 1.3671 */
}

/* +2F08: the six fall states' interrupt callback. Before an up special is used: the retail
 * Fall_IASA for Fall, FallF, FallB and FallAerial_IASA for the three FallAerial states. After
 * it (fp+22F4): only A or B pressed, or the C-stick past 0.1 on either axis, runs the retail
 * Fall_IASA_Inner (so no jump). */
static void ftSc_Fall_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float c;

    if (ftSc_FV(fp)->up_used != 1) {
        FtMotionId msid = fp->motion_id;
        if (msid > ftCo_MS_FallB) {
            if ((u32) (msid - ftCo_MS_FallAerial) <= 2) {
                ftCo_FallAerial_IASA(gobj);
            }
        } else if (msid > ftCo_MS_JumpAerialB) {
            ftCo_Fall_IASA(gobj);
        }
        return;
    }
    if (fp->input.pressed_buttons & (HSD_PAD_A | HSD_PAD_B)) {
        ftCo_Fall_IASA_Inner(gobj);
        return;
    }
    c = fp->input.cstick[0].x;
    if (!(c >= 0.0f)) {
        c = -c;
    }
    if (c > 0.1) {
        ftCo_Fall_IASA_Inner(gobj);
        return;
    }
    c = fp->input.cstick[0].y;
    if (c < 0.0f) {
        c = -c;
    }
    /* blelr: an unordered value runs the interrupt */
    if (!(c <= 0.1)) {
        ftCo_Fall_IASA_Inner(gobj);
    }
}

/* +28F8: the side taunt's animation callback (states 370, 371). cmd_vars[1] = 1 from the script
 * spawns the spin ball, cmd_vars[2] = 1 destroys his effects; at the end, the retail wait. */
void ftSc_AppealS_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        fp->cmd_vars[1] = 0;
        ftSc_SpinFx_Spawn(gobj);
    }
    if (fp->cmd_vars[2] == 1) {
        fp->cmd_vars[2] = 0;
        efLib_DestroyAll(gobj);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +33F8: the distance between two points of the plane. |dx| squared in double, then fused with
 * |dy| squared (fmadd), rounded to single, and the game's sqrtf. */
float ftSc_Distance(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy;

    if (!(dx >= 0.0f)) {
        dx = -dx;
    }
    dy = y2 - y1;
    if (!(dy >= 0.0f)) {
        dy = -dy;
    }
    return sqrtf((float) __builtin_fma((double) dy, (double) dy, (double) dx * (double) dx));
}

/* +01D4 (slot 3, move_logic): 31 rows, numbered from 341. Every word is the file's
 * (tables.txt). Rows 0, 1 and 10, 11 have no camera callback. The retail callbacks are named by
 * the address in the row. */
const MotionState ftSc_Init_MotionStateTable[FTSC_MS_COUNT] = {
    /* 341 empty */
    { -1, 0x00000000, 0x01000000, NULL, NULL, NULL, NULL, NULL },
    /* 342 empty */
    { -1, 0x00000000, 0x01000000, NULL, NULL, NULL, NULL, NULL },
    /* 343 SpecialN */
    { 295, 0x00000000, 0x12000000, ftSc_SpecialN_Anim, NULL, ftSc_SpecialN_Phys,
      ftSc_SpecialN_Coll, ftCamera_UpdateCameraBox },
    /* 344 SpecialAirN: the same callbacks */
    { 296, 0x00000000, 0x12000000, ftSc_SpecialN_Anim, NULL, ftSc_SpecialN_Phys,
      ftSc_SpecialN_Coll, ftCamera_UpdateCameraBox },
    /* 345 SpecialS */
    { 297, 0x00000000, 0x13000000, ftSc_SpecialS_Anim, NULL, ftSc_SpecialS_Phys,
      ftSc_SpecialS_Coll, ftCamera_UpdateCameraBox },
    /* 346 SpecialAirS */
    { 298, 0x00341012, 0x13000000, ftSc_SpecialAirS_Anim, NULL, ftSc_SpecialAirS_Phys,
      ftSc_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 347 SpecialHi */
    { 299, 0x00340213, 0x14000000, ftSc_SpecialHi_Anim, ftSc_SpecialHi_IASA, ftSc_SpecialHi_Phys,
      ftSc_SpecialHi_Coll, ftCamera_UpdateCameraBox },
    /* 348 SpecialAirHi: the same callbacks */
    { 300, 0x00340613, 0x14000000, ftSc_SpecialHi_Anim, ftSc_SpecialHi_IASA, ftSc_SpecialHi_Phys,
      ftSc_SpecialHi_Coll, ftCamera_UpdateCameraBox },
    /* 349 SpecialLw: physics retail ftCo_AttackS4_Phys (0x8008C7F0) */
    { 301, 0x00000000, 0x15000000, ftSc_SpecialLw_Anim, NULL, ftCo_AttackS4_Phys,
      ftSc_SpecialLw_Coll, ftCamera_UpdateCameraBox },
    /* 350 SpecialAirLw */
    { 302, 0x00000000, 0x15000000, ftSc_SpecialAirLw_Anim, NULL, ftSc_SpecialAirLw_Phys,
      ftSc_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 351: retail ftCo_800C7158 (0x800C7158) as physics, nothing else; nothing enters it */
    { 14, 0x00000000, 0x01000000, NULL, NULL, ftCo_800C7158, NULL, NULL },
    /* 352: retail ftCo_800C7200 (0x800C7200) as physics; nothing enters it */
    { 15, 0x00000000, 0x01000000, NULL, NULL, ftCo_800C7200, NULL, NULL },
    /* 353 forward smash held: retail ftCo_AttackS4_Phys, ftCo_AttackS4_Coll (0x8008C810) */
    { 303, 0x00240A09, 0x0A800000, ftSc_AttackS4Hold_Anim, NULL, ftCo_AttackS4_Phys,
      ftCo_AttackS4_Coll, ftCamera_UpdateCameraBox },
    /* 354 spin charge, ground */
    { 304, 0x00000000, 0x15000000, ftSc_SpecialLwCharge_Anim, NULL, ftCo_AttackS4_Phys,
      ftSc_SpecialLw_Coll, ftCamera_UpdateCameraBox },
    /* 355 roll stopped: no physics callback */
    { 305, 0x00000000, 0x15000000, ftSc_SpecialLwStop_Anim, NULL, NULL, ftSc_SpecialLw_Coll,
      ftCamera_UpdateCameraBox },
    /* 356 spin charge, air */
    { 306, 0x00000000, 0x15000000, ftSc_SpecialAirLwCharge_Anim, NULL, ftSc_SpecialAirLw_Phys,
      ftSc_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 357 charge let go */
    { 307, 0x00000000, 0x15000000, ftSc_SpecialLwCancel_Anim, NULL, ftCo_AttackS4_Phys,
      ftCo_AttackS4_Coll, ftCamera_UpdateCameraBox },
    /* 358 roll, ground */
    { 308, 0x00000000, 0x15000000, ftSc_SpecialLwRoll_Anim, ftSc_SpecialLwRoll_IASA,
      ftSc_SpecialLwRoll_Phys, ftSc_SpecialLw_Coll, ftCamera_UpdateCameraBox },
    /* 359 roll, air */
    { 309, 0x00000000, 0x15000000, ftSc_SpecialAirLwRoll_Anim, ftSc_SpecialAirLwRoll_IASA,
      ftSc_SpecialAirLwRoll_Phys, ftSc_SpecialAirLw_Coll, ftCamera_UpdateCameraBox },
    /* 360 spin dash charge, air */
    { 310, 0x00000000, 0x13000000, ftSc_SpecialAirSCharge_Anim, NULL, ftSc_SpecialSCharge_Phys,
      ftSc_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 361 spin dash charge, ground: physics +22F4 is +219C again (same eight words) */
    { 311, 0x00000000, 0x13000000, ftSc_SpecialSCharge_Anim, NULL, ftSc_SpecialSCharge_Phys,
      ftSc_SpecialS_Coll, ftCamera_UpdateCameraBox },
    /* 362 brake: retail ftCo_RunBrake_Phys (0x800CAE18), ftCo_RunBrake_Coll (0x800CAE60) */
    { 312, 0x00000000, 0x13000000, ftSc_SpecialSBrake_Anim, ftSc_SpecialSBrake_IASA,
      ftCo_RunBrake_Phys, ftCo_RunBrake_Coll, ftCamera_UpdateCameraBox },
    /* 363 air spin dash end */
    { 313, 0x00000000, 0x13000000, ftSc_SpecialAirSEnd_Anim, NULL, ftSc_SpecialAirSEnd_Phys,
      ftSc_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 364 homing dash */
    { 314, 0x00000000, 0x12000000, ftSc_SpecialNDash_Anim, ftSc_SpecialNDash_IASA,
      ftSc_SpecialNDash_Phys, ftSc_SpecialNDash_Coll, ftCamera_UpdateCameraBox },
    /* 365 homing landed: retail ftCo_LandingAir_Anim/IASA/Phys/Coll (0x8008D78C to D7D0) */
    { 315, 0x00000000, 0x12000000, ftCo_LandingAir_Anim, ftCo_LandingAir_IASA,
      ftCo_LandingAir_Phys, ftCo_LandingAir_Coll, ftCamera_UpdateCameraBox },
    /* 366 homing rebound */
    { 316, 0x00000000, 0x12000000, ftSc_SpecialNRebound_Anim, NULL, ftSc_SpecialNRebound_Phys,
      ftSc_SpecialNRebound_Coll, ftCamera_UpdateCameraBox },
    /* 367 homing miss */
    { 317, 0x00000000, 0x12000000, ftSc_SpecialNMiss_Anim, NULL, ftSc_SpecialNMiss_Phys,
      ftSc_SpecialNMiss_Coll, ftCamera_UpdateCameraBox },
    /* 368 air roll end */
    { 318, 0x00000000, 0x15000000, ftSc_SpecialAirLwEnd_Anim, NULL, ftSc_SpecialAirLwEnd_Phys,
      ftSc_SpecialAirLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 369 air spin dash release */
    { 319, 0x00000000, 0x13000000, ftSc_SpecialAirSRelease_Anim, NULL, ftSc_SpecialAirSEnd_Phys,
      ftSc_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 370 side taunt left: retail ftCo_AppealS_IASA/Phys/Coll (0x800DED30, 0x800DEE44,
     * 0x800DEE64) */
    { 320, 0x00000000, 0x01000000, ftSc_AppealS_Anim, ftCo_AppealS_IASA, ftCo_AppealS_Phys,
      ftCo_AppealS_Coll, ftCamera_UpdateCameraBox },
    /* 371 side taunt right */
    { 321, 0x00000000, 0x01000000, ftSc_AppealS_Anim, ftCo_AppealS_IASA, ftCo_AppealS_Phys,
      ftCo_AppealS_Coll, ftCamera_UpdateCameraBox },
};

/* The 14 slots PlSc.dat exports. Every other field is NULL: the registry takes MxDt's default,
 * Mario's function. Ready: the 0.9 scripted matches pass. */
const MuAkFighter mu_ak_sonicbm = {
    .name = "Sonic BM",
    .file = "PlSc.dat",

    .onload = ftSc_Init_OnLoad,
    .specialn = ftSc_SpecialN_Enter,
    .specialairn = ftSc_SpecialAirN_Enter,
    .specials = ftSc_SpecialS_Enter,
    .specialairs = ftSc_SpecialAirS_Enter,
    .specialhi = ftSc_SpecialHi_Enter,
    .specialairhi = ftSc_SpecialAirHi_Enter,
    .speciallw = ftSc_SpecialLw_Enter,
    .specialairlw = ftSc_SpecialAirLw_Enter,
    .onframe = ftSc_Init_OnFrame,
    .onactionstatechange = ftSc_Init_OnActionStateChange,
    .onrespawn = ftSc_Init_OnRespawn,
    .onlanding = ftSc_Init_OnLanding,

    .move_logic = ftSc_Init_MotionStateTable,
    .move_logic_count = FTSC_MS_COUNT,

    .flags = MU_AK_READY,
    .articles = itSc_Articles,
    .article_count = ftSc_Article_Count,
};
