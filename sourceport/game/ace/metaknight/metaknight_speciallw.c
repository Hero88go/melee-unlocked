/* ACE's Meta Knight: the down special (states 355 to 364), his own code.
 *
 *   355  start on the ground (360 in the air), counted in fp+2364. While the script holds
 *        cmd_vars[2] above 0, A or B, or the C stick past 0.275 sideways, leaves at once into
 *        357 (forward) or 358 (back, turned); A or B with the stick inside 0.275 into 356.
 *        After frame 19 the trigger (pressed in the last 29 frames) ends it into 359.
 *        On frame 19, with B held and the stick inside 0.275 (on the ground: x inside, y under),
 *        356; else the stick picks one of eight angles (+32D8) for the dash, the dash physics
 *        (+34A4, in the air +3780) replaces the state's, and every jump is given back.
 *        At the end of the animation: B let go, 359 (turned to the stick); else by the stick
 *        356 (inside 0.275), 357 (forward) or 358 (back, turned to the stick).
 *   dash physics: 2.5 along the angle; a stick inside 0.2 on frame 19 marks fp+236C and the
 *        next frame ends it into 359 (364); on the ground any rise takes him into the air (360)
 *   356 to 359 (361 to 364): the follow-ups. With cmd_vars[3] set by the script, cmd_vars[2] == 2
 *        boosts every hitbox once (damage and base damage times 1.5, element 13). The state
 *        change event spawns the common effect 1012 for 356 to 358 and 361 to 363 when
 *        cmd_vars[2] is 2. In the air they end in the special fall; 361 to 364 fall with
 *        gravity while cmd_vars[1] is 1, else they hold still; 364 with cmd_vars[1] == 1 swaps
 *        its collision callback for a landing test into the special landing (+381C).
 *   Walking off 355 to 359 goes on in 360 to 364 from the same frame; landing in 360 or 364
 *        goes on in 355 or 359, with model part 2 shown and part 3 hidden. */
#include "metaknight.h"

#include <math.h>
#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_09F7.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>

static float ftMk_SpecialLw_StickAngle(Fighter* fp);
static void ftMk_SpecialLw_DashPhys(HSD_GObj* gobj);
static void ftMk_SpecialAirLw_DashPhys(HSD_GObj* gobj);
static void ftMk_SpecialLw_WalkOff(HSD_GObj* gobj);
static void ftMk_SpecialLw_Boost(HSD_GObj* gobj);
static void ftMk_SpecialAirLw_EndLandingTest(HSD_GObj* gobj);
static void ftMk_SpecialAirLw_EndLanding(HSD_GObj* gobj);
static void ftMk_SpecialAirLw_Land(HSD_GObj* gobj);

/* +3AC8 + 0xC: the effect offset of +2E8C (the words 0.0, 6.0, -5.0). */
static const Vec3 ftMk_SpecialLw_EffectOffset = { 0.0f, 6.0f, -5.0f };

/* +05C4 (slot 10, speciallw) */
void ftMk_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv;

    FTMK_CHANGE(gobj, ftMk_MS_SpecialLwStart);
    mv = ftMk_MVars(fp);
    mv->x2364.i = 0;
    mv->x236C = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[1] = 0;
}

/* +0624 (slot 11, specialairlw) */
void ftMk_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv;

    FTMK_CHANGE(gobj, ftMk_MS_SpecialAirLwStart);
    mv = ftMk_MVars(fp);
    mv->x2364.i = 0;
    mv->x236C = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[1] = 0;
}

/* +1FB4 (355) and +24EC (360): the same code with the state numbers 5 apart and the dash
 * physics +34A4 / +3780, but for one test: on frame 19 with B held, the ground start compares the
 * stick's y as it is with 0.275, the air start its size. The branch senses are the file's
 * (comments name the ground start's offsets). */
