/* Akaneia's Charizard: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpLz.dat).
 *
 * Kirby's Charizard ability is Charizard's Flamethrower (ftlz_specialn.c) compiled again with four
 * things changed: the state changes go through KirbyStateChange (the animations and scripts come
 * from the hat file), the parameters are a block inside the hat file's own code (ftKbLz_Params)
 * where Charizard reads his attributes, the two flame reserves live in Kirby's fighter variables
 * at fp+0x2274 and fp+0x2278 (ftKbLz_Vars) where Charizard has fp+0x222C and fp+0x2230, and the
 * flame is Charizard's item 3 (item kind 265 on Akaneia, data in the hat file) spawned from
 * Kirby's part 44 where Charizard uses his part 27. The flame's spawn and setup routines and the
 * three effect tables are the same words as Charizard's own, so itLzFire_Spawn and the tables of
 * ftlz_specialn.c serve both.
 *
 * This hat file is the only one of the seven with export 7, the per-frame slot the engine calls
 * from Kirby's entry of the fighters' per-frame table (ftKb_Init_UnkMotionStates3, m-ex hook at
 * 800F1B94; mu_ak_kirby_frame natively): it refills the reserves, as Charizard's own onframe does.
 *
 * The hat file has no debug symbols. Each function names the offset of the routine it was written
 * from in the hat file's code block (run-source/rel09-b1-wolf/kirby/listings/PlKbCpLz.listing.txt
 * shows it at 0x81800000 plus the offset) and its size in words. The code has no fixed console
 * address: m-ex loads it with the hat file. Four ranges that listing names as functions are data:
 * +0x2B8 to +0x2E0 (the parameters; the entry routine starts at +0x2E4), +0xD80, +0xE00 and
 * +0x10CC (the three effect tables).
 *
 * Nothing below keeps state outside Kirby's Fighter; the `logged` counter only limits the log. */
#include "ftlizardon.h"

#include <dolphin/os.h>

#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 405). They
 * mirror Charizard's own states 343 to 348. */
enum {
    ftKbLz_State_SpecialNStart,    /* 0 */
    ftKbLz_State_SpecialN,         /* 1: the loop */
    ftKbLz_State_SpecialNEnd,      /* 2 */
    ftKbLz_State_SpecialAirNStart, /* 3 */
    ftKbLz_State_SpecialAirN,      /* 4: the loop */
    ftKbLz_State_SpecialAirNEnd,   /* 5 */
    ftKbLz_State_Count
};

/* Kirby's motion id in state 0 of an added ability (KirbyStateChange gives 400 + state). */
#define FTKBLZ_MOTION_BASE 400

/* The flame sounds Kirby plays (complete ids, bank 33: they pass the layer's sound rule
 * unchanged) and the ones the same code plays for any other fighter kind, which cannot happen
 * here. Both sets are in the hat file's code, as in Charizard's own. */
#define FTKBLZ_SFX_FIRE_HIGH 0x13EF
#define FTKBLZ_SFX_FIRE_MID 0x13F2
#define FTKBLZ_SFX_FIRE_LOW 0x13F5
#define FTKBLZ_SFX_KB_FIRE_HIGH 0x50910
#define FTKBLZ_SFX_KB_FIRE_MID 0x50913
#define FTKBLZ_SFX_KB_FIRE_LOW 0x50916

#define FTKBLZ_LOG 4 /* evidence lines that go in the log, per run */

/* +0x2B8 (11 words of data): the parameters. Charizard's own attributes have the same values in
 * the same order (ftLz_DatAttrs +0x14 to +0x3C) except the last, the part the flames leave from. */
static const ftKbLz_Params ftKbLz_ParamBlock = {
    .specialn_min_frames = 40,
    .fire_speed_regen = 0.7F,
    .fire_size_regen = 0.7F,
    .fire_speed_max = 360.0F,
    .fire_speed_min = 40.0F,
    .fire_size_max = 380.0F,
    .fire_size_min = 60.0F,
    .specialn_quake_period = 30,
    .fire_offset_x = 0.0F,
    .fire_offset_y = 0.0F,
    .fire_part = 44,
};

