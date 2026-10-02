/* Akaneia's Diddy Kong: fighter table, load/respawn hooks, item hooks, victory pose, CPU. */
#include "ftdiddy.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbanim.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>

#include "../mu_ak_fighter.h"

/* ------------------------------------------------------------------------------------------ */
/* move_logic                                                                                 */
/* ------------------------------------------------------------------------------------------ */

#define DD_STATE(sm, move, name)                                                            \
    {                                                                                       \
        sm, ftDd_MF_Special, { (move) << 24 }, ftDd_##name##_Anim, ftDd_##name##_IASA,      \
            ftDd_##name##_Phys, ftDd_##name##_Coll, ftCamera_UpdateCameraBox,               \
    }

MotionState ftDd_MotionStateTable[ftDd_MS_SelfCount] = {
    DD_STATE(ftDd_SM_SpecialNStart, FtMoveId_SpecialN, SpecialNStart),               /* 341 */
    DD_STATE(ftDd_SM_SpecialNCharge, FtMoveId_SpecialN, SpecialNCharge),             /* 342 */
    DD_STATE(ftDd_SM_SpecialNDanger, FtMoveId_SpecialN, SpecialNDanger),             /* 343 */
    DD_STATE(ftDd_SM_SpecialNBlow, FtMoveId_SpecialN, SpecialNBlow),                 /* 344 */
    DD_STATE(ftDd_SM_SpecialNShoot, FtMoveId_SpecialN, SpecialNShoot),               /* 345 */
    DD_STATE(ftDd_SM_SpecialAirNStart, FtMoveId_SpecialN, SpecialAirNStart),         /* 346 */
    DD_STATE(ftDd_SM_SpecialAirNCharge, FtMoveId_SpecialN, SpecialAirNCharge),       /* 347 */
    DD_STATE(ftDd_SM_SpecialAirNDanger, FtMoveId_SpecialN, SpecialAirNDanger),       /* 348 */
    DD_STATE(ftDd_SM_SpecialAirNBlow, FtMoveId_SpecialN, SpecialAirNBlow),           /* 349 */
    DD_STATE(ftDd_SM_SpecialAirNShoot, FtMoveId_SpecialN, SpecialAirNShoot),         /* 350 */
    DD_STATE(ftDd_SM_SpecialSStart, FtMoveId_SpecialS, SpecialSStart),               /* 351 */
    DD_STATE(ftDd_SM_SpecialSStick, FtMoveId_SpecialS, SpecialSStick),               /* 352 */
    DD_STATE(ftDd_SM_SpecialSStickAttack, FtMoveId_SpecialS, SpecialSStickAttack),   /* 353 */
    DD_STATE(ftDd_SM_SpecialSStickAttack2, FtMoveId_SpecialS, SpecialSStickAttack2), /* 354 */
    DD_STATE(ftDd_SM_SpecialSStickJump, FtMoveId_SpecialS, SpecialSStickJump),       /* 355 */
    DD_STATE(ftDd_SM_SpecialSStickJump2, FtMoveId_SpecialS, SpecialSStickJump2),     /* 356 */
    DD_STATE(ftDd_SM_SpecialAirSStart, FtMoveId_SpecialS, SpecialAirSStart),         /* 357 */
    DD_STATE(ftDd_SM_SpecialAirSJump, FtMoveId_SpecialS, SpecialAirSJump),           /* 358 */
    DD_STATE(ftDd_SM_SpecialAirSKick, FtMoveId_SpecialS, SpecialAirSKick),           /* 359 */
    DD_STATE(ftDd_SM_SpecialHiStart, FtMoveId_SpecialHi, SpecialHiStart),            /* 360 */
    DD_STATE(ftDd_SM_SpecialHiCharge, FtMoveId_SpecialHi, SpecialHiCharge),          /* 361 */
    DD_STATE(ftDd_SM_SpecialAirHiStart, FtMoveId_SpecialHi, SpecialAirHiStart),      /* 362 */
    DD_STATE(ftDd_SM_SpecialAirHiCharge, FtMoveId_SpecialHi, SpecialAirHiCharge),    /* 363 */
    DD_STATE(ftDd_SM_SpecialAirHiJump, FtMoveId_SpecialHi, SpecialAirHiJump),        /* 364 */
    DD_STATE(ftDd_SM_SpecialAirHiDamage, FtMoveId_SpecialHi, SpecialAirHiDamage),    /* 365 */
    DD_STATE(ftDd_SM_SpecialAirHiDamage2, FtMoveId_SpecialHi, SpecialAirHiDamage),   /* 366 */
    DD_STATE(ftDd_SM_SpecialLw, FtMoveId_SpecialLw, SpecialLw),                      /* 367 */
    DD_STATE(ftDd_SM_SpecialAirLw, FtMoveId_SpecialLw, SpecialAirLw),                /* 368 */
    /* Victim states. The shared animation ids are Diddy's, played on the victim through the
     * thrower-animation path of Fighter_ChangeMotionState (see ftDd_SpecialS_TaroChange). */
    DD_STATE(ftDd_SM_SpecialSStickWaitTaro, FtMoveId_SpecialS, SpecialSStickWaitTaro),       /* 369 */
    DD_STATE(ftDd_SM_SpecialSStickWaitTaro, FtMoveId_SpecialS, SpecialAirSStickWaitTaro),    /* 370 */
    DD_STATE(ftDd_SM_SpecialSStickJumpTaro, FtMoveId_SpecialS, SpecialSStickJumpTaro),       /* 371 */
    DD_STATE(ftDd_SM_SpecialSStickJumpTaro, FtMoveId_SpecialS, SpecialAirSStickJumpTaro),    /* 372 */
    DD_STATE(ftDd_SM_SpecialSStickAttackTaro, FtMoveId_SpecialS, SpecialSStickAttackTaro),   /* 373 */
    DD_STATE(ftDd_SM_SpecialSStickAttackTaro, FtMoveId_SpecialS, SpecialAirSStickAttackTaro), /* 374 */
};

