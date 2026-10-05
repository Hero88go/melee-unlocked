/* ACE's B. Falcon: the state callbacks that are his own. Every other callback of his state table
 * is Captain Falcon's retail function (bfalcon.c). */
#include "bfalcon.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <sysdolphin/baselib/controller.h>

_Static_assert(ftCo_MS_Wait == 0xE && ftCo_MS_Fall == 0x1D,
               "the common state ids PlBF.dat passes to Fighter_ChangeMotionState");
_Static_assert(ftCa_MS_SpecialN == 0x15B && ftCa_MS_SpecialAirN == 0x15C &&
                   ftCa_MS_SpecialHiThrow1 == 0x16B,
               "Captain Falcon's state ids PlBF.dat passes to Fighter_ChangeMotionState");

/* The buttons that drop the punch: Z, R or L pressed this frame (mask 0x70 of fp+668). */
#define ftBf_CancelButtons (HSD_PAD_Z | HSD_PAD_R | HSD_PAD_L)

/* Every state change of this file starts at frame 0 unless said, speed 1, no blend, no flags. */
static inline void ftBf_ChangeState(HSD_GObj* gobj, FtMotionId msid, float frame)
{
    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, frame, 1.0f, 0.0f, NULL);
}

/* +10A8 "SpecialN_OnTakeDamage": hit before the punch is out, the charge is lost. */
static void ftBf_SpecialN_OnTakeDamage(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftBf_Vars(fp)->charge = 0;
}

/* +10B8 "SpecialN_OnTakeDamage2": empty. From frame 60 a hit no longer clears the charge. */
static void ftBf_SpecialN_OnTakeDamage2(HSD_GObj* gobj)
{
    (void) gobj;
}

/* What differs between the grounded routine (+04B0) and the aerial one (+088C): the same 247
 * instructions with other constants. */
typedef struct ftBf_SpecialNParams {
    float early_end;           /* the first window ends at this frame: 60 grounded, 119 aerial */
    FtMotionId cancel;         /* Wait, Fall */
    FtMotionId self;           /* 347, 348: started again at frame 80 by a charge of 1 */
    FtMotionId charge0;        /* B in the first window: 366, 367 */
    FtMotionId charge1;        /* B from frame 60 to 119: 368, 369 */
    FtMotionId charge2;        /* B from frame 120, and a kept charge before 119: 364, 365 */
    bool (*jump)(HSD_GObj* gobj);
} ftBf_SpecialNParams;

/* The body of "SpecialN_Start_IASACB" and "SpecialAirN_Start_IASACB". Read from the file
 * instruction by instruction: the frame is read again from the fighter at every test (a state
 * change in between resets it), and the order of the tests is the file's.
 *
 * - first window: a hit clears the charge; Z, R or L drops to Wait or Fall; B goes to the first
 *   charge state; with a charge of exactly 1, frame 22 starts the punch again at frame 80 with
 *   the charged color; frame 30 costs 1 percent.
 * - frame 60: the charge goes up by 1, 1 percent.
 * - frames 60 to 119: B goes to the second charge state, Z, R or L drops; frame 90 costs 1.
 * - from frame 120: B goes to the third charge state; frame 120 costs 2 and adds 2 to the charge.
 * - before frame 119 with a charge over 1: the third charge state, charge cleared.
 * - from frame 145: stick sideways (0.25) or Z, R, L drops; a jump input jumps.
 *
 * The state changes do not end the routine: it goes on with the fighter in the new state, as the
 * file does. */