/* [OnKirbySwallow] +0x000 (63 words): put the hat on (the retail hat attach without the x2225_b2
 * write, skipped when the hat is already on) and fill both reserves. */
static void ftKbLz_OnSwallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKbLz_FighterVars* fv = ftKbLz_Vars(fp);

    mu_ak_kirby_attach_hat(gobj);
    fv->params = &ftKbLz_ParamBlock;
    fv->fire_speed = ftKbLz_ParamBlock.fire_speed_max;
    fv->fire_size = ftKbLz_ParamBlock.fire_size_max;
}

/* [OnKirbyLoseAbility] +0x0FC (23 words): take the hat off. Instruction for instruction the retail
 * ftKb_SpecialN_800EFAF0. */
static void ftKbLz_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [OnKirbyHurt] +0x168 (1 word): empty. */
static void ftKbLz_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x16C (3 words): the hat data's first extra word (+0xC) is the article data of
 * Charizard's item 3. */
static void ftKbLz_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), ftLz_Item_KirbyFire);
}

/* [OnFrame] +0x238 (32 words), export 7, Charizard's "RefuelFire": outside the first five states
 * of the ability (motion ids 400 to 404) the reserves refill. Like Charizard's, the air end state
 * already refills. */
static void ftKbLz_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftKbLz_Params* p = &ftKbLz_ParamBlock;
    ftKbLz_FighterVars* fv;

    if ((u32) (fp->motion_id - FTKBLZ_MOTION_BASE) <= 4) {
        return;
    }
    fv = ftKbLz_Vars(fp);
    fv->fire_speed += p->fire_speed_regen;
    if (fv->fire_speed > p->fire_speed_max) {
        fv->fire_speed = p->fire_speed_max;
    }
    fv->fire_size += p->fire_size_regen;
    if (fv->fire_size > p->fire_size_max) {
        fv->fire_size = p->fire_size_max;
    }
}

/* +0x2E4 (31 words), Charizard's "SpecialN_Enter": [SpecialN] +0x158 and [SpecialAirN] +0x160
 * load state 0 or 3 and branch here. KirbyStateChange(gobj, state, 0, NULL, 0.0, 1.0, 0.0), then
 * the motion variables. */
static void ftKbLz_SpecialN_EnterState(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    mu_ak_kirby_state_change(gobj, state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    mv->specialn.fire_cycle = 0;
    mv->specialn.hit_id = Item_8026AE60();
    mv->specialn.gfx = 0;
    mv->specialn.frames = 0;
    mv->specialn.sfx_cycle = 0;
    mv->specialn.quake_timer = 0;
    mv->specialn.loops_left = 1;
}

/* [SpecialN] +0x158 (2 words) */
static void ftKbLz_SpecialN_Enter(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_EnterState(gobj, ftKbLz_State_SpecialNStart);
}

/* [SpecialAirN] +0x160 (2 words) */
static void ftKbLz_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_EnterState(gobj, ftKbLz_State_SpecialAirNStart);
}

/* +0xACC (173 words), Charizard's "SpecialN_SpawnFire": one flame from Kirby's part, then the
 * batch hit id and the sound. The flame itself (+0xE80, 147 words, with its setup +0x117C and its
 * first physics step +0x1258) is the same instructions as Charizard's "SpawnItem_Fire":
 * itLzFire_Spawn. */
