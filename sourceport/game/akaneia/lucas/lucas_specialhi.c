/* Akaneia's Lucas: up special, PK Thunder. Start spawns the head (article 3, lucas_it_pkthunder.c);
 * Hold steers it and watches for it coming back to Lucas; when it touches him he launches as PK
 * Thunder 2 (SpecialHi / SpecialAirHi), which can bounce off walls and ceilings (SpecialHiBound).
 * Same shape as Ness's ftnessspecialhi.c, rewritten by the m-ex authors; the behavior below is
 * the disc's, including where it differs from Ness. */
#include "lucas.h"

#include <math.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownBound.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>

#define LC_PI 3.14159265358979323846
#define LC_HALF_PI 1.5707963267948966
#define LC_TWO_PI 6.283185307179586
#define LC_DEG_TO_RAD 0.017453292 /* .rodata.cst8+0x20 as the disc rounds it (double) */
#define LC_DEG_TO_RAD_B 0.0174533 /* .rodata.cst8+0x8 */

/* code+0x4648 "Lucas_DestroySpecialHiEffects": clear the PK Thunder effects while in any of the
 * nine PK Thunder states. */
void ftLc_SpecialHi_DestroyGfx(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if ((u32) (fp->motion_id - ftLc_MS_SpecialHiStart) <= 8) {
        efLib_DestroyAll(gobj);
        ftLc_Vars(fp)->pkthunder_gfx = 0;
    }
}

/* code+0x5AD4 "Lucas_RemovePKThunderGOBJ". The disc checks the PSI Magnet range (359-367) for
 * the effect cleanup here, not the PK Thunder range; kept as shipped. */
void ftLc_RemovePKThunder(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    if (vars->pkthunder_gobj != NULL) {
        Item_8026A8EC(vars->pkthunder_gobj);
        vars->pkthunder_gobj = NULL;
    }
    if ((u32) (fp->motion_id - ftLc_MS_SpecialLwStart) <= 8) {
        efLib_DestroyAll(gobj);
        vars->pkthunder_gfx = 0;
    }
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
}

/* code+0x3A58 "Init_SpecialHi" */
static void ftLc_SpecialHi_Init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    mv->specialhi.state = 1;
    mv->specialhi.loop1 = attrs->x58_PKT_LOOP1;
    mv->specialhi.loop2 = attrs->x5C_PKT_LOOP2;
    mv->specialhi.gravity_delay = attrs->x60_PKT_GRAVITY_DELAY;
    mv->specialhi.head_pos0.x = 0.0f;
    mv->specialhi.head_pos0.y = 0.0f;
    mv->specialhi.head_pos1.x = 0.0f;
    mv->specialhi.head_pos1.y = 0.0f;
    mv->specialhi.angle = fp->facing_dir == 1.0f ? 0.0f : (float) LC_PI;
    mv->specialhi.x236C = 0.0f;
    mv->specialhi.dir_y = 1.0f;
    mv->specialhi.saved_vel.x = 0.0f;
    mv->specialhi.saved_vel.y = 0.0f;
    mv->specialhi.saved_vel.z = 0.0f;
    mv->specialhi.frame = 0;
    mv->specialhi.drop.x = 0.0f;
    mv->specialhi.drop.y = 0.0f;
    mv->specialhi.drop.z = 0.0f;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
}

/* m-ex specialhi, code+0x6BC */
void ftLc_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftLc_SpecialHi_Init(gobj);
    ftAnim_8006EBA4(gobj);
}

/* m-ex specialairhi, code+0x718 */
void ftLc_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftLc_SpecialHi_Init(gobj);
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

void ftLc_SpecialHi_NoIASA(HSD_GObj* gobj) {}

/* code+0x4544 "SpecialHi_Start_Anim_Shared": spawn the head at the end of the start animation. */
static void ftLc_SpecialHiStart_AnimShared(HSD_GObj* gobj, FtMotionId next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, next, 0, 0.0f, 1.0f, 0.0f, NULL);
    if (vars->pkthunder_gobj == NULL) {
        Vec3 pos;
        lb_8000B1CC(fp->parts[ftLc_Part_Head].joint, NULL, &pos);
        pos.z = 0.0f;
        vars->pkthunder_gobj = ftLc_PKThunder_Spawn(gobj, &pos, fp->facing_dir);
        if (vars->pkthunder_gobj != NULL) {
            fp->death2_cb = ftLc_RemoveAllArticles;
            fp->take_dmg_cb = ftLc_RemoveAllArticles;
        }
    }
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
    ftLc_SpecialHi_DestroyGfx(gobj);
    vars->pkthunder_gfx = 1;
    efSync_Spawn(0x1388, gobj, fp->parts[ftLc_Part_Hip].joint);
}

