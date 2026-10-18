/* ACE's Chun-Li: the cancels of the onframe event, the light down tilt (364) and state 366
 * (m-ex "ftFunction" of PlCn.dat).
 *
 * [onframe] (+07BC to +16F8) is one function the compiler laid out as a web of shared tails.
 * It is written here with the labels of the console code (L_xxxx = block offset) and the same
 * jumps, so that every path takes the same tests in the same order. Each test reads the
 * Fighter again where the console reads memory again; where the console keeps a register
 * across a state change it has always reloaded it before the next use, so the fresh reads give
 * the same values. What it does:
 *
 *  1. On the ground, B held and A pressed in the first two frames of any state but 367: the B
 *     + A shot (367).
 *  2. A down tilt (57) still before frame +88 (4) with A let go: the light down tilt (364)
 *     from the same frame, and "Going to light DTilt" on the console log.
 *  3. In jab 1 and 2, forward tilt, up tilt, down tilt (on the ground) and neutral, up and down
 *     air, with a hit landed (hitbox 0 has a victim) and B pressed in the move's frame window:
 *     stick down -> the down special (355, or the air one), stick up -> the up special, stick
 *     forward -> the side special from frame 12 (no script step), no stick -> the neutral
 *     special. The windows (open at both ends): jab 1 4..11, jab 2 4..20, forward tilt
 *     11..19, up tilt 6..29, down tilt 9..17, neutral air 9..20, up air 8..16, down air
 *     10..30; the light down tilt 4..11 for the side special.
 *  4. In the light down tilt with a hit, stick down and A pressed, frames 4..11: the light down
 *     tilt again, with its cancel callback.
 * Kept as on the console: after one change the remaining tests still run on the new state (a
 * second change in the same frame is possible); the neutral branch with no hit returns when
 * she faces right and runs test 4 when she faces left; the side special is entered by the
 * forward direction only (stick right facing right, stick left facing left). */
#include "chunli.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* The motion states the cancels test: bit (msid - 0x2C) of 0x03203203 (+0808 to +0818). */
#define FTCN_CANCEL_SET 0x03203203u

/* Inside a frame window, open at both ends: "ble lo" then "bge hi" on the console, so a NaN
 * frame is outside. */
static inline bool ftCn_InWindow(float frame, float lo, float hi)
{
    return frame > lo && frame < hi;
}

static inline HSD_Pad ftCn_Held(Fighter* fp)
{
    return fp->input.held_buttons[0];
}

static inline HSD_Pad ftCn_Pressed(Fighter* fp)
{
    return fp->input.pressed_buttons;
}

static inline bool ftCn_HitLanded(Fighter* fp)
{
    return fp->x914[0].victims_1[0].victim != NULL;
}

/* +07BC, [onframe]. */
void ftCn_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* da = ftCn_Attrs(fp);
    int msid;
    u32 bit;

    if ((ftCn_Held(fp) & HSD_PAD_B) && (ftCn_Pressed(fp) & HSD_PAD_A)) {
        /* L_E04 */
        if (fp->motion_id == ftCn_MS_SpecialNAlt) {
            goto L_E10;
        }
        /* L_EFC */
        if (!(fp->cur_anim_frame < 2.0f)) {
            goto L_7F4;
        }
        if (fp->ground_or_air != GA_Ground) {
            goto L_7F4;
        }
        fp->throw_flags_b0 = false;
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialNAlt, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        fp->accessory4_cb = ftCn_SpecialNAlt_OnAccessory4;
    }

L_7F4:
    if (fp->motion_id == ftCo_MS_AttackLw3) {
        goto L_E34;
    }
L_7FC:
    msid = fp->motion_id;
    bit = (u32) (msid - ftCo_MS_Attack11);
    if (bit > 0x19 || !((FTCN_CANCEL_SET >> bit) & 1)) {
        goto L_E10;
    }
L_81C:
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        goto L_EDC;
    }
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_ANY)) {
        goto L_12F0;
    }
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
        goto L_F98;
    }
    if (!ftCn_HitLanded(fp)) {
        goto L_FA4;
    }

    /* ---- stick down: the down special ---- */