static void ftKbLz_SpawnFire(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftKbLz_Params* p = &ftKbLz_ParamBlock;
    ftKbLz_FighterVars* fv = ftKbLz_Vars(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);
    Vec3 pos;
    int gfx;
    int kind;
    float speed_scale;
    float size_scale;

    lb_8000B1CC(fp->parts[p->fire_part].joint, NULL, &pos);
    pos.x += fp->x34_scale.y * p->fire_offset_x * fp->facing_dir;
    pos.y += fp->x34_scale.y * p->fire_offset_y;

    switch (mv->specialn.gfx) {
    case 1:
    case 2:
        gfx = ftLz_FireGfxA[HSD_Randi(32)];
        break;
    default:
        if (ftLz_FireFlip[HSD_Randi(32)] == 0) {
            gfx = ftLz_FireGfxA[HSD_Randi(32)];
        } else {
            gfx = ftLz_FireGfxB[HSD_Randi(32)];
        }
        break;
    }

    speed_scale = fv->fire_speed / p->fire_speed_max;
    size_scale = fv->fire_size / p->fire_size_max;
    mv->specialn.gfx = gfx;
    kind = mu_ak_article_kind(gobj, ftLz_Item_KirbyFire);
    if (kind < 0) {
        /* Not an m-ex case: no item kind for the article. */
        OSReport("[ak] Kirby (Charizard): no item kind for the flame article (copied kind %d)\n",
                 (int) fp->u.kb.hat.kind);
    } else {
        static int logged;
        HSD_GObj* flame = itLzFire_Spawn(gobj, &pos, mv->specialn.hit_id, gfx, kind,
                                         fp->facing_dir, speed_scale, size_scale);
        if (logged < FTKBLZ_LOG) {
            logged++;
            OSReport("[ak] Kirby (Charizard): flame %s, item kind %d, gfx %d, at (%.2f, %.2f), "
                     "speed %.3f size %.3f\n",
                     flame != NULL ? "spawned" : "NOT spawned", kind, gfx, pos.x, pos.y,
                     speed_scale, size_scale);
        }
    }

    if (mv->specialn.sfx_cycle == 0) {
        mv->specialn.hit_id = Item_8026AE60();
        ft_80089824(gobj);
        ft_800892A0(gobj);
    }
    if (mv->specialn.sfx_cycle % 3 == 0) {
        float f = (fv->fire_size - p->fire_size_min) / (p->fire_size_max - p->fire_size_min);
        bool kirby = fp->kind == Ft_Kind_Kirby;
        int sfx;
        /* The console tests "!(f >= x)", so a NaN counts as low. */
        if (!(f >= 0.33)) {
            sfx = kirby ? FTKBLZ_SFX_KB_FIRE_LOW : FTKBLZ_SFX_FIRE_LOW;
        } else if (!(f >= 0.66)) {
            sfx = kirby ? FTKBLZ_SFX_KB_FIRE_MID : FTKBLZ_SFX_FIRE_MID;
        } else {
            sfx = kirby ? FTKBLZ_SFX_KB_FIRE_HIGH : FTKBLZ_SFX_FIRE_HIGH;
        }
        ft_80088478(fp, sfx, 0x7F, 0x40);
    }
    /* The console's division here also folds in the sign of the speed reserve taken as an
     * integer; the reserve never goes below fire_speed_min (40), so it is the plain remainder. */
    mv->specialn.sfx_cycle = (mv->specialn.sfx_cycle + 1) % 12;
}

/* +0x97C (84 words), Charizard's "SpecialN_Loop", the loop states' IASA: keep breathing while B
 * is held or the first loop is not done, else go to the end state. */