/* code+0x4690 "SpecialHi_Hold_ProcessAnimTimers" */
static void ftLc_SpecialHiHold_Timers(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->specialhi.loop1 > 0) {
        mv->specialhi.loop1--;
    }
    if (ftLc_Vars(fp)->pkthunder_gobj == NULL && mv->specialhi.loop2 > 0) {
        mv->specialhi.loop2--;
    }
}

/* code+0x46CC "Lucas_CheckForPKTCollision": 1 once the head, having first left the box around
 * Lucas, comes back into it. */
static int ftLc_SpecialHi_CheckHeadHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    Item_GObj* head = ftLc_Vars(fp)->pkthunder_gobj;
    float px, py, dx, dy;
    Vec3 head_pos;

    if (head == NULL) {
        return 0;
    }
    px = fp->cur_pos.x;
    py = (fp->cur_pos.y + 3.0f) + fp->x34_scale.y * 5.0f;
    ftLc_ThunderHead_GetPosition(head, &head_pos, 0);

    switch (mv->specialhi.state) {
    case 0:
        dx = px - head_pos.x;
        if (dx < 0.0f) {
            dx = -dx;
        }
        if (dx >= 7.333333) {
            return 0;
        }
        dy = py - head_pos.y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < 12.333333) {
            mv->specialhi.state = 2;
            ftLc_ThunderHead_GetPosition(head, &mv->specialhi.head_pos0, 0);
            ftLc_ThunderHead_GetPosition(head, &mv->specialhi.head_pos1, 1);
            return 1;
        }
        return 0;
    case 1:
        dx = px - head_pos.x;
        if (dx < 0.0f) {
            dx = -dx;
        }
        if (dx < 7.333333) {
            return 0;
        }
        dy = py - head_pos.y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < 12.333333) {
            return 0;
        }
        mv->specialhi.state = 0;
        return 0;
    default:
        return 0;
    }
}

/* code+0x61DC "AS_LucasPKHit": launch as PK Thunder 2 along `dir`. */
static void ftLc_SpecialHi_Launch(HSD_GObj* gobj, Vec3* dir, bool airborne)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    fp->facing_dir = ftLc_Sign(dir->x);
    mv->specialhi.dir_y = ftLc_Sign(dir->y);
    mv->specialhi.angle = atan2f(dir->y, dir->x);
    mv->specialhi.x236C = attrs->x70_PKT2_UNK;
    if (airborne) {
        ftCommon_8007D60C(fp);
        fp->self_vel.x = attrs->x6C_PKT2_SPEED * cosf(mv->specialhi.angle);
        fp->self_vel.y = attrs->x6C_PKT2_SPEED * sinf(mv->specialhi.angle);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHi, 0, 0.0f, 1.0f, 0.0f, NULL);
    } else {
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHi, 0, 0.0f, 1.0f, 0.0f, NULL);
        fp->gr_vel = attrs->x6C_PKT2_SPEED * fp->facing_dir;
    }
    ftPartSetRotX(fp, 0,
                  (float) (atan2f(fp->self_vel.x, fp->self_vel.y) * fp->facing_dir - LC_HALF_PI));
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* The launch direction: from the head's position when it hit to Lucas's chest. */
static void ftLc_SpecialHi_HitDir(Fighter* fp, Vec3* dir)
{
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    dir->x = fp->cur_pos.x - mv->specialhi.head_pos0.x;
    dir->y = fp->x34_scale.y * 5.0f + fp->cur_pos.y - mv->specialhi.head_pos0.y;
    dir->z = 0.0f;
}

/* code+0x48F8 "Fighter_EnterSpecialHi_Grounded": on the ground, a launch into the floor either
 * slides along it or knocks him down. */