L_844:
    switch (fp->motion_id) {
    case ftCo_MS_Attack11:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_88C;
        }
        goto L_FE0;
    case ftCo_MS_Attack12:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 20.0f)) {
            goto L_88C;
        }
        goto L_FE0;
    case ftCo_MS_AttackLw3:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 17.0f)) {
            goto L_88C;
        }
        goto L_FE0;
    case ftCo_MS_AttackHi3:
        if (ftCn_InWindow(fp->cur_anim_frame, 6.0f, 29.0f)) {
            goto L_88C;
        }
        goto L_FE0;
    case ftCo_MS_AttackS3S:
        if (ftCn_InWindow(fp->cur_anim_frame, 11.0f, 19.0f)) {
            goto L_88C;
        }
        goto L_FE0;
    default:
        goto L_8DC;
    }
L_88C:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
L_8DC:
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 20.0f)) {
            goto L_918;
        }
        goto L_FE0;
    case ftCo_MS_AttackAirHi:
        if (ftCn_InWindow(fp->cur_anim_frame, 8.0f, 16.0f)) {
            goto L_918;
        }
        goto L_FE0;
    case ftCo_MS_AttackAirLw:
        if (ftCn_InWindow(fp->cur_anim_frame, 10.0f, 30.0f)) {
            goto L_918;
        }
        goto L_FE0;
    default:
        goto L_FE0;
    }
L_918:
    ftCn_SpecialAirLw_Enter(gobj);
    goto L_948;
L_FA4:
    goto L_AA4;
L_FE0:
    if (ftCn_Held(fp) & FTCN_PAD_LS_UP) {
        goto L_950;
    }
    goto L_AA4;

    /* ---- stick up: the up special ---- */
L_948:
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_UP)) {
        goto L_AA4;
    }
L_950:
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        goto L_AA4;
    }
L_95C:
    if (!ftCn_HitLanded(fp)) {
        goto L_AA4;
    }
    switch (fp->motion_id) {
    case ftCo_MS_Attack11:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_9B0;
        }
        goto L_AA4;
    case ftCo_MS_Attack12:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 20.0f)) {
            goto L_9B0;
        }
        goto L_AA4;
    case ftCo_MS_AttackLw3:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 17.0f)) {
            goto L_9B0;
        }
        goto L_AA4;
    case ftCo_MS_AttackHi3:
        if (ftCn_InWindow(fp->cur_anim_frame, 6.0f, 29.0f)) {
            goto L_9B0;
        }
        goto L_AA4;
    case ftCo_MS_AttackS3S:
        if (ftCn_InWindow(fp->cur_anim_frame, 11.0f, 19.0f)) {
            goto L_9B0;
        }
        goto L_AA4;
    default:
        goto L_A18;
    }
L_9B0: {
    Fighter* fp2 = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftCommon_8007D5D4(fp2);
}
L_A18:
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 20.0f)) {
            goto L_A54;
        }
        goto L_AA4;
    case ftCo_MS_AttackAirHi:
        if (ftCn_InWindow(fp->cur_anim_frame, 8.0f, 16.0f)) {
            goto L_A54;
        }
        goto L_AA4;
    case ftCo_MS_AttackAirLw:
        if (ftCn_InWindow(fp->cur_anim_frame, 10.0f, 30.0f)) {
            goto L_A54;
        }
        goto L_AA4;
    default:
        goto L_AA4;
    }
L_A54:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);

    /* ---- stick forward: the side special (L_1018 and L_101C are this test too) ---- */
L_AA4:
    if (fp->facing_dir != 1.0f) {
        goto L_C00;
    }
    /* L_AB8: facing right, stick right */
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_RIGHT)) {
        goto L_D54;
    }
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        goto L_D54;
    }
    if (!ftCn_HitLanded(fp)) {
        goto L_D54;
    }
    switch (fp->motion_id) {
    case ftCo_MS_Attack11:
        /* L_121C: outside the window it goes on to the left test (L_10B8) */
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_B28;
        }
        goto L_C00;
    case ftCo_MS_Attack12:
        /* L_1094: not past 4 -> L_D54; past 20 -> the left test */
        if (!(fp->cur_anim_frame > 4.0f)) {
            goto L_D54;
        }
        if (fp->cur_anim_frame < 20.0f) {
            goto L_B28;
        }
        goto L_C00;
    case ftCo_MS_AttackLw3:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 17.0f)) {
            goto L_B28;
        }
        goto L_D54;
    case ftCo_MS_AttackHi3:
        if (ftCn_InWindow(fp->cur_anim_frame, 6.0f, 29.0f)) {
            goto L_B28;
        }
        goto L_D54;
    case ftCo_MS_AttackS3S:
        if (ftCn_InWindow(fp->cur_anim_frame, 11.0f, 19.0f)) {
            goto L_B28;
        }
        goto L_D54;
    case ftCn_MS_AttackLw3Light:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_B28;
        }
        goto L_D54;
    default:
        goto L_B74;
    }
