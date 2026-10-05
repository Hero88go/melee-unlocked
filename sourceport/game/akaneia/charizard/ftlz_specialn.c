/* Charizard Neutral B, Flamethrower (states 343-348).
 *
 * Bowser's Fire Breath (ftkoopaspecialn.c) move for move: a start, a loop that breathes a flame
 * item every third frame while B is held (at least specialn_min_frames), and an end. Two reserves in
 * the fighter variables shrink while breathing and refill otherwise; they scale the flames' speed
 * and size. The flame gfx variant is picked with Bowser's own random table. */
#include "ftlizardon.h"

#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

/* Flame gfx variants (PlLz .rodata fire_effect_table1..3), the same values as Bowser's
 * ftKp_Init_803CF2A0: 32 entries each. Kirby's copy of the move (charizard_kirby.c) has the same
 * three tables in its own code block and reads these. */
int const ftLz_FireFlip[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
int const ftLz_FireGfxA[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3,
    0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3,
};
int const ftLz_FireGfxB[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
};

/* Flame sound per reserve third; Kirby (kind 4) has its own copies of Charizard's sounds. */
#define FTLZ_SFX_FIRE_HIGH 0x13EF
#define FTLZ_SFX_FIRE_MID 0x13F2
#define FTLZ_SFX_FIRE_LOW 0x13F5
#define FTLZ_SFX_KB_FIRE_HIGH 0x50910
#define FTLZ_SFX_KB_FIRE_MID 0x50913
#define FTLZ_SFX_KB_FIRE_LOW 0x50916

static void ftLz_SpecialN_ResetVars(Fighter* fp)
{
    ftLz_MotionVars* mv = ftLz_MV(fp);

    mv->specialn.fire_cycle = 0;
    mv->specialn.hit_id = Item_8026AE60();
    mv->specialn.gfx = 0;
    mv->specialn.frames = 0;
    mv->specialn.loops_left = 1;
    mv->specialn.sfx_cycle = 0;
    mv->specialn.quake_timer = 0;
}

/* PlLz SpecialN_Enter, shared by the ground and air entries. */
static void ftLz_SpecialN_EnterState(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialN_ResetVars(GET_FIGHTER(gobj));
}

/* m-ex "specialn". */
void ftLz_SpecialN_Enter(HSD_GObj* gobj)
{
    ftLz_SpecialN_EnterState(gobj, ftLz_MS_SpecialNStart);
}

/* m-ex "specialairn". */
void ftLz_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftLz_SpecialN_EnterState(gobj, ftLz_MS_SpecialAirNStart);
}

/* PlLz RefuelFire (m-ex onframe): outside the first five Flamethrower states the reserves refill.
 * Like Bowser, the air end state (348) already refills. */
void ftLz_SpecialN_RefuelFire(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da;
    ftLz_FighterVars* fv;

    if ((u32) (fp->motion_id - ftLz_MS_SpecialNStart) <= 4) {
        return;
    }
    da = ftLz_Attrs(fp);
    fv = ftLz_Vars(fp);
    fv->fire_speed += da->fire_speed_regen;
    if (fv->fire_speed > da->fire_speed_max) {
        fv->fire_speed = da->fire_speed_max;
    }
    fv->fire_size += da->fire_size_regen;
    if (fv->fire_size > da->fire_size_max) {
        fv->fire_size = da->fire_size_max;
    }
}

/* PlLz SpecialN_SpawnFire: one flame from the mouth bone, then the batch hit id and the sound. */
static void ftLz_SpecialN_SpawnFire(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_FighterVars* fv = ftLz_Vars(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);
    Vec3 pos;
    int gfx;
    float speed_scale;
    float size_scale;

    lb_8000B1CC(fp->parts[da->fire_part].joint, NULL, &pos);
    pos.x += fp->x34_scale.y * da->fire_offset_x * fp->facing_dir;
    pos.y += fp->x34_scale.y * da->fire_offset_y;

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
    mv->specialn.gfx = gfx;

    speed_scale = fv->fire_speed / da->fire_speed_max;
    size_scale = fv->fire_size / da->fire_size_max;
    itLzFire_Spawn(gobj, &pos, mv->specialn.hit_id, gfx,
                   mu_ak_article_kind(gobj, ftLz_Item_Fire), fp->facing_dir, speed_scale,
                   size_scale);

    if (mv->specialn.sfx_cycle == 0) {
        mv->specialn.hit_id = Item_8026AE60();
        ft_80089824(gobj);
        ft_800892A0(gobj);
    }
    if (mv->specialn.sfx_cycle % 3 == 0) {
        float f = (fv->fire_size - da->fire_size_min) / (da->fire_size_max - da->fire_size_min);
        bool kirby = fp->kind == Ft_Kind_Kirby;
        int sfx;
        /* The console tests "!(f >= x)", so a NaN counts as low. */
        if (!(f >= 0.33)) {
            sfx = kirby ? FTLZ_SFX_KB_FIRE_LOW : FTLZ_SFX_FIRE_LOW;
        } else if (!(f >= 0.66)) {
            sfx = kirby ? FTLZ_SFX_KB_FIRE_MID : FTLZ_SFX_FIRE_MID;
        } else {
            sfx = kirby ? FTLZ_SFX_KB_FIRE_HIGH : FTLZ_SFX_FIRE_HIGH;
        }
        ft_80088478(fp, sfx, 0x7F, 0x40);
    }
    mv->specialn.sfx_cycle = (mv->specialn.sfx_cycle + 1) % 12;
}