#undef DD_STATE

/* ------------------------------------------------------------------------------------------ */
/* Articles                                                                                   */
/* ------------------------------------------------------------------------------------------ */

/* Every article Diddy spawns (popgun, peanut, banana) is created the same way: a SpawnItem at a
 * point, facing Diddy's way, no velocity, created raw so the hold kind stays as given (the
 * console code calls the creator itself, not the Item_80268B18/B5C wrappers that rewrite it). */
Item_GObj* ftDd_SpawnArticle(HSD_GObj* gobj, int article, int hold_kind, Vec3* pos,
                             GroundOrAir ga)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn;

    /* The console leaves x45..x47 as stack garbage; zero is the only safe native choice. */
    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, article);
    spawn.hold_kind = hold_kind;
    spawn.x10 = 0;
    spawn.pos = *pos;
    spawn.prev_pos = *pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.facing_dir = fp->facing_dir;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = false;
    spawn.x48_ground_or_air = ga;
    return mu_ak_item_create(&spawn);
}

/* ------------------------------------------------------------------------------------------ */
/* Victory pose: two popguns, fired by the win animation's script                             */
/* ------------------------------------------------------------------------------------------ */

static void ftDd_Win_Accessory(HSD_GObj* gobj);
static void ftDd_Win1_Anim(HSD_GObj* gobj);

/* DiddyWin_SpawnGun: a popgun held on an already resolved bone index. */
Item_GObj* ftDd_Win_SpawnGun(HSD_GObj* gobj, Fighter_Part bone)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* gun = ftDd_SpawnArticle(gobj, ftDd_Article_Popgun, 8, &fp->cur_pos, GA_Ground);

    ftDd_MV(fp)->specialn.gun = gun;
    if (gun == NULL) {
        return NULL;   /* a failed create: no gun to attach */
    }
    Item_8026AB54(gun, gobj, bone);
    ftDd_SpecialN_GunChangeModel(gun, 0);
    return gun;
}

/* DiddyWin1_Shoot: fire a peanut out of @p gun's barrel (joint 7), straight along the barrel. */
void ftDd_Win_Shoot(HSD_GObj* gobj, Item_GObj* gun)
{
    HSD_JObj* barrel;
    Vec3 muzzle;
    Vec3 ahead;
    Vec3 dir;
    Vec3 offset = { 0.0F, -5.0F, 0.0F };
    Item_GObj* nut;
    Item* ip;

    lb_80011E24(GET_JOBJ(gun), &barrel, 7, -1);
    lb_8000B1CC(barrel, NULL, &muzzle);
    lb_8000B1CC(barrel, &offset, &ahead);

    nut = ftDd_SpawnArticle(gobj, ftDd_Article_Peanut, 8, &muzzle, GA_Air);
    ip = GET_ITEM(nut);

    PSVECSubtract(&ahead, &muzzle, &dir);
    lbVector_Normalize(&dir);
    ip->x40_vel = dir;
    ip->x40_vel.x *= 8.0F;
    ip->x40_vel.y *= 8.0F;
    ip->x40_vel.z *= 8.0F;
}