L_B28:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSStart, Ft_MF_None, 12.0f, 1.0f, 0.0f, NULL);
L_B74:
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 20.0f)) {
            goto L_BB0;
        }
        goto L_C00;
    case ftCo_MS_AttackAirHi:
        if (ftCn_InWindow(fp->cur_anim_frame, 8.0f, 16.0f)) {
            goto L_BB0;
        }
        goto L_C00;
    case ftCo_MS_AttackAirLw:
        if (ftCn_InWindow(fp->cur_anim_frame, 10.0f, 30.0f)) {
            goto L_BB0;
        }
        goto L_C00;
    default:
        goto L_C00;
    }
L_BB0:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSStart, Ft_MF_None, 12.0f, 1.0f, 0.0f,
                              NULL);
L_C00:
    if (fp->facing_dir != -1.0f) {
        goto L_D54;
    }
    /* facing left, stick left */
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_LEFT)) {
        goto L_D54;
    }
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        goto L_D54;
    }
    if (!ftCn_HitLanded(fp)) {
        goto L_D54;
    }
    switch (fp->motion_id) {
    case ftCo_MS_Attack11:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_C80;
        }
        goto L_D54;
    case ftCo_MS_Attack12:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 20.0f)) {
            goto L_C80;
        }
        goto L_D54;
    case ftCo_MS_AttackLw3:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 17.0f)) {
            goto L_C80;
        }
        goto L_D54;
    case ftCo_MS_AttackHi3:
        if (ftCn_InWindow(fp->cur_anim_frame, 6.0f, 29.0f)) {
            goto L_C80;
        }
        goto L_D54;
    case ftCo_MS_AttackS3S:
        if (ftCn_InWindow(fp->cur_anim_frame, 11.0f, 19.0f)) {
            goto L_C80;
        }
        goto L_D54;
    case ftCn_MS_AttackLw3Light:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
            goto L_C80;
        }
        goto L_D54;
    default:
        goto L_CCC;
    }
L_C80:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialSStart, Ft_MF_None, 12.0f, 1.0f, 0.0f, NULL);
L_CCC:
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 20.0f)) {
            goto L_D08;
        }
        goto L_D54;
    case ftCo_MS_AttackAirHi:
        if (ftCn_InWindow(fp->cur_anim_frame, 8.0f, 16.0f)) {
            goto L_D08;
        }
        goto L_D54;
    case ftCo_MS_AttackAirLw:
        if (ftCn_InWindow(fp->cur_anim_frame, 10.0f, 30.0f)) {
            goto L_D08;
        }
        goto L_D54;
    default:
        goto L_D54;
    }
L_D08:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirSStart, Ft_MF_None, 12.0f, 1.0f, 0.0f,
                              NULL);

    /* ---- the light down tilt again ---- */
L_D54:
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
        return;
    }
    if (!(ftCn_Pressed(fp) & HSD_PAD_A)) {
        return;
    }
    if (!ftCn_HitLanded(fp)) {
        return;
    }
    if (fp->motion_id != ftCn_MS_AttackLw3Light) {
        return;
    }
    if (!ftCn_InWindow(fp->cur_anim_frame, 4.0f, 11.0f)) {
        return;
    }
    {
        Fighter* fp2 = GET_FIGHTER(gobj);

        Fighter_ChangeMotionState(gobj, ftCn_MS_AttackLw3Light, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                  NULL);
        ftAnim_8006EBA4(gobj);
        fp2->deal_dmg_cb = ftCn_AttackLw3Light_Cancel;
    }
    return;

    /* ---- not a cancel state ---- */
L_E10:
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
        goto L_948;
    }
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        goto L_AA4;
    }
    if (ftCn_HitLanded(fp)) {
        goto L_844;
    }
    goto L_FA4;

    /* ---- the down tilt: light when A is let go early ---- */
L_E34:
    if (!(fp->cur_anim_frame <= (float) (u32) da->attacklw3_light_frame)) {
        goto L_81C;
    }
    if (ftCn_Held(fp) & HSD_PAD_A) {
        goto L_81C;
    }
    OSReport("Going to light DTilt\n");
    Fighter_ChangeMotionState(gobj, ftCn_MS_AttackLw3Light, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
    goto L_7FC;

    /* ---- B not pressed ---- */
L_EDC:
    if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
        goto L_948;
    }
    goto L_AA4;

    /* ---- a stick direction, not down ---- */