/* PlLz SpecialN_Loop, the loop states' IASA: keep breathing while B is held or the first loop is
 * not done, else go to @p end_msid. */
static void ftLz_SpecialN_Loop(HSD_GObj* gobj, FtMotionId end_msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_FighterVars* fv = ftLz_Vars(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    if (mv->specialn.frames < da->specialn_min_frames) {
        if (mv->specialn.fire_cycle == 0) {
            ftLz_SpecialN_SpawnFire(gobj);
        }
    } else if ((fp->input.held_buttons[0] & HSD_PAD_B) || mv->specialn.loops_left != 0) {
        if (mv->specialn.fire_cycle == 0) {
            ftLz_SpecialN_SpawnFire(gobj);
        }
    } else {
        Fighter_ChangeMotionState(gobj, end_msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    }

    mv->specialn.fire_cycle += 1;
    if (mv->specialn.fire_cycle > 2) {
        mv->specialn.fire_cycle = 0;
    }
    fv->fire_speed -= 1.0F;
    if (fv->fire_speed < da->fire_speed_min) {
        fv->fire_speed = da->fire_speed_min;
    }
    fv->fire_size -= 1.0F;
    if (fv->fire_size < da->fire_size_min) {
        fv->fire_size = da->fire_size_min;
    }
    mv->specialn.frames += 1;
    if (mv->specialn.frames > da->specialn_min_frames) {
        mv->specialn.frames = da->specialn_min_frames;
    }
}

/* Ground to air and back keep the frame, as in Bowser's move. */
static void ftLz_SpecialN_GroundToAir(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj) == GA_Ground) {
        ftCommon_GroundToAirStateChange(gobj, fp, msid, ftCommon_GroundAirColl_MF);
    }
}

static void ftLz_SpecialN_AirToGround(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj) != GA_Ground) {
        ftCommon_AirToGroundStateChange(gobj, fp, msid, ftCommon_GroundAirColl_MF);
    }
}

/* PlLz SpecialN_Loop_AnimCB, shared by both loops: the minimum-loop countdown and a small camera
 * rumble every specialn_quake_period frames. */
void ftLz_SpecialN_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    if (fp->cur_anim_frame == 0.0F) {
        int left = mv->specialn.loops_left - 1;
        mv->specialn.loops_left = left <= 0 ? 0 : left;
    }
    if (mv->specialn.quake_timer == 0) {
        Camera_RequestQuake(QuakeKind_Small, &fp->cur_pos);
    }
    mv->specialn.quake_timer += 1;
    if (mv->specialn.quake_timer > da->specialn_quake_period) {
        mv->specialn.quake_timer = 0;
    }
}

/* ---- 343 SpecialNStart ---- */

void ftLz_SpecialNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialN, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    }
}

void ftLz_SpecialNStart_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialNStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftLz_SpecialNStart_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_GroundToAir(gobj, ftLz_MS_SpecialAirNStart);
}

/* ---- 344 SpecialN ---- */

void ftLz_SpecialN_IASA(HSD_GObj* gobj)
{
    ftLz_SpecialN_Loop(gobj, ftLz_MS_SpecialNEnd);
}

void ftLz_SpecialN_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftLz_SpecialN_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_GroundToAir(gobj, ftLz_MS_SpecialAirN);
}

/* ---- 345 SpecialNEnd ---- */

void ftLz_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftLz_SpecialNEnd_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialNEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftLz_SpecialNEnd_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_GroundToAir(gobj, ftLz_MS_SpecialAirNEnd);
}

/* ---- 346 SpecialAirNStart ---- */

void ftLz_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialAirN, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                                  NULL);
    }
}

void ftLz_SpecialAirNStart_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirNStart_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

void ftLz_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_AirToGround(gobj, ftLz_MS_SpecialNStart);
}

/* ---- 347 SpecialAirN ---- */

void ftLz_SpecialAirN_Anim(HSD_GObj* gobj)
{
    ftLz_SpecialN_Anim(gobj);
}

void ftLz_SpecialAirN_IASA(HSD_GObj* gobj)
{
    ftLz_SpecialN_Loop(gobj, ftLz_MS_SpecialAirNEnd);
}

void ftLz_SpecialAirN_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

void ftLz_SpecialAirN_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_AirToGround(gobj, ftLz_MS_SpecialN);
}

/* ---- 348 SpecialAirNEnd ---- */

void ftLz_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftLz_SpecialAirNEnd_IASA(HSD_GObj* gobj) {}

void ftLz_SpecialAirNEnd_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

void ftLz_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialN_AirToGround(gobj, ftLz_MS_SpecialNEnd);
}