static void ftMk_SpecialLw_StartAnimCommon(HSD_GObj* gobj, FtMotionId base,
                                           HSD_GObjEvent dash_phys, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    const FtMotionId st_n = base + 1;
    const FtMotionId st_f = base + 2;
    const FtMotionId st_b = base + 3;
    const FtMotionId st_end = base + 4;
    s32 t = mv->x2364.i;
    const bool frame19 = t == 19;
    s32 saved;

    if (frame19) {
        /* +213C */
        if (fp->input.held_buttons[0] & HSD_PAD_B) {
            const float lx = fp->input.lstick[0].x;
            const float ax = lx >= 0.0f ? lx : -lx;
            if (!(ax >= 0.275)) { /* bge: a NaN goes on to the y test */
                float ly = fp->input.lstick[0].y;
                if (air) {
                    ly = ly >= 0.0f ? ly : -ly;
                }
                if (ly < 0.275) {
                    FTMK_CHANGE(gobj, st_n); /* +2398 */
                    return;
                }
            }
        }
        /* +217C, +21C4 */
        if (fp->active_timer.trigger <= 29) {
            /* +2340 */
            ftCommon_UpdateFacing(fp);
            FTMK_CHANGE(gobj, st_end);
            return;
        }
        saved = (s32) fp->cmd_vars[2];
        if (saved > 0) {
            goto inputs;
        }
        goto turn_angle;
    }
    if (t > 18) {
        /* +2198 */
        if (fp->active_timer.trigger <= 29) {
            ftCommon_UpdateFacing(fp);
            FTMK_CHANGE(gobj, st_end);
            return;
        }
    }
    saved = (s32) fp->cmd_vars[2];
    if (saved <= 0) {
        goto count; /* +21B8 */
    }

inputs: /* +1FF0 */
    if (fp->input.pressed_buttons & HSD_PAD_AB) {
        /* +20A8: the state change clears the command variables; cmd_vars[2] is put back */
        const float lx = fp->input.lstick[0].x;
        const float ax = lx < 0.0f ? -lx : lx;
        if (ax < 0.275) {
            /* +23C8 */
            FTMK_CHANGE(gobj, st_n);
            fp->cmd_vars[2] = saved;
            return;
        }
        if (!(fp->facing_dir <= 0.0f)) {
            if (lx < -0.275) {
                goto pressed_back;
            }
        } else if (!(fp->facing_dir >= 0.0f)) {
            /* +2258 */
            if (!(lx <= 0.275)) {
                goto pressed_back;
            }
        }
        /* +20F4 */
        FTMK_CHANGE(gobj, st_f);
        fp->cmd_vars[2] = saved;
        return;
    pressed_back: /* +2264 */
        fp->facing_dir = -fp->facing_dir;
        FTMK_CHANGE(gobj, st_b);
        fp->cmd_vars[2] = saved;
        return;
    }
    {
        const float cx = fp->input.cstick[0].x;
        const float acx = cx < 0.0f ? -cx : cx;
        if (acx >= 0.275) {
            /* +22A0: as above on the C stick, without putting cmd_vars[2] back */
            if (!(fp->facing_dir <= 0.0f)) {
                if (cx < -0.275) {
                    goto c_back;
                }
            } else if (!(fp->facing_dir >= 0.0f)) {
                /* +22F0 */
                if (!(cx <= 0.275)) {
                    goto c_back;
                }
            }
            FTMK_CHANGE(gobj, st_f); /* +22C0 */
            return;
        c_back: /* +22FC */
            fp->facing_dir = -fp->facing_dir;
            FTMK_CHANGE(gobj, st_b);
            return;
        }
    }
    if (!frame19) {
        goto count;
    }

turn_angle: /* +21EC */
    {
        const float angle = ftMk_SpecialLw_StickAngle(fp);
        const s32 max_jumps = fp->co_attrs.max_jumps;
        t = mv->x2364.i;
        mv->x2368.f = angle;
        fp->phys_cb = dash_phys;
        fp->x1968_jumpsUsed = (u8) max_jumps;
    }

count: /* +2030 */
    t = t + 1;
    mv->x2364.i = t;
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        /* +2214 */
        ftCommon_UpdateFacing(fp);
        FTMK_CHANGE(gobj, st_end);
        return;
    }
    {
        const float lx = fp->input.lstick[0].x;
        float ax;
        if (!(fp->facing_dir <= 0.0f)) {
            if (lx < -0.275) {
                goto held_back;
            }
        } else if (!(fp->facing_dir >= 0.0f)) {
            /* +2370 */
            if (lx > 0.275) {
                goto held_back;
            }
            if (!(lx < 0.0f)) {
                FTMK_CHANGE(gobj, st_n); /* +2390 */
                return;
            }
        }
        /* +2080, +2090 */
        ax = lx < 0.0f ? -lx : lx;
        if (ax <= 0.275) {
            FTMK_CHANGE(gobj, st_n); /* +2390 */
        } else {
            FTMK_CHANGE(gobj, st_f); /* +22C0 */
        }
        return;
    }