L_F98:
    if (ftCn_Held(fp) & FTCN_PAD_LS_UP) {
        goto L_95C;
    }
    goto L_AA4;

    /* ---- no stick direction: the neutral special ---- */
L_12F0:
    if (!ftCn_HitLanded(fp)) {
        /* L_1598 */
        if (fp->facing_dir == 1.0f) {
            return;
        }
        goto L_D54;
    }
    switch (fp->motion_id) {
    case ftCo_MS_Attack11:
        /* L_15DC: not past 4 -> L_1654 (the air test of L_13B4, which fails) */
        if (!(fp->cur_anim_frame > 4.0f)) {
            if (fp->motion_id != ftCo_MS_AttackAirN) {
                goto L_13B4;
            }
            goto L_1528;
        }
        if (fp->cur_anim_frame < 11.0f) {
            goto L_1338;
        }
        goto L_AA4;
    case ftCo_MS_Attack12:
        if (ftCn_InWindow(fp->cur_anim_frame, 4.0f, 20.0f)) {
            goto L_1338;
        }
        goto L_AA4;
    case ftCo_MS_AttackLw3:
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 17.0f)) {
            goto L_1338;
        }
        goto L_AA4;
    case ftCo_MS_AttackHi3:
        if (ftCn_InWindow(fp->cur_anim_frame, 6.0f, 29.0f)) {
            goto L_1338;
        }
        goto L_AA4;
    case ftCo_MS_AttackS3S:
        if (ftCn_InWindow(fp->cur_anim_frame, 11.0f, 19.0f)) {
            goto L_1338;
        }
        goto L_AA4;
    default:
        goto L_13A8;
    }
L_1338: {
    Fighter* fp2 = GET_FIGHTER(gobj);

    fp2->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp2->accessory4_cb = ftCn_SpecialN_OnAccessory4;
}
L_13A8:
    if (fp->motion_id == ftCo_MS_AttackAirN) {
        if (ftCn_InWindow(fp->cur_anim_frame, 9.0f, 20.0f)) {
            goto L_13E8;
        }
        goto L_1528;
    }
L_13B4:
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirHi:
        if (ftCn_InWindow(fp->cur_anim_frame, 8.0f, 16.0f)) {
            goto L_13E8;
        }
        goto L_1528;
    case ftCo_MS_AttackAirLw:
        if (ftCn_InWindow(fp->cur_anim_frame, 10.0f, 30.0f)) {
            goto L_13E8;
        }
        goto L_1528;
    default:
        goto L_1528;
    }
L_13E8: {
    Fighter* fp2 = GET_FIGHTER(gobj);

    fp2->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp2->accessory4_cb = ftCn_SpecialN_OnAccessory4;
}
    goto L_E10;
L_1528:
    goto L_E10;
}

/* +263C, the animation callback of 364: crouch (the common SquatWait, 40) when the animation
 * is over, with a full state change. */
void ftCn_AttackLw3Light_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftCo_MS_SquatWait, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    }
}

/* +26A8, the interrupt callback of 364: while the animation runs, the cancel callback is put in
 * the damage dealt slot (fp+21C0) and run. */
void ftCn_AttackLw3Light_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp->deal_dmg_cb = ftCn_AttackLw3Light_Cancel;
    ftCn_AttackLw3Light_Cancel(gobj);
}

/* +2718, the collision callback of 364: off the ground, the common fall-off-the-edge step. */
void ftCn_AttackLw3Light_Coll(HSD_GObj* gobj)
{
    if (ft_80082708(gobj)) {
        return;
    }
    ft_80084104(gobj);
}

/* +27B0, the interrupt callback of 366: the same with its own cancel. */
void ftCn_AttackHi3Light_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    fp->deal_dmg_cb = ftCn_AttackHi3Light_Cancel;
    ftCn_AttackHi3Light_Cancel(gobj);
}

/* +2820, the collision callback of 366: the grounding step (ftCommon_8007D7FC) every frame,
 * with no floor test. Mod quirk, kept. */
void ftCn_AttackHi3Light_Coll(HSD_GObj* gobj)
{
    ftCommon_8007D7FC(GET_FIGHTER(gobj));
}