static void ftLc_SpecialHi_HitGrounded(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    Vec3 dir;

    ftLc_SpecialHi_HitDir(fp, &dir);
    /* floor.flags 0x100 (console byte fp+0x842 bit 0): a pass-through platform. */
    if (!(fp->coll_data.floor.flags & 0x100)) {
        float angle = lbVector_Angle(&fp->coll_data.floor.normal, &dir);
        if (!(angle < LC_HALF_PI || LC_ISNAN(angle))) {
            if (angle > (attrs->x78_PKT2_KNOCKDOWN_ANGLE + 90.0f) * LC_DEG_TO_RAD_B ||
                LC_ISNAN(angle))
            {
                ftLc_SpecialHi_DestroyGfx(gobj);
                ftPartSetRotX(fp, 0, 0.0f);
                ftCo_80097D40(gobj);
                return;
            }
            ftLc_SpecialHi_Launch(gobj, &dir, false);
            return;
        }
    }
    ftLc_SpecialHi_Launch(gobj, &dir, true);
}

/* code+0x4AC4 "Fighter_EnterSpecialHi_Airborne" */
static void ftLc_SpecialHi_HitAirborne(HSD_GObj* gobj)
{
    Vec3 dir;
    ftLc_SpecialHi_HitDir(GET_FIGHTER(gobj), &dir);
    ftLc_SpecialHi_Launch(gobj, &dir, true);
}

/* code+0x4A1C "SpecialHi_Anim_CheckSpawnEffect": the PK Thunder 2 body effect, first frame only. */
static void ftLc_SpecialHi_StartGfx(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    mv->specialhi.frame++;
    if (mv->specialhi.frame == 1) {
        ftLc_SpecialHi_DestroyGfx(gobj);
        efSync_Spawn(0x1389, gobj, fp->parts[ftLc_Part_Hip].joint);
        ftLc_Vars(fp)->pkthunder_gfx = 1;
    }
}

/* code+0x6EE0 "ClampRotation": wrap into [0, 2pi]. */
static float ftLc_WrapAngle(float a)
{
    if (a < 0.0f) {
        do {
            a = (float) (a + LC_TWO_PI);
        } while (a < 0.0f);
    }
    while (a > LC_TWO_PI) {
        a = (float) (a - LC_TWO_PI);
    }
    return a;
}

/* code+0x6384 "SpecialHi_CalculateRotationOffWall": turn the launch so it runs along the wall
 * he is hugging (or by `angle` when no wall is flagged). */
static void ftLc_SpecialHi_SlideOffWall(HSD_GObj* gobj, CollData* coll, float angle)
{
    static Vec3 unit_z = { 0.0f, 0.0f, 1.0f }; /* .data.unit_z.0 */
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    float target = angle;

    mv->specialhi.angle = ftLc_WrapAngle(mv->specialhi.angle);
    if (coll->env_flags & Collide_LeftWallMask) {
        float wall = ftLc_WrapAngle(atan2f(coll->left_facing_wall.normal.y,
                                           coll->left_facing_wall.normal.x));
        float d = ftLc_WrapAngle((float) (mv->specialhi.angle + LC_PI)) - wall;
        target = (float) (d < 0.0f || LC_ISNAN(d) ? wall + LC_HALF_PI : wall - LC_HALF_PI);
    }
    if (coll->env_flags & Collide_RightWallMask) {
        float wall = atan2f(coll->right_facing_wall.normal.y, coll->right_facing_wall.normal.x);
        float d = mv->specialhi.angle - ftLc_WrapAngle((float) (wall + LC_PI));
        target = (float) (d < 0.0f || LC_ISNAN(d) ? wall + LC_HALF_PI : wall - LC_HALF_PI);
    }
    lbVector_RotateAboutUnitAxis(&fp->self_vel, &unit_z, target - mv->specialhi.angle);
    mv->specialhi.angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

/* code+0x4B48 "SpecialHi_CollisionWithAngle": steep hits bounce (SpecialHiBound), shallow wall
 * hits slide. */
static void ftLc_SpecialHi_HitSurface(HSD_GObj* gobj, Vec3* normal, bool is_wall)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    float angle = lbVector_Angle(normal, &fp->self_vel);

    if (angle > (attrs->x7C_PKT2_WALLHUG_ANGLE + 90.0f) * LC_DEG_TO_RAD_B) {
        Vec3 rot;
        lbVector_Mirror(&fp->self_vel, normal);
        fp->self_vel.x *= 0.5f;
        fp->self_vel.y *= 0.5f;
        ftCommon_ClampSelfVelX(fp, fp->co_attrs.air_drift_max);
        fp->facing_dir = ftLc_Sign(fp->self_vel.x);
        ftLc_SpecialHi_DestroyGfx(gobj);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiBound, Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        rot.x = atan2f(-normal->x, normal->y);
        rot.y = 0.0f;
        rot.z = 0.0f;
        efSync_Spawn(1030, gobj, &fp->cur_pos, &rot);
    } else if (is_wall) {
        ftLc_SpecialHi_SlideOffWall(gobj, &fp->coll_data, angle);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Grounded start / hold / end.
 * ------------------------------------------------------------------------------------------- */

/* code+0x1950 */
void ftLc_SpecialHiStart_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialHiStart_AnimShared(gobj, ftLc_MS_SpecialHiHold);
}

/* code+0x1978 */
void ftLc_SpecialHiStart_Phys(HSD_GObj* gobj)
{
    ftLucas_MotionVars* mv = ftLc_MV(GET_FIGHTER(gobj));
    if (mv->specialhi.gravity_delay != 0) {
        mv->specialhi.gravity_delay--;
    }
    ft_80084F3C(gobj);
}

static void ftLc_SpecialHi_GroundToAir(HSD_GObj* gobj, FtMotionId msid, bool set_death_cb)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80082708(gobj)) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, msid, ftLc_MF_Switch, fp->cur_anim_frame, 1.0f, 0.0f,
                                  NULL);
        if (set_death_cb) {
            fp->death2_cb = ftLc_SpecialHi_DestroyGfx;
            fp->take_dmg_cb = ftLc_SpecialHi_DestroyGfx;
        }
    }
}