/* Diddy_WinAccessory: on the first victory motion, give Diddy a popgun in each hand. */
static void ftDd_Win_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->motion_id != 0) {
        return;
    }
    ftDd_MV(fp)->win.gun_right =
        ftDd_Win_SpawnGun(gobj, ftParts_GetBoneIndex(fp, (Fighter_Part) ftDd_Part_GunHand));
    ftDd_MV(fp)->win.gun_left =
        ftDd_Win_SpawnGun(gobj, ftParts_GetBoneIndex(fp, (Fighter_Part) ftDd_Part_GunHand2));
}

/* DiddyWin1_AnimCB: the win script raises cmd_vars[0] / [1] to fire the left / right gun. */
static void ftDd_Win1_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] != 0) {
        ftDd_Win_Shoot(gobj, ftDd_MV(fp)->win.gun_left);
        fp->cmd_vars[0] = 0;
    }
    if (fp->cmd_vars[1] != 0) {
        ftDd_Win_Shoot(gobj, ftDd_MV(fp)->win.gun_right);
        fp->cmd_vars[1] = 0;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* CPU: m-ex's "spoof" template. The AI tables are indexed by fighter kind and have no row for  */
/* Diddy, so while the AI thinks, Diddy is presented as Mario.                                 */
/* ------------------------------------------------------------------------------------------ */

#define ftDd_CpuSpoofKind Ft_Kind_Mario

/* MexCPU_ProcSpoof. m-ex's own MexCPU_Process is ftCo_800B3900 without its Ice Climbers partner
 * sync (ftCo_800B0AF4), which finds no partner for a lone fighter and does nothing. */
static void ftDd_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftDd_CpuSpoofKind;
    ftCo_800B3900(gobj);
    fp->kind = kind;
}

/* MexCPU_InitProc: runs every frame until it finds the game's CPU proc, then swaps it out. The
 * console never removes this proc once the swap is done; it keeps scanning, harmlessly. */
static void ftDd_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftDd_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* MexCPU_InitSpoofData. m-ex's custom-AI path (a non-NULL mexcpu_data) is never taken by
 * PlDd.dat: nothing writes that pointer, so only the spoof is installed. */
static void ftDd_Cpu_Init(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftDd_Cpu_InitProc, 2);
}

/* ------------------------------------------------------------------------------------------ */
/* ftFunction                                                                                 */
/* ------------------------------------------------------------------------------------------ */

static inline float ftDd_AnimLength(Fighter* fp, enum_t sm)
{
    return lbAnim_8001E8F8(ftData_80085E50(fp, sm));
}

void ftDd_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    typedef DISC_PTR(void) ArticlePtr;
    ArticlePtr* articles;

    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), ftDd_DatAttrs_Size);

    ftDd_FV(fp)->peanut_charge_frames = ftDd_AnimLength(fp, ftDd_SM_SpecialNCharge) +
                                        ftDd_AnimLength(fp, ftDd_SM_SpecialNDanger);
    ftDd_FV(fp)->hi_jump_frames = ftDd_AnimLength(fp, ftDd_SM_SpecialAirHiJump);
    ftDd_FV(fp)->hi_charge_frames = ftDd_AnimLength(fp, ftDd_SM_SpecialHiCharge);

    fp->dat_attrs = fp->dat_attrs_backup;

    articles = (ArticlePtr*) DP(fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(articles[ftDd_Article_Popgun]), ftDd_Article_Popgun);
    mu_ak_register_article(fp->kind, DP(articles[ftDd_Article_Peanut]), ftDd_Article_Peanut);
    mu_ak_register_article(fp->kind, DP(articles[ftDd_Article_Banana]), ftDd_Article_Banana);

    ftDd_FV(fp)->banana = NULL;

    /* A result-screen fighter has only the 14 demo motions (fp->x18 counts the common motions). */
    if (fp->x18 != 14) {
        ftDd_Cpu_Init(gobj);
    } else {
        MotionState* demo;

        fp->x21EC = ftDd_Win_Accessory;
        demo = HSD_MemAlloc(14 * sizeof(MotionState));
        memcpy(demo, fp->x1C_actionStateList, 14 * sizeof(MotionState));
        demo[0].anim_cb = ftDd_Win1_Anim;
        fp->x1C_actionStateList = demo;
    }
}