static void ftKbLz_SpecialN_Loop(HSD_GObj* gobj, int end_state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftKbLz_Params* p = &ftKbLz_ParamBlock;
    ftKbLz_FighterVars* fv = ftKbLz_Vars(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    if (mv->specialn.frames < p->specialn_min_frames) {
        if (mv->specialn.fire_cycle == 0) {
            ftKbLz_SpawnFire(gobj);
        }
    } else if ((fp->input.held_buttons[0] & HSD_PAD_B) || mv->specialn.loops_left != 0) {
        if (mv->specialn.fire_cycle == 0) {
            ftKbLz_SpawnFire(gobj);
        }
    } else {
        mu_ak_kirby_state_change(gobj, end_state, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }

    mv->specialn.fire_cycle += 1;
    if (mv->specialn.fire_cycle > 2) {
        mv->specialn.fire_cycle = 0;
    }
    fv->fire_speed -= 1.0F;
    if (fv->fire_speed < p->fire_speed_min) {
        fv->fire_speed = p->fire_speed_min;
    }
    fv->fire_size -= 1.0F;
    if (fv->fire_size < p->fire_size_min) {
        fv->fire_size = p->fire_size_min;
    }
    mv->specialn.frames += 1;
    if (mv->specialn.frames > p->specialn_min_frames) {
        mv->specialn.frames = p->specialn_min_frames;
    }
}

/* +0x3D4, +0x500, +0x5DC (34 words each): leaving the ground keeps the frame and goes to the air
 * state. */
static void ftKbLz_SpecialN_GroundToAir(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj) == GA_Ground) {
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, state, ftCommon_GroundAirColl_MF, fp->cur_anim_frame, 1.0F,
                                 0.0F);
    }
}

/* +0x6D8, +0x804, +0x8E0 (34 words each): landing keeps the frame and goes to the ground state. */
static void ftKbLz_SpecialN_AirToGround(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj) != GA_Ground) {
        ftCommon_8007D7FC(fp);
        mu_ak_kirby_state_change(gobj, state, ftCommon_GroundAirColl_MF, fp->cur_anim_frame, 1.0F,
                                 0.0F);
    }
}

/* ---- state 0, SpecialNStart ---- */

/* [state 0 Anim] +0x360 (27 words) */
static void ftKbLz_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        mu_ak_kirby_state_change(gobj, ftKbLz_State_SpecialN, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }
}

/* [state 0 Coll] +0x3D4 */
static void ftKbLz_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_GroundToAir(gobj, ftKbLz_State_SpecialAirNStart);
}

/* ---- state 1, SpecialN ---- */

/* [state 1 Anim] +0x45C and [state 4 Anim] +0x760 (38 words each, the same code), Charizard's
 * "SpecialN_Loop_AnimCB": the minimum-loop countdown and a small camera rumble every
 * specialn_quake_period frames. */
static void ftKbLz_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftKbLz_Params* p = &ftKbLz_ParamBlock;
    ftLz_MotionVars* mv = ftLz_MV(fp);

    if (fp->cur_anim_frame == 0.0F) {
        int left = mv->specialn.loops_left - 1;
        mv->specialn.loops_left = left <= 0 ? 0 : left;
    }
    if (mv->specialn.quake_timer == 0) {
        Camera_RequestQuake(QuakeKind_Small, &fp->cur_pos);
    }
    mv->specialn.quake_timer += 1;
    if (mv->specialn.quake_timer > p->specialn_quake_period) {
        mv->specialn.quake_timer = 0;
    }
}

/* [state 1 IASA] +0x4F4 (2 words) */
static void ftKbLz_SpecialN_IASA(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_Loop(gobj, ftKbLz_State_SpecialNEnd);
}

/* [state 1 Coll] +0x500 */
static void ftKbLz_SpecialN_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_GroundToAir(gobj, ftKbLz_State_SpecialAirN);
}

/* ---- state 2, SpecialNEnd ---- */

/* [state 2 Coll] +0x5DC */
static void ftKbLz_SpecialNEnd_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_GroundToAir(gobj, ftKbLz_State_SpecialAirNEnd);
}

/* ---- state 3, SpecialAirNStart ---- */

/* [state 3 Anim] +0x664 (27 words) */
static void ftKbLz_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        mu_ak_kirby_state_change(gobj, ftKbLz_State_SpecialAirN, Ft_MF_None, 0.0F, 1.0F, 0.0F);
    }
}

/* [state 3 Coll] +0x6D8 */
static void ftKbLz_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_AirToGround(gobj, ftKbLz_State_SpecialNStart);
}

/* ---- state 4, SpecialAirN ---- */