/* code+0x19B0 */
void ftLc_SpecialHiStart_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialHi_GroundToAir(gobj, ftLc_MS_SpecialAirHiStart, true);
}

/* code+0x1A38 / 0x20E0: steer phase. The head flies on its own; Lucas waits for it to come back,
 * or for both timers once it is gone. */
static void ftLc_SpecialHiHold_AnimShared(HSD_GObj* gobj, FtMotionId end, bool airborne)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    ftLc_SpecialHiHold_Timers(gobj);
    if (vars->pkthunder_gobj != NULL) {
        if (gobj == GET_ITEM(vars->pkthunder_gobj)->owner) {
            if (ftLc_SpecialHi_CheckHeadHit(gobj) == 1) {
                if (airborne) {
                    ftLc_SpecialHi_HitAirborne(gobj);
                } else {
                    ftLc_SpecialHi_HitGrounded(gobj);
                }
            }
            return;
        }
        vars->pkthunder_gobj = NULL;
        Fighter_ChangeMotionState(gobj, end, 0, 0.0f, 1.0f, 0.0f, NULL);
        ftLc_SpecialHi_DestroyGfx(gobj);
        return;
    }
    if (mv->specialhi.loop1 <= 0 && mv->specialhi.loop2 <= 0) {
        Fighter_ChangeMotionState(gobj, end, 0, 0.0f, 1.0f, 0.0f, NULL);
        ftLc_SpecialHi_DestroyGfx(gobj);
    }
}

void ftLc_SpecialHiHold_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialHiHold_AnimShared(gobj, ftLc_MS_SpecialHiEnd, false);
}

/* code+0x1B30 */
void ftLc_SpecialHiHold_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1B50 */
void ftLc_SpecialHiHold_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialHi_GroundToAir(gobj, ftLc_MS_SpecialAirHiHold, true);
}

/* code+0x1BD8 */
void ftLc_SpecialHiEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* code+0x1C1C */
void ftLc_SpecialHiEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* code+0x1C3C */
void ftLc_SpecialHiEnd_Coll(HSD_GObj* gobj)
{
    ftLc_SpecialHi_GroundToAir(gobj, ftLc_MS_SpecialAirHiEnd, false);
}

/* ---------------------------------------------------------------------------------------------
 * PK Thunder 2 on the ground (353).
 * ------------------------------------------------------------------------------------------- */

/* code+0x1CB4 */
void ftLc_SpecialHi_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialHi_StartGfx(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 30.0f);
        ftLc_SpecialHi_DestroyGfx(gobj);
    }
}

/* code+0x1D14: slow down by DECEL per frame, but never below DECEL itself (the disc keeps the
 * old speed once the step would cross it). */
void ftLc_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    float old_vel = fp->gr_vel;
    float over = fp->facing_dir * ftLc_Attrs(fp)->x74_PKT2_DECEL - old_vel;

    fp->gr_vel = -over;
    if (fp->facing_dir == 1.0f) {
        if (!(over < 0.0f || LC_ISNAN(over))) {
            fp->gr_vel = old_vel;
        }
    } else if (!(over > 0.0f || LC_ISNAN(over))) {
        fp->gr_vel = old_vel;
    }
    mv->specialhi.saved_vel = fp->self_vel;
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    ftPartSetRotX(
        fp, 0,
        (float) (atan2f(fp->self_vel.x, fp->self_vel.y) * fp->facing_dir - LC_HALF_PI));
}