/* ondeath slot (PlDd.dat names it onRespawn): back to the default model parts. */
void ftDd_Init_OnDeath(HSD_GObj* gobj)
{
    ftParts_80074A4C(gobj, 0, 0);
}

/* onunknown slot: empty on the console too. */
void ftDd_Init_OnDestroy(HSD_GObj* gobj) {}

void ftDd_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

void ftDd_Init_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

void ftDd_Init_OnItemVisible(HSD_GObj* gobj)
{
    Fighter_OnItemVisible(gobj, true);
}

void ftDd_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

void ftDd_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item)
{
    ftDd_Init_OnItemPickup(gobj, catch_item);
}

void ftDd_Init_OnItemUnk(HSD_GObj* gobj, bool drop_item)
{
    ftDd_Init_OnItemDrop(gobj, drop_item);
}

/* onhit / onunknowneyetexturerelated slots: the damaged eyes. */
void ftDd_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    Fighter_OnKnockbackEnter(gobj, 1);
}

void ftDd_Init_OnKnockbackExit(HSD_GObj* gobj)
{
    Fighter_OnKnockbackExit(gobj, 1);
}

/* OnFrame: per-motion animation speed changes from the attribute table (the console reads the
 * backup copy of the attributes, which dat_attrs points at anyway). Every matching row applies,
 * in order, so the last row whose frame has passed wins. */
void ftDd_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* attrs = fp->dat_attrs_backup;
    ftDd_AnimRate* row = DP(attrs->anim_rates);

    if (row == NULL) {
        return;
    }
    for (; row->motion_id != -1; row++) {
        if (fp->motion_id == row->motion_id && !(fp->cur_anim_frame < row->frame)) {
            ftAnim_SetAnimRate(gobj, row->rate);
        }
    }
}

/* onrespawn slot (ResetAttributes): fresh attributes from the file. */
void ftDd_Init_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    memcpy(fp->dat_attrs, DP(fp->ft_data->ext_attr), ftDd_DatAttrs_Size);
}

void ftDd_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* ------------------------------------------------------------------------------------------ */
/* The fighter                                                                                */
/* ------------------------------------------------------------------------------------------ */

/* The item callbacks take a second argument (the decomp's ftData_OnItem* tables are
 * void (*)(HSD_GObj*, bool)); MuAkEvent has one. The integration layer calls these through the
 * two-argument type (see NOTES.md). */
#define DD_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (fn))

const MuAkFighter mu_ak_diddy = {
    .name = "Diddy Kong",
    .file = "PlDd.dat",
    .onload = ftDd_Init_OnLoad,
    .ondeath = ftDd_Init_OnDeath,
    .onunknown = ftDd_Init_OnDestroy,
    .specialn = ftDd_SpecialN_Enter,
    .specialairn = ftDd_SpecialAirN_Enter,
    .specials = ftDd_SpecialS_Enter,
    .specialairs = ftDd_SpecialAirS_Enter,
    .specialhi = ftDd_SpecialHi_Enter,
    .specialairhi = ftDd_SpecialAirHi_Enter,
    .speciallw = ftDd_SpecialLw_Enter,
    .specialairlw = ftDd_SpecialAirLw_Enter,
    .onitempickup = DD_ITEM_EVENT(ftDd_Init_OnItemPickup),
    .onmakeiteminvisible = ftDd_Init_OnItemInvisible,
    .onmakeitemvisible = ftDd_Init_OnItemVisible,
    .onitemdrop = DD_ITEM_EVENT(ftDd_Init_OnItemDrop),
    .onitemcatch = DD_ITEM_EVENT(ftDd_Init_OnItemCatch),
    .onunknownitemrelated = DD_ITEM_EVENT(ftDd_Init_OnItemUnk),
    .onhit = ftDd_Init_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftDd_Init_OnKnockbackExit,
    .onframe = ftDd_Init_OnFrame,
    .onrespawn = ftDd_Init_OnRespawn,
    .enterdoublejump = ftDd_Init_EnterDoubleJump,
    .move_logic = ftDd_MotionStateTable,
    .move_logic_count = ftDd_MS_SelfCount,

    .articles = ftDd_ItemLogic,
    .article_count = 3,
};

#undef DD_ITEM_EVENT