held_back: /* +23FC */
    ftCommon_UpdateFacing(fp);
    FTMK_CHANGE(gobj, st_b);
}

/* +1FB4: animation of 355. */
void ftMk_SpecialLwStart_Anim(HSD_GObj* gobj)
{
    ftMk_SpecialLw_StartAnimCommon(gobj, ftMk_MS_SpecialLwStart, ftMk_SpecialLw_DashPhys, false);
}

/* +24EC: animation of 360. */
void ftMk_SpecialAirLwStart_Anim(HSD_GObj* gobj)
{
    ftMk_SpecialLw_StartAnimCommon(gobj, ftMk_MS_SpecialAirLwStart, ftMk_SpecialAirLw_DashPhys,
                                   true);
}

/* +2444: physics of 355: a branch to ft_80084F3C. */
void ftMk_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +2448: collision of 355 to 359: off the floor, +35B8. */
void ftMk_SpecialLw_Coll(HSD_GObj* gobj)
{
    if (ft_80082708(gobj) == 0) {
        ftMk_SpecialLw_WalkOff(gobj);
    }
}

/* +2494: animation of 356 to 359: the hitbox boost (+36EC), then the wait at the end. */
void ftMk_SpecialLwDash_Anim(HSD_GObj* gobj)
{
    ftMk_SpecialLw_Boost(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +24E8: physics of 356 to 359: a branch to ft_80084F3C. */
void ftMk_SpecialLwDash_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* +2990: physics of 360: no speed. */
void ftMk_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = 0.0f;
    fp->self_vel.x = 0.0f;
}

/* +29A8: collision of 360 to 364. In 364 with cmd_vars[1] == 1 the collision callback becomes
 * +381C (from the next frame). Then a landing test, with +3828 on landing. */
void ftMk_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->motion_id == ftMk_MS_SpecialAirLwEnd && (s32) fp->cmd_vars[1] == 1) {
        /* +29F0 */
        fp->coll_cb = ftMk_SpecialAirLw_EndLandingTest;
    }
    if (ft_80081D0C(gobj)) {
        ftMk_SpecialAirLw_Land(gobj);
    }
}

/* +2A30: animation of 361 to 364: the hitbox boost, then at the end the special fall (no
 * interrupt, mobility 1, landing lag 30), every jump given back. */
void ftMk_SpecialAirLwDash_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftMk_SpecialLw_Boost(gobj);
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, false, 1.0f, 30.0f);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +2ABC: physics of 361 to 364: with cmd_vars[1] == 1 the common fall and drift, else no speed. */
void ftMk_SpecialAirLwDash_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if ((s32) fp->cmd_vars[1] == 1) {
        /* +2AE0 */
        ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
        ftCommon_CalcSelfAccel_Drift(fp);
        return;
    }
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
}

/* +2E8C: the effect the state change event spawns for 356 to 358 and 361 to 363: with
 * cmd_vars[2] == 2, common effect 1012 (0x3F4, a particle generator of the common bank) on part
 * 0 at (0, 6, -5) with no spread. The float argument of ftCo_8009F834 is not set by the file
 * (f1 is whatever the caller left); the callee does not read it. */