/* code+0x1DF0 */
void ftLc_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    s32 env = fp->coll_data.env_flags;

    if (ft_80082708(gobj)) {
        float offset;
        if (env & 0x6FFF) {
            /* Hit a wall or the ceiling while sliding: knocked down. */
            fp->gr_vel = 0.0f;
            ftLc_SpecialHi_DestroyGfx(gobj);
            ftPartSetRotX(fp, 0, 0.0f);
            ftCo_80097D40(gobj);
            return;
        }
        /* Follow the floor. */
        if (fp->coll_data.floor.normal.y > 0.0f || LC_ISNAN(fp->coll_data.floor.normal.y)) {
            offset = fp->facing_dir == 1.0f ? (float) -LC_HALF_PI : (float) LC_HALF_PI;
        } else {
            offset = fp->facing_dir == 1.0f ? (float) LC_HALF_PI : (float) -LC_HALF_PI;
        }
        mv->specialhi.angle =
            atan2f(fp->coll_data.floor.normal.y, fp->coll_data.floor.normal.x) + offset;
        return;
    }

    ftCommon_8007D60C(fp);
    if ((env & 0xFFF) == 0) {
        /* Slid off an edge: keep flying. */
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHi, ftLc_MF_SwitchGfxHit,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        return;
    }
    mv->specialhi.loop1 = attrs->x58_PKT_LOOP1;
    mv->specialhi.loop2 = attrs->x5C_PKT_LOOP2;
    mv->specialhi.gravity_delay = attrs->x60_PKT_GRAVITY_DELAY;
    mv->specialhi.saved_vel.x = 0.0f;
    mv->specialhi.saved_vel.y = 0.0f;
    mv->specialhi.saved_vel.z = 0.0f;
    mv->specialhi.drop.x = 0.0f;
    mv->specialhi.drop.y = 0.0f;
    mv->specialhi.drop.z = 0.0f;
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
    Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialAirHiEnd, ftLc_MF_Switch, fp->cur_anim_frame,
                              1.0f, 0.0f, NULL);
}

/* ---------------------------------------------------------------------------------------------
 * Aerial start / hold / end.
 * ------------------------------------------------------------------------------------------- */

/* code+0x1FD0 */
void ftLc_SpecialAirHiStart_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialHiStart_AnimShared(gobj, ftLc_MS_SpecialAirHiHold);
}

/* code+0x1FF8 (also the Hold and End physics) */
void ftLc_SpecialAirHiStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    if (mv->specialhi.gravity_delay != 0) {
        mv->specialhi.gravity_delay--;
    } else {
        ftCommon_Fall(fp, ftLc_Attrs(fp)->x68_PKT_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* code+0x2058 */
void ftLc_SpecialAirHiStart_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiStart, ftLc_MF_Switch,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->death2_cb = ftLc_SpecialHi_DestroyGfx;
        fp->take_dmg_cb = ftLc_SpecialHi_DestroyGfx;
    }
}

/* code+0x20E0 */
void ftLc_SpecialAirHiHold_Anim(HSD_GObj* gobj)
{
    ftLc_SpecialHiHold_AnimShared(gobj, ftLc_MS_SpecialAirHiEnd, true);
}

/* code+0x21F8: lands with a small fixed box (.rodata at code+0x4B30). */
void ftLc_SpecialAirHiHold_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCollisionBox box = { 12.0f, 0.0f, { -2.0f, 2.0f }, { 2.0f, 2.0f } };
    if (ft_800824A0(gobj, &box)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiHold, ftLc_MF_Switch,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        fp->death2_cb = ftLc_SpecialHi_DestroyGfx;
        fp->take_dmg_cb = ftLc_SpecialHi_DestroyGfx;
    }
}

/* Free fall or fall with the PK Thunder landing lag. */
static void ftLc_SpecialHi_EnterFall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftCommon_8007D60C(fp);
    if (attrs->x88_PKT_LANDING_LAG == 0.0f) {
        ftCo_Fall_Enter(gobj);
    } else {
        ftCo_800969D8(gobj, 1, 0, 1, 1.0f, attrs->x88_PKT_LANDING_LAG,
                      attrs->x84_PKT_FREEFALL_MOBILITY);
    }
}