/* [state 4 IASA] +0x7F8 (2 words) */
static void ftKbLz_SpecialAirN_IASA(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_Loop(gobj, ftKbLz_State_SpecialAirNEnd);
}

/* [state 4 Coll] +0x804 */
static void ftKbLz_SpecialAirN_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_AirToGround(gobj, ftKbLz_State_SpecialN);
}

/* ---- state 5, SpecialAirNEnd ---- */

/* [state 5 Coll] +0x8E0 */
static void ftKbLz_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftKbLz_SpecialN_AirToGround(gobj, ftKbLz_State_SpecialNEnd);
}

/* [move_logic] +0x178 (six entries of 0x20 bytes). Words +4 and +8 are Charizard's own (move id
 * 0x12); KirbyStateChange does not read them. The callbacks that are the same instructions as
 * Charizard's own are Charizard's functions:
 *   [state 0, 2, 3, 5 IASA] +0x3CC, +0x5D4, +0x6D0, +0x8D8, a lone return;
 *   [state 0, 1, 2 Phys] +0x3D0, +0x4FC, +0x5D8, a branch to ft_80084F3C;
 *   [state 3, 4, 5 Phys] +0x6D4, +0x800, +0x8DC, a branch to ft_80084DB0;
 *   [state 2 Anim] +0x588 (19 words), to ft_8008A2BC at the end of the animation;
 *   [state 5 Anim] +0x88C (19 words), to ftCo_Fall_Enter at the end of the animation. */
static const MotionState ftKbLz_MotionStateTable[ftKbLz_State_Count] = {
    {
        /* state 0, ftcmd 0 "LzSpecialNStart" */
        0,
        0x00340011,
        0x12 << 24,
        ftKbLz_SpecialNStart_Anim,
        ftLz_SpecialNStart_IASA,
        ftLz_SpecialNStart_Phys,
        ftKbLz_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "LzSpecialN" */
        1,
        0x003C0011,
        0x12 << 24,
        ftKbLz_SpecialN_Anim,
        ftKbLz_SpecialN_IASA,
        ftLz_SpecialN_Phys,
        ftKbLz_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2, ftcmd 2 "LzSpecialNEnd" */
        2,
        0x00340011,
        0x12 << 24,
        ftLz_SpecialNEnd_Anim,
        ftLz_SpecialNEnd_IASA,
        ftLz_SpecialNEnd_Phys,
        ftKbLz_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3, ftcmd 3 "LzSpecialAirNStart" */
        3,
        0x00340411,
        0x12 << 24,
        ftKbLz_SpecialAirNStart_Anim,
        ftLz_SpecialAirNStart_IASA,
        ftLz_SpecialAirNStart_Phys,
        ftKbLz_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 4, ftcmd 4 "LzSpecialAirN" */
        4,
        0x003C0411,
        0x12 << 24,
        ftKbLz_SpecialN_Anim,
        ftKbLz_SpecialAirN_IASA,
        ftLz_SpecialAirN_Phys,
        ftKbLz_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 5, ftcmd 5 "LzSpecialAirNEnd" */
        5,
        0x00340411,
        0x12 << 24,
        ftLz_SpecialAirNEnd_Anim,
        ftLz_SpecialAirNEnd_IASA,
        ftLz_SpecialAirNEnd_Phys,
        ftKbLz_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpLz.dat (0 to 7). */
const MuAkKirbyCopy ftKbLz_Copy = {
    .on_swallow = ftKbLz_OnSwallow,
    .on_lose = ftKbLz_OnLose,
    .special_n = ftKbLz_SpecialN_Enter,
    .special_air_n = ftKbLz_SpecialAirN_Enter,
    .on_hurt = ftKbLz_OnHurt,
    .init_items = ftKbLz_InitCopyItems,
    .move_logic = ftKbLz_MotionStateTable,
    .move_logic_count = ftKbLz_State_Count,
    .on_frame = ftKbLz_OnFrame,
};