void ftMk_SpecialLw_Effect(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 offset;
    Vec3 spread;

    if ((s32) fp->cmd_vars[2] != 2) {
        return;
    }
    offset = ftMk_SpecialLw_EffectOffset;
    spread.x = 0.0f;
    spread.y = 0.0f;
    spread.z = 0.0f;
    ftCo_8009F834(gobj, 1012, 0, 0, 0, &offset, &spread, 0.0f);
}

/* +32D8: the dash angle from the stick. Inside 0.2 on both axes: fp+236C = 1 and 0. Else the
 * stick's angle (atan2f, below -pi/6 lifted by 2pi in double) picked into eight sectors of pi/3
 * and 2pi/3 widths as the file has them; the returned values are its float words. */
static float ftMk_SpecialLw_StickAngle(Fighter* fp)
{
    const float lx = fp->input.lstick[0].x;
    const float ly = fp->input.lstick[0].y;
    const float ax = lx < 0.0f ? -lx : lx;
    float angle;

    if (!(ax > 0.2)) {
        const float ay = ly < 0.0f ? -ly : ly;
        if (ay <= 0.2) {
            /* +3408 */
            ftMk_MVars(fp)->x236C = 1;
            return 0.0f;
        }
    }
    /* +332C */
    angle = atan2f(ly, lx);
    if (!(angle >= -0.5235987755982988)) {
        angle = (float) ((double) angle + 6.283185307179586);
    }
    if (!(angle >= -0.5235987755982988)) {
        return 5.497786998748779f;
    }
    if (angle < 0.5235987755982988) {
        return 0.0f;
    }
    if (angle < 1.0471975511965976) {
        return 0.7853981852531433f;
    }
    if (angle < 2.0943951023931953) {
        return 1.5707963705062866f;
    }
    if (angle < 2.6179938779914944) {
        return 2.356194496154785f;
    }
    if (angle < 3.665191429188092) {
        return 3.1415927410125732f;
    }
    if (angle < 4.1887902047863905) {
        return 3.9269907474517822f;
    }
    if (angle >= 5.235987755982989) {
        return 5.497786998748779f;
    }
    return 4.71238899230957f;
}

/* +34A4: the ground dash physics, put in fp->phys_cb on frame 19 of 355 (and by +35B8). fp+236C
 * set: cleared, 359. Else 2.5 along fp+2368; any rise takes him into the air: 360 from the same
 * frame, every jump given back, the air dash physics. */
static void ftMk_SpecialLw_DashPhys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);

    if (mv->x236C == 1) {
        /* +3584 */
        mv->x236C = 0;
        FTMK_CHANGE(gobj, ftMk_MS_SpecialLwEnd);
        return;
    }
    fp->self_vel.x = cosf(mv->x2368.f) * 2.5f;
    fp->self_vel.y = sinf(mv->x2368.f) * 2.5f;
    if (fp->self_vel.y > 0.0f) {
        /* +3528 */
        ftCommon_8007D5D4(fp);
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwStart);
        fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
        fp->phys_cb = ftMk_SpecialAirLw_DashPhys;
    }
}

/* +3780: the air dash physics, put in fp->phys_cb on frame 19 of 360 and by +34A4. fp+236C
 * set: cleared, 364. Else 2.5 along fp+2368. */
static void ftMk_SpecialAirLw_DashPhys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);

    if (mv->x236C == 1) {
        /* +37E8 */
        mv->x236C = 0;
        FTMK_CHANGE(gobj, ftMk_MS_SpecialAirLwEnd);
        return;
    }
    fp->self_vel.x = cosf(mv->x2368.f) * 2.5f;
    fp->self_vel.y = sinf(mv->x2368.f) * 2.5f;
}

/* +35B8: walking off 355 to 359: in the air, the matching state 360 to 364 from the same frame.
 * Off the start after frame 18 (the dash had begun) the ground dash physics +34A4 replaces the
 * state's. The jump table +3B8C points into +3BA0, a second copy of this routine (equal
 * instruction for instruction, consts.txt), which is reached only through it. */