/* code+0x22BC */
void ftLc_SpecialAirHiEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
        ftLc_SpecialHi_EnterFall(gobj);
    }
}

/* code+0x2380 */
void ftLc_SpecialAirHiEnd_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHiEnd, ftLc_MF_Switch,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

/* ---------------------------------------------------------------------------------------------
 * PK Thunder 2 in the air (357).
 * ------------------------------------------------------------------------------------------- */

/* code+0x23F8 */
void ftLc_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    ftLc_SpecialHi_StartGfx(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        float drop = mv->specialhi.drop.x;
        if (drop < 0.0f) {
            drop = -drop;
        }
        fp->self_vel.y = -drop;
        ftLc_SpecialHi_EnterFall(gobj);
    }
}

/* code+0x24BC: keep the speed along the launch angle, bleeding DECEL while above it; once the
 * command script sets cmd_vars[0], sink by an accumulating drop added straight to the position. */
void ftLc_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    float speed = sqrtf(fp->self_vel.x * fp->self_vel.x + fp->self_vel.y * fp->self_vel.y);
    float slowed = speed - attrs->x74_PKT2_DECEL;

    if (!(slowed > 0.0f) && !LC_ISNAN(slowed)) {
        slowed = speed;
    }
    fp->self_vel.x = cosf(mv->specialhi.angle) * slowed;
    fp->self_vel.y = sinf(mv->specialhi.angle) * slowed;
    ftPartSetRotX(
        fp, 0,
        (float) (fp->facing_dir * atan2f(fp->self_vel.x, fp->self_vel.y) - LC_HALF_PI));
    mv->specialhi.saved_vel = fp->self_vel;

    if (fp->cmd_vars[0] == 1) {
        float drop = mv->specialhi.drop.x - attrs->x68_PKT_FALL_ACCEL;
        float floor_drop = -attrs->x6C_PKT2_SPEED;
        mv->specialhi.drop.x = drop;
        if (!(drop < floor_drop)) {
            floor_drop = drop;
        }
        mv->specialhi.drop.x = floor_drop;
        fp->cur_pos.y += floor_drop;
    }
}

/* code+0x25DC */
void ftLc_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    /* Collide with the velocity from before this frame's collision response. */
    fp->self_vel = mv->specialhi.saved_vel;

    if (ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
        float angle = lbVector_Angle(&fp->coll_data.floor.normal, &fp->self_vel);
        if (angle > (attrs->x7C_PKT2_WALLHUG_ANGLE + 90.0f) * LC_DEG_TO_RAD_B || LC_ISNAN(angle)) {
            /* Straight into the ground: knocked down. */
            fp->self_vel.x = 0.0f;
            fp->self_vel.y = 0.0f;
            fp->self_vel.z = 0.0f;
            ftLc_SpecialHi_DestroyGfx(gobj);
            ftPartSetRotX(fp, 0, 0.0f);
            ftCo_80097D40(gobj);
        } else {
            /* Glancing: keep going along the ground. */
            ftCommon_8007D7FC(fp);
            Fighter_ChangeMotionState(gobj, ftLc_MS_SpecialHi, ftLc_MF_SwitchGfxHit,
                                      fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        }
        return;
    }
    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
        return;
    }
    {
        s32 env = fp->coll_data.env_flags;
        if (env & Collide_CeilingMask) {
            ftLc_SpecialHi_HitSurface(gobj, &fp->coll_data.ceiling.normal, false);
        } else if (env & Collide_LeftWallMask) {
            ftLc_SpecialHi_HitSurface(gobj, &fp->coll_data.left_facing_wall.normal, true);
        } else if (env & Collide_RightWallMask) {
            ftLc_SpecialHi_HitSurface(gobj, &fp->coll_data.right_facing_wall.normal, true);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Bounced off a wall or ceiling (358).
 * ------------------------------------------------------------------------------------------- */

/* code+0x27B8 */
void ftLc_SpecialHiBound_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftLc_SpecialHi_EnterFall(gobj);
    }
}

/* code+0x2854 */
void ftLc_SpecialHiBound_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* code+0x2898 */
void ftLc_SpecialHiBound_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_CheckGroundAndLedge(gobj, 0) == 1) {
        fp->self_vel.x = 0.0f;
        fp->self_vel.y = 0.0f;
        fp->self_vel.z = 0.0f;
        ftLc_SpecialHi_DestroyGfx(gobj);
        ftPartSetRotX(fp, 0, 0.0f);
        ftCo_80097D40(gobj);
    } else if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
    }
}