/* +2A7C, the cancel callback of 364 (in fp+21C0 and run by its interrupt callback). The tests
 * run one after the other; after a change the next tests run on the new state:
 *   stick down: A pressed, frames 7..14 -> 364 again; B pressed -> the down special (355);
 *   stick up: B pressed -> the up special (349), airborne;
 *   B pressed past frame 6 -> the neutral special (341). */
void ftCn_AttackLw3Light_Cancel(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftCn_Held(fp) & FTCN_PAD_LS_DOWN) {
        /* L_2AD0 */
        if ((ftCn_Pressed(fp) & HSD_PAD_A) && fp->cur_anim_frame < 14.0f &&
            fp->cur_anim_frame > 7.0f)
        {
            /* L_2C40 */
            Fighter_ChangeMotionState(gobj, ftCn_MS_AttackLw3Light, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                      NULL);
            ftAnim_8006EBA4(gobj);
            fp->deal_dmg_cb = ftCn_AttackLw3Light_Cancel;
            if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
                goto L_2B4C;
            }
        }
        /* L_2AFC */
        if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
            return;
        }
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
    }
L_2B4C:
    if (ftCn_Held(fp) & FTCN_PAD_LS_UP) {
        /* L_2B60 */
        if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
            return;
        }
        {
            Fighter* fp2 = GET_FIGHTER(gobj);

            Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f,
                                      NULL);
            ftAnim_8006EBA4(gobj);
            ftCommon_8007D5D4(fp2);
        }
    }
    /* L_2AA8 */
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        return;
    }
    if (!(fp->cur_anim_frame > 6.0f)) {
        return;
    }
    /* L_2BD0 */
    {
        Fighter* fp2 = GET_FIGHTER(gobj);

        fp2->throw_flags_b0 = false;
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp2->accessory4_cb = ftCn_SpecialN_OnAccessory4;
    }
}

/* +2FEC, the cancel callback of 366 (in fp+21C0 and run by its interrupt callback), the mirror
 * of +2A7C with the stick up for the A chain:
 *   stick up: A pressed, frames 7..14 -> 366 again (then stick down and B -> the down special,
 *     or on to the stick up test); B pressed -> the up special (349), airborne;
 *   stick down (and not up): B pressed -> the down special (355), then the stick up test;
 *   B pressed past frame 6 -> the neutral special (341).
 * Both specials write "FEIN-UH" on the console log. */
void ftCn_AttackHi3Light_Cancel(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(ftCn_Held(fp) & FTCN_PAD_LS_UP)) {
        /* L_30A0 */
        if (ftCn_Held(fp) & FTCN_PAD_LS_DOWN) {
            goto L_3138;
        }
        goto L_30AC;
    }
    if (ftCn_Pressed(fp) & HSD_PAD_A) {
        /* L_314C */
        if (fp->cur_anim_frame < 14.0f && fp->cur_anim_frame > 7.0f) {
            Fighter_ChangeMotionState(gobj, ftCn_MS_AttackHi3Light, Ft_MF_None, 0.0f, 1.0f,
                                      0.0f, NULL);
            ftAnim_8006EBA4(gobj);
            fp->deal_dmg_cb = ftCn_AttackHi3Light_Cancel;
            if (!(ftCn_Held(fp) & FTCN_PAD_LS_DOWN)) {
                goto L_3244;
            }
            if (ftCn_Pressed(fp) & HSD_PAD_B) {
                goto L_31FC;
            }
            return;
        }
    }
    /* L_3014 */
    if (ftCn_Held(fp) & FTCN_PAD_LS_DOWN) {
        goto L_3138;
    }
L_3020:
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        return;
    }
    {
        Fighter* fp2 = GET_FIGHTER(gobj);

        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialHi, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        ftCommon_8007D5D4(fp2);
        OSReport("FEIN-UH\n");
    }
L_30AC:
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        return;
    }
    if (!(fp->cur_anim_frame > 6.0f)) {
        return;
    }
    {
        Fighter* fp2 = GET_FIGHTER(gobj);

        fp2->throw_flags_b0 = false;
        Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
        ftAnim_8006EBA4(gobj);
        fp2->accessory4_cb = ftCn_SpecialN_OnAccessory4;
    }
    return;
L_3138:
    if (!(ftCn_Pressed(fp) & HSD_PAD_B)) {
        return;
    }
L_31FC:
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialLw, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    OSReport("FEIN-UH\n");
L_3244:
    if (ftCn_Held(fp) & FTCN_PAD_LS_UP) {
        goto L_3020;
    }
    goto L_30AC;
}