static void ftMk_SpecialLw_WalkOff(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const s32 t = ftMk_MVars(fp)->x2364.i;

    ftCommon_8007D5D4(fp);
    switch (fp->motion_id) {
    case ftMk_MS_SpecialLwStart: /* +3C1C */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwStart);
        if (t > 18) {
            fp->phys_cb = ftMk_SpecialLw_DashPhys;
        }
        break;
    case ftMk_MS_SpecialLwN: /* +3C94 */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwN);
        break;
    case ftMk_MS_SpecialLwF: /* +3CB4 */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwF);
        break;
    case ftMk_MS_SpecialLwB: /* +3C54 */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwB);
        break;
    case ftMk_MS_SpecialLwEnd: /* +3C74 */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialAirLwEnd);
        break;
    default:
        break;
    }
}

/* +36EC: the hitbox boost of 356 to 359 and 361 to 364. When the script raised cmd_vars[3]: it
 * is cleared, and with cmd_vars[2] == 2 the four hitboxes get element 13, damage times 1.5
 * (fmuls) and the base damage word times 1.5 (fmul in double, fctiwz). */
static void ftMk_SpecialLw_Boost(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    s32 kind;
    int i;

    if ((s32) fp->cmd_vars[3] != 1) {
        return;
    }
    kind = (s32) fp->cmd_vars[2];
    fp->cmd_vars[3] = 0;
    if (kind != 2) {
        return;
    }
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        const double base = (double) (s32) hit->unk_count;
        hit->element = 13;
        hit->damage = hit->damage * 1.5f;
        hit->unk_count = (u32) (s32) (base * 1.5);
    }
}

/* +381C: the collision callback 364 installs: ft_80083A48 with the console address of
 * ftCo_LandingFallSpecial_Enter (0x800D5CB0) as the landing event, which the game calls with
 * the fighter object only. */
static void ftMk_SpecialAirLw_EndLandingTest(HSD_GObj* gobj)
{
    ft_80083A48(gobj, ftMk_SpecialAirLw_EndLanding);
}

/* The landing event of +381C: ftCo_LandingFallSpecial_Enter(gobj, allow_interrupt, landing_lag)
 * reached through a one-argument event pointer, so on the console the two other arguments are
 * the registers r4 and f1 as ft_80083A48 leaves them. Read from main.dol (ft_80083A48 at
 * 0x80083A48 and ft_80081A00 at 0x80081A00): r4 is the word of coll_data.cur_pos.x loaded at
 * 0x80083AD8, and on the path with no item object in the game and the two early-out bits of
 * ft_80081A00 clear, f1 is fp->cur_pos.y loaded at 0x80081A74. That path is reproduced here. With
 * items present, or with x2219_b2 / x2223_b5 set, the console's values are other leftovers
 * (NOTES.md, unproven). A lag of 0 (a floor at y = 0) gives an infinite animation rate, as on the
 * console. */
static void ftMk_SpecialAirLw_EndLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 r4;

    memcpy(&r4, &fp->coll_data.cur_pos.x, sizeof r4);
    ftCo_LandingFallSpecial_Enter(gobj, (bool) r4, fp->cur_pos.y);
}

/* +3828: the landing of 360 and 364: 360 goes on as 355 and 364 as 359 from the same frame,
 * then model part 2 to 0 and part 3 to 1 (ftParts_80074B0C through the word at +2F08). */
static void ftMk_SpecialAirLw_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    if (fp->motion_id == ftMk_MS_SpecialAirLwStart) {
        /* +38E8 */
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialLwStart);
    } else if (fp->motion_id == ftMk_MS_SpecialAirLwEnd) {
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialLwEnd);
    } else {
        return;
    }
    /* +3880 */
    ftParts_80074B0C(gobj, 2, 0);
    ftParts_80074B0C(gobj, 3, 1);
}