static inline void ftBf_SpecialN_Interrupt(HSD_GObj* gobj, const ftBf_SpecialNParams* p)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBf_FighterVars* fv = ftBf_Vars(fp);
    float frame;
    s32 charge;

    if (fp->cur_anim_frame < p->early_end) {
        fp->take_dmg_cb = ftBf_SpecialN_OnTakeDamage;
        if (fp->input.pressed_buttons & ftBf_CancelButtons) {
            ftBf_ChangeState(gobj, p->cancel, 0.0f);
        }
        if (fp->input.pressed_buttons & HSD_PAD_B) {
            ftBf_ChangeState(gobj, p->charge0, 0.0f);
            fv->charge = 0;
            charge = 0;
        } else {
            charge = fv->charge;
        }
        /* +052C, +0748: an unsigned compare in the file. When the frame is 22 and the charge is
         * not 1 the file jumps past the two tests below (frames 30 and 60); both are false then,
         * so falling through is the same. */
        if ((u32) charge <= 1 && fp->cur_anim_frame == 22.0f && charge == 1) {
            ftBf_ChangeState(gobj, p->self, 80.0f);
            ftCo_800BFFD0(fp, ftBf_ColAnim_Charged, false);
        }
        if (fp->cur_anim_frame == 30.0f) {
            Fighter_TakeDamage_8006CC7C(fp, 1.0f);
        }
    }
    if (fp->cur_anim_frame == 60.0f) {
        fv->charge++;
        Fighter_TakeDamage_8006CC7C(fp, 1.0f);
    }

    frame = fp->cur_anim_frame;
    if (frame >= 60.0f) {
        if (frame < 120.0f) {
            if (fp->input.pressed_buttons & HSD_PAD_B) {
                ftBf_ChangeState(gobj, p->charge1, 0.0f);
                fv->charge = 0;
            }
            if (fp->input.pressed_buttons & ftBf_CancelButtons) {
                ftBf_ChangeState(gobj, p->cancel, 0.0f);
            }
            fp->take_dmg_cb = ftBf_SpecialN_OnTakeDamage2;
            if (fp->cur_anim_frame == 90.0f) {
                Fighter_TakeDamage_8006CC7C(fp, 1.0f);
            }
            if (!(fp->cur_anim_frame >= 120.0f)) {
                goto before_120;
            }
        }
        if (fp->input.pressed_buttons & HSD_PAD_B) {
            ftBf_ChangeState(gobj, p->charge2, 0.0f);
            fv->charge = 0;
        }
        fp->take_dmg_cb = ftBf_SpecialN_OnTakeDamage2;
        if (fp->cur_anim_frame == 120.0f) {
            Fighter_TakeDamage_8006CC7C(fp, 2.0f);
            fv->charge += 2;
        }
        if (fp->cur_anim_frame < 119.0f) {
            goto release;
        }
        goto late;
    }

before_120:
    if (!(fp->cur_anim_frame < 119.0f)) {
        return;
    }
release:
    if (fv->charge <= 1) {
        return;
    }
    ftBf_ChangeState(gobj, p->charge2, 0.0f);
    fv->charge = 0;
late:
    if (!(fp->cur_anim_frame >= 145.0f)) {
        return;
    }
    if (fp->input.lstick[0].x >= 0.25f || fp->input.lstick[0].x <= -0.25f ||
        (fp->input.pressed_buttons & ftBf_CancelButtons))
    {
        ftBf_ChangeState(gobj, p->cancel, 0.0f);
    }
    /* +06F0, +082C: the check is called a second time when the first one entered the jump */
    if (p->jump(gobj)) {
        p->jump(gobj);
    }
}

/* +04B0 "SpecialN_Start_IASACB": interrupt callback of state 347, where Captain Falcon has the
 * empty ftCa_SpecialN_IASA. */
void ftBf_SpecialN_IASA(HSD_GObj* gobj)
{
    static const ftBf_SpecialNParams params = {
        60.0f,
        ftCo_MS_Wait,
        ftCa_MS_SpecialN,
        ftBf_MS_SpecialNCharge0,
        ftBf_MS_SpecialNCharge1,
        ftBf_MS_SpecialNCharge2,
        ftCo_Jump_CheckInput,
    };

    ftBf_SpecialN_Interrupt(gobj, &params);
}

/* +088C "SpecialAirN_Start_IASACB": interrupt callback of state 348. The first window runs to
 * frame 119 here (60 on the ground); the other frame numbers are the same. */
void ftBf_SpecialAirN_IASA(HSD_GObj* gobj)
{
    static const ftBf_SpecialNParams params = {
        119.0f,
        ftCo_MS_Fall,
        ftCa_MS_SpecialAirN,
        ftBf_MS_SpecialAirNCharge0,
        ftBf_MS_SpecialAirNCharge1,
        ftBf_MS_SpecialAirNCharge2,
        ftCo_800CB870,
    };

    ftBf_SpecialN_Interrupt(gobj, &params);
}

/* The body of +0C68 and +0CD0: past the given frame of an aerial side special, a ledge in reach
 * (ftCo_800C3A14, the air catch's ledge test) that no other fighter holds is grabbed. */
static inline void ftBf_SpecialAirS_CheckLedge(HSD_GObj* gobj, float after)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!(fp->cur_anim_frame > after)) {
        return;
    }
    if (ftCo_800C3A14(gobj) && ft_80082E3C(gobj) == NULL) {
        ftCliffCommon_80081370(gobj);
    }
}

/* +0C68 "SpecialAirS_Start_IASACB": state 351, where Captain Falcon has the empty
 * ftCa_SpecialAirSStart_IASA. */
void ftBf_SpecialAirSStart_IASA(HSD_GObj* gobj)
{
    ftBf_SpecialAirS_CheckLedge(gobj, 39.0f);
}

/* +0CD0 "SpecialAirS_Hit_IASACB": state 352, in place of the empty ftCa_SpecialAirS_IASA. */
void ftBf_SpecialAirS_IASA(HSD_GObj* gobj)
{
    ftBf_SpecialAirS_CheckLedge(gobj, 25.0f);
}

/* +10BC "SpecialLw_Hit": the down special connected: state 363 (Captain Falcon's unused
 * SpecialHiThrow1 row, here with the collision callback below). */
static void ftBf_SpecialLw_Hit(HSD_GObj* gobj)
{
    ftBf_ChangeState(gobj, ftCa_MS_SpecialHiThrow1, 0.0f);
}

/* +0D38 "SpecialLw_Start_IASACB": interrupt callback of states 357 and 359 (Captain Falcon has
 * none): it only keeps the hit callback installed. */
void ftBf_SpecialLw_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->deal_dmg_cb = ftBf_SpecialLw_Hit;
}

/* +0D4C "Rebound_Coll": collision of state 363, in place of ftCa_SpecialHiThrow1_Coll. On the
 * floor: no ground speed, Wait. */
void ftBf_SpecialHiThrow1_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        fp->gr_vel = 0.0f;
        ftBf_ChangeState(gobj, ftCo_MS_Wait, 0.0f);
    }
}

/* +0DC0 "SpecialN_Charge_AnimCB": animation callback of the grounded charge states 364, 366 and
 * 368. Speed from the animation's root motion; Wait at the end. */
void ftBf_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->take_dmg_cb = ftBf_SpecialN_OnTakeDamage;
    ft_80085134(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +0E18 "SpecialN_Charge_CollCB": collision of states 364, 366 and 368. */
void ftBf_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ft_800827A0(gobj);
    }
}

/* +0E58 "SpecialAirN_Charge_AnimCB": animation callback of the aerial charge states 365, 367 and
 * 369. Frame 12 sets the vertical speed to 1.25; Fall at the end. */
void ftBf_SpecialAirNCharge_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->take_dmg_cb = ftBf_SpecialN_OnTakeDamage;
    if (fp->cur_anim_frame == 12.0f) {
        fp->self_vel.y = 1.25f;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* The body of +0ECC and +0F44: an aerial charge state reached the floor and goes on as a
 * grounded one at the same frame. */
static inline void ftBf_SpecialAirNCharge_Land(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj) == 1) {
        ftCommon_8007D7FC(fp);
        ftBf_ChangeState(gobj, msid, fp->cur_anim_frame);
    }
}

/* +0ECC "SpecialAirN_Charge_CollCB": collision of state 365; lands in 364. */
void ftBf_SpecialAirNCharge2_Coll(HSD_GObj* gobj)
{
    ftBf_SpecialAirNCharge_Land(gobj, ftBf_MS_SpecialNCharge2);
}

/* +0F44 "SpecialAirN_Charge2_CollCB": collision of states 367 and 369; both land in 366 (369
 * does not land in its own grounded state 368: the file's table gives it this callback). */
void ftBf_SpecialAirNCharge0_Coll(HSD_GObj* gobj)
{
    ftBf_SpecialAirNCharge_Land(gobj, ftBf_MS_SpecialNCharge0);
}
